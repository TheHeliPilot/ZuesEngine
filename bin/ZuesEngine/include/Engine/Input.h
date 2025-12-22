#pragma once

#include "ZuesAPI.h"

#include <GLFW/glfw3.h>
#include <map>
#include <utility>

namespace Engine { namespace Math { struct Vec2; } } // Forward declarations

namespace Engine {

    class ZUES_API Input final {
    public:
        // --- Window Setup ---
        // Must be called once during initialization to set the GLFW window
        static void SetWindow(GLFWwindow* window);
        static GLFWwindow* GetWindow();

        // --- Core Lifecycle ---
        // Must be called once per frame BEFORE systems run (typically in main.cpp)
        static void UpdateState();

        // --- Keyboard Functions ---

        // Equivalent to Unity's Input.GetKey(KeyCode)
        static bool IsKeyPressed(int keycode); // Key is currently held down

        // Equivalent to Unity's Input.GetKeyDown(KeyCode)
        static bool IsKeyJustPressed(int keycode); // Key was pressed this frame

        // Equivalent to Unity's Input.GetKeyUp(KeyCode)
        static bool IsKeyJustReleased(int keycode); // Key was released this frame

        // --- Mouse Functions ---

        // Equivalent to Unity's Input.GetMouseButton(Button)
        static bool IsMouseButtonPressed(int button); // Button is currently held down

        // Equivalent to Unity's Input.GetMouseButtonDown(Button)
        static bool IsMouseButtonJustPressed(int button); // Button was pressed this frame

        // Equivalent to Unity's Input.GetMouseButtonUp(Button)
        static bool IsMouseButtonJustReleased(int button); // Button was released this frame

        // --- Cursor Position ---
        static Engine::Math::Vec2 GetMousePosition();

    private:
        static GLFWwindow* s_Window;

        // Caching the state for one-shot checks
        static std::map<int, bool> m_CurrentKeyState;
        static std::map<int, bool> m_PreviousKeyState;
        static std::map<int, bool> m_CurrentMouseState;
        static std::map<int, bool> m_PreviousMouseState;

        // Internal helper for GLFW polling (using a wider range than A-Z)
        static void PollKeys();
        static void PollMouseButtons();
    };
}