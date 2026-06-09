#pragma once

// HUD render pass. Iterates entities tagged with UIAnchor and draws their
// Text / Sprite component into the currently-bound render target in
// SCREEN-SPACE pixel coords (anchor 0..1 within the viewport + pixel
// offset, pivot, h_align). World-space sprite_render_system excludes the
// same entities, so HUD elements never bleed into the world.
//
// Both editor and runtime register one of these. The editor binds the
// Game render target before its Render phase tick; the runtime binds the
// swapchain. The system reads viewport size from the render-camera
// service so it scales correctly across both.
//
// Default font: lazy-loaded once from <project>/assets/fonts/default.ttf,
// falling back to the engine's bundled Exo2 if that file is missing. A
// per-Text custom font handle in Text.font (FontHandle.id != 0) overrides.

#include <zues/api.h>
#include <zues/ecs/world.h>

#include <string>

struct IRenderer_2D_v1;
typedef uint32_t ZuesFontHandle;

namespace Engine::host {

struct UIRenderSystem {
    // Register the system on Phase::Render. `default_font_search_dirs` is
    // an absolute path list searched for "fonts/default.ttf"; the first
    // file that exists wins. Pass the project_dir/assets first, then the
    // engine assets dir as fallback.
    bool register_into(Engine::ecs::World& world,
                       ::IRenderer_2D_v1* renderer,
                       std::initializer_list<std::string> font_search_dirs);

    void unregister_from(Engine::ecs::World& world);
};

}  // namespace Engine::host
