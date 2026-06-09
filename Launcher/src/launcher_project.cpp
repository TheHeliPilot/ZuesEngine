#include "launcher.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace Engine::launcher {

namespace fs = std::filesystem;

// Write a small text file; returns false on failure.
static bool write_file(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

// Path -> backslashes, suitable for batch files. Despite the misleading
// name, this swaps `/` for `\\` because cmd.exe is picky about forward
// slashes when used as path separators in some contexts.
static std::string fwd(const fs::path& p) {
    std::string s = p.generic_string();
    // batch is happiest with backslashes; substitute back
    for (auto& c : s) if (c == '/') c = '\\';
    return s;
}

// Path -> forward-slash string, safe to drop into JSON literals as-is.
// Backslashes inside JSON strings are escape sequences (`\b` is backspace,
// `\L` is invalid, etc.), so writing raw Windows paths into a JSON file
// produces a corrupt project descriptor that the editor's loader rejects
// with a parse error. The generic_string() call already returns forward
// slashes — we just don't undo it.
static std::string json_path(const fs::path& p) {
    return p.generic_string();
}

// Locate the launcher.exe directory at runtime so the generated build.bat
// is portable: when shipped in dist/ the lync toolchain lives under
// `<launcher_dir>/lync/`; in the dev build tree we fall back to sibling
// LyncLang + ProjectAPI sources.
struct ToolchainPaths {
    fs::path lync_exe;
    fs::path plugin_dll;
    fs::path prelude_lync;
    fs::path include_dir;
    bool ok = false;
    std::string missing;  // diagnostic for the first thing not found
};

static fs::path launcher_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

// Try a candidate root: returns true if all four artefacts exist beneath it.
static bool try_layout(const fs::path& lync_exe,
                        const fs::path& plugin_dll,
                        const fs::path& prelude_lync,
                        const fs::path& include_dir,
                        ToolchainPaths& out) {
    if (!fs::exists(lync_exe))     { out.missing = lync_exe.string();     return false; }
    if (!fs::exists(plugin_dll))   { out.missing = plugin_dll.string();   return false; }
    if (!fs::exists(prelude_lync)) { out.missing = prelude_lync.string(); return false; }
    if (!fs::exists(include_dir))  { out.missing = include_dir.string();  return false; }
    out.lync_exe     = lync_exe;
    out.plugin_dll   = plugin_dll;
    out.prelude_lync = prelude_lync;
    out.include_dir  = include_dir;
    out.ok = true;
    return true;
}

static ToolchainPaths discover_toolchain() {
    ToolchainPaths out;
    const fs::path here = launcher_dir();

    // 1. Deploy layout: <launcher_dir>/lync/{lync.exe, zues_lync_plugin.dll,
    //                                        zues_api.lync, include/}
    if (try_layout(here / "lync" / "lync.exe",
                   here / "lync" / "zues_lync_plugin.dll",
                   here / "lync" / "zues_api.lync",
                   here / "lync" / "include",
                   out)) return out;

    // 2. Dev layout: launcher.exe is in build/<cfg>/bin/, alongside the
    //    plugin DLL. Source files live in the engine + sibling LyncLang.
    //    here = .../ZuesEngine/build/<cfg>/bin
    const fs::path engine_root = here.parent_path().parent_path().parent_path();
    const fs::path repo_root   = engine_root.parent_path();
    if (try_layout(repo_root   / "LyncLang" / "build_win" / "lync.exe",
                   here        / "zues_lync_plugin.dll",
                   engine_root / "LyncPlugin" / "zues_api.lync",
                   engine_root / "ProjectAPI" / "include",
                   out)) return out;

    // 3. Env-var override (last resort): ZUES_HOME points at a deploy-style
    //    directory layout. Useful when shipping ad-hoc builds.
    if (const char* zh = std::getenv("ZUES_HOME")) {
        const fs::path root = zh;
        if (try_layout(root / "lync" / "lync.exe",
                       root / "lync" / "zues_lync_plugin.dll",
                       root / "lync" / "zues_api.lync",
                       root / "lync" / "include",
                       out)) return out;
    }

    return out;  // ok == false; out.missing carries the last-tried path
}

bool create_lync_skeleton(const fs::path& full, const std::string& name) {
    std::error_code ec;
    fs::create_directories(full, ec);
    if (ec) return false;
    fs::create_directories(full / "assets",  ec);
    fs::create_directories(full / "scenes",  ec);
    fs::create_directories(full / "src",     ec);
    fs::create_directories(full / "build",   ec);

    // Discover toolchain first - we need its paths in the .zuesproject so the
    // editor's auto-compile pipeline can run without per-machine config.
    ToolchainPaths tc = discover_toolchain();
    if (!tc.ok) {
        std::fprintf(stderr,
            "[launcher] cannot locate Zues toolchain - missing: %s\n"
            "          set ZUES_HOME to a directory containing lync/lync.exe + lync/zues_lync_plugin.dll\n",
            tc.missing.c_str());
        return false;
    }

    // .zuesproject - declares lync_main + toolchain paths. The editor watches
    // any .lync file under <project>/src and re-runs the compiler when one
    // changes; on file-set changes it regenerates src/_zues_main.lync as the
    // actual entry. The user just edits regular .lync files - no manual
    // build.bat invocation needed.
    char zp[2048];
    std::snprintf(zp, sizeof(zp),
        "{\n"
        "    \"name\": \"%s\",\n"
        "    \"default_world\": \"scenes/main.zworld\",\n"
        "    \"source_dir\": \"src\",\n"
        "    \"build\": {\n"
        "        \"dll_path\":      \"build/%s.dll\",\n"
        "        \"lync_main\":     \"src/Game.lync\",\n"
        "        \"lync_compiler\": \"%s\",\n"
        "        \"lync_plugin\":   \"%s\",\n"
        "        \"lync_include\":  \"%s\",\n"
        "        \"lync_prelude\":  \"%s\"\n"
        "    }\n"
        "}\n",
        name.c_str(), name.c_str(),
        json_path(tc.lync_exe).c_str(),
        json_path(tc.plugin_dll).c_str(),
        json_path(tc.include_dir).c_str(),
        json_path(tc.prelude_lync).c_str());
    if (!write_file(full / (name + ".zuesproject"), zp)) return false;

    // src/<name>.lync - user entry point. Picked up by the editor's manifest
    // auto-regen alongside any other .lync file in src/.
    // Empty starter — no boilerplate components / hooks. Lync's plugin
    // emits a no-op project entry even with zero attributes, so the
    // resulting DLL is loadable from frame one. Users add their own
    // [Component] / [OnLoad] / [System] when they're ready, and can
    // delete this file outright if they want to start from scratch
    // (the editor regenerates _zues_main.lync to whatever .lync files
    // remain under src/).
    char lync[256];
    std::snprintf(lync, sizeof(lync),
        "// %s -- starter Lync source. Add [Component]s, [System]s and\n"
        "// [OnLoad] hooks here, or split them across multiple .lync files\n"
        "// under src/. Delete this file freely if you don't need it.\n",
        name.c_str());
    if (!write_file(full / "src" / "Game.lync", lync)) return false;

    // build.bat - escape hatch for command-line builds outside the editor
    // (e.g. CI). The editor itself runs lync.exe directly using the toolchain
    // paths in .zuesproject, so this file is purely optional.
    char bat[1024];
    std::snprintf(bat, sizeof(bat),
        "@echo off\n"
        "setlocal\n"
        "rem Manual build - normally the editor's auto-compile handles this.\n"
        "if not exist \"%%~dp0build\" mkdir \"%%~dp0build\"\n"
        "\"%s\" \"%%~dp0src\\Game.lync\" --target=dll ^\n"
        "    --plugin=\"%s\" ^\n"
        "    --prelude=\"%s\" ^\n"
        "    --include=\"%s\" ^\n"
        "    -o \"%%~dp0build\\%s.dll\"\n"
        "if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )\n"
        "echo Build succeeded.\n",
        fwd(tc.lync_exe).c_str(),
        fwd(tc.plugin_dll).c_str(),
        fwd(tc.prelude_lync).c_str(),
        fwd(tc.include_dir).c_str(),
        name.c_str());
    if (!write_file(full / "build.bat", bat)) return false;

    // .gitkeep sentinels
    write_file(full / "assets" / ".gitkeep", "");
    write_file(full / "scenes" / ".gitkeep", "");

    // .gitignore
    if (!write_file(full / ".gitignore",
        "build/\n"
        "*.exe\n"
        "*.dll\n"
        ".zues/cache/\n")) return false;

    // README.md
    char readme[256];
    std::snprintf(readme, sizeof(readme),
        "# %s\n\n"
        "A Zues Engine / Lync project.\n\n"
        "Run `build.bat` to compile, then open `%s.zuesproject` in the launcher.\n",
        name.c_str(), name.c_str());
    if (!write_file(full / "README.md", readme)) return false;

    return true;
}

}  // namespace Engine::launcher
