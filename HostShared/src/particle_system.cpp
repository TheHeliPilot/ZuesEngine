#include <zues/host/particle_system.h>

#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/engine.h>
#include <zues/host/task_runner.h>
#include <zues/log.h>
#include <zues/service.h>
#include <zues/services/debug_draw.h>
#include <zues/services/render_camera.h>
#include <zues/services/renderer_2d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

// CPU-tier particle pool + sim.
//
// Particle data lives OFF the component (which stays small + reflectable).
// Pool keyed by entity id -- when an entity is destroyed or its component
// removed, the pool is reaped lazily on the next sweep. SoA storage so a
// future SIMD pass can vectorize integrate/age cheaply. For now scalar
// loops are clear enough.

namespace Engine::host {

namespace {

// Uniform 2D spatial grid for O(neighbors) nearest-particle queries.
// Cells keyed by (ix, iy) integer coords -- unbounded, no precomputed
// extents. Built per-frame on demand the first time anything queries
// a given pool, invalidated when the pool's `count` or `frame_id`
// changes between rebuilds. Each cell holds slot indexes into the
// pool's parallel arrays.
struct SpatialGrid {
    float cell_size = 1.0f;
    // Pack (ix, iy) -> uint64 for the hashmap key. Each cell index is
    // shifted +32768 first so negative coords stay non-negative.
    std::unordered_map<u64, std::vector<int>> cells;
    // Generation counter -- bumped on every rebuild. Outside callers
    // (e.g. ParticleNearestNeighbor host fn) compare to know if the
    // grid needs rebuild before query.
    u32 build_gen = 0;
    int build_for_count = -1;     // count at last build

    static inline u64 key(int ix, int iy) {
        return ((u64)(u32)(ix + 32768) << 32) | (u32)(iy + 32768);
    }
    inline void cell_of(float x, float y, int& ix, int& iy) const {
        const float inv = 1.0f / std::max(0.001f, cell_size);
        ix = (int)std::floor(x * inv);
        iy = (int)std::floor(y * inv);
    }
};

struct Pool {
    // Per-particle state, parallel SoA arrays. Swap-remove storage:
    // live particles always occupy slots [0, count). Slot allocation
    // is O(1) (push to count); death is O(1) (swap with count-1, then
    // count--). No live[] flag needed, no holes to skip in iteration.
    // Critical at scale -- a linear find_free_slot scan was the
    // dominant cost above ~50k particles.
    std::vector<float> px,  py;
    std::vector<float> vx,  vy;
    std::vector<float> age, lifetime;
    std::vector<float> size_start, size_end;
    std::vector<float> r0, g0, b0, a0;     // start color
    std::vector<float> r1, g1, b1, a1;     // end color
    std::vector<float> rand_seed;          // 0..1 per particle, stable for the life

    // User-defined per-particle scratch slots. Eight parallel float
    // arrays addressable by the names in Particles::extra_names. Lync
    // (or future C++ behavior modules) read/write these via name
    // lookup. Allocated to `capacity` regardless of whether the user
    // names them -- 8 * sizeof(float) per particle is cheap.
    static constexpr int EXTRA_SLOTS = 8;
    std::array<std::vector<float>, EXTRA_SLOTS> extra;

    // Lazy spatial index. Built/refreshed by rebuild_grid_if_stale()
    // before every host-API spatial query and once per integrate tick
    // when use_steering is on (for the avoidance pass).
    SpatialGrid grid;
    u32         frame_seen = 0;     // last sim-frame that touched count

    // Field-name -> extra slot cache. Built lazily the first time
    // `field_slot_cached` is hit; invalidated by `field_cache_stamp`
    // (a hash of extra_names). Saves the per-call string-walk that
    // dominates Lync swarm AI cost at scale.
    std::unordered_map<std::string, int> field_to_slot;
    u32                                  field_cache_stamp = 0;

    // Spawn accounting. Carries fractional emissions across frames so
    // a 0.5 particle/sec rate still emits exactly one every two seconds
    // instead of being floor()'d to 0.
    float spawn_accum    = 0.0f;
    // Scheduled burst counter -- next burst time we'll honour.
    float next_burst_at  = 0.0f;
    // Cap mirrored from the component so we can detect a resize.
    int   capacity       = 0;
    // Live particle count (swap-remove front-segment).
    int   count          = 0;

    void resize(int n) {
        capacity = n;
        count    = 0;
        px.assign(n, 0.0f);   py.assign(n, 0.0f);
        vx.assign(n, 0.0f);   vy.assign(n, 0.0f);
        age.assign(n, 0.0f);  lifetime.assign(n, 1.0f);
        size_start.assign(n, 1.0f); size_end.assign(n, 1.0f);
        r0.assign(n, 1.0f);   g0.assign(n, 1.0f);
        b0.assign(n, 1.0f);   a0.assign(n, 1.0f);
        r1.assign(n, 1.0f);   g1.assign(n, 1.0f);
        b1.assign(n, 1.0f);   a1.assign(n, 1.0f);
        rand_seed.assign(n, 0.0f);
        for (auto& a : extra) a.assign(n, 0.0f);
        spawn_accum = 0.0f;
        next_burst_at = 0.0f;
    }

