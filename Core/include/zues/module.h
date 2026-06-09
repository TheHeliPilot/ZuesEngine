#pragma once
#include <zues/api.h>
#include <zues/types.h>

namespace Engine {

class ServiceRegistry;
class EventBus;

// Passed to every module callback. Opaque pointers to Core-owned state.
struct ModuleContext {
    ServiceRegistry* services = nullptr;
    EventBus*        events   = nullptr;
};

// Each module.dll fills one of these in and returns a pointer from
// zues_module_entry(). See docs/05-module-system.md.
struct ModuleInfo {
    const char* name;          // "zues_renderer_gl"
    const char* version;       // "0.1.0"
    u32         abi_version;   // must equal ZUES_MODULE_ABI_VERSION

    void (*on_load)  (ModuleContext*) = nullptr;
    void (*on_ready) (ModuleContext*) = nullptr;
    void (*on_update)(ModuleContext*, f32 dt) = nullptr;
    void (*on_unload)(ModuleContext*) = nullptr;
};

constexpr u32 ZUES_MODULE_ABI_VERSION = 1;

// Function-pointer type for the module entry point.
using ZuesModuleEntryFn = const ModuleInfo* (*)();

}  // namespace Engine

// Module writes:
//   ZUES_MODULE_EXPORT const Engine::ModuleInfo* zues_module_entry() { ... }
