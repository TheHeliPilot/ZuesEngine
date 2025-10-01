// ViewportCameraSystem.cpp

#include "../include/Engine/ECS/Systems/ViewportCameraSystem.h"

#include "../include/Engine/EngineDefines.h"
#include "../include/Engine/Renderer.h"

// NOTE: Including Input.h in the header was correct because it defines Engine::Input::IsKeyPressed
// However, the original file was ViewportCameraSystem.h, so I'll name the file based on the class.

ViewportCameraSystem::ViewportCameraSystem() {
    role = SystemRole::Editor;
}

void ViewportCameraSystem::Update(const float deltaTime, Engine::ECS::Component::TransformComponent* pos, Engine::ECS::Component::CameraComponent* cam, Engine::ECS::Component::ViewportCameraTag* vc_tag) {


    if (Engine::Input::IsKeyPressed(GLFW_KEY_W)) {
        pos->worldPosition.y += deltaTime * vc_tag->cameraSpeed;
        //LOG_INFO(std::to_string(pos->worldPosition.y));
    }
    if (Engine::Input::IsKeyPressed(GLFW_KEY_S)) {
        pos->worldPosition.y -= deltaTime * vc_tag->cameraSpeed;
    }
    if (Engine::Input::IsKeyPressed(GLFW_KEY_A)) {
        pos->worldPosition.x -= deltaTime * vc_tag->cameraSpeed;
    }
    if (Engine::Input::IsKeyPressed(GLFW_KEY_D)) {
        pos->worldPosition.x += deltaTime * vc_tag->cameraSpeed;
    }
}