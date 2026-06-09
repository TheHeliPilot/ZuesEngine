# Prefabs and References

A prefab is one entity (plus all its children) saved to disk as a
single asset. Drop the file into the scene to spawn an instance -- same
shape, fresh entity ids, ready to be tweaked or destroyed without
touching the original.

References (`PrefabRef`, `EntityRef`, `SpriteRef`, ...) are typed slots
on components. They survive moves, renames, and merges because they
store a stable GUID, not a path or a slot index.

## Asset GUIDs

Every asset gets a 128-bit GUID the first time it's seen by the editor.
Two storage shapes -- picked by file type:

- **Owned formats** (`.zprefab`, `.zsprite`, `.zworld`) -- the GUID lives
  at top level inside the JSON: `{ "guid": "a3f2c1...", ... }`. One
  file, one source of truth.
- **Foreign formats** (`.png`, `.wav`, `.ttf`, ...) -- the GUID lives in
  a sidecar: `<asset>.meta`. Required because we can't embed metadata
  in someone else's binary format without breaking it.

The editor walks `assets/` once at project load, fills the
`AssetRegistry`, and any subsequent reference resolves through it.
Move or rename a file in the asset browser and references stay valid;
the GUID followed it.

## Saving a prefab

1. Build the entity in the scene -- children, components, the lot.
2. Right-click it in the Hierarchy -> **Save as Prefab**.
3. The editor writes `assets/prefabs/<Name>.zprefab` and registers the
   new asset. Duplicate names get `_1`, `_2`, ... so nothing is
   silently overwritten.

The file captures every component and every descendant. The root's
`Parent` and `NextSibling` are deliberately stripped -- placement is
the caller's job at instantiate time.

## Instantiating

Three flavours, all returning the spawned root entity (or null on
failure):

### Editor

Drag the `.zprefab` from the Asset Browser onto the Scene viewport.
The root spawns at the cursor's world position; descendants keep
their offsets. Wrapped in undo (Ctrl+Z reverses the spawn).

### Lync -- string path

```lync
// Pragmatic: doesn't require a populated PrefabRef field.
bullet: EntityRef = Instantiate("assets/prefabs/Bullet.zprefab", x, y);
```

Path is project-relative with forward slashes.

### Lync -- typed `PrefabRef`

When the prefab to spawn is configurable per entity or per project,
declare it as a `PrefabRef` field and let the editor's drag-drop
populate it. The value is GUID-keyed under the hood, so renames and
moves of the prefab file don't break it.

```lync
[Component, Singleton]
GameManager: struct {
    bullet_prefab: PrefabRef,
    enemy_prefab:  PrefabRef
}

[System("Update", "Game")]
def shoot(eng: ptr, dt: float, user: ptr): void {
    if (IsKeyPressed(KeyCode().Space)) {
        match GameManager() {
            some(cfg): InstantiatePrefab(cfg.bullet_prefab, 0.0, 0.0);
            null:      { }
        }
    }
}
```

Each instantiation makes brand-new entities. Intra-prefab `EntityRef`
fields are remapped to the new ids; refs that pointed *outside* the
saved subtree drop to NULL -- they wouldn't survive the trip anyway.

## Reference field types

| Field type    | What it stores                  | Inspector UI                                        |
| ------------- | -------------------------------- | --------------------------------------------------- |
| `EntityRef`   | Live entity in the same world    | Drop target -- drag from Hierarchy. Right-click clears. |
| `PrefabRef`   | GUID of a `.zprefab` asset       | Drop target -- drag from Asset Browser.              |
| `SpriteRef`   | GUID of a `.zsprite` asset       | Same.                                               |
| `TextureRef`  | GUID of a `.png`/`.jpg` asset    | Same.                                               |
| `AudioRef`    | GUID of a `.wav`/`.ogg` asset    | Same.                                               |
| `FontRef`     | GUID of a `.ttf`/`.otf` asset    | Same.                                               |

Asset-ref drops are kind-checked: dropping a `.png` onto a `PrefabRef`
slot is silently rejected. `EntityRef` is layout-equivalent to the
internal `Entity` handle (8 bytes); the asset refs each store a
16-byte GUID.

## Where refs should live

| Ref placement                                | Use case                                     |
| -------------------------------------------- | -------------------------------------------- |
| Field on a per-instance component             | Each entity points at a *different* thing -- `Follower.target`, `Spawner.what_to_spawn` |
| Field on a `[Singleton]` component            | Project-wide config -- `GameManager.bullet_prefab`, `AudioConfig.coin_sound` |
| Hard-coded path in a system                   | **Don't.** That's the bug Singleton + asset registry exists to fix |

`[Singleton]` components carry zero per-instance memory cost (one
designated entity, one component instance) while keeping the data
inspectable, serialisable, undoable, and hot-reload-safe. See
`components` for the full singleton primer.

## v1 limits

- No override tracking -- edits to a spawned instance don't propagate
  back to the source `.zprefab`, and editing the source doesn't push
  changes into already-spawned instances. Both ship in v2.
- No nested prefabs (a prefab inside a prefab). Ships in v3.
- The editor's "Save as Prefab" stores everything in the subtree;
  there's no per-component opt-out yet.

## Cookbook

### Spawn a player at the saved spawn point

```lync
[Component, Singleton]
LevelConfig: struct {
    player_prefab: PrefabRef,
    spawn_x: float,
    spawn_y: float
}

[OnLoad]
def boot(eng: ptr, host: ptr): int {
    match LevelConfig() {
        some(cfg): InstantiatePrefab(cfg.player_prefab, cfg.spawn_x, cfg.spawn_y);
        null:      { }
    }
    return 0;
}
```

Inspector flow: drop the `.zprefab` onto `LevelConfig.player_prefab`,
type the spawn coords. No code change to swap the player prefab.

### Pool of bullets fired by a system

```lync
[System("Update", "Game")]
def shoot(eng: ptr, dt: float, user: ptr): void {
    if (IsKeyPressed(KeyCode().Space)) {
        match GameManager() {
            some(mgr): {
                bullet: EntityRef = InstantiatePrefab(mgr.bullet_prefab, px, py);
                bullet.SetVelocity(0.0, 600.0);
            }
            null: { }
        }
    }
}
```

### Wire a target reference at edit time

In C++ component declarations, an `EntityRef` field shows up as an
empty slot in the inspector:

```cpp
struct Follower {
    Engine::ecs::EntityRef target;
    float                  speed = 50.0f;
};
ZUES_PROJECT_FIELDS    (Follower, target, speed);
ZUES_REGISTER_COMPONENT(Follower, "Project");
```

Drop the entity onto the slot in the Inspector. The system reads
`follower.target` like any other component handle:

```cpp
zues::Each<Follower>([&](int e_idx, Follower* f) {
    if (f->target.is_null()) return;
    Engine::ecs::Entity tgt = f->target.to_entity();
    // ...
});
```

In Lync, `EntityRef` is a normal struct with `index` and `generation`
fields:

```lync
[Component] Follower: struct {
    target: EntityRef,
    speed:  float
}

[System("PreUpdate", "Game")]
def follow(eng: ptr, dt: float, user: ptr): void {
    Each<Follower>(tick);
}
def tick(e: EntityRef, f: Follower?): void {
    match f {
        some(fol): {
            if (fol.target.generation == 0) { return; }   // null
            // fol.target is an EntityRef -- use UFCS:
            // fol.target.Get<Transform2D>() etc.
        }
        null: { }
    }
}
```
