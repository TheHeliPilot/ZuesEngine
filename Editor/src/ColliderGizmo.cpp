#include "../include/ColliderGizmo.h"
#include "../include/EditorUi.h"
#include "Core.h"
#include "Renderer.h"
#include "Debug.h"
#include "Input.h"
#include "ECS/Components.h"
#include <cmath>

namespace Editor {

    // Static member initialization
    ColliderHandle ColliderGizmo::s_ActiveHandle = ColliderHandle::None;
    EntityID ColliderGizmo::s_ActiveEntity = NullEntityID();
    Engine::Math::Vec2 ColliderGizmo::s_DragStart = {0, 0};
    bool ColliderGizmo::s_IsBoxCollider = true;

    // Constants for gizmo appearance (in screen pixels)
    static constexpr float HANDLE_SCREEN_SIZE = 6.0f;      // Handle size in screen pixels
    static constexpr float HANDLE_HIT_SCREEN_SIZE = 10.0f; // Hitbox size in screen pixels
    static constexpr float LINE_SCREEN_THICKNESS = 1.5f;   // Line thickness in screen pixels
    static constexpr float GIZMO_Z = 100.0f;               // Draw on top

    // Colors - Clean, professional style
    static const Engine::Math::Vec4 COLLIDER_COLOR = {0.0f, 0.9f, 0.4f, 0.8f};           // Bright green
    static const Engine::Math::Vec4 COLLIDER_EDIT_COLOR = {0.0f, 1.0f, 0.5f, 1.0f};      // Brighter green in edit mode
    static const Engine::Math::Vec4 TRIGGER_COLOR = {0.2f, 0.6f, 1.0f, 0.8f};            // Blue for triggers
    static const Engine::Math::Vec4 TRIGGER_EDIT_COLOR = {0.3f, 0.7f, 1.0f, 1.0f};       // Brighter blue in edit mode
    static const Engine::Math::Vec4 HANDLE_COLOR = {1.0f, 1.0f, 1.0f, 1.0f};             // White handles
    static const Engine::Math::Vec4 HANDLE_HOVER_COLOR = {0.0f, 1.0f, 0.5f, 1.0f};       // Green when hovered
    static const Engine::Math::Vec4 HANDLE_ACTIVE_COLOR = {1.0f, 0.9f, 0.0f, 1.0f};      // Yellow when dragging
    static const Engine::Math::Vec4 HANDLE_OUTLINE_COLOR = {0.0f, 0.0f, 0.0f, 1.0f};     // Black outline

    // Helper: use centralized Renderer function
    static float ScreenToWorldSize(float screenPixels) {
        return Engine::Renderer::ScreenToWorldSize(screenPixels);
    }

    // Helper function to draw a rotated rectangle outline (4 lines)
    static void DrawRectOutline(const Engine::Math::Vec2& center, const Engine::Math::Vec2& size,
                                 const Engine::Math::Vec4& color, float rotRad) {
        float cosR = std::cos(rotRad);
        float sinR = std::sin(rotRad);
        float halfW = size.x / 2.0f;
        float halfH = size.y / 2.0f;
        float thickness = ScreenToWorldSize(LINE_SCREEN_THICKNESS);

        auto rotatePoint = [&](float lx, float ly) -> Engine::Math::Vec2 {
            return {
                center.x + lx * cosR - ly * sinR,
                center.y + lx * sinR + ly * cosR
            };
        };

        Engine::Math::Vec2 tl = rotatePoint(-halfW, halfH);
        Engine::Math::Vec2 tr = rotatePoint(halfW, halfH);
        Engine::Math::Vec2 br = rotatePoint(halfW, -halfH);
        Engine::Math::Vec2 bl = rotatePoint(-halfW, -halfH);

        Engine::Renderer::DrawLine(tl, tr, color, thickness);
        Engine::Renderer::DrawLine(tr, br, color, thickness);
        Engine::Renderer::DrawLine(br, bl, color, thickness);
        Engine::Renderer::DrawLine(bl, tl, color, thickness);
    }

