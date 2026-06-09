#include <zues/host/sprite_render_system.h>

#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/engine.h>
#include <zues/log.h>
#include <zues/service.h>
#include <zues/services/render_camera.h>
#include <zues/services/renderer_2d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Engine::host {

namespace {
    struct DrawCmd {
        float cx, cy, w_px, h_px, angle;
        float u0, v0, u1, v1;
        float r, g, b, a;
        u32   texture;
        i32   layer;
        i32   order;
        float world_y;   // entity's Transform2D.position.y, used for Y-sort modes
    };

    // Resolved interfaces + cached IDs + per-tick camera snapshot. Lives as
    // long as the system is registered. Single-instance — one editor + one
    // active world for now.
    struct SystemCtx {
        ::IRenderer_2D_v1*  renderer    = nullptr;
        ::IRenderCamera_v1* camera_svc  = nullptr;
        ecs::ComponentId  xform_id    = 0;
        ecs::ComponentId  sprite_id   = 0;

        // Refreshed at the top of each tick from the camera service so the
        // per-entity branch reads them as plain locals.
        float pan_x = 0, pan_y = 0;
        float ppu = 100.0f;          // pixels_per_unit * zoom
        float view_cx = 0, view_cy = 0;

        std::vector<DrawCmd> draw_buf;
    };
    SystemCtx g_ctx{};

