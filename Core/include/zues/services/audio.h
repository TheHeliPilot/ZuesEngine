#pragma once

// Audio service. Engine-wide audio device, mixer, and per-clip playback
// store. The HostShared module implements this on top of miniaudio; the
// editor and the runtime register the same service so a project DLL
// hears the same sound whether it's running in the editor's Play mode or
// in a shipped game.
//
// Two playback styles, both routed through the same mixer:
//
//   * "fire and forget" one-shots (`play_one_shot`) -- the audio system
//     owns the voice, mixes it, and recycles it on completion. Returns a
//     stable u32 handle so callers can ramp / stop a specific instance,
//     or 0 when the voice failed to start.
//
//   * AudioSource component playback (`source_play` / `source_stop`) --
//     the voice is bound to an entity. The audio system reads the
//     entity's Transform2D each frame and updates 3D position + listener
//     attenuation automatically. Stopping the source destroys the voice.
//
// 3D model: a single AudioListener entity per scene picks "where the
// camera ears are." Sources tagged `is_3d = true` apply distance
// attenuation between min_distance (full volume) and max_distance
// (silence) plus a stereo pan derived from the source's offset to the
// listener. 2D sources (`is_3d = false`) skip the spatializer entirely
// and play through the master bus at their authored volume + pan.

#include <zues/api.h>
#include <zues/types.h>

#define ZUES_SERVICE_AUDIO          "zues.audio"
#define ZUES_SERVICE_AUDIO_VERSION  1

#ifdef __cplusplus
extern "C" {
#endif

// Opaque entity (matches ZuesEntity layout) -- the audio system uses
// this for source-bound voices without depending on editor/ECS headers.
typedef struct ZuesAudioEntity {
    Engine::u32 index;
    Engine::u32 generation;
} ZuesAudioEntity;

// Voice handle. 0 = invalid / failed-to-start. Returned by play_one_shot
// and stable until the voice finishes naturally or the caller stops it.
typedef Engine::u32 ZuesVoiceHandle;

// Spatial parameters passed to play_one_shot. When `is_3d == 0` the
// position / distances are ignored and the clip plays as 2D.
typedef struct ZuesAudioPlayParams {
    float volume;        // 0..1, multiplied with master + bus volumes
    float pitch;         // 1.0 = normal, 0.5 = octave down, 2.0 = up
    float pan;           // -1 = left, 0 = center, +1 = right (2D only)
    int   loop;          // 0 / 1 -- one-shots are usually 0
    int   is_3d;         // 0 = 2D, 1 = 3D (uses x,y + min/max_distance)
    float x;
    float y;
    float min_distance;  // < this radius the voice plays at full volume
    float max_distance;  // > this radius the voice is silent
} ZuesAudioPlayParams;

typedef struct IAudio_v1 {
    Engine::u32 abi_version;

    // ---- Master bus ----------------------------------------------------
    // 0..1 master volume scaler applied after every other gain stage.
    // `set_muted` overrides volume for "audio off" without losing the
    // user's slider position; `is_muted` reports it back to the UI.
    float (*master_volume)    (struct IAudio_v1* self);
    void  (*set_master_volume)(struct IAudio_v1* self, float v);
    int   (*is_muted)         (struct IAudio_v1* self);
    void  (*set_muted)        (struct IAudio_v1* self, int muted);

    // ---- Asset cache ---------------------------------------------------
    // Resolve a project-relative or absolute path to a cached clip.
    // Loads on first use, dedupes by path. Returns a non-zero clip id
    // on success, 0 on failure (file missing / decode error).
    Engine::u32 (*load_clip)(struct IAudio_v1* self, const char* path);
    // Drop a single clip. Voices currently playing the clip are stopped.
    // Safe to call with 0 (no-op).
    void        (*unload_clip)(struct IAudio_v1* self, Engine::u32 clip);
    // Drop everything. Called on project unload / editor shutdown.
    void        (*unload_all_clips)(struct IAudio_v1* self);

    // ---- One-shots -----------------------------------------------------
    // Start a fire-and-forget voice. `params` may be null for sensible
    // 2D defaults (volume=1, pitch=1, pan=0, loop=0). Returns a handle
    // that stays valid until the voice ends; passing the handle to
    // stop_voice / set_voice_volume is always safe.
    ZuesVoiceHandle (*play_one_shot)(struct IAudio_v1* self,
                                     Engine::u32 clip,
                                     const ZuesAudioPlayParams* params);
    void            (*stop_voice)        (struct IAudio_v1* self,
                                          ZuesVoiceHandle voice);
    void            (*set_voice_volume)  (struct IAudio_v1* self,
                                          ZuesVoiceHandle voice, float v);
    void            (*set_voice_pitch)   (struct IAudio_v1* self,
                                          ZuesVoiceHandle voice, float p);
    int             (*is_voice_playing)  (struct IAudio_v1* self,
                                          ZuesVoiceHandle voice);

    // ---- Listener ------------------------------------------------------
    // The audio system tracks one active listener per frame. Editor /
    // runtime call `set_listener` from the AudioListener system after
    // composing world transforms. (x, y) in WORLD units.
    void (*set_listener)(struct IAudio_v1* self, float x, float y);

    // ---- Component-bound voices ---------------------------------------
    // Start (or restart) the voice associated with an AudioSource entity.
    // The audio system reads `clip` + 2D/3D params off the component
    // each frame and steers the voice. Stopping clears the bound voice.
    void (*source_play) (struct IAudio_v1* self, ZuesAudioEntity e);
    void (*source_stop) (struct IAudio_v1* self, ZuesAudioEntity e);
    void (*source_pause)(struct IAudio_v1* self, ZuesAudioEntity e, int paused);
    int  (*source_is_playing)(struct IAudio_v1* self, ZuesAudioEntity e);

    // Per-frame sync: walk every AudioSource + AudioListener, push
    // positions + parameters to the underlying engine, recycle finished
    // voices. Called by the audio system itself; consumers normally
    // don't invoke this. Exposed so the editor can advance audio in
    // edit mode for previewing without entering Play.
    void (*tick)(struct IAudio_v1* self, float dt);

    // ---- Diagnostics --------------------------------------------------
    // Live counts -- the Audio Mixer panel reads these to show the user
    // how many voices are mixing right now. `voices_active` includes
    // both one-shots and source-bound voices.
    int (*voices_active)(struct IAudio_v1* self);
    int (*clips_loaded) (struct IAudio_v1* self);
} IAudio_v1;

#ifdef __cplusplus
}
#endif
