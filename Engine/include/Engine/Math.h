#include "ZuesAPI.h"

#ifndef ZUESENGINE_MATH_H
#define ZUESENGINE_MATH_H

#include <cmath> // For sin, cos, sqrt
#include <cstring> // For memset (in Mat4)

namespace Engine::Math {

    // --- Constants ---
    constexpr float PI = 3.14159265359f;
    // CRITICAL FIX: The missing constant used in CameraSystem.h and RenderingSystem.h
    constexpr float DEGREES_TO_RADIANS = PI / 180.0f;

    // --- Vector Definitions ---

    // Vec2: Used for position, size, and UV coordinates
    struct ZUES_API Vec2 {
        float x, y;

        // Default constructor
        Vec2() : x(0.0f), y(0.0f) {}
        // Value constructor
        Vec2(const float x_val, const float y_val) : x(x_val), y(y_val) {}

        float Length() const { return std::sqrt(x * x + y * y); }

        Vec2 Normalize() {
            if (const float len = Length(); len != 0.0f) {
                x /= len;
                y /= len;
            }
            return *this;
        }

        bool operator==(const Engine::Math::Vec2 & vec2) const {
            return vec2.x == x && vec2.y == y;
        }

        Vec2 &operator+=(const Math::Vec2 & vec2) {
            // Add the components of the input vector (vec2) to the current vector (*this)
            this->x += vec2.x;
            this->y += vec2.y;

            // Return a reference to the modified current object
            return *this;
        }

        static Vec2 Normalized(const Vec2& v) {
            if (const float len = v.Length(); len != 0.0f) {
                return { v.x / len, v.y / len };
            }
            return {0.0f, 0.0f}; // return zero vector if can't normalize
        }

        float Dot(const Vec2& other) const {
            return x * other.x + y * other.y;
        }

        // Operator overloads (Essential for game math)
        Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
        Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
        Vec2 operator*(const float scalar) const { return Vec2(x * scalar, y * scalar); }
        Vec2 operator/(const float scalar) const { return Vec2(x / scalar, y / scalar); }

        // Unary minus (useful for inverse camera translation)
        Vec2 operator-() const { return Vec2(-x, -y); }
    };

    // Vec3: Used for 3D position, normal, and scale
    struct ZUES_API Vec3 {
        float x, y, z;

        // Default constructor
        Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
        // Value constructor
        Vec3(const float x_val, const float y_val, const float z_val) : x(x_val), y(y_val), z(z_val) {}

        float Length() const { return std::sqrt(x * x + y * y + z * z); }

        Vec3 Normalize() {
            if (const float len = Length(); len != 0.0f) {
                x /= len;
                y /= len;
                z /= len;
            }
            return *this;
        }

        static Vec3 Normalized(const Vec3& v) {
            if (const float len = v.Length(); len != 0.0f) {
                return { v.x / len, v.y / len, v.z / len };
            }
            return {0.0f, 0.0f, 0.0f}; // return zero vector if can't normalize
        }

        float Dot(const Vec3& other) const {
            return x * other.x + y * other.y + z * other.z;
        }

        // Cross product (Essential for 3D operations like calculating normals)
        Vec3 Cross(const Vec3& other) const {
            return {
                y * other.z - z * other.y,
                z * other.x - x * other.z,
                x * other.y - y * other.x
            };
        }

        // Operator overloads
        Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
        Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
        Vec3 operator*(const float scalar) const { return Vec3(x * scalar, y * scalar, z * scalar); }
        Vec3 operator/(const float scalar) const { return Vec3(x / scalar, y / scalar, z / scalar); }

        // Unary minus
        Vec3 operator-() const { return Vec3(-x, -y, -z); }
    };

    // Vec4: Used for color (RGBA)
    struct ZUES_API Vec4 {
        float x, y, z, w; // Or r, g, b, a

        // Default constructor (often white)
        Vec4() : x(1.0f), y(1.0f), z(1.0f), w(1.0f) {}
        // Value constructor
        Vec4(const float x_val, const float y_val, const float z_val, const float w_val) : x(x_val), y(y_val), z(z_val), w(w_val) {}
    };

