#pragma once
#include "../ECS/Component.h"
#include "../../json/json.hpp"
#include "../Math.h"

// You must include the json header here or ensure it's included higher up
// #include "../../json/json.hpp" // Uncomment if WorldSerializationHelpers.h doesn't cover this

// --- Core Data Structures ---
namespace Engine::ECS::Component {
    // 1. Position Component
    struct TransformComponent {
        Engine::Math::Vec2 worldPosition = {0.0f, 0.0f};
        float worldRotation = 0.0f; // Angle in degrees
        Engine::Math::Vec2 localPosition = {0.0f, 0.0f};
        float localRotation = 0.0f; //

        EntityID parent = NULL_ENTITY_ID;
    };
    // Add serialization for PositionComponent
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TransformComponent, worldPosition, worldRotation)


    // 2. Sprite Component
    struct SpriteComponent {
        uint32_t textureID = 0;
        Engine::Math::Vec2 size = {1.0f, 1.0f};
        Engine::Math::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        // NEW: Fields for Z-Ordering
        int layer = 0;      // Primary sorting key (e.g., Background=0, Default=1, UI=2)
        int sortOrder = 0;  // Secondary sorting key within a layer (for elements on the same layer)
    };
    // Update serialization for SpriteComponent
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SpriteComponent, textureID, size, color, layer, sortOrder)


    // 3. Camera Component
    struct CameraComponent {
        float zoom = 1.0f;
        float halfHeight = 10.0f;
        Engine::Math::Vec4 backgroundColor = {0.1f, 0.1f, 0.1f, 1.0f};
        bool isActive = false;
    };
    // Add serialization for CameraComponent
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraComponent, zoom, halfHeight, backgroundColor, isActive)


    // TAGS
    struct ViewportCameraTag {
        float cameraSpeed = 5;
    };
    // Add serialization for ViewportCameraTag (Fixes your compilation error for this type)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ViewportCameraTag, cameraSpeed)

    struct TestObjectMoverTag{bool move = true;};
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TestObjectMoverTag, move);
}