    // Per-frame: snapshot camera, collect draw commands, sort, then flush.
    void run_system(ecs::World& world, float, void* user) {
        auto* ctx = static_cast<SystemCtx*>(user);
        if (!ctx || !ctx->renderer || !ctx->camera_svc) return;
        if (!ctx->xform_id || !ctx->sprite_id)          return;

        ZuesRenderCamera cam{};
        ctx->camera_svc->get_active(ctx->camera_svc, &cam);
        if (cam.viewport_w <= 0 || cam.viewport_h <= 0) return;

        ctx->pan_x   = cam.pan_x;
        ctx->pan_y   = cam.pan_y;
        ctx->ppu     = cam.pixels_per_unit * cam.zoom;
        ctx->view_cx = static_cast<float>(cam.viewport_w) * 0.5f;
        ctx->view_cy = static_cast<float>(cam.viewport_h) * 0.5f;

        ctx->draw_buf.clear();
        const int sort_mode = cam.sort_mode;

        const ecs::ComponentId required[] = { ctx->xform_id, ctx->sprite_id };

        // UIAnchor entities render in a separate screen-space pass (see
        // panel_game.cpp / panel_scene.cpp overlay step). Exclude them
        // from the world render so HUD elements don't appear inside the
        // game world.
        const ecs::ComponentId ui_id = world.find_component_id("UIAnchor");
        const ecs::ComponentId excluded[] = { ui_id };
        const uint32_t n_excluded = (ui_id == ecs::INVALID_COMPONENT_ID) ? 0u : 1u;

        // Capture `world` in the user pointer so the lambda can compose
        // the entity's world transform up the parent chain. Without this,
        // children render at their LOCAL position in world space (a child
        // at local (0,0) under a parent at world (10,5) would render at
        // (0,0) instead of (10,5)).
        struct Closure { SystemCtx* c; ecs::World* w; } closure{ctx, &world};

        world.iterate_query(required, 2, excluded, n_excluded,
            +[](void* u, ecs::Entity e, void** cols, u32) {
                auto* cl = static_cast<Closure*>(u);
                auto* c  = cl->c;
                auto* sp = static_cast<components::Sprite*>(cols[1]);

                // Compose world transform from the hierarchy. Roots return
                // their own Transform2D unchanged, so this is a no-op cost
                // for entities with no parent.
                const auto W = cl->w->world_transform_2d(e);

                // Sprite world size (cm) -> pixel size in viewport. Scale
                // comes from the COMPOSED hierarchy scale, not just local.
                const float w_px = sp->size.x * W.scale_x * c->ppu;
                const float h_px = sp->size.y * W.scale_y * c->ppu;

                // World->screen: pan = world point at viewport center.
                // World Y is up; viewport is Y-down → negate the Y delta.
                const float cx = (W.pos_x - c->pan_x) * c->ppu + c->view_cx;
                const float cy = (-(W.pos_y - c->pan_y)) * c->ppu + c->view_cy;

                // Y-flip means a CCW math rotation looks CW on screen. Negate
                // the angle so a positive rotation feels CCW to the user.
                const float angle = -W.rot;

                // Pivot (0..1) shifts the sprite center off the transform
                // position. With pivot=(0.5,0.5) the offset is zero and
                // (cx,cy) IS the sprite center. The offset itself rotates
                // with the sprite, so apply it in local space pre-rotation.
                const float pox = (0.5f - sp->pivot.x) * w_px;
                const float poy = (0.5f - sp->pivot.y) * h_px;
                const float ca = std::cos(angle), sa = std::sin(angle);
                const float pivot_dx = pox * ca - poy * sa;
                const float pivot_dy = pox * sa + poy * ca;

                // Slice support: if the Sprite has a non-zero slice
                // rect, sample only that sub-region of the texture.
                // The animator system writes these for each frame; a
                // statically-set sprite leaves them at zero and gets
                // the whole texture (the legacy 0..1 path).
                float u0, v0, u1, v1;
                if (sp->slice_w > 0 && sp->slice_h > 0) {
                    // Need texture pixel size to convert pixel rect to
                    // UVs. Resolve through the renderer service.
                    int tw = 0, th = 0;
                    if (c->renderer && c->renderer->get_texture_size)
                        c->renderer->get_texture_size(c->renderer,
                            sp->texture.index, &tw, &th);
                    const float fw = (tw > 0) ? (float)tw : 1.0f;
                    const float fh = (th > 0) ? (float)th : 1.0f;
                    u0 = (float)sp->slice_x / fw;
                    v0 = (float)sp->slice_y / fh;
                    u1 = (float)(sp->slice_x + sp->slice_w) / fw;
                    v1 = (float)(sp->slice_y + sp->slice_h) / fh;
                    if (sp->flip_x) std::swap(u0, u1);
                    if (sp->flip_y) std::swap(v0, v1);
                } else {
                    // Whole texture, with optional flip.
                    u0 = sp->flip_x ? 1.0f : 0.0f;
                    u1 = sp->flip_x ? 0.0f : 1.0f;
                    v0 = sp->flip_y ? 1.0f : 0.0f;
                    v1 = sp->flip_y ? 0.0f : 1.0f;
                }

                // ---- 9-slice fast-path test --------------------------
                // All-zero borders means "not 9-sliced" -- emit one
                // quad and skip the multi-region math. Same code path
                // as before; the slice-rect UVs above already cover
                // both whole-texture and slice-rect cases.
                const bool nine_sliced =
                    sp->slice_w > 0 && sp->slice_h > 0 &&
                    (sp->border_l + sp->border_r + sp->border_t + sp->border_b) > 0;

                // Diagnostic: log when the 9-slice border state changes
                // for any (texture, slice-rect) key we've seen. Fires
                // once per change rather than once per frame, so the
                // console gets one line per real edit instead of 60.
                {
                    struct Key {
                        u32 tex; i32 sx, sy, sw, sh;
                        bool operator==(const Key& o) const {
                            return tex==o.tex && sx==o.sx && sy==o.sy &&
                                   sw==o.sw && sh==o.sh;
                        }
                    };
                    struct Val { i32 bl, br, bt, bb, sm; };
                    static thread_local std::vector<std::pair<Key,Val>> seen;
                    Key k{ sp->texture.index, sp->slice_x, sp->slice_y,
                           sp->slice_w, sp->slice_h };
                    Val v{ sp->border_l, sp->border_r, sp->border_t,
                           sp->border_b, sp->scale_mode };
                    bool changed = true;
                    for (auto& e : seen) {
                        if (e.first == k) {
                            if (e.second.bl == v.bl && e.second.br == v.br &&
                                e.second.bt == v.bt && e.second.bb == v.bb &&
                                e.second.sm == v.sm) {
                                changed = false;
                            } else {
                                e.second = v;
                            }
                            goto done;
                        }
                    }
                    seen.push_back({k, v});
                done:
                    if (changed) {
                        char dbg[256];
                        std::snprintf(dbg, sizeof(dbg),
                                      "9slice-dbg: tex=%u slice=[%d,%d %dx%d] "
                                      "borders=[L%d R%d T%d B%d] mode=%d -> %s",
                                      sp->texture.index,
                                      sp->slice_x, sp->slice_y,
                                      sp->slice_w, sp->slice_h,
                                      sp->border_l, sp->border_r,
                                      sp->border_t, sp->border_b,
                                      sp->scale_mode,
                                      nine_sliced ? "9SLICED" : "single-quad");
                        ZUES_LOG_INFO(dbg);
                    }
                }

                const float final_cx = cx + pivot_dx;
                const float final_cy = cy + pivot_dy;

                if (!nine_sliced) {
                    c->draw_buf.push_back({
                        final_cx, final_cy, w_px, h_px, angle,
                        u0, v0, u1, v1,
                        sp->tint.r, sp->tint.g, sp->tint.b, sp->tint.a,
                        sp->texture.index,
                        sp->layer, sp->order,
                        W.pos_y
                    });
                } else {
                    // ---- 9-slice draw (Stretch / Tile / TileFit) -----
                    // Border ratios on the source slice. Borders
                    // themselves are corner pieces; their RENDERED size
                    // also uses the per-pixel scale below so corners
                    // stay 1:1 with source pixels at the sprite's PPU.
                    const float fsw = (float)sp->slice_w;
                    const float fsh = (float)sp->slice_h;

                    // "How many screen pixels does ONE source pixel
                    // become." Independent of how the sprite has been
                    // stretched. We need this so tiled regions can know
                    // their natural tile size and corners can stay
                    // pixel-perfect. ppu = camera_ppu / texture_ppu.
                    const float tex_ppu = (sp->texture_ppu > 0.0f)
                                            ? sp->texture_ppu : 100.0f;
                    const float src_to_screen = c->ppu / tex_ppu;

                    const float bl_px = (float)sp->border_l * src_to_screen;
                    const float br_px = (float)sp->border_r * src_to_screen;
                    const float bt_px = (float)sp->border_t * src_to_screen;
                    const float bb_px = (float)sp->border_b * src_to_screen;
                    const float cw_px = std::max(0.0f, w_px - bl_px - br_px);
                    const float ch_px = std::max(0.0f, h_px - bt_px - bb_px);

                    // Border UV split within the slice's UV range.
                    const float du = u1 - u0;
                    const float dv = v1 - v0;
                    const float ul = u0 + ((float)sp->border_l / fsw) * du;
                    const float ur = u1 - ((float)sp->border_r / fsw) * du;
                    const float vt = v0 + ((float)sp->border_t / fsh) * dv;
                    const float vb = v1 - ((float)sp->border_b / fsh) * dv;

                    // Per-region target screen sizes (one per row/col).
                    const float col_w[3] = { bl_px, cw_px, br_px };
                    const float row_h[3] = { bt_px, ch_px, bb_px };
                    const float us[4]    = { u0, ul, ur, u1 };
                    const float vs[4]    = { v0, vt, vb, v1 };

                    // Natural (unstretched) center sizes in screen px.
                    // For corners/edges the natural size IS the rendered
                    // size (corners never scale; edges scale only along
                    // their stretch axis).
                    const float center_src_w = std::max(0.0f, fsw - sp->border_l - sp->border_r);
                    const float center_src_h = std::max(0.0f, fsh - sp->border_t - sp->border_b);
                    const float natural_cw   = center_src_w * src_to_screen;
                    const float natural_ch   = center_src_h * src_to_screen;
                    const int edge_mode   = sp->scale_mode;
                    const int center_mode = sp->center_mode;

                    // Build segment lists per axis per region. A segment
                    // is (pixel_offset_within_region, pixel_length, uv_a, uv_b).
                    // The mode is passed in per-call so the center can
                    // use a different mode than the four edges.
                    struct Seg { float off, len, ua, ub; };
                    auto make_segs = [&](std::vector<Seg>& out,
                                         float target_px, float natural_px,
                                         float u_a, float u_b,
                                         bool tilable, int mode) {
                        out.clear();
                        if (target_px <= 0.0f) return;
                        if (!tilable || mode == 0 /*Stretch*/ || natural_px <= 0.0f) {
                            out.push_back({0.0f, target_px, u_a, u_b});
                            return;
                        }
                        if (mode == 1 /*Tile*/) {
                            const int n = (int)std::floor(target_px / natural_px);
                            float used = 0.0f;
                            for (int k = 0; k < n; ++k) {
                                out.push_back({used, natural_px, u_a, u_b});
                                used += natural_px;
                            }
                            const float rem = target_px - used;
                            if (rem > 0.0f) {
                                const float frac = rem / natural_px;
                                const float u_partial = u_a + (u_b - u_a) * frac;
                                out.push_back({used, rem, u_a, u_partial});
                            }
                        } else /*TileFit*/ {
                            const int count = std::max(1,
                                (int)std::round(target_px / natural_px));
                            const float seg = target_px / (float)count;
                            for (int k = 0; k < count; ++k) {
                                out.push_back({k * seg, seg, u_a, u_b});
                            }
                        }
                    };

                    static thread_local std::vector<Seg> x_segs, y_segs;

                    // Cumulative offsets for region top-lefts.
                    const float col_off[3] = { 0.0f, bl_px, bl_px + cw_px };
                    const float row_off[3] = { 0.0f, bt_px, bt_px + ch_px };

                    for (int j = 0; j < 3; ++j) {
                        if (row_h[j] <= 0.0f) continue;
                        const bool y_tilable = (j == 1);
                        const float natural_h = (j == 1) ? natural_ch : row_h[j];

                        for (int i = 0; i < 3; ++i) {
                            if (col_w[i] <= 0.0f) continue;
                            const bool x_tilable = (i == 1);
                            const float natural_w = (i == 1) ? natural_cw : col_w[i];

                            // Region picks its mode: center (i==j==1)
                            // uses center_mode; everyone else uses
                            // edge_mode. Corners are non-tilable so the
                            // mode value is irrelevant for them.
                            const bool is_center = (i == 1 && j == 1);
                            const int  region_mode = is_center ? center_mode : edge_mode;

                            make_segs(y_segs, row_h[j], natural_h,
                                       vs[j], vs[j + 1], y_tilable, region_mode);
                            make_segs(x_segs, col_w[i], natural_w,
                                       us[i], us[i + 1], x_tilable, region_mode);

                            for (const auto& ys : y_segs) {
                                for (const auto& xs : x_segs) {
                                    const float sub_w = xs.len;
                                    const float sub_h = ys.len;
                                    if (sub_w <= 0.0f || sub_h <= 0.0f) continue;

                                    // Sub-quad center offset from sprite center.
                                    const float lcx = (col_off[i] + xs.off + sub_w * 0.5f)
                                                      - w_px * 0.5f;
                                    const float lcy = (row_off[j] + ys.off + sub_h * 0.5f)
                                                      - h_px * 0.5f;
                                    const float rdx = lcx * ca - lcy * sa;
                                    const float rdy = lcx * sa + lcy * ca;

                                    c->draw_buf.push_back({
                                        final_cx + rdx, final_cy + rdy,
                                        sub_w, sub_h, angle,
                                        xs.ua, ys.ua, xs.ub, ys.ub,
                                        sp->tint.r, sp->tint.g,
                                        sp->tint.b, sp->tint.a,
                                        sp->texture.index,
                                        sp->layer, sp->order,
                                        W.pos_y
                                    });
                                }
                            }
                        }
                    }
                }
            }, &closure);

        // stable_sort preserves iteration order for equal-key pairs.
        std::stable_sort(ctx->draw_buf.begin(), ctx->draw_buf.end(),
            [sort_mode](const DrawCmd& a, const DrawCmd& b) {
                if (a.layer != b.layer) return a.layer < b.layer;
                if (sort_mode == ZUES_SORT_Y_DESCENDING) {
                    // Lower world-Y draws first (behind); higher Y on top.
                    if (a.world_y != b.world_y) return a.world_y < b.world_y;
                } else if (sort_mode == ZUES_SORT_Y_ASCENDING) {
                    // Higher world-Y draws first (behind); lower Y on top.
                    if (a.world_y != b.world_y) return a.world_y > b.world_y;
                }
                return a.order < b.order;
            });

        for (const DrawCmd& cmd : ctx->draw_buf) {
            ctx->renderer->draw_sprite_rot(ctx->renderer, cmd.texture,
                cmd.cx, cmd.cy, cmd.w_px, cmd.h_px, cmd.angle,
                cmd.u0, cmd.v0, cmd.u1, cmd.v1,
                cmd.r, cmd.g, cmd.b, cmd.a);
        }
    }
}

