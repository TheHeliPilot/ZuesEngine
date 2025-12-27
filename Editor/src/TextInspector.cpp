#include "../include/customInspectors/TextInspector.h"
#include "../include/EditorUi.h"
#include "imgui.h"
#include "Core.h"
#include "Renderer.h"

bool TextInspector::OnGui(const char* label, nlohmann::json& j) {
    bool changed = false;

    ImGui::PushID("TextComponent");

    bool headerOpen = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("TextComponentContext")) {
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

            if (ImGui::InputTextMultiline("##TextContent", buffer, sizeof(buffer), ImVec2(-1, 60))) {
                j["text"] = std::string(buffer);
                changed = true;
            }
        }

        // Font dropdown
        if (j.contains("fontID")) {
            int fontID = j["fontID"].get<int>();
            const auto& fonts = Engine::Renderer::GetLoadedFonts();

            // Build current font name
            std::string currentFontName = "(None)";
            if (fontID > 0 && static_cast<size_t>(fontID) <= fonts.size()) {
                currentFontName = fonts[fontID - 1].Name;
            }

            if (ImGui::BeginCombo("Font", currentFontName.c_str())) {
                // Option for no font / invalid
                if (ImGui::Selectable("(None)", fontID == 0)) {
                    j["fontID"] = 0;
                    changed = true;
                }

                // List all loaded fonts
                for (size_t i = 0; i < fonts.size(); i++) {
                    const bool isSelected = (fontID == static_cast<int>(i + 1));
                    if (ImGui::Selectable(fonts[i].Name.c_str(), isSelected)) {
                        j["fontID"] = static_cast<int>(i + 1);
                        changed = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
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

    ImGui::PopID();
    return changed;
}
