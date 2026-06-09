#pragma once

// HostContext — the small handful of fields the engine-runtime side of the
// host_api needs from its embedder. Both the editor and the standalone
// runtime exe construct a HostContext at startup and hand it to host_api.
// The editor stores additional UI state (Inspector selection, undo stacks,
// toast list, etc.) outside this struct -- those don't belong here because
// the runtime doesn't have any of them.
//
// Replaces the older `EditorState* g_state` global the host_api used to
// reach for. The pointer is owned by the embedder; host_api just borrows
// it for the lifetime of the application.

#include <zues/api.h>
#include <zues/ecs/world.h>

#include <string>

namespace Engine::host {

struct HostContext {
    // Live world the host fns (create_entity, register_component, etc.)
    // operate on. nullptr makes every world-touching host fn a safe no-op
    // (returns 0 / null).
    Engine::ecs::World* world = nullptr;

    // Filesystem root of the loaded project. Used for resolving project-
    // relative prefab paths inside Instantiate(...). Empty when no project
    // has been opened yet (host_api gracefully degrades).
    std::string project_dir;

    // True once project_loader.load() has succeeded for this context. Lets
    // path-resolving host fns (instantiate_prefab*) tell "no project yet"
    // from "project loaded but project_dir empty (impossible)".
    bool project_loaded = false;
};

// Wire the context. host_api borrows the pointer until set_host_context(nullptr)
// is called. Pass nullptr at shutdown so any post-shutdown host call no-ops
// instead of dereferencing freed editor/runtime state.
void set_host_context(HostContext* ctx);
HostContext* get_host_context();

}  // namespace Engine::host
