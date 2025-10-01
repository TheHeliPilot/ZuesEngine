// HierarchySystem.cpp

#include "../include/Engine/ECS/Systems/HierarchySystem.h"
#include <cmath> // Needed for M_PI, std::cos, std::sin
#include <stdexcept> // Needed for std::exception

HierarchySystem::HierarchySystem() {
    role = SystemRole::Shared;
}

void HierarchySystem::Run(World *world, float deltaTime) {
    world->ForEach<Engine::ECS::Component::TransformComponent*>(
    [&](EntityID entityID, Engine::ECS::Component::TransformComponent* childT) {

        // 1. Check if the entity is a root (no parent)
        if (!childT->parent.IsValid()) {
            return;
        }

        try {
            // 2. Get the Parent's TransformComponent (P_W, P_R) via World lookup
            // Assumes world->GetComponent<T> throws if the entity doesn't exist or lacks the component
            const Engine::ECS::Component::TransformComponent& parentT = world->GetComponent<Engine::ECS::Component::TransformComponent>(childT->parent);

            // --- World Rotation Calculation (C_R^W = P_R + C_R) ---
            // The child's world rotation is the sum of the parent's world rotation and its own local rotation.
            childT->worldRotation = parentT.worldRotation + childT->localRotation;

            // --- World Position Calculation ---

            // Convert parent's WORLD rotation (P_R) to radians (theta)
            // NOTE: The original code was missing the definition of M_PI, which is standard in <cmath>
            const float parentRotRad = parentT.worldRotation * static_cast<float>(M_PI) / 180.0f;
            const float cosR = std::cos(parentRotRad);
            const float sinR = std::sin(parentRotRad);

            const Engine::Math::Vec2& localPos = childT->localPosition;

            // 3. Calculate the Rotated Offset Vector (C_L^Rotated)
            // This applies the parent's world rotation to the child's local offset.
            Engine::Math::Vec2 rotatedLocalPos;
            rotatedLocalPos.x = (localPos.x * cosR) - (localPos.y * sinR);
            rotatedLocalPos.y = (localPos.x * sinR) + (localPos.y * cosR);

            // 4. Calculate the final World Position (C_W = P_W + C_L^Rotated)
            // The final world position is the parent's world position plus the rotated local offset.
            childT->worldPosition = parentT.worldPosition + rotatedLocalPos;

        } catch (const std::exception& e) {
            // Error handling: If the parent entity is missing (e.g., destroyed out of sync),
            // we treat the child as a root entity to prevent crashes and log the error.
            childT->parent = NULL_ENTITY_ID;
            childT->worldPosition = childT->localPosition;
            childT->worldRotation = childT->localRotation;
            // Optionally log the error here using your engine's logger:
            // Engine::Log::Warn("HierarchySystem: Parent entity missing for ID %llu", entityID.id);
        }
    });
}

// SystemBase requires this to be defined, but the logic is in Run.
void HierarchySystem::Update(float deltaTime, Engine::ECS::Component::TransformComponent *) {
    // Implementation is in Run(World*, float)
}