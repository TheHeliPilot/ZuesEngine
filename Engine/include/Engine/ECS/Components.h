#pragma once
#include "../ECS/Component.h"
#include "../../json/json.hpp"
#include "../Math.h"

// You must include the json header here or ensure it's included higher up
// #include "../../json/json.hpp" // Uncomment if WorldSerializationHelpers.h doesn't cover this

namespace nlohmann {
    template <>
    struct adl_serializer<EntityID> {
        // Serialization (from C++ to JSON)
        static void to_json(json& j, const EntityID& entityId) {
            // Store as the underlying integer value (assuming uint64_t for safety)
            j = entityId.id;
        }

        // Deserialization (from JSON to C++)
        static void from_json(const json& j, EntityID& entityId) {
            // Read the JSON value into a standard integer type and cast back
            entityId.id = j.get<uint64_t>();
        }
    };
}

namespace Engine::ECS::Component {

    struct TransformComponent {
        Math::Vec2 worldPosition = {0.0f, 0.0f};
        float worldRotation = 0.0f; // Angle in degrees
        Math::Vec2 localPosition = {0.0f, 0.0f};
        float localRotation = 0.0f; //

        EntityID parent = NULL_ENTITY_ID;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TransformComponent, worldPosition, worldRotation, localPosition, localRotation, parent)

    // 2. Sprite Component
    struct SpriteComponent {
        uint32_t textureID = 0;
        Math::Vec2 size = {1.0f, 1.0f};
        Math::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        int layer = 0;      // Primary sorting key (e.g., Background=0, Default=1, UI=2)
        int sortOrder = 0;  // Secondary sorting key within a layer (for elements on the same layer)
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SpriteComponent, textureID, size, color, layer, sortOrder)

    // 3. Camera Component
    struct CameraComponent {
        float zoom = 1.0f;
        float halfHeight = 10.0f;
        Math::Vec4 backgroundColor = {0.1f, 0.1f, 0.1f, 1.0f};
        bool isActive = false;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraComponent, zoom, halfHeight, backgroundColor, isActive)

    // TAGS
    struct MainCameraTag {
        bool bullshit = true;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MainCameraTag, bullshit)

    struct ViewportCameraTag {
        float cameraSpeed = 5;
    };
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ViewportCameraTag, cameraSpeed);

    struct TestObjectMoverTag{bool move = true;};
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TestObjectMoverTag, move);
}