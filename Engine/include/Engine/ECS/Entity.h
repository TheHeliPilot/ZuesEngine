#pragma once

#include "../ZuesAPI.h"
#include <cstdint>
#include <string>
#include <functional>

// 64-bit Entity ID: 32 bits for Index, 32 bits for Generation
using EntityIndex = uint32_t;
using EntityGeneration = uint32_t;

struct ZUES_API EntityID {
    uint64_t id;
    std::string name;

    // Constructors (DECLARATION ONLY)
    EntityID();
    EntityID(EntityIndex index, EntityGeneration generation);

    // Methods
    void setName(const std::string& meno);

    EntityIndex GetIndex() const;
    EntityGeneration GetGeneration() const;
    bool IsValid() const;

    bool operator==(const EntityID& other) const;
    bool operator!=(const EntityID& other) const;
    bool operator<(const EntityID& other) const;
};

// Exported factory for a null entity
ZUES_API EntityID NullEntityID();

// std::hash specialization (header-only by necessity)
template<>
struct std::hash<EntityID> {
    size_t operator()(const EntityID& entityID) const noexcept {
        return std::hash<uint64_t>()(entityID.id);
    }
};
