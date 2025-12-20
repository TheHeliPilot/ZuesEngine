#include "../include/customInspectors/SpriteInspector.h"

#include "Engine.h"
#include "imgui.h"
#include "TextureManager.h" // Only this file needs it

bool SpriteInspector::OnGui(const char* label, nlohmann::json& j) {
    bool changed = false;

    if (!j.contains("spriteName"))
        return false;

    const auto current = j["spriteName"].get<std::string>();

    if (ImGui::BeginCombo(label, current.c_str())) {
        for (const auto& texName : Engine::TextureManager::GetAllSpriteNames()) {
            const bool selected = (texName == current);
            if (ImGui::Selectable(texName.c_str(), selected)) {
                j["spriteName"] = texName;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return changed;
}
