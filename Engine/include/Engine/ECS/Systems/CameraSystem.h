// CameraSystem.h

#pragma once
#include "../System.h"
#include "../Components.h"

// This system finds the active camera and tells the Renderer what the view matrix should be.
class CameraSystem final : public SystemBase<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::CameraComponent*, Engine::ECS::Component::MainCameraTag*> {
public:
    CameraSystem();

    // The base System::Run (in World.inl) will call QueryAndRun, which calls this Update.
    void Update(float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::CameraComponent* cam, Engine::ECS::Component::MainCameraTag* mc_tag) override;
};