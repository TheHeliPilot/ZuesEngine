// ViewportCameraSystem.h

#pragma once
#include "../../Input.h"
#include "../System.h"
#include "../Components.h"

class ViewportCameraSystem final : public SystemBase<TransformComponent*, CameraComponent*, ViewportCameraTag*> {
public:
    ViewportCameraSystem();

    void Update(const float deltaTime, TransformComponent* pos, CameraComponent* cam, ViewportCameraTag* vc_tag) override;
};