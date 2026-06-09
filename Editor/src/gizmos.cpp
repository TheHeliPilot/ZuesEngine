#include "gizmos.h"

#include <zues/services/renderer_2d.h>

#include <cmath>
#include <vector>

namespace Engine::editor {

namespace {
    // Per-frame command buffer. Two primitive types: rotated quads (covers
    // lines, filled rects, disk-fan triangles) — that's all draw_sprite_rot
    // can give us. Everything else decomposes into these.
    struct QuadCmd {
        float cx, cy;          // center in screen pixels
        float w, h;            // size in screen pixels
        float angle;           // radians, screen-space (Y-down)
        float r, g, b, a;
    };

    struct GizmoState {
        std::vector<QuadCmd> quads;
        // Camera snapshot at gizmos_begin so primitive helpers can do
        // world->screen without taking it as a parameter every call.
        int   vw = 0, vh = 0;
        float pan_x = 0, pan_y = 0;
        float ppu = 100.0f;       // pixels_per_unit * zoom
    };
    GizmoState g{};

    // World cm -> screen pixel.
    inline void world_to_screen(Engine::math::vec2 w, float& sx, float& sy) {
        sx = (w.x - g.pan_x) * g.ppu + static_cast<float>(g.vw) * 0.5f;
        sy = (-(w.y - g.pan_y)) * g.ppu + static_cast<float>(g.vh) * 0.5f;
    }

    inline void push_quad(float cx, float cy, float w, float h, float angle,
                          const Engine::math::color& col) {
        g.quads.push_back({cx, cy, w, h, angle, col.r, col.g, col.b, col.a});
    }

    // Line in screen-space pixels: thin rotated quad from (sx0,sy0) to (sx1,sy1).
    void push_line_screen(float sx0, float sy0, float sx1, float sy1,
                          float thickness_px, const Engine::math::color& col) {
        const float dx = sx1 - sx0;
        const float dy = sy1 - sy0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.0001f) return;
        const float angle = std::atan2(dy, dx);
        const float cx = (sx0 + sx1) * 0.5f;
        const float cy = (sy0 + sy1) * 0.5f;
        push_quad(cx, cy, len, thickness_px, angle, col);
    }
}

// ---- Lifecycle ---------------------------------------------------------------

void gizmos_begin(int viewport_w, int viewport_h,
                  Engine::math::vec2 camera_pan,
                  float zoom, float pixels_per_unit) {
    g.quads.clear();
    g.vw    = viewport_w;
    g.vh    = viewport_h;
    g.pan_x = camera_pan.x;
    g.pan_y = camera_pan.y;
    g.ppu   = pixels_per_unit * zoom;
}

void gizmos_flush(::IRenderer_2D_v1* renderer) {
    if (!renderer || !renderer->draw_sprite_rot) { g.quads.clear(); return; }
    for (const auto& q : g.quads) {
        renderer->draw_sprite_rot(renderer, /*texture=*/0,
            q.cx, q.cy, q.w, q.h, q.angle,
            0.0f, 0.0f, 1.0f, 1.0f,
            q.r, q.g, q.b, q.a);
    }
    g.quads.clear();
}

// ---- Primitives --------------------------------------------------------------

void gizmo_line(Engine::math::vec2 a, Engine::math::vec2 b,
                Engine::math::color color, float thickness_px) {
    float ax, ay, bx, by;
    world_to_screen(a, ax, ay);
    world_to_screen(b, bx, by);
    push_line_screen(ax, ay, bx, by, thickness_px, color);
}

void gizmo_rect_filled(Engine::math::vec2 center, Engine::math::vec2 size,
                       Engine::math::color color) {
    float cx, cy;
    world_to_screen(center, cx, cy);
    const float w = size.x * g.ppu;
    const float h = size.y * g.ppu;
    push_quad(cx, cy, w, h, 0.0f, color);
}