    // Swap slot `dst` with the LAST live slot, then shrink. Caller
    // must NOT advance `dst` after the swap -- the new occupant of
    // dst still needs processing this frame.
    inline void swap_remove(int dst) {
        const int last = count - 1;
        if (dst != last) {
            px[dst] = px[last];   py[dst] = py[last];
            vx[dst] = vx[last];   vy[dst] = vy[last];
            age[dst] = age[last]; lifetime[dst] = lifetime[last];
            size_start[dst] = size_start[last];
            size_end[dst]   = size_end[last];
            r0[dst] = r0[last]; g0[dst] = g0[last];
            b0[dst] = b0[last]; a0[dst] = a0[last];
            r1[dst] = r1[last]; g1[dst] = g1[last];
            b1[dst] = b1[last]; a1[dst] = a1[last];
            rand_seed[dst]    = rand_seed[last];
            for (auto& a : extra) a[dst] = a[last];
        }
        --count;
    }

    // Reserve the next free slot. Returns -1 if at capacity.
    inline int alloc() {
        if (count >= capacity) return -1;
        return count++;
    }

    // Rebuild the spatial grid if stale (count changed since the last
    // build, or this is the first build this frame). Cheap when
    // already up-to-date.
    void rebuild_grid_if_stale(float cell_size, u32 frame_id) {
        if (cell_size <= 0.0f) cell_size = 1.0f;
        if (grid.build_for_count == count &&
            grid.build_gen == frame_id &&
            std::fabs(grid.cell_size - cell_size) < 1e-4f) {
            return;
        }
        grid.cell_size = cell_size;
        grid.cells.clear();
        for (int i = 0; i < count; ++i) {
            int ix, iy;
            grid.cell_of(px[i], py[i], ix, iy);
            grid.cells[SpatialGrid::key(ix, iy)].push_back(i);
        }
        grid.build_for_count = count;
        grid.build_gen = frame_id;
    }

    // Nearest live particle to (qx, qy) within max_radius. Returns -1
    // on miss. Scans the (qx, qy) cell + its 8 neighbors -- caller is
    // responsible for choosing a cell_size that contains max_radius.
    int nearest(float qx, float qy, float max_radius) const {
        const float r2 = max_radius * max_radius;
        int  best   = -1;
        float bestd = r2;
        int qix, qiy;
        grid.cell_of(qx, qy, qix, qiy);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = grid.cells.find(
                    SpatialGrid::key(qix + dx, qiy + dy));
                if (it == grid.cells.end()) continue;
                for (int idx : it->second) {
                    const float ex = px[idx] - qx;
                    const float ey = py[idx] - qy;
                    const float d2 = ex*ex + ey*ey;
                    if (d2 < bestd) { bestd = d2; best = idx; }
                }
            }
        }
        return best;
    }
};

struct SystemCtx {
    ::IRenderer_2D_v1*  renderer    = nullptr;
    ::IRenderCamera_v1* camera_svc  = nullptr;
    ::IDebugDraw_v1*    dbg_svc     = nullptr;
    ecs::World*         world       = nullptr;
    ecs::ComponentId    xform_id    = 0;
    ecs::ComponentId    parts_id    = 0;
    // Per-entity pool (keyed by encoded entity id).
    std::unordered_map<u64, Pool> pools;
    // Shared RNG -- not deterministic across runs (Heavy tier won't
    // be either, by design).
    std::mt19937 rng{std::random_device{}()};

    // Camera snapshot, refreshed at the top of each render pass. Used
    // to convert world-space particle positions to screen-space pixels
    // for the immediate-mode draw_sprite_rot calls below.
    float cam_pan_x = 0.0f, cam_pan_y = 0.0f;
    float cam_ppu   = 100.0f;
    float view_cx   = 0.0f, view_cy = 0.0f;

    // Editor preview target. Updated each frame by the editor; the
    // Editor-domain preview system ticks ONLY this entity so users
    // see emitters play while they're selected in the Inspector,
    // without having to enter Play mode.
    ecs::Entity preview_entity{};
    // Last preview target observed by the preview tick. When the
    // user deselects (or selects a different emitter), we drop the
    // previous one's pool so a re-select starts from zero rather
    // than picking up where the previous preview left off.
    ecs::Entity last_preview_entity{};

    // Last tick mode we saw. When this transitions (Edit -> Play or
    // Play -> Edit), every Particles pool is dropped so a fresh Play
    // session doesn't inherit the dying state of the previous one
    // (or vice versa). The editor's snapshot/restore handles entity
    // + component data, but pools live OFF the component in our heap
    // and need their own reset hook.
    ecs::TickMode last_tick_mode = ecs::TickMode::Edit;
};
SystemCtx g_ctx{};

inline u64 ent_key(ecs::Entity e) {
    return (u64(e.generation) << 32) | u64(e.index);
}

inline float urand(SystemCtx& c) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(c.rng);
}
inline float srand(SystemCtx& c) {
    return std::uniform_real_distribution<float>(-1.0f, 1.0f)(c.rng);
}