    // --- Matrix Definitions ---

    // Mat4: 4x4 Matrix (Column-major layout)
    struct ZUES_API Mat4 {
        float elements[16]; // Column-major (OpenGL style)

        // --- Constructors ---

        Mat4() {
            // Initialize to Identity
            memset(elements, 0, sizeof(elements));
            elements[0 + 0 * 4] = 1.0f;
            elements[1 + 1 * 4] = 1.0f;
            elements[2 + 2 * 4] = 1.0f;
            elements[3 + 3 * 4] = 1.0f;
        }

        explicit Mat4(float diag) {
            memset(elements, 0, sizeof(elements));
            elements[0 + 0 * 4] = diag;
            elements[1 + 1 * 4] = diag;
            elements[2 + 2 * 4] = diag;
            elements[3 + 3 * 4] = diag;
        }

        // --- Static Helpers ---

        static Mat4 Identity() {
            return Mat4(1.0f);
        }

        static Mat4 Orthographic(float left, float right, float bottom, float top, float near, float far) {
            Mat4 result(1.0f);

            result.elements[0 + 0 * 4] = 2.0f / (right - left);
            result.elements[1 + 1 * 4] = 2.0f / (top - bottom);
            result.elements[2 + 2 * 4] = -2.0f / (far - near);

            result.elements[0 + 3 * 4] = -(right + left) / (right - left);
            result.elements[1 + 3 * 4] = -(top + bottom) / (top - bottom);
            result.elements[2 + 3 * 4] = -(far + near) / (far - near);

            return result;
        }

        static Mat4 Translate(const Vec2& translation) {
            Mat4 result(1.0f);
            result.elements[0 + 3 * 4] = translation.x;
            result.elements[1 + 3 * 4] = translation.y;
            return result;
        }

        static Mat4 Rotate(float radians) {
            Mat4 result(1.0f);

            float c = std::cos(radians);
            float s = std::sin(radians);

            result.elements[0 + 0 * 4] = c;
            result.elements[1 + 0 * 4] = s;
            result.elements[0 + 1 * 4] = -s;
            result.elements[1 + 1 * 4] = c;

            return result;
        }

        static Mat4 Scale(const Vec2& scale) {
            Mat4 result(1.0f);
            result.elements[0 + 0 * 4] = scale.x;
            result.elements[1 + 1 * 4] = scale.y;
            return result;
        }

        // --- Operators ---

