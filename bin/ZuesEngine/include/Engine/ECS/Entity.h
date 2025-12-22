#pragma once

#include "../ZuesAPI.h"
#include <cstdint>
#include <functional> // For std::hash
#include <string>

// 64-bit Entity ID: 32 bits for Index, 32 bits for Generation
using EntityIndex = uint32_t;
using EntityGeneration = uint32_t;

struct ZUES_API EntityID {
    uint64_t id;
    std::string name;
    void setName(const std::string &meno) {name = meno;}

    // Constructor/utility to create the full ID
    EntityID(const EntityIndex index, const EntityGeneration generation)
        : id((static_cast<uint64_t>(generation) << 32) | index) {}

    // Default constructor for an invalid/null entity
    EntityID() : id(0) {}

    // Convenience getters
    EntityIndex GetIndex() const {
        return static_cast<EntityIndex>(id & 0xFFFFFFFF);
    }

    EntityGeneration GetGeneration() const {
        return static_cast<EntityGeneration>(id >> 32);
    }

    bool IsValid() const {
        return id != 0;
    }

    bool operator==(const EntityID& other) const { return id == other.id; }
    bool operator!=(const EntityID& other) const { return id != other.id; }
    bool operator<(const EntityID& other) const { return id < other.id; }
};

// Global definition for an invalid entity
const EntityID NULL_ENTITY_ID = EntityID();

// Required for using EntityID in std::unordered_map/set
namespace std {
    template<>
    struct hash<EntityID> {
        size_t operator()(const EntityID& entityID) const {
            return std::hash<uint64_t>()(entityID.id);
        }
    };
}