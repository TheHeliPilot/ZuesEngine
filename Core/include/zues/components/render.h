#pragma once

// Render-related component types. Type definitions live in Core (so any
// module + the editor + user code can read them). The systems that consume
// these types live in the renderer module / editor.
//
// Sorting fields (`layer`, `order`) live ON the renderable components
// (Sprite, Text) directly — Unity-style. There is no separate RenderLayer
// component: an entity that has Sprite has its own sort key, end of story.
// `layer` is the primary partition; `order` tie-breaks within a layer.

#include <zues/api.h>
#include <zues/asset.h>
#include <zues/types.h>
#include <zues/ecs/reflection.h>
#include <zues/math/vec2.h>
#include <zues/math/color.h>

namespace Engine::components {

// Opaque asset handles. The renderer service owns the asset registry; these
// just identify a slot. Generation lets the renderer detect stale handles.
using TextureHandle = Engine::Handle;
using FontHandle    = Engine::Handle;

// Sort behaviour applied to entities visible by a Camera2D.
enum class SortMode : Engine::u32 {
    OrderOnly,      // sort by Sprite.layer/order alone (UI, side-scrollers)
    YDescending,    // top-down 2.5D — lower y draws first, higher y on top
    YAscending,     // less common (e.g. isometric where lower y is "front")
};

struct Camera2D {
    // Vertical world-units visible at "normal" framing. Pixels-per-unit at
    // render time = viewport_h / ortho_size, so the same world area is
    // visible regardless of viewport pixel size.
    float    ortho_size = 10.0f;
    SortMode sort_mode  = SortMode::OrderOnly;
    bool     is_active  = true;
};

struct Sprite {
    TextureHandle       texture = {};
    Engine::math::vec2  size    = {1.0f, 1.0f};
    Engine::math::vec2  pivot   = {0.5f, 0.5f};   // 0..1 within sprite
    Engine::math::color tint    = Engine::math::color::white();
    bool                flip_x  = false;
    bool                flip_y  = false;
    Engine::i32         layer   = 0;              // sorting layer
    Engine::i32         order   = 0;              // tiebreaker within layer
    // Sub-rect of the texture this sprite renders. Pixels in the
    // texture's own coordinate space, with (0,0) at the top-left.
    // All zeros means "render the whole texture" (the renderer treats
    // a zero-w/h rect as a sentinel). Non-zero rects are typically
    // written by the Animator system from the texture's slice array.
    Engine::i32         slice_x = 0;
    Engine::i32         slice_y = 0;
    Engine::i32         slice_w = 0;
    Engine::i32         slice_h = 0;
    // 9-slice borders in source pixels (copied from the slice's
    // SpriteAssetSettings entry by the slice picker). All zeros means
    // "no 9-slice"; the renderer takes a one-quad fast path.
    Engine::i32         border_l = 0;
    Engine::i32         border_r = 0;
    Engine::i32         border_t = 0;
    Engine::i32         border_b = 0;
    // 0=Stretch, 1=Tile, 2=TileFit. Stored as i32 so the field appears
    // as a regular int in reflection -- the inspector hides this row
    // and renders a labelled combo from the slice picker instead.
    Engine::i32         scale_mode  = 0;   // applied to T/B/L/R edges
    Engine::i32         center_mode = 0;   // applied to the center region
    // Asset's PPU at the time the slice was assigned. Tiled 9-slice
    // needs an absolute "one source pixel = N screen pixels" ratio
    // that doesn't change when the user stretches the sprite -- the
    // renderer derives that ratio as (camera_ppu / texture_ppu). The
    // slice picker copies the value from the texture's .meta; old
    // sprites without it default to 100 and behave like Stretch.
    Engine::f32         texture_ppu = 100.0f;
};

struct Text {
    FontHandle           font      = {};
    char                 utf8[256] = {};
    float                size_px   = 16.0f;
    Engine::math::color  color     = Engine::math::color::white();
    Engine::u8           h_align   = 0;          // 0 = left, 1 = center, 2 = right
    Engine::i32          layer     = 0;
    Engine::i32          order     = 0;
};

// Attach to an entity to render it in SCREEN SPACE (HUD overlay) instead
// of world space. Pairs with Sprite (HUD icons) or Text (score, lives,
// dialogue boxes). The render system skips screen-space entities when
// drawing the main world pass, then runs a second pass after the camera
// composite that projects UIAnchor positions directly to viewport pixels.
//
//   anchor      0..1 normalized point on the viewport. (0,0) = top-left,
//               (1,1) = bottom-right, (0.5,0.5) = center. Choose where
//               the entity STICKS to as the viewport resizes.
//   pixel_offset offset in raw pixels from the anchor point. Use this to
//               nudge an element a few pixels off the corner without
//               relying on aspect-ratio math.
//   pivot       0..1 within the rendered element. (0,0) = its top-left
//               aligns with the anchor; (0.5,0.5) = its center; etc.
struct UIAnchor {
    Engine::math::vec2 anchor       = {0.5f, 0.5f};
    Engine::math::vec2 pixel_offset = {0.0f, 0.0f};
    Engine::math::vec2 pivot        = {0.5f, 0.5f};
};

// Drives a sibling Sprite component's `texture` and UV rect from a
// .zanim asset. The animator system advances `time` each frame, picks
// the right frame, writes the texture handle + slice rect into the
// Sprite. No allocation per frame.
//
// `animation` is the .zanim asset to play; assigning a new value
// resets `time = 0` and `current = 0` (handled by the system).
//
// `time_scale` is applied to the per-frame dt: 1 = normal speed,
// 0 = paused, 2 = double speed, -1 = play backwards.
// Maximum size of the inline clip table (newline-separated rows of
// "name<TAB>guid_hex"). Sized for ~16 clips of 30-char names.
inline constexpr Engine::u32 ANIMATOR_CLIPS_BUF = 1024;

struct Animator {
    // Resolved guid of the currently-playing clip. Driven by the
    // editor's clip picker / the animator system. Persisted so a
    // saved scene plays the same clip on load.
    Engine::AnimationRef animation = {};
    float                time       = 0.0f; // accumulated, modulo total length
    int                  current    = 0;    // current clip index in `clips`
    bool                 playing    = true;
    float                time_scale = 1.0f;
    // Inline named-clip table. Each row: "<name>\t<guid_hex>\n". The
    // inspector parses + edits + re-encodes; the runtime resolves a
    // play-by-name request through the same parse. Empty rows tolerated.
    // Stored as a CharBuffer so reflection serializes it as a string
    // -- no special-cased component serialiser required.
    char                 clips[ANIMATOR_CLIPS_BUF] = {};
};

// =============================================================================
// Particles -- unified VFX + swarm component (slice 1: data model only).
// =============================================================================
//
// Design overview (see /docs/particles.md once the runtime exists):
//
// One component handles BOTH small VFX puffs (smoke, sparks) and massive
// game-logic swarms (Total War-style soldiers, bullet hells). The
// distinction is feature flags (Profile + module toggles), not separate
// components. Same compute pipeline + same instanced render path; the
// only thing that changes per emitter is which subsystems are active.
//
// Execution tier (`tier`):
//   0 = Light  (CPU, single-thread, up to ~10k -- predictable + debuggable)
//   1 = Medium (CPU jobs, parallel, up to ~100k -- deterministic)
//   2 = Heavy  (GPU compute, up to ~1M -- non-deterministic, no per-agent CPU)
//
// Profile (`profile`) sets reasonable defaults for the toggles below.
// Users CAN override any toggle after picking a profile.
//   0 = VFX        -- lifetime curves on, spatial grid off, events off
//   1 = Swarm      -- spatial grid on, steering on, events on, curves off
//   2 = Custom     -- nothing implied; user picks every flag manually
//
// All data here is editor-authored config + CPU-visible aggregates.
// The actual per-particle SoA arrays live OFF this component (in the
// runtime particle system's heap / GPU buffers). This struct stays
// small + reflectable + serializable.

inline constexpr Engine::u32 PARTICLES_NAME_LEN = 32;

struct Particles {
    // ---- Identity ------------------------------------------------------
    char         name[PARTICLES_NAME_LEN] = {};   // for debug overlays
    // 0=Light(CPU), 1=Medium(jobs), 2=Heavy(GPU). Editor inspector
    // renders this as a combo. Switching tier mid-play resets the
    // particle pool because the storage backend changes.
    Engine::i32  tier    = 0;
    // 0=VFX, 1=Swarm, 2=Custom. Inspector preset; choosing a non-Custom
    // value flips the toggles below to sensible defaults but doesn't
    // lock them.
    Engine::i32  profile = 0;

