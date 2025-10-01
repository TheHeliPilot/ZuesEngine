// HierarchySystem.h

#pragma once
#include "../System.h"
#include "../Components.h"
#include "../World.h" // Needed for World::ForEach

// This system finds the active camera and tells the Renderer what the view matrix should be.
class HierarchySystem final : public SystemBase<Engine::ECS::Component::TransformComponent*> {
public:
    HierarchySystem();

    void Run(World *world, float deltaTime) override;

    // No implementation required for this Update, as logic is in Run(World*)
    void Update(float deltaTime, Engine::ECS::Component::TransformComponent *) override;
};