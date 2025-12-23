// World.inl (Core Logic)
#pragma once
#include "World.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <type_traits>

#include "Components.h"
#include "../EngineDefines.h"

// =========================================================================
// Core Iteration Mechanism: World::QueryAndRun
// =========================================================================

template<typename... TArgs>
ComponentSignature World::CalculateSignature() const {
    ComponentSignature signature;
    (signature.set(Engine::ECS::Component::GetTypeID<std::remove_pointer_t<TArgs>>()), ...);
    return signature;
}

template<typename... TCompPtrs, typename SystemT, typename TupleT, std::size_t... I>
void CallSystemUpdateImpl(SystemT* system, float deltaTime, size_t index, TupleT& componentArrays, std::index_sequence<I...>) {
    system->Update(deltaTime,
        static_cast<std::tuple_element_t<I, std::tuple<TCompPtrs...>>>(
            std::get<I>(componentArrays)->GetVoidPtr(index)
        )...
    );
}

template<typename... TArgs>
void World::QueryAndRun(SystemBase<TArgs...>* system, float deltaTime) {
    const ComponentSignature& requiredSignature = system->signature;

    for (auto const& [signature, archetypePtr] : archetypes) {
        if ((signature & requiredSignature) == requiredSignature) {
            Archetype* archetype = archetypePtr.get();
            if (archetype->entityIDs.empty()) continue;

            using ComponentArrayTuple = std::tuple<
                std::conditional_t<true, IComponentArray*, TArgs>...
            >;

            ComponentArrayTuple componentArrays = std::make_tuple(
                archetype->componentArrays.at(
                    Engine::ECS::Component::GetTypeID<std::remove_pointer_t<TArgs>>()
                ).get()...
            );

            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {
                CallSystemUpdateImpl<TArgs...>(system, deltaTime, i, componentArrays, std::make_index_sequence<sizeof...(TArgs)>{});
            }
        }
    }
}

// =========================================================================
// SystemBase::Run Definition
// =========================================================================

template<typename... TArgs>
void SystemBase<TArgs...>::Run(World* world, float deltaTime) {
    world->QueryAndRun<TArgs...>(this, deltaTime);
}

// =========================================================================
// Templated API Definitions
// =========================================================================

template<typename T>
void World::AddComponent(const EntityID entityID, const T& component) {
    if (!IsComponentRegistered<T>()) {
        const std::string componentName = typeid(T).name();
        RegisterComponent<T>(componentName);
    }

    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to AddComponent to invalid EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    if (currentArchetype->signature.test(componentID)) {
        currentArchetype->GetComponentArray<T>()->Get(data.archetypeIndex) = component;
        return;
    }

    ComponentSignature nextSignature = currentArchetype->signature;
    nextSignature.set(componentID);

    Archetype* nextArchetype = GetOrCreateArchetype(nextSignature);
    MoveEntity(entityID, currentArchetype, nextArchetype);
    nextArchetype->GetComponentArray<T>()->data.push_back(component);
}

template<typename T>
void World::RemoveComponent(const EntityID entityID) {
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to RemoveComponent from invalid EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    if (!currentArchetype->signature.test(componentID)) {
        return;
    }

    ComponentSignature nextSignature = currentArchetype->signature;
    nextSignature.reset(componentID);

    Archetype* nextArchetype = GetOrCreateArchetype(nextSignature);
    MoveEntity(entityID, currentArchetype, nextArchetype);
}

template<typename T>
T& World::GetComponent(const EntityID entityID) {
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to GetComponent from invalid/destroyed EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);
    Engine::ECS::Component::TypeID componentID = Engine::ECS::Component::GetTypeID<T>();

    if (!currentArchetype->signature.test(componentID)) {
        throw std::runtime_error("Entity does not have the requested component.");
    }

    ComponentArray<T>* array = currentArchetype->GetComponentArray<T>();
    if (!array) {
        throw std::runtime_error("Internal ECS error: Archetype signature present, but component array not found.");
    }

    return array->Get(data.archetypeIndex);
}