// Sample a 2D point inside the emitter's spawn shape. Returns the
// offset in WORLD UNITS relative to the emitter entity's transform.
void sample_shape(const components::Particles& p, SystemCtx& ctx,
                   float& ox, float& oy) {
    const float w = std::max(0.0f, p.shape_w);
    const float h = std::max(0.0f, p.shape_h);
    switch (p.shape) {
        default: case 0:                       // Point
            ox = 0.0f; oy = 0.0f; return;
        case 1: {                              // Circle
            // shape_distribution: 0=Volume, 1=Edge, 2=Random
            const float r = (w + h) * 0.5f;
            const float t = urand(ctx) * 6.28318530718f;
            const float rr = (p.shape_distribution == 1)
                              ? r : r * std::sqrt(urand(ctx));
            ox = std::cos(t) * rr;
            oy = std::sin(t) * rr;
            return;
        }
        case 2:                                // Rect
            if (p.shape_distribution == 1) {
                // Edge: pick one of four sides uniformly.
                const int side = (int)(urand(ctx) * 4.0f) & 3;
                const float u = urand(ctx);
                if      (side == 0) { ox = -w*0.5f + u*w; oy = -h*0.5f; }
                else if (side == 1) { ox = -w*0.5f + u*w; oy =  h*0.5f; }
                else if (side == 2) { ox = -w*0.5f;       oy = -h*0.5f + u*h; }
                else                 { ox =  w*0.5f;       oy = -h*0.5f + u*h; }
            } else {
                ox = srand(ctx) * w * 0.5f;
                oy = srand(ctx) * h * 0.5f;
            }
            return;
        case 3: {                              // Edge (horizontal line)
            ox = srand(ctx) * w * 0.5f;
            oy = 0.0f;
            return;
        }
        case 4: {                              // Ring (alias of circle-edge)
            const float r = (w + h) * 0.5f;
            const float t = urand(ctx) * 6.28318530718f;
            ox = std::cos(t) * r;
            oy = std::sin(t) * r;
            return;
        }
    }
}

// Spawn one particle at slot `i` for emitter `p`, world position (ex, ey).
void spawn_one(int i, components::Particles& p, SystemCtx& ctx,
                float ex, float ey) {
    Pool& pool = ctx.pools[ent_key({0, 0})]; (void)pool;  // unused; pool resolved by caller
    // (Caller passes the actual Pool& -- we only need ctx for RNG. Refactored below.)
}

// Sentinel marking a dead slot during the parallel integrate pass.
// Picked as a hugely negative age that no real frame will ever
// produce (we'd need a >2-billion-second clip).
constexpr float DEAD_SENTINEL = -1.0e9f;

// Integrate a sub-range. SAFE to call from a worker thread; only
// touches per-particle SoA cells and reads scalar emitter data.
inline void integrate_range(components::Particles& p, Pool& pool,
                             int begin, int end,
                             float dt, float damp_per_dt) {
    const float gx = p.gravity.x;
    const float gy = p.gravity.y;
    for (int i = begin; i < end; ++i) {
        pool.age[i] += dt;
        if (pool.age[i] >= pool.lifetime[i]) {
            pool.age[i] = DEAD_SENTINEL;
            continue;
        }
        pool.vx[i] += gx * dt;
        pool.vy[i] += gy * dt;
        pool.vx[i] *= damp_per_dt;
        pool.vy[i] *= damp_per_dt;
        pool.px[i] += pool.vx[i] * dt;
        pool.py[i] += pool.vy[i] * dt;
    }
}

// Native per-particle repulsion. When use_steering is on AND
// steer_avoid_radius / steer_avoid_weight are positive, push each
// particle away from neighbors within radius. Uses the pool's grid
// (rebuilt by the caller before invoking). O(N * k) where k is the
// average neighbor count per cell -- way cheaper than O(N^2) at scale.
//
// "Native" here means: no Lync callbacks, no host-API roundtrips.
// Just pointer math on the SoA arrays. This is the per-soldier
// "interferance" the user wants -- crowds spread out instead of
// stacking on the same world point.
inline void apply_repulsion_range(const components::Particles& p, Pool& pool,
                                   int begin, int end, float dt) {
    const float radius = p.steer_avoid_radius;
    const float weight = p.steer_avoid_weight;
    if (radius <= 0.0f || weight <= 0.0f) return;
    const float r2 = radius * radius;
    for (int i = begin; i < end; ++i) {
        const float mx = pool.px[i];
        const float my = pool.py[i];
        int qix, qiy;
        pool.grid.cell_of(mx, my, qix, qiy);
        float push_x = 0.0f, push_y = 0.0f;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = pool.grid.cells.find(
                    SpatialGrid::key(qix + dx, qiy + dy));
                if (it == pool.grid.cells.end()) continue;
                for (int j : it->second) {
                    if (j == i) continue;
                    const float ex = mx - pool.px[j];
                    const float ey = my - pool.py[j];
                    const float d2 = ex*ex + ey*ey;
                    if (d2 < r2 && d2 > 1e-6f) {
                        // Force scales linearly with how-close-they-are
                        // so distant neighbors barely contribute.
                        const float falloff = 1.0f - std::sqrt(d2) / radius;
                        const float inv = 1.0f / std::sqrt(d2);
                        push_x += ex * inv * falloff;
                        push_y += ey * inv * falloff;
                    }
                }
            }
        }
        // Apply directly to position (positional avoidance) so it
        // doesn't fight the per-frame velocity reset patterns in
        // Lync AI. weight is in world-units / sec at saturation.
        const float step = weight * dt;
        pool.px[i] += push_x * step;
        pool.py[i] += push_y * step;
    }
}

// Single-threaded compaction pass: walk the slot range, swap-remove
// any sentinel-marked dead slots. Cheap relative to the integrate
// itself even at 1M+ because each particle is touched once.
inline void compact_dead(Pool& pool) {
    for (int i = 0; i < pool.count; ) {
        if (pool.age[i] == DEAD_SENTINEL) pool.swap_remove(i);
        else                              ++i;
    }
}

