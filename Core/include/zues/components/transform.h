#pragma once

// 2D transform — position, rotation (radians), scale. Read by render,
// physics, and net systems. Lives in Core because many subsystems need it.

#include <zues/api.h>
#include <zues/ecs/reflection.h>
#include <zues/math/vec2.h>

namespace Engine::components {

struct Transform2D {
    Engine::math::vec2 position{0.0f, 0.0f};
    float              rotation{0.0f};            // radians
    Engine::math::vec2 scale   {1.0f, 1.0f};
};

}  // namespace Engine::components

ZUES_COMPONENT_FIELDS(Engine::components::Transform2D, position, rotation, scale);
