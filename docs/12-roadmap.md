# Implementation Roadmap

Phases in order. Each phase leaves the engine in a usable, demo-able state.

## Phase 0 — Scaffolding (done)

Goal: CMake + CLion integration. Empty core/editor/module/project build and link.

- [x] Root `CMakeLists.txt`, `CMakePresets.json`
- [x] `.clang-format`, `.gitignore`
- [x] `cmake/` helpers (flags, module, deps)
- [x] `ProjectAPI/` C ABI header
- [x] `Core/` library skeleton (headers + stub sources)
- [x] `Editor/` exe skeleton
- [x] Two module stubs: `Window_GLFW`, `Renderer_GL`
- [x] `MyGame/` sample project skeleton
- [x] Docs (this folder)
- [x] Configure + build passes

## Phase 1 — Engine core runtime (done)

Goal: modules actually load. Services register and get called. Events dispatch.

- [x] Module loader (`LoadLibrary`/`dlopen`, symbol resolve, ABI check)
- [x] `ServiceRegistry` real implementation
- [x] `EventBus` raw subscribe + publish
- [x] `EventBus` typed wrapper — `bus->subscribe<E>(handler)` / `bus->publish(E{...})`. Events declare `static constexpr const char* EVENT_ID`.
- [x] `log` basic impl (stdout/stderr). File sink → later if needed.
- [x] Reflection infrastructure probe (`-freflection-latest` compiler check)
- [x] Module-load test — `IWindowProbe_v1` service registered by `zues_window_glfw`, called from editor across the DLL boundary. Verifies module → service registry → cross-DLL function call chain.

## Phase 2 — ECS (done)

- [x] **2.1** Storage: generational `Entity`, `Archetype`, migration on add/remove.
- [x] **2.2 Stage A** Reflection scaffolding: `FieldInfo`, `ComponentFieldsOf<T>`, macro path + reflection path. Toolchain set up via Docker (`zues-toolchain` image with clang-p2996 + cmake + ninja). See [13-clang-p2996-toolchain](13-clang-p2996-toolchain.md). Auto-introspection verified — type names are real (`vec2`, `SortMode`, `int32_t`, `bool`) under reflection mode.
- [x] **2.2 Stage B** Built-in components in `Core/include/zues/components/`: `Transform2D`, hierarchy primitives (`Parent`, `FirstChild`, `NextSibling`), `Name`, `Disabled`, `Lifetime`, `Camera2D` (with `SortMode`), `Sprite`, `Text`, `RenderLayer`, `SortingGroup`. Auto-registered via `world.register_builtins()`. Hierarchy helper API on `World` (`set_parent`, `unparent`, `parent_of`, `iterate_children`, `child_count`).
- [x] **2.3** Queries: `world.query<A, B>().each(fn)` with `.without<X, Y>()` exclusion filter. Type→id mapping cached via `typeid(T).name()`. Non-template `iterate_query` keeps `Archetype` private.
- [x] **2.4** Systems + phases: `world.add_system(name, phase, fn, user)` returns `SystemHandle`. Fixed phase order (Input → PreUpdate → Physics → PostUpdate → NetReplicate → UiInput → UiLayout → Render → UiRender) executed by `world.tick(dt)`. Per-phase tick available via `world.tick_phase(phase, dt)`. Verified: registered movement system, ticked 3× at dt=0.5, position incremented {3,0} → {4.5,0}.
- [x] **2.5** Binary world save/load via `save_binary()` / `load_binary(data, size)`. Round-trips all entities, archetype layout, generations, component data. Type matching by name + size + align — schema mismatch returns `Result::AbiMismatch`. JSON form deferred. Verified: saved 693 bytes, destroyed 2 entities, loaded back, full state restored including post-tick component values.

## Phase 2 — ECS

Goal: create entities, attach components, iterate via queries, systems run in phases.

- Generational entity IDs
- Archetype graph: type-set → chunks
- Chunk allocator (fixed 16 KiB chunks)
- Component registration (reflection-driven when `ZUES_HAS_REFLECTION`, macro fallback otherwise)
- Queries with required / optional / excluded
- System registration + ordered phase execution
- World `save` / `load` (JSON + binary via reflection)

## Phase 3 — Window + Renderer ← current

Goal: open a window, draw sprites at 60fps.

