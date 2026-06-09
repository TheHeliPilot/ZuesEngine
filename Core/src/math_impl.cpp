// Math operations whose implementation uses glm internally. Public types
// (Engine::math::vec2, mat4, quat, ...) stay POD-shaped in zues/math/*.h —
// glm only exists below this line, never in a public header. This keeps glm
// out of user project.dll headers and lets us swap the backend later without
// breaking user code.

#include <zues/math/mat4.h>
#include <zues/math/quat.h>
#include <zues/math/vec2.h>
#include <zues/math/vec3.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>

namespace Engine::math {

// ---- Engine <-> glm interop helpers (private) -------------------------------
namespace {
    inline glm::vec2 to_glm(vec2 v) noexcept { return {v.x, v.y}; }
    inline glm::vec3 to_glm(vec3 v) noexcept { return {v.x, v.y, v.z}; }

    inline glm::mat4 to_glm(const mat4& m) noexcept {
        glm::mat4 g;
        std::memcpy(&g[0][0], m.m, sizeof(m.m));
        return g;
    }

    // glm::quat is {w, x, y, z}; we store {x, y, z, w}.
    inline glm::quat to_glm(quat q) noexcept { return {q.w, q.x, q.y, q.z}; }

    inline vec2 from_glm(const glm::vec2& g) noexcept { return {g.x, g.y}; }
    inline vec3 from_glm(const glm::vec3& g) noexcept { return {g.x, g.y, g.z}; }

    inline mat4 from_glm(const glm::mat4& g) noexcept {
        mat4 m;
        std::memcpy(m.m, &g[0][0], sizeof(m.m));
        return m;
    }

    inline quat from_glm(const glm::quat& g) noexcept { return {g.x, g.y, g.z, g.w}; }
}

// ---- vec2 -------------------------------------------------------------------
float length (vec2 v) { return glm::length(to_glm(v)); }
float length2(vec2 v) { return v.x * v.x + v.y * v.y; }

vec2 normalize(vec2 v) {
    if (v.x == 0.0f && v.y == 0.0f) return {0.0f, 0.0f};
    return from_glm(glm::normalize(to_glm(v)));
}

float dot     (vec2 a, vec2 b) { return glm::dot(to_glm(a), to_glm(b)); }
float distance(vec2 a, vec2 b) { return glm::distance(to_glm(a), to_glm(b)); }

// ---- vec3 -------------------------------------------------------------------
float length(vec3 v) { return glm::length(to_glm(v)); }

vec3 normalize(vec3 v) {
    if (v.x == 0.0f && v.y == 0.0f && v.z == 0.0f) return {0.0f, 0.0f, 0.0f};
    return from_glm(glm::normalize(to_glm(v)));
}

float dot  (vec3 a, vec3 b) { return glm::dot  (to_glm(a), to_glm(b)); }
vec3  cross(vec3 a, vec3 b) { return from_glm(glm::cross(to_glm(a), to_glm(b))); }

// ---- mat4 -------------------------------------------------------------------
mat4 identity() { return mat4{}; }

mat4 translate(const mat4& m, vec3 t) {
    return from_glm(glm::translate(to_glm(m), to_glm(t)));
}

mat4 scale(const mat4& m, vec3 s) {
    return from_glm(glm::scale(to_glm(m), to_glm(s)));
}

mat4 rotate_z(const mat4& m, float radians) {
    return from_glm(glm::rotate(to_glm(m), radians, glm::vec3(0.0f, 0.0f, 1.0f)));
}

mat4 multiply(const mat4& a, const mat4& b) {
    return from_glm(to_glm(a) * to_glm(b));
}

mat4 inverse(const mat4& m) {
    return from_glm(glm::inverse(to_glm(m)));
}

mat4 ortho(float left, float right, float bottom, float top, float near_z, float far_z) {
    return from_glm(glm::ortho(left, right, bottom, top, near_z, far_z));
}

mat4 perspective(float fovy_rad, float aspect, float near_z, float far_z) {
    return from_glm(glm::perspective(fovy_rad, aspect, near_z, far_z));
}

// ---- quat -------------------------------------------------------------------
quat identity_quat() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

quat from_axis_angle(vec3 axis, float radians) {
    const auto a = to_glm(normalize(axis));
    return from_glm(glm::angleAxis(radians, a));
}

quat normalize(quat q) {
    const float l2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (l2 == 0.0f) return identity_quat();
    return from_glm(glm::normalize(to_glm(q)));
}

quat slerp(quat a, quat b, float t) {
    return from_glm(glm::slerp(to_glm(a), to_glm(b), t));
}

vec3 rotate(quat q, vec3 v) {
    // glm overloads operator* for quat-on-vec3, no gtx needed.
    return from_glm(to_glm(q) * to_glm(v));
}

}  // namespace Engine::math
