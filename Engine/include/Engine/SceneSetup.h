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

    inline void SetupHierarchyTestScene(World* world, const uint32_t textureHandle) {

        // --- Parent Entity 1 (Root) ---
        const EntityID parent1 = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(parent1, {
            .worldPosition = {-5.0f, 5.0f},
            // Note: worldScale is not in TransformComponent, so we omit it.
        });
        world->AddComponent<ECS::Component::SpriteComponent>(parent1, {
            .spriteName = "",
            .size = {2.0f, 2.0f}, // Using Sprite size for visual scale
            .color = {1.0f, 0.0f, 0.0f, 1.0f}, // Red
        });

        // --- Child Entity 1.1 ---
        const EntityID child1_1 = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(child1_1, {
            .worldRotation = 45.0f,
            .localPosition = {3.0f, 0.0f},
            .parent = parent1 // <-- SETTING HIERARCHY HERE
        });
        world->AddComponent<ECS::Component::SpriteComponent>(child1_1, {
            .spriteName = "",
            .size = {1.0f, 1.0f},
            .color = {0.0f, 1.0f, 0.0f, 1.0f}, // Green
        });

        // --- Child Entity 1.1.1 (Grandchild) ---
        const EntityID grandchild1_1_1 = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(grandchild1_1_1, {
            .localPosition = {0.0f, -2.0f},
            .parent = child1_1 // <-- SETTING HIERARCHY HERE
        });
        world->AddComponent<ECS::Component::SpriteComponent>(grandchild1_1_1, {
            .spriteName = "",
            .size = {0.5f, 0.5f},
            .color = {0.0f, 0.0f, 1.0f, 1.0f}, // Blue
        });

        // --- Parent Entity 2 (Another Root) ---
        const EntityID parent2 = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(parent2, {
            .worldPosition = {5.0f, -5.0f},
            .worldRotation = 90.0f,
        });
        world->AddComponent<ECS::Component::SpriteComponent>(parent2, {
            .spriteName = "",
            .size = {1.5f, 1.5f},
            .color = {1.0f, 1.0f, 0.0f, 1.0f}, // Yellow
        });
        world->AddComponent<ECS::Component::MainCameraTag>(parent2, {});

        // --- Child Entity 2.1 ---
        const EntityID child2_1 = world->CreateEntity();
        world->AddComponent<Engine::ECS::Component::TransformComponent>(child2_1, {
            .localPosition = {-1.0f, 0.0f},
            .parent = parent2 // <-- SETTING HIERARCHY HERE
        });
        world->AddComponent<ECS::Component::SpriteComponent>(child2_1, {
            .spriteName = "",
            .size = {0.7f, 0.7f},
            .color = {0.0f, 1.0f, 1.0f, 1.0f}, // Cyan
        });

        // Rebuild hierarchy cache after all entities are created
        ECS::Hierarchy::BuildCache(world);
    }
}