void integrate_and_render(components::Particles& p,
                           Pool& pool,
                           float dt,
                           SystemCtx* ctx,
                           bool do_render) {
    auto* r = ctx ? ctx->renderer : nullptr;
    const float damp_per_dt = 1.0f - std::clamp(p.drag * dt, 0.0f, 1.0f);

    // Tier-aware integrate. Light: single-thread inline. Medium /
    // Heavy: parallel via the task runner (Heavy is data-only on the
    // CPU today; falls through to Medium until slice 4 lands compute).
    if (p.tier == 0) {
        integrate_range(p, pool, 0, pool.count, dt, damp_per_dt);
    } else {
        // ~4k slots per worker chunk -- empirically a good balance
        // between scheduling overhead (smaller chunks lose to dispatch)
        // and load imbalance (larger chunks waste cores at the tail).
        TaskRunner::instance().parallel_for(
            0, pool.count, 4096,
            [&p, &pool, dt, damp_per_dt](int b, int e) {
                integrate_range(p, pool, b, e, dt, damp_per_dt);
            });
    }
    compact_dead(pool);

    // Native per-particle repulsion (the "soldier interferance" the
    // user asked for). Runs after integrate so velocities are settled
    // before the positional push. Cell size derived from avoid_radius
    // so the 3x3 cell scan covers exactly the influence zone.
    if (p.use_steering && p.steer_avoid_weight > 0.0f &&
        p.steer_avoid_radius > 0.0f && pool.count > 1) {
        static thread_local u32 frame_id = 0;
        ++frame_id;
        pool.rebuild_grid_if_stale(p.steer_avoid_radius, frame_id);
        // Single-threaded -- multi-thread would race on px/py writes
        // (one thread writes a slot, another thread reads the same
        // slot as a neighbor). A two-pass compute-then-apply pattern
        // could parallelize safely; doing single-thread for v1.
        apply_repulsion_range(p, pool, 0, pool.count, dt);
    }

    // Render single-threaded -- draw_sprite_rot updates renderer
    // state and isn't thread-safe. Slice 3+ will batch this into one
    // instanced draw and we can drop the per-particle draw call.
    if (do_render && r && r->draw_sprite_rot) {
        for (int i = 0; i < pool.count; ++i) {
            const float t = pool.age[i] / pool.lifetime[i];      // 0..1
            const float sz_world = pool.size_start[i] +
                                    (pool.size_end[i] - pool.size_start[i]) * t;
            const float pr = pool.r0[i] + (pool.r1[i] - pool.r0[i]) * t;
            const float pg = pool.g0[i] + (pool.g1[i] - pool.g0[i]) * t;
            const float pb = pool.b0[i] + (pool.b1[i] - pool.b0[i]) * t;
            const float pa = pool.a0[i] + (pool.a1[i] - pool.a0[i]) * t;

            // World -> screen.
            const float cx_px = (pool.px[i] - ctx->cam_pan_x) * ctx->cam_ppu
                                + ctx->view_cx;
            const float cy_px = (-(pool.py[i] - ctx->cam_pan_y)) * ctx->cam_ppu
                                + ctx->view_cy;
            const float sz_px = sz_world * ctx->cam_ppu;

            r->draw_sprite_rot(r,
                p.texture.index,
                cx_px, cy_px, sz_px, sz_px, 0.0f,
                0.0f, 0.0f, 1.0f, 1.0f,
                pr, pg, pb, pa);
        }
    }
    p.live_count = pool.count;
}

void emit_one(int slot, components::Particles& p, Pool& pool,
               SystemCtx& ctx, float ex, float ey) {
    pool.age[slot]       = 0.0f;
    pool.lifetime[slot]  = std::max(0.001f,
        p.lifetime + p.lifetime_random * srand(ctx));
    pool.size_start[slot]= p.start_size;
    pool.size_end[slot]  = (p.end_size > 0.0f) ? p.end_size : p.start_size;
    pool.r0[slot] = p.start_color.r; pool.g0[slot] = p.start_color.g;
    pool.b0[slot] = p.start_color.b; pool.a0[slot] = p.start_color.a;
    pool.r1[slot] = p.end_color.r;   pool.g1[slot] = p.end_color.g;
    pool.b1[slot] = p.end_color.b;   pool.a1[slot] = p.end_color.a;
    pool.rand_seed[slot] = urand(ctx);
    // User-defined extras zero-init at spawn -- Lync code is expected
    // to set them right after via ParticleSpawn / ParticleSet.
    for (auto& a : pool.extra) a[slot] = 0.0f;

    float ox, oy;
    sample_shape(p, ctx, ox, oy);
    pool.px[slot] = ex + ox;
    pool.py[slot] = ey + oy;
    pool.vx[slot] = p.initial_velocity.x + p.velocity_random.x * srand(ctx);
    pool.vy[slot] = p.initial_velocity.y + p.velocity_random.y * srand(ctx);
}

void spawn_step(components::Particles& p, Pool& pool, SystemCtx& ctx,
                 float dt, float ex, float ey) {
    if (!p.playing) return;

    // Continuous rate. Alloc is O(1) -- bounded by capacity.
    if (p.rate > 0.0f) {
        pool.spawn_accum += p.rate * dt;
        while (pool.spawn_accum >= 1.0f) {
            pool.spawn_accum -= 1.0f;
            const int slot = pool.alloc();
            if (slot < 0) { pool.spawn_accum = 0.0f; break; }   // pool full
            emit_one(slot, p, pool, ctx, ex, ey);
        }
    }

    // Bursts. burst_count > 0 + period 0 => one-shot at burst_time;
    // period > 0 => repeating every `period` after the initial burst.
    if (p.burst_count > 0 &&
        p.age >= p.burst_time && p.age >= pool.next_burst_at) {
        const int n = std::min(p.burst_count,
                                pool.capacity - pool.count);
        for (int k = 0; k < n; ++k) {
            const int slot = pool.alloc();
            if (slot < 0) break;
            emit_one(slot, p, pool, ctx, ex, ey);
        }
        pool.next_burst_at = (p.burst_period > 0.0f)
                             ? p.age + p.burst_period
                             : 1.0e9f;     // never again
    }
}

