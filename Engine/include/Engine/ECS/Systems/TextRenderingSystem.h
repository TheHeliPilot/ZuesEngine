#pragma once
#include "../System.h"
#include "../Components.h"
#include <vector>

class World; // Forward declaration

// Helper struct to hold an Entity's components for sorting
struct RenderableTextEntity {
    Engine::ECS::Component::TransformComponent* transform;
    Engine::ECS::Component::TextComponent* text;
};

class TextRenderingSystem : public SystemBase<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::TextComponent*> {
public:
    TextRenderingSystem();

    void Run(World* world, const float deltaTime) override;

    // The per-entity logic: Only submits data to the batch buffer.
    void Update(float deltaTime, Engine::ECS::Component::TransformComponent* transform, Engine::ECS::Component::TextComponent* text) override;
};
