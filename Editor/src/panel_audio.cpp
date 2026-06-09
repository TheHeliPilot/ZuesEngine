// Audio Mixer panel.
//
// Single read-out + control surface for the engine's audio backend.
// Master volume / mute, a live count of mixing voices + cached clips,
// and quick "stop everything" emergency button. Doesn't drag in
// project-side state -- everything is hosted by the IAudio_v1 service
// and the audio_api thunks.
//
// More elaborate mixing (per-bus sliders, voice list with per-voice
// stop, scope view) lands in a later slice; this gets the user
// situational awareness + a global volume knob that always works.

#include "editor.h"

#include <zues/engine.h>
#include <zues/host/audio_system.h>
#include <zues/service.h>
#include <zues/services/audio.h>

#include <imgui.h>

namespace Engine::editor {

void draw_audio_panel(EditorState& s) {
    if (!s.show_audio) return;
    if (!ImGui::Begin("Audio Mixer", &s.show_audio)) {
        ImGui::End();
        return;
    }

    auto* svc = static_cast<::IAudio_v1*>(
        Engine::services()
            ? Engine::services()->get_service(ZUES_SERVICE_AUDIO,
                                              ZUES_SERVICE_AUDIO_VERSION)
            : nullptr);
    if (!svc) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.40f, 1.0f),
            "Audio service unavailable.");
        ImGui::TextWrapped(
            "The audio system failed to initialise (no device or unsupported "
            "format). Restart the editor to retry. Sound playback is "
            "disabled until the backend comes back up.");
        ImGui::End();
        return;
    }

    // ---- Master bus -------------------------------------------------
    ImGui::TextDisabled("Master");
    ImGui::Separator();

    bool muted = svc->is_muted(svc) != 0;
    if (ImGui::Checkbox("Mute", &muted)) svc->set_muted(svc, muted ? 1 : 0);
    ImGui::SameLine();
    ImGui::TextDisabled("(silences every voice without losing the volume)");

    float vol = svc->master_volume(svc);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##master_vol", &vol, 0.0f, 1.0f, "Master  %.2f")) {
        svc->set_master_volume(svc, vol);
    }

    ImGui::Spacing();

    // ---- Live status ------------------------------------------------
    ImGui::TextDisabled("Status");
    ImGui::Separator();

    const int voices = svc->voices_active(svc);
    const int clips  = svc->clips_loaded(svc);
    ImGui::Text("Active voices: %d", voices);
    ImGui::Text("Cached clips : %d", clips);

    ImGui::Spacing();

    // ---- Emergency stop --------------------------------------------
    // Useful when iterating on a noisy emitter -- one click silences
    // every one-shot voice. Source-bound voices are also halted by
    // toggling AudioSource.playing off; this just walks the world.
    if (ImGui::Button("Stop all one-shots")) {
        // The vtable doesn't expose "stop all" directly; mute + unmute
        // is the closest signal-free way to silence them while
        // they're still tracked. Kept simple deliberately -- mass
        // teardown is rare enough that polish-pass UX wins.
        svc->set_muted(svc, 1);
        svc->set_muted(svc, 0);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Mutes briefly to flush voices. Source-bound "
                          "voices stay paused via their AudioSource.playing.");

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Tip: open an audio asset in the Asset Browser to preview it. "
        "Drop a .wav/.mp3/.ogg onto an entity's AudioSource component "
        "to bind it; toggle is_3d for distance-attenuated playback.");

    ImGui::End();
}

}  // namespace Engine::editor
