#include "editor.h"

#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/engine.h>
#include <zues/service.h>
#include <zues/services/render_camera.h>
#include <zues/services/renderer_2d.h>

#include <imgui.h>

#include <cstdint>

namespace Engine::editor {

namespace {
    // Snapshot of the entity that drives the game viewport. `found=false`
    // means there's no active Camera2D — the panel renders an empty viewport
    // with a "no active camera" overlay so users notice they need to add one.
    struct GameCamera {
        bool              found        = false;
        ecs::Entity       entity{};
        float             pan_x        = 0.0f;
        float             pan_y        = 0.0f;
        float             ortho_size   = 10.0f;   // vertical world cm visible
        int               active_count = 0;        // for "multiple active" warning
        components::SortMode sort_mode = components::SortMode::OrderOnly;
    };

    // First entity with Camera2D + Transform2D and `is_active == true` wins.
    // When multiple are tagged active we just take whichever the iteration
    // order returns first — proper "primary camera" selection is its own UX
    // problem (a flag, a sorting layer, a dedicated component) and lands
    // when there's a real reason for >1 active camera.
    // ---- UI overlay -------------------------------------------------------
    // Walks every entity carrying UIAnchor and draws its Text / Sprite on
    // top of the rendered game image, in panel-pixel coordinates. The
    // sprite_render_system excludes UI-anchored entities from the world
    // pass so they only show up here -- HUD overlays on top of gameplay,
    // never inside the world.
    //
    // Both passes use ImGui's draw list for now (text via AddText, sprites
    // via AddImage). The renderer doesn't yet have a draw_text path; once
    // it does, a packaged standalone runtime can use the same component
    // shape with a different drawing backend.
    void draw_ui_overlay(ecs::World& world,
                          ImVec2 image_pos, ImVec2 image_size) {
        const auto ui_id     = world.find_component_id("UIAnchor");
        const auto sprite_id = world.find_component_id("Sprite");
        const auto text_id   = world.find_component_id("Text");
        if (!ui_id) return;
        auto* dl = ImGui::GetWindowDrawList();
        if (!dl) return;

        // Pass A: UIAnchor + Sprite -> AddImage at (anchor + offset),
        // sized by Sprite.size * pixels-per-unit-equivalent. Without a
        // texture handle we draw a flat-coloured filled rect using the
        // sprite's tint -- matches the renderer's "white default" path
        // so it's not surprising for users.
        if (sprite_id) {
            const ecs::ComponentId required[] = { ui_id, sprite_id };
            struct Ctx { ImDrawList* dl; ImVec2 ip, sz; };
            Ctx ctx{ dl, image_pos, image_size };
            world.iterate_query(required, 2, nullptr, 0,
                +[](void* u, ecs::Entity, void** cols, u32) {
                    auto* c  = static_cast<Ctx*>(u);
                    auto* ui = static_cast<components::UIAnchor*>(cols[0]);
                    auto* sp = static_cast<components::Sprite*>(cols[1]);
                    // Sprite "size" was authored in world units; for HUD
                    // a 1-unit-tall icon would be invisible. Treat size
                    // as PIXELS when the entity is screen-space. This is
                    // the small UX cost of reusing Sprite for both modes;
                    // documented in the Text/UIAnchor docs page.
                    const float w = sp->size.x;
                    const float h = sp->size.y;
                    const float ax = c->ip.x + ui->anchor.x       * c->sz.x
                                            + ui->pixel_offset.x;
                    const float ay = c->ip.y + ui->anchor.y       * c->sz.y
                                            + ui->pixel_offset.y;
                    const float x0 = ax - ui->pivot.x * w;
                    const float y0 = ay - ui->pivot.y * h;
                    const ImU32 col = IM_COL32(
                        (int)(sp->tint.r * 255.0f),
                        (int)(sp->tint.g * 255.0f),
                        (int)(sp->tint.b * 255.0f),
                        (int)(sp->tint.a * 255.0f));
                    if (sp->texture.index != 0) {
                        c->dl->AddImage(
                            (ImTextureID)(std::uintptr_t)sp->texture.index,
                            ImVec2{x0, y0},
                            ImVec2{x0 + w, y0 + h},
                            ImVec2{0, 0}, ImVec2{1, 1}, col);
                    } else {
                        c->dl->AddRectFilled(ImVec2{x0, y0},
                                              ImVec2{x0 + w, y0 + h}, col);
                    }
                }, &ctx);
        }

        // Pass B: UIAnchor + Text -> AddText. ImGui's default font is the
        // editor font; we honour `Text.size_px` by scaling against the
        // current font size.
        if (text_id) {
            const ecs::ComponentId required[] = { ui_id, text_id };
            struct Ctx { ImDrawList* dl; ImVec2 ip, sz; };
            Ctx ctx{ dl, image_pos, image_size };
            world.iterate_query(required, 2, nullptr, 0,
                +[](void* u, ecs::Entity, void** cols, u32) {
                    auto* c  = static_cast<Ctx*>(u);
                    auto* ui = static_cast<components::UIAnchor*>(cols[0]);
                    auto* tx = static_cast<components::Text*>(cols[1]);
                    if (tx->utf8[0] == 0) return;
                    ImFont* font = ImGui::GetFont();
                    const float font_size = tx->size_px > 0 ? tx->size_px
                                                            : ImGui::GetFontSize();
                    const ImVec2 ts = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, tx->utf8);
                    const float ax = c->ip.x + ui->anchor.x       * c->sz.x
                                            + ui->pixel_offset.x;
                    const float ay = c->ip.y + ui->anchor.y       * c->sz.y
                                            + ui->pixel_offset.y;
                    // h_align (left=0, center=1, right=2) overrides pivot.x
                    // for Text -- typing a label that grows from "0" to
                    // "120" otherwise drifts as the string widens, which is
                    // surprising. Pivot.y still applies vertically.
                    float pivot_x = ui->pivot.x;
                    if      (tx->h_align == 1) pivot_x = 0.5f;
                    else if (tx->h_align == 2) pivot_x = 1.0f;
                    else if (tx->h_align == 0) pivot_x = 0.0f;
                    const float x0 = ax - pivot_x        * ts.x;
                    const float y0 = ay - ui->pivot.y    * ts.y;
                    const ImU32 col = IM_COL32(
                        (int)(tx->color.r * 255.0f),
                        (int)(tx->color.g * 255.0f),
                        (int)(tx->color.b * 255.0f),
                        (int)(tx->color.a * 255.0f));
                    c->dl->AddText(font, font_size, ImVec2{x0, y0}, col, tx->utf8);
                }, &ctx);
        }
    }

