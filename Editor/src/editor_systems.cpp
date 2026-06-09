#include "editor_systems.h"
#include "editor.h"

#include <zues/components/name.h>
#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/log.h>

#include <cstdio>

namespace Engine::editor {

// NOTE: camera publishing (EditorState::camera -> IRenderCamera_v1) used to
// be a Phase::Input system but was moved into panel_scene because viewport
// dimensions only become known mid-imgui-frame. Per-viewport publishing is
// the right granularity — Scene and (future) Game panels each push their
// own camera into the service before driving their Render phase.

void register_editor_systems(ecs::World& /*world*/, EditorState& /*state*/) {
    // No systems installed yet. Hook for future editor-side default systems
    // (selection-outline render, gizmos, autosave, etc.). Kept as a no-op
    // call site so main.cpp doesn't need to flip when the first one lands.
}

void spawn_editor_demo_sprites(ecs::World& world) {
    using namespace Engine::components;
    using namespace Engine::ecs;

    // Make sure builtins are registered. Idempotent.
    world.register_builtins();

    const auto xform_id  = world.find_component_id("Transform2D");
    const auto sprite_id = world.find_component_id("Sprite");
    const auto name_id   = world.find_component_id("Name");
    if (!xform_id || !sprite_id) {
        ZUES_LOG_WARN("spawn_editor_demo_sprites: builtins not registered");
        return;
    }

    // Three squares around world origin (cm). Default white sprite (no
    // texture handle), each tinted differently so we can confirm tinting.
    struct Spec { const char* name; float x, y; float r, g, b; float scale; };
    constexpr Spec specs[] = {
        {"DemoSprite Red",   -1.5f, 0.0f,  1.0f, 0.3f, 0.3f, 1.0f},
        {"DemoSprite Green",  0.0f, 0.0f,  0.3f, 1.0f, 0.3f, 1.5f},
        {"DemoSprite Blue",   1.5f, 0.0f,  0.3f, 0.5f, 1.0f, 0.8f},
    };

    for (const auto& s : specs) {
        const auto e = world.create_entity();

        Name n{};
        std::snprintf(n.value, sizeof(n.value), "%s", s.name);
        if (name_id) world.add_component(e, name_id, &n);

        Transform2D t{};
        t.position = {s.x, s.y};
        t.scale    = {s.scale, s.scale};
        world.add_component(e, xform_id, &t);

        Sprite sp{};
        sp.size = {1.0f, 1.0f};         // 1×1 cm — default white square
        sp.tint = Engine::math::color{s.r, s.g, s.b, 1.0f};
        world.add_component(e, sprite_id, &sp);
    }
    ZUES_LOG_INFO("spawn_editor_demo_sprites: 3 demo sprites at world origin");

    // Default Game camera so the Game panel has something to render before
    // the user (or project) sets up their own. Placed at origin so it frames
    // the demo sprites. is_active=true means the Game panel picks this one.
    const auto cam_id = world.find_component_id("Camera2D");
    if (cam_id) {
        const auto cam_e = world.create_entity();
        Name cn{};
        std::snprintf(cn.value, sizeof(cn.value), "Main Camera");
        if (name_id) world.add_component(cam_e, name_id, &cn);
        Transform2D ct{};
        ct.position = {0.0f, 0.0f};
        world.add_component(cam_e, xform_id, &ct);
        Camera2D c{};
        c.ortho_size = 10.0f;         // 10 units vertical visible
        c.is_active  = true;
        world.add_component(cam_e, cam_id, &c);
        ZUES_LOG_INFO("spawn_editor_demo_sprites: + Main Camera at world origin");
    }
}

}  // namespace Engine::editor