        Mat4 operator*(const Mat4& other) const {
            Mat4 result(0.0f);
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    float sum = 0.0f;
                    for (int e = 0; e < 4; e++) {
                        sum += elements[x + e * 4] * other.elements[e + y * 4];
                    }
                    result.elements[x + y * 4] = sum;
                }
            }
            return result;
        }

        // --- Utilities ---

        Mat4 Transposed() const {
            Mat4 result(0.0f);
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    result.elements[y + x * 4] = elements[x + y * 4];
                }
            }
            return result;
        }

        Mat4 Inverted() const {
            Mat4 result(0.0f);
            float inv[16], det;
            const float* m = elements;

            inv[0] =   m[5]  * m[10] * m[15] -
                       m[5]  * m[11] * m[14] -
                       m[9]  * m[6]  * m[15] +
                       m[9]  * m[7]  * m[14] +
                       m[13] * m[6]  * m[11] -
                       m[13] * m[7]  * m[10];

            inv[4] =  -m[4]  * m[10] * m[15] +
                       m[4]  * m[11] * m[14] +
                       m[8]  * m[6]  * m[15] -
                       m[8]  * m[7]  * m[14] -
                       m[12] * m[6]  * m[11] +
                       m[12] * m[7]  * m[10];

            inv[8] =   m[4]  * m[9]  * m[15] -
                       m[4]  * m[11] * m[13] -
                       m[8]  * m[5]  * m[15] +
                       m[8]  * m[7]  * m[13] +
                       m[12] * m[5]  * m[11] -
                       m[12] * m[7]  * m[9];

            inv[12] = -m[4]  * m[9]  * m[14] +
                       m[4]  * m[10] * m[13] +
                       m[8]  * m[5]  * m[14] -
                       m[8]  * m[6]  * m[13] -
                       m[12] * m[5]  * m[10] +
                       m[12] * m[6]  * m[9];

            inv[1] =  -m[1]  * m[10] * m[15] +
                       m[1]  * m[11] * m[14] +
                       m[9]  * m[2]  * m[15] -
                       m[9]  * m[3]  * m[14] -
                       m[13] * m[2]  * m[11] +
                       m[13] * m[3]  * m[10];

            inv[5] =   m[0]  * m[10] * m[15] -
                       m[0]  * m[11] * m[14] -
                       m[8]  * m[2]  * m[15] +
                       m[8]  * m[3]  * m[14] +
                       m[12] * m[2]  * m[11] -
                       m[12] * m[3]  * m[10];

            inv[9] =  -m[0]  * m[9]  * m[15] +
                       m[0]  * m[11] * m[13] +
                       m[8]  * m[1]  * m[15] -
                       m[8]  * m[3]  * m[13] -
                       m[12] * m[1]  * m[11] +
                       m[12] * m[3]  * m[9];

            inv[13] =  m[0]  * m[9]  * m[14] -
                       m[0]  * m[10] * m[13] -
                       m[8]  * m[1]  * m[14] +
                       m[8]  * m[2]  * m[13] +
                       m[12] * m[1]  * m[10] -
                       m[12] * m[2]  * m[9];

            inv[2] =   m[1]  * m[6]  * m[15] -
                       m[1]  * m[7]  * m[14] -
                       m[5]  * m[2]  * m[15] +
                       m[5]  * m[3]  * m[14] +
                       m[13] * m[2]  * m[7]  -
                       m[13] * m[3]  * m[6];

            inv[6] =  -m[0]  * m[6]  * m[15] +
                       m[0]  * m[7]  * m[14] +
                       m[4]  * m[2]  * m[15] -
                       m[4]  * m[3]  * m[14] -
                       m[12] * m[2]  * m[7]  +
                       m[12] * m[3]  * m[6];

            inv[10] =  m[0]  * m[5]  * m[15] -
                       m[0]  * m[7]  * m[13] -
                       m[4]  * m[1]  * m[15] +
                       m[4]  * m[3]  * m[13] +
                       m[12] * m[1]  * m[7]  -
                       m[12] * m[3]  * m[5];

            inv[14] = -m[0]  * m[5]  * m[14] +
                       m[0]  * m[6]  * m[13] +
                       m[4]  * m[1]  * m[14] -
                       m[4]  * m[2]  * m[13] -
                       m[12] * m[1]  * m[6]  +
                       m[12] * m[2]  * m[5];

            inv[3] =  -m[1] * m[6] * m[11] +
                       m[1] * m[7] * m[10] +
                       m[5] * m[2] * m[11] -
                       m[5] * m[3] * m[10] -
                       m[9] * m[2] * m[7]  +
                       m[9] * m[3] * m[6];

            inv[7] =   m[0] * m[6] * m[11] -
                       m[0] * m[7] * m[10] -
                       m[4] * m[2] * m[11] +
                       m[4] * m[3] * m[10] +
                       m[8] * m[2] * m[7]  -
                       m[8] * m[3] * m[6];

            inv[11] = -m[0] * m[5] * m[11] +
                       m[0] * m[7] * m[9]  +
                       m[4] * m[1] * m[11] -
                       m[4] * m[3] * m[9]  -
                       m[8] * m[1] * m[7]  +
                       m[8] * m[3] * m[5];

            inv[15] =  m[0] * m[5] * m[10] -
                       m[0] * m[6] * m[9]  -
                       m[4] * m[1] * m[10] +
                       m[4] * m[2] * m[9]  +
                       m[8] * m[1] * m[6]  -
                       m[8] * m[2] * m[5];

            det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

            if (det == 0.0f) {
                return Identity();
            }

            det = 1.0f / det;

            for (int i = 0; i < 16; i++) {
                result.elements[i] = inv[i] * det;
            }

            return result;
        }
    };


}

#endif //ZUESENGINE_MATH_H