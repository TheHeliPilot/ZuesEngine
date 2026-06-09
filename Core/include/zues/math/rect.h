#pragma once
#include <zues/math/vec2.h>

namespace Engine::math {

struct rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr rect() = default;
    constexpr rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    constexpr vec2 min()    const { return {x, y}; }
    constexpr vec2 max()    const { return {x + w, y + h}; }
    constexpr vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    constexpr vec2 size()   const { return {w, h}; }

    constexpr bool contains(vec2 p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }

    constexpr bool operator==(const rect&) const = default;
};

constexpr bool overlaps(rect a, rect b) {
    return !(a.x + a.w < b.x || b.x + b.w < a.x
          || a.y + a.h < b.y || b.y + b.h < a.y);
}

}  // namespace Engine::math
