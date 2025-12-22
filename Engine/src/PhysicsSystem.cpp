#include "../include/Engine/ECS/Systems/PhysicsSystem.h"
#include "../include/Engine/ECS/World.h"
#include <box2d/box2d.h>
#include <box2d/math_functions.h>
#include <cmath>

#include "Core.h"
#include "Engine.h"
#include "EventSystem/Events.h"

PhysicsSystem::PhysicsSystem() {
    // Create the Box2D world with gravity
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {gravity.x, gravity.y};
    physicsWorldId = b2CreateWorld(&worldDef);

    // No signature setup - we'll manually query entities
    role = SystemRole::Game;

    SetEventSystem(Engine::GetCurrentEventSystem());
}

PhysicsSystem::~PhysicsSystem() {
    // Clean up all bodies before destroying the world
    entityBodyMap.clear();
    b2DestroyWorld(physicsWorldId);
}

void PhysicsSystem::SetGravity(float x, float y) {
    gravity = {x, y};
    b2World_SetGravity(physicsWorldId, {x, y});
}

Engine::Math::Vec2 PhysicsSystem::GetGravity() const {
    return gravity;
}

void PhysicsSystem::Run(World* world, const float deltaTime) {
    if (!isActive || !world) return;

    // Create bodies for new entities
    CreateBodies(world);

    // Step the physics simulation
    StepPhysics(deltaTime);

    // Sync physics transforms back to ECS
    SyncTransforms(world);

    // Clean up destroyed entities
    DestroyBodies(world);
}

void PhysicsSystem::CreateBodies(World* world) {
    world->ForEach<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::RigidbodyComponent*>(
        [this, world](const EntityID &entityID, Engine::ECS::Component::TransformComponent* transform, Engine::ECS::Component::RigidbodyComponent* rb) {

            b2BodyId existingBodyId = b2_nullBodyId;
            if (rb->body != nullptr) {
                const auto storedId = reinterpret_cast<uintptr_t>(rb->body);
                existingBodyId = b2LoadBodyId(storedId);
            }

            if (rb->body != nullptr && b2Body_IsValid(existingBodyId)) {
                // 1. Update existing body properties
                b2Body_SetType(existingBodyId, static_cast<b2BodyType>(rb->bodyType));
                b2Body_SetGravityScale(existingBodyId, rb->gravityScale);

                // Update user data to store entity ID
                b2Body_SetUserData(existingBodyId, reinterpret_cast<void*>(entityID.id));

                b2MotionLocks locks = b2Body_GetMotionLocks(existingBodyId);
                locks.angularZ = rb->fixedRotation;
                b2Body_SetMotionLocks(existingBodyId, locks);

                b2Body_SetAwake(existingBodyId, true);
            }
            else if (rb->body == nullptr) {
                // 2. Create new body
                const b2BodyId bodyId = CreateBody(world, entityID);

                const uint64_t storedId = b2StoreBodyId(bodyId);
                rb->body = reinterpret_cast<void*>(storedId);
                entityBodyMap[entityID] = bodyId;

                CreateFixtures(bodyId, world, entityID);
                b2Body_SetAwake(bodyId, true);
            }
        }
    );
}

b2BodyId PhysicsSystem::CreateBody(World* world, const EntityID &entityID) const {
    const auto* transform = &world->GetComponent<Engine::ECS::Component::TransformComponent>(entityID);
    auto* rb = &world->GetComponent<Engine::ECS::Component::RigidbodyComponent>(entityID);

    // Create body definition
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = static_cast<b2BodyType>(rb->bodyType);
    bodyDef.position = {transform->worldPosition.x, transform->worldPosition.y};

    // Convert degrees to rotation (Box2D v3 uses b2Rot which is cos/sin pair)
    const float angleRadians = transform->worldRotation * (3.14159265359f / 180.0f);
    bodyDef.rotation = b2MakeRot(angleRadians);

    bodyDef.gravityScale = rb->gravityScale;
    bodyDef.userData = reinterpret_cast<void*>(entityID.id);

    // Handle fixed rotation via motion locks
    if (rb->fixedRotation) {
        bodyDef.motionLocks.angularZ = true;
    }

    // Create the body in the physics world
    const b2BodyId bodyId = b2CreateBody(physicsWorldId, &bodyDef);
    return bodyId;
}

