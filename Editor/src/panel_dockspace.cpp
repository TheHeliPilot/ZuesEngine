#include "editor.h"

#include <zues/services/debug_draw.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Engine::editor {

// Set to true to force a layout rebuild on the next frame.
// Exposed via request_reset_layout() so menu items can trigger it.
static bool s_force_rebuild_layout = false;

void request_reset_layout() {
    s_force_rebuild_layout = true;
}

void draw_main_menu_bar(EditorState& s) {
    // Reset per-frame focus flag - the Lync editor body sets it true again
    // during its draw if the embedded TextEditor owns keyboard focus. Used
    // by the global Ctrl+S handler below to skip world-save when the user
    // is typing in code.
    s.lync_editor_focused = false;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // (New / Open Project live in the Launcher — keeps lifecycle
            // ops out of the per-project editor.)

            // ---- World ops -----------------------------------------------
            if (ImGui::MenuItem("New World", "Ctrl+Shift+N", false, !s.is_playing)) {
                s.world->clear();
                s.current_world_path.clear();
                s.selected_entity = ecs::Entity{};
                s.world_dirty     = false;
                show_toast(s, "New world created", 2.0f, false);
            }
            if (ImGui::MenuItem("Open World...", "Ctrl+O", false, !s.is_playing)) {
                s.world_dialog_kind = EditorState::WorldDialogKind::Open;
                s.world_dialog_buf[0] = 0;
                s.world_dialog_just_opened = true;
            }
            ImGui::Separator();
            {
                const bool can_save    = !s.is_playing && !s.current_world_path.empty();
                const bool can_save_as = !s.is_playing;
                if (ImGui::MenuItem("Save World", "Ctrl+S", false, can_save))
                    s.want_save_world = true;
                if (ImGui::MenuItem("Save World As...", "Ctrl+Shift+S", false, can_save_as)) {
                    s.world_dialog_kind = EditorState::WorldDialogKind::SaveAs;
                    // Pre-fill with current name (sans extension) if any.
                    s.world_dialog_buf[0] = 0;
                    if (!s.current_world_path.empty()) {
                        const auto stem = std::filesystem::path(s.current_world_path).stem().string();
                        std::strncpy(s.world_dialog_buf, stem.c_str(),
                                     sizeof(s.world_dialog_buf) - 1);
                    }
                    s.world_dialog_just_opened = true;
                }
            }
            // Recent Worlds submenu
            if (ImGui::BeginMenu("Recent Worlds", !s.recent_worlds.empty())) {
                for (const auto& p : s.recent_worlds) {
                    if (ImGui::MenuItem(p.c_str(), nullptr, false, !s.is_playing))
                        s.want_load_world_path = p;
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();

            // ---- Project reload ------------------------------------------
            if (ImGui::MenuItem("Reload Project", "Ctrl+R"))
                s.want_manual_reload = true;
            ImGui::MenuItem("Auto-reload on rebuild", nullptr, &s.auto_reload_enabled);
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                // No-op for now; the GLFW close handler does this.
            }
            ImGui::EndMenu();
        }

        // ---- Global keyboard shortcuts (work when menu is closed) ----------
        if (!s.is_playing) {
            const ImGuiIO& io = ImGui::GetIO();

            // Ctrl+R — reload project
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false))
                s.want_manual_reload = true;

            // Ctrl+Z — undo, Ctrl+Shift+Z / Ctrl+Y — redo. Lync editor
            // owns its own per-doc undo (TextEditor's internal stack)
            // when focused, so we suppress these here in that case.
            if (!s.lync_editor_focused && io.KeyCtrl) {
                if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                    if (undo_perform_undo(s))
                        show_toast(s, "Undo", 0.6f, false);
                }
                if ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                    if (undo_perform_redo(s))
                        show_toast(s, "Redo", 0.6f, false);
                }
            }

            // Ctrl+S — save world (UNLESS the Lync editor has focus, in
            // which case it saves the active .lync doc; that path is owned
            // by panel_lync_editor and runs before this handler). The
            // `lync_editor_focused` flag is set at the end of every frame
            // the Lync editor was focused, so this branch only fires when
            // we're sure no .lync doc is taking the keystroke.
            if (!io.KeyShift && io.KeyCtrl &&
                    ImGui::IsKeyPressed(ImGuiKey_S, false) &&
                    !s.lync_editor_focused &&
                    !s.current_world_path.empty()) {
                s.want_save_world = true;
            }

            // Ctrl+Shift+S — save as
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                s.world_dialog_kind = EditorState::WorldDialogKind::SaveAs;
                s.world_dialog_buf[0] = 0;
                if (!s.current_world_path.empty()) {
                    const auto stem = std::filesystem::path(s.current_world_path).stem().string();
                    std::strncpy(s.world_dialog_buf, stem.c_str(),
                                 sizeof(s.world_dialog_buf) - 1);
                }
                s.world_dialog_just_opened = true;
            }

            // Ctrl+O — open world
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                s.world_dialog_kind = EditorState::WorldDialogKind::Open;
                s.world_dialog_buf[0] = 0;
                s.world_dialog_just_opened = true;
            }
        }
        // Ctrl+Shift+F — focus Search panel (works even while playing)
        {
            const ImGuiIO& io2 = ImGui::GetIO();
            if (io2.KeyCtrl && io2.KeyShift &&
                    ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                s.show_search          = true;
                s.search_focus_pending = true;
            }
        }
        if (ImGui::BeginMenu("Build")) {
            const bool can_export =
                s.project_loaded && !s.is_playing && s.world != nullptr;
            // Two configs side-by-side. Debug bundles the console-visible
            // runtime; Release strips logs and hides the console. They land
            // in separate dist subfolders so iterating one doesn't blow
            // away the other.
            if (ImGui::MenuItem("Export Debug",   nullptr, false, can_export)) {
                s.want_build_export = true;
                s.build_export_kind = ExportKind::Debug;
            }
            if (ImGui::MenuItem("Export Release", nullptr, false, can_export)) {
                s.want_build_export = true;
                s.build_export_kind = ExportKind::Release;
            }
            if (!can_export) {
                ImGui::TextDisabled(s.is_playing
                    ? "(stop play mode first)"
                    : "(load a project first)");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy",  nullptr, &s.show_hierarchy);
            ImGui::MenuItem("Inspector",  nullptr, &s.show_inspector);
            ImGui::MenuItem("Console",    nullptr, &s.show_console);
            ImGui::MenuItem("Scene",      nullptr, &s.show_scene);
            ImGui::MenuItem("Game",       nullptr, &s.show_game);
            ImGui::MenuItem("Systems",    nullptr, &s.show_systems);
            ImGui::MenuItem("Assets",     nullptr, &s.show_assets);
            ImGui::MenuItem("Audio Mixer",nullptr, &s.show_audio);
            ImGui::MenuItem("AudioCue",   nullptr, &s.show_cue_editor);
            ImGui::MenuItem("TODOs",      nullptr, &s.show_todos);
            ImGui::MenuItem("Lync Editor",nullptr, &s.show_lync_editor);
            ImGui::MenuItem("Search",     nullptr, &s.show_search);
            ImGui::MenuItem("Docs",       nullptr, &s.show_docs);
            ImGui::MenuItem("Project Settings", nullptr, &s.show_project_settings);
            ImGui::Separator();
            // Debug gizmo categories. Each toggle flips one bit in
            // the IDebugDraw_v1 service's mask -- engine subsystems
            // read it before publishing their visualizations.
            if (ImGui::BeginMenu("Debug Gizmos")) {
                u32 mask = debug_draw_categories();
                auto bit = [&](const char* label, u32 b) {
                    bool on = (mask & b) != 0u;
                    if (ImGui::MenuItem(label, nullptr, &on)) {
                        if (on) mask |= b; else mask &= ~b;
                        debug_draw_set_categories(mask);
                    }
                };
                bit("Particles", ZUES_DBG_PARTICLES);
                bit("Animator",  ZUES_DBG_ANIMATOR);
                bit("Audio",     ZUES_DBG_AUDIO);
                bit("Physics",   ZUES_DBG_PHYSICS);
                bit("AI",        ZUES_DBG_AI);
                ImGui::Separator();
                if (ImGui::MenuItem("All on"))  debug_draw_set_categories(ZUES_DBG_ALL);
                if (ImGui::MenuItem("All off")) debug_draw_set_categories(0u);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) request_reset_layout();
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &s.show_demo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextDisabled("Zues Engine 0.1.0");
            ImGui::EndMenu();
        }

        // ---- Play / Pause toolbar (centered) -------------------------------
        // Two toggle buttons. Play flips Edit↔Play (clicking again stops).
        // Pause is only meaningful while playing. The actual snapshot save/
        // restore happens in main's frame loop on the is_playing transition.
        // Uses real PNG icons when available; falls back to unicode glyphs
        // when icons failed to load (e.g. assets dir missing).
        {
            const float center_x = ImGui::GetWindowWidth() * 0.5f - 60.0f;
            ImGui::SameLine(center_x);

            const ImVec4 amber    {0.95f, 0.55f, 0.30f, 1.0f};
            const ImVec4 white    {1.0f,  1.0f,  1.0f,  1.0f};
            const ImVec4 dim      {0.55f, 0.55f, 0.58f, 1.0f};
            constexpr ImVec2 ICON_SZ {18, 18};

            auto icon_button = [&](const char* id, u32 tex, const char* fallback,
                                    const ImVec4& tint, bool disabled_visual) -> bool {
                if (disabled_visual) ImGui::BeginDisabled();
                bool clicked;
                if (tex != 0) {
                    clicked = ImGui::ImageButton(id,
                        static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                        ICON_SZ, ImVec2(0, 0), ImVec2(1, 1),
                        ImVec4(0, 0, 0, 0), tint);
                } else {
                    clicked = ImGui::Button(fallback, ImVec2(34, 0));
                }
                if (disabled_visual) ImGui::EndDisabled();
                return clicked;
            };

            // Play toggle.
            if (icon_button("##play", s.icons.play, "\xE2\x96\xB6",
                            s.is_playing ? amber : white, /*disabled=*/false)) {
                s.is_playing = !s.is_playing;
                if (s.is_playing) {
                    s.want_focus_game = true;
                    s.is_paused       = false;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(s.is_playing ? "Stop (restore scene)"
                                               : "Play (snapshot scene & start)");
            }

            ImGui::SameLine();

            // Pause toggle — visually disabled when not playing.
            const bool can_pause = s.is_playing;
            const ImVec4 pause_tint = !can_pause ? dim
                                    : (s.is_paused ? amber : white);
            if (icon_button("##pause", s.icons.pause, "\xE2\x9A\x8B",
                            pause_tint, !can_pause)) {
                if (can_pause) s.is_paused = !s.is_paused;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(!can_pause ? "Pause (start playing first)"
                                  : (s.is_paused ? "Resume" : "Pause"));
            }

            // Live state badge -- plain green/amber/dim text right of
            // the transport so the user always sees "is the world
            // ticking right now" without scanning the icon tints.
            ImGui::SameLine();
            if (s.is_playing && s.is_paused) {
                ImGui::TextColored(ImVec4(0.95f,0.78f,0.40f,1.0f), "PAUSED");
            } else if (s.is_playing) {
                ImGui::TextColored(ImVec4(0.55f,0.85f,0.55f,1.0f), "PLAYING");
            } else {
                ImGui::TextDisabled("edit");
            }
        }

        // Project label on the right side of the menu bar.
        if (s.project_loaded) {
            char label[256];
            std::snprintf(label, sizeof(label), "  %s  -  %s",
                          s.project_name.c_str(), s.project_dir.c_str());
            const float w = ImGui::CalcTextSize(label).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
            ImGui::TextDisabled("%s", label);
        } else {
            const char* msg = "  (no project loaded)";
            const float w = ImGui::CalcTextSize(msg).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
            ImGui::TextDisabled("%s", msg);
        }

        ImGui::EndMainMenuBar();
    }
}

