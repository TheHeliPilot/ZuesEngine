#include "../include/LoggerUI.h"
#include "imgui.h"
#include <string>
#include <vector>


using namespace EditorWindows;

std::vector<Engine::LogEvent> allLogs;
std::vector<Engine::LogEvent> infoLogs;
std::vector<Engine::LogEvent> warningLogs;
std::vector<Engine::LogEvent> errorLogs;

bool autoScroll = true;        // Auto-scroll toggle

void LoggerUI::GetLogEvent(const Engine::LogEvent& event) {
    allLogs.emplace_back(event);
    if (event.logLevel == LOGLEVEL_ERR)
        errorLogs.emplace_back(event);
    else if (event.logLevel == LOGLEVEL_WARN)
        warningLogs.emplace_back(event);
    else if (event.logLevel == LOGLEVEL_INFO)
        infoLogs.emplace_back(event);
}

void LoggerUI::LoggerWindow()
{
    static bool toggleState[3] = { true, true, true }; // Info, Warning, Error

    ImGui::Begin("Logger");

    // --- Header ---
    if (ImGui::Button("Clear"))
    {
        allLogs.clear();
        infoLogs.clear();
        warningLogs.clear();
        errorLogs.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);

    // --- Toggle Buttons ---
    ImGui::SameLine(0, 50.0f);

    auto DrawToggleButton = [](const std::string& label, size_t count, bool& state, ImVec4 activeColor) {
        bool pushed = false;
        if (state)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertFloat4ToU32(activeColor));
            pushed = true;
        }
        if (ImGui::Button((label + " [" + std::to_string(count) + "]").c_str()))
            state = !state;
        if (pushed) ImGui::PopStyleColor();
    };

    DrawToggleButton("Info", infoLogs.size(), toggleState[0], ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
    ImGui::SameLine(0, 10.0f);
    DrawToggleButton("Warning", warningLogs.size(), toggleState[1], ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
    ImGui::SameLine(0, 10.0f);
    DrawToggleButton("Error", errorLogs.size(), toggleState[2], ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

    ImGui::Separator();

    // --- Body / Log Output ---
    ImGui::BeginChild("LogScrollRegion", ImVec2(0,0), true, ImGuiWindowFlags_HorizontalScrollbar);

    int id = 0;
    for (const auto& log : allLogs)
    {
        if ((log.logLevel == LOGLEVEL_INFO && toggleState[0]) ||
            (log.logLevel == LOGLEVEL_WARN && toggleState[1]) ||
            (log.logLevel == LOGLEVEL_ERR && toggleState[2]))
        {
            // --- Colors ---
            ImVec4 timeColor  = ImVec4(0.85f, 0.85f, 0.85f, 1.0f); // grey timestamp
            ImVec4 msgColor   = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);    // white message
            ImVec4 levelColor;
            std::string levelStr;
            switch (log.logLevel)
            {
                case LOGLEVEL_INFO:  levelColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); levelStr = "INFO"; break;
                case LOGLEVEL_WARN:  levelColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); levelStr = "WARN"; break;
                case LOGLEVEL_ERR:   levelColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); levelStr = "ERROR"; break;
                default: levelColor = ImVec4(1,1,1,1); levelStr = "INFO"; break;
            }

            // --- Timestamp ---
            auto t = std::chrono::system_clock::to_time_t(log.timestamp);
            std::tm tm = *std::localtime(&t);
            char timeBuf[16];
            std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

            // --- Clickable log line ---
            ImGui::TextColored(timeColor, "[%s] ", timeBuf);
            ImGui::SameLine(0, 0);
            ImGui::TextColored(levelColor, "[%s] ", levelStr.c_str());
            ImGui::SameLine(0, 0);

            ImGui::Selectable((log.message + "###log" + std::to_string(id)).c_str(), false, 0, ImVec2(0,0));

            // Only open editor on double-click
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) // 0 = left mouse button
            {
                // Example: open in VS Code
                if (!log.file.empty())
                {
                    //CLION
                    //std::string cmd = "clion --line " + std::to_string(log.line) + " \"" + log.file + "\"";
                    //VS CODE:
                    std::string cmd = "code -g \"" + log.file + "\":" + std::to_string(log.line);
                    //VS
                    //std::string cmd = "devenv /edit \"" + log.file + "\" /command \"Edit.GoTo " + std::to_string(log.line) + "\"";
                    system(cmd.c_str());
                }
            }
        }
        id++;
    }

    // Auto-scroll
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
}
