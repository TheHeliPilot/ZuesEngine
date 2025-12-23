#include "../include/customInspectors/TextInspector.h"
#include "../include/EditorUi.h"
#include "imgui.h"
#include "Core.h"

bool TextInspector::OnGui(const char* label, nlohmann::json& j) {
    bool changed = false;

    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove Component")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (headerOpen) {
        ImGui::Indent();

        // Text content (multiline)
        if (j.contains("text")) {
            std::string text = j["text"].get<std::string>();
            char buffer[1024];
            strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';

            if (ImGui::InputTextMultiline("Text", buffer, sizeof(buffer), ImVec2(-1, 60))) {
                j["text"] = std::string(buffer);
                changed = true;
            }
        }

        // Font ID (for now just a number, could be a dropdown later)
        if (j.contains("fontID")) {
            int fontID = j["fontID"].get<int>();
            if (ImGui::InputInt("Font ID", &fontID)) {
                if (fontID < 0) fontID = 0;
                j["fontID"] = fontID;
                changed = true;
            }
        }

        // Color
        if (j.contains("color")) {
            float color[4] = {
                j["color"]["x"].get<float>(),
                j["color"]["y"].get<float>(),
                j["color"]["z"].get<float>(),
                j["color"]["w"].get<float>()
            };
            if (ImGui::ColorEdit4("Color", color)) {
                j["color"]["x"] = color[0];
                j["color"]["y"] = color[1];
                j["color"]["z"] = color[2];
                j["color"]["w"] = color[3];
                changed = true;
            }
        }

        // Scale
        if (j.contains("scale")) {
            float scale = j["scale"].get<float>();
            if (ImGui::DragFloat("Scale", &scale, 0.1f, 0.1f, 100.0f)) {
                j["scale"] = scale;
                changed = true;
            }
        }

        // World Scale
        if (j.contains("worldScale")) {
            float worldScale = j["worldScale"].get<float>();
            if (ImGui::DragFloat("World Scale", &worldScale, 0.01f, 0.001f, 10.0f, "%.3f")) {
                j["worldScale"] = worldScale;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("World units per pixel");
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Rendering");

        // Layer
        if (j.contains("layer")) {
            int layer = j["layer"].get<int>();
            if (ImGui::DragInt("Layer", &layer)) {
                j["layer"] = layer;
                changed = true;
            }
        }

        // Sort Order
        if (j.contains("sortOrder")) {
            int sortOrder = j["sortOrder"].get<int>();
            if (ImGui::DragInt("Sort Order", &sortOrder)) {
                j["sortOrder"] = sortOrder;
                changed = true;
            }
        }

        ImGui::Unindent();
    }

    return changed;
}
