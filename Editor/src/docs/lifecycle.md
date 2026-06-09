# Lifecycle hooks

Six optional hooks per project DLL. They run when the engine loads,
ticks, unloads, and when physics contacts are detected. Most projects
keep these tiny and let worlds + systems do the heavy lifting.

**Snippet expansion:** type an attribute on its own line and press Enter
in the Lync editor -- the matching function skeleton is inserted with
the cursor placed inside the body. For `[System(...)]` the placeholder
name is pre-selected so you can type to replace it immediately.

## Declare

```lync
[OnLoad]
def my_load(eng: ptr, host: ptr): int {
    LogInfo("project loaded");
    return 0;
}

[OnUpdate]
def my_update(eng: ptr, dt: float, user: ptr): void {
    // Per-frame, non-entity work. Prefer a [System] for entity work.
}

[OnUnload]
def my_unload(eng: ptr): int {
    LogInfo("project unloading");
    return 0;
}

[OnCollision]
def my_collision(eng: ptr, a: EntityRef, b: EntityRef): void {
    // Called when two physics bodies begin to overlap.
    // Use a.Get<T>() / b.Get<T>() to inspect components.
}

[OnTriggerEnter]
def my_trigger_enter(eng: ptr, self: EntityRef, other: EntityRef): void {
    // self has a sensor collider; other entered it.
}

[OnTriggerExit]
def my_trigger_exit(eng: ptr, self: EntityRef, other: EntityRef): void {
    // other left self.
}
```

```cpp
#include <zues/cpp_helpers.h>

extern "C" void on_load(ZuesEngine* eng, const ZuesHostApi* host) {
    zues::bind(eng, host);                  // wire up the helpers
    zues::register_all_components(eng, host);

    // Add systems explicitly -- C++ has no [System] attribute.
    host->add_system_with_domain(eng, "movement",
        ZUES_PHASE_PRE_UPDATE, ZUES_DOMAIN_GAME, movement, nullptr);

    // Spawn singleton entities (the lync plugin does this for you).
    host->ensure_singleton(eng,
        host->find_component_id(eng, "GameManager"));
}

extern "C" void on_update(ZuesEngine* eng, float dt) {
    // Optional per-frame hook. Most logic should live in systems.
}

extern "C" void on_unload(ZuesEngine* eng) {
    // Free any resources you allocated in on_load.
}

extern "C" const ZuesProjectApi* zues_project_entry(void) {
    static const ZuesProjectApi api = {
        .abi_version       = ZUES_PROJECT_API_VERSION,
        .on_load           = on_load,
        .on_update         = on_update,
        .on_unload         = on_unload,
        .on_collision      = nullptr,
        .on_trigger_enter  = nullptr,
        .on_trigger_exit   = nullptr,
    };
    return &api;
}
```

In the lync flow, `zues_project_entry`, the per-component
registrations, the system calls, and the `ensure_singleton` calls are
all emitted by the plugin. C++ projects declare `zues_project_entry`
themselves (one-time boilerplate).

## Where do they live?

In **lync**: anywhere in any `.lync` file under `src/`. The plugin
scans every file in the project, collects every `[OnLoad]` /
`[OnUpdate]` / `[OnUnload]` it finds, and chains them all into a
single project entry. You don't need to write or see the entry
point. The auto-generated `_zues_main.lync` (hidden in the Source
pane) is the manifest the editor maintains.

In **cpp**: write `on_load` / `on_update` / `on_unload` in any of the
.cpp files your `mygame.dll` target compiles. There's only one
`zues_project_entry` per DLL -- the entry struct points at the specific
functions to call.

## Multiple hooks of the same kind

Lync: yes. Several files can each have their own `[OnLoad]`, and they
all run at project load (in source order). Same for `[OnUpdate]` (each
ticks every frame) and `[OnUnload]`. Useful for keeping per-feature
setup with its feature file:

```lync
// audio.lync
[OnLoad]
def audio_init(eng: ptr, host: ptr): int { return 0; }

// input.lync
[OnLoad]
def input_init(eng: ptr, host: ptr): int { return 0; }
```

```cpp
// In cpp you have one on_load. Compose by calling helpers from it:
extern "C" void on_load(ZuesEngine* eng, const ZuesHostApi* host) {
    zues::bind(eng, host);
    zues::register_all_components(eng, host);
    audio_init(eng, host);
    input_init(eng, host);
}
```

## Logging

Four severities, each routed to the editor's Console panel + stdout:

```lync
LogDebug("path-finder considered 42 nodes");
LogInfo("project loaded");
LogWarn("config file not found, using defaults");
LogError("save failed");
```

The Console renders `HH:MM:SS  LVL  source: message` with a colored
level letter, a per-source hash color, and dot-depth indented sources.

C++ uses the `ZUES_LOG_*` macros from `<zues/log.h>`:
`ZUES_LOG_INFO("msg")`, `ZUES_LOG_WARN`, `ZUES_LOG_ERROR`, etc. The
module name (`source` column in the console) comes from the build's
`ZUES_MODULE_NAME` define automatically.

## When to actually use them

**OnLoad / OnUpdate / OnUnload are escape hatches.** Reach for them
only when something genuinely can't be expressed as data + a system.

| You want to ...                                | Do this instead                          |
| ---------------------------------------------- | ---------------------------------------- |
| Spawn a player entity                          | Save it in the world file                |
| Per-frame work that touches entities           | Write a `[System]`                       |
| Project-wide config (prefab refs, audio refs)  | Use a `[Singleton]` component            |
| Hot reload survives                            | All of the above (state lives in world)  |
| One-time resource preload                      | `OnLoad` (this is the right hook)        |
| Plugin / network handshake init                | `OnLoad`                                 |
| Cleanup engine doesn't auto-handle             | `OnUnload`                               |

Most projects end up with empty hooks and a few systems. That's the
goal.

## Hot reload

The editor recompiles on every `.lync` save and swaps the DLL while
the editor stays open. The world is snapshotted (`save_json` ->
schema-tolerant `load_json`) so entity state survives the swap; system
registrations clear; `[OnUnload]` runs first, then the new DLL's
`[OnLoad]` runs. Singleton entities are adopted (not re-created) by
the new DLL's `ensure_singleton` calls.

If you keep `OnLoad` empty (just resource preload) and put gameplay
state in the world or in singleton components, hot reload feels
instantaneous: edit lync, hit Ctrl+S, see the change.
