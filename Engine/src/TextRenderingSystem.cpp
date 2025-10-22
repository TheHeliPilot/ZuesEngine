// TextRenderingSystem.cpp

#include "../include/Engine/ECS/Systems/TextRenderingSystem.h"
#include "../include/Engine/Renderer.h"
#include "../include/Engine/ECS/World.h"
#include <iostream>
#include <algorithm>

TextRenderingSystem::TextRenderingSystem() {
    role = SystemRole::Shared;
}

void TextRenderingSystem::Run(World* world, const float deltaTime) {
    const ComponentSignature requiredSignature = this->signature;

    std::vector<RenderableTextEntity> renderables;

    // --- Manual Archetype Iteration ---
    for (const auto& pair : world->GetArchetypes()) {
        const ComponentSignature& archetypeSignature = pair.first;
        Archetype* archetype = pair.second.get();

        // Check if this Archetype contains all the required components
        if ((archetypeSignature & requiredSignature) == requiredSignature) {
            // Get component arrays for the required types
            auto* transformArray = archetype->GetComponentArray<Engine::ECS::Component::TransformComponent>();
            auto* textArray = archetype->GetComponentArray<Engine::ECS::Component::TextComponent>();

            if (!transformArray || !textArray) continue;

            // Loop through all entities in this matching Archetype
            for (size_t i = 0; i < archetype->entityIDs.size(); ++i) {
                renderables.push_back({
                    transformArray->GetData() + i,
                    textArray->GetData() + i
                });
            }
        }
    }

    // Sort the collected entities by Layer and then SortOrder
    std::sort(renderables.begin(), renderables.end(),
        [](const RenderableTextEntity& a, const RenderableTextEntity& b) {
            if (a.text->layer != b.text->layer) {
                return a.text->layer < b.text->layer; // Lower layer first (back)
            }
            return a.text->sortOrder < b.text->sortOrder; // Lower sortOrder first (back)
        }
    );

    // Execute the rendering logic in sorted order
    for (const auto& entity : renderables) {
        Update(deltaTime, entity.transform, entity.text);
    }
}

void TextRenderingSystem::Update(float deltaTime, Engine::ECS::Component::TransformComponent* transform, Engine::ECS::Component::TextComponent* text) {
    // Skip rendering if fully transparent or empty text
    if (text->color.w <= 0.0f || text->text.empty()) {
        return;
    }

    // Render the text using the Renderer::DrawText function
    Engine::Renderer::DrawText(
        text->fontID,
        text->text,
        transform->worldPosition,
        text->color,
        text->scale,
        text->worldScale,
        transform->worldRotation * Engine::Math::DEGREES_TO_RADIANS
    );
}
