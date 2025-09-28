// CameraSystem.h

#pragma once
#include "../System.h"
#include "../Components.h"
#include "../../Renderer.h"
#include <cmath>

// Conversions utility (Degrees to Radians)

// This system finds the active camera and tells the Renderer what the view matrix should be.
class CameraSystem final : public SystemBase<PositionComponent*, CameraComponent*> {
public:
    // The base System::Run (in World.inl) will call QueryAndRun, which calls this Update.
    void Update(float deltaTime, PositionComponent* pos, CameraComponent* cam) override {
        if (cam->isActive) {
            // Set the global View-Projection matrix in the Renderer.
            // We ensure rotation is converted from degrees (in Component) to radians (for Mat4::Rotate).
            Engine::Renderer::SetCamera(
                pos->position,
                cam->zoom,
                cam->halfHeight,
                pos->rotation * Engine::Math::DEGREES_TO_RADIANS
            );

            Engine::Renderer::SetClearColor(cam->backgroundColor);
        }
    }
    // No need to override Run; the base SystemBase::Run handles the iteration setup.
};