// HotReloadUI.cpp - ImGui panel for hot-reload status and controls
#include "../include/HotReloadUI.h"
#include "../include/GameDLLLoader.h"
#include "../include/EditorUi.h"
#include <iomanip>
#include <sstream>

namespace Editor {

    bool HotReloadUI::s_IsVisible = true;
    std::vector<HotReloadLogEntry> HotReloadUI::s_Log;
    bool HotReloadUI::s_AutoScroll = true;

    void HotReloadUI::Draw() {
        // Sync visibility with EditorUi
        s_IsVisible = EditorWindows::EditorUi::showHotReload;

        if (!s_IsVisible) return;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Hot-Reload", &EditorWindows::EditorUi::showHotReload)) {
            ImGui::End();
            s_IsVisible = EditorWindows::EditorUi::showHotReload;
            return;
        }

        DrawStatusSection();
        ImGui::Separator();
        DrawControlsSection();
        ImGui::Separator();
        DrawLogSection();

        ImGui::End();
    }

    void HotReloadUI::DrawStatusSection() {
        auto& loader = GameDLLLoader::Get();
        GameDLLStatus status = loader.GetStatus();

        // Status indicator with color
        ImVec4 statusColor;
        const char* statusIcon;

        switch (status) {
            case GameDLLStatus::Loaded:
                statusColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green
                statusIcon = "\xef\x81\x98"; // Check circle icon (if using icon font)
                break;
            case GameDLLStatus::Loading:
            case GameDLLStatus::Building:
            case GameDLLStatus::Reloading:
                statusColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f); // Yellow
                statusIcon = "\xef\x84\x9d"; // Spinner icon
                break;
            case GameDLLStatus::Error:
                statusColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // Red
                statusIcon = "\xef\x81\x97"; // X circle icon
                break;
            default:
                statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
                statusIcon = "\xef\x81\x99"; // Info circle icon
                break;
        }

        ImGui::TextColored(statusColor, "Status: %s", loader.GetStatusString().c_str());

        if (loader.IsLoaded()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(Reload #%d)", loader.GetReloadCount());
        }

        if (status == GameDLLStatus::Error) {
            ImGui::TextWrapped("Error: %s", loader.GetLastError().c_str());
        }

        // DLL path
        if (!loader.GetDLLPath().empty()) {
            ImGui::TextDisabled("DLL: %s", loader.GetDLLPath().filename().string().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", loader.GetDLLPath().string().c_str());
            }
        }

        // Last reload time
        if (loader.GetReloadCount() > 0) {
            auto reloadTime = loader.GetLastReloadTime();
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - reloadTime);

            std::string timeAgo;
            if (elapsed.count() < 60) {
                timeAgo = std::to_string(elapsed.count()) + "s ago";
            } else if (elapsed.count() < 3600) {
                timeAgo = std::to_string(elapsed.count() / 60) + "m ago";
            } else {
                timeAgo = std::to_string(elapsed.count() / 3600) + "h ago";
            }

            ImGui::TextDisabled("Last reload: %s", timeAgo.c_str());
        }
    }

    void HotReloadUI::DrawControlsSection() {
        auto& loader = GameDLLLoader::Get();
        bool isLoaded = loader.IsLoaded();
        bool isBuilding = loader.IsBuildInProgress();

        // Auto-reload toggle
        bool autoReload = loader.IsAutoReloadEnabled();
        if (ImGui::Checkbox("Auto-Reload", &autoReload)) {
            loader.EnableAutoReload(autoReload);
            if (autoReload) {
                Log("Auto-reload enabled - watching for source changes");
            } else {
                Log("Auto-reload disabled");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically rebuild and reload when source files change");
        }

        ImGui::SameLine();

        // Build button
        ImGui::BeginDisabled(isBuilding);
        if (ImGui::Button("Build")) {
            Log("Starting build...");
            loader.BuildGameDLL(true);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        // Reload button
        ImGui::BeginDisabled(!isLoaded || isBuilding);
        if (ImGui::Button("Reload")) {
            Log("Manual reload triggered...");
            loader.ReloadDLL();
        }
        ImGui::EndDisabled();

        // Load/Unload buttons
        ImGui::BeginDisabled(isLoaded || isBuilding);
        if (ImGui::Button("Load DLL")) {
            if (loader.LoadDLL()) {
                Log("DLL loaded successfully");
            } else {
                Log("Failed to load DLL: " + loader.GetLastError(), true);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(!isLoaded);
        if (ImGui::Button("Unload DLL")) {
            loader.UnloadDLL();
            Log("DLL unloaded");
        }
        ImGui::EndDisabled();

        // Build progress indicator
        if (isBuilding) {
            ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0), "Building...");
        }
    }

    void HotReloadUI::DrawLogSection() {
        ImGui::Text("Log");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            ClearLog();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &s_AutoScroll);

        ImGui::BeginChild("HotReloadLog", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& entry : s_Log) {
            // Format timestamp
            auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
            std::tm tm = *std::localtime(&time);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%H:%M:%S");

            ImVec4 color = entry.isError ?
                ImVec4(0.9f, 0.3f, 0.3f, 1.0f) :  // Red for errors
                ImVec4(0.8f, 0.8f, 0.8f, 1.0f);   // Light gray for info

            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", oss.str().c_str());
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", entry.message.c_str());
        }

        if (s_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }

    void HotReloadUI::Log(const std::string& message, bool isError) {
        HotReloadLogEntry entry;
        entry.timestamp = std::chrono::system_clock::now();
        entry.message = message;
        entry.isError = isError;
        s_Log.push_back(entry);

        // Keep log size reasonable
        if (s_Log.size() > 100) {
            s_Log.erase(s_Log.begin());
        }
    }

    void HotReloadUI::ClearLog() {
        s_Log.clear();
    }

} // namespace Editor
