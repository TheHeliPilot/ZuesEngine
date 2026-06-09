// AudioCue editor panel.
//
// Two flavours of cue, one panel:
//
//   * Auto-generated cue (created alongside a .wav by the asset
//     registry). Wraps a single audio file. The entries list is
//     locked because it's tied to the source file -- removing the
//     wav would unbind every reference. The user can still tweak
//     volume / pitch / random / loop.
//
//   * User-authored cue (`.zcue` saved anywhere under assets/).
//     Full edit: drag audio files onto the entries drop zone to
//     append, hit X to remove, reorder via the up/down arrows.
//
// Edits autosave on change. After every successful save we tell the
// audio backend to drop its cached copy of this cue so the next
// playback re-reads the file -- this means changes are audible
// immediately when the user hits ▶ Test.

#include "editor.h"

#include <zues/asset.h>
#include <zues/audio_cue.h>
#include <zues/host/audio_system.h>

#include <imgui.h>

#include <filesystem>

namespace Engine::editor {

namespace {

namespace fs = std::filesystem;

// Resolve a cue guid to an absolute path on disk. Empty when not in
// the registry / no project loaded.
std::string cue_abs_path(EditorState& s, Engine::Guid g) {
    if (g.is_null() || s.project_dir.empty()) return {};
    const auto* entry = AssetRegistry::instance().find(g);
    if (!entry || entry->kind != AssetKind::AudioCue) return {};
    return s.project_dir + "/" + s.assets_root_relative + "/" + entry->path;
}

// Persist the editor's buffer back to disk + tell the audio backend
// to drop its cached copy. Called after every field edit.
void autosave(EditorState& s) {
    const std::string abs = cue_abs_path(s, s.cue_editor_target);
    if (abs.empty()) return;
    if (!save_audio_cue(abs.c_str(), s.cue_editor_buffer,
                         s.cue_editor_target)) return;
    Engine::host::audio_api::invalidate_cue(s.cue_editor_target);
    s.cue_editor_save_flash_until_s = ImGui::GetTime() + 0.6;
}

// "Display name" for an audio asset shown in the entries list.
std::string audio_label(Engine::Guid clip) {
    if (clip.is_null()) return "<none>";
    const char* p = AssetRegistry::instance().path_for(clip);
    return p ? p : "<missing>";
}

}  // namespace

void open_cue_editor_for_cue(EditorState& s, Engine::Guid cue_guid) {
    if (cue_guid.is_null()) return;
    const std::string abs = cue_abs_path(s, cue_guid);
    if (abs.empty()) return;
    AudioCue cue;
    Guid     g{};
    if (!load_audio_cue(abs.c_str(), cue, &g)) return;
    s.cue_editor_target = cue_guid;
    s.cue_editor_buffer = std::move(cue);
    s.show_cue_editor   = true;
    ImGui::SetWindowFocus("AudioCue");
}

void open_cue_editor_for_audio(EditorState& s, Engine::Guid audio_guid) {
    const auto* cue_entry =
        AssetRegistry::instance().find_auto_cue_for(audio_guid);
    if (!cue_entry) return;
    open_cue_editor_for_cue(s, cue_entry->guid);
}

void draw_cue_editor_panel(EditorState& s) {
    if (!s.show_cue_editor) return;
    if (!ImGui::Begin("AudioCue", &s.show_cue_editor)) {
        ImGui::End();
        return;
    }

    if (s.cue_editor_target.is_null()) {
        ImGui::TextDisabled("No cue loaded.");
        ImGui::TextWrapped(
            "Double-click a .zcue or audio file in the asset browser to "
            "open it here. Auto-generated cues let you tweak volume / "
            "pitch / random for a single sound; user-authored cues add "
            "an entries list you can fill with multiple variants.");
        ImGui::End();
        return;
    }

    AudioCue&  cue   = s.cue_editor_buffer;
    const auto* meta = AssetRegistry::instance().find(s.cue_editor_target);

    // ---- Header --------------------------------------------------
    if (cue.auto_generated) {
        ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.30f, 1.0f),
            "Auto-generated cue");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("This cue was created automatically for an "
                              "audio file. The entries list is locked.");
        if (!cue.wraps_clip.is_null()) {
            const char* wrapped = AssetRegistry::instance().path_for(cue.wraps_clip);
            ImGui::SameLine();
            ImGui::TextDisabled("- wraps %s", wrapped ? wrapped : "<missing>");
        }
    } else if (meta) {
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
            "%s", meta->path.c_str());
    }

    // Save-flash indicator (brief glow when autosave fires).
    if (ImGui::GetTime() < s.cue_editor_save_flash_until_s) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "saved");
    }

    ImGui::Spacing();
    if (ImGui::Button("\xE2\x96\xB6 Test", ImVec2(80, 0))) {
        Engine::host::audio_api::preview_cue(s.cue_editor_target);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Audition the cue with its current settings + "
                          "random pick.");
    ImGui::Separator();

    // ---- Playback settings ---------------------------------------
    ImGui::TextDisabled("Playback");
    if (ImGui::SliderFloat("Volume", &cue.volume, 0.0f, 2.0f, "%.2f"))
        autosave(s);
    if (ImGui::SliderFloat("Volume \xC2\xB1 random", &cue.volume_random,
                             0.0f, 1.0f, "%.2f"))
        autosave(s);
    if (ImGui::SliderFloat("Pitch", &cue.pitch, 0.1f, 4.0f, "%.2f"))
        autosave(s);
    if (ImGui::SliderFloat("Pitch \xC2\xB1 random", &cue.pitch_random,
                             0.0f, 1.0f, "%.2f"))
        autosave(s);
    if (ImGui::Checkbox("Loop", &cue.loop)) autosave(s);

    {
        const char* modes[] = { "Random" };
        int idx = static_cast<int>(cue.pick_mode);
        if (ImGui::Combo("Pick mode", &idx, modes, IM_ARRAYSIZE(modes))) {
            cue.pick_mode = static_cast<AudioCuePickMode>(idx);
            autosave(s);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Entries -------------------------------------------------
    ImGui::TextDisabled("Entries (%zu)", cue.entries.size());

    int remove_idx = -1;
    int move_up    = -1;
    int move_down  = -1;
    for (size_t i = 0; i < cue.entries.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%2zu.", i + 1);
        ImGui::SameLine();
        ImGui::TextUnformatted(audio_label(cue.entries[i].clip).c_str());

        if (!cue.auto_generated) {
            ImGui::SameLine();
            const float right_w = 90.0f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - right_w);
            if (ImGui::SmallButton("\xE2\x96\xB2") && i > 0) move_up = (int)i;
            ImGui::SameLine();
            if (ImGui::SmallButton("\xE2\x96\xBC") &&
                i + 1 < cue.entries.size()) move_down = (int)i;
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) remove_idx = (int)i;
        }
        ImGui::PopID();
    }

    if (move_up >= 0) {
        std::swap(cue.entries[move_up], cue.entries[move_up - 1]);
        autosave(s);
    } else if (move_down >= 0) {
        std::swap(cue.entries[move_down], cue.entries[move_down + 1]);
        autosave(s);
    } else if (remove_idx >= 0) {
        cue.entries.erase(cue.entries.begin() + remove_idx);
        autosave(s);
    }

    if (!cue.auto_generated) {
        ImGui::Spacing();
        ImGui::Button("Drop audio files here to add", ImVec2(-FLT_MIN, 36));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl =
                    ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                const char* abs = static_cast<const char*>(pl->Data);
                if (abs) {
                    const Guid g =
                        AssetRegistry::instance().guid_for_any_path(abs);
                    const AssetKind k = asset_kind_from_extension(abs);
                    if (!g.is_null() && k == AssetKind::Audio) {
                        cue.entries.push_back(AudioCueEntry{ g });
                        autosave(s);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled(
            "(Entries are read-only on auto-generated cues. "
            "Create a separate .zcue to combine multiple audio files.)");
    }

    ImGui::End();
}

}  // namespace Engine::editor
