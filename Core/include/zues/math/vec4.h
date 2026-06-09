#pragma once

namespace Engine::math {

struct vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    constexpr vec4  operator+ (vec4 o) const { return {x+o.x, y+o.y, z+o.z, w+o.w}; }
    constexpr vec4  operator- (vec4 o) const { return {x-o.x, y-o.y, z-o.z, w-o.w}; }
    constexpr vec4  operator* (float s) const { return {x*s, y*s, z*s, w*s}; }
    constexpr vec4& operator+=(vec4 o) { x+=o.x; y+=o.y; z+=o.z; w+=o.w; return *this; }

    constexpr bool operator==(const vec4&) const = default;
};

}  // namespace Engine::math
