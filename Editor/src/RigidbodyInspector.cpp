#include "../include/customInspectors/RigidbodyInspector.h"
#include "../include/EditorUi.h"
#include "imgui.h"
#include "Core.h"

bool RigidbodyInspector::OnGui(const char* label, nlohmann::json& j) {
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

        // Body Type as combo box
        if (j.contains("bodyType")) {
            int bodyType = j["bodyType"].get<int>();
            const char* bodyTypes[] = { "Static", "Kinematic", "Dynamic" };

            if (ImGui::Combo("Body Type", &bodyType, bodyTypes, IM_ARRAYSIZE(bodyTypes))) {
                j["bodyType"] = bodyType;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Static: Never moves\nKinematic: Moved by code only\nDynamic: Affected by physics");
            }
        }

        // Mass
        if (j.contains("mass")) {
            float mass = j["mass"].get<float>();
            if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.001f, 10000.0f, "%.3f kg")) {
                if (mass < 0.001f) mass = 0.001f;
                j["mass"] = mass;
                changed = true;
            }
        }

        // Gravity Scale
        if (j.contains("gravityScale")) {
            float gravityScale = j["gravityScale"].get<float>();
            if (ImGui::DragFloat("Gravity Scale", &gravityScale, 0.1f, -10.0f, 10.0f)) {
                j["gravityScale"] = gravityScale;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Multiplier for gravity. 0 = no gravity, negative = anti-gravity");
            }
        }

        // Fixed Rotation
        if (j.contains("fixedRotation")) {
            bool fixedRotation = j["fixedRotation"].get<bool>();
            if (ImGui::Checkbox("Fixed Rotation", &fixedRotation)) {
                j["fixedRotation"] = fixedRotation;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Prevents the body from rotating due to physics");
            }
        }

        ImGui::Unindent();
    }

    return changed;
}
