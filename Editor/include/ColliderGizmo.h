#pragma once

#include "ECS/Entity.h"
#include "ZuesMath.h"

namespace Editor {

    // Gizmo handle types for collider editing
    enum class ColliderHandle {
        None,
        Left,
        Right,
        Top,
        Bottom,
        Radius,      // For circle collider edge
        Center       // For circle collider offset movement
    };

    // Accessor functions from ColliderInspector
    bool IsColliderEditMode();
    EntityID GetEditingColliderEntity();
    bool IsEditingBoxCollider();
    void ClearColliderEditMode();

    class ColliderGizmo {
    public:
        // Draw collider gizmos for selected entities
        static void Draw();

        // Handle mouse input for collider editing
        static void Update();

        // Check if we're currently dragging a handle
        static bool IsDragging() { return s_ActiveHandle != ColliderHandle::None; }

    private:
        // Draw box collider with edit handles
        static void DrawBoxCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation, bool isEditMode);

        // Draw circle collider with edit handle
        static void DrawCircleCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation, bool isEditMode);

        // Get handle at mouse position for box collider
        static ColliderHandle GetBoxHandleAtMouse(
            const Engine::Math::Vec2& colliderCenter,
            const Engine::Math::Vec2& colliderSize,
            float rotation,
            const Engine::Math::Vec2& mouseWorld
        );

        // Get handle at mouse position for circle collider
        static ColliderHandle GetCircleHandleAtMouse(
            const Engine::Math::Vec2& colliderCenter,
            float radius,
            const Engine::Math::Vec2& mouseWorld
        );

        static ColliderHandle s_ActiveHandle;
        static EntityID s_ActiveEntity;
        static Engine::Math::Vec2 s_DragStart;
        static bool s_IsBoxCollider;  // true = box, false = circle
    };

} // namespace Editor
