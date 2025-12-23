#include "../include/Engine/ECS/World.h"
#include "../include/Engine/ZuesAPI.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>

#include "../include/Engine/Core.h"
#include "../include/Engine/ECS/HierarchyOutliner.h"
#include "../include/Engine/ECS/WorldSerializationHelpers.h"

// Global component registry - MUST be exported for game DLLs to access
namespace Engine::ECS::Component {
    ZUES_API std::map<TypeID, ComponentCreator> componentRegistry;
}

std::size_t std::hash<std::bitset<64>>::operator()(const ComponentSignature &signature) const {
    // Safe for MAX_COMPONENTS=64
    return std::hash<unsigned long long>()(signature.to_ullong());
}

EntityID World::CreateEntity(const std::string &name) {
    EntityIndex index;
    EntityGeneration generation = 0;

    if (!freeIndices.empty()) {
        index = freeIndices.back();
        freeIndices.pop_back();
        generation = entityLookup[index].generation;
    } else {
        index = entityLookup.size();
        // Expand the lookup table if we need a new slot
        entityLookup.emplace_back();
    }

    // Find or create the default (empty) archetype
    Archetype* defaultArchetype = GetOrCreateArchetype(ComponentSignature());

    // Initialize data
    EntityID newID(index, generation);
    newID.name = name;
    entityLookup[index] = { generation, defaultArchetype, defaultArchetype->entityIDs.size() };
    defaultArchetype->entityIDs.push_back(newID);

    Engine::ECS::Hierarchy::BuildCache(Engine::Core::GetCurrentWorld());
    return newID;
}

void World::DestroyEntity(const EntityID entityID) {
    const EntityIndex index = entityID.GetIndex();

    // 1. Validation check
    if (index >= entityLookup.size() || entityLookup[index].generation != entityID.GetGeneration() || entityLookup[index].archetypePtr == nullptr) {
        // Invalid or already destroyed
        return;
    }

    EntityData& data = entityLookup[index];
    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);
    const size_t indexInArchetype = data.archetypeIndex;

    // 2. Perform Swap-and-Pop on the Archetype

    // The entity we are swapping in (the last one in the list)
    const EntityID entityToSwapID = currentArchetype->entityIDs.back();

    // a) Swap-and-Pop component data
    for (auto const& [typeID, arrayPtr] : currentArchetype->componentArrays) {
        // The IComponentArray::RemoveComponent uses Swap-and-Pop internally
        arrayPtr->RemoveComponent(indexInArchetype);
    }

    // b) Swap-and-Pop the EntityID list
    currentArchetype->entityIDs[indexInArchetype] = entityToSwapID;
    currentArchetype->entityIDs.pop_back();

    // 3. Update the lookup table for the entity that was SWAPPED into the removed slot
    if (indexInArchetype < currentArchetype->entityIDs.size()) {
        // Only update if a swap actually occurred (i.e., we weren't removing the last element)
        entityLookup[entityToSwapID.GetIndex()].archetypeIndex = indexInArchetype;
    }

    // 4. Cleanup the destroyed entity's lookup entry
    data.generation++; // Increment generation for safety
    data.archetypePtr = nullptr;
    data.archetypeIndex = 0;
    freeIndices.push_back(index);
    Engine::ECS::Hierarchy::BuildCache(Engine::Core::GetCurrentWorld());
}

void World::RegisterSystem(std::unique_ptr<System> system) {
    systems.push_back(std::move(system));
}

void World::UpdateSystems(const float deltaTime, const System::SystemRole currentMode) {
    for (const auto& system : systems) {
        if (system->isActive && (system->role == System::SystemRole::Shared || system->role == currentMode)) {
            system->Run(this, deltaTime);
        }
    }
}

