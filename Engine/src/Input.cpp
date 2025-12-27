#include "../include/Engine/Input.h"
#include "../include/Engine/ZuesMath.h"

namespace Engine {

    // Initialize static members
    std::map<int, bool> Input::m_CurrentKeyState;
    std::map<int, bool> Input::m_PreviousKeyState;
    std::map<int, bool> Input::m_CurrentMouseState;
    std::map<int, bool> Input::m_PreviousMouseState;
    float Input::m_MouseX = 0.0f;
    float Input::m_MouseY = 0.0f;
    float Input::m_ScrollDelta = 0.0f;
    float Input::m_ScrollAccumulator = 0.0f;

    // --- Frame Lifecycle ---

    void Input::BeginFrame() {
        // Move current state to previous
        m_PreviousKeyState = m_CurrentKeyState;
        m_PreviousMouseState = m_CurrentMouseState;

        // Finalize scroll delta from accumulator
        m_ScrollDelta = m_ScrollAccumulator;
        m_ScrollAccumulator = 0.0f;
    }

    // --- State Injection ---

    void Input::SetMouseButtonState(int button, bool pressed) {
        m_CurrentMouseState[button] = pressed;
    }

    void Input::SetKeyState(int keycode, bool pressed) {
        m_CurrentKeyState[keycode] = pressed;
    }

    void Input::SetMousePosition(float x, float y) {
        m_MouseX = x;
        m_MouseY = y;
    }

    void Input::AddScrollDelta(float delta) {
        m_ScrollAccumulator += delta;
    }

    // --- Keyboard Queries ---

    bool Input::IsKeyPressed(int keycode) {
        auto it = m_CurrentKeyState.find(keycode);
        return it != m_CurrentKeyState.end() && it->second;
    }

    bool Input::IsKeyJustPressed(int keycode) {
        bool current = IsKeyPressed(keycode);
        auto it = m_PreviousKeyState.find(keycode);
        bool previous = it != m_PreviousKeyState.end() && it->second;
        return current && !previous;
    }

    bool Input::IsKeyJustReleased(int keycode) {
        bool current = IsKeyPressed(keycode);
        auto it = m_PreviousKeyState.find(keycode);
        bool previous = it != m_PreviousKeyState.end() && it->second;
        return !current && previous;
    }

    // --- Mouse Queries ---

    bool Input::IsMouseButtonPressed(int button) {
        auto it = m_CurrentMouseState.find(button);
        return it != m_CurrentMouseState.end() && it->second;
    }

    bool Input::IsMouseButtonJustPressed(int button) {
        bool current = IsMouseButtonPressed(button);
        auto it = m_PreviousMouseState.find(button);
        bool previous = it != m_PreviousMouseState.end() && it->second;
        return current && !previous;
    }

    bool Input::IsMouseButtonJustReleased(int button) {
        bool current = IsMouseButtonPressed(button);
        auto it = m_PreviousMouseState.find(button);
        bool previous = it != m_PreviousMouseState.end() && it->second;
        return !current && previous;
    }

    // --- Cursor Position ---

    Math::Vec2 Input::GetMousePosition() {
        return {m_MouseX, m_MouseY};
    }

    // --- Mouse Scroll ---

    float Input::GetMouseScrollDelta() {
        return m_ScrollDelta;
    }
}
