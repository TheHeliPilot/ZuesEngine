#include "../include/InspectorUI.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <variant>

#include "Core.h"
#include "../include/EditorUi.h"
#include "ECS/HierarchyOutliner.h"

namespace EditorReflection {
    inline std::unordered_map<Engine::ECS::Component::TypeID, std::string> g_componentNames;

    template<typename T>
    void RegisterComponentName(const std::string& name) {
        g_componentNames[Engine::ECS::Component::GetTypeID<T>()] = name;
    }

    inline const std::string& GetName(Engine::ECS::Component::TypeID id) {
        static std::string unknown = "Unknown";
        auto it = g_componentNames.find(id);
        return (it != g_componentNames.end()) ? it->second : unknown;
    }
}


using namespace EditorWindows;

// ---------- Field System ---------- //

enum class FieldType
{
    Float,
    Float3,
    Bool,
    String,
    Color3
};

struct Field
{
    std::string name;
    FieldType type;
    void* data;           // pointer to actual variable
    size_t size = 0;      // needed for strings (buffer size)
};

// ---------- Generic Component Drawer ---------- //

void DrawComponent(const char* componentName, const std::vector<Field>& fields)
{
    if (ImGui::CollapsingHeader(componentName, ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& field : fields)
        {
            switch (field.type)
            {
                case FieldType::Float:
                    ImGui::DragFloat(field.name.c_str(), static_cast<float*>(field.data), 0.1f);
                    break;

                case FieldType::Float3:
                    ImGui::DragFloat3(field.name.c_str(), static_cast<float*>(field.data), 0.1f);
                    break;

                case FieldType::Bool:
                    ImGui::Checkbox(field.name.c_str(), static_cast<bool*>(field.data));
                    break;

                case FieldType::String:
                    ImGui::InputText(field.name.c_str(), static_cast<char*>(field.data), field.size);
                    break;

                case FieldType::Color3:
                    ImGui::ColorEdit3(field.name.c_str(), static_cast<float*>(field.data));
                    break;
            }
        }
    }
}

// ---------- Inspector Entry ---------- //

void InspectorUI::InspectorWindow() {
    const EntityID e = EditorUi::selectedEntity;

    if (e == NULL_ENTITY_ID) return;

    ImGui::Begin("Inspector");

    for (auto& [typeID, compPtr] : Engine::Core::GetCurrentWorld()->GetAllComponents(e)) {
        const std::string& compName = EditorReflection::GetName(typeID);

        // Dispatch by typeid → template
        if (typeID == Engine::ECS::Component::GetTypeID<Engine::ECS::Component::TransformComponent>()) {
            DrawComponentEditor(compName.c_str(), static_cast<Engine::ECS::Component::TransformComponent*>(compPtr));
        } else if (typeID == Engine::ECS::Component::GetTypeID<Engine::ECS::Component::SpriteComponent>()) {
            DrawComponentEditor(compName.c_str(), static_cast<Engine::ECS::Component::SpriteComponent*>(compPtr));
        } else if (typeID == Engine::ECS::Component::GetTypeID<Engine::ECS::Component::CameraComponent>()) {
            DrawComponentEditor(compName.c_str(), static_cast<Engine::ECS::Component::CameraComponent*>(compPtr));
        }
        // etc… (later you can generate this automatically)
    }


    ImGui::End();
}
