#include "module_loader_internal.h"

#include <zues/log.h>

#include <cstdio>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    using LibHandle = HMODULE;
    static LibHandle lib_open (const char* path)          { return ::LoadLibraryA(path); }
    static void*     lib_sym  (LibHandle h, const char* n){ return reinterpret_cast<void*>(::GetProcAddress(h, n)); }
    static void      lib_close(LibHandle h)               { ::FreeLibrary(h); }
    static const char* lib_ext() { return ".dll"; }
#else
    #include <dlfcn.h>
    using LibHandle = void*;
    static LibHandle lib_open (const char* path)           { return ::dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    static void*     lib_sym  (LibHandle h, const char* n) { return ::dlsym(h, n); }
    static void      lib_close(LibHandle h)                { ::dlclose(h); }
    static const char* lib_ext() { return ".so"; }
#endif

namespace Engine::internal {

namespace {
    bool is_module_filename(const std::filesystem::path& p) {
        auto ext = p.extension().string();
        if (ext != lib_ext()) return false;
        auto stem = p.stem().string();
        if (stem.rfind("zues_", 0) != 0) return false;   // must start with zues_
        if (stem == "zues_core") return false;           // don't reload ourselves
        return true;
    }

    void log_fmt(LogLevel level, const char* fmt, ...) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        log_write(level, "core", buf);
    }
}

ModuleRegistry::~ModuleRegistry() {
    // Defensive: if unload_all wasn't called, at least close libraries.
    for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
        if (it->library_handle) lib_close(static_cast<LibHandle>(it->library_handle));
    }
}

void ModuleRegistry::load_all(const char* dir, ModuleContext* ctx) {
    namespace fs = std::filesystem;

    if (!dir || !*dir) dir = ".";

    std::error_code ec;
    auto iter = fs::directory_iterator(dir, ec);
    if (ec) {
        log_fmt(LogLevel::Warn, "Module dir unreadable: %s (%s)", dir, ec.message().c_str());
        return;
    }

    for (const auto& entry : iter) {
        if (!entry.is_regular_file(ec)) continue;
        if (!is_module_filename(entry.path())) continue;

        const auto path_str = entry.path().string();

        LibHandle lib = lib_open(path_str.c_str());
        if (!lib) {
            log_fmt(LogLevel::Error, "LoadLibrary failed: %s", path_str.c_str());
            continue;
        }

        auto entry_fn = reinterpret_cast<ZuesModuleEntryFn>(lib_sym(lib, "zues_module_entry"));
        if (!entry_fn) {
            // Common case: someone dropped a non-engine DLL into the bin
            // dir (Lync compiler plugins, project DLLs, third-party libs).
            // Trace-only — module discovery is supposed to be silent for
            // non-modules, only loud when an actual module fails to load.
            log_fmt(LogLevel::Trace, "skipping non-module DLL: %s", path_str.c_str());
            lib_close(lib);
            continue;
        }

        const ModuleInfo* info = entry_fn();
        if (!info) {
            log_fmt(LogLevel::Error, "zues_module_entry returned null: %s", path_str.c_str());
            lib_close(lib);
            continue;
        }

        if (info->abi_version != ZUES_MODULE_ABI_VERSION) {
            log_fmt(LogLevel::Error,
                    "ABI mismatch for module %s: got %u, expected %u",
                    info->name ? info->name : "?",
                    info->abi_version,
                    ZUES_MODULE_ABI_VERSION);
            lib_close(lib);
            continue;
        }

        log_fmt(LogLevel::Info, "Loading module %s %s",
                info->name    ? info->name    : "?",
                info->version ? info->version : "?");

        if (info->on_load) info->on_load(ctx);

        modules.push_back({
            info->name ? info->name : "",
            lib,
            info,
        });
    }
}

void ModuleRegistry::signal_ready(ModuleContext* ctx) {
    for (auto& m : modules) {
        if (m.info && m.info->on_ready) m.info->on_ready(ctx);
    }
}

void ModuleRegistry::unload_all(ModuleContext* ctx) {
    for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
        if (it->info && it->info->on_unload) it->info->on_unload(ctx);
        if (it->library_handle) lib_close(static_cast<LibHandle>(it->library_handle));
        it->library_handle = nullptr;
    }
    modules.clear();
}

}  // namespace Engine::internal
