// ViewportCameraSystem.h

#pragma once

#include "ECS/System.h"
#include "ECS/Components.h"

class ViewportCameraSystem final : public SystemBase<
    Engine::ECS::Component::TransformComponent*,
    Engine::ECS::Component::CameraComponent*,
    Engine::ECS::Component::ViewportCameraTag*
> {
public:
    ViewportCameraSystem();

    void Update(float deltaTime,
                Engine::ECS::Component::TransformComponent* transform,
                Engine::ECS::Component::CameraComponent* cam,
                Engine::ECS::Component::ViewportCameraTag* tag) override;
};
