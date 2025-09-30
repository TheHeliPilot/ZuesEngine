//
// Created by kukko on 29. 9. 2025.
//

#pragma once
#include "imgui.h"
#include "EventSystem/Events.h"

namespace EditorWindows
{
    class LoggerUI final
    {
    public:
        // Logger Window function
        static void LoggerWindow();

        static ImVec4 HexToImVec4(const char *hex);

        // Event callback for the LogEvent
        static void GetLogEvent(const Engine::LogEvent& e);
    };
}
