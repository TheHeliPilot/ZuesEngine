// Build → Export. One-button "ship a self-contained game folder" flow.
//
// Output layout (dest = <project>/dist/<Name>/ by default):
//   <Name>.exe                  — copy of zues_runtime.exe, renamed
//   zues_core.dll               — engine module DLLs (next to the editor)
//   zues_renderer_gl.dll
//   zues_window_glfw.dll
//   zues_physics_box2d.dll
//   glfw3.dll
//   <project>.dll               — project DLL (build.dll_path)
//   <Name>.zuesproject          — copy of the loaded project file
//   assets/...                  — recursive copy of project assets
//   worlds/...                  — only if project.worlds_dir lives outside assets/
//
// The runtime, when launched with no args, scans its own dir for *.zuesproject
// and loads the first one it finds. So the user can zip this folder and
// double-click the .exe on another machine.
//
// What we do NOT ship:
//   - The Lync compiler / plugin / prelude. The exported game runs the
//     already-compiled project DLL; it never recompiles at runtime.
//   - Project source (.lync, .h, .cpp). Pure dead weight in a shipped build.
//   - The editor exe / its panels assets (custom-icons, fonts).
//   - .zues/cache, build/ — these are scratch dirs.
//
// Pre-flight: the project DLL must exist on disk. We don't kick a Lync
// rebuild from here — the auto-watcher already did that on the last save.

#include "editor.h"

#include <zues/log.h>
#include <zues/project.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace Engine::editor {

namespace fs = std::filesystem;

namespace {

// Engine module DLLs that must sit next to the runtime exe at runtime.
// These are the same ones the editor itself loads; resolved via state.exe_dir.
constexpr std::array<const char*, 5> kEngineDlls = {
    "zues_core.dll",
    "zues_renderer_gl.dll",
    "zues_window_glfw.dll",
    "zues_physics_box2d.dll",
    "glfw3.dll",
};

// Recursively copy `src` into `dst`, creating directories as needed. Any
// file copy failure aborts the walk and logs the offending path. Returns
// false on the first error so the caller can toast a useful message.
bool copy_dir_recursive(const fs::path& src, const fs::path& dst,
                         std::string* err_out) {
    std::error_code ec;
    if (!fs::exists(src, ec) || !fs::is_directory(src, ec)) {
        if (err_out) *err_out = "missing source dir: " + path_str(src);
        return false;
    }
    fs::create_directories(dst, ec);
    if (ec) {
        if (err_out) *err_out = "mkdir failed: " + path_str(dst);
        return false;
    }
    for (auto it = fs::recursive_directory_iterator(src, ec);
         !ec && it != fs::recursive_directory_iterator{}; ++it) {
        const auto& p   = it->path();
        const auto rel  = fs::relative(p, src, ec);
        const auto into = dst / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(into, ec);
            if (ec) {
                if (err_out) *err_out = "mkdir failed: " + path_str(into);
                return false;
            }
        } else if (it->is_regular_file(ec)) {
            fs::copy_file(p, into, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                if (err_out) *err_out = "copy failed: " + path_str(p);
                return false;
            }
        }
        // Skip symlinks / specials silently.
    }
    return !ec;
}

// Copy a single file with overwrite. err_out gets a human-readable path on
// failure so the toast surfaces *which* file the export choked on.
bool copy_file_overwrite(const fs::path& src, const fs::path& dst,
                          std::string* err_out) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (err_out) *err_out = "copy failed: " + path_str(src);
        return false;
    }
    return true;
}

// Find the .zuesproject file at the project root. The editor doesn't track
// the original path on EditorState, so we scan. There should be exactly one;
// if there are multiples we pick the one whose stem matches project_name,
// else the first hit.
fs::path find_zuesproject(const fs::path& project_dir,
                           const std::string& project_name) {
    std::error_code ec;
    fs::path first;
    for (auto it = fs::directory_iterator(project_dir, ec);
         !ec && it != fs::directory_iterator{}; ++it) {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".zuesproject") continue;
        if (first.empty()) first = it->path();
        if (it->path().stem().string() == project_name) return it->path();
    }
    return first;
}

