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

    if (EditorUi::isReloading) {
        ImGui::Text("Hot-Reloading Game DLL...");
        ImGui::End();
        return;
    }

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

    // Validate entity still exists
    if (!entity.IsValid()) {
        ImGui::Text("Invalid entity selected");
        ImGui::End();
        return;
    }

    static char nameBuffer[256] = {};
    static EntityID lastEditedEntity;

    // Reset buffer when selecting a different entity
    if (lastEditedEntity.id != entity.id) {
        strncpy(nameBuffer, entity.name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        lastEditedEntity = entity;
    }

    if (ImGui::InputText("Entity Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (strlen(nameBuffer) > 0) {
            world->RenameEntity(entity, std::string(nameBuffer));
            EditorUi::MarkWorldAsModified();
        }
    }
    // Also save on deactivation (when clicking away)
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (strlen(nameBuffer) > 0) {
            world->RenameEntity(entity, std::string(nameBuffer));
            EditorUi::MarkWorldAsModified();
        }
    }
    ImGui::Separator();

    const auto& registry = world->GetComponentRegistry();
    auto components = world->GetAllComponents(entity);

    if (components.empty()) {
        ImGui::Text("No components");
    }

    for (auto& [typeID, compPtr] : components) {
        try {
            if (!compPtr) {
                continue; // Skip null component pointers
            }

            const auto* serializer = registry.GetSerializer(typeID);
            const std::string& compName = registry.GetTypeName(typeID);

            // If serializer is null, the DLL is missing for this component
            if (!serializer) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.1f, 0.1f, 1.0f)); // Subtle red
                if (ImGui::CollapsingHeader((compName + " (Inactive)").c_str())) {
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Code for this component is not loaded.");
                    ImGui::Text("Data is preserved in memory.");

                    if (ImGui::Button("Remove Component")) {
                        world->RemoveComponentByType(entity, typeID);
                        EditorUi::MarkWorldAsModified();
                    }
                }
                ImGui::PopStyleColor();
                continue; // Skip the rest of the drawing for this component
            }

            nlohmann::json j = serializer->SerializeFromPointer(compPtr);

            ImGui::PushID(typeID);

            if (InspectorRegistry::Has(typeID)) {
                IComponentInspector* inspector =
                    InspectorRegistry::Get(typeID);

                if (inspector->OnGui(compName.c_str(), j)) {
                    serializer->DeserializeIntoPointer(compPtr, j);
                    EditorUi::MarkWorldAsModified();
                }
            }
            else {
                if (DrawJsonComponentEditor(compName.c_str(), j, typeID)) {
                    serializer->DeserializeIntoPointer(compPtr, j);
                    EditorUi::MarkWorldAsModified();
                }
            }

            ImGui::PopID();
        }
        catch (const std::exception& ex) {
            ImGui::Text("Inspector error: %s", ex.what());
        }
    }

    // Display dormant (unknown) components for this entity
    if (world->HasDormantComponents(entity)) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
        ImGui::Text("UNKNOWN COMPONENTS");
        ImGui::PopStyleColor();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Load DLL to recover data");

        const auto& dormantComps = world->GetDormantComponents(entity);
        for (const auto& dormantComp : dormantComps) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));

            std::string headerLabel = dormantComp.typeName.empty()
                ? "Unknown Component (ID: " + std::to_string(dormantComp.typeID) + ")"
                : dormantComp.typeName + " (Missing DLL)";

            if (ImGui::CollapsingHeader(headerLabel.c_str())) {
                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Component data preserved in memory");
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Type: %s", dormantComp.typeName.c_str());
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Build and reload the game DLL to restore this component.");
                ImGui::Unindent();
            }

            ImGui::PopStyleColor(3);
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
        auto& reg = world->GetComponentRegistry();

        // --- SECTION 1: Engine Components ---
        ImGui::SeparatorText("Engine");
        for (const auto& [id, name] : reg.typeNames) {

            if (name == "Viewport Camera" || name == "ViewportCameraTag") continue;

            // Only show built-in engine components here
            if (id < reg.engineComponentCount) {
                if (!world->HasComponent(entity, id) && ImGui::MenuItem(name.c_str())) {
                    world->AddComponentByType(entity, id);
                    EditorUi::MarkWorldAsModified();
                }
            }
        }

        ImGui::Spacing();

        // --- SECTION 2: Game Project (DLL) ---
        ImGui::SeparatorText("Game Project");
        bool gameCompFound = false;
        for (const auto& [id, name] : reg.typeNames) {
            // IDs equal to or greater than engineComponentCount are user-defined
            if (id >= reg.engineComponentCount) {
                gameCompFound = true;

                // Check if logic is currently loaded from the DLL
                bool isLoaded = reg.GetSerializer(id) != nullptr;
                bool alreadyHas = world->HasComponent(entity, id);

                // Enable the menu item only if the DLL is loaded and the entity doesn't have it
                if (ImGui::MenuItem(name.c_str(), nullptr, false, isLoaded && !alreadyHas)) {
                    world->AddComponentByType(entity, id);
                    EditorUi::MarkWorldAsModified();
                }

                // Provide a helpful tooltip if the DLL is missing
                if (!isLoaded && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("DLL Logic not loaded. Rebuild the project.");
                }
            }
        }

        if (!gameCompFound) {
            ImGui::TextDisabled("  (No game components found)");
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
                EditorUi::MarkWorldAsModified();
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
