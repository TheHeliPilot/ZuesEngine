# ECS

Archetype storage, SoA, 10k+ entities comfortably. Like Unity DOTS / Bevy / Flecs.

## Concepts

- **Entity** — generational ID: `{index: u32, generation: u32}`. Recycled after despawn.
- **Component** — POD struct. No logic. Registered via reflection.
- **Archetype** — the unique set of component types an entity has. Entities with the same components live in the same archetype.
- **Chunk** — fixed-size block (~16 KiB) storing component arrays (SoA) for one archetype. Holds up to ~1024 entities depending on component sizes.
- **Query** — filter over archetypes: required components, optional, excluded. Cached: resolved archetype list rebuilt only when archetype set changes.
- **System** — function + query + phase. Runs deterministically within its phase.

## Storage layout

```
Archetype {Position, Velocity, Health}
 ├─ Chunk 0 (capacity 512):
 │    [Entity...]                      // 512 × u64
 │    [Position...]  (contiguous)      // 512 × Position
 │    [Velocity...]  (contiguous)      // 512 × Velocity
 │    [Health...]    (contiguous)      // 512 × Health
 └─ Chunk 1 (capacity 512): ...
```

Iteration is cache-linear within each chunk. Multi-component queries get parallel arrays — zero indirection.

Add/remove component: entity migrates to a different archetype (copy out of old chunk, copy into new chunk, update index). Cost: O(component-size). Acceptable at expected rates.

## System phases

Fixed order per frame:

1. `input` — read input, produce input components
2. `pre_update` — gameplay logic before physics
3. `physics` — integrate, collide (physics module)
4. `post_update` — gameplay after physics
5. `net_replicate` — send/receive replicated state
6. `ui_input` — UI consumes input events
7. `ui_layout` — layout UI entity trees
8. `render` — build render commands for game
9. `ui_render` — build render commands for UI

Within a phase: systems run in registration order. No cross-system parallelism (v1). Intra-system parallel-for is fine.

## Component registration

Reflection-driven via clang-p2996. User writes a plain struct, a one-liner registers it:

```cpp
struct Position { float x, y; };
struct Velocity { float vx, vy; };

ZUES_REGISTER_COMPONENT(Position);
ZUES_REGISTER_COMPONENT(Velocity);
```

The macro expands via reflection to generate: size, alignment, field descriptors, copy/dtor (trivial for POD), serialization (JSON + binary), inspector metadata.

Attributes tune per-field behavior:

```cpp
struct Transform {
    [[Engine::replicated]]  Engine::vec2 position;
    [[Engine::replicated]]  float rotation;
                          float render_scale;   // local only, not networked
};

struct Sprite {
    [[Engine::asset_ref]] TextureHandle texture;
    Engine::color tint;
};
```

Before P2996 lands (or for stock-compiler builds with `ZUES_USE_REFLECTION=OFF`): a `ZUES_COMPONENT_FIELDS(Type, field1, field2, ...)` macro provides a manual fallback. Same API downstream.

## Queries

```cpp
world.query<Position, Velocity>()
     .each([dt](Entity e, Position& p, Velocity& v) {
         p.x += v.vx * dt;
         p.y += v.vy * dt;
     });
```

- Query filters resolved at creation, archetype list cached.
- `.each` variants: by-entity, plain, or per-chunk (for SIMD-able inner loops).

Exclude filter:
```cpp
world.query<Position>().without<Frozen>().each(...);
```

## Systems

```cpp
world.add_system("movement", SystemPhase::PreUpdate,
    [](World& w, float dt) {
        w.query<Position, Velocity>().each([dt](auto, Position& p, Velocity& v) {
            p.x += v.vx * dt; p.y += v.vy * dt;
        });
    });
```

Systems can declare ordering constraints: `.after("input")`, `.before("collision")`. (v1 uses insertion order; constraints come with the net module.)

## Serialization

Reflection gives us free JSON + binary serialization. World save/load is one call. Scene files (`.world`) are JSON for diffability.
