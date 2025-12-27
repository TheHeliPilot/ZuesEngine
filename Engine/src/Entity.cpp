//
// Created by bucka on 12/22/2025.
//

#include "../include/Engine/ECS/Entity.h"

// --------------------
// Constructors
// --------------------

// Use UINT64_MAX as the null sentinel value (index 0 with generation 0 is valid!)
static constexpr uint64_t NULL_ENTITY_ID = UINT64_MAX;

EntityID::EntityID()
    : id(NULL_ENTITY_ID) {}

EntityID::EntityID(const EntityIndex index, const EntityGeneration generation)
    : id((static_cast<uint64_t>(generation) << 32) | index) {}

// --------------------
// Methods
// --------------------

void EntityID::setName(const std::string& meno) {
    name = meno;
}

EntityIndex EntityID::GetIndex() const {
    return static_cast<EntityIndex>(id & 0xFFFFFFFF);
}

EntityGeneration EntityID::GetGeneration() const {
    return static_cast<EntityGeneration>(id >> 32);
}

bool EntityID::IsValid() const {
    return id != NULL_ENTITY_ID;
}

bool EntityID::operator==(const EntityID& other) const {
    return id == other.id;
}

bool EntityID::operator!=(const EntityID& other) const {
    return id != other.id;
}

bool EntityID::operator<(const EntityID& other) const {
    return id < other.id;
}

// --------------------
// Null entity factory
// --------------------

EntityID NullEntityID() {
    return EntityID{};
}