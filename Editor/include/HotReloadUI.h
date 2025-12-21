// HotReloadUI.h - ImGui panel for hot-reload status and controls
#pragma once

#include "imgui.h"
#include <string>
#include <chrono>
#include <vector>

namespace Editor {

    // Log entry for hot-reload events
    struct HotReloadLogEntry {
        std::chrono::system_clock::time_point timestamp;
        std::string message;
        bool isError;
    };

    class HotReloadUI {
    public:
        // Draw the hot-reload panel
        static void Draw();

        // Add a log message
        static void Log(const std::string& message, bool isError = false);

        // Clear the log
        static void ClearLog();

        // Set visibility
        static void SetVisible(bool visible) { s_IsVisible = visible; }
        static bool IsVisible() { return s_IsVisible; }

    private:
        static void DrawStatusSection();
        static void DrawControlsSection();
        static void DrawLogSection();

        static bool s_IsVisible;
        static std::vector<HotReloadLogEntry> s_Log;
        static bool s_AutoScroll;
    };

} // namespace Editor
