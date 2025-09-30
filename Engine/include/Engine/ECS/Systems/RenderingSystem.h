#pragma once
#include "../System.h"
#include "../Components.h"
#include <vector>

class World; // Forward declaration

// Helper struct to hold an Entity's components for sorting
struct RenderableEntity {
    TransformComponent* pos;
    SpriteComponent* sprite;
};

class RenderingSystem : public SystemBase<TransformComponent*, SpriteComponent*> {
public:
    RenderingSystem();

    void Run(World* world, const float deltaTime) override;

    // The per-entity logic: Only submits data to the batch buffer.
    void Update(float deltaTime, TransformComponent* pos, SpriteComponent* sprite) override;
};