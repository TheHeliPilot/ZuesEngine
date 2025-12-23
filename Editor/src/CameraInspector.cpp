#include "../include/customInspectors/CameraInspector.h"
#include "../include/EditorUi.h"
#include "imgui.h"
#include "Core.h"

bool CameraInspector::OnGui(const char* label, nlohmann::json& j) {
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

        // Zoom
        if (j.contains("zoom")) {
            float zoom = j["zoom"].get<float>();
            if (ImGui::DragFloat("Zoom", &zoom, 0.01f, 0.1f, 10.0f, "%.2f")) {
                j["zoom"] = zoom;
                changed = true;
            }
        }

        // Half Height (orthographic size)
        if (j.contains("halfHeight")) {
            float halfHeight = j["halfHeight"].get<float>();
            if (ImGui::DragFloat("Ortho Size", &halfHeight, 0.1f, 0.1f, 1000.0f, "%.1f")) {
                j["halfHeight"] = halfHeight;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Half of the vertical view size in world units");
            }
        }

        // Also check for "orthoSize" as an alternative field name
        if (j.contains("orthoSize")) {
            float orthoSize = j["orthoSize"].get<float>();
            if (ImGui::DragFloat("Ortho Size", &orthoSize, 0.1f, 0.1f, 1000.0f, "%.1f")) {
                j["orthoSize"] = orthoSize;
                changed = true;
            }
        }

        // Background Color
        if (j.contains("backgroundColor")) {
            float bgColor[4] = {
                j["backgroundColor"]["x"].get<float>(),
                j["backgroundColor"]["y"].get<float>(),
                j["backgroundColor"]["z"].get<float>(),
                j["backgroundColor"]["w"].get<float>()
            };
            if (ImGui::ColorEdit4("Background", bgColor)) {
                j["backgroundColor"]["x"] = bgColor[0];
                j["backgroundColor"]["y"] = bgColor[1];
                j["backgroundColor"]["z"] = bgColor[2];
                j["backgroundColor"]["w"] = bgColor[3];
                changed = true;
            }
        }

        // Is Active
        if (j.contains("isActive")) {
            bool isActive = j["isActive"].get<bool>();
            if (ImGui::Checkbox("Active", &isActive)) {
                j["isActive"] = isActive;
                changed = true;
            }
        }

        ImGui::Unindent();
    }

    return changed;
}
