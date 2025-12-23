#include "../include/ColliderGizmo.h"
#include "../include/EditorUi.h"
#include "Core.h"
#include "Renderer.h"
#include "Input.h"
#include "ECS/Components.h"
#include <cmath>

namespace Editor {

    // Static member initialization
    ColliderHandle ColliderGizmo::s_ActiveHandle = ColliderHandle::None;
    EntityID ColliderGizmo::s_ActiveEntity = NullEntityID();
    Engine::Math::Vec2 ColliderGizmo::s_DragStart = {0, 0};
    bool ColliderGizmo::s_IsBoxCollider = true;

    // Constants for gizmo appearance
    static constexpr float HANDLE_SIZE = 0.15f;  // Size of the square handle in world units
    static constexpr float HANDLE_HIT_SIZE = 0.25f;  // Larger hitbox for easier clicking
    static constexpr float LINE_THICKNESS = 2.0f;
    static constexpr float GIZMO_Z = 100.0f;  // Draw on top

    // Colors
    static const Engine::Math::Vec4 COLLIDER_COLOR = {0.0f, 1.0f, 0.0f, 0.8f};  // Green
    static const Engine::Math::Vec4 HANDLE_COLOR = {1.0f, 1.0f, 1.0f, 1.0f};    // White
    static const Engine::Math::Vec4 HANDLE_HOVER_COLOR = {1.0f, 1.0f, 0.0f, 1.0f};  // Yellow
    static const Engine::Math::Vec4 TRIGGER_COLOR = {0.0f, 0.5f, 1.0f, 0.8f};   // Blue for triggers

    void ColliderGizmo::Draw() {
        World* world = Engine::Core::GetCurrentWorld();
        if (!world) return;

        // Only draw when not in play mode
        if (EditorWindows::EditorUi::isPlayMode) return;

        // Draw for all selected entities that have colliders
        for (const EntityID& entity : EditorWindows::EditorUi::selectedEntities) {
            if (!entity.IsValid()) continue;

            // Get transform
            if (!world->HasComponent<Engine::ECS::Component::TransformComponent>(entity))
                continue;

            const auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(entity);

            // Check for box collider
            if (world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(entity)) {
                DrawBoxCollider(entity, transform.worldPosition, transform.worldRotation);
            }

            // Check for circle collider
            if (world->HasComponent<Engine::ECS::Component::CircleColliderComponent>(entity)) {
                DrawCircleCollider(entity, transform.worldPosition, transform.worldRotation);
            }
        }
    }

