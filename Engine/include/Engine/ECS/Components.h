#pragma once
#include "../ECS/Component.h"
#include "../Renderer.h" // For Engine::Vec4

// --- Core Data Structures ---

// 1. Position Component
struct PositionComponent {
    Engine::Math::Vec2 position = {0.0f, 0.0f};
    float rotation = 0.0f; // Angle in degrees
};

// 2. Sprite Component
// This component marks an entity as renderable and holds its visual properties.
struct SpriteComponent {
    // NOTE: This textureID must be the OpenGL handle for the texture (loaded in Renderer::Init)
    uint32_t textureID = 0; 
    Engine::Vec2 size = {1.0f, 1.0f}; // Width and Height in world units
    Engine::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // Tint (RGBA)
};

// 3. Camera Component
// An entity with this component will act as the viewport camera.
struct CameraComponent {
    // Orthographic projection settings
    float zoom = 1.0f;      // 1.0 = normal view
    float halfHeight = 10.0f; // Half the height of the view volume in world units.
    Engine::Vec4 backgroundColor = {0.1f, 0.1f, 0.1f, 1.0f};
    // Optional: Only render the scene if this camera is active.
    bool isActive = true; 
};

//TAGS
struct ViewportCameraTag {
    float cameraSpeed = 5;
};