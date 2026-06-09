#pragma once
#include <zues/types.h>

#include <cstddef>

namespace Engine::ecs {

// Generational ID. `generation == 0` is reserved for NULL. Live entities have
// generation >= 1. Generations bump each time the slot is recycled so stale
// Entity handles never resolve to a different live entity.
struct Entity {
    u32 index      = 0;
    u32 generation = 0;

    constexpr bool is_null() const { return generation == 0; }
    constexpr bool operator==(const Entity&) const = default;
};

constexpr Entity NULL_ENTITY = {};

struct EntityHash {
    std::size_t operator()(Entity e) const noexcept {
        return (static_cast<std::size_t>(e.generation) << 32) | static_cast<std::size_t>(e.index);
    }
};

// Typed alias used for cross-entity references in component fields. Holds the
// same handle as Entity, but the distinct type lets the inspector render a
// drag-target slot (instead of two raw integers) and lets Lync expose it as
// a first-class reference type with sugar (Get<T>, Has<T>, etc.).
//
// At runtime EntityRef IS an Entity — load/save, archetype storage, query
// iteration all treat it identically. The only difference is editor + script
// surface. Use Entity for "an entity I'm currently working on", EntityRef for
// "a slot pointing at some other entity in the world".
struct EntityRef {
    u32 index      = 0;
    u32 generation = 0;

    constexpr bool is_null() const { return generation == 0; }
    constexpr bool operator==(const EntityRef&) const = default;

    constexpr Entity to_entity() const { return Entity{index, generation}; }
    static constexpr EntityRef from_entity(Entity e) { return {e.index, e.generation}; }
};
static_assert(sizeof(EntityRef) == sizeof(Entity),
              "EntityRef must be layout-compatible with Entity");

}  // namespace Engine::ecs
