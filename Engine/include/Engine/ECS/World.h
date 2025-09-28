#pragma once
#include "System.h"
#include "Entity.h"
#include "Component.h"
#include "ECSConfig.h"

#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <functional>

// --- Helper Structs & Archetype Definitions ---

// Entity Lookup Data: Maps EntityIndex -> where the entity is
struct EntityData {
    EntityGeneration generation = 0;
    void* archetypePtr = nullptr; // Pointer to the Archetype this entity is in
    size_t archetypeIndex = 0;   // The index of the entity within that Archetype's arrays
};

// The Archetype (A unique set of components and their contiguous storage)
struct Archetype {
    ComponentSignature signature;
    std::vector<EntityID> entityIDs;
    // Stores the contiguous component arrays
    std::map<Engine::ECS::Component::TypeID, std::unique_ptr<IComponentArray>> componentArrays;

    template<typename T>
    ComponentArray<T>* GetComponentArray() {
        Engine::ECS::Component::TypeID id = Engine::ECS::Component::GetTypeID<T>();
        auto it = componentArrays.find(id);
        if (it == componentArrays.end()) {
            return nullptr;
        }
        return static_cast<ComponentArray<T>*>(it->second.get());
    }
};

// ----------------------------------------------------------------------
// Hash specialization for ComponentSignature (needed for std::unordered_map)
// ----------------------------------------------------------------------
namespace std {
    template<>
    struct hash<ComponentSignature> {
        size_t operator()(const ComponentSignature& signature) const {
            // Safe for MAX_COMPONENTS=64
            return std::hash<unsigned long long>()(signature.to_ullong());
        }
    };
}

// ----------------------------------------------------------------------
// Global Component Registry (Defined/Used in World.cpp)
// ----------------------------------------------------------------------
namespace Engine::ECS::Component {
    using ComponentCreator = std::function<std::unique_ptr<IComponentArray>()>;
    extern std::map<TypeID, ComponentCreator> componentRegistry;

    // Must be called once for every component type (e.g., in a main Init() function)
    template<typename T>
    inline void RegisterComponent() {
        TypeID id = GetTypeID<T>();
        if (componentRegistry.find(id) == componentRegistry.end()) {
            componentRegistry[id] = []() -> std::unique_ptr<IComponentArray> {
                return std::make_unique<ComponentArray<T>>();
            };
        }
    }
}


// ----------------------------------------------------------------------
// The World Class
// ----------------------------------------------------------------------
class World {
public:
    World() = default;
    ~World() = default;

    // === Entity & Component API ===
    EntityID CreateEntity();
    void DestroyEntity(EntityID entityID);

    template<typename T> void AddComponent(EntityID entityID, const T& component);
    template<typename T> void RemoveComponent(EntityID entityID);
    template<typename T> T& GetComponent(EntityID entityID);
    template<typename T> bool HasComponent(EntityID entityID);

    // === System Management API ===
    void RegisterSystem(std::unique_ptr<System> system);
    void UpdateSystems(float deltaTime);

    // === Core Iteration Mechanism (Called by SystemBase::Run) ===
    // This function performs the Archetype filtering and the inner loop execution.
    template<typename... TArgs>
    void QueryAndRun(SystemBase<TArgs...>* system, float deltaTime);


private:
    std::vector<EntityData> entityLookup;
    std::vector<EntityIndex> freeIndices;
    // Archetype storage: Fast lookup by signature
    std::unordered_map<ComponentSignature, std::unique_ptr<Archetype>> archetypes;
    std::vector<std::unique_ptr<System>> systems;

    // Archetype core logic
    Archetype* GetOrCreateArchetype(const ComponentSignature& signature);
    void MoveEntity(EntityID id, Archetype* currentArchetype, Archetype* nextArchetype);
};

#include "World.inl"