// Both Update and Render systems share this dispatch -- the only
// difference is whether we draw quads. We split into two systems so
// physics phases see updated positions before rendering.
void run_step(ecs::World& world, float dt, void* user, bool do_render) {
    auto* ctx = static_cast<SystemCtx*>(user);
    if (!ctx || !ctx->parts_id) return;

    // Tick-mode transition reset: when entering or leaving Play, drop
    // every pool so the next session starts fresh. Without this a
    // Play/Stop/Play cycle leaves the previous battle's particles
    // alive in the new run, looking like they "stuck around."
    // Run on the update pass only (render doesn't change tick mode).
    if (!do_render) {
        const ecs::TickMode now = world.tick_mode();
        if (now != ctx->last_tick_mode) {
            ctx->pools.clear();
            particles_api::invalidate_pool_cache();
            ctx->last_tick_mode = now;
            // Reset live_count on every Particles component too so the
            // inspector reads 0 immediately, not stale.
            const ecs::ComponentId required[] = { ctx->parts_id };
            world.iterate_query(required, 1, nullptr, 0,
                +[](void*, ecs::Entity, void** cols, u32) {
                    auto* p = static_cast<components::Particles*>(cols[0]);
                    p->live_count = 0;
                }, nullptr);
        }
    }

    // Refresh camera snapshot at the top of each render pass. Update
    // passes don't need it (no draw calls) -- skip the service hop.
    if (do_render && ctx->camera_svc) {
        ZuesRenderCamera cam{};
        ctx->camera_svc->get_active(ctx->camera_svc, &cam);
        if (cam.viewport_w <= 0 || cam.viewport_h <= 0) return;
        ctx->cam_pan_x = cam.pan_x;
        ctx->cam_pan_y = cam.pan_y;
        ctx->cam_ppu   = cam.pixels_per_unit * cam.zoom;
        ctx->view_cx   = static_cast<float>(cam.viewport_w) * 0.5f;
        ctx->view_cy   = static_cast<float>(cam.viewport_h) * 0.5f;
    }

    const ecs::ComponentId required[] = { ctx->parts_id, ctx->xform_id };

    struct Closure {
        SystemCtx* c;
        float      dt;
        bool       do_render;
    } closure{ctx, dt, do_render};

    // Publish gizmos for the SELECTED Particles emitter (if any)
    // ahead of the integrate. Cheap; gated by category bit + the
    // editor's selected_entity matching this emitter. Drawn on top
    // of the particles via the shared gizmo overlay.
    if (do_render && ctx->dbg_svc &&
        ctx->dbg_svc->is_enabled(ctx->dbg_svc, ZUES_DBG_PARTICLES)) {
        const ZuesDbgEntity sel = ctx->dbg_svc->selected_entity(ctx->dbg_svc);
        if (sel.index != 0 || sel.generation != 0) {
            const ecs::Entity sel_e{ sel.index, sel.generation };
            if (world.is_alive(sel_e) &&
                world.has_component(sel_e, ctx->parts_id)) {
                auto* p = static_cast<components::Particles*>(
                    world.get_component(sel_e, ctx->parts_id));
                auto* xf = ctx->xform_id
                    ? static_cast<components::Transform2D*>(
                        world.get_component(sel_e, ctx->xform_id))
                    : nullptr;
                if (p && xf) {
                    const float ex = xf->position.x;
                    const float ey = xf->position.y;
                    // Spawn shape outline -- lime green.
                    switch (p->shape) {
                        default: case 0:    // Point: small cross
                            ctx->dbg_svc->line(ctx->dbg_svc, ZUES_DBG_PARTICLES,
                                ex - 0.2f, ey, ex + 0.2f, ey,
                                0.2f, 1.0f, 0.4f, 1.0f);
                            ctx->dbg_svc->line(ctx->dbg_svc, ZUES_DBG_PARTICLES,
                                ex, ey - 0.2f, ex, ey + 0.2f,
                                0.2f, 1.0f, 0.4f, 1.0f);
                            break;
                        case 1: case 4:     // Circle / Ring
                            ctx->dbg_svc->circle(ctx->dbg_svc, ZUES_DBG_PARTICLES,
                                ex, ey, (p->shape_w + p->shape_h) * 0.5f,
                                0.2f, 1.0f, 0.4f, 1.0f);
                            break;
                        case 2:             // Rect
                            ctx->dbg_svc->rect(ctx->dbg_svc, ZUES_DBG_PARTICLES,
                                ex, ey, p->shape_w, p->shape_h, 0.0f,
                                0.2f, 1.0f, 0.4f, 1.0f);
                            break;
                        case 3:             // Edge (horizontal line)
                            ctx->dbg_svc->line(ctx->dbg_svc, ZUES_DBG_PARTICLES,
                                ex - p->shape_w * 0.5f, ey,
                                ex + p->shape_w * 0.5f, ey,
                                0.2f, 1.0f, 0.4f, 1.0f);
                            break;
                    }
                    // Avoid radius -- soft amber so it reads
                    // distinctly from the spawn shape.
                    if (p->use_steering && p->steer_avoid_radius > 0.0f) {
                        ctx->dbg_svc->circle(ctx->dbg_svc, ZUES_DBG_PARTICLES,
                            ex, ey, p->steer_avoid_radius,
                            1.0f, 0.65f, 0.2f, 0.8f);
                    }
                }
            }
        }
    }

    world.iterate_query(required, 2, nullptr, 0,
        +[](void* u, ecs::Entity e, void** cols, u32) {
            auto* cl = static_cast<Closure*>(u);
            auto* p  = static_cast<components::Particles*>(cols[0]);
            auto* W  = cl->c->xform_id ? cols[1] : nullptr;

            // Resolve (or allocate) the pool.
            const u64 key = ent_key(e);
            Pool& pool = cl->c->pools[key];
            if (pool.capacity != p->max_particles) {
                pool.resize(std::max(0, p->max_particles));
            }
            if (pool.capacity == 0) return;

            // Get the entity's world position from Transform2D. Slice 2
            // doesn't compose hierarchy -- root-level emitters only.
            // (sprite_render_system has the composed path; we'll lift
            // it later when emitter parenting becomes important.)
            float ex = 0.0f, ey = 0.0f;
            if (W) {
                auto* xf = static_cast<components::Transform2D*>(W);
                ex = xf->position.x;
                ey = xf->position.y;
            }

            // Lifecycle: advance age, stop if past duration.
            const bool was_playing = p->playing != 0;
            p->age += cl->dt;
            if (p->duration >= 0.0f && p->age >= p->duration) {
                if (p->loop) {
                    p->age = 0.0f;
                    pool.next_burst_at = 0.0f;
                } else {
                    p->playing = 0;
                }
            }

            if (!cl->do_render) {
                if (was_playing) spawn_step(*p, pool, *cl->c, cl->dt, ex, ey);
            }
            integrate_and_render(*p, pool, cl->dt, cl->c, cl->do_render);
        }, &closure);

    // Garbage-collect pools whose entities have died. Cheap; runs once
    // per second's worth of frames.
    static thread_local int gc_tick = 0;
    if (++gc_tick > 60 && do_render) {
        gc_tick = 0;
        bool any_erased = false;
        for (auto it = ctx->pools.begin(); it != ctx->pools.end(); ) {
            const ecs::Entity e{
                (u32)(it->first & 0xffffffffu),
                (u32)(it->first >> 32) };
            if (!world.is_alive(e)) {
                it = ctx->pools.erase(it);
                any_erased = true;
            } else {
                ++it;
            }
        }
        if (any_erased) particles_api::invalidate_pool_cache();
    }
}

