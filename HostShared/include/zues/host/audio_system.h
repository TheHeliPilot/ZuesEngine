#pragma once

// Default audio system. One per host process. Owns the miniaudio
// engine, the loaded-clip cache, every active voice, and implements
// IAudio_v1. Editor + runtime construct one at startup, register the
// service, and call tick() once per frame after the world updates so
// AudioSource entities push their latest position to the mixer.

#include <zues/api.h>
#include <zues/ecs/world.h>
#include <zues/guid.h>

namespace Engine::host {

struct AudioSystem {
    // Boot the device + register IAudio_v1 in the global service registry.
    // Returns false on backend failure (no device, OS rejected the format,
    // ...). Safe to call again after a successful call -- subsequent calls
    // are no-ops.
    bool init(ecs::World& world);

    // Tear down voices, unload clips, stop the device, unregister the
    // service. Called from editor shutdown / runtime shutdown. Safe to
    // call when init was never called or already shut down.
    void shutdown();

    // Drive per-frame ECS<->mixer sync. Call once per frame. Internally
    // triggered system tick runs in both Editor and Game domains so audio
    // hears scene changes during edit-mode preview as well as gameplay.
    void tick(float dt);

    // The editor pushes its current selection here so 3D-source gizmos
    // (range circles) only draw for the focused AudioSource. Pass
    // NULL_ENTITY to clear.
    void set_selected_entity(ecs::Entity e);

    ecs::SystemHandle update_handle{};
};

// ---- Lync-host accessors (called by host_api thunks) ------------------
// These reach into the running AudioSystem. Out-of-bounds entities,
// missing AudioSource components, and missing audio service all no-op
// safely. NOT thread-safe; meant to be called from the main loop / from
// project-DLL systems running in PreUpdate.
namespace audio_api {

// Fire-and-forget by AudioRef path. Returns a voice handle (0 on fail).
// `volume` <= 0 -> default 1.0; `pitch` <= 0 -> default 1.0.
Engine::u32 play_one_shot_path(const char* path, float volume, float pitch);
// 3D variant -- plays at world position with min/max distance attenuation.
Engine::u32 play_one_shot_at  (const char* path, float x, float y,
                               float min_dist, float max_dist, float volume);
// Stop a voice handle previously returned by play_one_shot_*.
void        stop_voice         (Engine::u32 voice);
// Per-source playback control.
void        source_play        (ecs::Entity e);
void        source_stop        (ecs::Entity e);
void        source_pause       (ecs::Entity e, int paused);
int         source_is_playing  (ecs::Entity e);

// Master bus.
float       master_volume      ();
void        set_master_volume  (float v);
int         is_muted           ();
void        set_muted          (int muted);

// Diagnostics for the mixer panel.
int         voices_active      ();
int         clips_loaded       ();

// Editor preview helper: play a clip by absolute filesystem path. Uses
// the same backend + cache as one-shots so the device only opens once.
Engine::u32 preview_path       (const char* abs_path);

// Cue cache invalidation. Called by the editor's cue editor after
// writing changes to disk so the next play re-reads the file. Pass
// NULL_GUID to drop the entire cache.
void        invalidate_cue     (Engine::Guid g);

// Preview a cue end-to-end: resolves its random pick + applies the
// cue's volume / pitch (and ± random) just like a real source play
// would. Lets the cue editor's "▶ Test" button audition the result
// the user will actually get at runtime. Returns 0 on failure.
Engine::u32 preview_cue        (Engine::Guid cue_guid);

// Fire-and-forget spawners. Create a fresh entity with an AudioSource
// bound to the cue, mark it auto_destroy, autoplay, and (for 3D) put
// it at (x, y) with max_distance attenuation. Audio system destroys
// the entity when the voice finishes. Returns NULL_ENTITY on failure
// (no project loaded, missing cue, etc.).
ecs::Entity spawn_for_cue   (Engine::Guid cue_guid);
ecs::Entity spawn_for_cue_3d(Engine::Guid cue_guid,
                              float x, float y, float max_distance);

}  // namespace audio_api

}  // namespace Engine::host
