#pragma once
#include <zues/api.h>

namespace Engine::math {

struct vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr vec2() = default;
    constexpr vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr vec2  operator+ (vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr vec2  operator- (vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr vec2  operator- () const       { return {-x, -y}; }
    constexpr vec2  operator* (float s) const { return {x * s, y * s}; }
    constexpr vec2  operator/ (float s) const { return {x / s, y / s}; }
    constexpr vec2& operator+=(vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr vec2& operator-=(vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    constexpr bool operator==(const vec2&) const = default;
};

// Non-trivial ops live in zues_core.dll (may use glm internally).
ZUES_API float length   (vec2 v);
ZUES_API float length2  (vec2 v);
ZUES_API vec2  normalize(vec2 v);
ZUES_API float dot      (vec2 a, vec2 b);
ZUES_API float distance (vec2 a, vec2 b);

}  // namespace Engine::math
