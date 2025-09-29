#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include <EventSystem/Events.h>

#include "imgui.h"
#include "Log.h" // For LogLevel
#include "Math.h"

// Forward declarations
namespace Engine {
    class World; // If needed later for system integration
}

class EditorUi final {
public:
    static std::filesystem::path projectDir;
    static Engine::Math::Vec2 viewportMousePos;
    static Engine::Math::Vec2 viewportSize;

    static void DrawWindowUi();

    // Event callback for the LogEvent
    static void TestGetLogEvent(const Engine::LogEvent& e);

    // NEW: Function to initiate a manual project build
    static void BuildProject();

    static inline Engine::Math::Vec2 FromImVec2(const ImVec2& v) {
        return { v.x, v.y };
    }

    static Engine::Math::Vec2 GetMousePositionInWindow(const std::string &windowName);
};
