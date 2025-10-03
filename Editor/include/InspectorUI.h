#pragma once
#include <string>
#include <algorithm>
#include <Engine.h>

namespace EditorWindows {
    class InspectorUI final {
    public:
        static void InspectorWindow();

    private:
        static bool DrawJsonComponentEditor(const char* name, nlohmann::json& j);
        static bool DrawJsonField(const char* label, nlohmann::json& value);
    };
}