// CameraSystem.cpp

#include "../include/Engine/ECS/Systems/CameraSystem.h"
#include "../include/Engine/Renderer.h"
#include <cmath> // Needed for implementation

CameraSystem::CameraSystem() {
    role = SystemRole::Shared;
}

// This system finds the active camera and tells the Renderer what the view matrix should be.
void CameraSystem::Update(float deltaTime, TransformComponent* pos, CameraComponent* cam) {
    if (cam->isActive) {
        // Set the global View-Projection matrix in the Renderer.
        // We ensure rotation is converted from degrees (in Component) to radians (for Mat4::Rotate).
        Engine::Renderer::SetCamera(
            pos->worldPosition,
            cam->zoom,
            cam->halfHeight,
            pos->worldRotation * Engine::Math::DEGREES_TO_RADIANS
        );

        Engine::Renderer::SetClearColor(cam->backgroundColor);
    }
}