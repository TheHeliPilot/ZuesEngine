#pragma once
#include "../ECS/Component.h"
#include "../../json/json.hpp"
#include "../Math.h"

// You must include the json header here or ensure it's included higher up
// #include "../../json/json.hpp" // Uncomment if WorldSerializationHelpers.h doesn't cover this

// --- Core Data Structures ---

// 1. Position Component
struct PositionComponent {
    Engine::Math::Vec2 position = {0.0f, 0.0f};
    float rotation = 0.0f; // Angle in degrees
};
// Add serialization for PositionComponent
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PositionComponent, position, rotation)


// 2. Sprite Component
struct SpriteComponent {
    uint32_t textureID = 0;
    Engine::Math::Vec2 size = {1.0f, 1.0f};
    Engine::Math::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};
// Add serialization for SpriteComponent (Fixes your compilation error for this type)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SpriteComponent, textureID, size, color)


// 3. Camera Component
struct CameraComponent {
    float zoom = 1.0f;
    float halfHeight = 10.0f;
    Engine::Math::Vec4 backgroundColor = {0.1f, 0.1f, 0.1f, 1.0f};
    bool isActive = true;
};
// Add serialization for CameraComponent (Fixes your compilation error for this type)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraComponent, zoom, halfHeight, backgroundColor, isActive)


// TAGS
struct ViewportCameraTag {
    float cameraSpeed = 5;
};
// Add serialization for ViewportCameraTag (Fixes your compilation error for this type)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ViewportCameraTag, cameraSpeed)