void PhysicsSystem::CreateFixtures(const b2BodyId bodyId, World* world, const EntityID &entityID) {
    // Default shape definition
    b2ShapeDef shapeDef = b2DefaultShapeDef();

    // Check for BoxCollider
    if (world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(entityID)) {
        const auto& collider = world->GetComponent<Engine::ECS::Component::BoxColliderComponent>(entityID);

        // Create polygon shape (box)
        const b2Polygon box = b2MakeOffsetBox(
            collider.size.x * 0.5f,
            collider.size.y * 0.5f,
            {collider.offset.x, collider.offset.y},
            b2Rot_identity
        );

        shapeDef.density = collider.density;
        shapeDef.material.friction = collider.friction;
        shapeDef.material.restitution = collider.restitution;
        shapeDef.isSensor = collider.isTrigger;

        b2CreatePolygonShape(bodyId, &shapeDef, &box);
    }

    // Check for CircleCollider
    if (world->HasComponent<Engine::ECS::Component::CircleColliderComponent>(entityID)) {
        const auto& collider = world->GetComponent<Engine::ECS::Component::CircleColliderComponent>(entityID);

        b2Circle circle;
        circle.center = {collider.offset.x, collider.offset.y};
        circle.radius = collider.radius;

        shapeDef.density = collider.density;
        shapeDef.material.friction = collider.friction;
        shapeDef.material.restitution = collider.restitution;
        shapeDef.isSensor = collider.isTrigger;

        b2CreateCircleShape(bodyId, &shapeDef, &circle);
    }

    // Update mass data after creating fixtures
    const auto* rb = &world->GetComponent<Engine::ECS::Component::RigidbodyComponent>(entityID);
    if (rb->bodyType == 2) { // Dynamic body
        b2MassData massData = b2Body_GetMassData(bodyId);
        massData.mass = rb->mass;
        b2Body_SetMassData(bodyId, massData);
    }
}

void PhysicsSystem::StepPhysics(const float deltaTime) {
    timeAccumulator += deltaTime;

    while (timeAccumulator >= fixedTimeStep) {
        b2World_Step(physicsWorldId, fixedTimeStep, subStepCount);

        // Fetch events from the simulation step
        const b2ContactEvents events = b2World_GetContactEvents(physicsWorldId);

        // 1. Handle Begin Events (CollisionEnter / TriggerEnter)
        for (int i = 0; i < events.beginCount; ++i) {
            const b2ContactBeginTouchEvent* ev = &events.beginEvents[i];

            // Extract raw uint64_t from Box2D
            const auto idA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(ev->shapeIdA)));
            const auto idB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(ev->shapeIdB)));

            // MANUALLY reconstruct the EntityID objects
            EntityID entA; entA.id = idA;
            EntityID entB; entB.id = idB;

            const bool isTrigger = b2Shape_IsSensor(ev->shapeIdA) || b2Shape_IsSensor(ev->shapeIdB);

            if (m_EventSystem) {
                if (isTrigger)
                    m_EventSystem->Dispatch(Engine::TriggerEnterEvent(entA, entB));
                else
                    m_EventSystem->Dispatch(Engine::CollisionEnterEvent(entA, entB));
            }

            // Debug Log
            printf("[Physics] %s Enter: %llu & %llu\n", isTrigger ? "Trigger" : "Collision", idA, idB);
        }

        // 2. Handle End Events (CollisionExit / TriggerExit)
        for (int i = 0; i < events.endCount; ++i) {
            const b2ContactEndTouchEvent* ev = &events.endEvents[i];

            const auto idA = reinterpret_cast<uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(ev->shapeIdA)));
            const auto idB = reinterpret_cast<uintptr_t>(b2Body_GetUserData(b2Shape_GetBody(ev->shapeIdB)));

            EntityID entA; entA.id = idA;
            EntityID entB; entB.id = idB;

            const bool isTrigger = b2Shape_IsSensor(ev->shapeIdA) || b2Shape_IsSensor(ev->shapeIdB);

            if (m_EventSystem) {
                if (isTrigger)
                    m_EventSystem->Dispatch(Engine::TriggerExitEvent(entA, entB));
                else
                    m_EventSystem->Dispatch(Engine::CollisionExitEvent(entA, entB));
            }

            // Debug Log
            printf("[Physics] %s Exit: %llu & %llu\n", isTrigger ? "Trigger" : "Collision", idA, idB);
        }

        timeAccumulator -= fixedTimeStep;
    }
}

void PhysicsSystem::SyncTransforms(World* world) {
    // Sync all physics bodies back to transform components
    for (auto& [entityID, bodyId] : entityBodyMap) {
        if (b2Body_IsValid(bodyId) && world->HasComponent<Engine::ECS::Component::TransformComponent>(entityID)) {
            auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(entityID);

            const b2Vec2 pos = b2Body_GetPosition(bodyId);
            const b2Rot rot = b2Body_GetRotation(bodyId);

            // Update world position
            transform.worldPosition.x = pos.x;
            transform.worldPosition.y = pos.y;

            // Convert rotation to angle in degrees
            const float angleRadians = b2Rot_GetAngle(rot);
            transform.worldRotation = angleRadians * (180.0f / 3.14159265359f);
        }
    }
}

void PhysicsSystem::DestroyBodies(World* world) {
    // Find bodies whose entities no longer exist or no longer have RigidbodyComponent
    std::vector<EntityID> toRemove;

    for (auto& [entityID, bodyId] : entityBodyMap) {
        if (!world->HasComponent<Engine::ECS::Component::RigidbodyComponent>(entityID)) {
            if (b2Body_IsValid(bodyId)) {
                b2DestroyBody(bodyId);
            }
            toRemove.push_back(entityID);
        }
    }

    // Remove from map
    for (EntityID id : toRemove) {
        if (world->HasComponent<Engine::ECS::Component::RigidbodyComponent>(id)) {
            auto& rb = world->GetComponent<Engine::ECS::Component::RigidbodyComponent>(id);
            rb.body = nullptr;
        }
        entityBodyMap.erase(id);
    }
}
