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
        Radius  // For circle colliders
    };

    class ColliderGizmo {
    public:
        // Draw collider gizmos for selected entities
        static void Draw();

        // Handle mouse input for collider editing
        static void Update();

        // Check if we're currently editing a collider
        static bool IsEditing() { return s_ActiveHandle != ColliderHandle::None; }

    private:
        // Draw box collider with edit handles
        static void DrawBoxCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation);

        // Draw circle collider with edit handle
        static void DrawCircleCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation);

        // Get handle at mouse position for box collider
        static ColliderHandle GetBoxHandleAtMouse(
            const Engine::Math::Vec2& colliderCenter,
            const Engine::Math::Vec2& colliderSize,
            float rotation,
            const Engine::Math::Vec2& mouseWorld
        );

        // Get if mouse is on radius handle for circle collider
        static bool IsOnRadiusHandle(
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
