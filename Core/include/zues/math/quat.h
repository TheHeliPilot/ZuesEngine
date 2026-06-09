#pragma once
#include <zues/api.h>
#include <zues/math/vec3.h>

namespace Engine::math {

struct quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr quat() = default;
    constexpr quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    constexpr bool operator==(const quat&) const = default;
};

ZUES_API quat identity_quat();
ZUES_API quat from_axis_angle(vec3 axis, float radians);
ZUES_API quat normalize(quat q);
ZUES_API quat slerp    (quat a, quat b, float t);
ZUES_API vec3 rotate   (quat q, vec3 v);

}  // namespace Engine::math
