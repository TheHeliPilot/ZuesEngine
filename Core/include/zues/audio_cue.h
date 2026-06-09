#pragma once

// AudioCue asset. JSON-owned format (.zcue) that wraps a list of audio
// clips with per-cue playback settings. Two flavours:
//
//   * User-authored cues live wherever the user puts them under
//     assets/ -- they show up in the asset browser, support drag-add
//     of audio files into the entries list, and let the user pick how
//     entries are chosen at play time (random for now).
//
//   * Auto-generated cues are minted by the asset registry next to
//     every audio file the project carries (`coin.wav` -> `coin.wav.zcue`).
//     They wrap a single entry pointing at their adjacent clip and
//     carry `auto_generated = true` + `wraps_clip = <audio guid>` so
//     the editor can resolve "user dragged a .wav onto a Cue slot"
//     to the matching auto-cue. They're hidden from the asset browser
//     to keep the listing clean.
//
// Settings are flat scalars + a single entries array so the JSON
// loads / serialises in twenty-something lines and stays diffable.

#include <zues/api.h>
#include <zues/guid.h>
#include <zues/types.h>

#include <vector>

namespace Engine {

enum class AudioCuePickMode : u32 {
    Random = 0,        // uniform pick over `entries`
};

struct AudioCueEntry {
    Guid clip{};       // guid of the audio asset (Audio kind)
};

struct AudioCue {
    bool             auto_generated = false;
    Guid             wraps_clip{};         // valid only when auto_generated
    f32              volume         = 1.0f;
    f32              volume_random  = 0.0f; // ± added per play
    f32              pitch          = 1.0f;
    f32              pitch_random   = 0.0f; // ± added per play
    bool             loop           = false;
    AudioCuePickMode pick_mode      = AudioCuePickMode::Random;
    std::vector<AudioCueEntry> entries;
};

// Read a .zcue from disk. `abs_path` is an absolute filesystem path.
// On success: fills `out` and (when non-null) stores the cue's guid in
// `out_guid`. Returns false when the file is missing / unparseable; the
// caller should treat the cue as fresh (defaults).
ZUES_API bool load_audio_cue(const char* abs_path, AudioCue& out,
                              Guid* out_guid);

// Write a .zcue to disk. Creates parent directories as needed. Returns
// false on filesystem error.
ZUES_API bool save_audio_cue(const char* abs_path, const AudioCue& cue,
                              Guid guid);

}  // namespace Engine