// Resolve the binaries directory for the requested config. The editor may
// itself be running from a Debug or Release build dir; we don't assume the
// two are the same and we don't assume one is the running config.
//
// Layout convention: `<repo>/build/<config-name>/bin`. If the editor's
// exe_dir matches that pattern we walk one level up and try sibling dirs.
// Override with the env var ZUES_DEBUG_BIN_DIR / ZUES_RELEASE_BIN_DIR if
// the user keeps builds elsewhere.
fs::path resolve_bin_dir(const fs::path& editor_dir, ExportKind kind) {
    const char* env_name = (kind == ExportKind::Release)
        ? "ZUES_RELEASE_BIN_DIR" : "ZUES_DEBUG_BIN_DIR";
    if (const char* v = std::getenv(env_name); v && *v) {
        fs::path p(v);
        std::error_code ec;
        if (fs::exists(p / "zues_runtime.exe", ec)) return p;
    }

    // Candidates by convention -- tried in order, first hit wins. Names
    // match the build dirs we set up (clang-reflection = Debug,
    // release-clang = Release) plus a few common alternates.
    static const char* debug_candidates[]   = {
        "clang-reflection", "debug", "Debug", "debug2"
    };
    static const char* release_candidates[] = {
        "release-clang", "release", "Release", "RelWithDebInfo"
    };
    const char* const* names = (kind == ExportKind::Release)
        ? release_candidates : debug_candidates;
    const std::size_t  count = (kind == ExportKind::Release)
        ? std::size(release_candidates) : std::size(debug_candidates);

    // editor_dir = <repo>/build/<config>/bin -- back up two levels for the
    // shared "build" parent, then try each candidate sibling.
    std::error_code ec;
    fs::path build_parent = editor_dir.parent_path().parent_path();
    if (fs::exists(build_parent, ec) && fs::is_directory(build_parent, ec)) {
        for (std::size_t i = 0; i < count; ++i) {
            const fs::path c = build_parent / names[i] / "bin";
            if (fs::exists(c / "zues_runtime.exe", ec)) return c;
        }
    }
    // Fallback: editor_dir itself, IF the kind matches whatever was built
    // there. We can't tell from a path alone, so just return it -- the
    // export pre-flight will catch a mismatch (e.g. Release flag asked
    // for but only a Debug runtime sits next door).
    return editor_dir;
}

const char* kind_label(ExportKind k) {
    return k == ExportKind::Release ? "Release" : "Debug";
}

}  // namespace

