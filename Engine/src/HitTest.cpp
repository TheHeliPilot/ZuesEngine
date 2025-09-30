#include "../include/Engine/HitTest.h"
#include "../include/Engine/Renderer.h"
#include <cmath>
#include <algorithm> // For std::max and std::min

namespace Engine {

    // --- Helper Function ---
    static float Sign(const Math::Vec2& p1, const Math::Vec2& p2, const Math::Vec2& p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    }

    // --- Constants ---
    constexpr float DEFAULT_EDGE_THICKNESS = 1.0f;

    // ----------------------------------------------------------------------------------
    // LINE  (matches rotated quad in Renderer::DrawLine)
    // ----------------------------------------------------------------------------------
    bool HitTest::Line(const Math::Vec2& p, const Math::Vec2& a,
                       const Math::Vec2& b, float thickness, float padding)
    {
        Math::Vec2 center = (a + b) * 0.5f;
        float length = (b - a).Length();
        float rotation = std::atan2(b.y - a.y, b.x - a.x);

        // Treat line as a rotated rectangle (exactly how it's rendered)
        return Rect(p, center, { length, thickness }, rotation, padding);
    }

    // ----------------------------------------------------------------------------------
    // RECT  (already matches rendering)
    // ----------------------------------------------------------------------------------
    bool HitTest::Rect(const Math::Vec2& p, const Math::Vec2& center,
                       const Math::Vec2& size, float rotationRadians, float padding)
    {
        Math::Vec2 local = p - center;

        if (rotationRadians != 0.0f) {
            const float c = std::cos(-rotationRadians);
            const float s = std::sin(-rotationRadians);
            local = { local.x * c - local.y * s,
                      local.x * s + local.y * c };
        }

        const float halfSizeX = size.x * 0.5f;
        const float halfSizeY = size.y * 0.5f;

        return (std::fabs(local.x) <= halfSizeX + padding &&
                std::fabs(local.y) <= halfSizeY + padding);
    }

    // ----------------------------------------------------------------------------------
    // CIRCLE  (now respects thickness properly)
    // ----------------------------------------------------------------------------------
    bool HitTest::Circle(const Math::Vec2 &point, const Math::Vec2 &center,
                         float radius, bool outlineOnly, float thickness, float padding)
    {
        const float dist = (point - center).Length();

        if (outlineOnly) {
            float inner = radius - (thickness * 0.5f) - padding;
            float outer = radius + (thickness * 0.5f) + padding;
            return dist >= inner && dist <= outer;
        }

        // Filled circle
        return dist <= (radius + padding);
    }

    // ----------------------------------------------------------------------------------
    // TRIANGLE (solid + edge fallback)
    // ----------------------------------------------------------------------------------
    bool HitTest::Triangle(const Math::Vec2& pt,
                           const Math::Vec2& v0,
                           const Math::Vec2& v1,
                           const Math::Vec2& v2,
                           float padding)
    {
        // Filled test
        const bool b1 = Sign(pt, v0, v1) < 0.0f;
        const bool b2 = Sign(pt, v1, v2) < 0.0f;
        const bool b3 = Sign(pt, v2, v0) < 0.0f;

        if ((b1 == b2) && (b2 == b3))
            return true;

        // Near any edge
        return Line(pt, v0, v1, DEFAULT_EDGE_THICKNESS, padding) ||
               Line(pt, v1, v2, DEFAULT_EDGE_THICKNESS, padding) ||
               Line(pt, v2, v0, DEFAULT_EDGE_THICKNESS, padding);
    }

    // ----------------------------------------------------------------------------------
    // ARROW (outline arrowhead = matches renderer)
    // ----------------------------------------------------------------------------------
    bool HitTest::Arrow(const Math::Vec2& p, const Math::Vec2& start,
                        const Math::Vec2& end, float thickness, float padding)
    {
        // Shaft
        if (Line(p, start, end, thickness, padding))
            return true;

        // Arrowhead outline (3 lines, same as rendering)
        const Math::Vec2 dir = (end - start).Normalize();
        const Math::Vec2 perp(-dir.y, dir.x);

        constexpr float headSize = 0.5f; // same as Renderer
        const Math::Vec2 tip = end;
        const Math::Vec2 left  = end - dir * headSize + perp * (headSize * 0.5f);
        const Math::Vec2 right = end - dir * headSize - perp * (headSize * 0.5f);

        return Line(p, tip, left, thickness, padding) ||
               Line(p, tip, right, thickness, padding) ||
               Line(p, left, right, thickness, padding);
    }

    // ----------------------------------------------------------------------------------
    // Overloads
    // ----------------------------------------------------------------------------------
    bool HitTest::Line(const Math::Vec2& p, const Engine::Line& line, float padding) {
        return Line(p, line.start, line.end, line.thickness, padding);
    }

    bool HitTest::Rect(const Math::Vec2& p, const Engine::Rect& rect, float padding) {
        return Rect(p, rect.position, rect.size, rect.rotationRadians, padding);
    }

    bool HitTest::Circle(const Math::Vec2& p, const Engine::Circle& circle, float padding) {
        return Circle(p, circle.center, circle.radius, circle.outlineOnly, circle.thickness, padding);
    }

    bool HitTest::Arrow(const Math::Vec2& p, const Engine::Arrow& arrow, float padding) {
        return Arrow(p, arrow.start, arrow.end, arrow.thickness, padding);
    }

} // namespace Engine
