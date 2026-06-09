#include "editor.h"
#include "gizmos.h"

#include <zues/components/physics.h>
#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/engine.h>
#include <zues/service.h>
#include <zues/services/render_camera.h>
#include <zues/services/renderer_2d.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine::editor {

namespace {
    // ---- Coordinate helpers -------------------------------------------------

    // Pixel inside the scene image -> world (cm).
    Engine::math::vec2 pixel_to_world(float px, float py,
                                      const EditorCamera& cam, int vw, int vh) {
        const float ppu = cam.pixels_per_unit * cam.zoom;
        const float dx = (px - vw * 0.5f) / ppu;
        const float dy = (py - vh * 0.5f) / ppu;
        return { cam.pan.x + dx, cam.pan.y - dy };   // Y flipped
    }

    // ---- Background --------------------------------------------------------

    void draw_background(IRenderer_2D_v1* r, int rt_w, int rt_h) {
        r->draw_quad(r, 0, 0, static_cast<float>(rt_w), static_cast<float>(rt_h),
                     0.04f, 0.05f, 0.07f, 1.0f);
    }

    // ---- Auto-scaling grid -------------------------------------------------
    //
    // Two grid levels render each frame. Spacing is chosen so the smaller
    // ("minor") level always sits at >= MIN_PX pixels apart; the larger
    // ("major") is 10× bigger. As you zoom out, the minor lines slide below
    // an alpha threshold and fade out smoothly while the major lines take
    // over. Then the next decade up becomes the new "minor" — process repeats.
    //
    // Result: the grid stays readable at any zoom without ever looking
    // crowded. Values in cm; metric decade math.

    void draw_grid_lines(int vw, int vh, const EditorCamera& cam,
                          float spacing_world, const Engine::math::color& col) {
        const float ppu     = cam.pixels_per_unit * cam.zoom;
        const float spacing = spacing_world * ppu;
        if (spacing < 4.0f) return;   // would alias

        // Find world coords of viewport top-left and bottom-right.
        const auto tl = pixel_to_world(0, 0, cam, vw, vh);
        const auto br = pixel_to_world(static_cast<float>(vw),
                                        static_cast<float>(vh), cam, vw, vh);
        const float wx_min = std::min(tl.x, br.x);
        const float wx_max = std::max(tl.x, br.x);
        const float wy_min = std::min(tl.y, br.y);
        const float wy_max = std::max(tl.y, br.y);

        // Snap to nearest grid line outside the visible range.
        const float fx_first = std::floor(wx_min / spacing_world) * spacing_world;
        const float fx_last  = std::ceil (wx_max / spacing_world) * spacing_world;
        const float fy_first = std::floor(wy_min / spacing_world) * spacing_world;
        const float fy_last  = std::ceil (wy_max / spacing_world) * spacing_world;

        for (float x = fx_first; x <= fx_last; x += spacing_world) {
            gizmo_line({x, wy_min}, {x, wy_max}, col, 1.0f);
        }
        for (float y = fy_first; y <= fy_last; y += spacing_world) {
            gizmo_line({wx_min, y}, {wx_max, y}, col, 1.0f);
        }
    }

    void draw_auto_grid(int vw, int vh, const EditorCamera& cam) {
        const float ppu = cam.pixels_per_unit * cam.zoom;
        if (ppu <= 0.0f) return;

        // Pick the smallest power-of-10 (in cm) whose pixel spacing is
        // at least MIN_PX. log10(MIN_PX/ppu) gives us the right exponent.
        constexpr float MIN_PX = 8.0f;
        const float k = std::log10(MIN_PX / ppu);
        const float minor_world = std::pow(10.0f, std::ceil(k));
        const float major_world = minor_world * 10.0f;
        const float minor_px    = minor_world * ppu;

        // Minor alpha fades from 0 (at MIN_PX) to ~1 (at MIN_PX*8). Smooth
        // crossfade between decades - no popping when the level switches.
        // Both layers ride a 0.5 opacity ceiling so the grid is visible but
        // doesn't fight scene content.
        constexpr float OPACITY = 0.5f;
        const float minor_a = std::clamp((minor_px - MIN_PX) / (MIN_PX * 7.0f),
                                          0.0f, 1.0f) * 0.18f * OPACITY;
        const float major_a = 0.32f * OPACITY;

        if (minor_a > 0.005f) {
            draw_grid_lines(vw, vh, cam, minor_world,
                            Engine::math::color{1.0f, 1.0f, 1.0f, minor_a});
        }
        draw_grid_lines(vw, vh, cam, major_world,
                        Engine::math::color{1.0f, 1.0f, 1.0f, major_a});

        // World-origin axes — slightly brighter so origin stands out.
        const auto tl = pixel_to_world(0, 0, cam, vw, vh);
        const auto br = pixel_to_world(static_cast<float>(vw),
                                        static_cast<float>(vh), cam, vw, vh);
        const float wx_min = std::min(tl.x, br.x);
        const float wx_max = std::max(tl.x, br.x);
        const float wy_min = std::min(tl.y, br.y);
        const float wy_max = std::max(tl.y, br.y);
        // X-axis (horizontal): subtle red tint. Y-axis: subtle green.
        gizmo_line({wx_min, 0}, {wx_max, 0},
                   Engine::math::color{0.55f, 0.25f, 0.25f, 0.7f}, 1.5f);
        gizmo_line({0, wy_min}, {0, wy_max},
                   Engine::math::color{0.25f, 0.55f, 0.25f, 0.7f}, 1.5f);
    }

    // ---- Camera input (zoom-to-mouse, middle-drag pan) ---------------------

