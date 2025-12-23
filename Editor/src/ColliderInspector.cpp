#include "../include/customInspectors/ColliderInspector.h"
#include "../include/EditorUi.h"
#include "imgui.h"
#include "Core.h"

// Helper function for common collider properties
static bool DrawColliderCommon(nlohmann::json& j, bool& changed) {
    // Density
    if (j.contains("density")) {
        float density = j["density"].get<float>();
        if (ImGui::DragFloat("Density", &density, 0.1f, 0.0f, 100.0f)) {
            if (density < 0.0f) density = 0.0f;
            j["density"] = density;
            changed = true;
        }
    }

    // Friction
    if (j.contains("friction")) {
        float friction = j["friction"].get<float>();
        if (ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 1.0f)) {
            friction = std::clamp(friction, 0.0f, 1.0f);
            j["friction"] = friction;
            changed = true;
        }
    }

    // Restitution (bounciness)
    if (j.contains("restitution")) {
        float restitution = j["restitution"].get<float>();
        if (ImGui::DragFloat("Bounciness", &restitution, 0.01f, 0.0f, 1.0f)) {
            restitution = std::clamp(restitution, 0.0f, 1.0f);
            j["restitution"] = restitution;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no bounce, 1 = perfect bounce");
        }
    }

    // Is Trigger
    if (j.contains("isTrigger")) {
        bool isTrigger = j["isTrigger"].get<bool>();
        if (ImGui::Checkbox("Is Trigger", &isTrigger)) {
            j["isTrigger"] = isTrigger;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Triggers detect collisions but don't cause physical response");
        }
    }

    return changed;
}

bool BoxColliderInspector::OnGui(const char* label, nlohmann::json& j) {
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

        // Size
        if (j.contains("size")) {
            float size[2] = {
                j["size"]["x"].get<float>(),
                j["size"]["y"].get<float>()
            };
            if (ImGui::DragFloat2("Size", size, 0.1f, 0.01f, 1000.0f)) {
                if (size[0] < 0.01f) size[0] = 0.01f;
                if (size[1] < 0.01f) size[1] = 0.01f;
                j["size"]["x"] = size[0];
                j["size"]["y"] = size[1];
                changed = true;
            }
        }

        // Offset
        if (j.contains("offset")) {
            float offset[2] = {
                j["offset"]["x"].get<float>(),
                j["offset"]["y"].get<float>()
            };
            if (ImGui::DragFloat2("Offset", offset, 0.1f)) {
                j["offset"]["x"] = offset[0];
                j["offset"]["y"] = offset[1];
                changed = true;
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Physics Material");

        DrawColliderCommon(j, changed);

        ImGui::Unindent();
    }

    return changed;
}

bool CircleColliderInspector::OnGui(const char* label, nlohmann::json& j) {
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

        // Radius
        if (j.contains("radius")) {
            float radius = j["radius"].get<float>();
            if (ImGui::DragFloat("Radius", &radius, 0.1f, 0.01f, 1000.0f)) {
                if (radius < 0.01f) radius = 0.01f;
                j["radius"] = radius;
                changed = true;
            }
        }

        // Offset
        if (j.contains("offset")) {
            float offset[2] = {
                j["offset"]["x"].get<float>(),
                j["offset"]["y"].get<float>()
            };
            if (ImGui::DragFloat2("Offset", offset, 0.1f)) {
                j["offset"]["x"] = offset[0];
                j["offset"]["y"] = offset[1];
                changed = true;
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Physics Material");

        DrawColliderCommon(j, changed);

        ImGui::Unindent();
    }

    return changed;
}
