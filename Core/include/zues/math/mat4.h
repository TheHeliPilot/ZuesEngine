#pragma once
#include <zues/api.h>
#include <zues/math/vec3.h>
#include <zues/math/vec4.h>

namespace Engine::math {

// Column-major 4x4 matrix, matching glm.
struct mat4 {
    // Columns as vec4, stored consecutively.
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    constexpr mat4() = default;
};

// All ops in core .cpp; may use glm internally.
ZUES_API mat4 identity();
ZUES_API mat4 translate(const mat4& m, vec3 t);
ZUES_API mat4 scale    (const mat4& m, vec3 s);
ZUES_API mat4 rotate_z (const mat4& m, float radians);
ZUES_API mat4 multiply (const mat4& a, const mat4& b);
ZUES_API mat4 inverse  (const mat4& m);
ZUES_API mat4 ortho    (float left, float right, float bottom, float top, float near_z, float far_z);
ZUES_API mat4 perspective(float fovy_rad, float aspect, float near_z, float far_z);

}  // namespace Engine::math
