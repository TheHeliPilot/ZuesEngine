#pragma once

// Audio components. Two pieces:
//
//   AudioSource   -- one entity = one voice. `clip` references the audio
//                    asset; `is_3d` toggles spatialisation; `playing`
//                    drives whether the voice is currently mixing. Edit
//                    the fields in the inspector and the audio system
//                    picks up the change next tick.
//
//   AudioListener -- the "where the ears are" entity. The audio system
//                    finds the first AudioListener with `is_active = 1`
//                    and uses that entity's Transform2D position for 3D
//                    attenuation. If no AudioListener exists, the system
//                    falls back to the active Camera2D's position so
//                    sounds are still spatialised against the camera.
//
// Both components are cheap PODs; the actual voice + decoder live in
// the audio system's heap, keyed off entity + clip.

#include <zues/api.h>
#include <zues/asset.h>
#include <zues/types.h>
#include <zues/ecs/reflection.h>

namespace Engine::components {

// AudioSource: emits sound from an entity.
//
//   cue             -- AudioCueRef. Cues wrap one or more audio clips
//                       plus playback settings (volume, pitch, random,
//                       loop, pick mode). The asset registry auto-
//                       generates a 1-entry cue for every .wav/.mp3/
//                       .ogg/.flac it finds, so dragging an audio file
//                       onto this slot just works -- the editor
//                       resolves to the auto-cue under the hood.
//   volume          -- 0..1 per-source multiplier ON TOP of cue.volume.
//                       The cue carries the clip's intrinsic loudness;
//                       this lets one entity be quieter than another
//                       playing the same cue.
//   pitch           -- per-source multiplier on the cue's pitch.
//   pan             -- -1..+1 stereo pan, used for 2D sources only.
//   playing         -- non-zero = voice should be playing this frame.
//                       Toggling starts/stops; saved scenes remember
//                       per-source state.
//   autoplay        -- non-zero = automatically start on world load /
//                       component add. Independent of `playing`.
//   is_3d           -- non-zero = spatialise against the active listener
//                       using min/max_distance. Zero = 2D (master + pan).
//   min_distance    -- inside this radius (world units) the source plays
//                       at full volume. Default 1.0.
//   max_distance    -- outside this radius the source is silent. Default
//                       20.0. Must be >= min_distance.
//   spatial_blend   -- 0..1, lerps between pure 2D (0) and pure 3D (1).
//   bus             -- 0 = SFX, 1 = Music, 2 = UI, 3 = Voice.
struct AudioSource {
    Engine::AudioCueRef cue           = {};
    Engine::f32         volume        = 1.0f;
    Engine::f32         pitch         = 1.0f;
    Engine::f32         pan           = 0.0f;
    Engine::i32         playing       = 0;
    Engine::i32         autoplay      = 0;
    Engine::i32         is_3d         = 0;
    Engine::f32         min_distance  = 1.0f;
    Engine::f32         max_distance  = 20.0f;
    Engine::f32         spatial_blend = 1.0f;
    Engine::i32         bus           = 0;
    // Non-zero = the audio system destroys this entity when the voice
    // finishes (one-shot lifetime). Used by SpawnAudio / SpawnAudio3D
    // to fire-and-forget a cue without manual cleanup. Looped cues are
    // never auto-destroyed even with this set; the user has to stop
    // them explicitly.
    Engine::i32         auto_destroy  = 0;
};

// AudioListener: the "ears" of the scene. Typically attached to the
// camera or the player. Only the FIRST entity with `is_active = 1` is
// used per tick; further listeners are ignored (with a warning if the
// editor inspector flags them).
struct AudioListener {
    Engine::i32 is_active = 1;
};

}  // namespace Engine::components

ZUES_COMPONENT_FIELDS(Engine::components::AudioSource,
    cue, volume, pitch, pan, playing, autoplay,
    is_3d, min_distance, max_distance, spatial_blend, bus, auto_destroy);

ZUES_COMPONENT_FIELDS(Engine::components::AudioListener, is_active);
