#pragma once
#include <zues/api.h>
#include <zues/types.h>
#include <zues/ecs/entity.h>
#include <zues/ecs/component_type.h>

#include <cstddef>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Engine::ecs {

class Archetype;   // private

template <typename... Ts>
class Query;

// Fixed system execution phases. See docs/06-ecs.md.
enum class Phase : u32 {
    Input        = 0,
    PreUpdate    = 1,
    Physics      = 2,
    PostUpdate   = 3,
    NetReplicate = 4,
    UiInput      = 5,
    UiLayout     = 6,
    Render       = 7,
    UiRender     = 8,
};
constexpr u32 PHASE_COUNT = 9;

// System callback signature. Function pointer (no std::function across the
// DLL boundary) plus an opaque user pointer for module-side state.
class World;
using SystemFn = void (*)(World& world, float dt, void* user);

// When does a system run? `Both` is the default — it ticks in both edit and
// play modes. `Editor` only runs while the editor is in edit mode (paused
// during Play). `Game` only runs while a Play session is active. Tick mode
// is set on the World via `set_tick_mode`.
enum class SystemDomain : u8 {
    Both   = 0,
    Editor = 1,
    Game   = 2,
};

enum class TickMode : u8 {
    Edit = 0,
    Play = 1,
};

struct SystemHandle {
    Phase phase = Phase::Input;
    u32   id    = 0;          // 0 = invalid

    constexpr bool is_valid() const { return id != 0; }
};

// Read-only system descriptor. Used by the editor's Systems panel to
// enumerate registered systems without exposing internal storage.
struct SystemInfo {
    SystemHandle handle;
    const char*  name;
    Phase        phase;
    SystemDomain domain;
    bool         enabled;
};

// The ECS container. One per game, typically owned by the editor or the
// project. Storage is archetype-based: entities with the same set of
// components live in the same archetype, and each archetype holds packed
// SoA arrays of component data (one array per component type).
class ZUES_API World {
public:
    World();
    ~World();
    World(const World&)            = delete;
    World& operator=(const World&) = delete;

    // ---- Component type registration ---------------------------------------

    // Register a component type by descriptor. Re-registering a name returns
    // the existing id (idempotent).
    ComponentId register_component_type(ComponentType desc);

    // Convenience: `world.register_component<Position>("Position")`.
    // Also indexes the type so `world.component_id<T>()` works.
    template <typename T>
    ComponentId register_component(const char* name) {
        const ComponentId id = register_component_type(make_component_type<T>(name));
        register_typeid(typeid(T).name(), id);
        return id;
    }

    // Lookup by name. Returns INVALID_COMPONENT_ID if not registered.
    ComponentId find_component_id(const char* name) const;

    // Lookup by C++ type. Uses `typeid(T).name()` as the key — stable per
    // process. Available after the type was passed through register_component<T>.
    template <typename T>
    ComponentId component_id() const {
        return find_component_id_by_typeid(typeid(T).name());
    }

    ComponentId find_component_id_by_typeid(const char* typeid_str) const;

    // Override the editor-menu category for an already-registered component.
    // No-op if `id` isn't registered. The category string must outlive the
    // World — pass a string literal or a stable static buffer.
    void set_component_category(ComponentId id, const char* category);

    // Drop every component type registered AFTER engine builtins. Callers
    // (the editor's project hot-reload path) should also clear() the world
    // first - removing types while entities still hold them is undefined.
    void unload_project_types();

    // Read-only access to a registered type's descriptor. Null if id is
    // invalid or unregistered.
    const ComponentType* get_component_type(ComponentId id) const;

    // ---- Entities -----------------------------------------------------------

    Entity create_entity();
    void   destroy_entity(Entity e);
    bool   is_alive(Entity e) const;

    // Resolve a bare slot index to the live (index, generation) pair, or
    // NULL_ENTITY if no entity occupies the slot. Used by the physics
    // event drain so callbacks receive the actual generation instead of
    // a fake "generation = 1" that breaks once slots get recycled.
    Entity live_entity_for_index(u32 index) const;

    // ---- Components (raw API) ----------------------------------------------
    //
    // The raw API is always available and takes ComponentId directly.
    // 2.2 will add a reflection-driven typed API on top.

    // Add component `id` to entity. If `initial_bytes` is non-null, copies
    // `size(id)` bytes from it. If null, zero-initializes. If the entity
    // already has this component, overwrites and returns the pointer.
    void* add_component(Entity e, ComponentId id, const void* initial_bytes);

    // Remove if present; no-op if absent.
    void remove_component(Entity e, ComponentId id);

    // Returns null if the entity doesn't have the component or is dead.
    void*       get_component(Entity e, ComponentId id);
    const void* get_component(Entity e, ComponentId id) const;

    bool has_component(Entity e, ComponentId id) const;

