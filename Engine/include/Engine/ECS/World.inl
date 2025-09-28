// World.inl (Core Logic)
#pragma once
#include "World.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility> // For std::index_sequence

// --- World::MoveEntity, AddComponent, RemoveComponent, GetComponent implementations remain the same ---


// =========================================================================
// Core Iteration Mechanism: World::QueryAndRun
// =========================================================================

// Helper struct to unpack the component array pointers and call the user's SystemBase::Update
template<typename SystemT, typename TupleT, std::size_t... I>
void CallSystemUpdate(SystemT* system, float deltaTime, TupleT& componentArrays, size_t index, std::index_sequence<I...>) {
    // This unpacks the tuple of component arrays and indexes each one by 'index'
    // The result is passed as arguments to the user's system->Update()
    system->Update(deltaTime,
        (std::get<I>(componentArrays) + index)... // Pointer arithmetic: array_ptr[index]
    );
}


template<typename... TArgs>
void World::QueryAndRun(SystemBase<TArgs...>* system, float deltaTime) {

    const ComponentSignature& requiredSignature = system->signature;

    // 1. Iterate over all Archetypes, filtering by signature
    for (auto const& [signature, archetypePtr] : archetypes) {
        if ((signature & requiredSignature) != requiredSignature) {
            continue;
        }

        Archetype* arch = archetypePtr.get();
        size_t count = arch->entityIDs.size();

        // 2. Get the component arrays (raw data pointers) and store them in a tuple
        // The components must be accessed as pointers (TArgs) since they are mutable.
        std::tuple<TArgs...> componentArrays = {
            static_cast<std::remove_pointer_t<TArgs>*>(arch->GetComponentArray<std::remove_pointer_t<TArgs>>()->GetData())...
        };

        // 3. Perform the INNER LOOP
        for (size_t i = 0; i < count; ++i) {

            // 4. Call the helper to unpack the pointers, index them, and call the user's Update function
            CallSystemUpdate(
                system,
                deltaTime,
                componentArrays,
                i,
                std::make_index_sequence<sizeof...(TArgs)>{} // Used for compile-time tuple indexing
            );
        }
    }
}

// Helper struct to unpack the component array pointers and call the user's lambda
// This is necessary to handle the variadic template arguments (TArgs) and the indexing (i)
template<typename LambdaT, typename TupleT, typename EntityIDT, std::size_t... I>
void CallLambdaWithReferences(LambdaT& lambda, EntityIDT entityID, TupleT& componentArrays, size_t index, std::index_sequence<I...>) {

    // FIX: The correct syntax for dereferencing the pointer arithmetic result within a fold expression
    lambda(entityID,
        *(std::get<I>(componentArrays) + index)... // CORRECTED fold expression syntax
    );
}

template<typename... TArgs>
void World::ForEachEntity(std::function<void(EntityID, TArgs&...)> lambda) {

    // 1. Calculate the required signature for the query
    ComponentSignature requiredSignature;
    // TArgs are the reference types (Position&). We need the base type (Position).
    (requiredSignature.set(Engine::ECS::Component::GetTypeID<std::remove_reference_t<TArgs>>()), ...);

    // 2. Iterate over ALL Archetypes
    for (auto const& [signature, archetypePtr] : archetypes) {

        // Filter Archetypes that do not satisfy the requirements
        if ((signature & requiredSignature) != requiredSignature) {
            continue;
        }

        Archetype* arch = archetypePtr.get();
        size_t count = arch->entityIDs.size();

        // 3. Get the component arrays (raw pointers)
        // Store raw T* pointers in a tuple for easy access.
        std::tuple<std::remove_reference_t<TArgs>*...> componentArrays = {
            arch->GetComponentArray<std::remove_reference_t<TArgs>>()->GetData()...
        };

        // 4. Perform the INNER LOOP
        for (size_t i = 0; i < count; ++i) {

            EntityID entityID = arch->entityIDs[i];

            // 5. Call the helper to unpack the pointers, index them, and execute the lambda
            CallLambdaWithReferences(
                lambda,
                entityID,
                componentArrays,
                i,
                std::make_index_sequence<sizeof...(TArgs)>{} // Used for compile-time tuple indexing
            );
        }
    }
}

// =========================================================================
// SystemBase::Run Definition (Must be AFTER World is fully defined)
// =========================================================================

template<typename... TArgs>
void SystemBase<TArgs...>::Run(World* world, float deltaTime) {
    // Calls the World's templated execution function, passing 'this' (the derived system)
    world->QueryAndRun<TArgs...>(this, deltaTime);
}