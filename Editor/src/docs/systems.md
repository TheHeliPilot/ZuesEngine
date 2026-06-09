# Systems

Systems are **behaviour**. They iterate entities matching a component
shape and mutate their data. One system per "kind of work."

## Declare

```lync
[System("PreUpdate", "Game")]
def movement(eng: ptr, dt: float, user: ptr): void {
    Each<Velocity>(apply_velocity);
}
// Each<T>'s callback signature is (EntityRef, ref T, dt: float). `ref` is
// a non-null borrowed pointer -- mutate v's fields directly, no match
// needed. dt is the live frame dt threaded from the calling system.
def apply_velocity(e: EntityRef, v: ref Velocity, dt: float): void {
    v.vx = v.vx * 0.99;   // damping
}
```

```cpp
#include <zues/cpp_helpers.h>

extern "C" void movement(ZuesEngine* eng, float dt, void* user) {
    // Cpp helper Each<T> callback signature is (int e_idx, T*).
    // dt is captured by reference from the surrounding system fn.
    zues::Each<Velocity>([&](int e_idx, Velocity* v) {
        // Mutate v->... ; dt is the live frame timestep
    });
}

// In on_load:
//   host->add_system_with_domain(eng, "movement",
//       ZUES_PHASE_PRE_UPDATE, ZUES_DOMAIN_GAME, movement, nullptr);
```

The lync attribute does the `add_system_with_domain` call for you. C++
projects make the call explicitly inside `on_load`.

## Phases

`[System(phase, domain)]` (lync) or the third arg of
`add_system_with_domain` (cpp). Phases run in this fixed order each
frame:

| Phase          | When it ticks                                                      |
| -------------- | ------------------------------------------------------------------ |
| `Input`        | top of frame; gather keyboard / mouse / pad state                  |
| `PreUpdate`    | early game logic. Most systems live here. Aliases: `Logic`, `Update` |
| `Physics`      | physics step + collision events                                    |
| `PostUpdate`   | late corrections + cleanup that depends on physics                 |
| `NetReplicate` | network state sync (when wired)                                    |
| `UiInput`      | UI mouse / keyboard intake                                         |
| `UiLayout`     | UI element layout                                                  |
| `Render`       | scene draw                                                         |
| `UiRender`     | UI draw on top of the scene                                        |

If you don't know which to pick, use `PreUpdate`.

C++ enum names: `ZUES_PHASE_INPUT`, `ZUES_PHASE_PRE_UPDATE`,
`ZUES_PHASE_PHYSICS`, `ZUES_PHASE_POST_UPDATE`, `ZUES_PHASE_NET_REPLICATE`,
`ZUES_PHASE_UI_INPUT`, `ZUES_PHASE_UI_LAYOUT`, `ZUES_PHASE_RENDER`,
`ZUES_PHASE_UI_RENDER`.

## Domain

Second arg of `[System(phase, domain)]` (lync) / fourth arg of
`add_system_with_domain` (cpp). Controls when the system ticks vs play
state:

- `"Game"`   / `ZUES_DOMAIN_GAME`   -- only ticks during Play (Stop pauses).
- `"Editor"` / `ZUES_DOMAIN_EDITOR` -- only ticks in Edit mode (gizmos, viewport tools).
- `"Both"`   / `ZUES_DOMAIN_BOTH`   -- always ticks (rendering, etc.).

## Spawning + destroying entities

Both safe to call from inside a system, including inside an `Each<T>`
callback.

```lync
e: EntityRef = CreateEntity();
e.SetTransform(10.0, 5.0, 0.0, 1.0, 1.0);
e.Add<Velocity>(Velocity{ vx: 1.0, vy: 0.0 });

DestroyEntity(target);                // queued: actual removal happens
                                       // at end of the current Each<T> walk
```

`CreateEntity` is **immediate** -- the returned index is valid for the
rest of the current frame. `DestroyEntity` is **deferred to the end of
the current iteration loop** so iteration order stays stable.

## Spawning prefabs

Two ways:

```lync
// String path -- pragmatic, doesn't require a populated PrefabRef field.
bullet: EntityRef = Instantiate("assets/prefabs/Bullet.zprefab", x, y);

// Typed PrefabRef -- survives renames/moves of the asset file, idiomatic
// when the prefab to spawn comes from a [Component] field the editor
// populated via drag-drop.
match GameManager() {
    some(mgr): InstantiatePrefab(mgr.bullet_prefab, x, y);
    null:      { }
}
```

Both return an `EntityRef`. On failure the returned generation is `0`,
so check `if (result.generation == 0) { /* failed */ }`. The spawned
subtree's intra-prefab `EntityRef` fields are remapped to the new ids;
refs that pointed *outside* the saved subtree drop to null.

## Reading singletons

`[Singleton]` components get a Unity-style cached getter. From any
system, anywhere:

```lync
[System("Update", "Game")]
def shoot(eng: ptr, dt: float, user: ptr): void {
    if (!IsKeyPressed(KeyCode().Space)) { return; }
    // GameManager() is the plugin-emitted cached getter for the
    // [Singleton] component. Returns GameManager? (nullable typed
    // pointer) -- the cache is refreshed lazily when the world's
    // archetype version changes.
    match GameManager() {
        some(cfg): {
            InstantiatePrefab(cfg.bullet_prefab, 0.0, 0.0);
            cfg.score = cfg.score + 1;
        }
        null: { /* unreachable: ensure_singleton ran in on_load */ }
    }
}
```

```cpp
extern "C" void shoot(ZuesEngine* eng, float dt, void*) {
    // C++ has no cached Singleton<T>() helper yet -- look up the
    // singleton entity by component id, then fetch the component
    // pointer. Cache the (entity, generation, world_version) triplet
    // yourself if this gets hot.
    const ZuesComponentId id =
        zues::g_host->find_component_id(eng, "GameManager");
    const ZuesEntity gm = zues::g_host->find_singleton(eng, id);
    if (gm.generation == 0) return;
    auto* cfg = static_cast<GameManager*>(
        zues::g_host->get_component(eng, gm, id));
    if (cfg) ++cfg->score;
}
```

Cost in steady state is one u64 compare against the world's archetype
version + one cached pointer return. The plugin refreshes the cache
automatically when the version bumps (component add/remove, world
load, hot-reload).

See `components` for the `[Singleton]` declaration syntax and when to
use a singleton vs a per-instance ref.

## Patterns

- **One system, many entities.** Don't loop entities by index -- let
  `Each<T>` do the iteration; the engine knows which archetypes match.
- **Stateless.** Frame state lives in components, not in the system body.
  Systems should be re-entrant -- same input -> same output.
- **Composable.** Multiple systems can read/write the same component;
  ordering inside a phase is registration order. Use separate phases
  when you want strict before/after ordering.
- **Refs are data.** A system that "needs the bullet prefab" should
  read it from a singleton or per-entity component, not bake the path
  into code. See `prefabs` and `components` for the rationale.
- **Multi-component queries** (entity has A AND B) are coming via a
  generated `Each<A, B>(cb)` helper; for now use `Each<A> + Has<B>` in
  the callback.

## Inside the callback

- It's safe to **read other components** of the same entity.
- It's safe to **mutate the component you're iterating** -- pointer is
  stable for the visit.
- `DestroyEntity(e)` is queued to end-of-walk; safe to call.
- Don't add components of the SAME type you're iterating during the
  walk (it resizes storage). Add other types freely.
