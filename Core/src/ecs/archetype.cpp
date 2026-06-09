#include "archetype.h"

#include <algorithm>
#include <cstring>

namespace Engine::ecs {

u32 Archetype::push_entity(Entity e) {
    const u32 row = static_cast<u32>(entities.size());
    entities.push_back(e);
    for (size_t i = 0; i < column_bytes.size(); ++i) {
        column_bytes[i].resize(column_bytes[i].size() + column_types[i]->size);
    }
    return row;
}

Entity Archetype::swap_remove(u32 row) {
    const u32 last = static_cast<u32>(entities.size() - 1);
    Entity moved_in = NULL_ENTITY;

    if (row != last) {
        entities[row] = entities[last];
        moved_in      = entities[row];
        for (size_t i = 0; i < column_bytes.size(); ++i) {
            const u32 size = column_types[i]->size;
            std::memcpy(&column_bytes[i][row * size],
                        &column_bytes[i][last * size],
                        size);
        }
    }

    entities.pop_back();
    for (size_t i = 0; i < column_bytes.size(); ++i) {
        column_bytes[i].resize(column_bytes[i].size() - column_types[i]->size);
    }
    return moved_in;
}

int Archetype::find_column(ComponentId id) const {
    auto it = std::lower_bound(component_ids.begin(), component_ids.end(), id);
    if (it == component_ids.end() || *it != id) return -1;
    return static_cast<int>(std::distance(component_ids.begin(), it));
}

u8* Archetype::column_at(int col, u32 row) {
    return &column_bytes[col][row * column_types[col]->size];
}

const u8* Archetype::column_at(int col, u32 row) const {
    return &column_bytes[col][row * column_types[col]->size];
}

}  // namespace Engine::ecs
