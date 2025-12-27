#include "../include/customInspectors/TransformInspector.h"
#include "../include/EditorUi.h"
#include "imgui.h"
#include "Core.h"

bool TransformInspector::OnGui(const char* label, nlohmann::json& j) {
    bool changed = false;

    ImGui::PushID("TransformComponent");

    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    // Transform cannot be removed - no context menu

    if (headerOpen) {
        ImGui::Indent();

        // Check if entity has a parent
        bool hasParent = false;
        if (j.contains("parent")) {
            uint64_t parentId = j["parent"].get<uint64_t>();
            hasParent = (parentId != 0);
        }

        if (hasParent) {
            // Has parent - show LOCAL position/rotation (what the user typically wants to edit)
            if (j.contains("localPosition")) {
                float pos[2] = {
                    j["localPosition"]["x"].get<float>(),
                    j["localPosition"]["y"].get<float>()
                };
                if (ImGui::DragFloat2("Position", pos, 0.1f)) {
                    j["localPosition"]["x"] = pos[0];
                    j["localPosition"]["y"] = pos[1];
                    changed = true;
                }
            }

            if (j.contains("localRotation")) {
                float rotation = j["localRotation"].get<float>();
                if (ImGui::DragFloat("Rotation", &rotation, 1.0f, -360.0f, 360.0f, "%.1f deg")) {
                    while (rotation > 180.0f) rotation -= 360.0f;
                    while (rotation < -180.0f) rotation += 360.0f;
                    j["localRotation"] = rotation;
                    changed = true;
                }
            }

            // Show world position as read-only info
            ImGui::Spacing();
            ImGui::TextDisabled("World Transform (read-only)");
            if (j.contains("worldPosition")) {
                float worldPos[2] = {
                    j["worldPosition"]["x"].get<float>(),
                    j["worldPosition"]["y"].get<float>()
                };
                ImGui::BeginDisabled();
                ImGui::DragFloat2("World Position", worldPos, 0.1f);
                ImGui::EndDisabled();
            }
            if (j.contains("worldRotation")) {
                float worldRot = j["worldRotation"].get<float>();
                ImGui::BeginDisabled();
                ImGui::DragFloat("World Rotation", &worldRot, 1.0f, -360.0f, 360.0f, "%.1f deg");
                ImGui::EndDisabled();
            }
        } else {
            // No parent - show WORLD position/rotation (which equals local for root objects)
            if (j.contains("worldPosition")) {
                float pos[2] = {
                    j["worldPosition"]["x"].get<float>(),
                    j["worldPosition"]["y"].get<float>()
                };
                if (ImGui::DragFloat2("Position", pos, 0.1f)) {
                    j["worldPosition"]["x"] = pos[0];
                    j["worldPosition"]["y"] = pos[1];
                    // Also update local since they should match for root objects
                    if (j.contains("localPosition")) {
                        j["localPosition"]["x"] = pos[0];
                        j["localPosition"]["y"] = pos[1];
                    }
                    changed = true;
                }
            }

            if (j.contains("worldRotation")) {
                float rotation = j["worldRotation"].get<float>();
                if (ImGui::DragFloat("Rotation", &rotation, 1.0f, -360.0f, 360.0f, "%.1f deg")) {
                    while (rotation > 180.0f) rotation -= 360.0f;
                    while (rotation < -180.0f) rotation += 360.0f;
                    j["worldRotation"] = rotation;
                    // Also update local since they should match for root objects
                    if (j.contains("localRotation")) {
                        j["localRotation"] = rotation;
                    }
                    changed = true;
                }
            }
        }

        ImGui::Unindent();
    }

    ImGui::PopID();
    return changed;
}
