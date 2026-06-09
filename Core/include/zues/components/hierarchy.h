#pragma once

// Linked-list hierarchy primitives. Engine maintains these via the helper
// API on World (set_parent / unparent / iterate_children / etc.). Users
// don't write them directly — they're visible in the inspector grayed-out
// for debugging only.
//
// Three POD components:
//   - Parent       : the entity's parent (absent on roots)
//   - FirstChild   : the entity's first child (absent on leaves)
//   - NextSibling  : the entity's next sibling in the parent's child list
//
// Together they form a linked list per parent — unlimited children, zero
// dynamic allocation, all POD-safe.

#include <zues/api.h>
#include <zues/ecs/entity.h>
#include <zues/ecs/reflection.h>

namespace Engine::components {

struct Parent      { Engine::ecs::Entity value{}; };
struct FirstChild  { Engine::ecs::Entity value{}; };
struct NextSibling { Engine::ecs::Entity value{}; };

}  // namespace Engine::components

ZUES_COMPONENT_FIELDS(Engine::components::Parent,      value);
ZUES_COMPONENT_FIELDS(Engine::components::FirstChild,  value);
ZUES_COMPONENT_FIELDS(Engine::components::NextSibling, value);