void show_toast(EditorState& s, const char* message,
                float seconds, bool is_warning) {
    s.toast_message     = message ? message : "";
    s.toast_remaining_s = seconds;
    s.toast_is_warning  = is_warning;
}

void draw_toast_overlay(EditorState& s) {
    if (s.toast_remaining_s <= 0.0f || s.toast_message.empty()) return;

    // Tick the timer here so the toast lives for `seconds` from when it
    // was shown, regardless of how often it's drawn. Uses last_dt — same
    // dt the rest of the editor sees this frame.
    s.toast_remaining_s -= s.last_dt;
    if (s.toast_remaining_s <= 0.0f) {
        s.toast_remaining_s = 0.0f;
        return;
    }

    // Fade out over the last ~0.4s.
    constexpr float FADE_LEN = 0.4f;
    const float alpha = std::clamp(s.toast_remaining_s / FADE_LEN, 0.0f, 1.0f);

    // Anchor in the top-right of the main viewport, below the menu bar.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos {vp->WorkPos.x + vp->WorkSize.x - 16.0f,
                      vp->WorkPos.y + 16.0f};
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.92f * alpha);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));

    const ImVec4 fg = s.toast_is_warning
        ? ImVec4{1.0f, 0.65f, 0.30f, alpha}
        : ImVec4{0.85f, 0.92f, 1.0f, alpha};
    ImGui::PushStyleColor(ImGuiCol_Text, fg);

    constexpr ImGuiWindowFlags TOAST_FLAGS =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##editor_toast", nullptr, TOAST_FLAGS)) {
        ImGui::TextUnformatted(s.toast_message.c_str());
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void draw_dockspace() {
    static constexpr ImGuiWindowFlags WIN_FLAGS =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("ZuesDockspace", nullptr,
                 WIN_FLAGS & ~ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspace_id = ImGui::GetID("ZuesDockspaceRoot");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    // Build the default layout when none exists yet (first launch or after
    // the user selects View -> Reset Layout).
    const bool needs_build = (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
                           || s_force_rebuild_layout;
    if (needs_build) {
        s_force_rebuild_layout = false;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, vp->WorkSize);

        // --- Horizontal split: peel left 20%, then right 22% of remainder ---
        ImGuiID left_id   = 0;
        ImGuiID center_id = 0;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.20f,
                                    &left_id, &center_id);

        ImGuiID right_id = 0;
        // 22% of the remaining 80% ~ 0.275 of what is left
        ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.275f,
                                    &right_id, &center_id);

        // --- Left column: top 60% Hierarchy, bottom 40% Assets --------------
        ImGuiID left_top_id    = 0;
        ImGuiID left_bottom_id = 0;
        ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Up, 0.60f,
                                    &left_top_id, &left_bottom_id);

        // --- Center column: top 65% viewport, bottom 35% tools --------------
        ImGuiID center_top_id    = 0;
        ImGuiID center_bottom_id = 0;
        ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Up, 0.65f,
                                    &center_top_id, &center_bottom_id);

        // --- Dock windows ----------------------------------------------------
        // Left column
        ImGui::DockBuilderDockWindow("Hierarchy", left_top_id);
        ImGui::DockBuilderDockWindow("Assets",    left_bottom_id);

        // Right column
        ImGui::DockBuilderDockWindow("Inspector", right_id);

        // Center top: Scene + Game tabs
        ImGui::DockBuilderDockWindow("Scene", center_top_id);
        ImGui::DockBuilderDockWindow("Game",  center_top_id);

        // Center bottom: Console + Systems + Docs + Lync Editor tabs
        ImGui::DockBuilderDockWindow("Console",     center_bottom_id);
        ImGui::DockBuilderDockWindow("Systems",     center_bottom_id);
        ImGui::DockBuilderDockWindow("Audio Mixer", center_bottom_id);
        ImGui::DockBuilderDockWindow("AudioCue",    center_bottom_id);
        ImGui::DockBuilderDockWindow("Docs",        center_bottom_id);
        ImGui::DockBuilderDockWindow("Lync Editor", center_bottom_id);

        // "Project Settings" intentionally left floating (transient popup).

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

