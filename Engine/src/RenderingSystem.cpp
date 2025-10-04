// RenderingSystem.cpp

#include "../include/Engine/ECS/Systems/RenderingSystem.h"
#include "../include/Engine/Renderer.h"
#include "../include/Engine/ECS/World.h"
#include <iostream>
#include <algorithm>
#include <cmath> // For degrees to radians constant

#include "../include/Engine/TextureManager.h"

RenderingSystem::RenderingSystem() {
    role = SystemRole::Shared;
}

void RenderingSystem::Run(World* world, const float deltaTime) {
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
            auto* posArray = archetype->GetComponentArray<Engine::ECS::Component::TransformComponent>();
            auto* spriteArray = archetype->GetComponentArray<Engine::ECS::Component::SpriteComponent>();

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

void RenderingSystem::Update(float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::SpriteComponent* sprite) {
    // Skip rendering if fully transparent
    if (sprite->color.w <= 0.0f ||sprite->spriteName == "") {
        return;
    }

    uint32_t textureID = 0;
    Engine::Math::Vec4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f};

    if (!sprite->spriteName.empty()) {
        Engine::TextureInfo texInfo = Engine::TextureManager::GetTexture(sprite->spriteName);

        if (texInfo.ID != 0) {
            textureID = texInfo.ID;
            uvRect = texInfo.TextureUVRect;
        }
    }

    Engine::Renderer::SubmitQuad(
        pos->worldPosition,
        0,
        sprite->size,
        sprite->color,
        textureID,
        0.0f,
        uvRect
    );
}