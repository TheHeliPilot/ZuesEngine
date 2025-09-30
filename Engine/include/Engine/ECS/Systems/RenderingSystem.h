#pragma once
#include "../System.h"
#include "../Components.h"
#include "../../Renderer.h"
#include "../World.h" // REQUIRED to access World::GetArchetypes()
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath> // For degrees to radians constant

// Helper struct to hold an Entity's components for sorting
struct RenderableEntity {
    PositionComponent* pos;
    SpriteComponent* sprite;
};

class RenderingSystem : public SystemBase<PositionComponent*, SpriteComponent*> {
public:
    RenderingSystem() {
        role = SystemRole::Shared;
    }

    void Run(World* world, const float deltaTime) override {
        // 1. Start the batching process
        Engine::Renderer::BeginBatch();

        // FIX 1: Use the public 'signature' member inherited from SystemBase
        const ComponentSignature requiredSignature = this->signature;

        std::vector<RenderableEntity> renderables;

        // --- Manual Archetype Iteration (Replaces QueryAndCollect) ---
        // Requires the GetArchetypes() method to be added to World.h
        for (const auto& pair : world->GetArchetypes()) {
            const ComponentSignature& archetypeSignature = pair.first;
            Archetype* archetype = pair.second.get();

            // Check if this Archetype contains all the required components
            if ((archetypeSignature & requiredSignature) == requiredSignature) {
                // Get component arrays for the required types
                auto* posArray = archetype->GetComponentArray<PositionComponent>();
                auto* spriteArray = archetype->GetComponentArray<SpriteComponent>();

                if (!posArray || !spriteArray) continue;

                // Loop through all entities in this matching Archetype
                for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {

                    // FIX 2 & 3: Use GetData() and pointer arithmetic to get the component pointer
                    renderables.push_back({
                        posArray->GetData() + i,
                        spriteArray->GetData() + i
                    });
                }
            }
        }
        // --- END Manual Iteration ---


        // 2. Sort the collected entities by Layer and then SortOrder
        std::sort(renderables.begin(), renderables.end(),
            [](const RenderableEntity& a, const RenderableEntity& b) {
                if (a.sprite->layer != b.sprite->layer) {
                    return a.sprite->layer < b.sprite->layer; // Lower layer first (back)
                }
                return a.sprite->sortOrder < b.sprite->sortOrder; // Lower sortOrder first (back)
            }
        );

        // 3. Execute the rendering logic (call Update internally) in sorted order
        for (const auto& entity : renderables) {
            Update(deltaTime, entity.pos, entity.sprite);
        }

        // 4. EndBatch is deferred to outside the systems (e.g., main.cpp)
    }

    // The per-entity logic: Only submits data to the batch buffer.
    void Update(float deltaTime, PositionComponent* pos, SpriteComponent* sprite) override {
        Engine::Renderer::SubmitQuad(
            pos->position,
            // Convert from degrees (in the component) to radians (for the Renderer)
            pos->rotation * Engine::Math::DEGREES_TO_RADIANS,
            sprite->size,
            sprite->color,
            sprite->textureID
        );
    }
};