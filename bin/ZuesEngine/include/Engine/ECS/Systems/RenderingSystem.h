#pragma once

#include "../../ZuesAPI.h"
#include "../System.h"
#include "../Components.h"
#include <vector>

class World; // Forward declaration

// Helper struct to hold an Entity's components for sorting
struct ZUES_API RenderableEntity {
    Engine::ECS::Component::TransformComponent* pos;
    Engine::ECS::Component::SpriteComponent* sprite;
};

class ZUES_API RenderingSystem : public SystemBase<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::SpriteComponent*> {
public:
    RenderingSystem();

    void Run(World* world, const float deltaTime) override;

    // The per-entity logic: Only submits data to the batch buffer.
    void Update(float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::SpriteComponent* sprite) override;
};