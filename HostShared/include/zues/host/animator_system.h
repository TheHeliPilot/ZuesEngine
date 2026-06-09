#pragma once

// Default Animator system installed by the editor / runtime.
//
// On register, this:
//   - Caches the Animator + Sprite component IDs from the world.
//   - Adds itself to the world in Phase::Update as a Game-domain
//     system (SystemDomain::Game) -- ticks ONLY in Play mode so the
//     scene doesn't animate while you're authoring it.
//
// Each tick it walks every entity with an Animator component, advances
// its `time` field by dt * time_scale, picks the active frame from the
// referenced .zanim asset, and (when the entity also has a Sprite)
// writes Sprite.texture + Sprite.slice_x/y/w/h to mirror the frame.
//
// Replaceable: `world.remove_system(handle)` to drop in your own
// playback policy without touching the editor binary.

#include <zues/api.h>
#include <zues/ecs/world.h>

struct IRenderer_2D_v1;

namespace Engine::host {

struct AnimatorSystem {
    ecs::SystemHandle handle{};

    bool register_into(ecs::World& world, ::IRenderer_2D_v1* renderer);
    void unregister_from(ecs::World& world);
};

}  // namespace Engine::host
