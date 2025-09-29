#include "../include/Engine/HitTest.h"
#include "../include/Engine/Renderer.h"
#include <cmath>

namespace Engine {

    bool HitTest::Line(const Math::Vec2& p, const Math::Vec2& a,
                       const Math::Vec2& b, const float thickness) {
        const Math::Vec2 ab = b - a;
        const Math::Vec2 ap = p - a;
        const float abLenSq = ab.Dot(ab);
        if (abLenSq < 1e-6f) return false; // Degenerate line

        const float t = std::max(0.0f, std::min(1.0f, ap.Dot(ab) / abLenSq));
        const Math::Vec2 closest = a + ab * t;
        const float dist = (p - closest).Length();
        return dist <= thickness * 0.5f;
    }

    bool HitTest::Circle(const Math::Vec2 &point, const Math::Vec2 &center, const float radius, const bool outlineOnly) {
        const float dist = (point - center).Length();
        if (outlineOnly) {
            return std::fabs(dist - radius) <= 0.5f;
        }
        //else filled
        return dist <= radius;
    }

    bool HitTest::Rect(const Math::Vec2& p, const Math::Vec2& center,
                       const Math::Vec2& size, const float rotationRadians) {
        Math::Vec2 local = p - center;

        if (rotationRadians != 0.0f) {
            const float c = std::cos(-rotationRadians);
            const float s = std::sin(-rotationRadians);
            local = { local.x * c - local.y * s,
                      local.x * s + local.y * c };
        }

        return (std::fabs(local.x) <= size.x * 0.5f &&
                std::fabs(local.y) <= size.y * 0.5f);
    }

    static float Sign(const Math::Vec2& p1,
                      const Math::Vec2& p2,
                      const Math::Vec2& p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) -
               (p2.x - p3.x) * (p1.y - p3.y);
    }

    bool HitTest::Triangle(const Math::Vec2& pt,
                           const Math::Vec2& v0,
                           const Math::Vec2& v1,
                           const Math::Vec2& v2) {
        const bool b1 = Sign(pt, v0, v1) < 0.0f;
        const bool b2 = Sign(pt, v1, v2) < 0.0f;
        const bool b3 = Sign(pt, v2, v0) < 0.0f;
        return ((b1 == b2) && (b2 == b3));
    }

    bool HitTest::Arrow(const Math::Vec2& p, const Math::Vec2& start,
                        const Math::Vec2& end, const float thickness)
    {
        // First, check the shaft (line part)
        if (Line(p, start, end, thickness))
            return true;

        // Arrowhead parameters (same as in DrawArrow)
        const Math::Vec2 dir = (end - start).Normalize();
        const Math::Vec2 perp(-dir.y, dir.x); // perpendicular
        constexpr float headSize = 0.5f;

        const Math::Vec2 tip = end;
        const Math::Vec2 left = end - dir * headSize + perp * (headSize * 0.5f);
        const Math::Vec2 right = end - dir * headSize - perp * (headSize * 0.5f);

        // Check if point is inside the triangle arrowhead
        return Triangle(p, tip, left, right);
    }

    bool HitTest::Line(const Math::Vec2& p, const Engine::Line& line) {
        return Line(p, line.start, line.end, line.thickness);
    }

    bool HitTest::Rect(const Math::Vec2& p, const Engine::Rect& rect) {
        return Rect(p, rect.position, rect.size, rect.rotationRadians);
    }

    bool HitTest::Circle(const Math::Vec2& p, const Engine::Circle& circle) {
        return Circle(p, circle.center, circle.radius, circle.outlineOnly);
    }

    bool HitTest::Arrow(const Math::Vec2& p, const Engine::Arrow& arrow) {
        return Arrow(p, arrow.start, arrow.end, arrow.thickness);
    }

}
