#pragma once

// Asset path resolution + editor icon registry.
//
// `assets_dir()` returns the base assets directory. Defaults to the path
// baked at build time via `ZUES_ASSETS_DIR_DEFAULT` (set by Editor's
// CMakeLists.txt to ${CMAKE_SOURCE_DIR}/assets). Override at runtime with
// the ZUES_ASSETS environment variable for testing alternate asset trees.
//
// `EditorIcons` holds renderer-loaded GL texture handles for the icons the
// editor uses across panels. Loaded once at startup; freed at shutdown.
// Icons are PNGs from the user-supplied asset pack — paths are relative to
// assets_dir() so adding a new icon is a one-liner.

#include <zues/types.h>

#include <string>

struct IRenderer_2D_v1;

namespace Engine::editor {

// Base assets directory. Honors $ZUES_ASSETS env var; falls back to the
// build-time constant. Always has a trailing path separator stripped.
const std::string& assets_dir();

// Convenience: assets_dir() + "/" + relative.
std::string asset_path(const char* relative);

// Texture handles for the editor's stock icons. 0 = "not loaded" — call
// sites should fall back to text labels when the handle is 0 (e.g. if the
// asset directory is missing).
struct EditorIcons {
    u32 play  = 0;
    u32 pause = 0;
    u32 stop  = 0;
};

// Load all stock icons via the renderer's load_texture_from_file. Logs
// warnings (not errors) for missing files so the editor still boots if the
// asset dir isn't there.
void icons_load(EditorIcons& out, ::IRenderer_2D_v1* renderer);

// Free every loaded icon texture. Called at shutdown before the renderer
// tears down. Safe if some handles are 0.
void icons_free(EditorIcons& icons, ::IRenderer_2D_v1* renderer);

}  // namespace Engine::editor
