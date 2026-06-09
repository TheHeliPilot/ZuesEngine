# Module System

The engine is a set of DLLs. Each DLL is a module. Modules expose **services** (typed interfaces) and subscribe to **events** (broadcasts).

## Module lifecycle

1. Core's loader scans the modules folder at startup.
2. For each `zues_*.dll`: `LoadLibrary` / `dlopen` → resolve `zues_module_entry` → call it.
3. `zues_module_entry()` returns a `const ModuleInfo*`. Loader checks `abi_version == ZUES_MODULE_ABI_VERSION`. Mismatch = refuse + log error.
4. `on_load(ctx)` — module registers its services with `ctx->services`.
5. After all modules' `on_load` completes: `on_ready(ctx)` — safe to consume services from other modules.
6. Per frame: `on_update(ctx, dt)` — optional.
7. Shutdown: `on_unload(ctx)`.

## ModuleInfo (from `Engine/module.h`)

```cpp
struct ModuleInfo {
    const char* name;          // "renderer_gl"
    const char* version;       // "0.1.0"
    uint32_t    abi_version;   // must equal ZUES_MODULE_ABI_VERSION

    void (*on_load)  (ModuleContext*);
    void (*on_ready) (ModuleContext*);
    void (*on_update)(ModuleContext*, float dt);
    void (*on_unload)(ModuleContext*);
};
```

Module writes exactly one exported function:

```cpp
ZUES_MODULE_EXPORT const Engine::ModuleInfo* zues_module_entry() { return &INFO; }
```

All other symbols stay internal.

## Services

A service is a named interface: string ID + version number + pointer to a vtable.

- Vtable is a C struct of function pointers (safest).
- Registered by the providing module in its `on_load`.
- Looked up by any module via `registry->get_service(id, version)`.

Example service definition:

```c
// Engine/services/renderer_2d.h  (shared header, pure C)
typedef struct IRenderer_2D_v1 {
    uint32_t abi_version;
    void (*begin_frame)(struct IRenderer_2D_v1*);
    void (*draw_sprite)(struct IRenderer_2D_v1*, TextureHandle, float x, float y);
    void (*end_frame)(struct IRenderer_2D_v1*);
} IRenderer_2D_v1;

#define ZUES_SERVICE_RENDERER_2D "Engine.renderer.2d"
#define ZUES_SERVICE_RENDERER_2D_VERSION 1
```

Typed lookup:
```cpp
auto* r = services->get<IRenderer_2D_v1>();  // wraps get_service + string IDs
```

## Events

Broadcast-only. Synchronous dispatch in registration order. Flushed once per frame.

```cpp
events->subscribe<WindowResized>([](const WindowResized& e) { /* ... */ });
events->publish(WindowResized{1920, 1080});
```

**Rules:**
- Events are POD (or POD-with-`const char*` — no `std::string`).
- Events cross module boundaries — the POD rule matters.
- Use for: window events, file-changed, hot-reload, input events.
- NEVER use for: per-frame per-entity updates (performance).

## Swapping a module

Two DLLs register the same service ID → second one wins (with a warning). To force a specific implementation:
- Delete the unwanted DLL from the modules folder, OR
- Use a future `modules.json` to enable/disable by name.

## Tool modules (editor extensions)

Editor loads additional "tool modules" that can register:
- Custom inspector widgets for specific component types (`ZUES_SERVICE_EDITOR_INSPECTOR`)
- Custom tool panels (`ZUES_SERVICE_EDITOR_PANEL`)
- Custom asset importers (`ZUES_SERVICE_ASSET_IMPORTER`)

Same module system, different service IDs. Covered in [11-editor](11-editor.md).
