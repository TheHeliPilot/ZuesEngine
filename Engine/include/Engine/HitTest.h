#ifndef ZUESENGINE_HITTEST_H
#define ZUESENGINE_HITTEST_H

#include "ZuesAPI.h"
#include "Math.h"
#include "Renderer.h"

namespace Engine {

    class ZUES_API HitTest {
    public:
        // Add optional padding/tolerance parameter (default 2.0f)
        static bool Line(const Math::Vec2& point,
                         const Math::Vec2& a,
                         const Math::Vec2& b,
                         float thickness,
                         float padding = 2.0f);

        static bool Circle(const Math::Vec2 &point,
                           const Math::Vec2 &center,
                           float radius,
                           bool outlineOnly,
                           float thickness,
                           float padding = 2.0f);

        static bool Rect(const Math::Vec2& point,
                         const Math::Vec2& center,
                         const Math::Vec2& size,
                         float rotationRadians = 0.0f,
                         float padding = 0.0f);

        static bool Triangle(const Math::Vec2& point,
                             const Math::Vec2& v0,
                             const Math::Vec2& v1,
                             const Math::Vec2& v2,
                             float padding = 0);

        static bool Arrow(const Math::Vec2 &p,
                          const Math::Vec2 &start,
                          const Math::Vec2 &end,
                          float thickness,
                          float padding = 2.0f);

        // Convenience overloads for shapes
        static bool Line(const Math::Vec2 &p, const Engine::Line &line, float padding = 2.0f);
        static bool Rect(const Math::Vec2 &p, const Engine::Rect &rect, float padding = 0.0f);
        static bool Circle(const Math::Vec2 &p, const Engine::Circle &circle, float padding = 2.0f);
        static bool Arrow(const Math::Vec2 &p, const Engine::Arrow &arrow, float padding = 2.0f);
    };

}

#endif //ZUESENGINE_HITTEST_H
