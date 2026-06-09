#pragma once
#include <zues/api.h>

namespace Engine::math {

struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr vec3  operator+ (vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr vec3  operator- (vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr vec3  operator- () const       { return {-x, -y, -z}; }
    constexpr vec3  operator* (float s) const { return {x * s, y * s, z * s}; }
    constexpr vec3  operator/ (float s) const { return {x / s, y / s, z / s}; }
    constexpr vec3& operator+=(vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr vec3& operator-=(vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    constexpr bool operator==(const vec3&) const = default;
};

ZUES_API float length   (vec3 v);
ZUES_API vec3  normalize(vec3 v);
ZUES_API float dot      (vec3 a, vec3 b);
ZUES_API vec3  cross    (vec3 a, vec3 b);

}  // namespace Engine::math
