// World.inl (Core Logic)
#pragma once
#include "World.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <type_traits> // Make sure this is included for std::remove_pointer_t

#include "Components.h"
#include "../EngineDefines.h"

// ... (World::AddComponent, RemoveComponent, GetComponent, HasComponent implementations remain the same) ...


// =========================================================================
// Core Iteration Mechanism: World::QueryAndRun
// =========================================================================

template<typename... TArgs>
ComponentSignature World::CalculateSignature() const {
    ComponentSignature signature;
    // TArgs are expected to be component POINTER types (e.g., TransformComponent*)
    (signature.set(Engine::ECS::Component::GetTypeID<std::remove_pointer_t<TArgs>>()), ...);
    return signature;
}

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


// =========================================================================
// Templated API Definitions
// =========================================================================

template<typename T>
void World::AddComponent(const EntityID entityID, const T& component) {

    if (!IsComponentRegistered<T>()) {
        // Use typeid(T).name() to generate a unique string name.
        // This string will be compiler-specific (mangled), but it is unique
        // and avoids requiring changes to the component structs.
        const std::string componentName = typeid(T).name();

        // Automatically register the component with the generated name
        RegisterComponent<T>(componentName);
    }

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
void World::RemoveComponent(const EntityID entityID) {
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
T& World::GetComponent(const EntityID entityID) {
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
bool World::HasComponent(const EntityID entityID) const {
    // 1. Get the entity's current data and validate
    EntityIndex index = entityID.GetIndex();

    // Perform bounds and generation check efficiently
    if (index >= entityLookup.size() || entityLookup[index].generation != entityID.GetGeneration() || !entityLookup[index].archetypePtr) {
        return false;
    }

    const auto currentArchetype = static_cast<Archetype*>(entityLookup[index].archetypePtr);
    const Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    return currentArchetype->signature.test(componentID);
}

template<typename T>
bool World::IsComponentRegistered() {
    Engine::ECS::Component::TypeID id = Engine::ECS::Component::GetTypeID<T>();
    return componentSerializationRegistry.serializers.count(id) > 0;
}

template<typename T>
void World::RegisterComponent(const std::string& typeName) {
    // 1. Ensures the static TypeID for this component is generated
    Engine::ECS::Component::TypeID id = Engine::ECS::Component::GetTypeID<T>();

    // 2. Registers the component type with the serialization registry.
    componentSerializationRegistry.RegisterComponent<T>(typeName);
}

inline bool World::SaveToJson(const std::string& filename) const {
    std::stringstream ss_start;
    ss_start << "World Save: Starting save process to file: " << filename;
    LOG_INFO(ss_start.str());

    Engine::WorldSnapshot snapshot;

    // Iterate over the entity lookup table to find active entities
    for (EntityIndex i = 0; i < entityLookup.size(); ++i) {
        const EntityData& data = entityLookup[i];

        // Only process entities that are valid and in an archetype
        if (data.archetypePtr && data.generation == EntityID(i, data.generation).GetGeneration()) {

            EntityID currentEntityID = EntityID(i, data.generation);
            std::stringstream ss_entity;
            ss_entity << "  Serializing Entity ID: " << currentEntityID.id
                      << " (Index: " << i << ", Archetype Index: " << data.archetypeIndex << ")";
            LOG_INFO(ss_entity.str());

            auto currentArchetype = static_cast<Archetype*>(data.archetypePtr);
            Engine::SerializedEntity se;
            se.id = currentEntityID;

            // 1. Collect all components for this entity
            for (const auto& pair : currentArchetype->componentArrays) {
                Engine::ECS::Component::TypeID compID = pair.first;
                IComponentArray* compArray = pair.second.get();

                // 2. Get the type-erased serializer
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(compID);

                // 3. Use the serializer to extract the component data as JSON
                json componentData = serializer->SerializeComponent(compArray, data.archetypeIndex);

                Engine::SerializedComponent sc;
                sc.typeID = compID;
                sc.data = componentData;
                se.components.push_back(std::move(sc));

                // Log the serialized component data.
                std::stringstream ss_comp;
                ss_comp << "    - Component ID " << compID << " serialized data: " << componentData.dump();
                LOG_INFO(ss_comp.str());
            }

            snapshot.entities.push_back(std::move(se));
        }
    }

    try {
        json j = snapshot;

        std::ofstream ofs(filename);
        if (!ofs.is_open()) {
            std::stringstream ss_error;
            ss_error << "World Save: FAILED to open file for writing: " << filename;
            LOG_ERROR(ss_error.str());
            return false;
        }
        ofs << j.dump(4);
        std::stringstream ss_success;
        ss_success << "World Save: SUCCESS! Snapshot written to " << filename;
        LOG_INFO(ss_success.str());
        return true;
    } catch (const std::exception& e) {
        std::stringstream ss_catch;
        ss_catch << "World Save: Exception during file writing: " << e.what();
        LOG_ERROR(ss_catch.str());
        return false;
    }
}

inline bool World::LoadFromJson(const std::string& filename) {
    std::stringstream ss_start;
    ss_start << "World Load: Starting load process from file: " << filename;
    LOG_INFO(ss_start.str());

    try {
        std::ifstream ifs(filename);
        if (!ifs.is_open()) {
            std::stringstream ss_error;
            ss_error << "Filestream didnt open! File: " << filename;
            LOG_ERROR(ss_error.str());
            return false;
        }

        json j;
        ifs >> j;
        LOG_INFO("World Load: Successfully parsed JSON from file.");

        const auto [version, entities] = j.get<Engine::WorldSnapshot>();
        std::stringstream ss_count;
        ss_count << "World Load: WorldSnapshot has " << entities.size() << " entities.";
        LOG_INFO(ss_count.str());

        // --- Clean up current world state ---
        entityLookup.clear();
        freeIndices.clear();
        archetypes.clear();
        LOG_INFO("World Load: Cleared existing world state.");

        // 1. Process all entities in the snapshot
        for (const auto&[id, components] : entities) {
            // Reconstruct the entity ID/index/generation
            EntityIndex index = id.GetIndex();
            EntityGeneration generation = id.GetGeneration();
            std::stringstream ss_entity;
            ss_entity << "  Loading Entity ID: " << id.id << " (Target Index: " << index << ", Generation: " << generation << ")";
            LOG_INFO(ss_entity.str());

            // Grow the lookup table if necessary
            if (index >= entityLookup.size()) {
                entityLookup.resize(index + 1);
                std::stringstream ss_resize;
                ss_resize << "    Resizing entity lookup table to size: " << entityLookup.size();
                LOG_INFO(ss_resize.str());
            }

            // 2. Determine the component signature for the target archetype
            ComponentSignature newSignature;
            for (const auto& sc : components) {
                newSignature.set(sc.typeID);
            }
            std::stringstream ss_sig;
            ss_sig << "    Determined new Component Signature based on " << components.size() << " components.";
            LOG_INFO(ss_sig.str());

            // 3. Get or create the target archetype
            Archetype* targetArchetype = GetOrCreateArchetype(newSignature);
            LOG_INFO("    Archetype found/created for signature.");

            // 4. Update the entity lookup data
            EntityData& data = entityLookup[index];
            data.generation = generation;
            data.archetypePtr = targetArchetype;
            data.archetypeIndex = targetArchetype->entityIDs.size(); // New component index
            targetArchetype->entityIDs.push_back(id);
            std::stringstream ss_register;
            ss_register << "    Entity registered to Archetype. New archetype index: " << data.archetypeIndex;
            LOG_INFO(ss_register.str());


            // 5. Populate component data
            for (const auto& sc : components) {
                // Get the type-erased serializer from the registry
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(sc.typeID);

                // Find the correct IComponentArray within the target archetype
                auto compArrayIt = targetArchetype->componentArrays.find(sc.typeID);
                if (compArrayIt == targetArchetype->componentArrays.end()) {
                    throw std::runtime_error("Internal ECS error: Archetype component array missing after creation.");
                }

                // Use the serializer's type-erased method to deserialize the JSON data
                // and add the component to the array.
                serializer->DeserializeAndAdd(compArrayIt->second.get(), sc.data);
                std::stringstream ss_comp;
                ss_comp << "      - Component ID " << sc.typeID << " Deserialized. Data: " << sc.data.dump(2);
                LOG_INFO(ss_comp.str());
            }
        }

        // 6. Recalculate free indices for future entity creation
        size_t freeCount = 0;
        for (size_t i = 0; i < entityLookup.size(); ++i) {
            if (!entityLookup[i].archetypePtr) {
                freeIndices.push_back(i);
                freeCount++;
            }
        }
        std::stringstream ss_free;
        ss_free << "World Load: Finished. Recalculated " << freeCount << " free entity indices.";
        LOG_INFO(ss_free.str());

        return true;
    } catch (const std::exception& e) {
        std::stringstream ss_catch;
        ss_catch << "World Load: Exception during world load: " << e.what();
        LOG_ERROR(ss_catch.str());
        return false;
    }
}

// TArgs... are the component POINTER types (e.g., TransformComponent*)
template<typename... TArgs, typename Func>
void World::ForEach(Func&& func) {
    // 1. Calculate the required signature based on the template arguments
    // FIX: Added 'this->template' to correctly resolve the dependent template name 'CalculateSignature'.
    const ComponentSignature requiredSignature = this->template CalculateSignature<TArgs...>();

    // 2. Iterate over all archetypes
    for (auto const& [sig, archetypePtr] : archetypes) {
        // 3. Filter: Check if the archetype contains ALL required components
        if ((sig & requiredSignature) == requiredSignature) {
            Archetype* archetype = archetypePtr.get();
            if (archetype->entityIDs.empty()) continue;

            // 4. Gather the component arrays and EntityIDs into a tuple for fast access
            // TArgs are the pointer types (e.g., TransformComponent*).
            // We need a tuple of ComponentArray<T>* pointers.
            using ComponentTuple = std::tuple<ComponentArray<std::remove_pointer_t<TArgs>>*...>;

            ComponentTuple componentArrays(archetype->GetComponentArray<std::remove_pointer_t<TArgs>>()...);

            // 5. Run the inner loop
            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {

                // Get the pointers to the components and the EntityID
                // The lambda (Func) is expected to have a signature like:
                // `(EntityID entityID, ComponentType1* c1, ComponentType2* c2, ...)`

                // FIX: Implemented correct tuple expansion using a nested lambda and std::index_sequence
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    func(
                        archetype->entityIDs[i],
                        // Correct Expansion: Use the index sequence I... to get the Nth element
                        // from the componentArrays tuple and cast its void* result correctly.
                        static_cast<std::remove_pointer_t<std::tuple_element_t<I, std::tuple<TArgs...> > > *>(std::get<I>(componentArrays)->
                            GetVoidPtr(i)) ...
                    );
                }(std::index_sequence_for<TArgs...>{});
            }
        }
    }
}

template<typename T>
void World::SetComponentData(const EntityID entityID, const T& componentData) {
    if (!entityID.IsValid()) {
        throw std::runtime_error("Cannot set component data for invalid entity.");
    }

    T& existingData = GetComponent<T>(entityID);

    existingData = componentData;
}

inline std::set<EntityID> World::GetEntityChildren(const EntityID parentID) const {
    std::set<EntityID> children;

    // Calculate signature for TransformComponent.
    ComponentSignature requiredSignature;
    requiredSignature.set(Engine::ECS::Component::GetTypeID<Engine::ECS::Component::TransformComponent>());

    // Iterate over all archetypes
    for (auto const& [sig, archetypePtr] : archetypes) {
        // 1. Check if the archetype contains the TransformComponent
        if ((sig & requiredSignature) == requiredSignature) {
            Archetype* archetype = archetypePtr.get();
            if (archetype->entityIDs.empty()) continue;

            // 2. Get the TransformComponent array
            ComponentArray<Engine::ECS::Component::TransformComponent>* transformArray =
                archetype->GetComponentArray<Engine::ECS::Component::TransformComponent>();

            if (!transformArray) continue;

            // 3. Loop through all entities in this archetype
            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {

                // Get the entity's transform data

                // 4. Check if this entity's parent matches the requested parentID
                //    (including NULL_ENTITY_ID for roots)
                if (const Engine::ECS::Component::TransformComponent& transform = transformArray->data[i]; transform.parent == parentID) {
                    children.insert(archetype->entityIDs[i]);
                }
            }
        }
    }
    return children;
}