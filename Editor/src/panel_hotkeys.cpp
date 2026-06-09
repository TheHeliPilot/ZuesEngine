// F1 hotkeys overlay. A discoverability cheat-sheet that lists every
// editor-wide keyboard shortcut. Toggled by F1 from anywhere in the
// editor (the global key handler in main.cpp flips show_hotkeys).
//
// Lives in its own panel file so the cheat-sheet is one source of
// truth -- when we add a new shortcut anywhere in the editor, we add
// the row HERE in the same change. The Docs panel can deep-link to
// this rather than restating the same list.

#include "editor.h"

#include <imgui.h>

namespace Engine::editor {

namespace {

struct Row {
    const char* keys;
    const char* desc;
};

// Section header + rows. Keep groups small (5-8 items) -- a wall of
// text scares users off. Categories follow what panels they live in.
struct Section {
    const char* title;
    const Row*  rows;
    int         count;
};

constexpr Row k_global[] = {
    { "F1",          "Toggle this hotkeys overlay" },
    { "Ctrl+S",      "Save current world" },
    { "Ctrl+R",      "Reload project (re-link DLL + re-load world)" },
    { "Ctrl+Z",      "Undo last component edit" },
    { "Ctrl+Shift+Z","Redo" },
    { "Esc",         "Close active modal / cancel rename" },
};

constexpr Row k_hierarchy[] = {
    { "F2",          "Rename selected entity" },
    { "Del",         "Delete selected entity (with confirm)" },
    { "Ctrl+N",      "New entity at root" },
    { "Ctrl+D",      "Duplicate selected entity (subtree)" },
    { "Ctrl+C / V",  "Copy / paste entity subtree" },
};

constexpr Row k_assets[] = {
    { "F2",          "Rename selected asset" },
    { "Right-click", "Create / Delete / Reveal / Rename menu" },
    { "Double-click",".png  -> Sprite Cutter" },
    { "Double-click",".zanim -> Animation Editor" },
    { "Double-click",".zworld -> load world" },
    { "Drag .png",   "into Sprite slot, animation editor, or scene" },
};

constexpr Row k_sprite_cutter[] = {
    { "Drag empty",  "Create a new slice" },
    { "Drag corner", "Resize selected slice" },
    { "Drag inside", "Move selected slice" },
    { "Drag green",  "Adjust 9-slice border (mid-edge handles)" },
    { "Alt+click",   "Cycle through stacked slices" },
};

constexpr Row k_scene[] = {
    { "Middle-drag", "Pan camera" },
    { "Wheel",       "Zoom" },
    { "Drag .png",   "Spawn as Sprite entity at drop point" },
    { "Click",       "Pick entity under cursor" },
};

constexpr Section k_sections[] = {
    { "Global",         k_global,        (int)(sizeof(k_global)/sizeof(*k_global)) },
    { "Hierarchy",      k_hierarchy,     (int)(sizeof(k_hierarchy)/sizeof(*k_hierarchy)) },
    { "Assets browser", k_assets,        (int)(sizeof(k_assets)/sizeof(*k_assets)) },
    { "Sprite cutter",  k_sprite_cutter, (int)(sizeof(k_sprite_cutter)/sizeof(*k_sprite_cutter)) },
    { "Scene viewport", k_scene,         (int)(sizeof(k_scene)/sizeof(*k_scene)) },
};

void draw_section(const Section& s) {
    if (ImGui::CollapsingHeader(s.title, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##hk", 2,
                ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthStretch);
            for (int i = 0; i < s.count; ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text,
                    IM_COL32(255, 198, 109, 255));
                ImGui::TextUnformatted(s.rows[i].keys);
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.rows[i].desc);
            }
            ImGui::EndTable();
        }
    }
}

}  // namespace

void draw_hotkeys_overlay(EditorState& s) {
    if (!s.show_hotkeys) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                     vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                              ImGuiCond_Appearing,
                              ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keyboard Shortcuts", &s.show_hotkeys,
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Press F1 anywhere to toggle this overlay.");
    ImGui::Spacing();

    for (const auto& sec : k_sections) {
        draw_section(sec);
    }

    ImGui::End();
}

}  // namespace Engine::editor
