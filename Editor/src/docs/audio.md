# Audio (2D + 3D)

Zues ships with a built-in audio mixer powered by **miniaudio**: a single
device, automatic format decoding (WAV / MP3 / OGG / FLAC), 2D + 3D
playback, and ECS-driven voice management. Drop a `.wav` into the asset
browser, attach an `AudioSource` to an entity, hit Play.

## AudioCues

`AudioSource` doesn't bind directly to a `.wav`. Instead it points at an
**AudioCue** -- a small `.zcue` asset that wraps one or more audio
files plus per-cue playback settings (volume, pitch, ± random, loop,
pick mode). At runtime the audio system loads the cue, picks one entry
(random for now), and plays it through miniaudio.

There are two ways to get cues:

* **Auto-generated.** The asset registry mints a 1-entry cue alongside
  every audio file (`coin.wav` -> `coin.wav.zcue`). The cue is hidden
  from the asset browser -- you see one `coin.wav` row, double-clicking
  it opens the cue editor with the entries list locked. Tweak volume /
  pitch / random / loop and the changes apply everywhere the cue is
  referenced.
* **User-authored.** Right-click in the asset browser -> Create -> AudioCue
  (or just drop a `.zcue` from disk) and you get a full cue you can fill
  with multiple audio files. Drag entries in, set the picker to `Random`,
  and every play picks a different variant. Great for footsteps, hits,
  generic SFX where one sound gets old fast.

Either way, `AudioSource.cue` is an `AudioCueRef`. Dropping a raw audio
file onto the slot resolves to its auto-cue automatically -- the
workflow feels like binding a sound directly, but it routes through the
cue pipeline so volume / pitch / random / loop all work uniformly.

## Components

### AudioSource

The "emitter." Attach to any entity; the audio system watches the
component each frame and runs a voice on its behalf.

| Field | What it controls |
| --- | --- |
| `cue`             | `AudioCueRef` -- drop a .zcue here (or a raw audio file; the editor resolves to its auto-cue). |
| `volume`          | Per-source multiplier on top of `cue.volume`. |
| `pitch`           | Per-source multiplier on top of `cue.pitch`. |
| `pan`             | -1 left .. +1 right. **2D sources only.** |
| `playing`         | 1 = voice is mixing this frame. Toggling starts/stops. |
| `autoplay`        | 1 = automatically start on world load / component add. |
| `is_3d`           | 1 = spatialise against the active listener with min/max range. |
| `min_distance`    | Inside this radius (world units) the source plays at full volume. |
| `max_distance`    | Outside this radius the source is silent (inverse model). |
| `spatial_blend`   | 0..1; lerps between 2D mix (0) and pure 3D (1). |
| `bus`             | 0=SFX, 1=Music, 2=UI, 3=Voice (informational for now). |

`loop` and the per-cue volume / pitch / random come from the cue, not
the source. Same cue assigned to two sources -> consistent intrinsic
loudness; per-source `volume` then nudges one quieter than the other.

Select an `AudioSource` and the Scene viewport draws the **min/max range
circles** in the Audio gizmo color so you can see falloff at a glance.
Toggle the Audio category off in `View → Debug Gizmos` to hide them.

### AudioListener

Marks "where the ears are." One active listener per scene. Typical
setup: attach to the camera entity. If no `AudioListener` is present,
the audio system falls back to the active `Camera2D`'s position so 3D
sounds still spatialise sensibly out of the box.

```
[Game]
  Camera (Camera2D + AudioListener)
  Player (Transform2D + Sprite + AudioSource{is_3d=1, clip=footsteps.wav})
  Music  (Transform2D + AudioSource{is_3d=0, clip=theme.ogg, autoplay=1, loop=1})
```

## Lync API

UFCS-friendly extern decls live in the prelude. No imports needed.

```lync
// Source-bound voices: e is an entity carrying AudioSource.
e.AudioPlay();              // start (or restart)
e.AudioStop();
e.AudioPause(true);
done: bool = e.AudioIsPlaying();

// Fire-and-forget by path. Returns a u32 voice handle (0 = failed);
// pass to AudioStopVoice if you want to cut it short. Path is
// resolved through the project's assets root so leading slashes
// aren't required.
voice: u32 = AudioPlayOneShot("audio/sfx/coin.wav", 1.0f, 1.0f);

// 3D one-shot at a world position. min/max distances define the
// inverse-attenuation envelope.
expl: u32 = AudioPlayOneShotAt("audio/sfx/boom.wav",
                               world_x, world_y,
                               2.0f, 25.0f, 1.0f);

// Master bus.
AudioSetMasterVolume(0.5f);
AudioMute(true);
```