    // ---- Capacity ------------------------------------------------------
    // Hard cap on live particles. Pool is allocated once at this size
    // (or on tier change). Going over caps spawning silently.
    Engine::i32  max_particles = 1024;

    // ---- Emission ------------------------------------------------------
    // Continuous emission. `rate` is particles per second; 0 = bursts only.
    Engine::f32  rate = 32.0f;
    // One-off bursts: emit `burst_count` at `burst_time` (seconds since
    // emitter start), repeat every `burst_period` seconds (0 = once).
    Engine::f32  burst_time   = 0.0f;
    Engine::i32  burst_count  = 0;
    Engine::f32  burst_period = 0.0f;

    // ---- Spawn shape ---------------------------------------------------
    // 0=Point, 1=Circle, 2=Rect, 3=Edge, 4=Ring. Combined with `shape_w/h`.
    Engine::i32  shape         = 0;
    Engine::f32  shape_w       = 1.0f;
    Engine::f32  shape_h       = 1.0f;
    // Where in the shape new particles spawn.
    // 0=Volume (anywhere inside), 1=Edge (on boundary), 2=Random
    Engine::i32  shape_distribution = 0;

    // ---- Initial velocity ---------------------------------------------
    Engine::math::vec2 initial_velocity = {0.0f, 0.0f};
    // Random spread added to initial velocity (uniform per-component).
    Engine::math::vec2 velocity_random  = {0.0f, 0.0f};