// ----------------------------------------------------------------------------
// World name dialog. Replaces the native Save As / Open file pickers — once
// the project root is chosen at creation time, worlds are name-only.
// ----------------------------------------------------------------------------
void draw_world_dialog(EditorState& s) {
    if (s.world_dialog_kind == EditorState::WorldDialogKind::None) return;
    if (!s.project_loaded || s.project_dir.empty()) {
        s.world_dialog_kind = EditorState::WorldDialogKind::None;
        show_toast(s, "Open a project first", 2.5f, true);
        return;
    }

    const bool is_save = (s.world_dialog_kind == EditorState::WorldDialogKind::SaveAs);
    const char* title  = is_save ? "Save World As" : "Open World";

    if (s.world_dialog_just_opened) {
        ImGui::OpenPopup(title);
        s.world_dialog_just_opened = false;
    }

    // Center on viewport.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::BeginPopupModal(title, &open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

        const std::filesystem::path worlds_root =
            std::filesystem::path(s.project_dir) / s.project_worlds_dir;

        ImGui::TextDisabled("Worlds folder: %s", Engine::editor::path_str(worlds_root).c_str());
        ImGui::Separator();

        // Existing worlds list (Open: click to select; Save: click to overwrite).
        std::error_code ec;
        if (std::filesystem::exists(worlds_root, ec)) {
            ImGui::TextDisabled("Existing:");
            ImGui::BeginChild("##world_list", ImVec2(360, 140), true);
            for (auto& it : std::filesystem::directory_iterator(worlds_root, ec)) {
                if (it.path().extension() != ".zworld") continue;
                const std::string stem = Engine::editor::path_str(it.path().stem());
                if (ImGui::Selectable(stem.c_str())) {
                    std::strncpy(s.world_dialog_buf, stem.c_str(),
                                 sizeof(s.world_dialog_buf) - 1);
                }
                // Double-click = commit immediately.
                if (ImGui::IsItemHovered() &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    std::strncpy(s.world_dialog_buf, stem.c_str(),
                                 sizeof(s.world_dialog_buf) - 1);
                    // Fall through to commit below by simulating Enter.
                    ImGui::SetKeyboardFocusHere(-1);
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("(folder will be created on save)");
        }

        ImGui::SetNextItemWidth(360);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", s.world_dialog_buf,
                                              sizeof(s.world_dialog_buf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled(".zworld appended automatically");

        const bool name_ok = s.world_dialog_buf[0] != 0;

        bool commit = enter;
        if (ImGui::Button(is_save ? "Save" : "Open", ImVec2(120, 0))) commit = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.world_dialog_kind = EditorState::WorldDialogKind::None;
            ImGui::CloseCurrentPopup();
        }

        if (commit && name_ok) {
            // Strip trailing ".zworld" if user typed it; we always re-append.
            std::string name = s.world_dialog_buf;
            const std::string ext = ".zworld";
            if (name.size() > ext.size() &&
                name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
                name.resize(name.size() - ext.size());
            }
            std::filesystem::create_directories(worlds_root, ec);
            const std::filesystem::path target = worlds_root / (name + ".zworld");
            if (is_save) s.want_save_world_path = normalize_path(target.string());
            else         s.want_load_world_path = normalize_path(target.string());
            s.world_dialog_kind = EditorState::WorldDialogKind::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open) s.world_dialog_kind = EditorState::WorldDialogKind::None;
}

// ----------------------------------------------------------------------------
// Generic confirm-on-delete modal. Used for any destructive op (delete file,
// delete folder, delete entity, remove component, ...) so users get a
// uniform "Are you sure?" gate. Owners stash the message + callback via
// request_confirm(); this draws + dispatches.
// ----------------------------------------------------------------------------
void draw_confirm_modal(EditorState& s) {
    if (s.confirm_open) {
        ImGui::OpenPopup("Confirm##editor");
        s.confirm_open = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm##editor", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("%s", s.confirm_message.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Yes", ImVec2(110, 0))) {
            if (s.confirm_action) s.confirm_action();
            s.confirm_action = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.confirm_action = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace Engine::editor