void update_system(ecs::World& w, float dt, void* user) {
    run_step(w, dt, user, /*do_render=*/false);
}
void render_system(ecs::World& w, float dt, void* user) {
    run_step(w, dt, user, /*do_render=*/true);
}

// Editor-domain preview: ticks ONE emitter (the selected entity) so
// users see particles animate while authoring without entering Play.
// We reuse the per-entity logic by short-circuiting the iterate_query
// loop to a single entity. Cheap; runs only when something's selected.
void preview_system(ecs::World& world, float dt, void* user) {
    auto* ctx = static_cast<SystemCtx*>(user);
    if (!ctx || !ctx->parts_id) return;

    // Selection change -> drop the previously-previewed pool so a
    // re-select restarts from zero. Without this, deselecting an
    // emitter pauses it; reselecting picks up where it stopped,
    // which feels broken vs the fresh-preview the user expects.
    if (ctx->preview_entity != ctx->last_preview_entity) {
        if (!ctx->last_preview_entity.is_null()) {
            ctx->pools.erase(ent_key(ctx->last_preview_entity));
            particles_api::invalidate_pool_cache();
            // Reset its live_count too, if the entity still exists.
            if (world.is_alive(ctx->last_preview_entity) &&
                world.has_component(ctx->last_preview_entity, ctx->parts_id)) {
                auto* p = static_cast<components::Particles*>(
                    world.get_component(ctx->last_preview_entity, ctx->parts_id));
                if (p) { p->live_count = 0; p->age = 0.0f; }
            }
        }
        ctx->last_preview_entity = ctx->preview_entity;
    }

    if (ctx->preview_entity.is_null()) return;
    if (!world.is_alive(ctx->preview_entity)) return;
    if (!world.has_component(ctx->preview_entity, ctx->parts_id)) return;

    auto* p = static_cast<components::Particles*>(
        world.get_component(ctx->preview_entity, ctx->parts_id));
    if (!p) return;

    const u64 key = ent_key(ctx->preview_entity);
    Pool& pool = ctx->pools[key];
    if (pool.capacity != p->max_particles)
        pool.resize(std::max(0, p->max_particles));
    if (pool.capacity == 0) return;

    float ex = 0.0f, ey = 0.0f;
    if (ctx->xform_id) {
        auto* xf = static_cast<components::Transform2D*>(
            world.get_component(ctx->preview_entity, ctx->xform_id));
        if (xf) { ex = xf->position.x; ey = xf->position.y; }
    }

    p->age += dt;
    if (p->duration >= 0.0f && p->age >= p->duration) {
        if (p->loop) { p->age = 0.0f; pool.next_burst_at = 0.0f; }
        else         { p->playing = 0; }
    }
    if (p->playing) spawn_step(*p, pool, *ctx, dt, ex, ey);
    integrate_and_render(*p, pool, dt, ctx, /*do_render=*/false);
}

}  // namespace

