#pragma once

// Editor gizmo system. Immediate-mode API: any code (panels, systems,
// future tools) calls gizmo_line / gizmo_rect / etc. between gizmos_begin
// and gizmos_flush; the queued primitives are submitted to the renderer in
// one pass after the entity render. Drawn on top of everything in the
// scene viewport.
//
// Coordinates are WORLD units (cm). The gizmo system handles world->screen
// projection internally using the camera passed to gizmos_begin. Callers
// don't think in pixels — same mental model as gameplay code.
//
// Usage:
//   gizmos_begin(viewport_w, viewport_h, cam_pan, cam_zoom, ppu);
//   gizmo_rect_outline(center, size, color, line_thickness_px);
//   gizmo_arrow(from, to, color, head_size, line_thickness_px);
//   gizmos_flush(renderer);   // submits everything, clears the buffer

#include <zues/math/color.h>
#include <zues/math/vec2.h>

struct IRenderer_2D_v1;

namespace Engine::editor {

// ---- Lifecycle -----------------------------------------------------------

void gizmos_begin(int viewport_w, int viewport_h,
                  Engine::math::vec2 camera_pan,
                  float zoom,
                  float pixels_per_unit);
void gizmos_flush(::IRenderer_2D_v1* renderer);

// ---- Primitives (all coords + sizes in world cm unless noted) ------------

// Line from a to b. Thickness is in screen pixels (so it stays readable
// at any zoom). World-aware: rotation + length scale with camera.
void gizmo_line(Engine::math::vec2 a, Engine::math::vec2 b,
                Engine::math::color color, float thickness_px = 1.5f);

// Filled rectangle, centered at `center`, axis-aligned. (Use a sequence of
// rotated lines for an oriented rect — that's what gizmo_rect_outline does.)
void gizmo_rect_filled(Engine::math::vec2 center, Engine::math::vec2 size,
                       Engine::math::color color);

// Outlined rectangle. `rotation` in radians (CCW in world space).
void gizmo_rect_outline(Engine::math::vec2 center, Engine::math::vec2 size,
                        Engine::math::color color,
                        float thickness_px = 1.5f, float rotation = 0.0f);

// Circle outline approximated by `segments` line segments.
void gizmo_circle(Engine::math::vec2 center, float radius,
                  Engine::math::color color,
                  float thickness_px = 1.5f, int segments = 32);

// Filled disk (uses a fan-of-quads approximation — good enough for handles).
void gizmo_disk(Engine::math::vec2 center, float radius,
                Engine::math::color color, int segments = 16);

// Arrow from `from` to `to`. Head is a triangle (drawn via 2 short lines
// from the tip). `head_size_px` is in screen pixels so the arrowhead stays
// the same on-screen size at every zoom.
void gizmo_arrow(Engine::math::vec2 from, Engine::math::vec2 to,
                 Engine::math::color color,
                 float thickness_px = 2.0f, float head_size_px = 12.0f);

}  // namespace Engine::editor
