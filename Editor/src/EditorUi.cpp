//
// Created by bucka on 9/27/2025.
//

#include "../include/EditorUi.h"

#include "imgui.h"
#include <string>
#include <EventSystem/Events.h>

std::string s;

void EditorUi::DrawWindowUi() {
    ImGui::Text(s.c_str());

    ImGui::Separator();

    static float f = 0.0f;
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
}

void EditorUi::TestGetLogEvent(const Engine::LogEvent &event) {
    s = event.GetMessage();
}
