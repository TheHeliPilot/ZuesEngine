//
// Created by bucka on 12/22/2025.
//

#include "../include/Engine/ECS/Entity.h"

// --------------------
// Constructors
// --------------------

EntityID::EntityID()
    : id(0) {}

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
    return id != 0;
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