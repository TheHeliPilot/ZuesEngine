# Particles

One unified `Particles` component handles both VFX (smoke, sparks,
fire) and large game-logic swarms (Total War-style soldiers, fish
schools, bullet hells). They share the same compute pipeline, the
same instanced render path, and the same Lync API; the distinction is
feature flags, not separate components.

The component itself stays neutral — no field is named `team` or
`hp`. Game-specific per-particle state lives in eight named **extra
fields** the user defines per-emitter; gameplay code in Lync reads /
writes them by name.


## Tier vs Profile

Two top-level dropdowns shape the emitter:

**Tier** picks how the simulation runs:

| Tier | Range | Sim |
|---|---|---|
| Light  (CPU)        | up to ~50k  | scalar SoA |
| Medium (CPU jobs)   | up to ~250k | parallel SoA via TaskRunner |
| Heavy  (GPU compute)| up to  ~1M  | (data-only today; falls back to Medium) |

**Profile** picks reasonable defaults for the module toggles below.

| Profile | Spatial grid | Steering | Events | Collision | Curves |
|---|---|---|---|---|---|
| VFX    | off | off | off | optional | on |
| Swarm  | on  | on  | on  | on       | optional |
| Custom | leaves toggles where they were |

Switching profile flips the `use_*` flags but doesn't hide them.


## Authoring an emitter

1. Add `Particles` to an entity (Add Component → Engine/Render → Particles).
2. Pick a Profile (VFX or Swarm) — module toggles update.
3. Set a `texture` (drag from Asset Browser).
4. Tune emission: `rate` for continuous, `burst_count` + `burst_time`
   for one-shots, `burst_period > 0` to repeat.
5. Pick a `shape` (Point / Circle / Rect / Edge / Ring) and dimensions.
6. Set `start_color` / `end_color` and `start_size` / `end_size` —
   particles lerp between them over their lifetime.

The emitter follows the entity's `Transform2D.position`. Spawn
positions are sampled INSIDE the shape relative to that anchor.


## Custom per-particle fields (`extra_names`)

Eight named float slots per particle. Names typed in the Inspector,
under the **Custom Fields** section. Lync reads / writes them by name
at runtime. The runtime stores them as 8 parallel float arrays inside
the pool; per-emitter field-name → slot lookup is cached so calls are
O(1) hashmap lookup, not string-walk.

Use them for `hp`, `team`, `morale`, `attack_timer`, whatever your
game needs. Empty rows = unused slot.


## Lifecycle

- `age` — seconds since the emitter started (or last restart).
- `duration` — stop spawning after this many seconds. `-1` = forever.
- `loop` — when `duration` elapses, restart at `age = 0`.
- `playing` — pause / resume without resetting age.
- `live_count` — read-only diagnostic (alive particles this frame).

The runtime increments `age` automatically and honors `duration` +
`loop`. Play↔Stop transition resets every pool. Selecting / deselecting
an emitter in the editor's preview path also resets its pool so a
re-select restarts cleanly from t=0.


## Native repulsion (soldier "interference")

When `use_steering = 1` AND both `steer_avoid_radius > 0` AND
`steer_avoid_weight > 0`, the C++ integrate pass runs an O(N·k)
particle-vs-particle repulsion via the spatial grid. Particles push
away from neighbors within `steer_avoid_radius`, with falloff —
spread soldiers out automatically without needing collision detection
or any Lync code. Cell size for the grid is `steer_avoid_radius`.


## Module toggles

- `use_spatial_grid` — bin agents in a uniform grid for O(neighbors)
  queries. Auto-enabled by `use_steering`.
- `use_steering` — enables the native repulsion pass + makes
  `ParticleNearestNeighbor` use a fresh grid each frame.
- `use_events` — append a record to a CPU-readable buffer when an
  agent dies or hits something. **Reserved** — runtime is a no-op.
- `use_collision` — sample the world's SDF and bounce / clamp.
  **Reserved** — runtime is a no-op.

Steering parameters: `steer_avoid_radius`, `steer_avoid_weight` (the
two used by native repulsion); seek / alignment / cohesion are
reserved for a future pass.


## Lync API — emitter control

```lync
EmitBurst(explosion_e, 200);     // one-shot burst layered on rate
SetEmitting(rain_e, false);      // pause / resume continuous emission
RestartEmitter(intro_e);         // reset age + replay burst schedule
```