bool build_export(EditorState& s, ExportKind kind) {
    if (!s.project_loaded || s.project_dir.empty()) {
        show_toast(s, "Export: no project loaded", 3.0f, true);
        return false;
    }
    if (s.is_playing) {
        show_toast(s, "Export: stop play mode first", 3.0f, true);
        return false;
    }

    const fs::path project_dir(s.project_dir);
    const fs::path editor_dir = s.exe_dir;
    const fs::path bin_dir    = resolve_bin_dir(editor_dir, kind);

    // Re-load the .zuesproject so we get the authoritative dll_path /
    // worlds_dir without threading those onto EditorState. Cheap — it's a
    // tiny JSON file.
    const fs::path zproj_src = find_zuesproject(project_dir, s.project_name);
    if (zproj_src.empty()) {
        show_toast(s, "Export: .zuesproject not found", 3.0f, true);
        return false;
    }
    Project p;
    if (load_project(zproj_src.string().c_str(), p) != Result::Ok) {
        show_toast(s, "Export: .zuesproject parse failed", 3.0f, true);
        return false;
    }

    // Pre-flight the inputs that the runtime can't be shipped without.
    const fs::path runtime_src = bin_dir / "zues_runtime.exe";
    std::error_code ec;
    if (!fs::exists(runtime_src, ec)) {
        const std::string msg = std::string("Export ") + kind_label(kind) +
            ": zues_runtime.exe not found at " + path_str(bin_dir);
        show_toast(s, msg.c_str(), 4.5f, true);
        return false;
    }
    if (p.build.dll_path.empty()) {
        show_toast(s, "Export: project has no build.dll_path", 3.5f, true);
        return false;
    }
    const fs::path proj_dll_src = project_dir / p.build.dll_path;
    if (!fs::exists(proj_dll_src, ec)) {
        show_toast(s, "Export: project DLL missing — rebuild first",
                   4.0f, true);
        return false;
    }

    // Destination: <project>/dist/<Name>/ . Wipe contents first so stale
    // files (renamed worlds, removed assets) don't stick around. We don't
    // remove the directory itself — keeps the user's file explorer happy.
    const std::string safe_name =
        s.project_name.empty() ? std::string("Game") : s.project_name;
    // Each config gets its own dist dir so iterating Debug doesn't
    // clobber a Release build (or vice versa). Suffix with the label,
    // not the raw config name, so it reads naturally.
    const fs::path dest = project_dir / "dist" /
        (safe_name + "-" + kind_label(kind));
    fs::create_directories(dest, ec);
    for (auto it = fs::directory_iterator(dest, ec);
         !ec && it != fs::directory_iterator{}; ++it) {
        std::error_code rmec;
        fs::remove_all(it->path(), rmec);
        // Don't fail the whole export over one undeletable file (e.g. the
        // user has the old exe open in File Explorer). The subsequent copy
        // overwrites with copy_options::overwrite_existing anyway.
    }

    std::string err;

    // 1. Runtime exe, renamed to <Name>.exe. Stable user-facing name; lets
    //    Steam / itch shortcuts not embed engine internals.
    const fs::path exe_dst = dest / (safe_name + ".exe");
    if (!copy_file_overwrite(runtime_src, exe_dst, &err)) {
        show_toast(s, ("Export: " + err).c_str(), 4.0f, true);
        return false;
    }

    // 2. Engine module DLLs. All five must be present — we error rather
    //    than ship a half-broken bundle.
    for (const char* dll : kEngineDlls) {
        const fs::path src = bin_dir / dll;
        if (!fs::exists(src, ec)) {
            show_toast(s,
                ("Export: missing " + std::string(dll)).c_str(),
                4.0f, true);
            return false;
        }
        if (!copy_file_overwrite(src, dest / dll, &err)) {
            show_toast(s, ("Export: " + err).c_str(), 4.0f, true);
            return false;
        }
    }

    // 3. Project DLL. Mirror the relative path the .zuesproject points at,
    //    so the runtime's load_project + project DLL lookup just works.
    const fs::path proj_dll_dst = dest / p.build.dll_path;
    if (!copy_file_overwrite(proj_dll_src, proj_dll_dst, &err)) {
        show_toast(s, ("Export: " + err).c_str(), 4.0f, true);
        return false;
    }

    // 4. Assets directory. Always under <project>/assets per project
    //    skeleton convention; we don't expose an override yet.
    const fs::path assets_src = project_dir / s.assets_root_relative;
    if (fs::exists(assets_src, ec)) {
        if (!copy_dir_recursive(assets_src, dest / s.assets_root_relative,
                                 &err)) {
            show_toast(s, ("Export: " + err).c_str(), 4.0f, true);
            return false;
        }
    }

    // 4a. Ensure a default font lands in <dest>/assets/fonts/. The
    //     ui_render_system searches assets/fonts/default.ttf and then
    //     assets/fonts/Exo2-VariableFont_wght.ttf; if neither exists,
    //     Text components render as missing-glyph rects in the shipped
    //     game. Copy the engine's bundled Exo2 if the project doesn't
    //     ship a font of its own. We don't override an existing font
    //     -- the user may have chosen a custom one.
    {
        const fs::path dest_fonts = dest / s.assets_root_relative / "fonts";
        const bool has_default =
            fs::exists(dest_fonts / "default.ttf", ec) ||
            fs::exists(dest_fonts / "Exo2-VariableFont_wght.ttf", ec);
        if (!has_default) {
            // Try the editor's known font locations in priority order.
            const fs::path candidates[] = {
                fs::path(editor_dir) / "assets" / "fonts" / "Exo2-VariableFont_wght.ttf",
                fs::path(ZUES_ASSETS_DIR_DEFAULT) / "fonts" / "Exo2-VariableFont_wght.ttf",
            };
            for (const auto& src : candidates) {
                if (fs::exists(src, ec)) {
                    fs::create_directories(dest_fonts, ec);
                    (void)copy_file_overwrite(
                        src, dest_fonts / "Exo2-VariableFont_wght.ttf", &err);
                    break;
                }
            }
        }
    }

    // 5. Worlds directory — only if the project keeps worlds OUTSIDE assets/.
    //    Default is "assets/worlds" which step 4 already covered.
    if (!p.worlds_dir.empty()) {
        const fs::path worlds_src = project_dir / p.worlds_dir;
        const fs::path worlds_rel = fs::path(p.worlds_dir);
        const std::string worlds_norm = path_str(worlds_rel);
        const std::string assets_norm = path_str(fs::path(s.assets_root_relative));
        // If worlds_dir isn't a child of assets_root_relative, copy it
        // separately. Cheap string-prefix check after path normalisation.
        const bool inside_assets =
            worlds_norm.size() > assets_norm.size() + 1 &&
            worlds_norm.compare(0, assets_norm.size(), assets_norm) == 0 &&
            worlds_norm[assets_norm.size()] == '/';
        if (!inside_assets && fs::exists(worlds_src, ec)) {
            if (!copy_dir_recursive(worlds_src, dest / p.worlds_dir, &err)) {
                show_toast(s, ("Export: " + err).c_str(), 4.0f, true);
                return false;
            }
        }
    }

    // 6. .zuesproject. Rename to <Name>.zuesproject so it sits next to the
    //    matching exe. The runtime scans for *.zuesproject so the literal
    //    filename doesn't matter, but matching keeps the dist/ tidy.
    const fs::path zproj_dst = dest / (safe_name + ".zuesproject");
    if (!copy_file_overwrite(zproj_src, zproj_dst, &err)) {
        show_toast(s, ("Export: " + err).c_str(), 4.0f, true);
        return false;
    }

    log_write(LogLevel::Info, "export",
              (std::string("exported ") + kind_label(kind) + " to " +
               path_str(dest)).c_str());
    show_toast(s,
        (std::string("Exported ") + kind_label(kind) + " -> " +
         path_str(dest)).c_str(),
        5.0f, false);
    return true;
}

}  // namespace Engine::editor