bool ParticleSystem::register_into(ecs::World& world,
                                    ::IRenderer_2D_v1* renderer) {
    g_ctx.renderer = renderer;
    g_ctx.world    = &world;
    g_ctx.parts_id = world.find_component_id("Particles");
    g_ctx.xform_id = world.find_component_id("Transform2D");
    if (auto* sr = Engine::services()) {
        g_ctx.camera_svc = static_cast<::IRenderCamera_v1*>(
            sr->get_service(ZUES_SERVICE_RENDER_CAMERA,
                             ZUES_SERVICE_RENDER_CAMERA_VERSION));
        g_ctx.dbg_svc = static_cast<::IDebugDraw_v1*>(
            sr->get_service(ZUES_SERVICE_DEBUG_DRAW,
                             ZUES_SERVICE_DEBUG_DRAW_VERSION));
    }
    if (!g_ctx.parts_id) {
        ZUES_LOG_WARN("Particles: component not registered yet -- "
                      "system installed but will idle until it is");
    }

    // Update in PreUpdate so spawn + integrate land before any
    // gameplay reads the live count or position. Render in Render so
    // we draw alongside sprites at the right point. Names appear in
    // the Systems panel; keep them human-readable.
    update_handle = world.add_system("Particles Update",
        ecs::Phase::PreUpdate, update_system, &g_ctx,
        ecs::SystemDomain::Game);
    render_handle = world.add_system("Particles Render",
        ecs::Phase::Render, render_system, &g_ctx,
        ecs::SystemDomain::Both);
    // Editor preview ticker: idle when nothing's selected; ticks the
    // selected emitter when set_preview_entity has been called this
    // frame. Editor-domain so it doesn't double-tick during Play.
    preview_handle = world.add_system("Particles Editor Preview",
        ecs::Phase::PreUpdate, preview_system, &g_ctx,
        ecs::SystemDomain::Editor);
    ZUES_LOG_INFO("Particles system registered (Update/Render/Preview)");
    return update_handle.is_valid() && render_handle.is_valid() &&
           preview_handle.is_valid();
}

void ParticleSystem::unregister_from(ecs::World& world) {
    if (update_handle.is_valid())  world.remove_system(update_handle);
    if (render_handle.is_valid())  world.remove_system(render_handle);
    if (preview_handle.is_valid()) world.remove_system(preview_handle);
    update_handle  = {};
    render_handle  = {};
    preview_handle = {};
    g_ctx = {};
}

void ParticleSystem::set_preview_entity(ecs::Entity e) {
    g_ctx.preview_entity = e;
}

// ---- Lync-host accessors --------------------------------------------
// All routed through the same g_ctx.pools map keyed by entity id.
// Slot bounds, missing pools, and unknown field names all fall through
// to no-op / 0.0f -- Lync calls hit the hot path 100k+ times/frame and
// we don't want them to crash on edge cases.

namespace particles_api {

namespace {
    // Per-call cache: most Lync swarm code hits the SAME emitter
    // dozens of times in a row. Caching the last (entity_key, pool*)
    // pair skips the unordered_map lookup on hot paths -- 50ns vs 5ns.
    thread_local u64    g_last_key  = 0;
    thread_local Pool*  g_last_pool = nullptr;

    Pool* pool_for(ecs::Entity e) {
        const u64 k = ent_key(e);
        if (k == g_last_key && g_last_pool != nullptr) return g_last_pool;
        auto it = g_ctx.pools.find(k);
        if (it == g_ctx.pools.end()) { g_last_key = 0; g_last_pool = nullptr; return nullptr; }
        g_last_key  = k;
        g_last_pool = &it->second;
        return g_last_pool;
    }
    bool slot_in_bounds(const Pool* pool, int idx) {
        return pool && idx >= 0 && idx < pool->count;
    }

    // Lightweight stamp of the extra_names buffer so we can detect
    // edits without storing a copy. FNV-1a 32-bit over the live
    // string content. Cheap; runs only when the cache is first
    // queried for a given pool that frame.
    u32 stamp_extras(const char* buf, std::size_t cap) {
        u32 h = 2166136261u;
        const std::size_t n = ::strnlen(buf, cap);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= (u8)buf[i];
            h *= 16777619u;
        }
        return h;
    }

    // Rebuild a pool's field_to_slot map from a Particles emitter's
    // extra_names buffer. extra_names is newline-separated; the Nth
    // non-empty row maps to pool.extra[N]. Empty rows skipped.
    void rebuild_field_cache(Pool& pool, const components::Particles* p) {
        pool.field_to_slot.clear();
        const char* buf = p->extra_names;
        const std::size_t cap = sizeof(p->extra_names);
        const char* row = buf;
        const char* end = buf + ::strnlen(buf, cap);
        int idx = 0;
        while (row < end && idx < Pool::EXTRA_SLOTS) {
            const char* row_end = row;
            while (row_end < end && *row_end != '\n') ++row_end;
            const std::size_t rlen = (std::size_t)(row_end - row);
            if (rlen > 0) {
                pool.field_to_slot.emplace(std::string(row, rlen), idx);
            }
            row = (row_end < end) ? row_end + 1 : row_end;
            ++idx;
        }
        pool.field_cache_stamp = stamp_extras(buf, cap);
    }

