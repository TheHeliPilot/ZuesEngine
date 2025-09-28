// World.inl (Core Logic)
#pragma once
#include "World.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <type_traits> // Make sure this is included for std::remove_pointer_t


// ... (World::AddComponent, RemoveComponent, GetComponent, HasComponent implementations remain the same) ...


// =========================================================================
// Core Iteration Mechanism: World::QueryAndRun
// =========================================================================

// Helper function to unpack the tuple of component arrays and call the user's Update function.
// TCompPtrs... is the variadic pack of component *pointer* types (TArgs...) from SystemBase.
template<typename... TCompPtrs, typename SystemT, typename TupleT, std::size_t... I>
void CallSystemUpdateImpl(SystemT* system, float deltaTime, size_t index, TupleT& componentArrays, std::index_sequence<I...>) {
    // The Update method expects component pointers (C_I*).
    // IComponentArray::GetVoidPtr(index) returns a void* pointing to the component data (C_I).
    // We must static_cast<C_I*>(void*) to get the correct pointer type for system->Update.

    system->Update(deltaTime,
        // TCompPtrs is the pack of required pointer types (e.g., PositionComponent*).
        // std::tuple_element_t<I, std::tuple<TCompPtrs...>> extracts the I-th pointer type.
        static_cast<std::tuple_element_t<I, std::tuple<TCompPtrs...>>>(
            std::get<I>(componentArrays)->GetVoidPtr(index)
        )...
    );
}

template<typename... TArgs>
void World::QueryAndRun(SystemBase<TArgs...>* system, float deltaTime) {

    const ComponentSignature& requiredSignature = system->signature;

    // 1. Iterate over all Archetypes, filtering by signature
    for (auto const& [signature, archetypePtr] : archetypes) {
        // Only run the system if the Archetype contains ALL required components
        if ((signature & requiredSignature) == requiredSignature) {

            Archetype* archetype = archetypePtr.get();
            if (archetype->entityIDs.empty()) continue;

            // 2. Gather component arrays required by the system
            // ComponentArrayTuple is a tuple of IComponentArray* pointers, one for each TArgs type.
            using ComponentArrayTuple = std::tuple<
                std::conditional_t<true, IComponentArray*, TArgs>...
            >;

            ComponentArrayTuple componentArrays = std::make_tuple(
                archetype->componentArrays.at(
                    Engine::ECS::Component::GetTypeID<std::remove_pointer_t<TArgs>>()
                ).get()...
            );

            // 3. Iterate over all entities in this Archetype
            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {

                // Call the helper to unpack the component arrays and run the system.
                // We explicitly pass TArgs... (the component pointer types) to the helper.
                CallSystemUpdateImpl<TArgs...>(system, deltaTime, i, componentArrays, std::make_index_sequence<sizeof...(TArgs)>{});
            }
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

// ... (AddComponent, RemoveComponent, GetComponent, HasComponent template definitions below) ...

// =========================================================================
// Templated API Definitions
// =========================================================================

template<typename T>
void World::AddComponent(EntityID entityID, const T& component) {
    // 1. Get the entity's current data and perform validation
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to AddComponent to invalid EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    // If component already exists, overwrite it and return
    if (currentArchetype->signature.test(componentID)) {
        currentArchetype->GetComponentArray<T>()->Get(data.archetypeIndex) = component;
        return;
    }

    // 2. Calculate the next signature
    ComponentSignature nextSignature = currentArchetype->signature;
    nextSignature.set(componentID);

    // 3. Get or create the new archetype
    Archetype* nextArchetype = GetOrCreateArchetype(nextSignature);

    // 4. Move the entity from the current archetype to the new one
    MoveEntity(entityID, currentArchetype, nextArchetype);

    // 5. Add the new component (T) to the new archetype's component array
    // This relies on the MoveEntity function handling component transfers for all *other* components.
    // The new component array for T will exist because GetOrCreateArchetype ensured it.
    // The vector is still sized 0 here, so we push_back.
    nextArchetype->GetComponentArray<T>()->data.push_back(component);
}

template<typename T>
void World::RemoveComponent(EntityID entityID) {
    // 1. Get the entity's current data and validate
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to RemoveComponent from invalid EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    // Check if the component exists
    if (!currentArchetype->signature.test(componentID)) {
        return; // Component does not exist, do nothing
    }

    // 2. Calculate the next signature
    ComponentSignature nextSignature = currentArchetype->signature;
    nextSignature.reset(componentID);

    // 3. Get or create the new archetype
    Archetype* nextArchetype = GetOrCreateArchetype(nextSignature);

    // 4. Move the entity from the current archetype to the new one.
    // The MoveEntity function must handle the component removal/skipping of component T.
    MoveEntity(entityID, currentArchetype, nextArchetype);
}

template<typename T>
T& World::GetComponent(EntityID entityID) {
    // 1. Get the entity's current data and validate
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to GetComponent from invalid/destroyed EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);

    // Check if the entity's signature contains the component, which is the most efficient check.
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();
    if (!currentArchetype->signature.test(componentID)) {
        throw std::runtime_error("Entity does not have the requested component.");
    }

    // Retrieve the component array and data
    ComponentArray<T>* array = currentArchetype->GetComponentArray<T>();
    // array should never be nullptr if the signature check passed, but a safety check is fine.
    if (!array) {
        // This case should ideally not happen if GetOrCreateArchetype works correctly.
        throw std::runtime_error("Internal ECS error: Archetype signature present, but component array not found.");
    }

    return array->Get(data.archetypeIndex);
}

template<typename T>
bool World::HasComponent(EntityID entityID) {
    // 1. Get the entity's current data and validate
    EntityIndex index = entityID.GetIndex();

    // Perform bounds and generation check efficiently
    if (index >= entityLookup.size() || entityLookup[index].generation != entityID.GetGeneration() || !entityLookup[index].archetypePtr) {
        return false;
    }

    Archetype* currentArchetype = static_cast<Archetype*>(entityLookup[index].archetypePtr);
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    return currentArchetype->signature.test(componentID);
}