- [x] **3.1** `Engine::math` glm-backed (private dependency of Core; user code never sees glm).
- [x] **3.2** `zues_window_glfw` module — real GLFW. Creates 1280x720 window in `on_load`, owns GL 4.5 context, exposes `IWindow_v1` service (`should_close`, `request_close`, `poll_events`, `swap_buffers`, `get_size`, `get_proc_address`). ESC closes the window.
- [x] **3.3** `zues_renderer_gl` module — minimal hand-rolled GL 2.0+ function loader (27 entry points), `IRenderer_2D_v1` with `begin_frame` (clears with color), `end_frame`, `draw_quad` (real implementation: vertex + fragment shader, unit-quad VBO/EBO/VAO, screen-pixel ortho projection, alpha blending). No glad/GLEW dependency.
- [x] **3.4** Editor game loop driven by services. Polls events, runs N frames or until close, animates clear color + 3 colored quads bouncing vertically. Verified: 600 frames @ 60fps, clean GL teardown on shutdown.
- [x] **3.5b** Sprite batcher + texture loading. stb_image fetched as a header for the renderer module. New `IRenderer_2D_v1` v2 fields: `load_texture_from_memory`, `load_texture_from_file`, `free_texture`, `get_texture_size`, `draw_sprite(texture, x,y,w,h, u0,v0,u1,v1, color)`. Single shader for textured + colored quads (1×1 white texture for `draw_quad`). Batches grouped by texture, MAX_SPRITES = 4096 per drawcall, single VBO + pre-baked EBO. `ZUES_SERVICE_RENDERER_2D_VERSION` bumped 1→2. Verified: `ZUES_RENDERER_DEMO=1` runs 100 sprites/frame in 1 drawcall.
- [ ] **3.5c** `Sprite` component → render system integration (query `<Transform2D, Sprite>`, submit drawcalls). Lands when we add a render system module that queries the World.
- [ ] **3.6** `zues_input_glfw` module — `IInput` service (keyboard/mouse/gamepad polling, input events on the bus).
- [ ] **3.7** Text rendering (stb_truetype baked atlas).
- [x] **3.8** Render targets / FBOs. New `IRenderer_2D_v1` v3 fields: `create_render_target(w,h)`, `destroy_render_target`, `resize_render_target`, `bind_render_target(handle or 0)`, `get_render_target_texture(handle)`. Renderer tracks current FBO + size; `bind_render_target` flushes the batcher first to avoid sprite spillover, then updates viewport + projection. `ZUES_SERVICE_RENDERER_2D_VERSION` bumped 2→3.

## Phase 4 — Editor v1 ← current

Goal: open editor, see world, edit entities, hit Play, see game.

- [x] **4.1** ImGui integration (imgui-glfw + imgui-opengl3 backends, v1.91.5-docking via FetchContent). Custom dark theme (`HexToImVec4` + minimal accents), dockspace, main menu bar.
  - **Cross-DLL fix:** glfw built as a SHARED library so window_glfw.dll and editor.exe share one `_glfw` window registry. Without this, `glfwGetWin32Window(handle_from_other_dll)` returns null and imgui's wndproc-hook setup asserts.
  - Console log sink: `set_editor_log_sink` in core; editor pushes lines into a ring buffer; Console panel filters by level + color-codes.
  - Hierarchy panel: walks roots → children via `iterate_alive` + `iterate_children`. Click selects.
  - Inspector panel: enumerates registered components via `iterate_component_types`, renders fields by type via reflection metadata (float / int / bool / vec2 / hex fallback for unknowns).
  - Scene panel: placeholder; FBO render in 4.2.
  - Demo world built at startup (Scene Root + Main Camera + 4 boxes) so panels have content. Real project loading lands in 4.2.
- [x] **4.2** Project system + launcher.
  - `.zuesproject` JSON file format. Loader/saver/skeleton creator in [Core/src/project.cpp](Core/src/project.cpp). nlohmann/json fetched as a private dep of Core.
  - **`zues_launcher.exe`** — separate executable. ImGui app (~720×480), no engine modules. Lists recents from `%APPDATA%\Zues\recents.json`, native Win32 file dialog for "Open Project", folder picker + name field for "New Project" (creates `Assets/`, `Worlds/`, `Source/`, `.zues/cache/`, and the `.zuesproject` file). Spawns `editor.exe --project=<path>` via `CreateProcessA(DETACHED_PROCESS)` and exits. Linux fork/exec path stubbed.
  - **Editor accepts `--project=<path>`** — parses arg, calls `load_project`, logs project name + dir, loads default world via existing `World::load_binary` if it exists. Falls back to demo world otherwise. Project name + dir shown in the menu bar's right side.