The "AudioPlay / AudioStop / AudioIsPlaying" naming follows the engine's
PascalCase convention -- same rule as `PlayByName` on Animator. UFCS
makes the call read like a method on the entity.

## Editor workflow

1. **Import a clip.** Drop a `.wav` (or .mp3/.ogg/.flac) into the asset
   browser. The registry auto-mints a `.meta` sidecar AND an
   auto-generated cue (`<file>.zcue`) sitting next to the audio file
   (the cue itself stays hidden from the browser).
2. **Preview from the asset browser.** Each audio row gets a small
   **▶** button next to the filename -- click to audition without
   stopping anything else.
3. **Edit the cue.** Double-click the audio file to open its auto-cue
   in the **AudioCue** panel. Volume, pitch, random ±, and loop are
   editable; the entries list is locked because it's tied to the
   audio file. Hit **▶ Test** at the top to audition with the current
   settings + a fresh random pick.
4. **Author a multi-clip cue.** Right-click in the asset browser to
   create a `.zcue`, double-click to open it, then drag audio files
   onto the "Drop audio files here to add" zone. With pick mode set
   to Random, every play chooses a different entry. Great for
   footsteps / hits / "generic" SFX.
5. **Attach an AudioSource.** Add Component → `Engine/Audio` →
   AudioSource. Drop a cue (or a raw audio file -- it resolves to the
   auto-cue) onto the `cue` slot. The **▶** mini-button right of the
   slot plays a cue preview through the same path the runtime uses.
6. **Pick 2D vs 3D.** Toggle `is_3d`. The Scene viewport's range
   circles update live so you can dial in min/max distance by feel.
7. **Listener.** Add an `AudioListener` to the camera (or any entity
   whose position represents the player's ears). If there's none,
   the active `Camera2D`'s position is used as a fallback.
8. **Audio Mixer panel** (`View → Audio Mixer`) gives you a master
   slider, a mute toggle, and a live count of mixing voices + cached
   clips. Useful when iterating on a noisy emitter.

## Service-side (C++)

Anyone holding the `IAudio_v1` service vtable (id `zues.audio`,
version 1) can drive the mixer directly. Pattern mirrors the other
engine services -- see `Core/include/zues/services/audio.h`.

```cpp
auto* a = static_cast<IAudio_v1*>(
    services()->get_service(ZUES_SERVICE_AUDIO,
                             ZUES_SERVICE_AUDIO_VERSION));
if (a) {
    Engine::u32 clip = a->load_clip(a, "/abs/path/coin.wav");
    a->play_one_shot(a, clip, /*params=*/nullptr);
}
```

The service caches clips by path (deduped through miniaudio's resource
manager) so calling `load_clip("foo.wav")` 1000 times decodes once.

## Implementation notes

- **One ma_engine per process.** Both editor and runtime construct a
  single `Engine::host::AudioSystem` at startup; it owns the device,
  registers `IAudio_v1`, and installs an ECS system in `PreUpdate`
  (domain Both) that walks `AudioSource` + `AudioListener` each frame.
- **Edit-mode preview works.** The audio system runs in both Edit and
  Play modes, so toggling `playing` on an `AudioSource` in the
  inspector starts the voice immediately -- no Play required.
- **Automatic cleanup.** One-shots that finish naturally are reaped
  every tick. Source-bound voices are torn down when the source's
  `playing` goes false or the entity is destroyed.
- **No file mutability requirement.** Audio assets are loaded read-
  only through miniaudio's resource manager; nothing rewrites the
  source file.

## Limitations (today)

- Pick mode is currently `Random` only. `Sequential`, `RoundRobin`,
  and weighted picks are next on the list.
- No per-bus volume sliders yet (the `bus` field is informational; a
  future Audio Mixer slice will add SFX / Music / UI / Voice).
- `spatial_blend` is treated as a hard 0/1 cutoff at 0.5 internally;
  fractional blends are coming with miniaudio's spatializer hookup.
- No reverb / DSP graph yet. miniaudio supports it; not surfaced.
