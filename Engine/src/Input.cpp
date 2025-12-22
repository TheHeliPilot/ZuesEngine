#include "../include/Engine/Input.h"
#include "../include/Engine/ZuesMath.h" // For Vec2 and GetMousePosition

namespace Engine {

    // Initialize static members
    GLFWwindow* Input::s_Window = nullptr;
    std::map<int, bool> Input::m_CurrentKeyState;
    std::map<int, bool> Input::m_PreviousKeyState;
    std::map<int, bool> Input::m_CurrentMouseState;
    std::map<int, bool> Input::m_PreviousMouseState;

    // --- Window Setup ---

    void Input::SetWindow(GLFWwindow* window) {
        s_Window = window;
    }

    GLFWwindow* Input::GetWindow() {
        return s_Window;
    }

    // --- Internal Polling Helpers ---

    void Input::PollKeys() {
        if (!s_Window) return;

        // Copy current state to previous state cache
        m_PreviousKeyState = m_CurrentKeyState;
        m_CurrentKeyState.clear();

        // This is a common range for GLFW keys (e.g., A=65, Space=32)
        // Adjust the range (32 to 348) if you need a specific set of keys
        for (int key = 32; key <= 348; ++key) {
            // glfwGetKey returns GLFW_PRESS or GLFW_RELEASE
            if (const bool isDown = glfwGetKey(s_Window, key) == GLFW_PRESS; isDown || m_PreviousKeyState.contains(key)) {
                // Only store keys that are currently pressed or were pressed last frame
                m_CurrentKeyState[key] = isDown;
            }
        }
    }

    void Input::PollMouseButtons() {
        if (!s_Window) return;

        m_PreviousMouseState = m_CurrentMouseState;
        m_CurrentMouseState.clear();

        for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_8; ++button) {
            if (const bool isDown = glfwGetMouseButton(s_Window, button) == GLFW_PRESS; isDown || m_PreviousMouseState.contains(button)) {
                 m_CurrentMouseState[button] = isDown;
            }
        }
    }

    // --- Public API Implementations ---

    void Input::UpdateState() {
        PollKeys();
        PollMouseButtons();
    }

    // --- Keyboard Queries ---

    bool Input::IsKeyPressed(const int keycode) {
        return m_CurrentKeyState.contains(keycode) && m_CurrentKeyState.at(keycode);
    }

    bool Input::IsKeyJustPressed(const int keycode) {
        const bool current = IsKeyPressed(keycode);
        const bool previous = m_PreviousKeyState.contains(keycode) && m_PreviousKeyState.at(keycode);
        // Pressed = Current is true AND Previous was false
        return current && !previous;
    }

    bool Input::IsKeyJustReleased(const int keycode) {
        const bool current = IsKeyPressed(keycode);
        const bool previous = m_PreviousKeyState.contains(keycode) && m_PreviousKeyState.at(keycode);
        // Released = Current is false AND Previous was true
        return !current && previous;
    }

    // --- Mouse Queries ---

    bool Input::IsMouseButtonPressed(const int button) {
        return (m_CurrentMouseState.contains(button) && m_CurrentMouseState.at(button));
    }

    bool Input::IsMouseButtonJustPressed(const int button) {
        const bool current = IsMouseButtonPressed(button);
        const bool previous = m_PreviousMouseState.contains(button) && m_PreviousMouseState.at(button);
        return current && !previous;
    }

    bool Input::IsMouseButtonJustReleased(const int button) {
        const bool current = IsMouseButtonPressed(button);
        const bool previous = m_PreviousMouseState.contains(button) && m_PreviousMouseState.at(button);
        return !current && previous;
    }

    // --- Cursor Position ---

    Engine::Math::Vec2 Input::GetMousePosition() {
        if (!s_Window) return {0.0f, 0.0f};
        double x, y;
        glfwGetCursorPos(s_Window, &x, &y);
        // Note: The coordinates are screen-space pixel coordinates relative to the window.
        return {static_cast<float>(x), static_cast<float>(y)};
    }
}
