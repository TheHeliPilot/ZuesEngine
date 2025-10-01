// ViewportCameraSystem.h

#pragma once
#include "../../Input.h"
#include "../System.h"
#include "../Components.h"

class ViewportCameraSystem final : public SystemBase<Engine::ECS::Component::TransformComponent*, Engine::ECS::Component::CameraComponent*, Engine::ECS::Component::ViewportCameraTag*> {
public:
    ViewportCameraSystem();

    void Update(const float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::CameraComponent* cam, Engine::ECS::Component::ViewportCameraTag* vc_tag) override;
};