template<typename T>
bool World::HasComponent(const EntityID entityID) const {
    EntityIndex index = entityID.GetIndex();

    if (index >= entityLookup.size() ||
        entityLookup[index].generation != entityID.GetGeneration() ||
        !entityLookup[index].archetypePtr) {
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
    Engine::ECS::Component::TypeID id = Engine::ECS::Component::GetTypeID<T>();
    componentSerializationRegistry.RegisterComponent<T>(typeName);
}

template<typename... TArgs, typename Func>
void World::ForEach(Func&& func) {
    const ComponentSignature requiredSignature = this->template CalculateSignature<TArgs...>();

    for (auto const& [sig, archetypePtr] : archetypes) {
        if ((sig & requiredSignature) == requiredSignature) {
            Archetype* archetype = archetypePtr.get();
            if (archetype->entityIDs.empty()) continue;

            using ComponentTuple = std::tuple<ComponentArray<std::remove_pointer_t<TArgs>>*...>;
            ComponentTuple componentArrays(archetype->GetComponentArray<std::remove_pointer_t<TArgs>>()...);

            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    func(
                        archetype->entityIDs[i],
                        static_cast<std::remove_pointer_t<std::tuple_element_t<I, std::tuple<TArgs...>>>*>(
                            std::get<I>(componentArrays)->GetVoidPtr(i)
                        )...
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

// =========================================================================
// Non-templated inline functions
// =========================================================================

inline bool World::SaveToJson(const std::string& filename) const {
    std::stringstream ss_start;
    ss_start << "World Save: Starting save process to file: " << filename;
    LOG_INFO(ss_start.str());

    Engine::WorldSnapshot snapshot;

    // Get the TypeID for ViewportCameraTag to skip editor-only entities
    const Engine::ECS::Component::TypeID viewportCameraTagID =
        Engine::ECS::Component::GetTypeID<Engine::ECS::Component::ViewportCameraTag>();

    for (EntityIndex i = 0; i < entityLookup.size(); ++i) {
        if (const auto&[generation, archetypePtr, archetypeIndex, hasUnknown] = entityLookup[i]; archetypePtr && generation == EntityID(i, generation).GetGeneration()) {

            auto currentArchetype = static_cast<Archetype*>(archetypePtr);

            // Skip editor-only entities (those with ViewportCameraTag)
            if (currentArchetype->signature.test(viewportCameraTagID)) {
                continue;
            }

            // 🌟 CRITICAL FIX: Retrieve the EXISTING EntityID object from the Archetype's list.
            // This ensures we get the stored 'name' field.
            EntityID currentEntityID = currentArchetype->entityIDs.at(archetypeIndex);
            std::stringstream ss_entity;
            ss_entity << "  Serializing Entity ID: " << currentEntityID.id
                      << " (Index: " << i << ", Archetype Index: " << archetypeIndex << ") [Name: " << currentEntityID.name << "]";
            LOG_INFO(ss_entity.str());

            Engine::SerializedEntity se;
            se.id = currentEntityID; // se.id now contains the correct name for JSON serialization

            for (const auto& [compID, snd] : currentArchetype->componentArrays) {
                IComponentArray* compArray = snd.get();
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(compID);

                Engine::SerializedComponent sc;
                sc.typeID = compID;
                sc.data = serializer->SerializeComponent(compArray, archetypeIndex);

                // Resolve the name (e.g., "PlayerComponent") so it's saved to the file
                sc.typeName = componentSerializationRegistry.GetTypeName(compID);

                se.components.push_back(std::move(sc));
            }

            // Also include any dormant components for this entity so we don't lose data
            EntityIndex entityIndex = currentEntityID.GetIndex();
            if (auto dormantIt = dormantData.find(entityIndex); dormantIt != dormantData.end()) {
                for (const auto& dormantComp : dormantIt->second) {
                    se.components.push_back(dormantComp);
                }
            }

            snapshot.entities.push_back(std::move(se));
        }
    }

    // Save active system states (non-required systems only)
    snapshot.activeSystems = GetActiveSystemStates();

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
            LOG_ERROR("Filestream didnt open! File: " + filename);
            return false;
        }

        json j;
        ifs >> j;
        LOG_INFO("World Load: Successfully parsed JSON from file.");

        // We load into a local object so we can modify the components inside it
        auto snapshot = j.get<Engine::WorldSnapshot>();
        auto& entities = snapshot.entities;

        std::stringstream ss_count;
        ss_count << "World Load: WorldSnapshot has " << entities.size() << " entities.";
        LOG_INFO(ss_count.str());

        // Reset world state
        entityLookup.clear();
        freeIndices.clear();
        archetypes.clear();
        dormantData.clear();  // Clear any previous dormant data
        LOG_INFO("World Load: Cleared existing world state.");

        // 1. First Pass: Resolve TypeIDs and create Archetypes
        for (auto&[id, components] : entities) {
            EntityIndex index = id.GetIndex();
            EntityGeneration generation = id.GetGeneration();

            if (index >= entityLookup.size()) {
                entityLookup.resize(index + 1);
            }

            ComponentSignature newSignature;
            std::vector<Engine::SerializedComponent> unknownComponents;
            std::vector<Engine::SerializedComponent> knownComponents;

            // Loop through components by reference to allow modification of typeID
            for (auto& sc : components) {
                // Check if component has a valid serializer (not just a name)
                bool hasValidSerializer = false;

                if (!sc.typeName.empty() && componentSerializationRegistry.HasName(sc.typeName)) {
                    // Update the volatile ID based on the persistent Name
                    sc.typeID = componentSerializationRegistry.GetIDByName(sc.typeName);
                    // Must also have a serializer - name alone isn't enough (DLL might be unloaded)
                    hasValidSerializer = (componentSerializationRegistry.GetSerializer(sc.typeID) != nullptr);
                } else if (sc.typeName.empty() && sc.typeID < MAX_COMPONENTS) {
                    // Engine component without typeName (backwards compatibility)
                    hasValidSerializer = (componentSerializationRegistry.GetSerializer(sc.typeID) != nullptr);
                }

                if (hasValidSerializer) {
                    newSignature.set(sc.typeID);
                    knownComponents.push_back(sc);
                } else {
                    // Unknown component from missing DLL - store it as dormant
                    // DO NOT add to signature - entity stays in archetype with known components only
                    LOG_WARN("Component '" + sc.typeName + "' is currently unknown. Storing as dormant data.");
                    unknownComponents.push_back(sc);
                }
            }

            Archetype* targetArchetype = GetOrCreateArchetype(newSignature);

            EntityData& data = entityLookup[index];
            data.generation = generation;
            data.archetypePtr = targetArchetype;
            data.archetypeIndex = targetArchetype->entityIDs.size();

            // Store dormant component data if we have any unknown components
            if (!unknownComponents.empty()) {
                dormantData[index] = std::move(unknownComponents);
                data.hasUnknownComponents = true;
            } else {
                data.hasUnknownComponents = false;
            }

            // Register the entity ID
            targetArchetype->entityIDs.push_back(id);

            // 2. Second Pass: Deserialize ONLY known component data
            for (const auto& sc : knownComponents) {
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(sc.typeID);

                if (!serializer) {
                    LOG_ERROR("No serializer found for Component ID: " + std::to_string(sc.typeID));
                    continue;
                }

                auto compArrayIt = targetArchetype->componentArrays.find(sc.typeID);
                if (compArrayIt != targetArchetype->componentArrays.end() && compArrayIt->second != nullptr) {
                    serializer->DeserializeAndAdd(compArrayIt->second.get(), sc.data);
                }
            }
        }

        // Rebuild free list
        size_t freeCount = 0;
        for (size_t i = 0; i < entityLookup.size(); ++i) {
            if (!entityLookup[i].archetypePtr) {
                freeIndices.push_back(i);
                freeCount++;
            }
        }

        // Log summary of dormant data
        if (!dormantData.empty()) {
            LOG_WARN("World Load: " + std::to_string(dormantData.size()) + " entities have unknown components. Load the DLL to recover.");
        }

        // Apply system states from the snapshot
        if (!snapshot.activeSystems.empty()) {
            ApplySystemStates(snapshot.activeSystems);
            LOG_INFO("World Load: Applied " + std::to_string(snapshot.activeSystems.size()) + " system states.");
        }

        LOG_INFO("World Load: Finished. Recalculated " + std::to_string(freeCount) + " free indices.");

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("World Load: Exception: " + std::string(e.what()));
        return false;
    }
}

inline std::set<EntityID> World::GetEntityChildren(const EntityID parentID) const {
    std::set<EntityID> children;

    ComponentSignature requiredSignature;
    requiredSignature.set(Engine::ECS::Component::GetTypeID<Engine::ECS::Component::TransformComponent>());

    for (auto const& [sig, archetypePtr] : archetypes) {
        if ((sig & requiredSignature) == requiredSignature) {
            Archetype* archetype = archetypePtr.get();
            if (archetype->entityIDs.empty()) continue;

            ComponentArray<Engine::ECS::Component::TransformComponent>* transformArray =
                archetype->GetComponentArray<Engine::ECS::Component::TransformComponent>();

            if (!transformArray) continue;

            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {
                if (const Engine::ECS::Component::TransformComponent& transform = transformArray->data[i];
                    transform.parent == parentID) {
                    children.insert(archetype->entityIDs[i]);
                }
            }
        }
    }
    return children;
}

// =========================================================================
// Type-erased component operations (for runtime/inspector use)
// =========================================================================

inline void World::RemoveComponentByType(const EntityID entityID, const Engine::ECS::Component::TypeID typeID) {
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to RemoveComponentByType from invalid EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);

    if (!currentArchetype->signature.test(typeID)) {
        return;
    }

    ComponentSignature nextSignature = currentArchetype->signature;
    nextSignature.reset(typeID);

    Archetype* nextArchetype = GetOrCreateArchetype(nextSignature);
    MoveEntity(entityID, currentArchetype, nextArchetype);
}

inline bool World::HasComponent(const EntityID entityID, const Engine::ECS::Component::TypeID typeID) const {
    EntityIndex index = entityID.GetIndex();

    if (index >= entityLookup.size() ||
        entityLookup[index].generation != entityID.GetGeneration() ||
        !entityLookup[index].archetypePtr) {
        return false;
    }

    const auto currentArchetype = static_cast<Archetype*>(entityLookup[index].archetypePtr);
    return currentArchetype->signature.test(typeID);
}

inline void World::AddComponentByType(const EntityID entityID, const Engine::ECS::Component::TypeID typeID) {
    EntityData& data = entityLookup[entityID.GetIndex()];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        throw std::runtime_error("Attempted to AddComponentByType to invalid EntityID.");
    }

    Archetype* currentArchetype = static_cast<Archetype*>(data.archetypePtr);

    if (currentArchetype->signature.test(typeID)) {
        return;
    }

    ComponentSignature nextSignature = currentArchetype->signature;
    nextSignature.set(typeID);

    Archetype* nextArchetype = GetOrCreateArchetype(nextSignature);
    MoveEntity(entityID, currentArchetype, nextArchetype);

    Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(typeID);
    if (!serializer) {
        throw std::runtime_error("Component serializer not found for typeID");
    }

    auto arrayIt = nextArchetype->componentArrays.find(typeID);
    if (arrayIt == nextArchetype->componentArrays.end()) {
        throw std::runtime_error("Internal ECS error: Component array not found in archetype");
    }

    nlohmann::json defaultJson = nlohmann::json::object();
    serializer->DeserializeAndAdd(arrayIt->second.get(), defaultJson);
}

inline std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> World::GetAllComponents(const EntityID entityID) const {
    std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> result;

    EntityIndex index = entityID.GetIndex();
    if (index >= entityLookup.size()) {
        return result;
    }

    const EntityData& data = entityLookup[index];
    if (data.generation != entityID.GetGeneration() || !data.archetypePtr) {
        return result;
    }

    Archetype* archetype = static_cast<Archetype*>(data.archetypePtr);

    for (const auto& [typeID, componentArray] : archetype->componentArrays) {
        void* componentPtr = componentArray->GetVoidPtr(data.archetypeIndex);
        result.emplace_back(typeID, componentPtr);
    }

    return result;
}

// Non-const overload (calls const version)
inline std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> World::GetAllComponents(const EntityID entityID) {
    return const_cast<const World*>(this)->GetAllComponents(entityID);
}

// =========================================================================
// Dormant Component Access Methods
// =========================================================================

inline const std::vector<Engine::SerializedComponent>& World::GetDormantComponents(EntityID entityID) const {
    static const std::vector<Engine::SerializedComponent> empty;
    auto it = dormantData.find(entityID.GetIndex());
    if (it != dormantData.end()) {
        return it->second;
    }
    return empty;
}

inline bool World::HasDormantComponents(EntityID entityID) const {
    return dormantData.find(entityID.GetIndex()) != dormantData.end();
}

inline void World::ClearDormantComponents(EntityID entityID) {
    dormantData.erase(entityID.GetIndex());
    if (entityID.GetIndex() < entityLookup.size()) {
        entityLookup[entityID.GetIndex()].hasUnknownComponents = false;
    }
}

inline bool World::EntityHasUnknownComponents(EntityID entityID) const {
    EntityIndex index = entityID.GetIndex();
    if (index >= entityLookup.size()) return false;
    if (entityLookup[index].generation != entityID.GetGeneration()) return false;
    return entityLookup[index].hasUnknownComponents;
}

inline bool World::HasAnyEntitiesWithUnknownComponents() const {
    return !dormantData.empty();
}

// =========================================================================
// Hot-Reload Recovery Methods (ReloadAndHeal)
// =========================================================================

inline nlohmann::json World::SerializeToMemory() const {
    Engine::WorldSnapshot snapshot;

    // Get the TypeID for ViewportCameraTag to skip editor-only entities
    const Engine::ECS::Component::TypeID viewportCameraTagID =
        Engine::ECS::Component::GetTypeID<Engine::ECS::Component::ViewportCameraTag>();

    for (EntityIndex i = 0; i < entityLookup.size(); ++i) {
        if (const auto&[generation, archetypePtr, archetypeIndex, hasUnknown] = entityLookup[i];
            archetypePtr && generation == EntityID(i, generation).GetGeneration()) {

            auto currentArchetype = static_cast<Archetype*>(archetypePtr);

            // Skip editor-only entities
            if (currentArchetype->signature.test(viewportCameraTagID)) {
                continue;
            }

            EntityID currentEntityID = currentArchetype->entityIDs.at(archetypeIndex);

            Engine::SerializedEntity se;
            se.id = currentEntityID;

            for (const auto& [compID, snd] : currentArchetype->componentArrays) {
                if (!snd) continue;  // Skip null arrays (DLL unloaded)

                IComponentArray* compArray = snd.get();
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(compID);

                if (!serializer) continue;  // Skip if no serializer (DLL unloaded)

                Engine::SerializedComponent sc;
                sc.typeID = compID;
                sc.data = serializer->SerializeComponent(compArray, archetypeIndex);
                sc.typeName = componentSerializationRegistry.GetTypeName(compID);

                se.components.push_back(std::move(sc));
            }

            // Also include any dormant components
            EntityIndex entityIndex = currentEntityID.GetIndex();
            if (auto dormantIt = dormantData.find(entityIndex); dormantIt != dormantData.end()) {
                for (const auto& dormantComp : dormantIt->second) {
                    se.components.push_back(dormantComp);
                }
            }

            snapshot.entities.push_back(std::move(se));
        }
    }

    return nlohmann::json(snapshot);
}

inline void World::LoadFromMemory(const nlohmann::json& snapshotJson) {
    LOG_INFO("World Load: Loading from memory snapshot...");

    try {
        auto snapshot = snapshotJson.get<Engine::WorldSnapshot>();
        auto& entities = snapshot.entities;

        LOG_INFO("World Load: Snapshot has " + std::to_string(entities.size()) + " entities.");

        // Reset world state
        entityLookup.clear();
        freeIndices.clear();
        archetypes.clear();
        dormantData.clear();

        // 1. First Pass: Resolve TypeIDs and create Archetypes
        for (auto&[id, components] : entities) {
            EntityIndex index = id.GetIndex();
            EntityGeneration generation = id.GetGeneration();

            if (index >= entityLookup.size()) {
                entityLookup.resize(index + 1);
            }

            ComponentSignature newSignature;
            std::vector<Engine::SerializedComponent> unknownComponents;
            std::vector<Engine::SerializedComponent> knownComponents;

            for (auto& sc : components) {
                // Check if component has a valid serializer (not just a name)
                bool hasValidSerializer = false;

                if (!sc.typeName.empty() && componentSerializationRegistry.HasName(sc.typeName)) {
                    sc.typeID = componentSerializationRegistry.GetIDByName(sc.typeName);
                    // Must also have a serializer - name alone isn't enough (DLL might be unloaded)
                    hasValidSerializer = (componentSerializationRegistry.GetSerializer(sc.typeID) != nullptr);
                } else if (sc.typeName.empty() && sc.typeID < MAX_COMPONENTS) {
                    hasValidSerializer = (componentSerializationRegistry.GetSerializer(sc.typeID) != nullptr);
                }

                if (hasValidSerializer) {
                    newSignature.set(sc.typeID);
                    knownComponents.push_back(sc);
                } else {
                    LOG_WARN("Component '" + sc.typeName + "' is currently unknown. Storing as dormant data.");
                    unknownComponents.push_back(sc);
                }
            }

            Archetype* targetArchetype = GetOrCreateArchetype(newSignature);

            EntityData& data = entityLookup[index];
            data.generation = generation;
            data.archetypePtr = targetArchetype;
            data.archetypeIndex = targetArchetype->entityIDs.size();

            if (!unknownComponents.empty()) {
                dormantData[index] = std::move(unknownComponents);
                data.hasUnknownComponents = true;
            } else {
                data.hasUnknownComponents = false;
            }

            targetArchetype->entityIDs.push_back(id);

            // 2. Second Pass: Deserialize ONLY known component data
            for (const auto& sc : knownComponents) {
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(sc.typeID);

                if (!serializer) continue;

                auto compArrayIt = targetArchetype->componentArrays.find(sc.typeID);
                if (compArrayIt != targetArchetype->componentArrays.end() && compArrayIt->second != nullptr) {
                    serializer->DeserializeAndAdd(compArrayIt->second.get(), sc.data);
                }
            }
        }

        // Rebuild free list
        size_t freeCount = 0;
        for (size_t i = 0; i < entityLookup.size(); ++i) {
            if (!entityLookup[i].archetypePtr) {
                freeIndices.push_back(i);
                freeCount++;
            }
        }

        if (!dormantData.empty()) {
            LOG_WARN("World Load: " + std::to_string(dormantData.size()) + " entities still have unknown components.");
        }

        LOG_INFO("World Load: Memory load complete. " + std::to_string(freeCount) + " free indices.");

    } catch (const std::exception& e) {
        LOG_ERROR("World Load from Memory: Exception: " + std::string(e.what()));
    }
}

inline void World::ReloadAndHeal() {
    LOG_INFO("=== ReloadAndHeal: Starting recovery of dormant components ===");

    // 1. Serialize the entire world to a JSON snapshot in memory
    nlohmann::json snapshot = SerializeToMemory();

    // 2. Wipe the world (but keep the registry since DLL is now loaded)
    entityLookup.clear();
    freeIndices.clear();
    archetypes.clear();
    dormantData.clear();

    // 3. Re-load from the snapshot
    // Since the DLL is now loaded, previously unknown components will be recognized
    LoadFromMemory(snapshot);

    if (dormantData.empty()) {
        LOG_INFO("=== ReloadAndHeal: Complete! All components recovered. ===");
    } else {
        LOG_WARN("=== ReloadAndHeal: Some components still unknown (" + std::to_string(dormantData.size()) + " entities affected) ===");
    }
}