    // ---- Stats -------------------------------------------------------------

    u32 entity_count()    const;
    u32 archetype_count() const;

    // ---- Built-in components -----------------------------------------------

    // Registers all engine-defined component types (Transform2D, hierarchy
    // primitives, Name, Camera2D, Sprite, Text). Caches the IDs needed by
    // the hierarchy helper API. Idempotent — safe to call multiple times.
    void register_builtins();

    // ---- Hierarchy (Unity-style UX, linked-list internals) -----------------
    //
    // Parent / FirstChild / NextSibling components are managed by these
    // helpers. Don't write them yourself — they're visible in the inspector
    // grayed-out for debugging only.

    // ---- Unknown components ----------------------------------------
    //
    // World load preserves component data even when the type isn't
    // registered (project DLL not loaded yet, type renamed, momentarily
    // commented out). The raw JSON `data` value is stashed against the
    // entity and round-tripped on save -- so editing a scene with a
    // stale build doesn't quietly delete fields the user spent time on.
    //
    // The inspector enumerates these via iterate_unknown_components and
    // renders a disabled row per blob with a Remove button (calls
    // remove_unknown_component). When the matching type *does* register
    // (hot-reload), adopt_unknown_components(name) materialises blobs
    // back into real component rows by parsing their saved JSON.
    using UnknownVisitor = void (*)(Entity, const char* type_name,
                                     const char* data_json, void* user);
    void  iterate_unknown_components(Entity e,
                                      UnknownVisitor fn, void* user) const;
    void  remove_unknown_component  (Entity e, const char* type_name);
    // Returns the number of blobs that were materialised this call.
    u32   adopt_unknown_components  (const char* type_name);

    void   set_parent(Entity child, Entity new_parent);
    void   unparent  (Entity child);
    Entity parent_of (Entity entity) const;

    // Compose a 2D world transform by walking the parent chain. Each
    // entity's stored Transform2D is treated as LOCAL (relative to its
    // parent). The composition is the standard 2D TRS:
    //   world_pos    = parent.world_pos + R(parent.world_rot) * (entity.local_pos * parent.world_scale)
    //   world_rot    = parent.world_rot + entity.local_rot
    //   world_scale  = parent.world_scale * entity.local_scale
    // Roots return their own Transform2D unchanged. Entities without a
    // Transform2D return identity (zero pos, zero rot, unit scale). Cheap
    // O(depth) walk -- callers cache if they iterate the same entity many
    // times in one frame.
    struct WorldTransform2D {
        float pos_x, pos_y;
        float rot;
        float scale_x, scale_y;
    };
    WorldTransform2D world_transform_2d(Entity e) const;

    // Sibling reorder. Both move `child` to share `sibling`'s parent (or
    // become a root if sibling is a root) and splice it into the chain
    // immediately before / after `sibling`. No-op if child == sibling or
    // if `sibling` is a descendant of `child` (would create a cycle).
    // Calling these is the only way to control intra-parent order outside
    // of the implicit "newest first" behaviour of set_parent.
    void   move_before(Entity child, Entity sibling);
    void   move_after (Entity child, Entity sibling);

    // Visit every root entity in the explicit ordered sequence. Used by
    // the editor's Hierarchy panel for the top-level pass — slot-order
    // iteration would mean the user can't reorder two top-level entities
    // without reparenting them to a synthetic root.
    using EntityVisitorRaw = void (*)(Entity, void* user);
    void iterate_roots_raw(EntityVisitorRaw fn, void* user) const;

    template <typename Fn>
    void iterate_roots(Fn&& fn) const {
        struct Ctx { Fn& f; };
        Ctx ctx{fn};
        iterate_roots_raw(
            +[](Entity e, void* user) { static_cast<Ctx*>(user)->f(e); },
            &ctx);
    }

    Entity first_child_of (Entity parent) const;
    Entity next_sibling_of(Entity entity) const;

    u32 child_count(Entity parent) const;

    // Direct children only (not recursive). To recurse, call
    // iterate_children inside fn.
    template <typename Fn>
    void iterate_children(Entity parent, Fn&& fn) const {
        for (Entity e = first_child_of(parent); !e.is_null(); e = next_sibling_of(e)) {
            fn(e);
        }
    }

    // ---- Iteration helpers (editor-friendly) -------------------------------

    // Calls fn(Entity) for every alive entity in slot order. Used by the
    // editor's hierarchy panel which needs to scan roots without going
    // through the archetype graph directly.
    using EntityVisitor = void (*)(Entity, void* user);
    void iterate_alive_raw(EntityVisitor fn, void* user) const;

    template <typename Fn>
    void iterate_alive(Fn&& fn) const {
        struct Ctx { Fn& f; };
        Ctx ctx{fn};
        iterate_alive_raw(
            +[](Entity e, void* user) { static_cast<Ctx*>(user)->f(e); },
            &ctx);
    }

