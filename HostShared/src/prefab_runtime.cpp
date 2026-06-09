// Runtime-side prefab instantiate. Pure: takes a World + an absolute path
// to a .zprefab + the desired world position, returns the spawned root
// entity. No editor concepts (undo, selection change, dirty flag, toast)
// touch this path -- those are the editor's responsibility, layered on
// top in Editor/src/prefab.cpp.

#include <zues/host/host_context.h>

#include <zues/components/transform.h>
#include <zues/ecs/world.h>
#include <zues/log.h>
#include <zues/math/vec2.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

namespace Engine::host {

using json = nlohmann::json;

Engine::ecs::Entity prefab_instantiate_runtime(
    Engine::ecs::World& world, const std::string& abs_path,
    Engine::math::vec2 world_pos)
{
    // Pull the subtree out of the .zprefab JSON.
    std::string body;
    {
        std::ifstream in(abs_path);
        if (!in) return Engine::ecs::NULL_ENTITY;
        std::string line;
        while (std::getline(in, line)) { body += line; body += '\n'; }
    }
    if (body.empty()) return Engine::ecs::NULL_ENTITY;

    std::string subtree_str;
    try {
        json doc = json::parse(body);
        if (!doc.contains("snapshot")) {
            Engine::log_write(Engine::LogLevel::Warn, "prefab",
                              "no 'snapshot' field");
            return Engine::ecs::NULL_ENTITY;
        }
        subtree_str = doc["snapshot"].dump();
    } catch (const std::exception&) {
        Engine::log_write(Engine::LogLevel::Warn, "prefab",
                          "JSON parse failed");
        return Engine::ecs::NULL_ENTITY;
    }

    const Engine::ecs::Entity root = world.instantiate_entity_subtree_json(
        subtree_str.data(), subtree_str.size());
    if (root.is_null()) return Engine::ecs::NULL_ENTITY;

    // Place the root at the drop point. If the prefab has a Transform2D
    // we overwrite its position; nested children keep their relative
    // offsets and compose through the renderer's parent-walk.
    const Engine::ecs::ComponentId xf_id = world.find_component_id("Transform2D");
    if (xf_id != Engine::ecs::INVALID_COMPONENT_ID) {
        auto* t = static_cast<Engine::components::Transform2D*>(
            world.get_component(root, xf_id));
        if (t) { t->position = world_pos; }
    }
    return root;
}

}  // namespace Engine::host
