#pragma once

// Project DLL loader. Owns the LoadLibrary handle for the user's project
// shared library, drives the lifecycle (on_load → on_update per frame →
// on_unload), and FreeLibrary's on shutdown.
//
// Hot reload (4.x.b):
//   - load() copies the source DLL to a "shadow" path next to it and loads
//     THAT. Lets the user rebuild the original mygame.dll while the editor
//     is still running (Windows file locking would otherwise block the
//     rebuild). The shadow is removed on unload().
//   - is_source_dirty() compares the source file's mtime to what we
//     captured at load time. Cheap; safe to poll each frame.
//   - reload() is unload + load against the same source/host/engine.
//     Project on_load re-registers components (idempotent by name) and
//     re-spawns its entities. Selected entity may become dead — UI handles
//     gracefully via is_alive checks.

#include <zues/api.h>
#include <zues/project_api.h>

#include <filesystem>
#include <string>

namespace Engine::host {

class ProjectDllLoader {
public:
    // load() copies `dll_path` to a shadow path, dlopens the shadow, finds
    // zues_project_entry, verifies abi_version, and calls on_load. Returns
    // false (and logs why) on any failure. Idempotent only when not loaded.
    bool load(const std::filesystem::path& dll_path,
              const ZuesHostApi* host,
              ZuesEngine* engine);

    // Per-frame project on_update. No-op if no project is loaded.
    void tick(float dt);

    // on_unload + FreeLibrary + remove the shadow. Safe when nothing loaded.
    void unload();

    // Reload from the cached source path with the cached host + engine.
    // Equivalent to unload() then load(). Returns true on success.
    bool reload();

    // Cheap mtime check on the source file. true if the file has changed
    // (rebuilt, replaced) since the last successful load/reload.
    bool is_source_dirty() const;

    bool is_loaded() const { return m_api != nullptr; }
    const std::string& source_path() const { return m_source_path; }

    // Last-load diagnostics. Set by every load() / reload() attempt
    // whether it succeeded or failed; lets the editor distinguish
    // "ABI mismatch -- offer rebuild prompt" from "missing file" or
    // "entry symbol not found." Both are 0 when no load has been
    // attempted yet.
    uint32_t last_observed_abi() const { return m_last_observed_abi; }
    uint32_t last_expected_abi() const { return m_last_expected_abi; }
    bool     last_failure_was_abi_mismatch() const {
        return m_last_observed_abi != 0 &&
               m_last_observed_abi != m_last_expected_abi;
    }

    // Read-only accessor for the project's exported callbacks. Returns null
    // when nothing is loaded. The editor's main loop uses this to wire
    // collision callbacks into the physics service after Play starts.
    const ZuesProjectApi* api() const { return m_api; }
    ZuesEngine*           engine() const { return m_engine; }

    ~ProjectDllLoader() { unload(); }

private:
    void*                            m_handle       = nullptr;   // HMODULE / void*
    const ZuesProjectApi*            m_api          = nullptr;
    const ZuesHostApi*               m_host         = nullptr;
    ZuesEngine*                      m_engine       = nullptr;
    std::string                      m_source_path;              // user-visible DLL
    std::string                      m_shadow_path;              // what we actually loaded
    std::filesystem::file_time_type  m_load_mtime{};             // mtime at load time

    // Diagnostics from the most recent load attempt. 0 means "no
    // load attempted yet"; observed != expected when the DLL was
    // built against a different engine API version.
    uint32_t                         m_last_observed_abi = 0;
    uint32_t                         m_last_expected_abi = 0;
};

}  // namespace Engine::host
