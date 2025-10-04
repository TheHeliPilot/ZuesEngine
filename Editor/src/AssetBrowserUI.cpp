#include "../include/AssetBrowserUI.h"
#include <functional>
#include "imgui.h"
#include "imgui_internal.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <chrono>

#include "../include/TextureCutterUI.h"

namespace EditorWindows {

    // Cached directory contents
    struct DirectoryCache {
        std::filesystem::path cachedPath;
        std::vector<std::filesystem::directory_entry> entries;
        std::chrono::steady_clock::time_point lastRefresh;
        
        bool IsStale(float maxAgeSeconds = 2.0f) const {
            auto now = std::chrono::steady_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - lastRefresh).count();
            return age >= maxAgeSeconds;
        }
    };

    static DirectoryCache s_Cache;
    static float s_FolderPaneWidth = 200.0f;

    // Refresh the cache for the current directory
    void RefreshDirectoryCache(const std::filesystem::path& directory) {
        s_Cache.entries.clear();
        s_Cache.cachedPath = directory;
        s_Cache.lastRefresh = std::chrono::steady_clock::now();

        try {
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                s_Cache.entries.push_back(entry);
            }

            // Sort: directories first, then alphabetical
            std::sort(s_Cache.entries.begin(), s_Cache.entries.end(), 
                [](const auto& a, const auto& b) {
                    if (a.is_directory() != b.is_directory()) {
                        return a.is_directory() > b.is_directory();
                    }
                    return a.path().filename().string() < b.path().filename().string();
                });

        } catch (const std::filesystem::filesystem_error& e) {
            LOG_ERROR("Failed to read directory: " + std::string(e.what()));
        }
    }

    void AssetBrowserUI::HandleFileDoubleClick(const std::filesystem::path& filePath) {
        LOG_INFO("Asset Double-Clicked: " + filePath.string());
        
        // Check if it's an image file and open texture cutter
        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            TextureCutterUI::OpenTextureCutter(filePath.string());
        }
    }

    void AssetBrowserUI::AssetBrowserWindow() {
        ImGui::Begin("Asset Browser");

        const ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float availableHeight = windowSize.y;
        const float minPaneWidth = 50.0f;
        const float splitterSize = 8.0f;

        // Check if we need to refresh the cache
        if (s_Cache.cachedPath != m_CurrentDirectory || s_Cache.IsStale()) {
            RefreshDirectoryCache(m_CurrentDirectory);
        }

        // --- LEFT PANE: Folder Tree ---
        ImGui::BeginChild("##FolderTree", ImVec2(s_FolderPaneWidth, availableHeight), true);
        {
            std::string rootName = EditorUi::projectDir.filename().string();
            if (rootName.empty() || rootName == ".") {
                rootName = "Assets Root";
            }

            if (ImGui::Selectable(rootName.c_str(), m_CurrentDirectory == EditorUi::projectDir)) {
                m_CurrentDirectory = EditorUi::projectDir;
                m_SelectedFilePath = "";
                RefreshDirectoryCache(m_CurrentDirectory); // Force refresh on navigation
            }

            try {
                for (const auto& entry : std::filesystem::directory_iterator(EditorUi::projectDir)) {
                    if (entry.is_directory()) {
                        const std::string dirName = entry.path().filename().string();
                        bool isSelected = (m_CurrentDirectory == entry.path());

                        if (ImGui::Selectable(dirName.c_str(), isSelected)) {
                            m_CurrentDirectory = entry.path();
                            m_SelectedFilePath = "";
                            RefreshDirectoryCache(m_CurrentDirectory); // Force refresh on navigation
                        }
                    }
                }
            } catch (const std::filesystem::filesystem_error& e) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error reading root:");
                ImGui::TextWrapped("%s", e.what());
            }
        }
        ImGui::EndChild();

        // --- SPLITTER ---
        ImGui::SameLine();
        ImGui::InvisibleButton("vsplitter", ImVec2(splitterSize, availableHeight));

        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            s_FolderPaneWidth += ImGui::GetIO().MouseDelta.x;
            s_FolderPaneWidth = std::clamp(s_FolderPaneWidth, minPaneWidth, windowSize.x - minPaneWidth);
        }

        ImGui::SameLine();

        // --- RIGHT PANE: Cached File List ---
        ImGui::BeginChild("##FolderContents", ImVec2(0, availableHeight), true);
        {
            ImGui::Text("Path: %s", m_CurrentDirectory.string().c_str());
            
            // Refresh button (optional, for manual refresh)
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) {
                RefreshDirectoryCache(m_CurrentDirectory);
            }
            
            ImGui::Separator();

            // ".." navigation (parent directory)
            if (m_CurrentDirectory != EditorUi::projectDir) {
                if (ImGui::Selectable("[Dir] ..", false)) {
                    m_CurrentDirectory = m_CurrentDirectory.parent_path();
                    m_SelectedFilePath = "";
                    RefreshDirectoryCache(m_CurrentDirectory); // Force refresh on navigation
                }
            }

            // Display cached entries (NO filesystem I/O here!)
            for (const auto& entry : s_Cache.entries) {
                const std::string fileName = entry.path().filename().string();
                if (fileName == ".") continue;

                const std::string icon = entry.is_directory() ? "[Dir] " : "[File] ";
                const std::string label = icon + fileName;

                // Handle directories
                if (entry.is_directory()) {
                    if (ImGui::Selectable(label.c_str(), false)) {
                        m_CurrentDirectory /= fileName;
                        m_SelectedFilePath = "";
                        RefreshDirectoryCache(m_CurrentDirectory); // Force refresh on navigation
                    }
                    continue;
                }

                // Handle files
                bool isSelected = (m_SelectedFilePath == entry.path());

                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_SelectedFilePath = entry.path();
                }

                // Double-click detection
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    HandleFileDoubleClick(entry.path());
                }
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }

} // namespace EditorWindows