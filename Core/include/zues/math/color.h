#pragma once
#include <zues/types.h>

namespace Engine::math {

// sRGB color, float 0..1 per channel. Engine assumes linear-space
// calculations; conversion helpers come later.
struct color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr color() = default;
    constexpr color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    constexpr bool operator==(const color&) const = default;

    static constexpr color white()       { return {1, 1, 1, 1}; }
    static constexpr color black()       { return {0, 0, 0, 1}; }
    static constexpr color transparent() { return {0, 0, 0, 0}; }
    static constexpr color red()         { return {1, 0, 0, 1}; }
    static constexpr color green()       { return {0, 1, 0, 1}; }
    static constexpr color blue()        { return {0, 0, 1, 1}; }
};

constexpr color rgba(Engine::u8 r, Engine::u8 g, Engine::u8 b, Engine::u8 a = 255) {
    return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

}  // namespace Engine::math