    void ColliderGizmo::Draw() {
        World* world = Engine::Core::GetCurrentWorld();
        if (!world) return;

        // Only draw when not in play mode
        if (EditorWindows::EditorUi::isPlayMode) return;

        // Check if we're in edit mode
        bool editModeActive = IsColliderEditMode();
        EntityID editingEntity = GetEditingColliderEntity();

        // Draw for all selected entities that have colliders
        for (const EntityID& entity : EditorWindows::EditorUi::selectedEntities) {
            if (!entity.IsValid()) continue;

            // Get transform
            if (!world->HasComponent<Engine::ECS::Component::TransformComponent>(entity))
                continue;

            const auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(entity);

            // Determine if this entity is in edit mode
            bool isThisEntityEditing = (editModeActive && editingEntity.id == entity.id);

            // Check for box collider
            if (world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(entity)) {
                bool showEditHandles = isThisEntityEditing && IsEditingBoxCollider();
                DrawBoxCollider(entity, transform.worldPosition, transform.worldRotation, showEditHandles);
            }

            // Check for circle collider
            if (world->HasComponent<Engine::ECS::Component::CircleColliderComponent>(entity)) {
                bool showEditHandles = isThisEntityEditing && !IsEditingBoxCollider();
                DrawCircleCollider(entity, transform.worldPosition, transform.worldRotation, showEditHandles);
            }
        }
    }

