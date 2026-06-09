#pragma once
// Remaining lifetime in seconds. Systems decrement this each tick;
// when <= 0 the entity is a candidate for destruction.

#include <zues/api.h>
#include <zues/ecs/reflection.h>

namespace Engine::components {

struct Lifetime {
    float remaining = 0.0f;   // seconds
};

}  // namespace Engine::components

ZUES_COMPONENT_FIELDS(Engine::components::Lifetime, remaining);
