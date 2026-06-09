// Prefab v1 — file-per-prefab .zprefab format. A prefab captures one entity
// plus every descendant via the hierarchy, written as JSON with a stable
// top-level "guid" so cross-asset references survive moves and renames.
//
// Save:   "Save Selected as Prefab" in the hierarchy / inspector — writes
//         <project>/assets/prefabs/<Name>.zprefab and registers the new
//         AssetEntry so the scene can drop-instantiate it immediately.
//
// Load:   drag the .zprefab from the asset browser onto the scene viewport.
//         The world creates fresh entities (new ids/generations), rewires
//         intra-subtree refs, and parents the root to the world (or to the
//         entity dropped on, in v2).
//
// v1 is "instantiate-and-forget": no override tracking, no live-link from
// edits in the prefab back to spawned instances. v2 adds per-instance
// overrides + an upstream guid we can refresh against. v3 nested prefabs.

#include "editor.h"

#include <zues/asset.h>
#include <zues/components/hierarchy.h>
#include <zues/components/name.h>
#include <zues/components/transform.h>
#include <zues/ecs/world.h>
#include <zues/guid.h>
#include <zues/log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// Forward decl from HostShared/src/prefab_runtime.cpp. Declared at global
// scope (outside Engine::editor) so the qualified names resolve against the
// real ::Engine, not a nested Engine::editor::Engine.
namespace Engine::host {
    Engine::ecs::Entity prefab_instantiate_runtime(
        Engine::ecs::World& world, const std::string& abs_path,
        Engine::math::vec2 world_pos);
}

namespace Engine::editor {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

// ASCII-sanitise a name for use as a filename. Spaces -> underscores, drop
// anything that isn't [A-Za-z0-9_-]. Empty result falls back to "Prefab".
std::string sanitize_for_filename(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == ' ') c = '_';
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            out += c;
        }
    }
    if (out.empty()) out = "Prefab";
    return out;
}

std::string entity_display_name(const ecs::World& w, ecs::Entity e) {
    const ecs::ComponentId name_id = w.find_component_id("Name");
    if (name_id == ecs::INVALID_COMPONENT_ID) return "Entity";
    auto* n = static_cast<const components::Name*>(w.get_component(e, name_id));
    if (!n || !n->value[0]) return "Entity";
    return n->value;
}

// Wrap the World's subtree snapshot with the .zprefab top-level shape.
//   { "guid": "...", "version": 1, "kind": "prefab", "snapshot": <subtree> }
// Embedding the World JSON as a parsed object (rather than a string) lets
// future tooling diff it natively.
std::string build_zprefab_body(Guid g, const std::string& subtree) {
    json doc;
    doc["guid"]    = guid_to_hex(g);
    doc["version"] = 1;
    doc["kind"]    = "prefab";
    try {
        doc["snapshot"] = json::parse(subtree);
    } catch (const std::exception&) {
        // Subtree must always be valid JSON — if the World produced
        // something we can't parse, that's a Core bug and we'd rather
        // surface it than write a corrupt file.
        log_write(LogLevel::Error, "prefab",
                  "subtree snapshot did not parse as JSON");
        return {};
    }
    return doc.dump(2);
}

}  // namespace

bool prefab_save_selected(EditorState& s) {
    if (!s.world)                     return false;
    if (!s.project_loaded)            return false;
    if (s.selected_entity.is_null())  return false;
    if (!s.world->is_alive(s.selected_entity)) return false;

    const std::string subtree = s.world->save_entity_subtree_json(s.selected_entity);
    if (subtree.empty() || subtree == "{}") {
        show_toast(s, "Prefab save: empty subtree", 2.5f, true);
        return false;
    }

    const Guid g = guid_new();
    const std::string body = build_zprefab_body(g, subtree);
    if (body.empty()) {
        show_toast(s, "Prefab save: snapshot parse failed", 2.5f, true);
        return false;
    }

    // Pick a destination path. assets/prefabs/<Name>.zprefab; dedupe by
    // appending _1, _2, ... so we never silently overwrite.
    fs::path prefabs_dir = fs::path(s.project_dir) /
                            s.assets_root_relative /
                            "prefabs";
    std::error_code ec;
    fs::create_directories(prefabs_dir, ec);

    std::string base = sanitize_for_filename(
        entity_display_name(*s.world, s.selected_entity));
    fs::path dest = prefabs_dir / (base + ".zprefab");
    int n = 1;
    while (fs::exists(dest, ec)) {
        dest = prefabs_dir / (base + "_" + std::to_string(n++) + ".zprefab");
    }

    {
        std::ofstream out(dest);
        if (!out) {
            show_toast(s, "Prefab save: open file failed", 2.5f, true);
            return false;
        }
        out << body;
    }

    // Index the new asset so subsequent drag-instantiate works without a
    // full rescan. Path stored project-relative + forward slashes.
    fs::path rel = fs::relative(dest, fs::path(s.project_dir), ec);
    AssetEntry e;
    e.guid = g;
    e.kind = AssetKind::Prefab;
    e.path = path_str(rel);
    AssetRegistry::instance().register_asset(e);

    show_toast(s, ("Saved prefab: " + dest.filename().string()).c_str(),
               2.5f, false);
    log_write(LogLevel::Info, "prefab",
              ("saved: " + path_str(dest)).c_str());
    return true;
}

