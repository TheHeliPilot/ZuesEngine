#pragma once
#include <string>
#include <vector>
#include <functional>
#include <EventSystem/Events.h>
#include "Log.h" // For LogLevel

// Forward declarations
namespace Engine {
    class World; // If needed later for system integration
}

class EditorUi final {
public:
    static void DrawWindowUi();

    // Event callback for the LogEvent
    static void TestGetLogEvent(const Engine::LogEvent& e);

    // NEW: Function to initiate a manual project build
    static void BuildProject();

};
