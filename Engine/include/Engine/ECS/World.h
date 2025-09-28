#pragma once
#include <functional>

#include "System.h" // Includes SystemBase and ComponentSignature
#include "Entity.h"
#include "Component.h" // ECS::Component::GetTypeID
#include "ECSConfig.h" // For MAX_COMPONENTS

#include <vector>
#include <map>
#include <memory>
#include <tuple>
#include <stdexcept>
#include <type_traits> // For std::remove_pointer_t

// --- Helper Structs & Archetype Definitions ---

// Entity Lookup Data
struct EntityData {
    EntityGeneration generation = 0;
    void* archetypePtr = nullptr; // Pointer to the Archetype this entity is in
    size_t archetypeIndex = 0;   // The index of the entity within that Archetype's arrays
};

// Component Storage Interface (Base class for type erasure)
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void RemoveComponent(size_t index) = 0;
    virtual void AddComponentFrom(IComponentArray* other, size_t index) = 0;
};

// Templated Component Storage (The contiguous arrays)
template<typename T>
class ComponentArray : public IComponentArray {
public:
    std::vector<T> data;

    void RemoveComponent(size_t index) override {
        // Swap-and-Pop: Copy the last element over the one being removed
        data[index] = std::move(data.back());
        data.pop_back();
    }

    void AddComponent(const T& component) {
        data.push_back(component);
    }

    void AddComponentFrom(IComponentArray* other, size_t index) override {
        T& component = static_cast<ComponentArray<T>*>(other)->data[index];
        data.push_back(std::move(component));
    }

    T& Get(size_t index) {
        return data[index];
    }

    // Direct access to the underlying data pointer (used for fast iteration)
    T* GetData() {
        return data.data();
    }
};

// The Archetype (The memory block)
struct Archetype {
    ComponentSignature signature;
    std::vector<EntityID> entityIDs;
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
// 2. The World Class (The Registry)
// ----------------------------------------------------------------------

class World {
public:
    // === Entity & Component API (Templated implementations in World.inl) ===
    EntityID CreateEntity();
    void DestroyEntity(EntityID entityID);

    template<typename T> void AddComponent(EntityID entityID, const T& component);
    template<typename T> void RemoveComponent(EntityID entityID);
    template<typename T> T& GetComponent(EntityID entityID);

    // === System Management API ===
    void RegisterSystem(std::unique_ptr<System> system);
    void UpdateSystems(float deltaTime);

    // TArgs are the component types (e.g., Position, Health) the user wants to access.
    // The lambda must accept EntityID and component references (TArgs&...).
    template<typename... TArgs>
    void ForEachEntity(std::function<void(EntityID, TArgs&...)> lambda);

    // === Core Iteration Mechanism (Templated - Called by SystemBase::Run) ===
    // This function performs the Archetype filtering and the inner loop execution.
    template<typename... TArgs>
    void QueryAndRun(SystemBase<TArgs...>* system, float deltaTime);

private:
    std::vector<EntityData> entityLookup;
    std::vector<EntityIndex> freeIndices;
    std::map<ComponentSignature, std::unique_ptr<Archetype>> archetypes;
    std::vector<std::unique_ptr<System>> systems;

    Archetype* GetOrCreateArchetype(const ComponentSignature& signature);
    void MoveEntity(EntityID id, Archetype* currentArchetype, Archetype* nextArchetype);
};

#include "World.inl" // Template implementation (must be included here)