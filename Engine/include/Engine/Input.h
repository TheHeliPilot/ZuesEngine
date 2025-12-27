#pragma once

#include "ZuesAPI.h"
#include <map>

namespace Engine { namespace Math { struct Vec2; } }

namespace Engine {

    /**
     * Input system for the Engine.
     *
     * IMPORTANT: This class does NOT poll GLFW directly.
     * The host application (Editor or Game EXE) must call the Set* methods
     * to inject input state each frame. This is necessary because GLFW state
     * doesn't share properly across DLL boundaries on Windows.
     *
     * Usage from Editor/Game:
     *   1. Poll GLFW in the host application
     *   2. Call Engine::Input::BeginFrame() at start of frame
     *   3. Call Set* methods to inject current input state
     *   4. Systems can then query input using Is* methods
     */
    class ZUES_API Input final {
    public:
        // --- Frame Lifecycle ---
        // Call at start of each frame before injecting new state
        static void BeginFrame();

        // --- State Injection (called by Editor/Game EXE) ---
        static void SetMouseButtonState(int button, bool pressed);
        static void SetKeyState(int keycode, bool pressed);
        static void SetMousePosition(float x, float y);
        static void AddScrollDelta(float delta);

        // --- Keyboard Queries ---
        static bool IsKeyPressed(int keycode);
        static bool IsKeyJustPressed(int keycode);
        static bool IsKeyJustReleased(int keycode);

        // --- Mouse Queries ---
        static bool IsMouseButtonPressed(int button);
        static bool IsMouseButtonJustPressed(int button);
        static bool IsMouseButtonJustReleased(int button);

        // --- Cursor Position ---
        static Math::Vec2 GetMousePosition();

        // --- Mouse Scroll ---
        static float GetMouseScrollDelta();

    private:
        // Key state
        static std::map<int, bool> m_CurrentKeyState;
        static std::map<int, bool> m_PreviousKeyState;

        // Mouse button state
        static std::map<int, bool> m_CurrentMouseState;
        static std::map<int, bool> m_PreviousMouseState;

        // Mouse position
        static float m_MouseX;
        static float m_MouseY;

        // Scroll state
        static float m_ScrollDelta;
        static float m_ScrollAccumulator;
    };
}
