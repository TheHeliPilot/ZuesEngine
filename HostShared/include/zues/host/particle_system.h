#pragma once

// Default particle system installed by the editor / runtime. Drives every
// Particles component each frame.
//
// Slice 2 scope (this version):
//   - Light tier (CPU, single-thread) only.
//   - Each emitter gets a per-instance heap pool sized to max_particles.
//   - Continuous spawn (rate) + scheduled bursts (count, period).
//   - Per-particle integrate (gravity, drag), age, lifetime kill.
//   - Color + size lerp over life.
//   - Renders through IRenderer_2D_v1::draw_sprite_rot, one call per
//     live particle.
//
// Future slices: Medium (CPU jobs), Heavy (GPU compute), spatial grid +
// steering, SDF collision, command buffer + event readback for swarms.

#include <zues/api.h>
#include <zues/ecs/world.h>

struct IRenderer_2D_v1;

namespace Engine::host {

struct ParticleSystem {
    ecs::SystemHandle update_handle{};
    ecs::SystemHandle render_handle{};
    // Editor-only preview ticker: when the user has an entity with a
    // Particles component selected, the editor sets it here each
    // frame. The Editor-domain preview system ticks JUST that one
    // entity so it animates in the Scene viewport without entering
    // Play mode. NULL_ENTITY (default) = no preview, no work.
    ecs::SystemHandle preview_handle{};

    bool register_into(ecs::World& world, ::IRenderer_2D_v1* renderer);
    void unregister_from(ecs::World& world);

    // Editor sets this each frame from the Inspector's selected entity
    // (or to a null entity when nothing relevant is selected). Safe to
    // call from anywhere; the update side reads it on the next tick.
    void set_preview_entity(ecs::Entity e);
};

// ---- Lync-host accessors (called by host_api thunks) ------------------
// These reach into the running ParticleSystem's per-entity pool by
// entity id. Out-of-bounds slot indexes / unknown emitter entities /
// no-pool-allocated cases all no-op safely. NOT thread-safe; meant to
// be called from the main loop or from Game-domain Lync systems
// running in PreUpdate.
namespace particles_api {

int   count    (ecs::Entity e);
void  get_pos  (ecs::Entity e, int idx, float* out_x, float* out_y);
void  set_pos  (ecs::Entity e, int idx, float x, float y);
void  get_vel  (ecs::Entity e, int idx, float* out_vx, float* out_vy);
void  set_vel  (ecs::Entity e, int idx, float vx, float vy);
// Resolve `field` against the emitter's `extra_names` to pick which
// of the 8 scratch slots to read/write. Returns 0 / no-ops on miss.
float get_field(ecs::Entity e, int idx, const char* field);
void  set_field(ecs::Entity e, int idx, const char* field, float value);
// Swap-remove a particle (drops it from the live segment).
void  kill     (ecs::Entity e, int idx);

// Spatial nearest-neighbor query on `target_emitter`. Lazy spatial
// grid is rebuilt the first time this is hit per frame. Returns -1
// when no live particle is within `max_radius` of (x, y).
int   nearest_neighbor(ecs::Entity target_emitter, float x, float y,
                       float max_radius);
// Move particle `idx` of `emitter` toward (target_x, target_y) at
// max_speed. Returns 1 if reached this tick (within 1e-3 units).
int   step_toward(ecs::Entity e, int idx,
                  float target_x, float target_y,
                  float max_speed, float dt);
// Iterate live particles, calling cb per element. Skips the per-call
// pool lookup that ParticleGet/Pos/etc would otherwise do.
using EachFn = void(*)(ecs::Entity emitter, int idx, float dt, void* user);
void  for_each(ecs::Entity emitter, EachFn cb, float dt, void* user);

// Internal: clear the thread-local pool-pointer cache. Call before
// any pool erase so subsequent lookups don't return a dangling ptr.
void  invalidate_pool_cache();

// Raw pointer slices into a pool's SoA arrays. Lets Lync (or any C
// extern) read/write per-particle data with ZERO host-call overhead
// inside the hot loop -- just pointer arithmetic. The pointers are
// valid until the pool is resized (max_particles change) or erased
// (entity destroy / preview deselect / Play↔Stop). Treat them as
// frame-local. Returns nullptr if the emitter has no pool yet.
//
// Layout: float* per scalar (px / py / vx / vy / age / lifetime)
// and per named extra slot. Each array length = ParticleCount(e).
float* slice_px   (ecs::Entity e);
float* slice_py   (ecs::Entity e);
float* slice_vx   (ecs::Entity e);
float* slice_vy   (ecs::Entity e);
float* slice_age  (ecs::Entity e);
float* slice_field(ecs::Entity e, const char* field);

}  // namespace particles_api

}  // namespace Engine::host
