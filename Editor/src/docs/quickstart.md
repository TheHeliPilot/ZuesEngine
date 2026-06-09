# Quickstart

Open the launcher, create a project, hit Play.

## 1. New project

Launcher -> **New Project** -> pick a folder + name. The editor opens
with an empty world.

The project lives at `<folder>/<name>.zuesproject` with this layout:

```text
my_project/
  MyGame.zuesproject
  assets/
    worlds/                   <- .zworld scene files (JSON, diff-friendly)
    prefabs/                  <- .zprefab assets (JSON)
    sprites/                  <- .png/.jpg + auto-minted .meta sidecars
  src/                        <- .lync (or .cpp) source
```

## 2. Add an entity (visually)

Hierarchy panel -> **+ New Entity** (or Ctrl+N). It gets `Transform2D`
+ `Name` auto-attached. Click it; edit fields in the Inspector. Save
the world with **Ctrl+S** -- it lands in `assets/worlds/<name>.zworld`
and becomes your project's start world automatically (first save wins).

## 3. Add a component to it

Inspector -> **Add Component** -> start typing. Engine builtins live
under `Engine/...`, your own under `Project/...` (or whatever
`[Category("...")]` says).

## 4. Write a component + system

Source pane -> **+ Component** -> name it. Then **+ System** -> name it.
Templates open in the Lync editor (or your editor of choice for `.cpp`
files).

```lync
// Velocity.lync
[Component] Velocity: struct { vx: float, vy: float }

// movement_system.lync
[System("PreUpdate", "Game")]
def movement(eng: ptr, dt: float, user: ptr): void {
    Each<Velocity>(tick);
}
// Each<T>'s callback gets (EntityRef, ref T, dt: float). `ref` is a
// non-null borrowed pointer -- no match-unwrap needed because the engine
// only invokes for matching entities. dt is the live frame dt threaded
// from the calling system.
def tick(e: EntityRef, v: ref Velocity, dt: float): void {
    match e.Get<Transform2D>() {
        some(pos): e.SetTransformPosition(
            pos.position.x + v.vx * dt,
            pos.position.y + v.vy * dt);
        null: { /* no Transform2D - skip */ }
    }
}
```

```cpp
// velocity.h
#include <zues/cpp_helpers.h>
struct Velocity { float vx; float vy; };
ZUES_PROJECT_FIELDS    (Velocity, vx, vy);
ZUES_REGISTER_COMPONENT(Velocity, "Project");

// movement_system.cpp
extern "C" void movement(ZuesEngine* eng, float dt, void* user) {
    zues::Each<Velocity>([&](int e_idx, Velocity* v) {
        if (auto* pos = zues::Get<Transform2D>(e_idx)) {
            pos->position.x += v->vx * dt;
            pos->position.y += v->vy * dt;
        }
    });
}
// In on_load:
//   host->add_system_with_domain(eng, "movement",
//       ZUES_PHASE_PRE_UPDATE, ZUES_DOMAIN_GAME, movement, nullptr);
```

## 5. Run

Hit Play in the toolbar. The world loads, entities spawn, systems tick.
Hit Play again to stop -- entity state restores to the pre-Play snapshot
so playtest mutations don't stick.

## What's actually happening

- Save the world -> JSON `.zworld` under `assets/worlds/`. Just data; no
  code involved.
- Create a `[Component]` / `ZUES_REGISTER_COMPONENT` -> editor sees it
  in Add Component, world load can deserialise it, the lync plugin
  auto-generates `Add<T>` / `Each<T>` / etc.
- `[System(...)]` -> ticks every frame in the chosen phase + domain
  (lync). C++ projects call `add_system_with_domain` from `on_load`.
- Entity setup lives in the world file, not in code. `[OnLoad]` is for
  resource preload + plugin init.

## Lync ergonomics cheat-sheet

```lync
// Compound assignment + increment / decrement
hp += 10;     hp -= 1;     hp *= 2;     hp /= 2;     hp %= 4;
hp++;         hp--;
v.x += dx;    arr[i] += 1;

// Nullable typed pointer + match unwrap
match e.Get<Health>() {
    some(h): { h.hp -= 1; }   // h has type Health* inside this arm
    null:    { /* no health on this entity */ }
}

// Truthy conditions: bool, int (non-zero)
count: int = 5;
while (count) { count -= 1; }

// KeyCode singleton -- Unity-style named constants
if (IsKeyDown(KeyCode().W)) { /* W held */ }
if (IsKeyPressed(KeyCode().Space)) { /* jump */ }
```

## Next steps

- **Lync** -- the language itself: types, ownership, match, attributes.
- **Components** -- declare data shapes, asset references, singletons.
- **Systems** -- write per-frame behaviour with phases + domains.
- **UI** -- `UIAnchor` + `Text` for HUD overlays. Rendered by the engine,
  ships with the runtime.
- **Worlds** -- scene files, undo/redo, schema-tolerant reload.
- **Prefabs** -- save an entity tree as an asset; instantiate by drag,
  by string path, or by typed `PrefabRef`.
- **Physics** -- `RigidBody` + colliders, in-scene edit handles,
  `[OnCollision]` / `[OnTriggerEnter]` / `[OnTriggerExit]`.
- **Editor** -- every panel, every shortcut, the build pipeline.
- **Templates** -- `def name<T>(...)` and `struct Name<T> { ... }`.
- **Lifecycle** -- `[OnLoad]` / `[OnUpdate]` / `[OnUnload]` and when
  *not* to use them.
- **Distributing** -- ship your game (Build -> Export) or share the
  engine itself with a friend.