    void ColliderGizmo::DrawBoxCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation, bool isEditMode) {
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

        // Choose color based on trigger state and edit mode
        Engine::Math::Vec4 color;
        if (isEditMode) {
            color = collider.isTrigger ? TRIGGER_EDIT_COLOR : COLLIDER_EDIT_COLOR;
        } else {
            color = collider.isTrigger ? TRIGGER_COLOR : COLLIDER_COLOR;
        }

        // Draw the collider outline (edges only)
        DrawRectOutline(center, collider.size, color, rotRad);

        // Only draw handles in edit mode
        if (!isEditMode) return;

        // Get mouse position in world space
        Engine::Math::Vec2 mouseWorld = Engine::Renderer::ScreenToWorld(Engine::Input::GetMousePosition());

        float halfW = collider.size.x / 2.0f;
        float halfH = collider.size.y / 2.0f;

        // Calculate world-space handle sizes (constant screen size regardless of zoom)
        float handleSize = ScreenToWorldSize(HANDLE_SCREEN_SIZE);
        float handleHitSize = ScreenToWorldSize(HANDLE_HIT_SCREEN_SIZE);

        // Draw handles (small squares on edge midpoints)
        auto drawHandle = [&](ColliderHandle handle, Engine::Math::Vec2 localPos) {
            // Transform local position to world
            Engine::Math::Vec2 handleWorld;
            handleWorld.x = center.x + localPos.x * cosR - localPos.y * sinR;
            handleWorld.y = center.y + localPos.x * sinR + localPos.y * cosR;

            // Check if this handle is being hovered or is active
            bool isActive = (s_ActiveEntity.id == entity.id && s_ActiveHandle == handle);
            bool isHovered = false;

            if (s_ActiveHandle == ColliderHandle::None) {
                // Check hover with screen-space hit size
                float dx = mouseWorld.x - handleWorld.x;
                float dy = mouseWorld.y - handleWorld.y;
                isHovered = (std::abs(dx) < handleHitSize && std::abs(dy) < handleHitSize);
            }

            Engine::Math::Vec4 handleColor;
            if (isActive) {
                handleColor = HANDLE_ACTIVE_COLOR;
            } else if (isHovered) {
                handleColor = HANDLE_HOVER_COLOR;
            } else {
                handleColor = HANDLE_COLOR;
            }

            // Draw handle outline (slightly larger black square behind)
            float outlineSize = handleSize * 1.3f;
            Engine::Renderer::SubmitQuad(handleWorld, rotRad, {outlineSize, outlineSize}, HANDLE_OUTLINE_COLOR, 0, GIZMO_Z + 0.5f);
            // Draw filled square handle
            Engine::Renderer::SubmitQuad(handleWorld, rotRad, {handleSize, handleSize}, handleColor, 0, GIZMO_Z + 1);
        };

        drawHandle(ColliderHandle::Left, {-halfW, 0});
        drawHandle(ColliderHandle::Right, {halfW, 0});
        drawHandle(ColliderHandle::Top, {0, halfH});
        drawHandle(ColliderHandle::Bottom, {0, -halfH});
    }

    void ColliderGizmo::DrawCircleCollider(EntityID entity, const Engine::Math::Vec2& worldPos, float worldRotation, bool isEditMode) {
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

        // Choose color based on trigger state and edit mode
        Engine::Math::Vec4 color;
        if (isEditMode) {
            color = collider.isTrigger ? TRIGGER_EDIT_COLOR : COLLIDER_EDIT_COLOR;
        } else {
            color = collider.isTrigger ? TRIGGER_COLOR : COLLIDER_COLOR;
        }

        // Draw the circle outline (convert screen thickness to world)
        float thickness = ScreenToWorldSize(LINE_SCREEN_THICKNESS);
        Engine::Renderer::DrawCircle(center, collider.radius, color, 32, true, thickness);

        // Only draw handles in edit mode
        if (!isEditMode) return;

        // Get mouse position in world space
        Engine::Math::Vec2 mouseWorld = Engine::Renderer::ScreenToWorld(Engine::Input::GetMousePosition());

        // Calculate world-space handle sizes (constant screen size regardless of zoom)
        float handleSize = ScreenToWorldSize(HANDLE_SCREEN_SIZE);
        float handleHitSize = ScreenToWorldSize(HANDLE_HIT_SCREEN_SIZE);
        float centerHandleSize = handleSize * 1.2f; // Slightly larger center handle

        // Calculate radius handle position (closest point on circle to mouse)
        float dx = mouseWorld.x - center.x;
        float dy = mouseWorld.y - center.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        Engine::Math::Vec2 radiusHandlePos;
        if (dist > 0.001f) {
            // Normalize direction and place handle on circle edge
            radiusHandlePos = {
                center.x + (dx / dist) * collider.radius,
                center.y + (dy / dist) * collider.radius
            };
        } else {
            // Default to right side if mouse is at center
            radiusHandlePos = {center.x + collider.radius, center.y};
        }

        // Check handle states
        bool radiusActive = (s_ActiveEntity.id == entity.id && s_ActiveHandle == ColliderHandle::Radius);
        bool centerActive = (s_ActiveEntity.id == entity.id && s_ActiveHandle == ColliderHandle::Center);

        bool radiusHovered = false;
        bool centerHovered = false;

        if (s_ActiveHandle == ColliderHandle::None) {
            // Check radius hover
            float rdx = mouseWorld.x - radiusHandlePos.x;
            float rdy = mouseWorld.y - radiusHandlePos.y;
            radiusHovered = (std::abs(rdx) < handleHitSize && std::abs(rdy) < handleHitSize);

            // Check center hover
            float cdx = mouseWorld.x - center.x;
            float cdy = mouseWorld.y - center.y;
            centerHovered = (std::abs(cdx) < handleHitSize && std::abs(cdy) < handleHitSize);
        }

        // Draw center handle (for moving offset)
        {
            Engine::Math::Vec4 handleColor;
            if (centerActive) {
                handleColor = HANDLE_ACTIVE_COLOR;
            } else if (centerHovered) {
                handleColor = HANDLE_HOVER_COLOR;
            } else {
                handleColor = HANDLE_COLOR;
            }

            // Draw handle outline
            float outlineSize = centerHandleSize * 1.3f;
            Engine::Renderer::DrawCircle(center, outlineSize, HANDLE_OUTLINE_COLOR, 12, false, 0);
            // Draw filled circle handle
            Engine::Renderer::DrawCircle(center, centerHandleSize, handleColor, 12, false, 0);
        }

        // Draw radius handle (small circle on the edge)
        {
            Engine::Math::Vec4 handleColor;
            if (radiusActive) {
                handleColor = HANDLE_ACTIVE_COLOR;
            } else if (radiusHovered) {
                handleColor = HANDLE_HOVER_COLOR;
            } else {
                handleColor = HANDLE_COLOR;
            }

            // Draw handle outline
            float outlineSize = handleSize * 1.3f;
            Engine::Renderer::DrawCircle(radiusHandlePos, outlineSize, HANDLE_OUTLINE_COLOR, 12, false, 0);
            // Draw filled circle handle
            Engine::Renderer::DrawCircle(radiusHandlePos, handleSize, handleColor, 12, false, 0);
        }
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

        // Use screen-relative hit size
        float hitSize = ScreenToWorldSize(HANDLE_HIT_SCREEN_SIZE);

        // Check each handle
        if (std::abs(localMouse.x - (-halfW)) < hitSize && std::abs(localMouse.y) < hitSize)
            return ColliderHandle::Left;
        if (std::abs(localMouse.x - halfW) < hitSize && std::abs(localMouse.y) < hitSize)
            return ColliderHandle::Right;
        if (std::abs(localMouse.y - halfH) < hitSize && std::abs(localMouse.x) < hitSize)
            return ColliderHandle::Top;
        if (std::abs(localMouse.y - (-halfH)) < hitSize && std::abs(localMouse.x) < hitSize)
            return ColliderHandle::Bottom;

        return ColliderHandle::None;
    }

    ColliderHandle ColliderGizmo::GetCircleHandleAtMouse(
        const Engine::Math::Vec2& colliderCenter,
        float radius,
        const Engine::Math::Vec2& mouseWorld
    ) {
        float dx = mouseWorld.x - colliderCenter.x;
        float dy = mouseWorld.y - colliderCenter.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        // Use screen-relative hit size
        float hitSize = ScreenToWorldSize(HANDLE_HIT_SCREEN_SIZE);

        // Check center handle first (higher priority)
        if (std::abs(dx) < hitSize && std::abs(dy) < hitSize) {
            return ColliderHandle::Center;
        }

        // Check radius handle (on the circle edge, closest to mouse)
        if (std::abs(dist - radius) < hitSize) {
            return ColliderHandle::Radius;
        }

        return ColliderHandle::None;
    }

    void ColliderGizmo::Update() {
        World* world = Engine::Core::GetCurrentWorld();
        if (!world) return;

        // Only update when not in play mode
        if (EditorWindows::EditorUi::isPlayMode) return;

        // Only process if in edit mode
        if (!IsColliderEditMode()) {
            s_ActiveHandle = ColliderHandle::None;
            s_ActiveEntity = NullEntityID();
            return;
        }

        EntityID editingEntity = GetEditingColliderEntity();
        if (!editingEntity.IsValid()) return;

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

                // Calculate collider center in world
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

                if (s_ActiveHandle == ColliderHandle::Radius) {
                    // Set radius based on distance from center to mouse
                    float dx = mouseWorld.x - center.x;
                    float dy = mouseWorld.y - center.y;
                    collider.radius = std::max(0.1f, std::sqrt(dx * dx + dy * dy));
                }
                else if (s_ActiveHandle == ColliderHandle::Center) {
                    // Move offset based on delta
                    Engine::Math::Vec2 delta = {mouseWorld.x - s_DragStart.x, mouseWorld.y - s_DragStart.y};

                    // Transform delta back to local space
                    Engine::Math::Vec2 localDelta;
                    localDelta.x = delta.x * cosR + delta.y * sinR;
                    localDelta.y = -delta.x * sinR + delta.y * cosR;

                    collider.offset.x += localDelta.x;
                    collider.offset.y += localDelta.y;

                    s_DragStart = mouseWorld;
                }
            }

            return;
        }

        // Check for new handle click - only on the editing entity
        if (Engine::Input::IsMouseButtonJustPressed(0)) {
            if (!world->HasComponent<Engine::ECS::Component::TransformComponent>(editingEntity))
                return;

            const auto& transform = world->GetComponent<Engine::ECS::Component::TransformComponent>(editingEntity);

            if (IsEditingBoxCollider()) {
                // Check box collider handles
                if (world->HasComponent<Engine::ECS::Component::BoxColliderComponent>(editingEntity)) {
                    const auto& collider = world->GetComponent<Engine::ECS::Component::BoxColliderComponent>(editingEntity);

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
                        s_ActiveEntity = editingEntity;
                        s_DragStart = mouseWorld;
                        s_IsBoxCollider = true;
                    }
                }
            } else {
                // Check circle collider handles
                if (world->HasComponent<Engine::ECS::Component::CircleColliderComponent>(editingEntity)) {
                    const auto& collider = world->GetComponent<Engine::ECS::Component::CircleColliderComponent>(editingEntity);

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

                    ColliderHandle handle = GetCircleHandleAtMouse(center, collider.radius, mouseWorld);
                    if (handle != ColliderHandle::None) {
                        s_ActiveHandle = handle;
                        s_ActiveEntity = editingEntity;
                        s_DragStart = mouseWorld;
                        s_IsBoxCollider = false;
                    }
                }
            }
        }
    }

} // namespace Editor
