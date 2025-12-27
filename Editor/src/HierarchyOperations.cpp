//
// Created by bucka on 9/30/2025.
//

#include "../include/HierarchyOperations.h"
#include "../include/HierarchyOperations.h"

#include <GLFW/glfw3.h>

#include "Core.h"
#include "../include/EditorUi.h"

#include "Engine.h"
#include "../include/EditorDefines.h"

HierarchyOperations::DraggingOperation HierarchyOperations::draggingStatus = DraggingOperation::None;
Engine::Math::Vec2 HierarchyOperations::lastMousePos = {-1, -1};

void HierarchyOperations::DoHierarchyOperations() {
    const auto hierarchy = Engine::ECS::Hierarchy::GetFlattenedHierarchy();

    if (Engine::Input::IsMouseButtonPressed(0)) {
        for (const auto [id, depth] : hierarchy) {
            if (id.id == 0) continue;
            Engine::ECS::Component::TransformComponent& transform_component = Engine::Core::GetCurrentWorld()->GetComponent<Engine::ECS::Component::TransformComponent>(id);

            if (draggingStatus == DraggingOperation::None) {
                if (Engine::Input::IsMouseButtonJustPressed(0) && EditorWindows::EditorUi::MouseInWindow("Viewport")) {
                    if (const float dis = (MOUSE_POS_WORLD - transform_component.worldPosition).Length(); dis < 1) {
                        // TODO: Multiselect Movement fix
                        if (Engine::Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Engine::Input::IsKeyPressed(GLFW_KEY_RIGHT_SHIFT))
                        {
                            EditorWindows::EditorUi::selectedEntities.clear();
                            EditorWindows::EditorUi::selectedEntities.push_back(id);
                        } else
                        {
                            EditorWindows::EditorUi::selectedEntities.clear();
                            EditorWindows::EditorUi::selectedEntities.push_back(id);
                        }
                        //LOG_INFO("Clicked on id " + std::to_string(id.id));
                        return;
                    }
                    EditorWindows::EditorUi::selectedEntities.clear();
                }
            } else if (EditorWindows::EditorUi::IsEntitySelected(id)) {

                if (lastMousePos.x == -1) {
                    lastMousePos = MOUSE_POS_WORLD;
                }

                // Calculate world-space delta
                Engine::Math::Vec2 worldDelta;
                worldDelta.x = MOUSE_POS_WORLD.x - lastMousePos.x;
                worldDelta.y = MOUSE_POS_WORLD.y - lastMousePos.y;

                switch (draggingStatus) {
                    case DraggingOperation::MoveX:
                    case DraggingOperation::MoveY:
                    case DraggingOperation::MoveXY: {
                        // Get the parent's world rotation to convert world delta to local delta
                        float parentWorldRotation = 0.0f;
                        if (transform_component.parent.IsValid()) {
                            try {
                                const Engine::ECS::Component::TransformComponent& parentT =
                                    Engine::Core::GetCurrentWorld()->GetComponent<Engine::ECS::Component::TransformComponent>(transform_component.parent);
                                parentWorldRotation = parentT.worldRotation;
                            } catch (...) {
                                // Parent missing, treat as root (no rotation)
                            }
                        }

                        // Convert world delta to local delta by rotating by -parentWorldRotation
                        const float parentRotRad = -parentWorldRotation * static_cast<float>(M_PI) / 180.0f;
                        const float cosR = std::cos(parentRotRad);
                        const float sinR = std::sin(parentRotRad);

                        Engine::Math::Vec2 localDelta;
                        localDelta.x = (worldDelta.x * cosR) - (worldDelta.y * sinR);
                        localDelta.y = (worldDelta.x * sinR) + (worldDelta.y * cosR);

                        // Apply the appropriate axis constraints
                        if (draggingStatus == DraggingOperation::MoveX) {
                            transform_component.localPosition.x += localDelta.x;
                        } else if (draggingStatus == DraggingOperation::MoveY) {
                            transform_component.localPosition.y += localDelta.y;
                        } else { // MoveXY
                            transform_component.localPosition.x += localDelta.x;
                            transform_component.localPosition.y += localDelta.y;
                        }
                        break;
                    }
                    case DraggingOperation::Rotate:
                        transform_component.localRotation -= worldDelta.x * 5;
                        break;
                    case DraggingOperation::ScaleX:
                        break;
                    case DraggingOperation::ScaleY:
                        break;
                    case DraggingOperation::ScaleXY:
                        break;
                    default: LOG_ERROR("Dragging operation out of bounds?? (What have you done?)");
                }
                lastMousePos = MOUSE_POS_WORLD;
            }
        }
    }
}