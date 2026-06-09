# Project Structure

```
ZuesEngine/
├── CMakeLists.txt              — root build
├── CMakePresets.json           — IDE configs
├── .clang-format
├── .gitignore
│
├── cmake/                      — shared CMake helpers
│   ├── ZuesCompilerFlags.cmake
│   ├── ZuesModule.cmake
│   └── ZuesDependencies.cmake
│
├── Core/                       — zues_core.dll
│   ├── include/Engine/
│   │   ├── api.h               — ZUES_API macro
│   │   ├── types.h             — u32, f32, Handle, Result
│   │   ├── engine.h            — startup/shutdown/services/events
│   │   ├── module.h            — ModuleInfo, ModuleContext, ABI consts
│   │   ├── service.h           — ServiceRegistry
│   │   ├── events.h            — EventBus
│   │   ├── log.h               — log_write + macros
│   │   ├── math.h              — umbrella header
│   │   ├── math/               — vec2, vec3, vec4, mat4, quat, rect
│   │   └── ecs/                — entity, world, query, system (stubs)
│   ├── src/                    — implementations
│   └── CMakeLists.txt
│
├── Editor/                     — editor.exe (ImGui-based host)
│   ├── src/main.cpp
│   └── CMakeLists.txt
│
├── Modules/                    — each subsystem is one DLL
│   ├── CMakeLists.txt          — aggregator
│   ├── Window_GLFW/
│   ├── Renderer_GL/
│   ├── Input_GLFW/    (later)
│   ├── Physics_Box2D/ (later)
│   ├── Net_UDP/       (later)
│   └── UI/            (later)
│
├── ProjectAPI/                 — pure C ABI, header-only
│   ├── include/Engine/project_api.h
│   └── CMakeLists.txt
│
├── MyGame/                     — sample user project
│   ├── src/mygame.cpp
│   └── CMakeLists.txt
│
└── docs/                       — AI-first docs
```

## What goes where

- **Core/** — everything reused by editor and any module. NO rendering, NO physics, NO networking in Core. Those are modules.
- **Modules/** — DLL plugins. Each module owns one subsystem and registers services.
- **Editor/** — the development application. Uses ImGui. Hosts the project.dll.
- **ProjectAPI/** — header-only, pure C. The contract between editor and project.dll. MUST stay C-safe.
- **MyGame/** — reference project. Users copy this to start their own game.

## Build outputs

All target files land in `build/<preset>/bin/`:
- `editor.exe`
- `zues_core.dll` + `.lib`
- `zues_<module>.dll` per module
- `mygame.dll`

The editor loads modules from its own directory at runtime.
