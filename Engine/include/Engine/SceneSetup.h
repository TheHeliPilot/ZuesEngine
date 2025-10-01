#pragma once

#include <memory>
#include <cmath> // For converting degrees to radians if needed later

#include "ECS/World.h"
#include "ECS/Components.h"
#include "ECS/HierarchyOutliner.h"

namespace Engine {

    inline void SpawnViewportCamera(World* world) {
        const EntityID cameraEntity = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(cameraEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f
        });
        world->AddComponent<Engine::ECS::Component::CameraComponent>(cameraEntity, {
            .zoom = 1.0f,
            .halfHeight = 10.0f, // Viewport height will be 20 world units (10 up, 10 down)
            .backgroundColor = {0.2f, 0.2f, 0.2f, 1.0f},
            .isActive = true
        });
        world->AddComponent<Engine::ECS::Component::ViewportCameraTag>(cameraEntity, {});
        world->AddComponent<ECS::Component::SpriteComponent>(cameraEntity, {
         .color = {1.0f, 1.0f, 1.0f, 0.0f},
        });
    }

    inline void SetupSimpleScene(World* world, const uint32_t textureHandle) {

        const EntityID cameraEntity = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(cameraEntity, {
            .worldPosition = {0.0f, 0.0f}, // Center the camera at world origin
            .worldRotation = 0.0f
        });
        world->AddComponent<Engine::ECS::Component::CameraComponent>(cameraEntity, {
            .zoom = 1.0f,
            .halfHeight = 10.0f, // Viewport height will be 20 world units (10 up, 10 down)
            .backgroundColor = {0.2f, 0.2f, 0.2f, 1.0f},
            .isActive = true
        });
        world->AddComponent<Engine::ECS::Component::MainCameraTag>(cameraEntity, {});

        ECS::Hierarchy::BuildCache(world);
        //LOG_INFO(("Num objects: ") + ECS::Hierarchy::GetFlattenedHierarchy().size());
    }
}