// The heart of entity migration
void World::MoveEntity(const EntityID id, Archetype* currentArchetype, Archetype* nextArchetype) {
    EntityData& data = entityLookup[id.GetIndex()];
    const size_t indexInCurrent = data.archetypeIndex;

    // 1. Transfer components from the current archetype to the next archetype.
    // The component arrays only contain the components that their respective archetype's signature requires.
    for (auto const& [typeID, currentArrayPtr] : currentArchetype->componentArrays) {
        if (nextArchetype->componentArrays.count(typeID)) {
            IComponentArray* nextArrayPtr = nextArchetype->componentArrays.at(typeID).get();
            // Move component from old array (at indexInCurrent) to the back of the new array.
            nextArrayPtr->AddComponentFrom(currentArrayPtr.get(), indexInCurrent);
        }
        // Components that are removed (not in nextArchetype) are simply ignored here.
    }

    // 2. Handle the Swap-and-Pop cleanup within the current archetype.

    // Entity ID to swap in (last entity in the current archetype)
    const EntityID entityToSwapID = currentArchetype->entityIDs.back();

    // a) Perform Swap-and-Pop for all component arrays in the current archetype.
    for (const auto &arrayPtr: currentArchetype->componentArrays | std::views::values) {
        arrayPtr->RemoveComponent(indexInCurrent);
    }

    // b) Perform Swap-and-Pop for the entityID list.
    currentArchetype->entityIDs[indexInCurrent] = entityToSwapID;
    currentArchetype->entityIDs.pop_back();

    // 3. Update EntityData for the entity that was SWAPPED into the moved entity's slot.
    if (indexInCurrent < currentArchetype->entityIDs.size()) {
        entityLookup[entityToSwapID.GetIndex()].archetypeIndex = indexInCurrent;
    }

    // 4. Update the EntityData for the MOVED entity.
    data.archetypePtr = nextArchetype;
    data.archetypeIndex = nextArchetype->entityIDs.size(); // The index it will occupy next.

    // 5. Add the entityID to the new archetype's list.
    nextArchetype->entityIDs.push_back(id);
}


Archetype* World::GetOrCreateArchetype(const ComponentSignature& signature) {
    // 1. Try to find an existing archetype
    if (const auto it = archetypes.find(signature); it != archetypes.end()) {
        return it->second.get();
    }

    // 2. Create new archetype
    auto newArchetype = std::make_unique<Archetype>();
    newArchetype->signature = signature;

    // 3. Initialize storage arrays
    for (size_t i = 0; i < MAX_COMPONENTS; ++i) {
        if (signature.test(i)) {
            if (auto regIt = Engine::ECS::Component::componentRegistry.find(i); regIt != Engine::ECS::Component::componentRegistry.end()) {
                // Component is registered (Engine or loaded DLL)
                newArchetype->componentArrays[i] = regIt->second();
            } else {
                // DLL is likely currently unloaded.
                // We keep the bit in the signature, but leave the array slot null for now.
                // Our Save/Load fix using 'typeName' handles the data recovery.
                LOG_WARN("Archetype requires Component ID " + std::to_string(i) + " but no creator found. Skipping array creation until DLL reload.");
                newArchetype->componentArrays[i] = nullptr;
            }
        }
    }

    Archetype* rawPtr = newArchetype.get();
    archetypes[signature] = std::move(newArchetype);
    return rawPtr;
}

void World::RemoveDLLComponents(const uint32_t startID) {
    for (const auto &archetype: archetypes | std::views::values) {
        // 1. Remove the actual data arrays
        auto it = archetype->componentArrays.lower_bound(startID);
        while (it != archetype->componentArrays.end()) {
            it = archetype->componentArrays.erase(it);
        }

        // 2. Update the signature inside the archetype instance
        // We modify 'archetype->signature' because 'mapKey' is const
        for (uint32_t i = startID; i < MAX_COMPONENTS; ++i) {
            if (archetype->signature.test(i)) {
                archetype->signature.set(i, false); // No longer missing const qualifier
            }
        }
    }
}