#pragma once

// Private header — internal to zues_core.dll.
// An archetype is the unique set of component types an entity has. All
// entities sharing the same set live in the same archetype. Within an
// archetype, each component type has its own packed dense byte array
// (column), and a parallel Entity array maps row -> entity id.

#include <zues/ecs/entity.h>
#include <zues/ecs/component_type.h>

#include <vector>

namespace Engine::ecs {

class Archetype {
public:
    // Sorted ascending. Keys for archetype lookup.
    std::vector<ComponentId>          component_ids;
    // Per-column raw byte storage, parallel to component_ids.
    std::vector<std::vector<u8>>      column_bytes;
    // Pointers to the ComponentType entries in World (not owned).
    std::vector<const ComponentType*> column_types;
    // Parallel Entity array: entities[row] is the entity at row `row`.
    std::vector<Entity>               entities;

    // Appends an empty row for `e` and returns the row index. Caller must
    // then fill columns (e.g. copying from another archetype).
    u32 push_entity(Entity e);

    // Swap-remove at `row`. Returns the Entity that moved into `row` (NULL
    // if `row` was already the last row). Caller is responsible for updating
    // that entity's slot to point at the new row.
    Entity swap_remove(u32 row);

    // Returns the column index for `id`, or -1 if this archetype doesn't
    // contain that component.
    int find_column(ComponentId id) const;

    // Pointer arithmetic into column `col` at row `row`.
    u8*       column_at(int col, u32 row);
    const u8* column_at(int col, u32 row) const;

    u32 count() const { return static_cast<u32>(entities.size()); }
};

}  // namespace Engine::ecs