    void handle_camera_input(EditorState& s, ImVec2 image_min, ImVec2 image_size) {
        if (!ImGui::IsItemHovered()) return;
        auto& io = ImGui::GetIO();

        // Mouse pos in pixels relative to the scene image (top-left origin).
        const ImVec2 mp_abs = io.MousePos;
        const float  mpx = mp_abs.x - image_min.x;
        const float  mpy = mp_abs.y - image_min.y;
        const int    vw  = static_cast<int>(image_size.x);
        const int    vh  = static_cast<int>(image_size.y);

        // Zoom toward the cursor: capture the world point under the mouse,
        // change zoom, then nudge pan so that same world point ends up at
        // the same screen pixel after the zoom change. Standard trick.
        if (io.MouseWheel != 0.0f) {
            const auto world_before = pixel_to_world(mpx, mpy, s.camera, vw, vh);
            const float factor = std::pow(1.1f, io.MouseWheel);
            s.camera.zoom = std::clamp(s.camera.zoom * factor, 0.05f, 50.0f);
            const auto world_after  = pixel_to_world(mpx, mpy, s.camera, vw, vh);
            s.camera.pan.x += world_before.x - world_after.x;
            s.camera.pan.y += world_before.y - world_after.y;
        }

        // Middle-mouse drag = pan. Convert pixel delta to world cm.
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f)) {
            const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
            const float ppu = s.camera.pixels_per_unit * s.camera.zoom;
            if (ppu > 0.0f) {
                s.camera.pan.x -= d.x / ppu;
                s.camera.pan.y += d.y / ppu;
            }
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        }
    }

    // ---- Click-to-select ---------------------------------------------------

    // AABB hit-test on the unrotated bounds (pivot-corrected). Picks the
    // LAST entity that passes — which roughly approximates "topmost" for
    // the current draw order. Real depth-aware picking arrives with
    // RenderLayer/sorting.
    void handle_pick(EditorState& s, ImVec2 image_min, ImVec2 image_size) {
        if (!ImGui::IsItemClicked(ImGuiMouseButton_Left)) return;
        if (!s.world) return;

        const auto xform_id  = s.world->find_component_id("Transform2D");
        const auto sprite_id = s.world->find_component_id("Sprite");
        if (!xform_id || !sprite_id) return;

        const ImVec2 mp = ImGui::GetIO().MousePos;
        const float mpx = mp.x - image_min.x;
        const float mpy = mp.y - image_min.y;
        const int   vw  = static_cast<int>(image_size.x);
        const int   vh  = static_cast<int>(image_size.y);
        const auto pick_world = pixel_to_world(mpx, mpy, s.camera, vw, vh);

        struct PickCtx {
            Engine::math::vec2 pt;
            ecs::Entity hit;
            ecs::World*  world;
        };
        PickCtx ctx{pick_world, ecs::Entity{}, s.world};

        const ecs::ComponentId required[] = {xform_id, sprite_id};
        s.world->iterate_query(required, 2, nullptr, 0,
            +[](void* user, ecs::Entity e, void** cols, u32) {
                auto* c  = static_cast<PickCtx*>(user);
                auto* sp = static_cast<components::Sprite*>(cols[1]);

                // Composed world transform -- the renderer also composes
                // through parents, so picking against the local Transform2D
                // would let click-tests miss every parented sprite.
                const auto W = c->world->world_transform_2d(e);
                const float w = sp->size.x * W.scale_x;
                const float h = sp->size.y * W.scale_y;
                // Pivot offset in WORLD space (rotation ignored for v1 hit-test).
                const float pox = (0.5f - sp->pivot.x) * w;
                const float poy = (0.5f - sp->pivot.y) * h;
                const float cx  = W.pos_x + pox;
                const float cy  = W.pos_y + poy;
                const float hw  = w * 0.5f;
                const float hh  = h * 0.5f;

                if (c->pt.x >= cx - hw && c->pt.x <= cx + hw &&
                    c->pt.y >= cy - hh && c->pt.y <= cy + hh) {
                    c->hit = e;   // last match wins ≈ topmost in draw order
                }
            }, &ctx);

        // Click on empty space deselects. Click on a sprite selects it.
        // Selection from outside the Hierarchy panel triggers the
        // auto-scroll-into-view so the user can find the row.
        const bool changed = (s.selected_entity != ctx.hit);
        s.selected_entity = ctx.hit;
        if (changed && !ctx.hit.is_null()) s.hierarchy_scroll_to_selected = true;
    }

    // ---- Transform gizmos --------------------------------------------------
    //
    // Move (X arrow + Y arrow + center square for both-axis) and Rotate
    // (ring around the origin). Handles are sized in PIXELS so they stay a
    // constant on-screen size at any zoom — converted to world-cm by
    // dividing by the active pixels-per-unit. Drag math is symmetric:
    // capture entity state + grab world point on mouse-down, then on each
    // tick set entity = start + (mouse_world - grab_world).

    constexpr float HANDLE_LEN_PX     = 64.0f;
    constexpr float HANDLE_HIT_PX     = 8.0f;
    constexpr float CENTER_SQ_PX      = 14.0f;
    constexpr float CENTER_HIT_PX     = 9.0f;
    constexpr float RING_RADIUS_PX    = 88.0f;
    constexpr float RING_HIT_PX       = 7.0f;

    // Helper: read selected entity's Transform2D (or null).
    components::Transform2D* selected_transform(EditorState& s) {
        if (!s.world || !s.world->is_alive(s.selected_entity)) return nullptr;
        const auto xform_id = s.world->find_component_id("Transform2D");
        if (!xform_id) return nullptr;
        return static_cast<components::Transform2D*>(
            s.world->get_component(s.selected_entity, xform_id));
    }

    // Compose the selected entity's WORLD transform up the hierarchy. The
    // gizmo and drag manipulator both work in world space (where the user's
    // eyes see the entity); writing back into Transform2D.position requires
    // converting the world-space delta through the parent's inverse transform.
    Engine::ecs::World::WorldTransform2D selected_world_xform(EditorState& s) {
        if (!s.world || !s.world->is_alive(s.selected_entity))
            return {0,0,0,1,1};
        return s.world->world_transform_2d(s.selected_entity);
    }

    // Compute parent's WORLD transform (or identity if root). Used to project
    // world-space drag deltas back into the local space the entity stores.
    Engine::ecs::World::WorldTransform2D selected_parent_world_xform(EditorState& s) {
        Engine::ecs::World::WorldTransform2D id{0,0,0,1,1};
        if (!s.world) return id;
        const auto p = s.world->parent_of(s.selected_entity);
        if (p.is_null()) return id;
        return s.world->world_transform_2d(p);
    }

    // Project a world-space position to the parent's local space. For root
    // entities (parent == identity) this is a no-op; for children it inverts
    // the parent's TRS so writing the result into Transform2D.position
    // produces the correct world position after the renderer composes back.
    Engine::math::vec2 world_to_parent_local(
            Engine::ecs::World::WorldTransform2D pw,
            float wx, float wy) {
        const float dx = wx - pw.pos_x;
        const float dy = wy - pw.pos_y;
        const float c  = std::cos(-pw.rot);
        const float s  = std::sin(-pw.rot);
        const float lx = c * dx - s * dy;
        const float ly = s * dx + c * dy;
        const float sx = (std::fabs(pw.scale_x) > 1e-6f) ? pw.scale_x : 1.0f;
        const float sy = (std::fabs(pw.scale_y) > 1e-6f) ? pw.scale_y : 1.0f;
        return {lx / sx, ly / sy};
    }

    // Distance from point P to segment AB, all in screen pixels.
    float seg_distance_px(float px, float py,
                           float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay;
        const float len2 = dx * dx + dy * dy;
        if (len2 < 0.0001f) {
            const float ex = px - ax, ey = py - ay;
            return std::sqrt(ex * ex + ey * ey);
        }
        float t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
        const float qx = ax + t * dx;
        const float qy = ay + t * dy;
        const float ex = px - qx, ey = py - qy;
        return std::sqrt(ex * ex + ey * ey);
    }

    // Project entity origin to panel pixels (using base PPU, since input math
    // is in panel coordinates — not the supersampled RT space).
    void entity_to_panel(const components::Transform2D& tr, const EditorCamera& cam,
                         int vw, int vh, float& sx, float& sy) {
        const float ppu = cam.pixels_per_unit * cam.zoom;
        sx = (tr.position.x - cam.pan.x) * ppu + vw * 0.5f;
        sy = (-(tr.position.y - cam.pan.y)) * ppu + vh * 0.5f;
    }

    // Same as entity_to_panel but takes a WORLD position directly. Use this
    // when drawing UI on top of an entity that may be parented (children's
    // Transform2D.position is local, not world).
    void world_pos_to_panel(float wx, float wy, const EditorCamera& cam,
                             int vw, int vh, float& sx, float& sy) {
        const float ppu = cam.pixels_per_unit * cam.zoom;
        sx = (wx - cam.pan.x) * ppu + vw * 0.5f;
        sy = (-(wy - cam.pan.y)) * ppu + vh * 0.5f;
    }

    void draw_transform_gizmos(EditorState& s) {
        auto* tr = selected_transform(s);
        if (!tr) return;

        // PPU-aware world sizes for the handles.
        const float ppu  = s.camera.pixels_per_unit * s.camera.zoom;
        if (ppu <= 0.0f) return;
        const float len_w = HANDLE_LEN_PX  / ppu;
        const float ring_w = RING_RADIUS_PX / ppu;
        const float sq_w  = CENTER_SQ_PX   / ppu;

        // Place the gizmo at the entity's COMPOSED WORLD position, not its
        // local Transform2D.position. For root entities those are equal; for
        // children of a moved parent the gizmo would otherwise appear at the
        // origin offset and not on top of the visible sprite.
        const auto W = selected_world_xform(s);
        const Engine::math::vec2 origin{W.pos_x, W.pos_y};
        const Engine::math::vec2 x_tip{origin.x + len_w, origin.y};
        const Engine::math::vec2 y_tip{origin.x, origin.y + len_w};

        // Highlight the handle being dragged (or hovered, future work).
        const auto active = s.transform_drag.mode;
        const bool ax_x = (active == EditorState::GizmoMode::MoveX);
        const bool ax_y = (active == EditorState::GizmoMode::MoveY);
        const bool ax_c = (active == EditorState::GizmoMode::MoveBoth);
        const bool ax_r = (active == EditorState::GizmoMode::Rotate);

        const Engine::math::color red   = ax_x ? Engine::math::color{1.0f, 1.0f, 0.4f, 1.0f}
                                                : Engine::math::color{0.95f, 0.30f, 0.30f, 1.0f};
        const Engine::math::color green = ax_y ? Engine::math::color{1.0f, 1.0f, 0.4f, 1.0f}
                                                : Engine::math::color{0.30f, 0.95f, 0.30f, 1.0f};
        const Engine::math::color cyan  = ax_c ? Engine::math::color{1.0f, 1.0f, 0.4f, 1.0f}
                                                : Engine::math::color{0.40f, 0.85f, 0.95f, 1.0f};
        const Engine::math::color blue  = ax_r ? Engine::math::color{1.0f, 1.0f, 0.4f, 1.0f}
                                                : Engine::math::color{0.55f, 0.65f, 1.0f, 1.0f};

        // X / Y arrows.
        gizmo_arrow(origin, x_tip, red,   2.5f, 14.0f);
        gizmo_arrow(origin, y_tip, green, 2.5f, 14.0f);

        // Center "move both" square (filled).
        gizmo_rect_filled(origin, {sq_w, sq_w}, cyan);

        // Rotation ring.
        gizmo_circle(origin, ring_w, blue, 2.0f, 48);
        // Tick mark on the ring at the entity's current rotation, so users
        // can see where "0 rotation" was relative to current. Composed
        // world rotation matches what the renderer draws.
        const float ca = std::cos(W.rot);
        const float sa = std::sin(W.rot);
        gizmo_line(
            {origin.x + ring_w * ca,         origin.y + ring_w * sa},
            {origin.x + ring_w * 1.18f * ca, origin.y + ring_w * 1.18f * sa},
            blue, 2.5f);
    }

    // Hit-test all handles. Returns the matched mode (or None).
    EditorState::GizmoMode hit_test_handles(EditorState& s,
                                             float mpx, float mpy,
                                             int vw, int vh) {
        auto* tr = selected_transform(s);
        if (!tr) return EditorState::GizmoMode::None;

        const auto W = selected_world_xform(s);
        float ox, oy; world_pos_to_panel(W.pos_x, W.pos_y,
                                          s.camera, vw, vh, ox, oy);

        // Center square first (highest priority — it's smallest and innermost).
        if (std::fabs(mpx - ox) <= CENTER_HIT_PX &&
            std::fabs(mpy - oy) <= CENTER_HIT_PX) {
            return EditorState::GizmoMode::MoveBoth;
        }

        // X arrow shaft (panel x increases right; same as world +X).
        const float x_tip_px = ox + HANDLE_LEN_PX;
        if (seg_distance_px(mpx, mpy, ox, oy, x_tip_px, oy) <= HANDLE_HIT_PX) {
            return EditorState::GizmoMode::MoveX;
        }
        // Y arrow shaft (panel y goes DOWN; world +Y goes UP → tip is at oy - LEN).
        const float y_tip_px = oy - HANDLE_LEN_PX;
        if (seg_distance_px(mpx, mpy, ox, oy, ox, y_tip_px) <= HANDLE_HIT_PX) {
            return EditorState::GizmoMode::MoveY;
        }

        // Rotation ring (annulus test).
        const float dx = mpx - ox, dy = mpy - oy;
        const float r = std::sqrt(dx * dx + dy * dy);
        if (std::fabs(r - RING_RADIUS_PX) <= RING_HIT_PX) {
            return EditorState::GizmoMode::Rotate;
        }

        return EditorState::GizmoMode::None;
    }

    // Returns true if the gizmo system is currently consuming input
    // (so panel_scene skips camera input + picking for this frame).
    bool handle_transform_gizmos(EditorState& s,
                                  ImVec2 image_min, ImVec2 image_size) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const float mpx = mp.x - image_min.x;
        const float mpy = mp.y - image_min.y;
        const int   vw  = static_cast<int>(image_size.x);
        const int   vh  = static_cast<int>(image_size.y);

        // Mouse-up always ends a drag, regardless of where the cursor is.
        if (s.transform_drag.mode != EditorState::GizmoMode::None &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            s.transform_drag.mode = EditorState::GizmoMode::None;
            undo_commit(s, "Transform drag");
            return true;
        }

        // Drag in progress: update entity each frame from current mouse pos.
        if (s.transform_drag.mode != EditorState::GizmoMode::None) {
            auto* tr = selected_transform(s);
            if (!tr) {
                s.transform_drag.mode = EditorState::GizmoMode::None;
                return true;
            }
            const auto mw = pixel_to_world(mpx, mpy, s.camera, vw, vh);

            // The drag math runs in WORLD space (where the user's eyes are):
            // compute the new world position the entity should sit at, then
            // project through the parent's inverse transform to get the
            // local Transform2D.position we need to store. For root entities
            // (parent identity) this is a no-op; for children it stops the
            // gizmo from feeling "offset" — moving the gizmo to (10, 5)
            // actually puts the entity AT (10, 5) regardless of parent.
            //
            // entity_start_pos is captured in WORLD space at drag start
            // (see the click handler below). grab_world is also world-space.
            const auto pw = selected_parent_world_xform(s);

            switch (s.transform_drag.mode) {
                case EditorState::GizmoMode::MoveX: {
                    const float new_world_x = s.transform_drag.entity_start_pos.x
                                            + (mw.x - s.transform_drag.grab_world.x);
                    const float new_world_y = s.transform_drag.entity_start_pos.y;
                    const auto loc = world_to_parent_local(pw, new_world_x, new_world_y);
                    tr->position.x = loc.x;
                    tr->position.y = loc.y;
                    break;
                }
                case EditorState::GizmoMode::MoveY: {
                    const float new_world_x = s.transform_drag.entity_start_pos.x;
                    const float new_world_y = s.transform_drag.entity_start_pos.y
                                            + (mw.y - s.transform_drag.grab_world.y);
                    const auto loc = world_to_parent_local(pw, new_world_x, new_world_y);
                    tr->position.x = loc.x;
                    tr->position.y = loc.y;
                    break;
                }
                case EditorState::GizmoMode::MoveBoth: {
                    const float new_world_x = s.transform_drag.entity_start_pos.x
                                            + (mw.x - s.transform_drag.grab_world.x);
                    const float new_world_y = s.transform_drag.entity_start_pos.y
                                            + (mw.y - s.transform_drag.grab_world.y);
                    const auto loc = world_to_parent_local(pw, new_world_x, new_world_y);
                    tr->position.x = loc.x;
                    tr->position.y = loc.y;
                    break;
                }
                case EditorState::GizmoMode::Rotate: {
                    // Rotation pivot is the entity's WORLD origin, not its
                    // local one (which may be offset by the parent chain).
                    const auto W = selected_world_xform(s);
                    const float ang = std::atan2(mw.y - W.pos_y, mw.x - W.pos_x);
                    const float new_world_rot = s.transform_drag.entity_start_rot
                                              + (ang - s.transform_drag.grab_angle);
                    // local_rot = world_rot - parent_world_rot
                    tr->rotation = new_world_rot - pw.rot;
                    break;
                }
                case EditorState::GizmoMode::None: break;
            }
            return true;   // we consume input while dragging
        }

        // Not dragging — only start a drag if mouse is hovered, mouse-down,
        // and on a handle.
        if (!ImGui::IsItemHovered()) return false;
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;

        const auto hit = hit_test_handles(s, mpx, mpy, vw, vh);
        if (hit == EditorState::GizmoMode::None) return false;

        auto* tr = selected_transform(s);
        if (!tr) return false;

        // Capture entity_start_pos / entity_start_rot in WORLD space so the
        // drag math is uniform regardless of parenting. The drag handler
        // converts back to local through the parent's inverse transform
        // when writing to Transform2D.position / rotation.
        const auto W0 = selected_world_xform(s);
        s.transform_drag.mode             = hit;
        s.transform_drag.grab_world       = pixel_to_world(mpx, mpy, s.camera, vw, vh);
        s.transform_drag.entity_start_pos = {W0.pos_x, W0.pos_y};
        s.transform_drag.entity_start_rot = W0.rot;
        s.transform_drag.grab_angle = std::atan2(
            s.transform_drag.grab_world.y - W0.pos_y,
            s.transform_drag.grab_world.x - W0.pos_x);
        undo_begin(s);
        return true;   // consumed the mouse-down — picking shouldn't fire
    }

    // ---- Camera viewport gizmo --------------------------------------------
    //
    // When the selected entity has a Camera2D, draw a rectangle showing
    // exactly what that camera sees in the Game viewport. Width is computed
    // from the Game RT's aspect ratio (last frame's), so the rect always
    // matches what the player will actually see — drag the camera around in
    // the Scene view and the rect updates live.

    void draw_camera_viewport_gizmo(EditorState& s) {
        if (!s.world || !s.world->is_alive(s.selected_entity)) return;

        const auto cam_id   = s.world->find_component_id("Camera2D");
        const auto xform_id = s.world->find_component_id("Transform2D");
        if (!cam_id || !xform_id) return;

        auto* cam = static_cast<components::Camera2D*>(
            s.world->get_component(s.selected_entity, cam_id));
        auto* tr  = static_cast<components::Transform2D*>(
            s.world->get_component(s.selected_entity, xform_id));
        if (!cam || !tr) return;

        // Aspect ratio comes from the Game RT (or 16:9 fallback if it
        // hasn't been sized yet). Pixels-per-unit and supersample don't
        // change the aspect, so the raw RT dims are fine.
        const float aspect = (s.game_rt_w > 0 && s.game_rt_h > 0)
            ? static_cast<float>(s.game_rt_w) / static_cast<float>(s.game_rt_h)
            : (16.0f / 9.0f);

        const float h = cam->ortho_size;
        const float w = h * aspect;

        const Engine::math::vec2 center{tr->position.x, tr->position.y};
        const Engine::math::color frame    {0.65f, 0.85f, 1.0f, 0.85f};
        const Engine::math::color frame_off{0.65f, 0.85f, 1.0f, 0.30f};

        // Just the outline. Active cameras show solid; disabled (is_active
        // false) cameras still get a dim frame so you can see where they're
        // pointed without thinking they're invisible.
        gizmo_rect_outline(center, {w, h},
                           cam->is_active ? frame : frame_off, 2.0f);
    }

    // ---- Collider gizmos --------------------------------------------------
    // Draw a thin outline around every Box / Circle collider in the world so
    // designers can see physics bounds without entering Play. Sensors render
    // in amber; solid colliders in green. Selected entity gets a brighter
    // accent so the outline doesn't compete with the selection-rect gizmo.
    void draw_collider_gizmos(EditorState& s) {
        if (!s.world) return;
        const auto xform_id  = s.world->find_component_id("Transform2D");
        const auto box_id    = s.world->find_component_id("BoxCollider");
        const auto circle_id = s.world->find_component_id("CircleCollider");
        if (!xform_id) return;

        struct Ctx {
            ecs::Entity   selected;
            float         th;
            ecs::World*   world;
        };
        Ctx ctx{ s.selected_entity, 1.5f, s.world };

        // Box colliders: rotated rect outline at world(transform) + offset.
        // The renderer composes through parents, so collider gizmos must
        // do the same -- a parented collider would otherwise draw at its
        // local origin and visually drift away from the sprite it
        // protects.
        if (box_id) {
            const ecs::ComponentId required[] = {xform_id, box_id};
            s.world->iterate_query(required, 2, nullptr, 0,
                +[](void* user, ecs::Entity e, void** cols, u32) {
                    auto* c  = static_cast<Ctx*>(user);
                    auto* bc = static_cast<components::BoxCollider*>(cols[1]);
                    if (!bc) return;
                    const auto W = c->world->world_transform_2d(e);
                    const float ca = std::cos(W.rot);
                    const float sa = std::sin(W.rot);
                    const Engine::math::vec2 center{
                        W.pos_x + bc->offset.x * ca - bc->offset.y * sa,
                        W.pos_y + bc->offset.x * sa + bc->offset.y * ca
                    };
                    const Engine::math::vec2 size{
                        bc->half_extents.x * 2.0f * W.scale_x,
                        bc->half_extents.y * 2.0f * W.scale_y
                    };
                    const bool sel = (e == c->selected);
                    Engine::math::color col = bc->is_sensor
                        ? Engine::math::color{0.30f, 0.70f, 1.0f, sel ? 1.0f : 0.65f}
                        : Engine::math::color{0.40f, 0.95f, 0.55f, sel ? 1.0f : 0.65f};
                    const float th = sel ? 2.5f : c->th;
                    gizmo_rect_outline(center, size, col, th, W.rot);
                }, &ctx);
        }

        if (circle_id) {
            const ecs::ComponentId required[] = {xform_id, circle_id};
            s.world->iterate_query(required, 2, nullptr, 0,
                +[](void* user, ecs::Entity e, void** cols, u32) {
                    auto* c  = static_cast<Ctx*>(user);
                    auto* cc = static_cast<components::CircleCollider*>(cols[1]);
                    if (!cc) return;
                    const auto W = c->world->world_transform_2d(e);
                    const float ca = std::cos(W.rot);
                    const float sa = std::sin(W.rot);
                    const Engine::math::vec2 center{
                        W.pos_x + cc->offset.x * ca - cc->offset.y * sa,
                        W.pos_y + cc->offset.x * sa + cc->offset.y * ca
                    };
                    const float scale_max = std::max(std::fabs(W.scale_x),
                                                      std::fabs(W.scale_y));
                    const float r = cc->radius * scale_max;
                    const bool sel = (e == c->selected);
                    Engine::math::color col = cc->is_sensor
                        ? Engine::math::color{1.0f, 0.85f, 0.30f, sel ? 1.0f : 0.65f}
                        : Engine::math::color{0.40f, 0.95f, 0.55f, sel ? 1.0f : 0.65f};
                    const float th = sel ? 2.5f : c->th;
                    gizmo_circle(center, r, col, th, 32);
                }, &ctx);
        }
    }

    // ---- Collider edit-in-scene handles ------------------------------------
    // Drawn only when:
    //   - Selected entity has a BoxCollider or CircleCollider
    //   - That collider has `edit_in_scene == 1` (toggle in inspector)
    // Box: 4 disk handles at the middle of each edge (top/bottom/left/right).
    // Circle: 1 disk handle at the right edge of the circle.
    // ALT held during drag mirrors the change to the opposite side
    // (resize from center). Without ALT the opposite side stays put.

    constexpr float COLL_HANDLE_PX     = 6.0f;     // visible disk radius
    constexpr float COLL_HANDLE_HIT_PX = 9.0f;     // click hit radius

    // Compute the four mid-edge handle positions for a BoxCollider in panel
    // pixel coordinates. Returns false if the prerequisites aren't met.
    bool box_handle_positions_panel(EditorState& s,
                                     const Engine::ecs::World::WorldTransform2D& W,
                                     const components::BoxCollider& bc,
                                     int vw, int vh,
                                     float out_xy[4][2])
    {
        const float ppu = s.camera.pixels_per_unit * s.camera.zoom;
        if (ppu <= 0.0f) return false;
        const float ca = std::cos(W.rot);
        const float sa = std::sin(W.rot);
        const Engine::math::vec2 center_w{
            W.pos_x + bc.offset.x * ca - bc.offset.y * sa,
            W.pos_y + bc.offset.x * sa + bc.offset.y * ca
        };
        const float hwx_world = bc.half_extents.x * std::fabs(W.scale_x);
        const float hwy_world = bc.half_extents.y * std::fabs(W.scale_y);
        // Mid-edge offsets in BOX-LOCAL space (no rotation). Order matches
        // ColliderHandle: BoxLeft, BoxRight, BoxTop, BoxBottom (1..4).
        const float local[4][2] = {
            {-hwx_world,  0.0f},   // BoxLeft
            { hwx_world,  0.0f},   // BoxRight
            { 0.0f,  hwy_world},   // BoxTop      (world +Y is up)
            { 0.0f, -hwy_world},   // BoxBottom
        };
        for (int i = 0; i < 4; ++i) {
            const float wx = center_w.x + local[i][0] * ca - local[i][1] * sa;
            const float wy = center_w.y + local[i][0] * sa + local[i][1] * ca;
            out_xy[i][0] = (wx - s.camera.pan.x) * ppu + vw * 0.5f;
            out_xy[i][1] = (-(wy - s.camera.pan.y)) * ppu + vh * 0.5f;
        }
        return true;
    }

    // Single radius handle for a circle collider at the right edge in
    // panel pixels.
    bool circle_handle_position_panel(EditorState& s,
                                       const Engine::ecs::World::WorldTransform2D& W,
                                       const components::CircleCollider& cc,
                                       int vw, int vh,
                                       float& out_x, float& out_y)
    {
        const float ppu = s.camera.pixels_per_unit * s.camera.zoom;
        if (ppu <= 0.0f) return false;
        const float ca = std::cos(W.rot);
        const float sa = std::sin(W.rot);
        const Engine::math::vec2 center_w{
            W.pos_x + cc.offset.x * ca - cc.offset.y * sa,
            W.pos_y + cc.offset.x * sa + cc.offset.y * ca
        };
        const float scale_max = std::max(std::fabs(W.scale_x),
                                          std::fabs(W.scale_y));
        const float r_world = cc.radius * scale_max;
        // Handle sits at center + (radius, 0) in box-local space.
        const float wx = center_w.x + r_world * ca;
        const float wy = center_w.y + r_world * sa;
        out_x = (wx - s.camera.pan.x) * ppu + vw * 0.5f;
        out_y = (-(wy - s.camera.pan.y)) * ppu + vh * 0.5f;
        return true;
    }

    void draw_collider_handles(EditorState& s) {
        if (!s.world || !s.world->is_alive(s.selected_entity)) return;
        const auto xform_id  = s.world->find_component_id("Transform2D");
        const auto box_id    = s.world->find_component_id("BoxCollider");
        const auto circle_id = s.world->find_component_id("CircleCollider");
        if (!xform_id) return;
        auto* tr = static_cast<components::Transform2D*>(
            s.world->get_component(s.selected_entity, xform_id));
        if (!tr) return;

        const float ppu = s.camera.pixels_per_unit * s.camera.zoom;
        if (ppu <= 0.0f) return;
        const float r_world = COLL_HANDLE_PX / ppu;

        const Engine::math::color col_idle  {0.95f, 0.95f, 0.30f, 0.95f};  // amber
        const Engine::math::color col_hot   {1.0f,  1.0f,  0.40f, 1.0f};
        const auto active_handle = s.collider_drag.handle;

        // Use composed world transform so collider edit handles ride
        // along with the parent's hierarchy (matches the renderer + the
        // outline gizmos drawn by draw_collider_gizmos).
        const auto W = s.world->world_transform_2d(s.selected_entity);
        if (box_id) {
            auto* bc = static_cast<components::BoxCollider*>(
                s.world->get_component(s.selected_entity, box_id));
            if (bc && bc->edit_in_scene) {
                const float ca = std::cos(W.rot);
                const float sa = std::sin(W.rot);
                const Engine::math::vec2 center_w{
                    W.pos_x + bc->offset.x * ca - bc->offset.y * sa,
                    W.pos_y + bc->offset.x * sa + bc->offset.y * ca
                };
                const float hwx = bc->half_extents.x * std::fabs(W.scale_x);
                const float hwy = bc->half_extents.y * std::fabs(W.scale_y);
                const float lx[4] = {-hwx,  hwx, 0.0f, 0.0f};
                const float ly[4] = { 0.0f, 0.0f, hwy, -hwy};
                const EditorState::ColliderHandle ids[4] = {
                    EditorState::ColliderHandle::BoxLeft,
                    EditorState::ColliderHandle::BoxRight,
                    EditorState::ColliderHandle::BoxTop,
                    EditorState::ColliderHandle::BoxBottom,
                };
                for (int i = 0; i < 4; ++i) {
                    const Engine::math::vec2 hp{
                        center_w.x + lx[i] * ca - ly[i] * sa,
                        center_w.y + lx[i] * sa + ly[i] * ca
                    };
                    const auto col = (active_handle == ids[i]) ? col_hot : col_idle;
                    gizmo_disk(hp, r_world, col, 16);
                }
            }
        }
        if (circle_id) {
            auto* cc = static_cast<components::CircleCollider*>(
                s.world->get_component(s.selected_entity, circle_id));
            if (cc && cc->edit_in_scene) {
                const float ca = std::cos(W.rot);
                const float sa = std::sin(W.rot);
                const Engine::math::vec2 center_w{
                    W.pos_x + cc->offset.x * ca - cc->offset.y * sa,
                    W.pos_y + cc->offset.x * sa + cc->offset.y * ca
                };
                const float scale_max = std::max(std::fabs(W.scale_x),
                                                  std::fabs(W.scale_y));
                const float r = cc->radius * scale_max;
                const Engine::math::vec2 hp{
                    center_w.x + r * ca,
                    center_w.y + r * sa
                };
                const auto col = (active_handle == EditorState::ColliderHandle::CircleRadius)
                    ? col_hot : col_idle;
                gizmo_disk(hp, r_world, col, 16);
            }
        }
    }

    // Hit-test the collider handles for the selected entity. Returns the
    // matched handle (None if no hit). Sets `out_is_circle` to true when
    // the hit was the circle radius handle.
    EditorState::ColliderHandle hit_test_collider_handles(
        EditorState& s, float mpx, float mpy, int vw, int vh, bool& out_is_circle)
    {
        out_is_circle = false;
        if (!s.world || !s.world->is_alive(s.selected_entity))
            return EditorState::ColliderHandle::None;
        const auto xform_id  = s.world->find_component_id("Transform2D");
        const auto box_id    = s.world->find_component_id("BoxCollider");
        const auto circle_id = s.world->find_component_id("CircleCollider");
        if (!xform_id) return EditorState::ColliderHandle::None;
        auto* tr = static_cast<components::Transform2D*>(
            s.world->get_component(s.selected_entity, xform_id));
        if (!tr) return EditorState::ColliderHandle::None;

        const auto W_hit = s.world->world_transform_2d(s.selected_entity);
        if (box_id) {
            auto* bc = static_cast<components::BoxCollider*>(
                s.world->get_component(s.selected_entity, box_id));
            if (bc && bc->edit_in_scene) {
                float pos[4][2];
                if (box_handle_positions_panel(s, W_hit, *bc, vw, vh, pos)) {
                    const EditorState::ColliderHandle ids[4] = {
                        EditorState::ColliderHandle::BoxLeft,
                        EditorState::ColliderHandle::BoxRight,
                        EditorState::ColliderHandle::BoxTop,
                        EditorState::ColliderHandle::BoxBottom,
                    };
                    for (int i = 0; i < 4; ++i) {
                        const float dx = mpx - pos[i][0];
                        const float dy = mpy - pos[i][1];
                        if (dx*dx + dy*dy <= COLL_HANDLE_HIT_PX*COLL_HANDLE_HIT_PX)
                            return ids[i];
                    }
                }
            }
        }
        if (circle_id) {
            auto* cc = static_cast<components::CircleCollider*>(
                s.world->get_component(s.selected_entity, circle_id));
            if (cc && cc->edit_in_scene) {
                float hx, hy;
                if (circle_handle_position_panel(s, W_hit, *cc, vw, vh, hx, hy)) {
                    const float dx = mpx - hx;
                    const float dy = mpy - hy;
                    if (dx*dx + dy*dy <= COLL_HANDLE_HIT_PX*COLL_HANDLE_HIT_PX) {
                        out_is_circle = true;
                        return EditorState::ColliderHandle::CircleRadius;
                    }
                }
            }
        }
        return EditorState::ColliderHandle::None;
    }

    // Drag handler. Returns true if input was consumed (so picking + camera
    // input + transform-gizmo input all skip this frame). Mirrors the
    // shape of `handle_transform_gizmos` so the wiring at the call site
    // is symmetric.
    bool handle_collider_gizmos(EditorState& s,
                                  ImVec2 image_min, ImVec2 image_size) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const float mpx = mp.x - image_min.x;
        const float mpy = mp.y - image_min.y;
        const int   vw  = static_cast<int>(image_size.x);
        const int   vh  = static_cast<int>(image_size.y);

        // End drag on mouse-up.
        if (s.collider_drag.handle != EditorState::ColliderHandle::None &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            s.collider_drag.handle = EditorState::ColliderHandle::None;
            undo_commit(s, "Collider edit");
            return true;
        }

        // Update during drag.
        if (s.collider_drag.handle != EditorState::ColliderHandle::None) {
            if (!s.world || !s.world->is_alive(s.collider_drag.entity)) {
                s.collider_drag.handle = EditorState::ColliderHandle::None;
                return true;
            }
            const auto xform_id  = s.world->find_component_id("Transform2D");
            if (!xform_id) {
                s.collider_drag.handle = EditorState::ColliderHandle::None;
                return true;
            }
            auto* tr = static_cast<components::Transform2D*>(
                s.world->get_component(s.collider_drag.entity, xform_id));
            if (!tr) {
                s.collider_drag.handle = EditorState::ColliderHandle::None;
                return true;
            }
            const auto mw = pixel_to_world(mpx, mpy, s.camera, vw, vh);
            // Drag math runs in the BOX-LOCAL frame -- to get there we
            // un-rotate around the entity's COMPOSED world origin (so a
            // collider on a parented entity rotates with the parent
            // chain) and then divide out the composed world scale below.
            const auto Wd = s.world->world_transform_2d(s.collider_drag.entity);
            const float ca = std::cos(Wd.rot);
            const float sa = std::sin(Wd.rot);
            auto world_to_local = [&](Engine::math::vec2 w) {
                const float dx = w.x - Wd.pos_x;
                const float dy = w.y - Wd.pos_y;
                return Engine::math::vec2{ dx * ca + dy * sa,
                                           -dx * sa + dy * ca };
            };
            const auto cur_l  = world_to_local(mw);
            const auto grab_l = world_to_local(s.collider_drag.grab_world);
            const float dlx = cur_l.x - grab_l.x;
            const float dly = cur_l.y - grab_l.y;
            const bool alt_held = ImGui::GetIO().KeyAlt;

            // half_extents is multiplied by COMPOSED world scale to produce
            // world size, so when computing how much a drag changes
            // half_extents we divide by composed scale (not just the
            // entity's local scale).
            const float invsx = (std::fabs(Wd.scale_x) > 1e-6f)
                ? 1.0f / std::fabs(Wd.scale_x) : 1.0f;
            const float invsy = (std::fabs(Wd.scale_y) > 1e-6f)
                ? 1.0f / std::fabs(Wd.scale_y) : 1.0f;

            if (s.collider_drag.is_circle) {
                const auto cir_id = s.world->find_component_id("CircleCollider");
                if (cir_id) {
                    auto* cc = static_cast<components::CircleCollider*>(
                        s.world->get_component(s.collider_drag.entity, cir_id));
                    if (cc) {
                        // Circle: drag distance from offset center sets radius.
                        // Compute distance from offset (in local) to current
                        // mouse position (in local).
                        const float odx = cur_l.x - s.collider_drag.start_offset.x;
                        const float ody = cur_l.y - s.collider_drag.start_offset.y;
                        const float scale_max = std::max(std::fabs(Wd.scale_x),
                                                          std::fabs(Wd.scale_y));
                        const float inv_scale_max = (scale_max > 1e-6f) ? 1.0f / scale_max : 1.0f;
                        float new_r = std::sqrt(odx*odx + ody*ody) * inv_scale_max;
                        if (new_r < 0.01f) new_r = 0.01f;
                        cc->radius = new_r;
                    }
                }
            } else {
                const auto box_id = s.world->find_component_id("BoxCollider");
                if (box_id) {
                    auto* bc = static_cast<components::BoxCollider*>(
                        s.world->get_component(s.collider_drag.entity, box_id));
                    if (bc) {
                        // Without ALT: dragging an edge moves only that edge;
                        // the opposite edge stays in place. half_extents
                        // changes by half the drag, offset shifts by the
                        // other half so the OPPOSITE edge stays put.
                        // With ALT: symmetric. half_extents changes by the
                        // full drag amount on that axis, offset stays.
                        const auto& sh = s.collider_drag.start_half_extents;
                        const auto& so = s.collider_drag.start_offset;
                        switch (s.collider_drag.handle) {
                            case EditorState::ColliderHandle::BoxLeft: {
                                // Dragging left handle right by +d shrinks
                                // half_w by d/2, moves offset by +d/2.
                                // ALT: shrink half_w by d, offset stays.
                                const float d = dlx * invsx;
                                if (alt_held) {
                                    bc->half_extents.x = std::max(0.01f, sh.x - d);
                                    bc->offset.x       = so.x;
                                } else {
                                    bc->half_extents.x = std::max(0.01f, sh.x - d * 0.5f);
                                    bc->offset.x       = so.x + d * 0.5f;
                                }
                                bc->half_extents.y = sh.y;
                                bc->offset.y       = so.y;
                                break;
                            }
                            case EditorState::ColliderHandle::BoxRight: {
                                const float d = dlx * invsx;
                                if (alt_held) {
                                    bc->half_extents.x = std::max(0.01f, sh.x + d);
                                    bc->offset.x       = so.x;
                                } else {
                                    bc->half_extents.x = std::max(0.01f, sh.x + d * 0.5f);
                                    bc->offset.x       = so.x + d * 0.5f;
                                }
                                bc->half_extents.y = sh.y;
                                bc->offset.y       = so.y;
                                break;
                            }
                            case EditorState::ColliderHandle::BoxTop: {
                                const float d = dly * invsy;
                                if (alt_held) {
                                    bc->half_extents.y = std::max(0.01f, sh.y + d);
                                    bc->offset.y       = so.y;
                                } else {
                                    bc->half_extents.y = std::max(0.01f, sh.y + d * 0.5f);
                                    bc->offset.y       = so.y + d * 0.5f;
                                }
                                bc->half_extents.x = sh.x;
                                bc->offset.x       = so.x;
                                break;
                            }
                            case EditorState::ColliderHandle::BoxBottom: {
                                const float d = dly * invsy;
                                if (alt_held) {
                                    bc->half_extents.y = std::max(0.01f, sh.y - d);
                                    bc->offset.y       = so.y;
                                } else {
                                    bc->half_extents.y = std::max(0.01f, sh.y - d * 0.5f);
                                    bc->offset.y       = so.y + d * 0.5f;
                                }
                                bc->half_extents.x = sh.x;
                                bc->offset.x       = so.x;
                                break;
                            }
                            default: break;
                        }
                        // Stale shape - the physics module will rebuild on
                        // the next pre_step.
                        bc->_shape_handle = 0;
                    }
                }
            }
            return true;   // consume input while dragging
        }

        // Not dragging — only start a drag on hover + click on a handle.
        if (!ImGui::IsItemHovered()) return false;
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;

        bool is_circle = false;
        const auto hit = hit_test_collider_handles(s, mpx, mpy, vw, vh, is_circle);
        if (hit == EditorState::ColliderHandle::None) return false;

        const auto xform_id  = s.world->find_component_id("Transform2D");
        if (!xform_id) return false;
        auto* tr = static_cast<components::Transform2D*>(
            s.world->get_component(s.selected_entity, xform_id));
        if (!tr) return false;

        s.collider_drag.handle      = hit;
        s.collider_drag.entity      = s.selected_entity;
        s.collider_drag.is_circle   = is_circle;
        s.collider_drag.grab_world  = pixel_to_world(mpx, mpy, s.camera, vw, vh);
        s.collider_drag.alt_at_grab = ImGui::GetIO().KeyAlt;
        undo_begin(s);
        if (is_circle) {
            const auto cir_id = s.world->find_component_id("CircleCollider");
            auto* cc = static_cast<components::CircleCollider*>(
                s.world->get_component(s.selected_entity, cir_id));
            if (cc) {
                s.collider_drag.start_radius = cc->radius;
                s.collider_drag.start_offset = cc->offset;
            }
        } else {
            const auto box_id = s.world->find_component_id("BoxCollider");
            auto* bc = static_cast<components::BoxCollider*>(
                s.world->get_component(s.selected_entity, box_id));
            if (bc) {
                s.collider_drag.start_half_extents = bc->half_extents;
                s.collider_drag.start_offset       = bc->offset;
            }
        }
        return true;
    }

    // ---- Selection outline gizmo ------------------------------------------

    void draw_selection_outline(EditorState& s) {
        if (!s.world || !s.world->is_alive(s.selected_entity)) return;

        const auto xform_id  = s.world->find_component_id("Transform2D");
        const auto sprite_id = s.world->find_component_id("Sprite");
        if (!xform_id || !sprite_id) return;

        auto* sp = static_cast<components::Sprite*>(
            s.world->get_component(s.selected_entity, sprite_id));
        if (!sp) return;

        // Use the COMPOSED world transform so the outline (and the origin
        // dot) line up with where the sprite is drawn -- the renderer also
        // composes through parents, so reading the local Transform2D here
        // would draw the outline at the local-space origin instead.
        const auto W = s.world->world_transform_2d(s.selected_entity);

        const Engine::math::vec2 size{
            sp->size.x * W.scale_x,
            sp->size.y * W.scale_y
        };
        // Pivot-aware center in world space (also rotates with the sprite,
        // so apply the rotation to the local pivot offset).
        const float ca = std::cos(-W.rot);
        const float sa = std::sin(-W.rot);
        const float pox = (0.5f - sp->pivot.x) * size.x;
        const float poy = (0.5f - sp->pivot.y) * size.y;
        const Engine::math::vec2 center{
            W.pos_x + pox * ca - poy * sa,
            W.pos_y + pox * sa + poy * ca
        };

        const Engine::math::color sel{1.0f, 0.78f, 0.20f, 1.0f};   // amber
        gizmo_rect_outline(center, size, sel, 2.0f, W.rot);

        // Origin handle dot at the entity's WORLD origin so it lines up
        // with the gizmo's center square (both use composed world pos).
        gizmo_disk(Engine::math::vec2{W.pos_x, W.pos_y},
                   3.0f / (s.camera.pixels_per_unit * s.camera.zoom),
                   sel, 12);
    }
}

