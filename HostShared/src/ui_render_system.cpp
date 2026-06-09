// HUD render pass. Mirrors the world sprite_render_system but everything
// here is in SCREEN-SPACE pixels: we ignore Transform2D entirely and
// derive position from UIAnchor + viewport size. Runs in Phase::Render
// AFTER the sprite system so HUD draws on top of the scene.
//
// Two passes per frame:
//   1) UIAnchor + Sprite -> draw_sprite (or draw_quad when texture==0)
//   2) UIAnchor + Text   -> measure_text + draw_text
//
// Default font: lazy-loaded once at the first run() call. We can't load
// in register_into() because the renderer's GL context isn't necessarily
// current then (modules' on_ready runs before host wiring).

#include <zues/host/ui_render_system.h>

#include <zues/components/render.h>
#include <zues/engine.h>
#include <zues/log.h>
#include <zues/service.h>
#include <zues/services/render_camera.h>
#include <zues/services/renderer_2d.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine::host {

namespace fs = std::filesystem;

namespace {

struct UICtx {
    ::IRenderer_2D_v1*  renderer    = nullptr;
    ::IRenderCamera_v1* camera_svc  = nullptr;
    ecs::ComponentId    ui_id       = 0;
    ecs::ComponentId    sprite_id   = 0;
    ecs::ComponentId    text_id     = 0;

    ZuesFontHandle      default_font  = 0;
    bool                font_attempted = false;   // don't retry every frame on failure
    std::vector<std::string> font_dirs;            // dirs to search

    ecs::SystemHandle   handle{};
};

UICtx g_ui{};

// Find the first existing "fonts/default.ttf" or "fonts/Exo2-VariableFont_wght.ttf"
// inside the search dirs. We accept either to be friendly: the editor's
// asset dir uses Exo2; project assets/fonts/default.ttf is the official
// override path.
std::string resolve_default_font(const std::vector<std::string>& dirs) {
    static const char* kCandidates[] = {
        "fonts/default.ttf",
        "fonts/Exo2-VariableFont_wght.ttf",
    };
    std::error_code ec;
    for (const auto& d : dirs) {
        for (const char* c : kCandidates) {
            const fs::path p = fs::path(d) / c;
            if (fs::exists(p, ec)) return p.string();
        }
    }
    return {};
}

void try_load_default_font(UICtx& ctx) {
    if (ctx.font_attempted || !ctx.renderer || !ctx.renderer->load_font_from_file)
        return;
    ctx.font_attempted = true;
    const std::string path = resolve_default_font(ctx.font_dirs);
    if (path.empty()) {
        log_write(LogLevel::Warn, "ui_render",
                  "default font not found in any search dir; "
                  "Text components will render as missing-glyph rects");
        return;
    }
    // 32-pixel bake covers most HUD sizes (12..48 px) at acceptable
    // crispness. Smaller text gets bilinear-downscaled; larger text gets
    // upsampled (a touch blurry past 2x but fine for v5).
    ctx.default_font = ctx.renderer->load_font_from_file(
        ctx.renderer, path.c_str(), 32);
    if (ctx.default_font == 0) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "ui_render: load_font_from_file failed: %s",
                      path.c_str());
        log_write(LogLevel::Warn, "ui_render", buf);
    } else {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "ui_render: default font loaded (%s)", path.c_str());
        log_write(LogLevel::Info, "ui_render", buf);
    }
}

