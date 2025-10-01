#pragma once
#include "Entity.h"
#include "Component.h"
#include "ECSConfig.h"
#include <bitset>
#include <type_traits> // For std::remove_pointer_t

class World; // Forward declaration

class System {
public:
    enum class SystemRole { Shared, Editor, Game };

    bool isActive = true;

    virtual ~System() = default;
    virtual void Run(World* world, float deltaTime) = 0; // The base execution function
    ComponentSignature signature;

    SystemRole role = SystemRole::Game;
};


// SystemBase: A convenience class for defining component requirements cleanly.
// TArgs should be the component types, typically pointers (e.g., Position*, Velocity*)
template<typename... TArgs>
class SystemBase : public System {
public:
    SystemBase() {
        // Automatically build the signature by OR-ing all required component TypeIDs
        // The pack expansion handles all TArgs
        (signature.set(Engine::ECS::Component::GetTypeID<std::remove_pointer_t<TArgs>>()), ...);
    }

    // This is the clean, mandatory signature the user implements
    // It receives pointers to the component data for iteration.
    virtual void Update(float deltaTime, TArgs... components) = 0;

    void Run(World* world, float deltaTime) override;
};