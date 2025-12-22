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
        if (const auto&[generation, archetypePtr, archetypeIndex] = entityLookup[i]; archetypePtr && generation == EntityID(i, generation).GetGeneration()) {

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

            for (const auto&[fst, snd] : currentArchetype->componentArrays) {
                Engine::ECS::Component::TypeID compID = fst;
                IComponentArray* compArray = snd.get();

                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(compID);
                json componentData = serializer->SerializeComponent(compArray, archetypeIndex);

                Engine::SerializedComponent sc;
                sc.typeID = compID;
                sc.data = componentData;
                se.components.push_back(std::move(sc));

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

        entityLookup.clear();
        freeIndices.clear();
        archetypes.clear();
        LOG_INFO("World Load: Cleared existing world state.");

        for (const auto&[id, components] : entities) {
            EntityIndex index = id.GetIndex();
            EntityGeneration generation = id.GetGeneration();
            std::stringstream ss_entity;
            ss_entity << "  Loading Entity ID: " << id.id << " (Target Index: " << index << ", Generation: " << generation << ")";
            LOG_INFO(ss_entity.str());

            if (index >= entityLookup.size()) {
                entityLookup.resize(index + 1);
                std::stringstream ss_resize;
                ss_resize << "    Resizing entity lookup table to size: " << entityLookup.size();
                LOG_INFO(ss_resize.str());
            }

            ComponentSignature newSignature;
            for (const auto& sc : components) {
                newSignature.set(sc.typeID);
            }
            std::stringstream ss_sig;
            ss_sig << "    Determined new Component Signature based on " << components.size() << " components.";
            LOG_INFO(ss_sig.str());

            Archetype* targetArchetype = GetOrCreateArchetype(newSignature);
            LOG_INFO("    Archetype found/created for signature.");

            EntityData& data = entityLookup[index];
            data.generation = generation;
            data.archetypePtr = targetArchetype;
            data.archetypeIndex = targetArchetype->entityIDs.size();
            targetArchetype->entityIDs.push_back(id);
            std::stringstream ss_register;
            ss_register << "    Entity registered to Archetype. New archetype index: " << data.archetypeIndex;
            LOG_INFO(ss_register.str());

            for (const auto& sc : components) {
                Engine::IComponentSerializer* serializer = componentSerializationRegistry.GetSerializer(sc.typeID);

                auto compArrayIt = targetArchetype->componentArrays.find(sc.typeID);
                if (compArrayIt == targetArchetype->componentArrays.end()) {
                    throw std::runtime_error("Internal ECS error: Archetype component array missing after creation.");
                }

                serializer->DeserializeAndAdd(compArrayIt->second.get(), sc.data);
                std::stringstream ss_comp;
                ss_comp << "      - Component ID " << sc.typeID << " Deserialized. Data: " << sc.data.dump(2);
                LOG_INFO(ss_comp.str());
            }
        }

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