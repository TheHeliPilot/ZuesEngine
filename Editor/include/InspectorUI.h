//
// Created by kukko on 2. 10. 2025.
//

#pragma once
#include <string>

#include "ECS/WorldSerializationHelpers.h"
#include "imgui.h"

namespace EditorWindows
{
    class InspectorUI final
    {
        public:
        // Main InspectorUI function
        static void InspectorWindow();
    };

    template<typename T>
void DrawComponentEditor(const char* name, T* component)
    {
        if (ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen))
        {
            // 1. Serialize component into json
            nlohmann::json j = *component;

            // 2. Iterate fields
            for (auto& [key, value] : j.items())
            {
                if (value.is_boolean()) {
                    bool v = value.get<bool>();
                    if (ImGui::Checkbox(key.c_str(), &v))
                        value = v;
                }
                else if (value.is_number_float()) {
                    float v = value.get<float>();
                    if (ImGui::DragFloat(key.c_str(), &v, 0.1f))
                        value = v;
                }
                else if (value.is_number_integer()) {
                    int v = value.get<int>();
                    if (ImGui::DragInt(key.c_str(), &v))
                        value = v;
                }
                else if (value.is_array() && value.size() == 2) {
                    float arr[2] = { value[0], value[1] };
                    if (ImGui::DragFloat2(key.c_str(), arr, 0.1f)) {
                        value[0] = arr[0]; value[1] = arr[1];
                    }
                }
                else if (value.is_array() && value.size() == 3) {
                    float arr[3] = { value[0], value[1], value[2] };
                    if (ImGui::DragFloat3(key.c_str(), arr, 0.1f)) {
                        value[0] = arr[0]; value[1] = arr[1]; value[2] = arr[2];
                    }
                }
                else if (value.is_array() && value.size() == 4) {
                    float arr[4] = { value[0], value[1], value[2], value[3] };
                    if (ImGui::ColorEdit4(key.c_str(), arr)) {
                        for (int i = 0; i < 4; i++) value[i] = arr[i];
                    }
                }
                else {
                    ImGui::Text("%s: (unsupported)", key.c_str());
                }
            }

            // 3. Write back into component
            *component = j.get<T>();
        }
    }

}

