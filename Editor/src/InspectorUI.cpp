#include "../include/InspectorUI.h"

#include "Core.h"
#include "imgui.h"
#include "../include/EditorUi.h"
#include "ECS/World.h"
#include "../include/customInspectors/InspectorRegistry.h"

using namespace EditorWindows;

// Track which component was right-clicked
static int selectedComponentTypeID = -1;

void InspectorUI::InspectorWindow() {
    ImGui::Begin("Inspector");

    if (EditorUi::selectedEntities.empty()) {
        ImGui::Text("No entity selected");
        ImGui::End();
        return;
    }

    if (EditorUi::selectedEntities.size() > 1) {
        ImGui::Text("Cannot edit multiple entities");
        ImGui::End();
        return;
    }

    World* world = Engine::Core::GetCurrentWorld();
    if (!world) {
        ImGui::Text("No active world");
        ImGui::End();
        return;
    }

    const EntityID entity = EditorUi::selectedEntities[0];

    char name[32] = {};
    strncpy(name, entity.name.c_str(), sizeof(name) - 1);
    ImGui::InputText("Entity Name", name, IM_ARRAYSIZE(name));
    ImGui::Separator();

    const auto& registry = world->GetComponentRegistry();
    auto components = world->GetAllComponents(entity);

    if (components.empty()) {
        ImGui::Text("No components");
    }

    for (auto& [typeID, compPtr] : components) {
        try {
            const auto* serializer = registry.GetSerializer(typeID);
            const std::string& compName = registry.GetTypeName(typeID);

            nlohmann::json j = serializer->SerializeFromPointer(compPtr);

            ImGui::PushID(typeID);

            if (InspectorRegistry::Has(typeID)) {
                IComponentInspector* inspector =
                    InspectorRegistry::Get(typeID);

                if (inspector->OnGui(compName.c_str(), j)) {
                    serializer->DeserializeIntoPointer(compPtr, j);
                }
            }
            else {
                if (DrawJsonComponentEditor(compName.c_str(), j, typeID)) {
                    serializer->DeserializeIntoPointer(compPtr, j);
                }
            }

            ImGui::PopID();
        }
        catch (const std::exception& ex) {
            ImGui::Text("Inspector error: %s", ex.what());
        }
    }

    ImGui::Separator();

    ImGui::Text("Registered Comps: %zu", world->GetComponentRegistry().GetAllSerializers().size());
    if (ImGui::Button("Add Component")) {
        for (const auto& all = world->GetComponentRegistry().GetAllSerializers(); auto const& [id, ser] : all) {
            LOG_INFO("Registry contains ID: " + std::to_string(id) + " Name: " + world->GetComponentRegistry().GetTypeName(id));
        }
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        for (const auto &typeID: registry.GetAllSerializers() | std::views::keys) {
            const std::string& compName = registry.GetTypeName(typeID);

            if (world->HasComponent(entity, typeID))
                continue;

            if (ImGui::MenuItem(compName.c_str())) {
                world->AddComponentByType(entity, typeID);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

bool InspectorUI::DrawJsonComponentEditor(
    const char* name,
    nlohmann::json& j,
    const int componentTypeID
) {
    bool changed = false;

    const bool headerOpen =
        ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem()) {
        selectedComponentTypeID = componentTypeID;

        if (ImGui::MenuItem("Remove Component")) {
            if (World* world = Engine::Core::GetCurrentWorld(); world && EditorUi::selectedEntities[0].IsValid()) {
                world->RemoveComponentByType(
                    EditorUi::selectedEntities[0],
                    componentTypeID
                );
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (headerOpen) {
        ImGui::Indent();

        for (auto& [key, value] : j.items()) {
            // Skip the "m0", "m1", etc. wrapper keys added by serialization
            // and draw their contents directly
            if (key.size() >= 2 && key[0] == 'm' && std::isdigit(key[1])) {
                // This is a wrapper key (m0, m1, etc.) - draw contents directly
                if (value.is_object()) {
                    for (auto& [innerKey, innerValue] : value.items()) {
                        changed |= DrawJsonField(innerKey.c_str(), innerValue);
                    }
                }
            } else {
                changed |= DrawJsonField(key.c_str(), value);
            }
        }

        ImGui::Unindent();
    }

    return changed;
}

bool InspectorUI::DrawJsonField(
    const char* label,
    nlohmann::json& value
) {
    bool changed = false;
    ImGui::PushID(label);

    if (value.is_boolean()) {
        bool v = value.get<bool>();
        if (ImGui::Checkbox(label, &v)) {
            value = v;
            changed = true;
        }
    }
    else if (value.is_number_float()) {
        float v = value.get<float>();
        if (ImGui::DragFloat(label, &v, 0.1f)) {
            value = v;
            changed = true;
        }
    }
    else if (value.is_number_integer()) {
        int v = value.get<int>();
        if (ImGui::DragInt(label, &v)) {
            value = v;
            changed = true;
        }
    }
    else if (value.is_string()) {
        const auto str = value.get<std::string>();
        char buffer[256];
        strncpy(buffer, str.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = std::string(buffer);
            changed = true;
        }
    }
    else if (value.is_object()) {
        // Vec detection
        if (value.contains("x") && value.contains("y")) {
            if (value.contains("z") && value.contains("w")) {
                float v[4] = {
                    value["x"], value["y"], value["z"], value["w"]
                };

                std::string lower = label;
                std::ranges::transform(lower, lower.begin(), ::tolower);

                if (lower.find("color") != std::string::npos) {
                    if (ImGui::ColorEdit4(label, v)) {
                        value["x"] = v[0];
                        value["y"] = v[1];
                        value["z"] = v[2];
                        value["w"] = v[3];
                        changed = true;
                    }
                }
                else if (ImGui::DragFloat4(label, v, 0.1f)) {
                    value["x"] = v[0];
                    value["y"] = v[1];
                    value["z"] = v[2];
                    value["w"] = v[3];
                    changed = true;
                }
            }
            else if (value.contains("z")) {
                float v[3] = {
                    value["x"], value["y"], value["z"]
                };

                if (ImGui::DragFloat3(label, v, 0.1f)) {
                    value["x"] = v[0];
                    value["y"] = v[1];
                    value["z"] = v[2];
                    changed = true;
                }
            }
            else {
                float v[2] = { value["x"], value["y"] };
                if (ImGui::DragFloat2(label, v, 0.1f)) {
                    value["x"] = v[0];
                    value["y"] = v[1];
                    changed = true;
                }
            }
        }
        else if (ImGui::TreeNode(label)) {
            for (auto& [k, v] : value.items()) {
                changed |= DrawJsonField(k.c_str(), v);
            }
            ImGui::TreePop();
        }
    }
    else if (value.is_array()) {
        ImGui::Text("%s: [array]", label);
    }

    ImGui::PopID();
    return changed;
}