    // Resolve a field name to its slot index in the pool's extra
    // arrays. Lazy + cached; rebuilds when extra_names content
    // changes (detected via FNV stamp).
    int field_slot(ecs::Entity e, const char* field) {
        if (!field || !*field || !g_ctx.world || !g_ctx.parts_id) return -1;
        Pool* pool = pool_for(e);
        if (!pool) return -1;
        auto* p = static_cast<components::Particles*>(
            g_ctx.world->get_component(e, g_ctx.parts_id));
        if (!p) return -1;
        const u32 cur_stamp = stamp_extras(p->extra_names,
                                            sizeof(p->extra_names));
        if (cur_stamp != pool->field_cache_stamp ||
            pool->field_to_slot.empty()) {
            rebuild_field_cache(*pool, p);
        }
        auto it = pool->field_to_slot.find(field);
        return (it == pool->field_to_slot.end()) ? -1 : it->second;
    }
}

int count(ecs::Entity e) {
    Pool* pool = pool_for(e);
    return pool ? pool->count : 0;
}
void get_pos(ecs::Entity e, int idx, float* out_x, float* out_y) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) {
        if (out_x) *out_x = 0.0f;
        if (out_y) *out_y = 0.0f;
        return;
    }
    if (out_x) *out_x = pool->px[idx];
    if (out_y) *out_y = pool->py[idx];
}
void set_pos(ecs::Entity e, int idx, float x, float y) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) return;
    pool->px[idx] = x; pool->py[idx] = y;
}
void get_vel(ecs::Entity e, int idx, float* out_vx, float* out_vy) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) {
        if (out_vx) *out_vx = 0.0f;
        if (out_vy) *out_vy = 0.0f;
        return;
    }
    if (out_vx) *out_vx = pool->vx[idx];
    if (out_vy) *out_vy = pool->vy[idx];
}
void set_vel(ecs::Entity e, int idx, float vx, float vy) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) return;
    pool->vx[idx] = vx; pool->vy[idx] = vy;
}
float get_field(ecs::Entity e, int idx, const char* field) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) return 0.0f;
    const int slot = field_slot(e, field);
    if (slot < 0) return 0.0f;
    return pool->extra[slot][idx];
}
void set_field(ecs::Entity e, int idx, const char* field, float value) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) return;
    const int slot = field_slot(e, field);
    if (slot < 0) return;
    pool->extra[slot][idx] = value;
}
void kill(ecs::Entity e, int idx) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) return;
    pool->swap_remove(idx);
    // swap_remove can move another particle into the killed slot; if
    // the cached pool is THIS one, the cache is still valid (same
    // pool ptr), but later reads at `idx` see the swapped neighbor.
    // That's the expected swap-remove semantic; no invalidation.
}

// ---- Spatial nearest-neighbor query --------------------------------
// Builds the target emitter's grid lazily (per-frame). Cell size =
// max_radius so a single 3x3 cell scan covers the search circle.
int nearest_neighbor(ecs::Entity target_emitter, float x, float y,
                      float max_radius) {
    Pool* pool = pool_for(target_emitter);
    if (!pool || pool->count == 0 || max_radius <= 0.0f) return -1;
    static thread_local u32 frame_id = 0;
    ++frame_id;
    pool->rebuild_grid_if_stale(max_radius, frame_id);
    return pool->nearest(x, y, max_radius);
}

// ---- Step toward target --------------------------------------------
// One call replaces get_pos / compute / set_pos. Returns 1 if reached.
int step_toward(ecs::Entity e, int idx, float tx, float ty,
                 float max_speed, float dt) {
    Pool* pool = pool_for(e);
    if (!slot_in_bounds(pool, idx)) return 0;
    const float dx = tx - pool->px[idx];
    const float dy = ty - pool->py[idx];
    const float d2 = dx*dx + dy*dy;
    const float step = max_speed * dt;
    if (d2 <= step * step + 1e-6f) {
        pool->px[idx] = tx; pool->py[idx] = ty;
        return 1;
    }
    const float inv = 1.0f / std::sqrt(d2);
    pool->px[idx] += dx * inv * step;
    pool->py[idx] += dy * inv * step;
    return 0;
}

// ---- Per-emitter iterator -----------------------------------------
// Runs the loop in C++ so the callback's per-particle ParticleGet/
// ParticleSet calls all hit the same cached pool ptr instead of
// re-doing the unordered_map lookup each one.
void for_each(ecs::Entity emitter, EachFn cb, float dt, void* user) {
    Pool* pool = pool_for(emitter);
    if (!pool || !cb) return;
    const int n = pool->count;
    for (int i = 0; i < n; ++i) {
        cb(emitter, i, dt, user);
    }
}

void invalidate_pool_cache() {
    g_last_key  = 0;
    g_last_pool = nullptr;
}

// ---- Raw SoA slices --------------------------------------------------
// Hand the caller a writable pointer into the pool's parallel arrays.
// Frame-local; pool resize / erase invalidates them. Returns nullptr
// when the emitter has no pool allocated yet.
float* slice_px(ecs::Entity e) {
    Pool* p = pool_for(e); return p ? p->px.data() : nullptr;
}
float* slice_py(ecs::Entity e) {
    Pool* p = pool_for(e); return p ? p->py.data() : nullptr;
}
float* slice_vx(ecs::Entity e) {
    Pool* p = pool_for(e); return p ? p->vx.data() : nullptr;
}
float* slice_vy(ecs::Entity e) {
    Pool* p = pool_for(e); return p ? p->vy.data() : nullptr;
}
float* slice_age(ecs::Entity e) {
    Pool* p = pool_for(e); return p ? p->age.data() : nullptr;
}
float* slice_field(ecs::Entity e, const char* field) {
    Pool* p = pool_for(e);
    if (!p) return nullptr;
    const int slot = field_slot(e, field);
    if (slot < 0) return nullptr;
    return p->extra[slot].data();
}

}  // namespace particles_api

}  // namespace Engine::host
