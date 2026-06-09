#pragma once

// Editor-registered ECS systems + scene helpers.
//
// All editor systems are registered into the world via these helpers. Each is
// a regular ecs::SystemFn — projects (or future editor extensions) can
// `world.remove_system(handle)` and drop in a replacement without touching
// the editor binary.

#include <zues/ecs/world.h>

namespace Engine::editor {

struct EditorState;

// Registers the editor's standard system set into `world`. Currently:
//   - "editor.camera_publish" (Phase::Input) — copies EditorState::camera into
//     the IRenderCamera_v1 service every frame.
// Returns nothing; the handles are kept editor-side so we can unregister on
// shutdown if needed (not yet wired).
void register_editor_systems(ecs::World& world, EditorState& state);

// Spawns a few visible demo entities (Transform2D + Sprite + Name) so the
// Scene viewport has something to render even when no project DLL adds its
// own sprites. Pure debug scaffolding — safe to remove once projects can
// declare sprites themselves.
void spawn_editor_demo_sprites(ecs::World& world);

}  // namespace Engine::editor
