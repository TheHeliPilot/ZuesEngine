#ifndef ZUESENGINE_MATH_H
#define ZUESENGINE_MATH_H

#include <cmath> // For sin, cos, sqrt
#include <cstring> // For memset (in Mat4)

namespace Engine { namespace Math {

    // --- Constants ---
    constexpr float PI = 3.14159265359f;
    // CRITICAL FIX: The missing constant used in CameraSystem.h and RenderingSystem.h
    constexpr float DEGREES_TO_RADIANS = PI / 180.0f;

    // --- Vector Definitions ---

    // Vec2: Used for position, size, and UV coordinates
    struct Vec2 {
        float x, y;

        // Default constructor
        Vec2() : x(0.0f), y(0.0f) {}
        // Value constructor
        Vec2(float x_val, float y_val) : x(x_val), y(y_val) {}

        // Operator overloads (Essential for game math)
        Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
        Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
        Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }
        Vec2 operator/(float scalar) const { return Vec2(x / scalar, y / scalar); }

        // Unary minus (useful for inverse camera translation)
        Vec2 operator-() const { return Vec2(-x, -y); }
    };

    // Vec4: Used for color (RGBA)
    struct Vec4 {
        float x, y, z, w; // Or r, g, b, a

        // Default constructor (often white)
        Vec4() : x(1.0f), y(1.0f), z(1.0f), w(1.0f) {}
        // Value constructor
        Vec4(float x_val, float y_val, float z_val, float w_val) : x(x_val), y(y_val), z(z_val), w(w_val) {}
    };

    // --- Matrix Definitions ---

    // Mat4: 4x4 Matrix (Column-major layout)
    struct Mat4 {
        float elements[4 * 4];

        Mat4() {
            // Initialize to Identity Matrix
            memset(elements, 0, 4 * 4 * sizeof(float));
            elements[0 + 0 * 4] = 1.0f; // Diagonal
            elements[1 + 1 * 4] = 1.0f;
            elements[2 + 2 * 4] = 1.0f;
            elements[3 + 3 * 4] = 1.0f;
        }

        // --- Operators ---

        // CRITICAL FIX: Add matrix multiplication operator for Mat4
        Mat4 operator*(const Mat4& other) const {
            Mat4 result;
            for (int y = 0; y < 4; y++) {
                for (int x = 0; x < 4; x++) {
                    float sum = 0.0f;
                    for (int e = 0; e < 4; e++) {
                        // result[x][y] = this[e][y] * other[x][e]
                        // Standard column-major matrix multiplication logic
                        sum += elements[x + e * 4] * other.elements[e + y * 4]; // (x,e) from 'this' * (e,y) from 'other'
                    }
                    result.elements[x + y * 4] = sum;
                }
            }
            return result;
        }

        // --- Static Factory Functions ---

        // Static factory function for creating an Orthographic Projection matrix
        static Mat4 Orthographic(float left, float right, float bottom, float top, float near, float far) {
            Mat4 result;
            result.elements[0 + 0 * 4] = 2.0f / (right - left);
            result.elements[1 + 1 * 4] = 2.0f / (top - bottom);
            result.elements[2 + 2 * 4] = -2.0f / (far - near); // Negative for Z-axis

            result.elements[0 + 3 * 4] = -(right + left) / (right - left);
            result.elements[1 + 3 * 4] = -(top + bottom) / (top - bottom);
            result.elements[2 + 3 * 4] = -(far + near) / (far - near);

            return result;
        }

        // Static factory function for creating a Translation matrix
        static Mat4 Translate(const Vec2& translation) {
            Mat4 result; // Identity
            result.elements[0 + 3 * 4] = translation.x;
            result.elements[1 + 3 * 4] = translation.y;
            return result;
        }

        // Static factory function for creating a Rotation matrix (Z-axis, 2D rotation)
        static Mat4 Rotate(float radians) {
            Mat4 result;
            float c = std::cos(radians);
            float s = std::sin(radians);

            result.elements[0 + 0 * 4] = c;
            result.elements[1 + 0 * 4] = s;
            result.elements[0 + 1 * 4] = -s;
            result.elements[1 + 1 * 4] = c;
            return result;
        }

        // Static factory function for creating a Scale matrix
        static Mat4 Scale(const Vec2& scale) {
            Mat4 result; // Identity
            result.elements[0 + 0 * 4] = scale.x;
            result.elements[1 + 1 * 4] = scale.y;
            return result;
        }
    };

}}

#endif //ZUESENGINE_MATH_H