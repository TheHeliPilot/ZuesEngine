# Worlds

A world is the persisted state of the entity scene: every entity, every
component, every value. Stored under `<project>/assets/worlds/` as a
`.zworld` JSON file so it shows up in the Asset Browser and diffs
cleanly in git.

## Save / load

- **Ctrl+S** saves the active world. If the world has no path yet
  (fresh project), the first save pops *Save World As*.
- **File -> Save World As** -> type a name (no folder picker). Lands in
  `assets/worlds/<name>.zworld`.
- **File -> Open World** or double-click any `.zworld` in the Asset
  Browser.
- The first world saved in a fresh project becomes its `default_world`
  automatically. Project Settings has a dropdown to change it.

The Hierarchy panel header shows the active world name; an `*` marks
unsaved changes. Right-click the header for Save / Save As / Open.

## Auto-load on editor start

When the editor opens a project it picks the world to load in this order:

1. **`recent_worlds[0]`** -- the last world you had open. This is the
   common case ("pick up where I left off") and wins over the
   project's default world setting.
2. **`project.default_world`** -- falls back when recents is empty
   (first session in a freshly-created project).
3. Otherwise the editor opens with an empty world; first save lands
   in `assets/worlds/<name>.zworld` and becomes default.

`recent_worlds.json` lives next to the editor exe; up to 5 entries.

## Format

`.zworld` is JSON. Top-level shape:

```json
{
  "version": 1,
  "next_name_index": 12,
  "entities": [
    {
      "index": 0,
      "generation": 1,
      "components": [
        { "type": "Transform2D",
          "data": { "position": [3.0, 5.0], "rotation": 0, "scale": [1,1] } },
        { "type": "Sprite",
          "data": { "size": [1,1], "tint": [1,1,1,1], "texture": 0 } }
      ]
    }
  ]
}
```

Field values map to JSON natively per `FieldKind`:

| FieldKind        | JSON shape                            |
|------------------|---------------------------------------|
| `Bool`           | `true` / `false`                      |
| Integer kinds    | number                                |
| `F32` / `F64`    | number (printf `%.9g` round-trips)    |
| `Vec2`/`3`/`4`   | `[x, y, ...]`                         |
| `Color`          | `[r, g, b, a]`                        |
| `Entity`/`Ref`   | `[index, generation]`                 |
| `PrefabRef`/...  | `"a3f2c1..."` (32-hex GUID, `""` = null) |
| `CharBuffer`     | `"string"` (truncated to N-1 chars)   |
| `Enum`           | number (underlying integer)           |

Components are referenced by **name string**, not by stored id --
hot-reload safe across DLL restarts. Fields are also keyed by name, so
adding/removing/reordering struct fields between sessions doesn't
corrupt the load.

## Schema-tolerant reload

The world load path is built to survive structural changes between
saves. After you edit a component (rename a field, change `int -> Vec2`,
flip `Vec3 -> Vec2`, drop a field), reloading the world keeps every
entity alive -- only the changed slots reset to zero/default.

Concretely:

- **Field added** -> reads as zero-init (or the prototype default).
- **Field removed** -> silently dropped on load.
- **Field renamed** -> looks like "removed + new"; old value lost, new
  reads zero. (Plan: rename hints to migrate.)
- **Field type changed** (e.g. `int -> Vec2`, or `Vec3 -> Vec2`) -> the
  reader saves cursor -> tries the new shape -> on mismatch rewinds and
  skips that JSON value, leaving the field zero-initialised. The
  entity, archetype, and other fields all survive intact.
- **Vec2 ↔ Vec3 ↔ Vec4** -> reads however many components the file has;
  extras are dropped, missing ones stay zero.
- **Component type removed from project DLL** -> the entity loads
  without that component (the data section is skipped).

This is what makes "edit a Lync struct, hit Save, reload" feel
instantaneous instead of corrupting your scene.

## Undo + redo

Snapshot-based: every undoable action calls `world.save_json()` before
the mutation; **Ctrl+Z** restores the snapshot, **Ctrl+Y** /
**Ctrl+Shift+Z** replays forward. Up to 100 entries by default; the
oldest gets dropped when the cap fills.

Wrapped actions today:

- Transform gizmo drag (move / rotate / scale)
- Collider edit-in-scene drag (handles)
- Inspector field edits (per-field, brackets `IsItemActivated` ->
  `IsItemDeactivatedAfterEdit`)
- Prefab instantiate

Cost is one JSON snapshot per undoable action -- a few KB for a small
scene. The "after" state is captured on the fly when you press Ctrl+Z,
so the redo stack is always in sync. Any new action wipes the redo
branch (standard).

## Play / Stop snapshot

Hitting Play snapshots the current world. Stop restores from that
snapshot -- playtest mutations don't persist. Same JSON path as Ctrl+S
under the hood.

## Project layout

```text
my_project/
  MyGame.zuesproject
  assets/
    worlds/
      main.zworld
      level_1.zworld
    prefabs/
      Player.zprefab
      Bullet.zprefab
    sprites/
      hero.png
      hero.png.meta
  src/
    Velocity.lync
    movement_system.lync
    _zues_main.lync       <- auto-generated, never edit
```

## Why prefer worlds over `[OnLoad]` spawning

- Visual editing in the Hierarchy.
- No code change to add or rearrange entities.
- Diff-friendly in git.
- Mod-friendly (ship multiple worlds).
- Same code path drives Play/Stop snapshot, undo/redo, hot-reload
  recovery -- write data once, get all three behaviours for free.

Keep `[OnLoad]` for resource preload and singleton wiring; entity
content belongs in `.zworld`.
