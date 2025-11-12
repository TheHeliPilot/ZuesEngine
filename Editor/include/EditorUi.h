#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include <EventSystem/Events.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "Log.h" // For LogLevel
#include "ECS/Entity.h"

struct ImGuiWindow;

namespace EditorWindows
{
    class EditorUi final {
    public:
        static std::filesystem::path projectDir;
        static Engine::Math::Vec2 viewportMousePos;
        static Engine::Math::Vec2 viewportSize;
        static std::vector<EntityID> selectedEntities;

        static void HandleWindowResize();

        static void DrawWindowUi();

        // NEW: Function to initiate a manual project build
        static void BuildProject(bool play);

        static inline Engine::Math::Vec2 FromImVec2(const ImVec2& v) {
            return { v.x, v.y };
        }

        static Engine::Math::Vec2 GetMousePositionInWindow(const std::string &windowName);

        static bool IsEntitySelected(const EntityID &entityID);

        static bool MouseInWindow(const char* windowName, ImGuiMouseButton button = ImGuiMouseButton_Left) {
            ImGuiWindow* window = ImGui::FindWindowByName(windowName);
            if (!window || window->Hidden)
                return false;

            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImVec2 bottomRight = ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);

            ImRect windowRect(window->Pos, bottomRight);
            return windowRect.Contains(mousePos);
        }

    };
}

