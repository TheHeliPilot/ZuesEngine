// ViewportCameraSystem.cpp
// NOTE: This is a placeholder - the actual viewport camera is handled in the Editor

#include "../include/Engine/ECS/Systems/ViewportCameraSystem.h"
#include "../include/Engine/EngineDefines.h"

ViewportCameraSystem::ViewportCameraSystem() {
    role = SystemRole::Editor;
}

void ViewportCameraSystem::Update(const float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::CameraComponent* cam, Engine::ECS::Component::ViewportCameraTag* vc_tag) {
    // Viewport camera controls are handled directly in Editor main loop
    // This system exists only for component signature matching
}