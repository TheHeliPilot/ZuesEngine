// Engine ECS regression tests. Dependency-free (links only zues_core, no
// test framework) so it can gate CI cheaply. Covers the correctness that
// every future refactor and the project hot-reload path ride on:
//   - binary save/load round-trip (entities, archetypes, generations, data)
//   - JSON save/load round-trip
//   - cross-world component-id REMAP (the hot-reload scenario: same type
//     names, different registration order -> different ids)
//   - ABI-mismatch rejection (same name, different size -> AbiMismatch)
//   - builtin Transform2D round-trip
//
// Exit code 0 = all passed; non-zero = at least one failure (CTest gate).

#include <zues/types.h>
#include <zues/ecs/world.h>
#include <zues/ecs/reflection.h>
#include <zues/components/transform.h>

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using Engine::Result;
using Engine::ecs::World;
using Engine::ecs::Entity;
using Engine::ecs::ComponentId;
using Engine::ecs::INVALID_COMPONENT_ID;
using Engine::components::Transform2D;

namespace zt {
struct Position    { float x;  float y;          };
struct Velocity    { float vx; float vy;         };
struct Health      { int   hp;                   };
// Same registered name as Position but a different size -> drives the
// AbiMismatch path.
struct PositionBig { float x;  float y; float z; };
}  // namespace zt

ZUES_COMPONENT_FIELDS(zt::Position, x, y);
ZUES_COMPONENT_FIELDS(zt::Velocity, vx, vy);
ZUES_COMPONENT_FIELDS(zt::Health, hp);
ZUES_COMPONENT_FIELDS(zt::PositionBig, x, y, z);

static int g_checks = 0;
static int g_fail   = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_fail;                                                      \
            std::printf("  FAIL: %s  [%s:%d]\n", (msg), __FILE__, __LINE__);\
        }                                                                  \
    } while (0)

