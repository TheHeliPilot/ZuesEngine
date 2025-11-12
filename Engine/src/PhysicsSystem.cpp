#include "../include/Engine/ECS/Systems/PhysicsSystem.h"
#include "../include/Engine/ECS/World.h"
#include <box2d/box2d.h>
#include <box2d/math_functions.h>
#include <cmath>

PhysicsSystem::PhysicsSystem() {
    // Create the Box2D world with gravity
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {gravity.x, gravity.y};
    physicsWorldId = b2CreateWorld(&worldDef);

    // No signature setup - we'll manually query entities
    role = SystemRole::Game;
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
    // Query all entities with TransformComponent and RigidbodyComponent
    world->ForEach<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::RigidbodyComponent*>(
        [this, world](EntityID entityID, Engine::ECS::Component::TransformComponent* transform, Engine::ECS::Component::RigidbodyComponent* rb) {
            // Check if body exists
            b2BodyId existingBodyId = b2_nullBodyId;
            if (rb->body != nullptr) {
                uint64_t storedId = reinterpret_cast<uintptr_t>(rb->body);
                existingBodyId = b2LoadBodyId(storedId);
            }

            // If body exists, check if we need to update its properties
            if (rb->body != nullptr && b2Body_IsValid(existingBodyId)) {
                b2BodyType currentType = b2Body_GetType(existingBodyId);
                if (currentType != static_cast<b2BodyType>(rb->bodyType)) {
                    // Body type changed - update it
                    b2Body_SetType(existingBodyId, static_cast<b2BodyType>(rb->bodyType));
                }

                // Update gravity scale
                b2Body_SetGravityScale(existingBodyId, rb->gravityScale);

                // Update fixed rotation
                b2MotionLocks locks = b2Body_GetMotionLocks(existingBodyId);
                locks.angularZ = rb->fixedRotation;
                b2Body_SetMotionLocks(existingBodyId, locks);

                // Wake the body to ensure it starts simulating
                b2Body_SetAwake(existingBodyId, true);
            }
            // If this entity doesn't have a body yet, create one
            else if (rb->body == nullptr) {
                b2BodyId bodyId = CreateBody(world, entityID);

                // Store the body ID in the component using Box2D's store function
                uint64_t storedId = b2StoreBodyId(bodyId);
                rb->body = reinterpret_cast<void*>(static_cast<uintptr_t>(storedId));
                entityBodyMap[entityID] = bodyId;

                // Create fixtures from collider components
                CreateFixtures(bodyId, world, entityID);

                // Wake the body to ensure it starts simulating
                b2Body_SetAwake(bodyId, true);
            }
        }
    );
}

b2BodyId PhysicsSystem::CreateBody(World* world, EntityID entityID) {
    auto* transform = &world->GetComponent<Engine::ECS::Component::TransformComponent>(entityID);
    auto* rb = &world->GetComponent<Engine::ECS::Component::RigidbodyComponent>(entityID);

    // Create body definition
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = static_cast<b2BodyType>(rb->bodyType);
    bodyDef.position = {transform->worldPosition.x, transform->worldPosition.y};

    // Convert degrees to rotation (Box2D v3 uses b2Rot which is cos/sin pair)
    float angleRadians = transform->worldRotation * (3.14159265359f / 180.0f);
    bodyDef.rotation = b2MakeRot(angleRadians);

    bodyDef.gravityScale = rb->gravityScale;
    bodyDef.userData = reinterpret_cast<void*>(entityID.id);

    // Handle fixed rotation via motion locks
    if (rb->fixedRotation) {
        bodyDef.motionLocks.angularZ = true;
    }

    // Create the body in the physics world
    b2BodyId bodyId = b2CreateBody(physicsWorldId, &bodyDef);
    return bodyId;
}

void PhysicsSystem::CreateFixtures(b2BodyId bodyId, World* world, EntityID entityID) {
    // Default shape definition
    b2ShapeDef shapeDef = b2DefaultShapeDef();

    // Check for BoxCollider
    if (world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(entityID)) {
        auto& collider = world->GetComponent<Engine::ECS::Component::BoxColliderComponent>(entityID);

        // Create polygon shape (box)
        b2Polygon box = b2MakeOffsetBox(
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
        auto& collider = world->GetComponent<Engine::ECS::Component::CircleColliderComponent>(entityID);

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
    auto* rb = &world->GetComponent<Engine::ECS::Component::RigidbodyComponent>(entityID);
    if (rb->bodyType == 2) { // Dynamic body
        b2MassData massData = b2Body_GetMassData(bodyId);
        massData.mass = rb->mass;
        b2Body_SetMassData(bodyId, massData);
    }
}

void PhysicsSystem::StepPhysics(float deltaTime) {
    // Use fixed time step for stable physics
    timeAccumulator += deltaTime;

    while (timeAccumulator >= fixedTimeStep) {
        b2World_Step(physicsWorldId, fixedTimeStep, subStepCount);
        timeAccumulator -= fixedTimeStep;
    }
}

void PhysicsSystem::SyncTransforms(World* world) {
    // Sync all physics bodies back to transform components
    for (auto& [entityID, bodyId] : entityBodyMap) {
        if (b2Body_IsValid(bodyId) && world->HasComponent<Engine::ECS::Component::TransformComponent>(entityID)) {
            auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(entityID);

            b2Vec2 pos = b2Body_GetPosition(bodyId);
            b2Rot rot = b2Body_GetRotation(bodyId);

            // Update world position
            transform.worldPosition.x = pos.x;
            transform.worldPosition.y = pos.y;

            // Convert rotation to angle in degrees
            float angleRadians = b2Rot_GetAngle(rot);
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
