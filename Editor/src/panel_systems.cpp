#include "editor.h"

#include <imgui.h>

#include <cstdio>

namespace Engine::editor {

namespace {
    const char* phase_label(ecs::Phase p) {
        switch (p) {
            case ecs::Phase::Input:        return "Input";
            case ecs::Phase::PreUpdate:    return "PreUpdate";
            case ecs::Phase::Physics:      return "Physics";
            case ecs::Phase::PostUpdate:   return "PostUpdate";
            case ecs::Phase::NetReplicate: return "NetReplicate";
            case ecs::Phase::UiInput:      return "UiInput";
            case ecs::Phase::UiLayout:     return "UiLayout";
            case ecs::Phase::Render:       return "Render";
            case ecs::Phase::UiRender:     return "UiRender";
        }
        return "?";
    }

    const char* domain_label(ecs::SystemDomain d) {
        switch (d) {
            case ecs::SystemDomain::Both:   return "Both";
            case ecs::SystemDomain::Editor: return "Editor";
            case ecs::SystemDomain::Game:   return "Game";
        }
        return "?";
    }

    ImVec4 domain_color(ecs::SystemDomain d) {
        switch (d) {
            case ecs::SystemDomain::Both:   return {0.55f, 0.65f, 0.80f, 1.0f};
            case ecs::SystemDomain::Editor: return {0.60f, 0.85f, 0.95f, 1.0f};
            case ecs::SystemDomain::Game:   return {0.95f, 0.75f, 0.45f, 1.0f};
        }
        return {1, 1, 1, 1};
    }
}

void draw_systems_panel(EditorState& s) {
    if (!s.show_systems) return;

    if (!ImGui::Begin("Systems", &s.show_systems)) { ImGui::End(); return; }

    if (!s.world) {
        ImGui::TextDisabled("No world.");
        ImGui::End();
        return;
    }

    // Tick mode is driven by the Play / Stop toolbar — read-only here.
    {
        const auto mode = s.world->tick_mode();
        const char* label = (mode == ecs::TickMode::Play) ? "PLAY" : "EDIT";
        const ImVec4 col  = (mode == ecs::TickMode::Play)
            ? ImVec4{1.0f, 0.55f, 0.30f, 1.0f}
            : ImVec4{0.55f, 0.65f, 0.80f, 1.0f};
        ImGui::TextUnformatted("Tick Mode:");
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", label);
        ImGui::SameLine();
        ImGui::TextDisabled("  (toggle with the Play/Stop toolbar)");
        ImGui::Separator();
    }

    if (ImGui::BeginTable("systems", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("On",      ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Phase",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Domain",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        // The systems list mutates as we toggle, so capture the changes and
        // apply after the iteration. iterate_systems takes a const-ish view
        // but set_system_enabled is a separate call — safe to invoke later.
        struct Toggle { ecs::SystemHandle h; bool enabled; };
        std::vector<Toggle> toggles;

        s.world->iterate_systems([&](const ecs::SystemInfo& info) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(info.handle.id));

            ImGui::TableSetColumnIndex(0);
            bool enabled = info.enabled;
            if (ImGui::Checkbox("##en", &enabled)) {
                toggles.push_back({info.handle, enabled});
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(info.name && *info.name ? info.name : "(unnamed)");

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", phase_label(info.phase));

            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(domain_color(info.domain), "%s",
                               domain_label(info.domain));

            ImGui::PopID();
        });

        for (const auto& t : toggles) {
            s.world->set_system_enabled(t.h, t.enabled);
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Total: %u system(s)", s.world->system_count());

    ImGui::End();
}

}  // namespace Engine::editor
