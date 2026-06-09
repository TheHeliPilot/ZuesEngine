#include "editor.h"

#include <imgui.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Engine::editor {

namespace {
    // List every .zworld in <project>/assets/worlds/, return the project-
    // relative path strings (forward-slashes for the .zuesproject file).
    std::vector<std::string> scan_worlds(const EditorState& s) {
        std::vector<std::string> out;
        if (s.project_dir.empty()) return out;
        const std::filesystem::path base(s.project_dir);
        const std::filesystem::path worlds =
            base / (s.project_worlds_dir.empty() ? "assets/worlds"
                                                 : s.project_worlds_dir);
        std::error_code ec;
        if (!std::filesystem::exists(worlds, ec)) return out;
        for (auto& it : std::filesystem::directory_iterator(worlds, ec)) {
            if (!it.is_regular_file(ec)) continue;
            if (it.path().extension() != ".zworld") continue;
            std::string rel = std::filesystem::relative(
                it.path(), base, ec).string();
            for (auto& c : rel) if (c == '\\') c = '/';
            if (!rel.empty()) out.push_back(std::move(rel));
        }
        std::sort(out.begin(), out.end());
        return out;
    }
}

void draw_project_settings_panel(EditorState& s) {
    if (!s.show_project_settings) return;
    if (!ImGui::Begin("Project Settings", &s.show_project_settings)) {
        ImGui::End();
        return;
    }

    if (!s.project_loaded) {
        ImGui::TextDisabled("(no project loaded)");
        ImGui::End();
        return;
    }

    ImGui::Text("%s", s.project_name.c_str());
    ImGui::TextDisabled("%s", s.project_dir.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Start world dropdown. Empty value = no auto-load (editor opens with
    // an empty world). Selecting a different one writes back to memory; the
    // main loop persists to .zuesproject when project_settings_dirty is set.
    ImGui::TextDisabled("Start world (loaded on project boot):");
    const auto worlds = scan_worlds(s);
    const std::string current = s.project_default_world;
    const char* preview = current.empty() ? "(none)" : current.c_str();
    if (ImGui::BeginCombo("##start_world", preview)) {
        if (ImGui::Selectable("(none)", current.empty())) {
            s.project_default_world.clear();
            s.project_settings_dirty = true;
        }
        ImGui::Separator();
        for (const auto& w : worlds) {
            const bool sel = (w == current);
            if (ImGui::Selectable(w.c_str(), sel)) {
                s.project_default_world  = w;
                s.project_settings_dirty = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        if (worlds.empty()) ImGui::TextDisabled("(no .zworld files in assets/worlds/)");
        ImGui::EndCombo();
    }
    if (s.project_settings_dirty) {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsaved)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Runtime window settings -----------------------------------------
    // These only affect the standalone runtime (.exe exported via Build ->
    // Export). The editor's own window is the dock-space, controlled by
    // the OS / user, and ignores these flags.
    ImGui::Text("Runtime window (applies to exported game)");

    int wh[2] = { s.project_window_width, s.project_window_height };
    if (ImGui::DragInt2("size (w x h)", wh, 1.0f, 64, 7680)) {
        if (wh[0] < 64) wh[0] = 64;
        if (wh[1] < 64) wh[1] = 64;
        s.project_window_width    = wh[0];
        s.project_window_height   = wh[1];
        s.project_settings_dirty  = true;
    }

    if (ImGui::Checkbox("Lock size (window not resizable)",
                         &s.project_fixed_size)) {
        s.project_settings_dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Disables the user-drag corner resize. The "
                          "exported runtime will refuse to be resized.");
    }

    if (ImGui::Checkbox("Launch fullscreen", &s.project_fullscreen)) {
        s.project_settings_dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Runtime starts in exclusive fullscreen on the "
                          "primary monitor at the size above. Implies "
                          "no user resize.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Source folder: %s",
        s.project_source_dir.empty() ? "src" : s.project_source_dir.c_str());
    ImGui::TextDisabled("Worlds folder: %s",
        s.project_worlds_dir.empty() ? "assets/worlds" : s.project_worlds_dir.c_str());
    ImGui::TextDisabled("Default language: %s",
        s.project_default_language.empty() ? "lync"
                                           : s.project_default_language.c_str());

    ImGui::End();
}

}  // namespace Engine::editor
