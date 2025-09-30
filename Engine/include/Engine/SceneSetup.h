#pragma once

#include <memory>
#include <cmath> // For converting degrees to radians if needed later

#include "ECS/World.h"
#include "ECS/Components.h"

namespace Engine {
    // A hypothetical function to set up the game world
    inline void SetupSimpleScene(World* world, const uint32_t textureHandle) {


        const EntityID cameraEntity = world->CreateEntity();
        world->AddComponent<PositionComponent>(cameraEntity, {
            .position = {0.0f, 0.0f}, // Center the camera at world origin
            .rotation = 0.0f
        });
        world->AddComponent<CameraComponent>(cameraEntity, {
            .zoom = 1.0f,
            .halfHeight = 10.0f, // Viewport height will be 20 world units (10 up, 10 down)
            .backgroundColor = {0.2f, 0.2f, 0.2f, 1.0f},
            .isActive = true
        });
        world->AddComponent<ViewportCameraTag>(cameraEntity, {

        });

         const EntityID testSquare = world->CreateEntity();
        world->AddComponent<PositionComponent>(testSquare, {
            .position = {0.0f, 0.0f},
            .rotation = 0.0f
        });
        world->AddComponent<SpriteComponent>(testSquare, {
            .textureID = 0,
            .size = {1,1},
            .color = {1,1,1,1},
            .layer = 0,
            .sortOrder = 0
        });

    }
}