bool prefab_overwrite_from_entity(EditorState& s,
                                   const std::string& abs_path,
                                   ecs::Entity src) {
    if (!s.world) return false;
    if (src.is_null() || !s.world->is_alive(src)) {
        show_toast(s, "Prefab overwrite: source entity not alive", 2.5f, true);
        return false;
    }

    // Pull the EXISTING guid out of the .zprefab so we can preserve it.
    // PrefabRef fields elsewhere in the project (and in saved worlds) refer
    // to the prefab by guid -- minting a new one would break every
    // reference. If the file is unreadable or has no guid we bail rather
    // than silently writing one with a fresh guid.
    Guid existing_guid{};
    {
        std::ifstream in(abs_path);
        if (!in) {
            show_toast(s, "Prefab overwrite: open failed", 2.5f, true);
            return false;
        }
        try {
            json doc = json::parse(in);
            if (!doc.contains("guid") || !doc["guid"].is_string()) {
                show_toast(s, "Prefab overwrite: target has no guid", 2.5f, true);
                return false;
            }
            existing_guid = guid_from_hex(doc["guid"].get<std::string>());
        } catch (const std::exception&) {
            show_toast(s, "Prefab overwrite: parse failed", 2.5f, true);
            return false;
        }
    }
    if (existing_guid.is_null()) {
        show_toast(s, "Prefab overwrite: target guid is null", 2.5f, true);
        return false;
    }

    // Rebuild the .zprefab body around the dragged entity's subtree, keeping
    // the original guid bytes. Same shape as prefab_save_selected.
    const std::string subtree = s.world->save_entity_subtree_json(src);
    if (subtree.empty() || subtree == "{}") {
        show_toast(s, "Prefab overwrite: empty subtree", 2.5f, true);
        return false;
    }
    const std::string body = build_zprefab_body(existing_guid, subtree);
    if (body.empty()) {
        show_toast(s, "Prefab overwrite: snapshot parse failed", 2.5f, true);
        return false;
    }

    // Atomic-ish write: write to a temp file in the same directory, then
    // rename over the original. Avoids leaving a half-written .zprefab
    // if the editor crashes mid-write.
    fs::path target(abs_path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) {
            show_toast(s, "Prefab overwrite: tmp open failed", 2.5f, true);
            return false;
        }
        out << body;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        // Fallback: copy + delete in case rename across volumes fails.
        fs::copy_file(tmp, target,
            fs::copy_options::overwrite_existing, ec);
        std::error_code rmec;
        fs::remove(tmp, rmec);
        if (ec) {
            show_toast(s, "Prefab overwrite: rename failed", 2.5f, true);
            return false;
        }
    }

    // The registry entry already maps guid <-> path; nothing else to update.
    show_toast(s, ("Overwrote prefab: " + target.filename().string()).c_str(),
               2.5f, false);
    log_write(LogLevel::Info, "prefab",
              ("overwrote (guid kept): " + path_str(target)).c_str());
    return true;
}

// The runtime version (forward-declared at file top, outside this namespace)
// is pure: read JSON -> instantiate -> place. This editor wrapper layers
// undo recording + selection-changes on top, neither of which exists in
// the standalone runtime.
ecs::Entity prefab_instantiate_from_file(EditorState& s,
                                          const std::string& abs_path,
                                          math::vec2 world_pos)
{
    if (!s.world) return ecs::NULL_ENTITY;

    undo_begin(s);
    const ecs::Entity root = Engine::host::prefab_instantiate_runtime(
        *s.world, abs_path, world_pos);
    if (root.is_null()) {
        undo_cancel(s);
        return ecs::NULL_ENTITY;
    }

    s.world_dirty     = true;
    s.selected_entity = root;
    s.hierarchy_scroll_to_selected = true;   // surface the new entity
    undo_commit(s, "Instantiate prefab");
    return root;
}

}  // namespace Engine::editor