void gizmo_rect_outline(Engine::math::vec2 center, Engine::math::vec2 size,
                        Engine::math::color color,
                        float thickness_px, float rotation) {
    // Rotate the four corners around the center in WORLD space, then project
    // each. Drawing the four edges as screen-space lines keeps thickness
    // constant at any zoom.
    const float hw = size.x * 0.5f;
    const float hh = size.y * 0.5f;
    const float ca = std::cos(rotation);
    const float sa = std::sin(rotation);

    auto corner = [&](float lx, float ly) {
        return Engine::math::vec2{
            center.x + lx * ca - ly * sa,
            center.y + lx * sa + ly * ca
        };
    };

    const auto p0 = corner(-hw, -hh);
    const auto p1 = corner( hw, -hh);
    const auto p2 = corner( hw,  hh);
    const auto p3 = corner(-hw,  hh);

    gizmo_line(p0, p1, color, thickness_px);
    gizmo_line(p1, p2, color, thickness_px);
    gizmo_line(p2, p3, color, thickness_px);
    gizmo_line(p3, p0, color, thickness_px);
}

void gizmo_circle(Engine::math::vec2 center, float radius,
                  Engine::math::color color,
                  float thickness_px, int segments) {
    if (segments < 3) segments = 3;
    constexpr float TAU = 6.2831853f;
    Engine::math::vec2 prev{center.x + radius, center.y};
    for (int i = 1; i <= segments; ++i) {
        const float t = (static_cast<float>(i) / segments) * TAU;
        Engine::math::vec2 cur{
            center.x + std::cos(t) * radius,
            center.y + std::sin(t) * radius
        };
        gizmo_line(prev, cur, color, thickness_px);
        prev = cur;
    }
}

void gizmo_disk(Engine::math::vec2 center, float radius,
                Engine::math::color color, int segments) {
    // Fan of skinny triangles approximated as overlapping quads — good
    // enough for small handles. For larger disks consider a proper
    // triangle-fan primitive later.
    if (segments < 6) segments = 6;
    constexpr float TAU = 6.2831853f;
    float cx, cy;
    world_to_screen(center, cx, cy);
    const float r_px = radius * g.ppu;
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / segments) * TAU;
        // Each "wedge" is a thin rotated rect from center outward.
        const float w = r_px;
        const float h = (TAU * r_px / segments) + 1.0f;     // arc-length-ish
        push_quad(cx + std::cos(t) * r_px * 0.5f,
                  cy + std::sin(t) * r_px * 0.5f,
                  w, h, t, color);
    }
}

void gizmo_arrow(Engine::math::vec2 from, Engine::math::vec2 to,
                 Engine::math::color color,
                 float thickness_px, float head_size_px) {
    // Shaft.
    float fx, fy, tx, ty;
    world_to_screen(from, fx, fy);
    world_to_screen(to,   tx, ty);
    push_line_screen(fx, fy, tx, ty, thickness_px, color);

    // Head: two screen-space line segments back from the tip at ±30° from
    // the shaft direction. Pixel-sized so the arrowhead doesn't shrink at
    // low zoom or balloon when zoomed in.
    const float dx = tx - fx;
    const float dy = ty - fy;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    const float ux = dx / len;
    const float uy = dy / len;
    constexpr float COS30 = 0.8660254f;
    constexpr float SIN30 = 0.5f;
    // Rotate (-ux, -uy) by ±30° to get the head leg directions.
    const float lx0 = -ux * COS30 + uy * SIN30;
    const float ly0 = -uy * COS30 - ux * SIN30;
    const float lx1 = -ux * COS30 - uy * SIN30;
    const float ly1 = -uy * COS30 + ux * SIN30;
    push_line_screen(tx, ty, tx + lx0 * head_size_px, ty + ly0 * head_size_px,
                     thickness_px, color);
    push_line_screen(tx, ty, tx + lx1 * head_size_px, ty + ly1 * head_size_px,
                     thickness_px, color);
}

}  // namespace Engine::editor
