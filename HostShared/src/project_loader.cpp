#include <zues/host/project_loader.h>

#include <zues/host/host_api.h>
#include <zues/host/path_util.h>

#include <zues/log.h>

#include <cstdio>
#include <cstdarg>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    using LibHandle = HMODULE;
    static LibHandle lib_open (const char* path)           { return ::LoadLibraryA(path); }
    static void*     lib_sym  (LibHandle h, const char* n) { return reinterpret_cast<void*>(::GetProcAddress(h, n)); }
    static void      lib_close(LibHandle h)                { ::FreeLibrary(h); }
#else
    #include <dlfcn.h>
    using LibHandle = void*;
    static LibHandle lib_open (const char* path)           { return ::dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    static void*     lib_sym  (LibHandle h, const char* n) { return ::dlsym(h, n); }
    static void      lib_close(LibHandle h)                { ::dlclose(h); }
#endif

namespace Engine::host {

namespace {
    void log_fmt(LogLevel lvl, const char* fmt, ...) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        log_write(lvl, "editor.project", buf);
    }

    // Per-session PID stamp keeps shadow filenames unique across concurrent
    // editor instances + zombie processes that haven't released their handles
    // yet. Without this, a stale `foo.loaded.dll` locked by a previous run
    // blocks every reload silently.
#if defined(_WIN32)
    inline unsigned long session_id() { return ::GetCurrentProcessId(); }
#else
    #include <unistd.h>
    inline unsigned long session_id() { return (unsigned long)::getpid(); }
#endif

    // Build the shadow path next to the source:
    //   foo.dll -> foo.loaded.<pid>.dll
    // Same dir so any side-load resources resolve identically. The PID stamp
    // means each editor process owns its own shadow file — multiple editors
    // can run side-by-side and a zombie holding the old shadow doesn't
    // block a new instance.
    std::filesystem::path shadow_for(const std::filesystem::path& src) {
        auto stem = src.stem().string();
        auto ext  = src.extension().string();
        auto out  = src;
        char tag[32];
        std::snprintf(tag, sizeof(tag), ".loaded.%lu", session_id());
        out.replace_filename(stem + tag + ext);
        return out;
    }

    // Sweep stale shadow files left over by previous editor sessions whose
    // process is no longer running. Cheap; runs once at load time.
    void cleanup_stale_shadows(const std::filesystem::path& src) {
        std::error_code ec;
        const auto dir = src.parent_path();
        if (dir.empty() || !std::filesystem::exists(dir, ec)) return;
        const auto stem  = src.stem().string();
        const auto ext   = src.extension().string();
        const auto needle = stem + ".loaded.";
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const auto fn = e.path().filename().string();
            if (fn.rfind(needle, 0) != 0)             continue;   // doesn't match prefix
            if (e.path().extension().string() != ext) continue;   // wrong ext
            // Try to remove. If the file is locked (another editor instance
            // owns it), removal will fail silently and we leave it alone.
            std::filesystem::remove(e.path(), ec);
        }
    }
}

