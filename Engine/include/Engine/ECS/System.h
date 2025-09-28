// System.h (Corrected)
#pragma once
#include "Entity.h"
#include "Component.h"
#include "ECSConfig.h"
#include <bitset>
#include <type_traits> // For std::remove_pointer_t

class World; // Forward declaration

using ComponentSignature = std::bitset<MAX_COMPONENTS>;

class System {
public:
    virtual void Run(World* world, float deltaTime) = 0; // The base execution function
    ComponentSignature signature;
    virtual ~System() = default;

protected:
    template<typename T>
    void RequireComponent() {
        // Use a 'using' or fully qualify the namespace
        signature.set(Engine::ECS::Component::GetTypeID<T>());
    }

    template<typename T>
    void ExcludeComponent() {
        signature.reset(Engine::ECS::Component::GetTypeID<T>());
    }
};

template<typename... TArgs>
class SystemBase : public System {
public:
    SystemBase() {
        // Automatically build the signature
        (signature.set(Engine::ECS::Component::GetTypeID<std::remove_pointer_t<TArgs>>()), ...);
    }

    // This is the clean, mandatory signature the user implements
    virtual void Update(float deltaTime, TArgs... components) = 0;

    // DECLARATION ONLY: Definition will be in World.inl
    void Run(World* world, float deltaTime) override;
};