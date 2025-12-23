//
// Created by Claude Assistant
//

#include "../include/SystemsUI.h"
#include "../include/EditorUi.h"

#include "Core.h"
#include "imgui.h"
#include "ECS/World.h"
#include "ECS/System.h"
#include "ECS/WorldSerializationHelpers.h"

using namespace EditorWindows;

void SystemsUI::SystemsWindow() {
    ImGui::Begin("Systems");

    World* world = Engine::Core::GetCurrentWorld();
    if (!world) {
        ImGui::Text("No world loaded");
        ImGui::End();
        return;
    }

    Engine::SystemRegistry& registry = world->GetSystemRegistry();
    const auto& allSystems = registry.GetAllSystems();

    if (allSystems.empty()) {
        ImGui::Text("No systems registered");
        ImGui::End();
        return;
    }

    // Group systems by role
    std::vector<std::pair<std::string, const Engine::SystemInfo*>> sharedSystems;
    std::vector<std::pair<std::string, const Engine::SystemInfo*>> gameSystems;
    std::vector<std::pair<std::string, const Engine::SystemInfo*>> editorSystems;

    for (const auto& [name, info] : allSystems) {
        if (info.isRequired) continue; // Skip required systems

        switch (info.role) {
            case System::SystemRole::Shared:
                sharedSystems.emplace_back(name, &info);
                break;
            case System::SystemRole::Game:
                gameSystems.emplace_back(name, &info);
                break;
            case System::SystemRole::Editor:
                editorSystems.emplace_back(name, &info);
                break;
        }
    }

    // Helper lambda to render a system row
    auto renderSystemRow = [&](const std::string& name, const Engine::SystemInfo* info) {
        bool isActive = world->HasActiveSystem(name);

        // Check if this is a dormant system (from unloaded DLL)
        bool isDormant = false;
        for (const auto& dormant : world->GetDormantSystems()) {
            if (dormant.systemName == name) {
                isDormant = true;
                break;
            }
        }

        ImGui::PushID(name.c_str());

        // Checkbox for active state
        if (ImGui::Checkbox("##active", &isActive)) {
            if (isActive) {
                if (world->AddSystemByName(name)) {
                    EditorUi::MarkWorldAsModified();
                }
            } else {
                if (world->RemoveSystemByName(name)) {
                    EditorUi::MarkWorldAsModified();
                }
            }
        }

        ImGui::SameLine();

        // System name with color coding
        if (isDormant) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f)); // Orange for dormant
            ImGui::Text("%s (dormant)", name.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("This system's DLL is not loaded. It will be restored when the DLL is reloaded.");
            }
        } else if (info->isFromDLL) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f)); // Light blue for DLL systems
            ImGui::Text("%s", name.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("This system is from the game DLL");
            }
        } else {
            ImGui::Text("%s", name.c_str());
        }

        ImGui::PopID();
    };

    // Shared Systems Section
    if (!sharedSystems.empty()) {
        if (ImGui::CollapsingHeader("Shared Systems", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            for (const auto& [name, info] : sharedSystems) {
                renderSystemRow(name, info);
            }
            ImGui::Unindent();
        }
    }

    // Game Systems Section
    if (!gameSystems.empty()) {
        if (ImGui::CollapsingHeader("Game Systems", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            for (const auto& [name, info] : gameSystems) {
                renderSystemRow(name, info);
            }
            ImGui::Unindent();
        }
    }

    // Editor Systems Section (if any non-required)
    if (!editorSystems.empty()) {
        if (ImGui::CollapsingHeader("Editor Systems")) {
            ImGui::Indent();
            for (const auto& [name, info] : editorSystems) {
                renderSystemRow(name, info);
            }
            ImGui::Unindent();
        }
    }

    // Show dormant systems that aren't in the registry (from unloaded DLLs)
    const auto& dormantSystems = world->GetDormantSystems();
    std::vector<Engine::SerializedSystem> unknownDormant;
    for (const auto& dormant : dormantSystems) {
        if (!registry.HasSystem(dormant.systemName)) {
            unknownDormant.push_back(dormant);
        }
    }

    if (!unknownDormant.empty()) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        if (ImGui::CollapsingHeader("Unknown Systems (DLL not loaded)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();
            ImGui::Indent();
            for (const auto& dormant : unknownDormant) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::BulletText("%s", dormant.systemName.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("This system is from an unloaded DLL. Load the game DLL to restore it.");
                }
            }
            ImGui::Unindent();
        } else {
            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
}
