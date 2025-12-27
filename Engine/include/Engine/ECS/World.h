#pragma once

#include "../ZuesAPI.h"
#include "System.h"
#include "ECSConfig.h"

#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <functional>
#include <set>

#include "WorldSerializationHelpers.h"

struct ZUES_API EntityData {
    EntityGeneration generation = 0;
    void* archetypePtr = nullptr;
    size_t archetypeIndex = 0;
    bool hasUnknownComponents = false;  // True if entity has components from unloaded DLL
};

struct ZUES_API Archetype {
    ComponentSignature signature;
    std::vector<EntityID> entityIDs;
    std::map<Engine::ECS::Component::TypeID, std::unique_ptr<IComponentArray>> componentArrays;

    // Non-copyable due to unique_ptr members
    Archetype() = default;
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;
    Archetype(Archetype&&) = default;
    Archetype& operator=(Archetype&&) = default;

    template<typename T>
    ComponentArray<T>* GetComponentArray() {
        const Engine::ECS::Component::TypeID id = Engine::ECS::Component::GetTypeID<T>();
        auto it = componentArrays.find(id);
        if (it == componentArrays.end()) {
            return nullptr;
        }
        return static_cast<ComponentArray<T>*>(it->second.get());
    }
};

namespace std {
    template<>
    struct hash<ComponentSignature> {
        size_t operator()(const ComponentSignature& signature) const;
    };
}

namespace Engine::ECS::Component {
    using ComponentCreator = std::function<std::unique_ptr<IComponentArray>()>;
    ZUES_API extern std::map<TypeID, ComponentCreator> componentRegistry;

    template<typename T>
    void RegisterComponent() {
        TypeID id = GetTypeID<T>();
        if (componentRegistry.find(id) == componentRegistry.end()) {
            componentRegistry[id] = []() -> std::unique_ptr<IComponentArray> {
                return std::make_unique<ComponentArray<T>>();
            };
        }
    }
}

class ZUES_API World {
public:
    World() = default;
    ~World() = default;

    // Non-copyable due to unique_ptr members
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Movable
    World(World&&) = default;
    World& operator=(World&&) = default;

    EntityID CreateEntity(const std::string &name);
    void DestroyEntity(EntityID entityID);
    bool RenameEntity(EntityID entityID, const std::string& newName);

    // --- Component Registration ---

    template<typename T>
    void RegisterComponent(const std::string& typeName);

    Engine::ComponentRegistry& GetComponentRegistry() { return componentSerializationRegistry; }

    void ClearSystems() {
        systems.clear(); // This deletes the unique_ptrs, which is safe IF done before FreeLibrary
    }

    // --- System Registry Access ---
    Engine::SystemRegistry& GetSystemRegistry() { return systemSerializationRegistry; }
    const Engine::SystemRegistry& GetSystemRegistry() const { return systemSerializationRegistry; }

    // Get list of active systems (for UI)
    const std::vector<std::unique_ptr<System>>& GetSystems() const { return systems; }

    // Add a system by name from the registry (for UI/serialization)
    bool AddSystemByName(const std::string& systemName);

    // Remove a system by name (for UI)
    bool RemoveSystemByName(const std::string& systemName);

    // Check if a system is active by name
    bool HasActiveSystem(const std::string& systemName) const;

    // Set system active state by name
    bool SetSystemActive(const std::string& systemName, bool active);

    // Get active system states for serialization (excludes required systems)
    std::vector<Engine::SerializedSystem> GetActiveSystemStates() const;

    // Apply system states from serialization
    void ApplySystemStates(const std::vector<Engine::SerializedSystem>& states);
    /**
     * Safe Registration for Hot-Reloading and DLLs.
     * Prevents TypeID collisions between different memory spaces.
     */
    template<typename T>
    void RegisterComponentSafe(const std::string& typeName) { // <--- FIX 1: Removed World::
        Engine::ECS::Component::TypeID targetID;

        // 1. Check if name exists (Sticky ID)
        if (componentSerializationRegistry.HasName(typeName)) {
            targetID = componentSerializationRegistry.GetIDByName(typeName);
            componentSerializationRegistry.UpdateSerializer<T>(targetID, typeName);
        } else {
            // 2. New component
            targetID = componentSerializationRegistry.GetTotalRegisteredCount();
            componentSerializationRegistry.AddSerializer<T>(targetID, typeName);
        }

        // 3. Re-link the creator map to the NEW DLL lambda
        Engine::ECS::Component::componentRegistry[targetID] = []() -> std::unique_ptr<IComponentArray> {
            return std::make_unique<ComponentArray<T>>();
        };
    }