bool ProjectDllLoader::load(const std::filesystem::path& dll_path,
                            const ZuesHostApi* host,
                            ZuesEngine* engine) {
    if (m_api) {
        log_fmt(LogLevel::Warn, "project DLL already loaded; call unload() first");
        return false;
    }
    if (!host || !engine) {
        log_fmt(LogLevel::Error, "project loader: host/engine missing");
        return false;
    }

    // Remember what we're TRYING to load BEFORE the attempt completes.
    // Any failure path below (file missing, ABI mismatch, entry symbol
    // missing) leaves these populated so reload() / is_source_dirty()
    // can find their way back to the same DLL once the user rebuilds.
    // Only m_api stays null on failure -- that's the "actually loaded"
    // signal the rest of the engine consults.
    m_source_path = Engine::host::path_str(dll_path);
    m_host        = host;
    m_engine      = engine;

    std::error_code ec;
    if (!std::filesystem::exists(dll_path, ec)) {
        log_fmt(LogLevel::Warn, "project DLL not found: %s", Engine::host::path_str(dll_path).c_str());
        return false;
    }

    // Capture mtime BEFORE the copy so a rebuild that lands during the
    // load-window doesn't fool is_source_dirty() into thinking we already
    // loaded the new build.
    const auto src_mtime = std::filesystem::last_write_time(dll_path, ec);
    if (ec) {
        log_fmt(LogLevel::Error, "project DLL stat failed: %s", Engine::host::path_str(dll_path).c_str());
        return false;
    }

    // Sweep dead shadow files from previous editor sessions, then shadow-copy
    // the live DLL into a name unique to THIS process. Each session owns its
    // own shadow so two editors can run side-by-side and a zombie's lock
    // doesn't block a new instance.
    cleanup_stale_shadows(dll_path);
    const auto shadow = shadow_for(dll_path);
    std::filesystem::copy_file(dll_path, shadow,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        log_fmt(LogLevel::Error, "project DLL shadow copy failed: %s -> %s (%s)",
                Engine::host::path_str(dll_path).c_str(), shadow.string().c_str(),
                ec.message().c_str());
        return false;
    }

    const auto src_str    = Engine::host::path_str(dll_path);
    const auto shadow_str = shadow.string();
    LibHandle h = lib_open(shadow_str.c_str());
    if (!h) {
        log_fmt(LogLevel::Error, "project DLL LoadLibrary failed: %s", shadow_str.c_str());
        std::filesystem::remove(shadow, ec);
        return false;
    }

    using EntryFn = const ZuesProjectApi* (*)();
    auto entry = reinterpret_cast<EntryFn>(lib_sym(h, "zues_project_entry"));
    if (!entry) {
        log_fmt(LogLevel::Error,
                "project DLL missing zues_project_entry: %s", src_str.c_str());
        lib_close(h);
        std::filesystem::remove(shadow, ec);
        return false;
    }

    const ZuesProjectApi* api = entry();
    if (!api) {
        log_fmt(LogLevel::Error, "zues_project_entry returned null: %s", src_str.c_str());
        lib_close(h);
        std::filesystem::remove(shadow, ec);
        return false;
    }
    m_last_observed_abi = api->abi_version;
    m_last_expected_abi = ZUES_PROJECT_API_VERSION;
    if (api->abi_version != ZUES_PROJECT_API_VERSION) {
        log_fmt(LogLevel::Error,
                "project ABI mismatch in %s: got %u, expected %u",
                src_str.c_str(), api->abi_version, ZUES_PROJECT_API_VERSION);
        lib_close(h);
        std::filesystem::remove(shadow, ec);
        // Stash mtime so is_source_dirty() picks up the rebuild even
        // though we never finished the load. (m_source_path / m_host /
        // m_engine were stashed at the top of load() above.)
        m_load_mtime = src_mtime;
        return false;
    }

    m_handle      = h;
    m_api         = api;
    m_host        = host;
    m_engine      = engine;
    m_source_path = src_str;
    m_shadow_path = shadow_str;
    m_load_mtime  = src_mtime;

    log_fmt(LogLevel::Info, "loading project DLL: %s", src_str.c_str());
    if (api->on_load) api->on_load(engine, host);
    return true;
}

void ProjectDllLoader::tick(float dt) {
    if (!m_api || !m_api->on_update) return;
    m_api->on_update(m_engine, dt);
}

void ProjectDllLoader::unload() {
    if (!m_api) return;

    // CRITICAL ORDER: pull every fn-pointer-into-the-DLL out of any host
    // table BEFORE on_unload + FreeLibrary. Anything that fires after
    // FreeLibrary calls into freed memory -- on Windows that's a silent
    // process termination with no dialog or stderr.
    //   - systems:  iterate_query thunks
    //   - timers:   SetTimeout / SetInterval callbacks
    unregister_project_systems();
    clear_timers();

    if (m_api->on_unload) m_api->on_unload(m_engine);
    if (m_handle) lib_close(static_cast<LibHandle>(m_handle));

    log_fmt(LogLevel::Info, "unloaded project DLL: %s", m_source_path.c_str());

    // Remove the shadow. Failure (e.g. AV briefly holds the file) is not
    // fatal — the next load will overwrite it.
    if (!m_shadow_path.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_shadow_path, ec);
    }

    m_handle      = nullptr;
    m_api         = nullptr;
    m_host        = nullptr;
    m_engine      = nullptr;
    m_source_path.clear();
    m_shadow_path.clear();
    m_load_mtime  = {};
}

bool ProjectDllLoader::reload() {
    // The "never successfully loaded" case isn't an error any more --
    // load() now stashes m_source_path on every attempt so we can
    // retry the same DLL after the user fixes whatever broke the
    // first load (typical case: ABI mismatch -> rebuild). Bail only
    // when we have no path at all to retry against.
    if (m_source_path.empty()) {
        log_fmt(LogLevel::Warn, "reload: nothing to reload");
        return false;
    }
    const auto host = m_host;
    const auto eng  = m_engine;
    const std::filesystem::path path = m_source_path;

    log_fmt(LogLevel::Info, "reloading project DLL: %s",
            Engine::host::path_str(path).c_str());
    if (m_api) unload();
    return load(path, host, eng);
}

bool ProjectDllLoader::is_source_dirty() const {
    // Watch the file even when the LAST load failed -- mtime will
    // change when the user rebuilds, and that's our cue to retry.
    if (m_source_path.empty()) return false;
    std::error_code ec;
    auto t = std::filesystem::last_write_time(m_source_path, ec);
    if (ec) return false;       // file vanished or unreadable
    return t != m_load_mtime;
}

}  // namespace Engine::host
