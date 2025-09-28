#pragma once

#include <memory>
#include <cmath> // For converting degrees to radians if needed later

#include "ECS/Components.h"
#include "ECS/World.h"

namespace Engine {
    // A hypothetical function to set up the game world
    inline void SetupSimpleScene(World* world, const uint32_t textureHandle) {


        EntityID cameraEntity = world->CreateEntity();
        world->AddComponent<PositionComponent>(cameraEntity, {
            .position = {0.0f, 0.0f}, // Center the camera at world origin
            .rotation = 0.0f
        });
        world->AddComponent<CameraComponent>(cameraEntity, {
            .zoom = 1.0f,
            .halfHeight = 10.0f, // Viewport height will be 20 world units (10 up, 10 down)
            .isActive = true
        });


        // --- Step B: Define Renderable Entities ---

        // 1. Red Square Entity
        const EntityID redSquare = world->CreateEntity();
        world->AddComponent<PositionComponent>(redSquare, {
            .position = {0.0f, 0.0f}, // Top-left area
            .rotation = 0.0f // Rotated 45 degrees
        });
        world->AddComponent<SpriteComponent>(redSquare, {
            .textureID = textureHandle, // Use the provided texture
            .size = {2.0f, 2.0f},
            .color = {1.0f, 1.0f, 1.0f, 1.0f}
        });
    }
}