    void ColliderGizmo::DrawBoxCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation) {
        World* world = Engine::Core::GetCurrentWorld();
        if (!world) return;

        const auto& collider = world->GetComponent<Engine::ECS::Component::BoxColliderComponent>(entity);

        // Calculate collider center in world space (applying offset)
        float rotRad = worldRotation * static_cast<float>(M_PI) / 180.0f;
        float cosR = std::cos(rotRad);
        float sinR = std::sin(rotRad);

        Engine::Math::Vec2 offsetWorld;
        offsetWorld.x = collider.offset.x * cosR - collider.offset.y * sinR;
        offsetWorld.y = collider.offset.x * sinR + collider.offset.y * cosR;

        Engine::Math::Vec2 center = {
            worldPos.x + offsetWorld.x,
            worldPos.y + offsetWorld.y
        };

        // Choose color based on trigger state
        Engine::Math::Vec4 color = collider.isTrigger ? TRIGGER_COLOR : COLLIDER_COLOR;

        // Draw the collider outline
        Engine::Renderer::DrawRect(center, collider.size, color, rotRad);

        // Get mouse position in world space
        Engine::Math::Vec2 mouseWorld = Engine::Renderer::ScreenToWorld(Engine::Input::GetMousePosition());

        // Draw handles (squares on edge midpoints)
        auto drawHandle = [&](ColliderHandle handle, Engine::Math::Vec2 localPos) {
            // Transform local position to world
            Engine::Math::Vec2 handleWorld;
            handleWorld.x = center.x + localPos.x * cosR - localPos.y * sinR;
            handleWorld.y = center.y + localPos.x * sinR + localPos.y * cosR;

            // Check if this handle is being hovered or is active
            bool isActive = (s_ActiveEntity.id == entity.id && s_ActiveHandle == handle);
            bool isHovered = false;

            if (s_ActiveHandle == ColliderHandle::None) {
                // Check hover
                float dx = mouseWorld.x - handleWorld.x;
                float dy = mouseWorld.y - handleWorld.y;
                isHovered = (std::abs(dx) < HANDLE_HIT_SIZE && std::abs(dy) < HANDLE_HIT_SIZE);
            }

            Engine::Math::Vec4 handleColor = (isActive || isHovered) ? HANDLE_HOVER_COLOR : HANDLE_COLOR;

            // Draw filled square handle
            Engine::Renderer::SubmitQuad(handleWorld, rotRad, {HANDLE_SIZE, HANDLE_SIZE}, handleColor, 0, GIZMO_Z + 1);
        };

        float halfW = collider.size.x / 2.0f;
        float halfH = collider.size.y / 2.0f;

        drawHandle(ColliderHandle::Left, {-halfW, 0});
        drawHandle(ColliderHandle::Right, {halfW, 0});
        drawHandle(ColliderHandle::Top, {0, halfH});
        drawHandle(ColliderHandle::Bottom, {0, -halfH});
    }

    void ColliderGizmo::DrawCircleCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation) {
        World* world = Engine::Core::GetCurrentWorld();
        if (!world) return;

        const auto& collider = world->GetComponent<Engine::ECS::Component::CircleColliderComponent>(entity);

        // Calculate collider center in world space (applying offset)
        float rotRad = worldRotation * static_cast<float>(M_PI) / 180.0f;
        float cosR = std::cos(rotRad);
        float sinR = std::sin(rotRad);

        Engine::Math::Vec2 offsetWorld;
        offsetWorld.x = collider.offset.x * cosR - collider.offset.y * sinR;
        offsetWorld.y = collider.offset.x * sinR + collider.offset.y * cosR;

        Engine::Math::Vec2 center = {
            worldPos.x + offsetWorld.x,
            worldPos.y + offsetWorld.y
        };

        // Choose color based on trigger state
        Engine::Math::Vec4 color = collider.isTrigger ? TRIGGER_COLOR : COLLIDER_COLOR;

        // Draw the circle outline
        Engine::Renderer::DrawCircle(center, collider.radius, color, 32, true, LINE_THICKNESS);

        // Get mouse position in world space
        Engine::Math::Vec2 mouseWorld = Engine::Renderer::ScreenToWorld(Engine::Input::GetMousePosition());

        // Draw radius handle (on the right side of the circle)
        Engine::Math::Vec2 handleWorld = {center.x + collider.radius, center.y};

        bool isActive = (s_ActiveEntity.id == entity.id && s_ActiveHandle == ColliderHandle::Radius);
        bool isHovered = IsOnRadiusHandle(center, collider.radius, mouseWorld);

        Engine::Math::Vec4 handleColor = (isActive || isHovered) ? HANDLE_HOVER_COLOR : HANDLE_COLOR;

        // Draw filled square handle at radius edge
        Engine::Renderer::SubmitQuad(handleWorld, 0, {HANDLE_SIZE, HANDLE_SIZE}, handleColor, 0, GIZMO_Z + 1);
    }

    ColliderHandle ColliderGizmo::GetBoxHandleAtMouse(
        const Engine::Math::Vec2& colliderCenter,
        const Engine::Math::Vec2& colliderSize,
        float rotation,
        const Engine::Math::Vec2& mouseWorld
    ) {
        float rotRad = rotation;
        float cosR = std::cos(rotRad);
        float sinR = std::sin(rotRad);

        // Transform mouse to local collider space
        Engine::Math::Vec2 localMouse;
        float dx = mouseWorld.x - colliderCenter.x;
        float dy = mouseWorld.y - colliderCenter.y;
        localMouse.x = dx * cosR + dy * sinR;
        localMouse.y = -dx * sinR + dy * cosR;

        float halfW = colliderSize.x / 2.0f;
        float halfH = colliderSize.y / 2.0f;

        // Check each handle
        if (std::abs(localMouse.x - (-halfW)) < HANDLE_HIT_SIZE && std::abs(localMouse.y) < HANDLE_HIT_SIZE)
            return ColliderHandle::Left;
        if (std::abs(localMouse.x - halfW) < HANDLE_HIT_SIZE && std::abs(localMouse.y) < HANDLE_HIT_SIZE)
            return ColliderHandle::Right;
        if (std::abs(localMouse.y - halfH) < HANDLE_HIT_SIZE && std::abs(localMouse.x) < HANDLE_HIT_SIZE)
            return ColliderHandle::Top;
        if (std::abs(localMouse.y - (-halfH)) < HANDLE_HIT_SIZE && std::abs(localMouse.x) < HANDLE_HIT_SIZE)
            return ColliderHandle::Bottom;

        return ColliderHandle::None;
    }

    bool ColliderGizmo::IsOnRadiusHandle(
        const Engine::Math::Vec2& colliderCenter,
        float radius,
        const Engine::Math::Vec2& mouseWorld
    ) {
        Engine::Math::Vec2 handlePos = {colliderCenter.x + radius, colliderCenter.y};
        float dx = mouseWorld.x - handlePos.x;
        float dy = mouseWorld.y - handlePos.y;
        return (std::abs(dx) < HANDLE_HIT_SIZE && std::abs(dy) < HANDLE_HIT_SIZE);
    }

    void ColliderGizmo::Update() {
        World* world = Engine::Core::GetCurrentWorld();
        if (!world) return;

        // Only update when not in play mode
        if (EditorWindows::EditorUi::isPlayMode) return;

        // Only handle if mouse is in viewport
        if (!EditorWindows::EditorUi::MouseInWindow("Viewport")) {
            if (s_ActiveHandle != ColliderHandle::None && !Engine::Input::IsMouseButtonPressed(0)) {
                s_ActiveHandle = ColliderHandle::None;
                s_ActiveEntity = NullEntityID();
            }
            return;
        }

        Engine::Math::Vec2 mouseWorld = Engine::Renderer::ScreenToWorld(Engine::Input::GetMousePosition());

        // Handle mouse button release
        if (!Engine::Input::IsMouseButtonPressed(0)) {
            if (s_ActiveHandle != ColliderHandle::None) {
                s_ActiveHandle = ColliderHandle::None;
                s_ActiveEntity = NullEntityID();
                EditorWindows::EditorUi::MarkWorldAsModified();
            }
            return;
        }

        // If we're already dragging, continue the drag
        if (s_ActiveHandle != ColliderHandle::None && s_ActiveEntity.IsValid()) {
            if (!world->HasComponent<Engine::ECS::Component::TransformComponent>(s_ActiveEntity))
                return;

            const auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(s_ActiveEntity);

            if (s_IsBoxCollider && world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(s_ActiveEntity)) {
                auto& collider = world->GetComponent<Engine::ECS::Component::BoxColliderComponent>(s_ActiveEntity);

                // Calculate delta in local collider space
                float rotRad = transform.worldRotation * static_cast<float>(M_PI) / 180.0f;
                float cosR = std::cos(rotRad);
                float sinR = std::sin(rotRad);

                Engine::Math::Vec2 delta = {mouseWorld.x - s_DragStart.x, mouseWorld.y - s_DragStart.y};

                // Transform delta to local space
                Engine::Math::Vec2 localDelta;
                localDelta.x = delta.x * cosR + delta.y * sinR;
                localDelta.y = -delta.x * sinR + delta.y * cosR;

                switch (s_ActiveHandle) {
                    case ColliderHandle::Left:
                        collider.size.x = std::max(0.1f, collider.size.x - localDelta.x);
                        collider.offset.x += localDelta.x * 0.5f;
                        break;
                    case ColliderHandle::Right:
                        collider.size.x = std::max(0.1f, collider.size.x + localDelta.x);
                        collider.offset.x += localDelta.x * 0.5f;
                        break;
                    case ColliderHandle::Top:
                        collider.size.y = std::max(0.1f, collider.size.y + localDelta.y);
                        collider.offset.y += localDelta.y * 0.5f;
                        break;
                    case ColliderHandle::Bottom:
                        collider.size.y = std::max(0.1f, collider.size.y - localDelta.y);
                        collider.offset.y += localDelta.y * 0.5f;
                        break;
                    default:
                        break;
                }

                s_DragStart = mouseWorld;
            }
            else if (!s_IsBoxCollider && world->HasComponent<Engine::ECS::Component::CircleColliderComponent>(s_ActiveEntity)) {
                auto& collider = world->GetComponent<Engine::ECS::Component::CircleColliderComponent>(s_ActiveEntity);

                // Calculate collider center
                float rotRad = transform.worldRotation * static_cast<float>(M_PI) / 180.0f;
                float cosR = std::cos(rotRad);
                float sinR = std::sin(rotRad);

                Engine::Math::Vec2 offsetWorld;
                offsetWorld.x = collider.offset.x * cosR - collider.offset.y * sinR;
                offsetWorld.y = collider.offset.x * sinR + collider.offset.y * cosR;

                Engine::Math::Vec2 center = {
                    transform.worldPosition.x + offsetWorld.x,
                    transform.worldPosition.y + offsetWorld.y
                };

                // Set radius based on distance from center to mouse
                float dx = mouseWorld.x - center.x;
                float dy = mouseWorld.y - center.y;
                collider.radius = std::max(0.1f, std::sqrt(dx * dx + dy * dy));
            }

            return;
        }

        // Check for new handle click
        if (Engine::Input::IsMouseButtonJustPressed(0)) {
            for (const EntityID& entity : EditorWindows::EditorUi::selectedEntities) {
                if (!entity.IsValid()) continue;

                if (!world->HasComponent<Engine::ECS::Component::TransformComponent>(entity))
                    continue;

                const auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(entity);

                // Check box collider handles
                if (world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(entity)) {
                    const auto& collider = world->GetComponent<Engine::ECS::Component::BoxColliderComponent>(entity);

                    float rotRad = transform.worldRotation * static_cast<float>(M_PI) / 180.0f;
                    float cosR = std::cos(rotRad);
                    float sinR = std::sin(rotRad);

                    Engine::Math::Vec2 offsetWorld;
                    offsetWorld.x = collider.offset.x * cosR - collider.offset.y * sinR;
                    offsetWorld.y = collider.offset.x * sinR + collider.offset.y * cosR;

                    Engine::Math::Vec2 center = {
                        transform.worldPosition.x + offsetWorld.x,
                        transform.worldPosition.y + offsetWorld.y
                    };

                    ColliderHandle handle = GetBoxHandleAtMouse(center, collider.size, rotRad, mouseWorld);
                    if (handle != ColliderHandle::None) {
                        s_ActiveHandle = handle;
                        s_ActiveEntity = entity;
                        s_DragStart = mouseWorld;
                        s_IsBoxCollider = true;
                        return;
                    }
                }

                // Check circle collider handles
                if (world->HasComponent<Engine::ECS::Component::CircleColliderComponent>(entity)) {
                    const auto& collider = world->GetComponent<Engine::ECS::Component::CircleColliderComponent>(entity);

                    float rotRad = transform.worldRotation * static_cast<float>(M_PI) / 180.0f;
                    float cosR = std::cos(rotRad);
                    float sinR = std::sin(rotRad);

                    Engine::Math::Vec2 offsetWorld;
                    offsetWorld.x = collider.offset.x * cosR - collider.offset.y * sinR;
                    offsetWorld.y = collider.offset.x * sinR + collider.offset.y * cosR;

                    Engine::Math::Vec2 center = {
                        transform.worldPosition.x + offsetWorld.x,
                        transform.worldPosition.y + offsetWorld.y
                    };

                    if (IsOnRadiusHandle(center, collider.radius, mouseWorld)) {
                        s_ActiveHandle = ColliderHandle::Radius;
                        s_ActiveEntity = entity;
                        s_DragStart = mouseWorld;
                        s_IsBoxCollider = false;
                        return;
                    }
                }
            }
        }
    }

} // namespace Editor
