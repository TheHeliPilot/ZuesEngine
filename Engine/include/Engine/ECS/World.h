#pragma once
#include "System.h"
#include "ECSConfig.h"

#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <functional>
#include <set>

#include "WorldSerializationHelpers.h"

struct EntityData {
    EntityGeneration generation = 0;
    void* archetypePtr = nullptr;
    size_t archetypeIndex = 0;
};

struct Archetype {
    ComponentSignature signature;
    std::vector<EntityID> entityIDs;
    std::map<Engine::ECS::Component::TypeID, std::unique_ptr<IComponentArray>> componentArrays;

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
    extern std::map<TypeID, ComponentCreator> componentRegistry;

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

class World {
public:
    World() = default;
    ~World() = default;

    EntityID CreateEntity(const std::string &name);
    void DestroyEntity(EntityID entityID);

    template<typename T> void RegisterComponent(const std::string& typeName);
    template<typename T> void AddComponent(EntityID entityID, const T& component);
    template<typename T> void RemoveComponent(EntityID entityID);
    template<typename T> T& GetComponent(EntityID entityID);
    template<typename T> bool HasComponent(EntityID entityID) const;
    template<typename T> void SetComponentData(EntityID entityID, const T& componentData);

    void AddComponentByType(EntityID entityID, Engine::ECS::Component::TypeID typeID);

    // NEW: Inspector API
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

    void RegisterSystem(std::unique_ptr<System> system);
    void UpdateSystems(float deltaTime, System::SystemRole currentMode);

    template<typename T>
    bool IsComponentRegistered();

    bool SaveToJson(const std::string& filename) const;
    bool LoadFromJson(const std::string& filename);

    template<class ... TArgs, class Func>
    void ForEach(Func &&func);

    template<typename... TArgs>
    void QueryAndRun(SystemBase<TArgs...>* system, float deltaTime);

    const std::unordered_map<ComponentSignature, std::unique_ptr<Archetype>>& GetArchetypes() const {
        return archetypes;
    }

private:
    std::vector<EntityData> entityLookup;
    std::vector<EntityIndex> freeIndices;
    std::unordered_map<ComponentSignature, std::unique_ptr<Archetype>> archetypes;
    std::vector<std::unique_ptr<System>> systems;

    Engine::ComponentRegistry componentSerializationRegistry;

    Archetype* GetOrCreateArchetype(const ComponentSignature& signature);
    void MoveEntity(EntityID id, Archetype* currentArchetype, Archetype* nextArchetype);
};

#include "World.inl"