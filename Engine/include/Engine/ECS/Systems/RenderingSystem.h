// RenderingSystem.h (FIXED)

#pragma once
#include "../System.h"
#include "../Components.h"
#include "../../Renderer.h"
#include <iostream>

// Conversions utility (Must be available here too)

class RenderingSystem : public SystemBase<PositionComponent*, SpriteComponent*> {
public:
    RenderingSystem() {
        role = SystemRole::Shared;
    }

    void Run(World* world, const float deltaTime) override {
        // 1. Start the batching process (MUST be done first)
        Engine::Renderer::BeginBatch(); // <--- REQUIRED START

        // 2. Execute the QueryAndRun mechanism (this calls Update for all matching entities)
        world->QueryAndRun<PositionComponent*, SpriteComponent*>(this, deltaTime);

        // 3. DO NOT CALL EndBatch() here. It will be called in main.cpp
        //    after all systems run to allow debug draws.
    }

    // The per-entity logic: Only submits data to the batch buffer.
    void Update(float deltaTime, PositionComponent* pos, SpriteComponent* sprite) override {
        Engine::Renderer::SubmitQuad(
            pos->position,
            // FIX: Convert from degrees (in the component) to radians (for the Renderer)
            pos->rotation * Engine::Math::DEGREES_TO_RADIANS,
            sprite->size,
            sprite->color,
            sprite->textureID
        );
    }
};