# Editor

Fixed editor app (`editor.exe`), ImGui-based, hosts the engine and the user's project. Extensible via tool modules.

## Built-in panels

- **Scene** — viewport. Displays the game's offscreen render target (`ImGui::Image`). Editor camera controls (pan / zoom for 2D).
- **Hierarchy** — tree view of the current world's entities. Drag-reparent, rename, create/delete.
- **Inspector** — shows selected entity's components. Fields edited via reflection metadata. Custom widget per type available via tool modules.
- **Assets** — file browser of the project's assets folder + asset metadata editing.
- **Console** — log output (color-coded by level) + command input.
- **Project Settings** — engine config, build flags, module enable/disable.
- **Play / Pause / Stop** — runs the project in-editor. No new window — scene panel becomes the game view.

## Project DLL hot reload

1. File watcher on `mygame.dll` timestamp.
2. Build tool (or IDE) rebuilds. Editor detects timestamp change.
3. Serialize current world to memory buffer (via reflection).
4. `on_unload(mygame)` → `FreeLibrary(mygame.dll)`.
5. Wait for new file to be fully written (retry with timeout).
6. `LoadLibrary(mygame.dll)` → `on_load(mygame, host_api)`.
7. Re-register components (reflection makes this cheap).
8. Deserialize world into new component set. If schemas incompatible in v1: warn + reset world. v2: field-migrate.
9. Resume.

## Play mode

- Pressing Play snapshots the world (via reflection serialization).
- Game runs in the Scene viewport. Input routed to game instead of editor.
- Stop restores the snapshot. No data loss.
- Pause freezes `dt` but keeps rendering.

## Tool modules (editor extensions)

Editor loads tool modules from a `tools/` folder alongside `modules/`. A tool module can register:

- **Custom inspectors** via `ZUES_SERVICE_EDITOR_INSPECTOR`:
  ```c
  typedef struct IEditorInspector_v1 {
      const char* (*component_type)();
      void (*draw)(const void* component, float* out_rect_h);
  } IEditorInspector_v1;
  ```
  Called per-component when the inspector panel is rendering.
- **Custom tool panels** via `ZUES_SERVICE_EDITOR_PANEL` — appears in the Window menu. Module draws whatever ImGui content.
- **Custom asset importers** via `ZUES_SERVICE_ASSET_IMPORTER` — handles new asset file extensions.
- **Custom viewport gizmos** via `ZUES_SERVICE_VIEWPORT_GIZMO` — draws in the Scene viewport for specific component types.

All tool-module interfaces are defined in `Core/include/Engine/editor_services.h` (shared header).

## Not in v1

- Visual scripting / blueprint-like node editor
- Dedicated UI-only editor mode (regular editor handles UI entities)
- Profiler panel (later)
- Remote debugging