- [x] **4.3** Scene viewport with offscreen FBO render. Scene panel ([panel_scene.cpp](Editor/src/panel_scene.cpp)) lazy-creates an RT sized to the panel's content area, resizes on layout change, renders demo content into the RT mid-imgui-frame, then `ImGui::Image`s the RT's color texture (Y-flipped uvs for GL's bottom-up convention). Demo content = animated 60-sprite grid against a procedural 64×64 checker — placeholder until a render system queries `<Transform2D, Sprite>` (lands when project DLL is wired).
- [x] **4.x.a** Project DLL load + lifecycle.
  - `ProjectAPI/include/zues/project_api.h` exposes `ZUES_PROJECT_EXPORT` macro for the project entry symbol.
  - `Core` adds `ProjectBuild { dll_path }` to `Project`; saved/loaded as `"build": { "dll_path": "..." }` in the JSON.
  - Editor: [project_loader.{h,cpp}](Editor/src/) does `LoadLibraryA` / `dlopen`, resolves `zues_project_entry`, ABI-checks, calls `on_load(engine, host) → on_update(engine, dt) per frame → on_unload(engine)`, then `FreeLibrary`.
  - Editor: [host_api.{h,cpp}](Editor/src/) builds a `ZuesHostApi` with thunks: `host_log` → `Engine::log_write`; `host_get_service` → `Engine::services()->get_service`. Engine handle is a sentinel pointer (single-engine process).
  - DLL path resolution priority: `--project-dll=<path>` arg → `.zuesproject` `build.dll_path` → fallback `<editor_dir>/mygame.dll`.
  - Verified end-to-end: editor loads `mygame.dll`, sample logs `mygame: on_load` / heartbeats every 120 ticks via `host->log` / clean `on_unload`. EXIT=0 in both Windows debug and Docker reflection builds.
- [x] **4.x.c** Host API expansion. `ZuesHostApi` v2 adds: `register_component`/`find_component_id`, `create_entity`/`destroy_entity`/`is_entity_alive`, `add_component`/`remove_component`/`get_component`/`has_component`, `add_system(name, phase, fn, user)`, `query_each(required[], excluded[], fn, user)`. New types: `ZuesEntity`, `ZuesPhase`, `ZuesComponentId`, `ZuesSystemFn`, `ZuesQueryFn`. `ZUES_PROJECT_API_VERSION` bumped 1→2. Editor's `host_api.cpp` thunks bridge to `ecs::World`: `set_active_world(&world)` wires the world pointer; system fns wrapped in a closure thunk; project-registered systems tracked + automatically removed on unload via `unregister_project_systems` (so `world.tick` never calls into freed code). Editor loop now calls `world.tick(frame_dt)` after `project_loader.tick(frame_dt)` so registered systems run. Verified: MyGame registers `MyGame.Position`/`MyGame.Velocity` (ids 16/17), spawns 5 entities, registers a movement system on PRE_UPDATE — heartbeat reports "movement system processed 5 entities last frame" each cycle (lambda fires across DLL boundary, mutates Position bytes in editor's heap, no crashes).
- [ ] **4.x.b** Project DLL hot reload — file watcher on the DLL timestamp, save world via `World::save_binary` → `unload` → `LoadLibrary` → `on_load` → restore via `load_binary`. Component re-registration on reload uses the same name → same id mapping (already idempotent in `World::register_component_type`).
- [ ] **4.4** Asset browser panel (file system → `Assets/`).
- [ ] **4.5** Play / Pause / Stop (ticks systems / freezes sim / restores snapshot).
- [ ] **4.6** World save/load wired to the File menu, save to `Worlds/*.world` inside the project.

## Phase 5 — Physics

Goal: rigid body movement, collisions, triggers.

- `zues_physics_box2d.dll` with `IPhysics2D_v1`
- `Body`, `Collider`, `RevoluteJoint`, etc. components
- Sync ECS ↔ Box2D each frame
- Collision events on the event bus

## Phase 6 — Networking

Goal: host a game, friend joins via code, both walk around replicated, prediction smooth.

- `zues_net_udp.dll`: transport with reliability layer
- STUN hole-punch
- `zues_net_replication.dll`: `[[replicated]]` scanning + delta sync
- `zues_net_prediction.dll`: `[[predicted_input]]` + reconciliation
- Interpolation helpers
- Server build target (`Engine build --server`)
- Sample: two MyGame clients connect via localhost

## Phase 7 — Game UI

Goal: menus, buttons, HUDs with artists-drag-not-code.

- `zues_ui.dll`: core UI components
- UI input, layout, render systems
- Nine-slice
- Polished text (kerning, wrapping)

## Phase 8 — Asset pipeline + Assets panel

Goal: drop a PNG into assets, see it in the editor, use it in a scene.

- `.meta` sidecar files
- Texture atlas packing (offline)
- Font baking
- Assets editor panel with thumbnails
- Asset importer tool-module API

## Phase 9 — Polish

- State-preserving hot reload (field migration)
- Tool-module API + sample tool module
- Input recording/replay (debug / bug repros)
- Better error messages everywhere
- Profiler panel
- Perf work where profiler complains
- Docs pass

## After v1

- 3D renderer module
- Rollback netcode module (for users making fighting games)
- Audio module (OpenAL)
- Web/WASM target (Emscripten)
- Mobile target
