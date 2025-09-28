// RenderingSystem.h (FIXED)

#pragma once
#include "../System.h"
#include "../Components.h"
#include "../../Renderer.h"
#include <iostream>

// Conversions utility (Must be available here too)

class RenderingSystem : public SystemBase<PositionComponent*, SpriteComponent*> {
public:
    // CRITICAL FIX: We MUST override Run to manage the batch lifecycle.
    void Run(World* world, float deltaTime) override {
        // 1. Start the batching process
        Engine::Renderer::BeginBatch();

        // 2. Execute the QueryAndRun mechanism (this calls Update for all matching entities)
        world->QueryAndRun<PositionComponent*, SpriteComponent*>(this, deltaTime);

        // 3. Submit the batch to the GPU (Draws the entire scene)
        Engine::Renderer::EndBatch();
    }

    // The per-entity logic: Only submits data to the batch buffer.
    void Update(float deltaTime, PositionComponent* pos, SpriteComponent* sprite) override {
        // NOTE: Begin/EndBatch MUST NOT be called here.
        Engine::Renderer::SubmitQuad(
            pos->position,
            pos->rotation * Engine::Math::DEGREES_TO_RADIANS, // FIX: Convert from degrees to radians
            sprite->size,
            sprite->color,
            sprite->textureID
        );
    }
};