void run_system(ecs::World& world, float, void* user) {
    auto* ctx = static_cast<UICtx*>(user);
    if (!ctx || !ctx->renderer || !ctx->camera_svc) return;
    if (!ctx->ui_id) {
        // Re-resolve in case UIAnchor was registered after we ran first.
        ctx->ui_id     = world.find_component_id("UIAnchor");
        ctx->sprite_id = world.find_component_id("Sprite");
        ctx->text_id   = world.find_component_id("Text");
        if (!ctx->ui_id) return;
    }

    ZuesRenderCamera cam{};
    ctx->camera_svc->get_active(ctx->camera_svc, &cam);
    if (cam.viewport_w <= 0 || cam.viewport_h <= 0) return;
    const float vw = static_cast<float>(cam.viewport_w);
    const float vh = static_cast<float>(cam.viewport_h);

    try_load_default_font(*ctx);

    // One-shot diagnostic: log how many UIAnchor entities the system sees
    // the first time it runs. Surfaces the "I added a Text but nothing
    // shows" case where the components weren't actually attached, vs a
    // font/render bug. Only logs the first frame to avoid log spam.
    static bool diag_logged = false;
    if (!diag_logged) {
        diag_logged = true;
        int n_sprite = 0, n_text = 0;
        if (ctx->sprite_id) {
            const ecs::ComponentId req[] = { ctx->ui_id, ctx->sprite_id };
            world.iterate_query(req, 2, nullptr, 0,
                +[](void* u, ecs::Entity, void**, u32){ ++*static_cast<int*>(u); }, &n_sprite);
        }
        if (ctx->text_id) {
            const ecs::ComponentId req[] = { ctx->ui_id, ctx->text_id };
            world.iterate_query(req, 2, nullptr, 0,
                +[](void* u, ecs::Entity, void**, u32){ ++*static_cast<int*>(u); }, &n_text);
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "ui_render: vw=%g vh=%g  UIAnchor+Sprite=%d  UIAnchor+Text=%d  font=%u",
            (double)vw, (double)vh, n_sprite, n_text, (unsigned)ctx->default_font);
        log_write(LogLevel::Info, "ui_render", buf);
    }

    // ---- Pass A: UIAnchor + Sprite ---------------------------------------
    if (ctx->sprite_id) {
        const ecs::ComponentId required[] = { ctx->ui_id, ctx->sprite_id };
        struct Closure { UICtx* c; float vw, vh; };
        Closure cls{ ctx, vw, vh };
        world.iterate_query(required, 2, nullptr, 0,
            +[](void* u, ecs::Entity, void** cols, u32) {
                auto* cl = static_cast<Closure*>(u);
                auto* ui = static_cast<components::UIAnchor*>(cols[0]);
                auto* sp = static_cast<components::Sprite*>(cols[1]);
                const float w  = sp->size.x;
                const float h  = sp->size.y;
                const float ax = ui->anchor.x * cl->vw + ui->pixel_offset.x;
                const float ay = ui->anchor.y * cl->vh + ui->pixel_offset.y;
                const float x0 = ax - ui->pivot.x * w;
                const float y0 = ay - ui->pivot.y * h;
                if (sp->texture.index != 0) {
                    cl->c->renderer->draw_sprite(cl->c->renderer,
                        sp->texture.index, x0, y0, w, h,
                        0.0f, 0.0f, 1.0f, 1.0f,
                        sp->tint.r, sp->tint.g, sp->tint.b, sp->tint.a);
                } else {
                    cl->c->renderer->draw_quad(cl->c->renderer,
                        x0, y0, w, h,
                        sp->tint.r, sp->tint.g, sp->tint.b, sp->tint.a);
                }
            }, &cls);
    }

    // ---- Pass B: UIAnchor + Text -----------------------------------------
    if (ctx->text_id && ctx->renderer->draw_text) {
        const ecs::ComponentId required[] = { ctx->ui_id, ctx->text_id };
        struct Closure { UICtx* c; float vw, vh; };
        Closure cls{ ctx, vw, vh };
        world.iterate_query(required, 2, nullptr, 0,
            +[](void* u, ecs::Entity, void** cols, u32) {
                auto* cl = static_cast<Closure*>(u);
                auto* ui = static_cast<components::UIAnchor*>(cols[0]);
                auto* tx = static_cast<components::Text*>(cols[1]);
                if (tx->utf8[0] == 0) return;

                ZuesFontHandle font = tx->font.index;
                if (font == 0) font = cl->c->default_font;
                if (font == 0) return;   // no font available, skip

                const float pixel_size = tx->size_px > 0 ? tx->size_px : 16.0f;
                float tw = 0.0f, th = 0.0f;
                cl->c->renderer->measure_text(cl->c->renderer, font,
                    tx->utf8, pixel_size, &tw, &th);

                // h_align overrides pivot.x for Text -- keeps a label that
                // grows ("0" -> "120") anchored at the same edge. pivot.y
                // still applies vertically.
                float pivot_x = ui->pivot.x;
                if      (tx->h_align == 1) pivot_x = 0.5f;
                else if (tx->h_align == 2) pivot_x = 1.0f;
                else if (tx->h_align == 0) pivot_x = 0.0f;

                const float ax = ui->anchor.x * cl->vw + ui->pixel_offset.x;
                const float ay = ui->anchor.y * cl->vh + ui->pixel_offset.y;
                const float x0 = ax - pivot_x      * tw;
                const float y0 = ay - ui->pivot.y  * th;

                cl->c->renderer->draw_text(cl->c->renderer, font,
                    tx->utf8, x0, y0, pixel_size,
                    tx->color.r, tx->color.g, tx->color.b, tx->color.a);
            }, &cls);
    }
}

}  // namespace

bool UIRenderSystem::register_into(ecs::World& world,
                                    ::IRenderer_2D_v1* renderer,
                                    std::initializer_list<std::string> font_search_dirs) {
    auto* sr = Engine::services();
    auto* cam_svc = sr ? static_cast<::IRenderCamera_v1*>(
        sr->get_service(ZUES_SERVICE_RENDER_CAMERA, ZUES_SERVICE_RENDER_CAMERA_VERSION))
        : nullptr;

    if (!renderer) { log_write(LogLevel::Warn, "ui_render", "no renderer; skipped");   return false; }
    if (!cam_svc)  { log_write(LogLevel::Warn, "ui_render", "no camera svc; skipped"); return false; }

    g_ui.renderer    = renderer;
    g_ui.camera_svc  = cam_svc;
    g_ui.ui_id       = world.find_component_id("UIAnchor");
    g_ui.sprite_id   = world.find_component_id("Sprite");
    g_ui.text_id     = world.find_component_id("Text");
    g_ui.font_dirs.assign(font_search_dirs.begin(), font_search_dirs.end());
    g_ui.default_font   = 0;
    g_ui.font_attempted = false;

    // Phase::Render, registered AFTER sprite_render_system so HUD draws on
    // top. Both domains so the HUD is visible during edit-mode preview as
    // well as play.
    g_ui.handle = world.add_system("UI Render",
                                    ecs::Phase::Render, run_system, &g_ui,
                                    ecs::SystemDomain::Both);
    log_write(LogLevel::Info, "ui_render",
              "registered on Phase::Render (HUD pass)");
    return g_ui.handle.is_valid();
}

void UIRenderSystem::unregister_from(ecs::World& world) {
    if (g_ui.handle.is_valid()) {
        world.remove_system(g_ui.handle);
        g_ui.handle = {};
    }
    if (g_ui.default_font && g_ui.renderer && g_ui.renderer->free_font) {
        g_ui.renderer->free_font(g_ui.renderer, g_ui.default_font);
    }
    g_ui = {};
}

}  // namespace Engine::host
