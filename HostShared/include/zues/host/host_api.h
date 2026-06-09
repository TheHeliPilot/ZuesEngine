#pragma once

// Host API construction. The runtime (or editor) builds a ZuesHostApi struct
// of function pointers that the project DLL calls back through. The
// ZuesEngine handle is opaque -- one engine per process for now.
//
// Lives in HostShared so both `editor.exe` and `runtime.exe` link the same
// implementation. Editor stacks UI state (Inspector selection, undo, panel
// visibility, etc.) on top via its own EditorState; the runtime has none of
// that and just constructs a HostContext + calls these.

#include <zues/api.h>
#include <zues/ecs/world.h>
#include <zues/host/host_context.h>
#include <zues/project_api.h>

namespace Engine::host {

ZuesHostApi build_host_api();
ZuesEngine* engine_handle();

// Walk back project-registered systems and remove them from the world.
// Called by ProjectDllLoader::unload BEFORE FreeLibrary so we don't leave
// dangling fn pointers in the world's system table.
void unregister_project_systems();

// Tick all live timers by `dt`. Call once per frame between PreUpdate and
// Update -- timers fire in PreUpdate-relative ordering so a timer scheduled
// from a system in tick N first sees the world at tick N+1's PreUpdate.
void tick_timers(float dt);

// Re-run ensure_singleton for every id the project DLL marked as a
// singleton during on_load. Call after world.load_json / world.clear so
// post-load state matches the pre-clear singleton entities (otherwise the
// editor's Hierarchy "Globals" section is empty until first Singleton<T>()
// use lazily adopts).
void resync_singletons();

// Cancel every live timer and drop the singleton-id cache. Call from
// ProjectDllLoader::unload BEFORE FreeLibrary -- timer callbacks are
// function pointers into the project DLL, and firing one after unload
// is a use-after-free that closes the host without a peep.
void clear_timers();

}  // namespace Engine::host
