#pragma once

// .zuesproject — the metadata file at the root of every Zues project.
// Plain JSON. Tiny on purpose: editor settings, default world, asset
// pipeline knobs land here over time.
//
//   {
//     "name": "MyGame",
//     "engine_version": "0.1.0",
//     "default_world": "Worlds/Main.world",
//     "settings": {
//       "window_width": 1280,
//       "window_height": 720,
//       "window_title": "My Game"
//     }
//   }
//
// load_project / save_project handle the whole struct — fields not present
// in the JSON take their default values, fields the JSON has that we don't
// recognize are ignored (forward-compatible).

#include <zues/api.h>
#include <zues/types.h>

#include <string>

namespace Engine {

struct ProjectSettings {
    int         window_width  = 1280;
    int         window_height = 720;
    std::string window_title;        // defaults to project.name if empty

    // Lock the runtime window to window_width x window_height -- GLFW will
    // refuse user resize attempts. Useful when the game's UI / camera math
    // is tuned for a specific resolution and arbitrary stretches break it.
    // Has no effect on the editor; only the standalone runtime honours it.
    bool        fixed_size  = false;

    // Launch the runtime fullscreen on the primary monitor at
    // window_width x window_height (GLFW picks the closest video mode).
    // When true, fixed_size is implicit -- the user can't resize a
    // fullscreen window anyway. Editor ignores this flag.
    bool        fullscreen  = false;
};

struct ProjectBuild {
    // Path to the project's compiled DLL, relative to project_dir.
    // Empty = editor falls back to <editor_dir>/<sample dll>.
    // Example: "build/bin/mygame.dll"
    std::string dll_path;

    // Lync auto-compile fields. All paths relative to project_dir.
    // If lync_main is non-empty the editor watches all .lync files under
    // project_dir and re-runs the compiler when any change.
    std::string lync_main;       // entry .lync file (e.g. "mygame.lync")
    std::string lync_compiler;   // path to lync.exe; empty → "lync" on PATH
    std::string lync_plugin;     // path to zues_lync_plugin.dll
    std::string lync_include;    // path to ProjectAPI/include
    std::string lync_prelude;    // optional .lync file prepended to source
};

struct Project {
    std::string      name;
    std::string      engine_version  = "0.1.0";
    std::string      default_world;        // path relative to project_dir
    // Folder under project_dir where worlds are kept. The editor's Save/Open
    // World UI is name-only (no path browsing) — files always land here.
    // Default sits under assets/ so worlds show up in the Asset Browser
    // (double-click a .zworld there to load it).
    std::string      worlds_dir       = "assets/worlds";
    // Source folder + the language used when the editor pre-generates new
    // components/systems. "lync" or "cpp". Changing this doesn't migrate
    // existing files; it just affects new ones.
    std::string      source_dir       = "src";
    std::string      default_language = "lync";
    ProjectSettings  settings;
    ProjectBuild     build;

    // Filled in by load_project. Not serialized — derived from the file path.
    std::string      project_dir;
};

// Load a .zuesproject file. Returns NotFound if the file doesn't exist,
// Error on parse failure.
ZUES_API Result load_project(const char* path, Project& out);

// Write a .zuesproject. The directory must exist. Pretty-printed JSON.
ZUES_API Result save_project(const char* path, const Project& p);

// Create the standard project layout (Assets/, Worlds/, Source/, .zues/cache/)
// + a .zuesproject file at <root>/<name>.zuesproject. Idempotent — safe to
// call on an existing directory; only fills in missing pieces.
ZUES_API Result create_project_skeleton(const char* root_dir, const char* name);

}  // namespace Engine