## Lync API — per-particle access

```lync
n: int = ParticleCount(emitter);

// Position / velocity
ParticleGetPos(emitter, idx, addr_of(x), addr_of(y));
ParticleSetPos(emitter, idx, x, y);
ParticleGetVel(emitter, idx, addr_of(vx), addr_of(vy));
ParticleSetVel(emitter, idx, vx, vy);

// Custom fields (the Inspector's `extra_names`)
hp: float = ParticleGet(emitter, idx, "hp");
ParticleSet(emitter, idx, "hp", hp - 10.0f);

// Remove a particle (swap-remove)
ParticleKill(emitter, idx);
```

`ParticleGet` / `ParticleSet` resolve the field name through a per-
emitter cache built lazily, so the lookup is a hashmap probe (~30ns)
rather than a string-walk (~100ns).


## Lync API — fast path for hot loops

These primitives let you write swarm AI that scales:

```lync
// Spatial nearest-neighbor query, O(grid neighbors). Cell size =
// max_radius so a single 3x3 cell scan covers the search circle.
target: int = ParticleNearestNeighbor(enemy_emitter, mx, my, max_radius);

// One-call move: replaces get_pos / compute / set_pos. Returns 1 if
// reached this tick (within 1e-3 units), 0 otherwise.
reached: int = ParticleStepToward(emitter, idx, tx, ty, max_speed, dt);

// C++-driven iterator with a cached pool ptr inside the host. The
// callback's per-particle ParticleGet/Pos/Set calls then skip the
// pool-by-entity-id hashmap lookup.
ParticleForEach(emitter, my_per_particle_callback);
```


## Lync API — direct memory slices (max performance)

For really tight loops (100k+ particles), skip host calls entirely
inside the inner loop. `ParticleSliceX` returns a writable `float*`
straight into the pool's SoA array; index it with pointer arithmetic
+ `memcpy` (the std.list pattern):

```lync
n:    int = ParticleCount(emitter);
px:   ptr = ParticleSlicePx(emitter);
py:   ptr = ParticleSlicePy(emitter);
hp:   ptr = ParticleSliceField(emitter, "hp");
// px / py / hp are float*, length n. Direct memory access -- ZERO
// host calls per element.

for (i: 0 to n - 1) {
    px_i: ptr = px + i * 4;       // sizeof(float)
    mx: float;
    memcpy(addr_of(mx), px_i, 4);
    /* ... */
}
```

Slices are valid until the pool is resized (`max_particles` change)
or erased (Play↔Stop, preview deselect, GC). Treat them as
frame-local; re-fetch each frame.


## What's implemented today

- Light + Medium tiers.
- Continuous emission + scheduled bursts.
- Spawn shapes + distribution.
- Per-particle integrate (gravity, drag), age, lifetime cull,
  swap-remove storage.
- Color + size lerp over life.
- 8 user-defined float slots per particle (`extra_names`).
- Per-emitter spatial grid (lazy, frame-cached) for nearest-neighbor.
- Native particle-vs-particle repulsion under `use_steering`.
- Lync API: emitter control, per-particle get/set, spatial query,
  bulk movement, batched iterator, raw SoA slices.
- Per-call pool ptr cache + per-pool field-name → slot cache.
- Play↔Stop reset; preview-selection reset.
- World save/load round-trips every field.


## What's coming

- **Heavy tier**: GPU compute simulation (real 1M+).
- Renderer instancing (one draw call per emitter, not per particle).
- `use_collision`: SDF rasterization of static colliders.
- `use_events`: GPU→CPU event readback for kill / hit / reach signals.


## Trade-offs

- Heavy tier is non-deterministic (driver-dependent compute order).
  Stick to Light or Medium for deterministic replay / lockstep
  multiplayer.
- Light / Medium render path issues one `draw_sprite_rot` per
  particle. Comfortable to ~10k visually; instanced draw lands later.
- The Lync `for` loop with `ParticleGet`/`Set` works to ~5k vs 5k
  comfortably. Above that, switch to slice-based direct memory access
  to eliminate host-call overhead.
- The native repulsion is single-threaded (would race in parallel
  due to neighbor reads/writes overlapping). Two-pass compute-then-
  apply could parallelize it later.