static bool feq(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// ---------------------------------------------------------------------------

static void test_binary_roundtrip() {
    std::printf("[binary round-trip]\n");
    World w;
    const ComponentId pid = w.register_component<zt::Position>("RT.Position");
    const ComponentId vid = w.register_component<zt::Velocity>("RT.Velocity");
    const ComponentId hid = w.register_component<zt::Health>("RT.Health");

    Entity e0 = w.create_entity();   // Position + Velocity
    zt::Position p0{1.5f, 2.5f};   w.add_component(e0, pid, &p0);
    zt::Velocity v0{0.25f, -0.5f}; w.add_component(e0, vid, &v0);

    Entity e1 = w.create_entity();   // Position only
    zt::Position p1{10.0f, 20.0f}; w.add_component(e1, pid, &p1);

    Entity e2 = w.create_entity();   // Health only
    zt::Health h2{42}; w.add_component(e2, hid, &h2);

    // Recycle a slot so generations aren't all trivially 1.
    Entity tmp = w.create_entity();
    w.destroy_entity(tmp);

    const auto count = w.entity_count();
    auto bytes = w.save_binary();
    CHECK(!bytes.empty(), "save_binary produced bytes");

    w.clear();
    CHECK(w.entity_count() == 0, "clear() emptied the world");

    const Result r = w.load_binary(bytes.data(), bytes.size());
    CHECK(r == Result::Ok, "load_binary returned Ok");
    CHECK(w.entity_count() == count, "entity_count restored");

    CHECK(w.is_alive(e0) && w.is_alive(e1) && w.is_alive(e2),
          "captured handles still resolve after reload");

    auto* gp0 = static_cast<zt::Position*>(w.get_component(e0, pid));
    CHECK(gp0 && feq(gp0->x, 1.5f) && feq(gp0->y, 2.5f), "e0 Position restored");
    auto* gv0 = static_cast<zt::Velocity*>(w.get_component(e0, vid));
    CHECK(gv0 && feq(gv0->vx, 0.25f) && feq(gv0->vy, -0.5f), "e0 Velocity restored");

    CHECK(w.has_component(e1, pid) && !w.has_component(e1, vid),
          "e1 archetype preserved (Position, no Velocity)");

    auto* gh2 = static_cast<zt::Health*>(w.get_component(e2, hid));
    CHECK(gh2 && gh2->hp == 42, "e2 Health restored");
    CHECK(!w.has_component(e2, pid), "e2 has no Position");
}

static void test_json_roundtrip() {
    std::printf("[JSON round-trip]\n");
    std::string json;
    Entity e0, e1;
    {
        World a;
        const ComponentId pid = a.register_component<zt::Position>("JS.Position");
        const ComponentId hid = a.register_component<zt::Health>("JS.Health");
        e0 = a.create_entity(); zt::Position p{3.5f, 4.5f}; a.add_component(e0, pid, &p);
        e1 = a.create_entity(); zt::Health h{7};            a.add_component(e1, hid, &h);
        json = a.save_json();
        CHECK(!json.empty(), "save_json produced text");
    }
    World b;
    const ComponentId pid = b.register_component<zt::Position>("JS.Position");
    const ComponentId hid = b.register_component<zt::Health>("JS.Health");
    const Result r = b.load_json(json.c_str(), json.size());
    CHECK(r == Result::Ok, "load_json returned Ok");

    auto* gp = static_cast<zt::Position*>(b.get_component(e0, pid));
    CHECK(gp && feq(gp->x, 3.5f) && feq(gp->y, 4.5f), "JSON Position restored");
    auto* gh = static_cast<zt::Health*>(b.get_component(e1, hid));
    CHECK(gh && gh->hp == 7, "JSON Health restored");
}

static void test_abi_remap() {
    std::printf("[cross-world component-id remap]\n");
    std::vector<Engine::u8> bytes;
    Entity ea;
    {
        World a;   // registration order P, V
        const ComponentId pid = a.register_component<zt::Position>("RM.Position");
        const ComponentId vid = a.register_component<zt::Velocity>("RM.Velocity");
        ea = a.create_entity();
        zt::Position p{7.0f, 8.0f};  a.add_component(ea, pid, &p);
        zt::Velocity v{9.0f, 10.0f}; a.add_component(ea, vid, &v);
        bytes = a.save_binary();
    }
    // Fresh world, DIFFERENT registration order (+ an extra type) so the
    // ids assigned to Position/Velocity differ from the saved ids. Exercises
    // the saved-id -> loading-world-id remap that hot-reload depends on.
    World b;
    b.register_component<zt::Health>("RM.Health");
    const ComponentId vid2 = b.register_component<zt::Velocity>("RM.Velocity");
    const ComponentId pid2 = b.register_component<zt::Position>("RM.Position");
    CHECK(pid2 != vid2, "distinct ids in loading world");

    const Result r = b.load_binary(bytes.data(), bytes.size());
    CHECK(r == Result::Ok, "load into reordered-id world Ok");

    auto* gp = static_cast<zt::Position*>(b.get_component(ea, pid2));
    CHECK(gp && feq(gp->x, 7.0f) && feq(gp->y, 8.0f), "Position remapped + restored");
    auto* gv = static_cast<zt::Velocity*>(b.get_component(ea, vid2));
    CHECK(gv && feq(gv->vx, 9.0f) && feq(gv->vy, 10.0f), "Velocity remapped + restored");
}

static void test_abi_mismatch() {
    std::printf("[ABI mismatch rejection]\n");
    std::vector<Engine::u8> bytes;
    {
        World a;
        const ComponentId pid = a.register_component<zt::Position>("MM.Position");  // 8 bytes
        Entity e = a.create_entity();
        zt::Position p{1.0f, 2.0f}; a.add_component(e, pid, &p);
        bytes = a.save_binary();
    }
    World b;
    // Same name, different layout (12 bytes) -> must be rejected, not
    // silently memcpy'd into a smaller/larger slot.
    b.register_component<zt::PositionBig>("MM.Position");
    const Result r = b.load_binary(bytes.data(), bytes.size());
    CHECK(r == Result::AbiMismatch, "size mismatch rejected with AbiMismatch");
}

static void test_builtins_roundtrip() {
    std::printf("[builtin Transform2D round-trip]\n");
    World w;
    w.register_builtins();
    const ComponentId tid = w.component_id<Transform2D>();
    CHECK(tid != INVALID_COMPONENT_ID, "Transform2D registered by register_builtins");

    Entity e = w.create_entity();   // auto-attaches a default Transform2D
    Transform2D t;
    t.position = {3.0f, 4.0f};
    t.rotation = 1.25f;
    t.scale    = {2.0f, 0.5f};
    w.add_component(e, tid, &t);    // overwrite the auto-attached one

    auto bytes = w.save_binary();
    w.clear();
    const Result r = w.load_binary(bytes.data(), bytes.size());
    CHECK(r == Result::Ok, "load builtins world Ok");

    auto* gt = static_cast<Transform2D*>(w.get_component(e, tid));
    CHECK(gt &&
          feq(gt->position.x, 3.0f) && feq(gt->position.y, 4.0f) &&
          feq(gt->rotation, 1.25f) &&
          feq(gt->scale.x, 2.0f) && feq(gt->scale.y, 0.5f),
          "Transform2D fields restored");
}

int main() {
    std::printf("=== Zues ECS regression tests ===\n");
    test_binary_roundtrip();
    test_json_roundtrip();
    test_abi_remap();
    test_abi_mismatch();
    test_builtins_roundtrip();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
