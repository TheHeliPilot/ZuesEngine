# Components

Components are **data**. No methods, no behaviour. Systems do the work.

A component is a plain struct. It only contains values -- never function
pointers, owning pointers, or runtime allocations. Multiple entities can
hold the same component type, each with its own values.

## Declare

```lync
[Component]
[Category("Combat/Stats")]
Health: struct {
    hp:  int,
    max: int
}
```

```cpp
#include <zues/cpp_helpers.h>

struct Health {
    int hp{};
    int max{};
};
ZUES_PROJECT_FIELDS    (Health, hp, max);
ZUES_REGISTER_COMPONENT(Health, "Combat/Stats");
```

`[Category("path/with/slashes")]` (lync) or the second arg of
`ZUES_REGISTER_COMPONENT` (cpp) controls where the component appears in
the Inspector's Add Component picker. Pass `nullptr` for "no category"
in cpp; the editor falls back to `Project`.

After registration the component shows up in:

- The Inspector's **Add Component** picker, under your category.
- The Lync editor autocomplete (`Add<Health>`, `Each<Health>`, ...).
- World save / load (the JSON references it by name).

## Field types

| Lync syntax    | FieldKind     | Inspector UI                                 |
| -------------- | ------------- | -------------------------------------------- |
| `bool`         | Bool          | checkbox                                     |
| `int`          | I32           | drag int                                     |
| `float`        | F32           | drag float                                   |
| `Vec2`         | Vec2          | 2x drag float                                |
| `Vec3`         | Vec3          | 3x drag float                                |
| `Color`        | Color         | colour picker swatch                         |
| `EntityRef`    | EntityRef     | drop target -- drag from Hierarchy            |
| `PrefabRef`    | PrefabRef     | drop target -- drag from Asset Browser        |
| `SpriteRef`    | SpriteRef     | drop target                                  |
| `TextureRef`   | TextureRef    | drop target                                  |
| `AudioRef`     | AudioRef      | drop target                                  |
| `FontRef`      | FontRef       | drop target                                  |

Internal fields starting with `_` (`_body_handle`, `_shape_handle`, ...)
are hidden from the inspector -- they're engine-owned bookkeeping.

`f32` rotations / angles auto-render in degrees in the inspector
(reading any field whose name contains `rotation` or `angle`); the
underlying storage stays radians.

## Auto-injected helper family

Once a component `T` is registered, the helpers below are available in
both languages.

| Lync                          | C++                            | What it does                  |
| ----------------------------- | ------------------------------ | ----------------------------- |
| `Add<T>(e, T{ ...fields })`   | `zues::Add<T>(e, T{...})`      | attach a copy                 |
| `Remove<T>(e)`                | `zues::Remove<T>(e)`           | detach                        |
| `e.Has<T>(): int`             | `zues::Has<T>(e) -> bool`      | presence check                |
| `e.Get<T>(): ref T?`          | `zues::Get<T>(e) -> T*`        | typed nullable borrow (`match` unwrap) |
| `Each<T>(cb)` cb is `(EntityRef, ref T, float) -> void` | `zues::Each<T>([](int e, T* c){...})` | iterate every entity with T |

In Lync, `e` is always an `EntityRef`. UFCS (`e.Has<T>()`) is the same
as `Has<T>(e)`. `Get<T>` returns a nullable borrowed pointer (`ref T?`), so the
analyzer threads the type through the unwrap and `pos.field` checks at
compile time.

UFCS in lync: `e.Has<T>()` is the same as `Has<T>(e)`, and `e.Get<T>()`
is the same as `Get<T>(e)`.

## Attach + read + iterate

```lync
e: EntityRef = CreateEntity();
Add<Health>(e, Health{ hp: 100, max: 100 });

// Get<T> returns `ref T?` (nullable borrow). Two unwrap idioms:
match e.Get<Health>() {
    some(h) => { h.hp -= 10; }    // h has type Health* inside this arm
    null    => { /* no health */ }
}
```

```cpp
// `e_idx` is what the helpers take. host->create_entity returns
// ZuesEntity; pull the index out for the helper calls.
const ZuesEntity e = zues::g_host->create_entity(zues::g_engine);
const int e_idx = static_cast<int>(e.index);

zues::Add<Health>(e_idx, Health{100, 100});

if (zues::Has<Health>(e_idx)) {
    Health* h = zues::Get<Health>(e_idx);
    h->hp -= 10;
}

zues::Each<Health>([](int e_idx, Health* h) {
    if (h->hp <= 0) { /* dead */ }
});
```

## `[Singleton]` components

`[Singleton]` declares a "there is exactly one of this in the world"
component. The plugin's auto-generated `on_load` calls
`ensure_singleton<T>` so the entity exists before any system ticks --
no `Awake()` boilerplate, no null checks, no race with the first
frame. The plugin emits a Unity-style cached getter `T()` that any
system can call to access the singleton.

```lync
[Component, Singleton]
GameManager: struct {
    player_prefab:  PrefabRef,
    bullet_prefab:  PrefabRef,
    coin_pickup:    AudioRef,
    score:          int,
    high_score:     int
}

[System("Update", "Game")]
def shoot(eng: ptr, dt: float, user: ptr): void {
    if (IsKeyPressed(KeyCode().Space)) {
        // Steady-state cost: one u64 compare + one cached pointer return.
        mgr: GameManager? = GameManager();
        match mgr {
            some(m): {
                InstantiatePrefab(m.bullet_prefab, 0.0, 0.0);
                m.score = m.score + 1;
            }
            null: { }
        }
    }
}
```

