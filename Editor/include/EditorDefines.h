//
// Created by bucka on 9/30/2025.
//

#ifndef EDITOR_EDITORDEFINES_H
#define EDITOR_EDITORDEFINES_H

/**
 * @brief Gets the mouse position relative to the specified ImGui Viewport Window.
 * * Assumes the function signature: Engine::Vec2 EditorWindows::EditorUi::GetMousePositionInWindow(const char* windowName)
 */
#define MOUSE_POS_SCREEN EditorWindows::EditorUi::GetMousePositionInWindow("Viewport")

/**
 * @brief Converts the screen-space mouse position into world coordinates.
 * * Assumes the function signature: Engine::Vec2 Engine::Renderer::ScreenToWorld(const Engine::Vec2& screenPos)
 */
#define MOUSE_POS_WORLD Engine::Renderer::ScreenToWorld(MOUSE_POS_SCREEN)


#endif //EDITOR_EDITORDEFINES_H