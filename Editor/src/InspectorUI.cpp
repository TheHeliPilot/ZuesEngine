#include "../include/InspectorUI.h"
#include "Core.h"
#include "imgui.h"
#include "../include/EditorUi.h"
#include "ECS/World.h"

using namespace EditorWindows;

// Track which component was right-clicked
static int selectedComponentTypeID = -1;

void InspectorUI::InspectorWindow() {
    const EntityID e = EditorUi::selectedEntity;

    ImGui::Begin("Inspector");

    if (e == NULL_ENTITY_ID) {
        ImGui::Text("No entity selected");
        ImGui::End();
        return;
    }

    World* world = Engine::Core::GetCurrentWorld();
    if (!world) {
        ImGui::Text("No active world");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity ID: %llu", static_cast<unsigned long long>(e.id));
    ImGui::Separator();

    const auto& registry = world->GetComponentRegistry();

    auto components = world->GetAllComponents(e);

    if (components.empty()) {
        ImGui::Text("No components");
    }

    for (auto& [typeID, compPtr] : components) {
        try {
            const auto* serializer = registry.GetSerializer(typeID);
            const std::string& compName = registry.GetTypeName(typeID);

            nlohmann::json j = serializer->SerializeFromPointer(compPtr);

            ImGui::PushID(typeID);

            // Draw the component editor and check for right-click
            if (DrawJsonComponentEditor(compName.c_str(), j, typeID)) {
                serializer->DeserializeIntoPointer(compPtr, j);
            }

            ImGui::PopID();

        } catch (const std::exception& ex) {
            ImGui::Text("Error inspecting component: %s", ex.what());
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        for (const auto& [typeID, serializer] : registry.GetAllSerializers()) {
            const std::string& compName = registry.GetTypeName(typeID);

            if (world->HasComponent(e, typeID))
                continue;

            if (ImGui::MenuItem(compName.c_str())) {
                world->AddComponentByType(e, typeID);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

bool InspectorUI::DrawJsonComponentEditor(const char* name, nlohmann::json& j, int componentTypeID) {
    bool changed = false;

    // Draw the collapsing header
    bool headerOpen = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);

    // Check for right-click on the header - use BeginPopupContextItem for automatic popup handling
    if (ImGui::BeginPopupContextItem()) {
        selectedComponentTypeID = componentTypeID;
        if (ImGui::MenuItem("Remove Component")) {
            World* world = Engine::Core::GetCurrentWorld();
            if (world && EditorUi::selectedEntity.IsValid()) {
                world->RemoveComponentByType(EditorUi::selectedEntity, componentTypeID);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (headerOpen) {
        ImGui::PushID(name);
        ImGui::Indent();

        for (auto& [key, value] : j.items()) {
            changed |= DrawJsonField(key.c_str(), value);
        }

        ImGui::Unindent();
        ImGui::PopID();
    }

    return changed;
}

bool InspectorUI::DrawJsonField(const char* label, nlohmann::json& value) {
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
    else if (value.is_number_unsigned()) {
        uint64_t v = value.get<uint64_t>();
        int intVal = static_cast<int>(v);
        if (ImGui::DragInt(label, &intVal, 1.0f, 0)) {
            value = static_cast<uint64_t>(intVal);
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
    else if (value.is_object()) {
        std::string lbl = label;

        // Check if label is like "m0", "m1", "m2" ...
        bool isMxWrapper = !lbl.empty() && lbl[0] == 'm' &&
                           std::all_of(lbl.begin() + 1, lbl.end(), ::isdigit);

        if (isMxWrapper) {
            // Unwrap directly (skip showing "mX")
            for (auto& [key, val] : value.items()) {
                changed |= DrawJsonField(key.c_str(), val);
            }
        }
        else if (value.contains("x") && value.contains("y")) {
            if (value.contains("z") && value.contains("w")) {
                // Vec4 / Color4
                float arr[4] = {
                    value["x"].get<float>(),
                    value["y"].get<float>(),
                    value["z"].get<float>(),
                    value["w"].get<float>()
                };

                std::string keyLower = lbl;
                std::ranges::transform(keyLower, keyLower.begin(), ::tolower);

                if (keyLower.find("color") != std::string::npos || keyLower.find("colour") != std::string::npos) {
                    if (ImGui::ColorEdit4(label, arr)) {
                        value["x"] = arr[0];
                        value["y"] = arr[1];
                        value["z"] = arr[2];
                        value["w"] = arr[3];
                        changed = true;
                    }
                } else {
                    if (ImGui::DragFloat4(label, arr, 0.1f)) {
                        value["x"] = arr[0];
                        value["y"] = arr[1];
                        value["z"] = arr[2];
                        value["w"] = arr[3];
                        changed = true;
                    }
                }
            }
            else if (value.contains("z")) {
                // Vec3
                float arr[3] = {
                    value["x"].get<float>(),
                    value["y"].get<float>(),
                    value["z"].get<float>()
                };

                if (ImGui::DragFloat3(label, arr, 0.1f)) {
                    value["x"] = arr[0];
                    value["y"] = arr[1];
                    value["z"] = arr[2];
                    changed = true;
                }
            }
            else {
                // Vec2
                float arr[2] = {
                    value["x"].get<float>(),
                    value["y"].get<float>()
                };

                if (ImGui::DragFloat2(label, arr, 0.1f)) {
                    value["x"] = arr[0];
                    value["y"] = arr[1];
                    changed = true;
                }
            }
        }
        else {
            // Normal nested object
            if (ImGui::TreeNode(label)) {
                for (auto& [key, val] : value.items()) {
                    changed |= DrawJsonField(key.c_str(), val);
                }
                ImGui::TreePop();
            }
        }
    }
    else if (value.is_string()) {
        std::string str = value.get<std::string>();
        char buffer[256];
        strncpy(buffer, str.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = std::string(buffer);
            changed = true;
        }
    }
    else if (value.is_array()) {
        ImGui::Text("%s: [array of %zu items]", label, value.size());
    }
    else {
        ImGui::Text("%s: (unsupported type)", label);
    }

    ImGui::PopID();
    return changed;
}