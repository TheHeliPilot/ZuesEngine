#include "../include/Engine/ECS/World.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>

#include "../include/Engine/Core.h"
#include "../include/Engine/ECS/HierarchyOutliner.h"
#include "../include/Engine/ECS/WorldSerializationHelpers.h"

// NEW: Define the global component registry
namespace Engine::ECS::Component {
    std::map<TypeID, ComponentCreator> componentRegistry;
}

std::size_t std::hash<std::bitset<64>>::operator()(const ComponentSignature &signature) const {
    // Safe for MAX_COMPONENTS=64
    return std::hash<unsigned long long>()(signature.to_ullong());
}

EntityID World::CreateEntity() {
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
    const EntityID newID(index, generation);
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
    for (auto const& [typeID, arrayPtr] : currentArchetype->componentArrays) {
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
    // 1. Try to find an existing archetype with the exact signature
    const auto it = archetypes.find(signature);
    if (it != archetypes.end()) {
        return it->second.get();
    }

    // 2. If not found, create a new one
    auto newArchetype = std::make_unique<Archetype>();
    newArchetype->signature = signature;

    // 3. For every component type set in the signature, initialize its storage array
    for (size_t i = 0; i < MAX_COMPONENTS; ++i) {
        if (signature.test(i)) {
            auto regIt = Engine::ECS::Component::componentRegistry.find(i);
            if (regIt == Engine::ECS::Component::componentRegistry.end()) {
                 throw std::runtime_error("Attempted to create Archetype for unregistered component TypeID. Did you forget to call Engine::ECS::Component::RegisterComponent<T>()?");
            }

            // Call the factory function to create the correct ComponentArray<T>
            newArchetype->componentArrays[i] = regIt->second();
        }
    }

    Archetype* rawPtr = newArchetype.get();
    archetypes[signature] = std::move(newArchetype);

    return rawPtr;
}

std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> World::GetAllComponents(const EntityID entityID) const {
    std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> result;

    if (entityID.id >= entityLookup.size())
        return result;

    const EntityData& data = entityLookup[entityID.id];

    if (!data.archetypePtr)
        return result;

    Archetype* archetype = static_cast<Archetype*>(data.archetypePtr);
    size_t index = data.archetypeIndex;

    for (auto& [typeID, array] : archetype->componentArrays)
    {
        void* compPtr = array->GetVoidPtr(index);
        result.emplace_back(typeID, compPtr);
    }

    return result;
}
