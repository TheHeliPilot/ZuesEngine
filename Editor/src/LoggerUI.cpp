#include "../include/LoggerUI.h"
#include "imgui.h"
#include <string>
#include <vector>


using namespace EditorWindows;

std::vector<std::string> infoLogs;
std::vector<std::string> warningLogs;
std::vector<std::string> errorLogs;

bool autoScroll = true;        // Auto-scroll toggle

void LoggerUI::GetLogEvent(const Engine::LogEvent& event) {
    if (event.logLevel == LOGLEVEL_ERR)
        errorLogs.push_back(event.GetMessage());
    else if (event.logLevel == LOGLEVEL_WARN)
        warningLogs.push_back(event.GetMessage());
    else if (event.logLevel == LOGLEVEL_INFO)
        infoLogs.push_back(event.GetMessage());
}

void LoggerUI::LoggerWindow()
{
    static bool selectablesState[3] = {true, true, true};

    // Header
    ImGui::Begin("Logger");
    if (ImGui::Button("Clear"))
    {
        infoLogs.clear();
        warningLogs.clear();
        errorLogs.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);

    std::string infoLabel = "Info [" + std::to_string(infoLogs.size()) + "]";
    float infoWidth = ImGui::CalcTextSize(infoLabel.c_str()).x;

    ImGui::SameLine(0, 50.0f);
    ImGui::PushID("Info");
    ImGui::Selectable(infoLabel.c_str() , &selectablesState[0]);
    ImGui::PopID();
    //ImGui::SameLine(0, 10.0f);
    //ImGui::Selectable("Warning", &selectablesState[1], 0, ImVec2(ImGui::CalcTextSize("Warning").x, 0));
    //ImGui::SameLine(0, 10.0f);
    //ImGui::Selectable("Error", &selectablesState[2], 0, ImVec2(ImGui::CalcTextSize("Error").x, 0));


    // Body
    ImGui::Separator();

    ImGui::BeginChild("LogScrollRegion", ImVec2(0,0), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (selectablesState[0])
        for (const auto& info : infoLogs) ImGui::TextUnformatted(info.c_str());
    if (selectablesState[1])
        for (const auto& warn : warningLogs) ImGui::TextUnformatted(warn.c_str());
    if (selectablesState[2])
        for (const auto& error : errorLogs) ImGui::TextUnformatted(error.c_str());

    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

