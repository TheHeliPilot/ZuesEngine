#pragma once
#include <string>
#include "Engine.h"
#include "imgui.h"

namespace EditorWindows {

    // Stores the state of which collider is currently being edited
    struct ColliderEditState {
        EntityID editingEntityID = NULL_ENTITY_ID;
        int editingComponentTypeID = -1;
        bool isEditing = false;

        // Dragging state
        bool isDragging = false;
        int draggingHandleIndex = -1;
        Engine::Math::Vec2 dragStartMousePos = {0, 0};
        Engine::Math::Vec2 dragStartValue = {0, 0};
    };

    class PhysicsComponentEditor {
    public:
        // Draw custom editor for RigidBody component
        static bool DrawRigidbodyEditor(const char* name, nlohmann::json& j, int componentTypeID, EntityID entityID);

        // Draw custom editor for BoxCollider component
        static bool DrawBoxColliderEditor(const char* name, nlohmann::json& j, int componentTypeID, EntityID entityID);

        // Draw custom editor for CircleCollider component
        static bool DrawCircleColliderEditor(const char* name, nlohmann::json& j, int componentTypeID, EntityID entityID);

        // Draw gizmos in the viewport for the currently edited collider
        static void DrawColliderGizmos();

        // Get the current edit state
        static ColliderEditState& GetEditState() { return s_EditState; }

    private:
        static ColliderEditState s_EditState;

        // Helper to draw common collider properties
        static bool DrawColliderProperties(nlohmann::json& j);

        // Helper to draw body type dropdown
        static bool DrawBodyTypeDropdown(nlohmann::json& bodyTypeValue);

        // Draw gizmo for box collider
        static void DrawBoxColliderGizmo(World* world, EntityID entityID, const nlohmann::json& colliderJson);

        // Draw gizmo for circle collider
        static void DrawCircleColliderGizmo(World* world, EntityID entityID, const nlohmann::json& colliderJson);

        // Convert world position to screen position in viewport
        static ImVec2 WorldToScreen(const Engine::Math::Vec2& worldPos, World* world);

        // Convert screen position in viewport to world position
        static Engine::Math::Vec2 ScreenToWorld(const ImVec2& screenPos, World* world);

        // Check if mouse is over a handle
        static bool IsMouseOverHandle(const ImVec2& handlePos, float handleRadius = 6.0f);
    };
}
