//
// Created by bucka on 9/27/2025.
//

#include "../include/EditorUi.h"

#include "imgui.h"
#include <string>
#include <EventSystem/Events.h>
#include <cstdint> // Ensure uint32_t is available for texture IDs
#include <iostream>

#include "Renderer.h"
// Include the new Engine component
#include "Core.h"
#include "ProjectManager.h"

std::vector<std::string> logs; // Store logs
bool autoScroll = true;        // Auto-scroll toggle

std::filesystem::path EditorUi::projectDir = "../../MyGameProject";

void EditorUi::DrawWindowUi() {
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    // Parent docking host
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_MenuBar;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::Begin("DockSpaceHost", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // DockSpace always active
    if (const ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    // ---------------- MENU BAR ----------------
    if (ImGui::BeginMenuBar())
    {
        // FILE MENU
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Project")) {
                logs.push_back("[File] New Project clicked!");
                // TODO: Hook up to ProjectManager::NewProject()
            }
            if (ImGui::MenuItem("Load Project")) {
                logs.push_back("[File] Load Project clicked!");
                // TODO: Hook up to ProjectManager::LoadProject()
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                // TODO: Handle exit
            }
            ImGui::EndMenu();
        }

        // EDIT MENU
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
            if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            if (ImGui::MenuItem("Copy", "CTRL+C")) {}
            if (ImGui::MenuItem("Paste", "CTRL+V")) {}
            ImGui::EndMenu();
        }

        // ADD MENU
        if (ImGui::BeginMenu("Add"))
        {
            if (ImGui::MenuItem("New World")) {
                Engine::Core::SaveWorld(projectDir.string() + "/Scenes/");
            }
            ImGui::EndMenu();
        }

        // BIG BUILD BUTTON
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
        if (ImGui::Button("🚀 Build Project 🚀")) {
            EditorUi::BuildProject();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndMenuBar();
    }

    ImGui::End(); // End DockSpaceHost

    // --- Logger Window ---
    {
        ImGui::Begin("Logger");

        if (ImGui::Button("Clear")) logs.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll);

        ImGui::Separator();

        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& log : logs)
            ImGui::TextUnformatted(log.c_str());

        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();

        ImGui::End();
    }

    // --- Viewport Window ---
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        ImGui::Begin("Viewport");

        const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        Engine::Renderer::SetViewportSize(viewportSize.x, viewportSize.y);

        if (const uint32_t textureID = Engine::Renderer::GetRenderTextureID(); textureID != 0) {
            ImGui::Image(
                (ImTextureID)(intptr_t)textureID,
                viewportSize,
                ImVec2(0, 1),
                ImVec2(1, 0)
            );
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}

void EditorUi::TestGetLogEvent(const Engine::LogEvent &event) {
    logs.push_back(event.GetMessage());
}

void EditorUi::BuildProject() {
    // The Editor component now simply delegates the build task to the Engine's ProjectManager.
    // This correctly separates the Editor's UI concerns from the Engine's core tooling.
    Engine::ProjectManager::BuildProject();
}