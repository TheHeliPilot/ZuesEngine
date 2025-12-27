#include "../include/customInspectors/SpriteInspector.h"
#include "../include/EditorUi.h"
#include "Engine.h"
#include "imgui.h"
#include "TextureManager.h"
#include "Core.h"

bool SpriteInspector::OnGui(const char* label, nlohmann::json& j) {
    bool changed = false;

    ImGui::PushID("SpriteComponent");

    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("SpriteComponentContext")) {
        if (ImGui::MenuItem("Remove Component")) {
            World* world = Engine::Core::GetCurrentWorld();
            if (world && !EditorWindows::EditorUi::selectedEntities.empty()) {
                world->RemoveComponentByType(
                    EditorWindows::EditorUi::selectedEntities[0],
                    Engine::ECS::Component::GetTypeID<Engine::ECS::Component::SpriteComponent>()
                );
                EditorWindows::EditorUi::MarkWorldAsModified();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (headerOpen) {
        ImGui::Indent();

        // Sprite/Texture selection
        if (j.contains("spriteName")) {
            const auto current = j["spriteName"].get<std::string>();

            if (ImGui::BeginCombo("##SpriteDropdown", current.empty() ? "(none)" : current.c_str())) {
                // Option to clear sprite
                if (ImGui::Selectable("(none)##SpriteNone", current.empty())) {
                    j["spriteName"] = "";
                    changed = true;
                }

                int spriteIdx = 0;
                for (const auto& texName : Engine::TextureManager::GetAllSpriteNames()) {
                    const bool selected = (texName == current);
                    ImGui::PushID(spriteIdx++);
                    if (ImGui::Selectable(texName.c_str(), selected)) {
                        j["spriteName"] = texName;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Sprite");
        }

        // Size
        if (j.contains("size")) {
            float size[2] = {
                j["size"]["x"].get<float>(),
                j["size"]["y"].get<float>()
            };
            if (ImGui::DragFloat2("Size", size, 0.1f, 0.01f, 1000.0f)) {
                j["size"]["x"] = size[0];
                j["size"]["y"] = size[1];
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

        ImGui::Separator();
        ImGui::TextDisabled("Rendering Order");

        // Layer
        if (j.contains("layer")) {
            int layer = j["layer"].get<int>();
            if (ImGui::DragInt("Layer", &layer)) {
                j["layer"] = layer;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Higher layers render on top");
            }
        }

        // Sort Order (within layer)
        if (j.contains("sortOrder")) {
            int sortOrder = j["sortOrder"].get<int>();
            if (ImGui::DragInt("Sort Order", &sortOrder)) {
                j["sortOrder"] = sortOrder;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Order within the same layer");
            }
        }

        ImGui::Unindent();
    }

    ImGui::PopID();
    return changed;
}