    // --- Entity Manipulation ---

    template<typename T> void AddComponent(EntityID entityID, const T& component);
    template<typename T> void RemoveComponent(EntityID entityID);
    template<typename T> T& GetComponent(EntityID entityID);
    template<typename T> bool HasComponent(EntityID entityID) const;
    template<typename T> void SetComponentData(EntityID entityID, const T& componentData);

    void AddComponentByType(EntityID entityID, Engine::ECS::Component::TypeID typeID);

    // --- Inspector / Tools API ---

    std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> GetAllComponents(EntityID entityID) const;
    std::vector<std::pair<Engine::ECS::Component::TypeID, void*>> GetAllComponents(EntityID entityID);

    const Engine::ComponentRegistry& GetComponentRegistry() const {
        return componentSerializationRegistry;
    }

    std::set<EntityID> GetEntityChildren(EntityID parentID) const;

    void RemoveComponentByType(EntityID entityID, Engine::ECS::Component::TypeID typeID);
    bool HasComponent(EntityID entityID, Engine::ECS::Component::TypeID typeID) const;

    template<typename... TArgs>
    ComponentSignature CalculateSignature() const;

    // --- Systems ---

    void RegisterSystem(std::unique_ptr<System> system);
    void UpdateSystems(float deltaTime, System::SystemRole currentMode);

    template<typename T>
    bool IsComponentRegistered();

    // --- Persistence ---

    bool SaveToJson(const std::string& filename) const;
    bool LoadFromJson(const std::string& filename);

    // --- Hot-Reload Recovery ---
    void ReloadAndHeal();  // Re-serialize and deserialize to recover unknown components
    nlohmann::json SerializeToMemory() const;
    void LoadFromMemory(const nlohmann::json& snapshot);

    // --- Dormant Component Access ---
    const std::vector<Engine::SerializedComponent>& GetDormantComponents(EntityID entityID) const;
    bool HasDormantComponents(EntityID entityID) const;
    void ClearDormantComponents(EntityID entityID);
    bool EntityHasUnknownComponents(EntityID entityID) const;  // Check hasUnknownComponents flag
    bool HasAnyEntitiesWithUnknownComponents() const;  // Check if any entity has unknown components

    // --- Iteration ---

    template<class ... TArgs, class Func>
    void ForEach(Func &&func);

    template<typename... TArgs>
    void QueryAndRun(SystemBase<TArgs...>* system, float deltaTime);

    const std::unordered_map<ComponentSignature, std::unique_ptr<Archetype>>& GetArchetypes() const {
        return archetypes;
    }

    void RemoveDLLComponents(uint32_t startID);

private:
    std::vector<EntityData> entityLookup;
    std::vector<EntityIndex> freeIndices;
    std::unordered_map<ComponentSignature, std::unique_ptr<Archetype>> archetypes;
    std::vector<std::unique_ptr<System>> systems;

    // This handles the mapping between TypeIDs and Serializers
    Engine::ComponentRegistry componentSerializationRegistry;

    // This handles the mapping between system names and creators
    Engine::SystemRegistry systemSerializationRegistry;

    // Storage for component data from unloaded DLLs (dormant components)
    // Key: Entity index, Value: Vector of serialized components with unknown types
    std::map<EntityIndex, std::vector<Engine::SerializedComponent>> dormantData;

    // Storage for system names from unloaded DLLs (dormant systems)
    std::vector<Engine::SerializedSystem> dormantSystems;

public:
    // Access dormant systems for UI
    const std::vector<Engine::SerializedSystem>& GetDormantSystems() const { return dormantSystems; }
    std::vector<Engine::SerializedSystem>& GetDormantSystems() { return dormantSystems; }

private:
    Archetype* GetOrCreateArchetype(const ComponentSignature& signature);

    void MoveEntity(EntityID id, Archetype* currentArchetype, Archetype* nextArchetype);
};

#include "World.inl"