void draw_scene_panel(EditorState& s) {
    if (!s.show_scene) return;

    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Scene", &s.show_scene)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int want_w = static_cast<int>(avail.x);
        const int want_h = static_cast<int>(avail.y);

        if (r && want_w > 4 && want_h > 4) {
            // 2x supersample for cheap edge AA. We render at 2× the displayed
            // pixel count and let ImGui's image sampler downsample to the
            // panel size. Costs 4x fillrate but takes zero infrastructure
            // changes — proper MSAA can replace this later if perf bites.
            constexpr int SS = 2;
            const int rt_w = want_w * SS;
            const int rt_h = want_h * SS;

            if (s.scene_rt == 0) {
                s.scene_rt   = r->create_render_target(r, rt_w, rt_h);
                s.scene_rt_w = rt_w;
                s.scene_rt_h = rt_h;
            } else if (rt_w != s.scene_rt_w || rt_h != s.scene_rt_h) {
                if (r->resize_render_target(r, s.scene_rt, rt_w, rt_h)) {
                    s.scene_rt_w = rt_w;
                    s.scene_rt_h = rt_h;
                }
            }

            // Scale ppu by SS so the 2x-resolution RT renders at the same
            // visual size after ImGui downsamples it.
            const float render_ppu = s.camera.pixels_per_unit * SS;

            r->bind_render_target(r, s.scene_rt);
            r->begin_frame(r, 0.04f, 0.05f, 0.07f, 1.0f);
            draw_background(r, s.scene_rt_w, s.scene_rt_h);

            // ---- Grid pass (gizmos) ----
            gizmos_begin(s.scene_rt_w, s.scene_rt_h,
                         s.camera.pan, s.camera.zoom, render_ppu);
            draw_auto_grid(s.scene_rt_w, s.scene_rt_h, s.camera);
            gizmos_flush(r);

            // ---- Camera publish + entity render pass ----
            if (auto* sr = Engine::services()) {
                if (auto* cs = static_cast<IRenderCamera_v1*>(sr->get_service(
                        ZUES_SERVICE_RENDER_CAMERA, ZUES_SERVICE_RENDER_CAMERA_VERSION))) {
                    ZuesRenderCamera cam{};
                    cam.pan_x           = s.camera.pan.x;
                    cam.pan_y           = s.camera.pan.y;
                    cam.zoom            = s.camera.zoom;
                    cam.rotation        = 0.0f;
                    cam.pixels_per_unit = render_ppu;   // SS-scaled for RT
                    cam.viewport_w      = s.scene_rt_w;
                    cam.viewport_h      = s.scene_rt_h;
                    cam.sort_mode       = ZUES_SORT_ORDER_ONLY;  // Scene view is god-view
                    cs->set_active(cs, &cam);
                }
            }
            // Begin a gizmo "engine pass" so subsystems running inside
            // tick_phase(Render) -- particles, animator, audio later --
            // can publish via IDebugDraw_v1 into the same queue. The
            // overlay-pass `gizmos_begin` below would otherwise wipe
            // anything they queued.
            gizmos_begin(s.scene_rt_w, s.scene_rt_h,
                         s.camera.pan, s.camera.zoom, render_ppu);
            if (s.world) s.world->tick_phase(ecs::Phase::Render, s.last_dt);
            gizmos_flush(r);

            // ---- Overlay pass (selection outline + camera + handles) ----
            gizmos_begin(s.scene_rt_w, s.scene_rt_h,
                         s.camera.pan, s.camera.zoom, render_ppu);
            draw_camera_viewport_gizmo(s);
            draw_collider_gizmos(s);
            draw_selection_outline(s);
            draw_transform_gizmos(s);
            draw_collider_handles(s);
            gizmos_flush(r);

            r->end_frame(r);
            r->bind_render_target(r, 0);

            // ---- Display + input handling ----
            const ImVec2 image_min = ImGui::GetCursorScreenPos();
            const auto tex = r->get_render_target_texture(r, s.scene_rt);
            if (tex != 0) {
                ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                             avail, ImVec2(0, 1), ImVec2(1, 0));

                // Drag-drop target for Asset Browser PNG drops. Spawns a
                // sprite entity at the cursor's world position. Wrapped in
                // BeginDragDropTarget so it only intercepts drops on the
                // scene image itself; clicks elsewhere are unaffected.
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                        const char* path = static_cast<const char*>(payload->Data);
                        const ImVec2 mp  = ImGui::GetIO().MousePos;
                        const float mpx = mp.x - image_min.x;
                        const float mpy = mp.y - image_min.y;
                        const auto world_pos = pixel_to_world(
                            mpx, mpy, s.camera,
                            static_cast<int>(avail.x),
                            static_cast<int>(avail.y));
                        // Route by extension: .zprefab instantiates a prefab,
                        // anything image-like spawns a sprite. Same payload
                        // type, asset kind disambiguates at the drop site.
                        std::string p = path ? path : "";
                        std::string ext;
                        if (auto dot = p.find_last_of('.'); dot != std::string::npos) {
                            ext = p.substr(dot);
                            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                        }
                        if (ext == ".zprefab") {
                            prefab_instantiate_from_file(s, p, world_pos);
                        } else {
                            spawn_sprite_from_asset(s, p, world_pos);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Input priority: collider handles > transform gizmo >
                // camera > picking. Collider handles win because they
                // overlap visually with the selection rect, and the user
                // wants to grab them precisely without first deselecting.
                const bool coll_consumed  = handle_collider_gizmos(s, image_min, avail);
                const bool gizmo_consumed = !coll_consumed &&
                    handle_transform_gizmos(s, image_min, avail);
                handle_camera_input(s, image_min, avail);
                if (!gizmo_consumed && !coll_consumed)
                    handle_pick(s, image_min, avail);
            }
        } else {
            ImGui::TextDisabled("(Scene panel needs renderer + space)");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

}  // namespace Engine::editor
