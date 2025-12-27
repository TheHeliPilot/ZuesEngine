// ViewportCameraSystem.cpp

#include "../include/ViewportCameraSystem.h"
#include "../include/EditorUi.h"

#include "Renderer.h"
#include "Input.h"
#include <GLFW/glfw3.h>

// Static state for camera dragging
static bool s_IsDragging = false;
static Engine::Math::Vec2 s_LastMousePos = {0, 0};

ViewportCameraSystem::ViewportCameraSystem() {
    role = SystemRole::Editor;
}

void ViewportCameraSystem::Update(float deltaTime,
                                   Engine::ECS::Component::TransformComponent* transform,
                                   Engine::ECS::Component::CameraComponent* cam,
                                   Engine::ECS::Component::ViewportCameraTag* tag) {
    // Ensure viewport size is valid
    if (EditorWindows::EditorUi::viewportSize.x <= 0 || EditorWindows::EditorUi::viewportSize.y <= 0) {
        return;
    }

    // Only process camera controls when mouse is in viewport and not in play mode
    if (!EditorWindows::EditorUi::MouseInWindow("Viewport") || EditorWindows::EditorUi::isPlayMode) {
        s_IsDragging = false;
        return;
    }

    // Get current mouse position
    Engine::Math::Vec2 mousePos = Engine::Input::GetMousePosition();

    // Right-click drag for panning
    if (Engine::Input::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
        if (!s_IsDragging) {
            s_IsDragging = true;
            s_LastMousePos = mousePos;
        } else {
            Engine::Math::Vec2 delta = mousePos - s_LastMousePos;

            float worldScale = cam->halfHeight / (EditorWindows::EditorUi::viewportSize.y * 0.5f);

            transform->localPosition.x -= delta.x * worldScale / cam->zoom;
            transform->localPosition.y += delta.y * worldScale / cam->zoom;

            s_LastMousePos = mousePos;
        }
    } else {
        s_IsDragging = false;
    }

    // Scroll wheel for zooming
    float scroll = Engine::Input::GetMouseScrollDelta();
    if (scroll != 0.0f) {
        float zoomFactor = 1.0f + scroll * 0.1f;
        cam->zoom *= zoomFactor;

        if (cam->zoom < 0.1f) cam->zoom = 0.1f;
        if (cam->zoom > 10.0f) cam->zoom = 10.0f;
    }

    // Apply viewport camera to renderer
    Engine::Renderer::SetCamera(transform->worldPosition, cam->zoom, cam->halfHeight, transform->worldRotation);
}
