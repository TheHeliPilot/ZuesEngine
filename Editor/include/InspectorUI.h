#pragma once
#include <string>
#include <algorithm>
#include "Engine.h"

namespace EditorWindows {
    class InspectorUI final {
    public:
        static void InspectorWindow();

    private:
        static bool DrawJsonComponentEditor(const char* name, nlohmann::json& j, int componentTypeID, bool isSpriteComponent = false);
        static bool DrawJsonField(const char *label, nlohmann::json &value, bool isSpriteComponent);
        static bool DrawSpriteField(const char* label, nlohmann::json& spriteNameValue); // Changed from textureIDValue
    };
}