#pragma once

// Umbrella header for Engine::math. User code can include this to get the
// common types, or include individual headers for finer control.

#include <zues/math/vec2.h>
#include <zues/math/vec3.h>
#include <zues/math/vec4.h>
#include <zues/math/mat4.h>
#include <zues/math/quat.h>
#include <zues/math/rect.h>
#include <zues/math/color.h>

// Common aliases into the root namespace for short user code.
namespace Engine {
    using math::vec2;
    using math::vec3;
    using math::vec4;
    using math::mat4;
    using math::quat;
    using math::rect;
    using math::color;
}