    // Calls fn(id, ComponentType) for every registered component type in id
    // order. Used by the inspector to enumerate which components an entity
    // has without hardcoding the list.
    using ComponentTypeVisitor = void (*)(ComponentId, const ComponentType&, void* user);
    void iterate_component_types_raw(ComponentTypeVisitor fn, void* user) const;

    template <typename Fn>
    void iterate_component_types(Fn&& fn) const {
        struct Ctx { Fn& f; };
        Ctx ctx{fn};
        iterate_component_types_raw(
            +[](ComponentId id, const ComponentType& t, void* user) {
                static_cast<Ctx*>(user)->f(id, t);
            },
            &ctx);
    }

    // ---- Queries ------------------------------------------------------------
    //
    //   world.query<Position, Velocity>().each([](Entity e, Position& p, Velocity& v) {
    //       p.x += v.vx; p.y += v.vy;
    //   });
    //
    //   world.query<Position>().without<SomeTag>().each(...);

    template <typename... Ts>
    Query<Ts...> query() { return Query<Ts...>(this); }

    // Generic archetype iteration used by Query<>::each. Filters archetypes
    // by required/excluded component IDs, then for each entity invokes
    // `fn(user, entity, column_ptrs, count)` with a stable pointer array
    // sized to `n_required`.
    using QueryRowFn = void (*)(void* user, Entity e, void** column_ptrs, u32 count);

    void iterate_query(const ComponentId* required, u32 n_required,
                       const ComponentId* excluded, u32 n_excluded,
                       QueryRowFn fn, void* user);

    // ---- Systems + tick ----------------------------------------------------

    // Append a system to the given phase. Systems within a phase run in
    // registration order. `user` is passed back to `fn` each tick.
    // `domain` controls which TickMode this system runs in (default: Both).
    SystemHandle add_system(const char* name, Phase phase,
                            SystemFn fn, void* user = nullptr,
                            SystemDomain domain = SystemDomain::Both);

    bool remove_system(SystemHandle h);

    // Per-system enable toggle. Disabled systems are skipped by tick/
    // tick_phase but stay registered. Returns false if h is invalid.
    bool set_system_enabled(SystemHandle h, bool enabled);

    // Active tick mode. Defaults to Edit. Editor flips to Play when the
    // user hits the Play button (Phase 4.5). Domain filtering uses this.
    void     set_tick_mode(TickMode m);
    TickMode tick_mode() const;

    // Run every phase in order: Input → PreUpdate → Physics → PostUpdate →
    // NetReplicate → UiInput → UiLayout → Render → UiRender.
    // Each system runs only if (enabled AND domain matches tick_mode).
    void tick(float dt);

    // Run a single phase. Same domain + enabled filtering as tick().
    void tick_phase(Phase phase, float dt);

    u32 system_count() const;
    u32 system_count(Phase phase) const;

    // Visit every registered system in (phase, registration) order. Used by
    // the editor's Systems panel.
    using SystemVisitor = void (*)(const SystemInfo& info, void* user);
    void iterate_systems_raw(SystemVisitor fn, void* user) const;

    template <typename Fn>
    void iterate_systems(Fn&& fn) const {
        struct Ctx { Fn& f; };
        Ctx ctx{fn};
        iterate_systems_raw(
            +[](const SystemInfo& info, void* user) {
                static_cast<Ctx*>(user)->f(info);
            }, &ctx);
    }

    // ---- Reset / serialization ---------------------------------------------

    // Destroy every alive entity. Component types and registered systems
    // stay. Used internally by load_binary; also useful for "new scene".
    void clear();

    // Binary world snapshot. Format:
    //   header: magic("ZUES") + version(1)
    //   types:  count + (name, size, align, saved_id)*
    //   slots:  count + generation*
    //   archs:  count + (col_count + saved_id* + row_count + (idx,gen)*
    //                    + raw column bytes ordered by saved id list)
    //
    // Round-trips entities, archetype layout, and component data. Component
    // types must already be registered in the target world (matched by name +
    // size + align).
    std::vector<u8> save_binary() const;
    Result          load_binary(const u8* data, std::size_t size);

    // VCS-friendly text format. Same data as save_binary, expressed as
    // entity/component records using the FieldInfo reflection so each
    // value reads as `{"hp": 100}` instead of opaque bytes. Diff + merge
    // friendly; slower to parse than binary but irrelevant for editor
    // workloads (worlds save/load at human pace).
    std::string     save_json() const;
    Result          load_json(const char* text, std::size_t size);

