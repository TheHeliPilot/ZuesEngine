#pragma once

// Private header. Lives under src/ (not include/Engine/) because only Core
// itself uses it. Module loader is an implementation detail of engine.cpp.

#include <zues/module.h>

#include <string>
#include <vector>

namespace Engine::internal {

struct LoadedModule {
    std::string       name;
    void*             library_handle;   // HMODULE on Win, void* on *nix
    const ModuleInfo* info;
};

class ModuleRegistry {
public:
    ~ModuleRegistry();

    // Scans `dir` for zues_*.dll (excluding zues_core.dll). For each found
    // module: LoadLibrary, resolve zues_module_entry, check ABI, call on_load.
    // Errors are logged; failed modules are skipped, not fatal.
    void load_all(const char* dir, ModuleContext* ctx);

    // Call on_ready for all loaded modules (in load order).
    void signal_ready(ModuleContext* ctx);

    // Call on_unload in reverse load order, then FreeLibrary each.
    void unload_all(ModuleContext* ctx);

    std::vector<LoadedModule> modules;
};

}  // namespace Engine::internal
