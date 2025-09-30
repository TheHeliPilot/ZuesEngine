// CameraSystem.h

#pragma once
#include "../System.h"
#include "../Components.h"

// This system finds the active camera and tells the Renderer what the view matrix should be.
class CameraSystem final : public SystemBase<TransformComponent*, CameraComponent*> {
public:
    CameraSystem();

    // The base System::Run (in World.inl) will call QueryAndRun, which calls this Update.
    void Update(float deltaTime, TransformComponent* pos, CameraComponent* cam) override;
};