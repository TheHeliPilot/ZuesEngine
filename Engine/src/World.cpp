//
// Created by bucka on 9/28/2025.
//

#include "../include/Engine/ECS/World.h"

// ... (Implementation for World::CreateEntity, DestroyEntity, and UpdateSystems)

EntityID World::CreateEntity() {
    EntityIndex index;
    EntityGeneration generation = 0;

    if (!freeIndices.empty()) {
        index = freeIndices.back();
        freeIndices.pop_back();
        generation = entityLookup[index].generation; // Keep old generation
    } else {
        index = entityLookup.size();
        entityLookup.emplace_back();
    }

    // Find or create the default (empty) archetype
    Archetype* defaultArchetype = GetOrCreateArchetype(ComponentSignature());

    // Initialize data
    entityLookup[index] = { generation, defaultArchetype, defaultArchetype->entityIDs.size() };
    defaultArchetype->entityIDs.push_back(EntityID(index, generation));

    return EntityID(index, generation);
}

// ... (Other non-templated implementation details)

void World::UpdateSystems(float deltaTime) {
    // The base System::Run function is called, which in turn triggers
    // SystemBase::Run and then World::QueryAndRun.
    for (const auto& system : systems) {
        system->Run(this, deltaTime);
    }
}