bool SpriteRenderSystem::register_into(ecs::World& world, ::IRenderer_2D_v1* renderer) {
    auto* sr = Engine::services();
    auto* cam_svc = sr ? static_cast<::IRenderCamera_v1*>(
        sr->get_service(ZUES_SERVICE_RENDER_CAMERA, ZUES_SERVICE_RENDER_CAMERA_VERSION))
        : nullptr;

    if (!renderer) { ZUES_LOG_WARN("sprite_render_system: no renderer; skipped");   return false; }
    if (!cam_svc)  { ZUES_LOG_WARN("sprite_render_system: no camera svc; skipped"); return false; }

    g_ctx.renderer   = renderer;
    g_ctx.camera_svc = cam_svc;
    g_ctx.xform_id   = world.find_component_id("Transform2D");
    g_ctx.sprite_id  = world.find_component_id("Sprite");

    if (!g_ctx.xform_id || !g_ctx.sprite_id) {
        ZUES_LOG_WARN("sprite_render_system: builtins not registered yet - "
                      "system installed but will idle until they are");
    }

    // Render visible in BOTH modes — the user wants to see the scene whether
    // they're editing or playing. Same call site for any future engine
    // built-in render pipelines.
    handle = world.add_system("Sprite Render",
                              ecs::Phase::Render, run_system, &g_ctx,
                              ecs::SystemDomain::Both);
    ZUES_LOG_INFO("Sprite Render system registered (Phase::Render)");
    return handle.is_valid();
}

void SpriteRenderSystem::unregister_from(ecs::World& world) {
    if (handle.is_valid()) {
        world.remove_system(handle);
        handle = {};
    }
    g_ctx = {};
}

}  // namespace Engine::host
