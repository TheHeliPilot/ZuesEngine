//
// Created by bucka on 10/3/2025.
//

#include "../include/AssetBrowserUI.h"
#include "imgui.h"
#include "imgui_internal.h" // Needed for ImGui::Splitter
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

namespace EditorWindows {

    // --- Static Member Definitions (Must be defined outside the class scope if not inline) ---
    // Note: If C++17 'inline static' is fully supported, these lines are technically redundant
    // but often required by older compilers or specific project setups. I'll define them
    // without initializers here to be safe, assuming the inline definitions in the header will be used.
    // const std::filesystem::path AssetBrowserUI::s_ProjectRootPath;
    // std::filesystem::path AssetBrowserUI::m_CurrentDirectory;
    // std::filesystem::path AssetBrowserUI::m_SelectedFilePath;

    void AssetBrowserUI::HandleFileDoubleClick(const std::filesystem::path& filePath) {
        // This is the function that fires when you double-click a file.
        // Replace this with your engine's asset loading or spawning logic.
        LOG_INFO("Asset Double-Clicked: " + filePath.string());
    }

    void AssetBrowserUI::AssetBrowserWindow() {
        ImGui::Begin("Asset Browser");

        // Use the current window size for the contents
        ImVec2 windowSize = ImGui::GetContentRegionAvail();

        // --- Splitter State ---
        static float folderPaneWidth = 200.0f; // Initial width of the left pane

        // Calculate the height available for both panes
        float availableHeight = windowSize.y;

        // --- 1. Left Pane: Folder Tree (Fixed Width) ---
        ImGui::BeginChild("##FolderTree", ImVec2(folderPaneWidth, availableHeight), true);
        {
            // Display the root directory
            std::string rootName = EditorUi::projectDir.filename().string();
            if (rootName.empty() || rootName == ".") {
                rootName = "Assets Root";
            }

            // Allow selection of the root directory
            if (ImGui::Selectable(rootName.c_str(), m_CurrentDirectory == EditorUi::projectDir)) {
                m_CurrentDirectory = EditorUi::projectDir;
                m_SelectedFilePath = "";
            }

            // List top-level directories under the root
            try {
                for (const auto& entry : std::filesystem::__cxx11::directory_iterator(EditorUi::projectDir)) {
                    if (entry.is_directory()) {
                        const std::string dirName = entry.path().filename().string();

                        bool isSelected = (m_CurrentDirectory == entry.path());

                        if (ImGui::Selectable(dirName.c_str(), isSelected)) {
                            m_CurrentDirectory = entry.path();
                            m_SelectedFilePath = "";
                        }
                    }
                }
            } catch (const std::filesystem::__cxx11::filesystem_error& e) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error reading root path:");
                ImGui::TextWrapped("%s", e.what());
            }
        }
        ImGui::EndChild(); // End FolderTree

        // --- Splitter ---
        ImGui::SameLine();

        // ImGui::Splitter is an internal function, used here for easy resize handling
        // --- Splitter Logic (using InvisibleButton) ---
        ImGui::SameLine();

        // This constant was likely defined locally and not available inside the ImGui::IsItemActive block
        const float minPaneWidth = 50.0f;

        const float splitterSize = 8.0f;
        ImGui::InvisibleButton("vsplitter", ImVec2(splitterSize, availableHeight));

        if (ImGui::IsItemHovered()) {
            // Change mouse cursor to indicate a resizeable bar
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            // Get the change in X position
            float deltaX = ImGui::GetIO().MouseDelta.x;

            // Update the width, clamping it to reasonable bounds
            folderPaneWidth += deltaX;
            if (folderPaneWidth < minPaneWidth) folderPaneWidth = minPaneWidth;
            if (folderPaneWidth > windowSize.x - minPaneWidth) folderPaneWidth = windowSize.x - minPaneWidth;
        }
        // --- End Splitter Logic ---

        ImGui::SameLine();

        // --- 2. Right Pane: Files in Selected Folder (Remaining Width) ---
        // Width of 0 uses the remaining space
        ImGui::BeginChild("##FolderContents", ImVec2(0, availableHeight), true);
        {
            // Header showing the current path
            ImGui::Text("Path: %s", m_CurrentDirectory.string().c_str());
            ImGui::Separator();

            try {
                // Display ".." to navigate up, unless we are at the root
                if (m_CurrentDirectory != EditorUi::projectDir) {
                    if (ImGui::Selectable("[Dir] ..", false)) {
                        m_CurrentDirectory = m_CurrentDirectory.parent_path();
                        m_SelectedFilePath = "";
                    }
                }

                // Collect and sort all entries in the current directory
                std::vector<std::filesystem::__cxx11::directory_entry> entries;
                for (const auto& entry : std::filesystem::__cxx11::directory_iterator(m_CurrentDirectory)) {
                    entries.push_back(entry);
                }

                // Simple sort: directories first, then alphabetical
                std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                    if (a.is_directory() != b.is_directory()) {
                        return a.is_directory() > b.is_directory();
                    }
                    return a.path().filename().string() < b.path().filename().string();
                });

                for (const auto& entry : entries) {
                    const std::string fileName = entry.path().filename().string();

                    if (fileName == ".") continue; // Skip the current directory symbol

                    const std::string icon = entry.is_directory() ? "[Dir] " : "[File] ";
                    const std::string label = icon + fileName;

                    // Handle Sub-Directories (clickable to navigate down)
                    if (entry.is_directory()) {
                         if (ImGui::Selectable(label.c_str(), false)) {
                            // Only change the directory if the entry is not the parent '..' (handled above)
                            if (fileName != "..") {
                                m_CurrentDirectory /= fileName;
                                m_SelectedFilePath = "";
                            }
                        }
                        continue;
                    }

                    // Handle Files
                    bool isSelected = (m_SelectedFilePath == entry.path());

                    // ImGuiSelectableFlags_AllowDoubleClick allows us to detect double-click later
                    if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                        m_SelectedFilePath = entry.path(); // Single click selects the file
                    }

                    // Double-click detection
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        HandleFileDoubleClick(entry.path());
                    }
                }

            } catch (const std::filesystem::filesystem_error& e) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error reading directory:");
                ImGui::TextWrapped("%s", e.what());
            }
        }
        ImGui::EndChild(); // End FolderContents

        ImGui::End(); // End Asset Browser
    }

} // namespace EditorWindows
