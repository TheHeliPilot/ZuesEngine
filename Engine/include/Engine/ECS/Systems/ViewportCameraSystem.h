// CameraSystem.h

#pragma once
#include "../System.h"
#include "../Components.h"
#include "../../Renderer.h"
#include <cmath>

#include "../../Input.h"

class ViewportCameraSystem final : public SystemBase<PositionComponent*, CameraComponent*, ViewportCameraTag*> {
public:
    void Update(const float deltaTime, PositionComponent* pos, CameraComponent* cam, ViewportCameraTag* vc_tag) override {

        //ENGINE_LOG(std::to_string(deltaTime));

        if (Engine::Input::IsKeyPressed(GLFW_KEY_W)) {
            pos->position.y += deltaTime * vc_tag->cameraSpeed;
        }
        if (Engine::Input::IsKeyPressed(GLFW_KEY_S)) {
            pos->position.y -= deltaTime * vc_tag->cameraSpeed;
        }
        if (Engine::Input::IsKeyPressed(GLFW_KEY_A)) {
            pos->position.x -= deltaTime * vc_tag->cameraSpeed;
        }
        if (Engine::Input::IsKeyPressed(GLFW_KEY_D)) {
            pos->position.x += deltaTime * vc_tag->cameraSpeed;
        }
    }
};