    // ---- Per-particle defaults ----------------------------------------
    Engine::f32         lifetime         = 1.0f;     // seconds
    Engine::f32         lifetime_random  = 0.0f;     // +/- range
    Engine::f32         start_size       = 1.0f;     // world units
    Engine::f32         end_size         = 1.0f;     // 0 = same as start
    Engine::math::color start_color      = Engine::math::color::white();
    Engine::math::color end_color        = Engine::math::color::white();

    // ---- Forces -------------------------------------------------------
    Engine::math::vec2  gravity          = {0.0f, 0.0f};
    Engine::f32         drag             = 0.0f;     // velocity *= (1 - drag*dt)

    // ---- Visual -------------------------------------------------------
    TextureHandle       texture          = {};
    Engine::i32         layer            = 0;
    Engine::i32         order            = 0;
    // 0=Alpha, 1=Additive, 2=Multiply. Maps to renderer blend states.
    Engine::i32         blend_mode       = 0;

    // ---- Module toggles (set by Profile, overridable) -----------------
    // Game-side features. Off by default for VFX; on for Swarm.
    Engine::i32  use_spatial_grid     = 0;   // enable neighbor queries
    Engine::i32  use_steering         = 0;   // cohesion / alignment / avoid
    Engine::i32  use_events           = 0;   // append death/hit events to CPU buffer
    Engine::i32  use_collision        = 0;   // SDF collision against world

    // ---- Steering (used when use_steering = 1) ------------------------
    Engine::f32  steer_seek_target_x  = 0.0f;
    Engine::f32  steer_seek_target_y  = 0.0f;
    Engine::f32  steer_seek_weight    = 0.0f;
    Engine::f32  steer_avoid_radius   = 0.5f;
    Engine::f32  steer_avoid_weight   = 0.0f;
    Engine::f32  steer_align_weight   = 0.0f;
    Engine::f32  steer_cohesion_weight = 0.0f;

    // ---- Lifecycle ----------------------------------------------------
    // Seconds since this emitter started. The runtime updates this so
    // bursts + duration logic survive save/load.
    Engine::f32  age            = 0.0f;
    // Stop spawning new particles after this many seconds (-1 = forever).
    Engine::f32  duration       = -1.0f;
    // Auto-restart loop. Continuous emitters typically loop=true; bursts
    // off (one-shot). Subject to `duration`.
    Engine::i32  loop           = 1;
    Engine::i32  playing        = 1;

    // ---- User-defined per-particle scratch fields --------------------
    // Eight named float slots whose semantics the USER decides.
    // Names are typed in the Inspector; Lync code reads/writes them
    // by name via ParticleGet/ParticleSet. Stored as one newline-
    // separated string for native CharBuffer serialization.
    char         extra_names[256] = {};

    // ---- Diagnostics (read-only from the inspector) -------------------
    // Live count this frame. Set by the runtime; serialized purely so
    // the inspector reads a stable value across the world load gap.
    Engine::i32  live_count     = 0;
};

}  // namespace Engine::components

ZUES_REGISTER_ENUM(Engine::components::SortMode,
    ZUES_ENUM_OPTION("OrderOnly",   0),
    ZUES_ENUM_OPTION("YDescending", 1),
    ZUES_ENUM_OPTION("YAscending",  2));

ZUES_COMPONENT_FIELDS(Engine::components::Camera2D, ortho_size, sort_mode, is_active);
ZUES_COMPONENT_FIELDS(Engine::components::Sprite,   texture, size, pivot, tint, flip_x, flip_y, layer, order, slice_x, slice_y, slice_w, slice_h, border_l, border_r, border_t, border_b, scale_mode, center_mode, texture_ppu);
ZUES_COMPONENT_FIELDS(Engine::components::Text,     font, utf8, size_px, color, h_align, layer, order);
ZUES_COMPONENT_FIELDS(Engine::components::UIAnchor, anchor, pixel_offset, pivot);
ZUES_COMPONENT_FIELDS(Engine::components::Animator, animation, time, current, playing, time_scale, clips);
ZUES_COMPONENT_FIELDS(Engine::components::Particles,
    name, tier, profile, max_particles,
    rate, burst_time, burst_count, burst_period,
    shape, shape_w, shape_h, shape_distribution,
    initial_velocity, velocity_random,
    lifetime, lifetime_random, start_size, end_size, start_color, end_color,
    gravity, drag,
    texture, layer, order, blend_mode,
    use_spatial_grid, use_steering, use_events, use_collision,
    steer_seek_target_x, steer_seek_target_y, steer_seek_weight,
    steer_avoid_radius, steer_avoid_weight,
    steer_align_weight, steer_cohesion_weight,
    age, duration, loop, playing,
    extra_names, live_count);
