#pragma once

#include "../../ZuesAPI.h"
#include "../System.h"
#include "../Components.h"
#include <memory>
#include <unordered_map>
#include <box2d/id.h>

#include "EventSystem/EventSystem.h"

class World; // Forward declaration

class ZUES_API PhysicsSystem final : public System {
public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    void SetEventSystem(Engine::EventSystem* eventSystem) { m_EventSystem = eventSystem; }

    void Run(World* world, float deltaTime) override;

    // Physics configuration
    void SetGravity(float x, float y);
    Engine::Math::Vec2 GetGravity() const;

private:
    b2WorldId physicsWorldId{};
    Engine::Math::Vec2 gravity = {0.0f, -9.8f};

    Engine::EventSystem* m_EventSystem = nullptr;

    // Physics simulation settings
    int32_t subStepCount = 4;  // Box2D v3 uses sub-steps instead of velocity/position iterations
    float timeAccumulator = 0.0f;
    float fixedTimeStep = 1.0f / 60.0f; // 60 FPS physics

    // Track which entities have bodies
    std::unordered_map<EntityID, b2BodyId> entityBodyMap;

    // Internal methods
    void CreateBodies(World* world);
    void StepPhysics(float deltaTime);
    void SyncTransforms(World* world);
    void DestroyBodies(World* world);

    b2BodyId CreateBody(World* world, const EntityID &entityID) const;

    static void CreateFixtures(b2BodyId bodyId, World* world, const EntityID &entityID);
};
