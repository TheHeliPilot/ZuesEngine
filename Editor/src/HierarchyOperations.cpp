//
// Created by bucka on 9/30/2025.
//

#include "../include/HierarchyOperations.h"
#include "../include/HierarchyOperations.h"

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
                if (Engine::Input::IsMouseButtonJustPressed(0)) {
                    if (const float dis = (MOUSE_POS_WORLD - transform_component.worldPosition).Length(); dis < 1) {
                        EditorWindows::EditorUi::selectedEntity = id;
                        //LOG_INFO("Clicked on id " + std::to_string(id.id));
                        return;
                    }
                    EditorWindows::EditorUi::selectedEntity = NULL_ENTITY_ID;
                }
            } else if (id.id == EditorWindows::EditorUi::selectedEntity.id) {

                if (lastMousePos.x == -1) {
                    lastMousePos = MOUSE_POS_WORLD;
                }

                switch (draggingStatus) {
                    case DraggingOperation::MoveX:
                        transform_component.localPosition.x += MOUSE_POS_WORLD.x - lastMousePos.x;
                        break;
                    case DraggingOperation::MoveY:
                        transform_component.localPosition.y += MOUSE_POS_WORLD.y - lastMousePos.y;
                        break;
                    case DraggingOperation::MoveXY:
                        transform_component.localPosition.x += MOUSE_POS_WORLD.x - lastMousePos.x;
                        transform_component.localPosition.y += MOUSE_POS_WORLD.y - lastMousePos.y;
                        break;
                    case DraggingOperation::Rotate:
                        transform_component.localRotation -= (MOUSE_POS_WORLD.x - lastMousePos.x) * 5;
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