    // ---- Singleton components ---------------------------------------------
    //
    // A "singleton" is a designated entity carrying a specific component.
    // The plugin's `[Singleton]` attribute calls `ensure_singleton<T>()`
    // from the auto-generated on_load so the entity exists before any
    // system ticks; user code looks it up via `find_singleton(id)` (or
    // through the plugin's cached getter).
    //
    // Tracking is best-effort — `find_singleton` returns the entity the
    // engine itself spawned for that type. If the user adds the same
    // component to additional entities those won't be visible here; the
    // singleton stays the original.
    //
    // `archetype_version` is a monotonic counter that bumps whenever the
    // archetype graph changes (component add/remove, world clear/load,
    // hot-reload). Cached singleton pointers compare against it to know
    // whether to refresh.

    // Find-or-create. If the singleton entry for `id` already points at a
    // live entity that still has the component, returns it. Otherwise
    // creates a new entity, adds the component (default-initialised), and
    // records it. Returns NULL_ENTITY only on bad id.
    Entity ensure_singleton(ComponentId id);

    template <typename T>
    Entity ensure_singleton() { return ensure_singleton(component_id<T>()); }

    // Look up the designated singleton entity for `id`. NULL_ENTITY if
    // none has been registered (or if the recorded entity has since been
    // destroyed / lost the component — those drop to null lazily).
    Entity find_singleton(ComponentId id) const;

    template <typename T>
    Entity find_singleton() const { return find_singleton(component_id<T>()); }

    // Cached-pointer convenience: returns the component on the singleton
    // entity, or nullptr. The plugin's per-singleton cached getter is
    // built on top of this.
    template <typename T>
    T* singleton() {
        const ComponentId id = component_id<T>();
        const Entity e = find_singleton(id);
        return e.is_null() ? nullptr : static_cast<T*>(get_component(e, id));
    }

    // Monotonic counter — increments on every archetype mutation so
    // long-lived caches can detect "anything moved since I last looked".
    u64 archetype_version() const;

    // ---- Subtree (prefab) serialisation -----------------------------------
    //
    // Snapshot a single entity + every descendant via the hierarchy
    // helpers. Output is a JSON document with a "root" pointing at the
    // captured entity's index inside an embedded "entities" array. Used by
    // the editor to write .zprefab files (it adds top-level "guid" and
    // "version" fields around the snapshot).
    //
    // Internal entity indices are remapped to a dense [0..N) range so the
    // file is independent of the source world's slot allocation.
    std::string save_entity_subtree_json(Entity root) const;

    // Inverse: spawn fresh entities from a subtree snapshot. Returns the
    // root of the instantiated copy (NULL_ENTITY on parse failure). New
    // entities get fresh indices/generations; parent links inside the
    // subtree are rewired to the new ids; references OUT of the subtree
    // are dropped (set to NULL_ENTITY).
    Entity instantiate_entity_subtree_json(const char* text, std::size_t size);

private:
    struct Impl;
    Impl* m_impl;

    // Used by the templated register_component<T> to record T's typeid string.
    void register_typeid(const char* typeid_str, ComponentId id);
};

// =============================================================================
// Query<Ts...>
// =============================================================================
//
// Built by `world.query<Ts...>()`. Pass a callable to `.each` whose signature
// matches `(Entity, Ts&...)`. Use `.without<X, Y>()` to exclude entities that
// have any of those components. Filters compose:
//
//   world.query<Transform2D, Sprite>()
//        .without<SomeTag>()
//        .each([](Entity e, Transform2D& t, Sprite& s) { ... });

template <typename... Ts>
class Query {
public:
    explicit Query(World* w) : m_world(w) {
        m_required = { w->component_id<Ts>()... };
    }

    template <typename... Excl>
    Query& without() {
        (m_excluded.push_back(m_world->component_id<Excl>()), ...);
        return *this;
    }

    template <typename Fn>
    void each(Fn&& fn) {
        // Wrap user fn in a stateless thunk + pointer-back to the closure.
        struct Ctx { Fn& f; };
        Ctx ctx{fn};

        auto thunk = +[](void* user, Entity e, void** ptrs, u32 /*count*/) {
            auto* c = static_cast<Ctx*>(user);
            invoke_with_indices(c->f, e, ptrs, std::index_sequence_for<Ts...>{});
        };

        m_world->iterate_query(
            m_required.data(), static_cast<u32>(m_required.size()),
            m_excluded.data(), static_cast<u32>(m_excluded.size()),
            thunk, &ctx);
    }

private:
    template <typename Fn, std::size_t... Is>
    static void invoke_with_indices(Fn& fn, Entity e, void** ptrs,
                                    std::index_sequence<Is...>) {
        fn(e, *static_cast<Ts*>(ptrs[Is])...);
    }

    World*                   m_world;
    std::vector<ComponentId> m_required;
    std::vector<ComponentId> m_excluded;
};

}  // namespace Engine::ecs