    GameCamera find_active_camera(ecs::World& world) {
        const auto cam_id   = world.find_component_id("Camera2D");
        const auto xform_id = world.find_component_id("Transform2D");
        if (!cam_id || !xform_id) return {};

        struct Ctx { GameCamera* out; };
        GameCamera snap{};
        Ctx ctx{&snap};

        const ecs::ComponentId required[] = {xform_id, cam_id};
        world.iterate_query(required, 2, nullptr, 0,
            +[](void* u, ecs::Entity e, void** cols, u32) {
                auto* c   = static_cast<Ctx*>(u);
                auto* tr  = static_cast<components::Transform2D*>(cols[0]);
                auto* cam = static_cast<components::Camera2D*>(cols[1]);
                if (!cam->is_active) return;

                c->out->active_count++;
                if (c->out->found) return;     // first match wins, keep counting

                c->out->found      = true;
                c->out->entity     = e;
                c->out->pan_x      = tr->position.x;
                c->out->pan_y      = tr->position.y;
                c->out->ortho_size = cam->ortho_size > 0.0f ? cam->ortho_size : 10.0f;
                c->out->sort_mode  = cam->sort_mode;
            }, &ctx);
        return snap;
    }
}

void draw_game_panel(EditorState& s) {
    if (!s.show_game) return;

    // Auto-focus when transitioning into Play. One-shot — cleared after use.
    if (s.want_focus_game) {
        ImGui::SetNextWindowFocus();
        s.want_focus_game = false;
    }

    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Game", &s.show_game)) {
        const ImVec2 dock_avail = ImGui::GetContentRegionAvail();

        // Fit-and-letterbox to the runtime window aspect (window_width :
        // window_height in Project Settings). What you see in the editor's
        // Game panel matches what the exported runtime will render -- same
        // letterbox bars when the dock pane is wider/taller than the
        // target. Fall back to the panel's full dock space if the project
        // hasn't set sane sizes yet.
        ImVec2 avail = dock_avail;
        ImVec2 letter_offset{0.0f, 0.0f};
        const float ta = (s.project_window_height > 0)
            ? (static_cast<float>(s.project_window_width) /
               static_cast<float>(s.project_window_height))
            : 0.0f;
        if (ta > 0.0f && dock_avail.x > 4.0f && dock_avail.y > 4.0f) {
            const float dock_aspect = dock_avail.x / dock_avail.y;
            if (dock_aspect > ta) {
                // Dock is wider than target -> pillarbox left/right.
                avail.x = dock_avail.y * ta;
                avail.y = dock_avail.y;
                letter_offset.x = (dock_avail.x - avail.x) * 0.5f;
            } else {
                // Dock is taller than target -> letterbox top/bottom.
                avail.x = dock_avail.x;
                avail.y = dock_avail.x / ta;
                letter_offset.y = (dock_avail.y - avail.y) * 0.5f;
            }
        }
        const int want_w = static_cast<int>(avail.x);
        const int want_h = static_cast<int>(avail.y);

        if (r && want_w > 4 && want_h > 4 && s.world) {
            constexpr int SS = 2;           // supersample, same as Scene panel
            const int rt_w = want_w * SS;
            const int rt_h = want_h * SS;

            // Lazy create / resize the Game RT.
            if (s.game_rt == 0) {
                s.game_rt   = r->create_render_target(r, rt_w, rt_h);
                s.game_rt_w = rt_w;
                s.game_rt_h = rt_h;
            } else if (rt_w != s.game_rt_w || rt_h != s.game_rt_h) {
                if (r->resize_render_target(r, s.game_rt, rt_w, rt_h)) {
                    s.game_rt_w = rt_w;
                    s.game_rt_h = rt_h;
                }
            }

            const auto snap = find_active_camera(*s.world);

            r->bind_render_target(r, s.game_rt);
            // Slightly darker clear than the Scene panel so the two viewports
            // are visually distinguishable at a glance.
            r->begin_frame(r, 0.02f, 0.02f, 0.04f, 1.0f);

            if (snap.found) {
                // ortho_size says "this many world-cm fill the viewport
                // vertically". Pixels-per-unit = viewport_h / ortho_size,
                // then zoom=1.0 in the shared camera struct (we've already
                // baked the ortho into ppu). Result: same world area visible
                // regardless of viewport pixel size.
                const float ppu = static_cast<float>(s.game_rt_h) / snap.ortho_size;
                if (auto* sr = Engine::services()) {
                    if (auto* cs = static_cast<IRenderCamera_v1*>(sr->get_service(
                            ZUES_SERVICE_RENDER_CAMERA, ZUES_SERVICE_RENDER_CAMERA_VERSION))) {
                        ZuesRenderCamera cam{};
                        cam.pan_x           = snap.pan_x;
                        cam.pan_y           = snap.pan_y;
                        cam.zoom            = 1.0f;
                        cam.rotation        = 0.0f;
                        cam.pixels_per_unit = ppu;
                        cam.viewport_w      = s.game_rt_w;
                        cam.viewport_h      = s.game_rt_h;
                        cam.sort_mode       = static_cast<int32_t>(snap.sort_mode);
                        cs->set_active(cs, &cam);
                    }
                }
                s.world->tick_phase(ecs::Phase::Render, s.last_dt);
            }

            r->end_frame(r);
            r->bind_render_target(r, 0);

            const auto tex = r->get_render_target_texture(r, s.game_rt);
            if (tex != 0) {
                // Offset cursor by the letterbox margin so the image lands
                // centered when the runtime aspect differs from the panel
                // aspect (=> bars). ImGui's dummy-
                // before-image trick keeps the surrounding window draw list
                // untouched -- the leftover area shows the panel's window
                // background, which gives us "free" black bars.
                if (letter_offset.x > 0.0f || letter_offset.y > 0.0f) {
                    const ImVec2 cur = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2{cur.x + letter_offset.x,
                                                      cur.y + letter_offset.y});
                }
                const ImVec2 image_pos = ImGui::GetCursorScreenPos();
                ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                             avail, ImVec2(0, 1), ImVec2(1, 0));

                // HUD (UIAnchor + Text / Sprite) is now drawn by the
                // engine's ui_render_system inside the Render phase tick
                // above (renderer-side, not ImGui). The editor preview and
                // the exported runtime use the same path, so what the
                // user sees here matches the shipped game pixel-for-pixel.
                (void)image_pos;

                // Status overlays — tiny labels in the corners. Don't steal
                // input focus; they're pure presentation.
                auto* dl = ImGui::GetWindowDrawList();
                const ImU32 dim   = IM_COL32(220, 220, 220, 180);
                const ImU32 amber = IM_COL32(255, 165, 60,  220);
                const ImU32 grey  = IM_COL32(160, 160, 160, 200);

                // Top-left stack: PLAY/EDIT badge + (if playing) snapshot
                // indicator. Snapshot tells the user "your edits during
                // play are reverted on Stop" so it's not a surprise.
                {
                    const auto mode = s.world->tick_mode();
                    const char* tag = (mode == ecs::TickMode::Play)
                        ? "PLAYING" : "EDIT";
                    dl->AddText({image_pos.x + 10.0f, image_pos.y + 8.0f},
                                (mode == ecs::TickMode::Play) ? amber : dim, tag);

                    if (s.is_playing && !s.play_snapshot.empty()) {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf),
                                      "snapshot held (%zu KB) - Stop reverts",
                                      s.play_snapshot.size() / 1024);
                        dl->AddText({image_pos.x + 10.0f, image_pos.y + 26.0f},
                                    grey, buf);
                    }
                    if (s.is_paused) {
                        dl->AddText({image_pos.x + 10.0f,
                                     image_pos.y + 44.0f}, amber, "PAUSED");
                    }
                }

                // Top-right: active camera info or "(no active camera)".
                if (snap.found) {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf),
                                  "Camera: entity %u  ortho %.2f",
                                  snap.entity.index, snap.ortho_size);
                    const float tw = ImGui::CalcTextSize(buf).x;
                    dl->AddText({image_pos.x + avail.x - tw - 10.0f,
                                 image_pos.y + 8.0f}, grey, buf);

                    // Multi-camera warning — sit it just below the camera
                    // info line so it visually attaches. First-active wins;
                    // we don't try to be clever about which "should" win.
                    if (snap.active_count > 1) {
                        char wbuf[120];
                        std::snprintf(wbuf, sizeof(wbuf),
                            "WARNING: %d active cameras - using entity %u",
                            snap.active_count, snap.entity.index);
                        const float ww = ImGui::CalcTextSize(wbuf).x;
                        dl->AddText({image_pos.x + avail.x - ww - 10.0f,
                                     image_pos.y + 26.0f}, amber, wbuf);
                    }
                } else {
                    const char* msg = "(no active Camera2D entity)";
                    const float tw  = ImGui::CalcTextSize(msg).x;
                    dl->AddText({image_pos.x + avail.x - tw - 10.0f,
                                 image_pos.y + 8.0f}, amber, msg);
                    const char* hint = "Add an entity with a Camera2D component\n"
                                       "and set is_active = true.";
                    const ImVec2 sz = ImGui::CalcTextSize(hint);
                    dl->AddText({image_pos.x + (avail.x - sz.x) * 0.5f,
                                 image_pos.y + (avail.y - sz.y) * 0.5f},
                                dim, hint);
                }
            }
        } else {
            ImGui::TextDisabled("(Game panel needs renderer + space)");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

}  // namespace Engine::editor