```cpp
struct GameManager {
    PrefabRef player_prefab;
    PrefabRef bullet_prefab;
    int       score;
};
ZUES_PROJECT_FIELDS    (GameManager, player_prefab, bullet_prefab, score);
ZUES_REGISTER_COMPONENT(GameManager, "Project");

extern "C" void on_load(ZuesEngine* eng, const ZuesHostApi* host) {
    zues::bind(eng, host);
    zues::register_all_components(eng, host);
    host->ensure_singleton(eng,
        host->find_component_id(eng, "GameManager"));
}

extern "C" void shoot(ZuesEngine* eng, float dt, void*) {
    // C++ singleton fetch: find the singleton entity, then read its
    // component pointer. There's no cached `world.singleton<T>()` helper
    // yet; cache the (entity, generation, version) triplet yourself if
    // this becomes hot.
    const ZuesComponentId id =
        zues::g_host->find_component_id(eng, "GameManager");
    const ZuesEntity gm = zues::g_host->find_singleton(eng, id);
    if (gm.generation == 0) return;
    auto* mgr = static_cast<GameManager*>(
        zues::g_host->get_component(eng, gm, id));
    if (mgr) ++mgr->score;
}
```

### How the cache works

The plugin emits one cached pointer + one `archetype_version` snapshot
per `[Singleton]` type. Steady-state path: compare cached version
against `host->world_version(eng)`. If equal, return the cached pointer.
If not, look up the singleton entity (`find_singleton`), fetch its
component pointer (`get_component`), record the new version, return.

Cost amortises to **one u64 compare + one indirection** for every call
that follows a no-op archetype graph state. Mutations bump the version
once each (component add/remove, world clear/load, hot-reload), so
even heavy spawn loops pay the refresh exactly once per "shape change."

### Singleton entities in the editor

Singletons appear in a pinned **Globals** section at the top of the
Hierarchy panel -- collapsible, always above the regular scene tree.
Their auto-assigned name is `<Type> (singleton)`; you can rename them
freely. Delete is disabled (an auto-respawn would just re-create them
on the next tick); remove the `[Singleton]` flag if you really want
the entity gone.

A single entity can host multiple singleton components -- useful for
"one Manager entity holding all globals":

```lync
[Component, Singleton] GameConfig:  struct { ... }
[Component, Singleton] InputConfig: struct { ... }
// Both end up on the same entity if you add them via the inspector;
// the plugin's adopt-existing path detects each component and reuses
// the entity instead of spawning a second one.
```

### Per-instance refs vs singleton refs

Use this rule of thumb when deciding where a `*Ref` field belongs:

| Where the ref lives                          | When                                          |
| -------------------------------------------- | --------------------------------------------- |
| Field on a per-instance component             | Each entity points at a *different* thing -- `Follower.target`, `Spawner.what_to_spawn` |
| Field on a `[Singleton]` component            | Project-wide config -- `GameManager.bullet_prefab`, `AudioConfig.coin_sound` |
| Hard-coded path string in a system            | **Never** -- that's the bug Singleton + asset registry exists to fix |

## Iteration semantics

`Each<T>` only visits entities that **have** T. No null checks needed
inside the callback -- the pointer is always valid.

```lync
[System("PreUpdate", "Game")]
def movement(eng: ptr, dt: float, user: ptr): void {
    Each<Velocity>(apply_velocity);
}
// Each<T>'s callback signature is (EntityRef, ref T, float). `ref` means
// the component pointer is non-null inside the body; mutate freely.
def apply_velocity(e: EntityRef, v: ref Velocity, dt: float): void {
    match e.Get<Transform2D>() {
        some(pos): e.SetTransformPosition(
                    pos.position.x + v.vx * dt,
                    pos.position.y + v.vy * dt);
        null:      { /* entity has Velocity but no Transform2D */ }
    }
}
```

Things to know inside the callback:

- It's safe to **read other components** of the same entity.
- It's safe to **mutate the component you're iterating** -- the data
  pointer is stable for the visit.
- `DestroyEntity(e)` is queued to end-of-walk; safe to call.
- Don't add components of the SAME type you're iterating during the
  walk -- that resizes storage. Add other types freely.

## Rules + limits

- Trivially copyable, trivially destructible.
- No `std::string` / `std::vector` / owning pointers -- use fixed-size
  buffers + handles.
- Lync tags need at least one field (a single `_: int` is fine).
- Component count per entity: practically unlimited; per-frame iteration
  cost scales with the number of entities matching the query, not the
  total entity count.
- Field reorder / type change between sessions is **safe** -- the world
  loader is schema-tolerant (see `worlds`).

## Migration

The old snake_case free-function names (`AddHealth`, `HasHealth`,
`GetHealth`, `EachHealth`, `RemoveHealth`) still compile as aliases.
New code should use the template form.

| Old (legacy alias)      | New (primary form)              |
| ----------------------- | ------------------------------- |
| `AddHealth(e, ...)`     | `Add<Health>(e, Health{ ... })` |
| `e.HasHealth()`         | `e.Has<Health>()`               |
| `e.GetHealth()`         | `e.Get<Health>()`               |
| `EachHealth(cb)`        | `Each<Health>(cb)`              |
| `RemoveHealth(e)`       | `Remove<Health>(e)`             |
