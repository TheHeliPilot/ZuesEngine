# Architecture Overview

Zues is a 2D multiplayer-first game engine with modular DLL subsystems.

## Pitch

Plug-and-play multiplayer. Mark a component `[[Engine::replicated]]`, mark inputs `[[Engine::predicted_input]]`, run the game — friend joins via connection info. Prediction + reconciliation + interpolation are automatic.

## Binary layout at runtime

```
editor.exe        ── host app (ImGui-based)
 ├── zues_core.dll          ── module loader, service registry, event bus, ECS, math, reflection, log
 ├── zues_window_glfw.dll   ── IWindow service
 ├── zues_input_glfw.dll    ── IInput service
 ├── zues_renderer_gl.dll   ── IRenderer_2D service
 ├── zues_physics_box2d.dll ── IPhysics2D service
 ├── zues_net_udp.dll       ── INetTransport service
 ├── zues_ui.dll            ── UI component & systems
 └── mygame.dll             ── user project (components + systems)
```

## Rules of the graph

- Core knows nothing about editor, modules, or project.
- Modules link Core only. Modules don't link each other.
- Editor links Core and loads modules + project.dll at runtime.
- **Project.dll never links Core at build time.** It uses a pure C ABI vtable handed in at load.

## Startup flow

1. `editor.exe` launches, links `zues_core.dll`.
2. Core calls `engine_startup`, scans the modules folder.
3. For each `zues_*.dll`: `LoadLibrary` → call `zues_module_entry()` → verify ABI → call `on_load` → module registers its services.
4. After all modules load: call `on_ready` on each (can now reach other services).
5. Editor loads `mygame.dll` via the project-API C ABI, calls `on_load(host_api)`.
6. Project registers its components and systems. Engine runs.

## Hot reload

- **Project.dll:** file watcher → serialize world → unload → rebuild → reload → re-register components → deserialize → resume.
- **Module DLL:** not v1. Load-once. Consider later if users ask.

## Two mental models

- **Data flow:** input → ECS systems (per phase) → render. Frame-driven. Single thread for now.
- **Capability lookup:** services found by string ID + version. Any module can ask for any other service by name.
