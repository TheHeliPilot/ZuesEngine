#ifndef ZUESENGINE_HITTEST_H
#define ZUESENGINE_HITTEST_H

#include "Math.h"
#include "Renderer.h"

namespace Engine {

    class HitTest {
    public:
        static bool Line(const Math::Vec2& point,
                         const Math::Vec2& a,
                         const Math::Vec2& b,
                         float thickness);

        static bool Circle(const Math::Vec2& point,
                           const Math::Vec2& center,
                           float radius,
                           bool outlineOnly);

        static bool Rect(const Math::Vec2& point,
                         const Math::Vec2& center,
                         const Math::Vec2& size,
                         float rotationRadians = 0.0f);

        static bool Triangle(const Math::Vec2& point,
                             const Math::Vec2& v0,
                             const Math::Vec2& v1,
                             const Math::Vec2& v2);

        static bool Arrow(const Math::Vec2 &p,
                          const Math::Vec2 &start,
                          const Math::Vec2 &end,
                          float thickness);

        static bool Line(const Math::Vec2 &p, const Engine::Line &line);

        static bool Rect(const Math::Vec2 &p, const Engine::Rect &rect);

        static bool Circle(const Math::Vec2 &p, const Engine::Circle &circle);

        static bool Arrow(const Math::Vec2 &p, const Engine::Arrow &arrow);
    };

}

#endif //ZUESENGINE_HITTEST_H
