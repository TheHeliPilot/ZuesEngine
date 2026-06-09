#pragma once

// Default sprite-render system installed by the editor.
//
// On register, this:
//   - Caches the Transform2D + Sprite component IDs from the world.
//   - Adds itself to the world in Phase::Render.
//
// Each tick it walks every entity with Transform2D + Sprite, pulls the
// current camera from IRenderCamera_v1, applies the world->screen transform,
// and submits a sprite. Sprites with `texture == 0` rely on the renderer's
// built-in 1×1 white-pixel substitution (so a default Sprite renders as a
// 1×1-cm white quad scaled by Transform2D.scale and tinted by Sprite.tint).
//
// Replaceable: `world.remove_system(handle)` to drop in your own render
// policy without touching the editor binary.

#include <zues/api.h>
#include <zues/ecs/world.h>

// IRenderer_2D_v1 is a pure-C struct at global scope (renderer_2d.h uses
// `extern "C"`), so the forward decl must be unqualified.
struct IRenderer_2D_v1;

namespace Engine::host {

struct SpriteRenderSystem {
    ecs::SystemHandle handle{};

    bool register_into(ecs::World& world, ::IRenderer_2D_v1* renderer);
    void unregister_from(ecs::World& world);
};

}  // namespace Engine::host
