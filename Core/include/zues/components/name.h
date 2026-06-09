#pragma once

// Display name for the editor inspector. Auto-attached to every entity by
// World::create_entity (default = "entity_<n>"). Manual add overwrites.

#include <zues/api.h>
#include <zues/ecs/reflection.h>

namespace Engine::components {

// Fixed 64-byte buffer (no heap, POD-safe). Treated as null-terminated UTF-8.
struct Name {
    char value[64] = {};
};

}  // namespace Engine::components

ZUES_COMPONENT_FIELDS(Engine::components::Name, value);
