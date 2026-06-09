#include <zues/ecs/world.h>

#include <zues/asset.h>
#include <zues/components.h>
#include <zues/guid.h>
#include <zues/log.h>

#include "archetype.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::ecs {

namespace {
    struct EntitySlot {
        u32        generation = 0;          // 0 = slot never used; live entity has gen >= 1
        bool       alive      = false;
        Archetype* archetype  = nullptr;
        u32        row        = 0;
    };

    struct ArchetypeKey {
        std::vector<ComponentId> ids;       // sorted ascending
        bool operator==(const ArchetypeKey& o) const { return ids == o.ids; }
    };

    struct ArchetypeKeyHash {
        size_t operator()(const ArchetypeKey& k) const noexcept {
            size_t h = 0;
            for (auto id : k.ids) {
                h ^= static_cast<size_t>(id) + 0x9e3779b9u + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    std::vector<ComponentId> ids_plus(const std::vector<ComponentId>& a, ComponentId x) {
        std::vector<ComponentId> r;
        r.reserve(a.size() + 1);
        bool placed = false;
        for (auto id : a) {
            if (!placed && x < id) { r.push_back(x); placed = true; }
            if (id != x)           r.push_back(id);
        }
        if (!placed) r.push_back(x);
        return r;
    }

    std::vector<ComponentId> ids_minus(const std::vector<ComponentId>& a, ComponentId x) {
        std::vector<ComponentId> r;
        r.reserve(a.size());
        for (auto id : a) if (id != x) r.push_back(id);
        return r;
    }
}

struct SystemEntry {
    u32          id;
    std::string  name;
    SystemFn     fn;
    void*        user;
    SystemDomain domain  = SystemDomain::Both;
    bool         enabled = true;
};

struct World::Impl {
    // `std::deque` instead of `std::vector` so element addresses stay stable
    // across push_back. Archetypes cache `const ComponentType*` pointers into
    // this container (see `column_types[i] = &types[id-1]`); a vector
    // reallocation would dangle every existing archetype's column metadata
    // and crash the next destroy_entity / migrate. Deque uses chunked
    // allocation (16 elems / chunk on MSVC), so growth never moves existing
    // entries. Lookup by index stays O(1).
    std::deque<ComponentType>                     types;       // indexed by (id - 1)
    std::unordered_map<std::string, ComponentId>  by_name;
    std::unordered_map<std::string, ComponentId>  by_typeid;   // typeid(T).name() -> id

    std::vector<EntitySlot>                       slots;
    std::vector<u32>                              free_indices;

    std::unordered_map<ArchetypeKey,
                       std::unique_ptr<Archetype>,
                       ArchetypeKeyHash>          archetypes;

    Archetype* empty_arch = nullptr;

    // Cached IDs for hierarchy helpers (populated by register_builtins).
    ComponentId parent_id       = INVALID_COMPONENT_ID;
    ComponentId first_child_id  = INVALID_COMPONENT_ID;
    ComponentId next_sibling_id = INVALID_COMPONENT_ID;

    // Cached Transform2D id. When set (after register_builtins), every new
    // entity created via create_entity gets a default Transform2D — same UX
    // as Unity where every GameObject has a Transform. INVALID before
    // register_builtins runs, in which case create_entity skips the auto-add.
    ComponentId transform2d_id  = INVALID_COMPONENT_ID;

    // Cached Name id. Same auto-attach behavior — every new entity gets a
    // default Name set to "entity_<n>" with a monotonic counter so reused
    // slot indices don't collide. Manual name overrides via add_component
    // overwrite the default — last write wins.
    ComponentId name_id          = INVALID_COMPONENT_ID;
    u32         next_name_index  = 0;

    // Watermark of how many component types existed RIGHT AFTER the engine
    // builtins were registered. Project DLLs add types beyond this index;
    // unload_project_types() truncates back to this count so a hot-reload
    // doesn't leak Velocity / Position / etc. registrations from the old DLL.
    u32         builtin_type_count = 0;

    // Unknown components stashed during load_json. Keyed by live entity
    // index. When the world JSON references a component type that isn't
    // registered (because the project DLL hasn't loaded yet, or the type
    // was renamed, or just temporarily commented out), we save the raw
    // `data` JSON text instead of dropping it. The inspector renders a
    // disabled "?" row per stash, save_json round-trips the bytes back
    // out, and adopt_unknown_components(name) is the hot-reload hook
    // that re-materialises blobs once the type registers.
    struct UnknownComponentBlob {
        std::string type_name;
        std::string data_json;   // including outer braces, e.g. "{\"x\":1}"
    };
    std::unordered_map<u32, std::vector<UnknownComponentBlob>> unknowns;

    // Systems by phase.
    std::array<std::vector<SystemEntry>, PHASE_COUNT> systems;
    u32      next_system_id = 1;
    TickMode tick_mode      = TickMode::Edit;

    // Singleton tracking. The "designated" entity for a singleton component
    // is the one auto-spawned via ensure_singleton; if the user manually
    // adds the same component to other entities those won't be tracked here
    // (singleton semantics are best-effort — first one wins). The plugin's
    // generated cached getter looks this up via find_singleton(); the cache
    // invalidation key is `archetype_version`, which bumps on any add /
    // remove / archetype-creating mutation so stale pointers can't survive.
    std::unordered_map<ComponentId, Entity> singletons;
    u64                                     archetype_version = 1;

    // Root entity ordering. Roots aren't stored in any linked list — the
    // hierarchy primitives (FirstChild / NextSibling) only express
    // parent→child→child relationships, so without an explicit list the
    // editor's tree view would render roots in slot-allocation order.
    // That breaks user expectations the moment they want to reorder two
    // top-level entities.
    //
    // We track an explicit ordered list here instead. Maintenance hooks:
    //   create_entity()  -> append index
    //   destroy_entity() -> remove index
    //   set_parent(non-null)   -> remove (no longer a root)
    //   unparent / set_parent(null) -> append (becomes a root)
    // Reorder ops splice within the vector. Save/load round-trip via a
    // top-level "root_order" array in the JSON.
    std::vector<u32> roots_order;

    Archetype* get_or_create_archetype(const std::vector<ComponentId>& sorted_ids) {
        ArchetypeKey key{sorted_ids};
        if (auto it = archetypes.find(key); it != archetypes.end()) return it->second.get();

        auto arch = std::make_unique<Archetype>();
        arch->component_ids = sorted_ids;
        arch->column_bytes.resize(sorted_ids.size());
        arch->column_types.resize(sorted_ids.size());
        for (size_t i = 0; i < sorted_ids.size(); ++i) {
            arch->column_types[i] = &types[sorted_ids[i] - 1];
        }
        Archetype* raw = arch.get();
        archetypes.emplace(std::move(key), std::move(arch));
        return raw;
    }
};

// ---- ctor/dtor --------------------------------------------------------------

World::World() : m_impl(new Impl) {
    m_impl->empty_arch = m_impl->get_or_create_archetype({});
}

World::~World() { delete m_impl; }

// ---- Component types --------------------------------------------------------

ComponentId World::register_component_type(ComponentType desc) {
    std::string name = desc.name ? desc.name : "";
    if (auto it = m_impl->by_name.find(name); it != m_impl->by_name.end()) {
        return it->second;
    }
    const ComponentId id = static_cast<ComponentId>(m_impl->types.size() + 1);
    desc.id = id;
    m_impl->types.push_back(desc);
    const std::string name_copy = name;   // keep a copy past the move
    m_impl->by_name.emplace(std::move(name), id);
    // Hot-reload UX: if any entities loaded with this type still have
    // it stashed as an unknown blob, materialise them now into real
    // component rows. The data round-trips through their saved JSON,
    // not bytes, so type renames + field reorders survive cleanly.
    if (!name_copy.empty() && !m_impl->unknowns.empty()) {
        adopt_unknown_components(name_copy.c_str());
    }
    return id;
}

ComponentId World::find_component_id(const char* name) const {
    if (!name) return INVALID_COMPONENT_ID;
    auto it = m_impl->by_name.find(name);
    return (it != m_impl->by_name.end()) ? it->second : INVALID_COMPONENT_ID;
}

ComponentId World::find_component_id_by_typeid(const char* typeid_str) const {
    if (!typeid_str) return INVALID_COMPONENT_ID;
    auto it = m_impl->by_typeid.find(typeid_str);
    return (it != m_impl->by_typeid.end()) ? it->second : INVALID_COMPONENT_ID;
}

void World::register_typeid(const char* typeid_str, ComponentId id) {
    if (!typeid_str || id == INVALID_COMPONENT_ID) return;
    m_impl->by_typeid[typeid_str] = id;
}

const ComponentType* World::get_component_type(ComponentId id) const {
    if (id == INVALID_COMPONENT_ID || id > m_impl->types.size()) return nullptr;
    return &m_impl->types[id - 1];
}

void World::set_component_category(ComponentId id, const char* category) {
    if (id == INVALID_COMPONENT_ID || id > m_impl->types.size()) return;
    m_impl->types[id - 1].category = category ? category : "";
}

// ---- Entities ---------------------------------------------------------------

namespace {
    // Tiny helpers that keep the "is this index in roots_order?" logic in
    // one place. roots_order entries are slot indices, not full Entity
    // handles — generation flips are caught by the destroy path.
    void roots_remove(std::vector<u32>& roots, u32 idx) {
        auto it = std::find(roots.begin(), roots.end(), idx);
        if (it != roots.end()) roots.erase(it);
    }
    void roots_append_unique(std::vector<u32>& roots, u32 idx) {
        if (std::find(roots.begin(), roots.end(), idx) == roots.end())
            roots.push_back(idx);
    }

    // Forward decl — defined in the anon namespace further down (lives
    // alongside set_parent/unparent/iterate_children helpers). destroy_entity
    // calls it to splice a destroyed child out of its parent's
    // FirstChild / NextSibling list before the slot is reaped.
    void remove_from_chain(World& world, ComponentId first_child_id,
                           ComponentId next_sibling_id,
                           Entity parent, Entity child);
}  // namespace

Entity World::create_entity() {
    u32 idx;
    if (!m_impl->free_indices.empty()) {
        idx = m_impl->free_indices.back();
        m_impl->free_indices.pop_back();
    } else {
        idx = static_cast<u32>(m_impl->slots.size());
        m_impl->slots.emplace_back();
    }

    auto& slot = m_impl->slots[idx];
    ++slot.generation;
    if (slot.generation == 0) slot.generation = 1;   // skip 0 on wrap
    slot.alive     = true;
    slot.archetype = m_impl->empty_arch;
    const Entity e{idx, slot.generation};
    slot.row = slot.archetype->push_entity(e);
    // New entities are roots until set_parent says otherwise.
    roots_append_unique(m_impl->roots_order, idx);

    // Auto-attach Transform2D. After register_builtins runs, every new
    // entity gets one by default (Unity-style). Skipped during the brief
    // window before register_builtins, and intentionally bypassed by
    // load_binary (which builds slots directly without create_entity).
    if (m_impl->transform2d_id != INVALID_COMPONENT_ID) {
        Engine::components::Transform2D t{};
        add_component(e, m_impl->transform2d_id, &t);
    }
    // Auto-attach Name with a monotonic "entity_N" default. Counter only
    // advances; reused slot indices never get the same auto-name twice.
    if (m_impl->name_id != INVALID_COMPONENT_ID) {
        Engine::components::Name n{};
        std::snprintf(n.value, sizeof(n.value), "entity_%u",
                      m_impl->next_name_index++);
        add_component(e, m_impl->name_id, &n);
    }
    return e;
}

void World::destroy_entity(Entity e) {
    if (!is_alive(e)) return;

    // ---- Unlink from parent's child chain ---------------------------------
    // If this entity is a child of someone, splice it out of the parent's
    // FirstChild / NextSibling list BEFORE we destroy it. Without this, the
    // parent's FirstChild (or some previous sibling's NextSibling) still
    // points at the dead slot, and iterate_children halts the moment it
    // hits the dead reference -- every sibling AFTER the destroyed entity
    // vanishes from the Hierarchy walk.
    if (m_impl->parent_id != INVALID_COMPONENT_ID) {
        Entity parent = parent_of(e);
        if (!parent.is_null() && is_alive(parent) &&
            m_impl->first_child_id  != INVALID_COMPONENT_ID &&
            m_impl->next_sibling_id != INVALID_COMPONENT_ID) {
            remove_from_chain(*this, m_impl->first_child_id,
                              m_impl->next_sibling_id, parent, e);
        }
    }

    // Recursively destroy descendants. Without this, children keep a
    // `Parent` component pointing at the destroyed slot and become
    // orphans -- they can't appear as roots (they have a Parent column)
    // and can't be reached through any live parent either. Unity-style
    // "deleting a node deletes the subtree".
    //
    // Snapshot child handles before iterating: each destroy_entity call
    // mutates the parent's FirstChild / NextSibling chain, which would
    // invalidate iterate_children halfway through. The snapshot is small
    // (children of one entity) so the allocation cost is negligible.
    {
        std::vector<Entity> kids;
        iterate_children(e, [&](Entity c) { kids.push_back(c); });
        for (Entity c : kids) destroy_entity(c);
    }

    auto& slot = m_impl->slots[e.index];

    Entity moved = slot.archetype->swap_remove(slot.row);
    if (!moved.is_null()) {
        m_impl->slots[moved.index].row = slot.row;
    }

    slot.alive     = false;
    slot.archetype = nullptr;
    slot.row       = 0;
    m_impl->free_indices.push_back(e.index);
    roots_remove(m_impl->roots_order, e.index);
    // Drop any stashed unknown components -- the entity slot is going
    // back into the free list and may be reused by an unrelated entity.
    m_impl->unknowns.erase(e.index);
}

bool World::is_alive(Entity e) const {
    if (e.index >= m_impl->slots.size()) return false;
    const auto& slot = m_impl->slots[e.index];
    return slot.alive && slot.generation == e.generation;
}

Entity World::live_entity_for_index(u32 index) const {
    if (index >= m_impl->slots.size()) return NULL_ENTITY;
    const auto& slot = m_impl->slots[index];
    if (!slot.alive) return NULL_ENTITY;
    return Entity{ index, slot.generation };
}

// ---- Components -------------------------------------------------------------

void* World::add_component(Entity e, ComponentId id, const void* initial_bytes) {
    if (!is_alive(e))                        return nullptr;
    if (id == INVALID_COMPONENT_ID)          return nullptr;
    if (id > m_impl->types.size())           return nullptr;

    auto& slot  = m_impl->slots[e.index];
    const u32 size = m_impl->types[id - 1].size;

    // Already present → overwrite in place.
    if (int col = slot.archetype->find_column(id); col >= 0) {
        u8* p = slot.archetype->column_at(col, slot.row);
        if (initial_bytes) std::memcpy(p, initial_bytes, size);
        return p;
    }

    // Migrate to a new archetype with this component added.
    const auto new_ids  = ids_plus(slot.archetype->component_ids, id);
    Archetype* new_arch = m_impl->get_or_create_archetype(new_ids);
    const u32  new_row  = new_arch->push_entity(e);

    // Copy overlap columns from old archetype to new.
    for (size_t i = 0; i < slot.archetype->component_ids.size(); ++i) {
        const ComponentId cid = slot.archetype->component_ids[i];
        const int new_col = new_arch->find_column(cid);
        std::memcpy(new_arch->column_at(new_col, new_row),
                    slot.archetype->column_at(static_cast<int>(i), slot.row),
                    slot.archetype->column_types[i]->size);
    }

    // Initialize the new component: explicit bytes → default prototype → zero.
    const int new_col_for_id = new_arch->find_column(id);
    u8* dst = new_arch->column_at(new_col_for_id, new_row);
    if (initial_bytes) {
        std::memcpy(dst, initial_bytes, size);
    } else {
        const auto& desc = m_impl->types[id - 1];
        if (desc.default_data && desc.default_data_size == size)
            std::memcpy(dst, desc.default_data, size);
        else
            std::memset(dst, 0, size);
    }

    // Remove entity from the old archetype.
    Entity moved = slot.archetype->swap_remove(slot.row);
    if (!moved.is_null()) {
        m_impl->slots[moved.index].row = slot.row;
    }

    slot.archetype = new_arch;
    slot.row       = new_row;
    // Bump the version so cached singleton pointers (and anything else that
    // tracks "did the archetype graph change since I last looked") refresh
    // on next read. Cheap — just a u64 increment.
    ++m_impl->archetype_version;
    return dst;
}

void World::remove_component(Entity e, ComponentId id) {
    if (!is_alive(e)) return;
    auto& slot = m_impl->slots[e.index];
    if (slot.archetype->find_column(id) < 0) return;

    const auto new_ids  = ids_minus(slot.archetype->component_ids, id);
    Archetype* new_arch = m_impl->get_or_create_archetype(new_ids);
    const u32  new_row  = new_arch->push_entity(e);

    for (size_t i = 0; i < slot.archetype->component_ids.size(); ++i) {
        const ComponentId cid = slot.archetype->component_ids[i];
        if (cid == id) continue;
        const int new_col = new_arch->find_column(cid);
        std::memcpy(new_arch->column_at(new_col, new_row),
                    slot.archetype->column_at(static_cast<int>(i), slot.row),
                    slot.archetype->column_types[i]->size);
    }

    Entity moved = slot.archetype->swap_remove(slot.row);
    if (!moved.is_null()) {
        m_impl->slots[moved.index].row = slot.row;
    }

    slot.archetype = new_arch;
    slot.row       = new_row;
    ++m_impl->archetype_version;
}

void* World::get_component(Entity e, ComponentId id) {
    if (!is_alive(e)) return nullptr;
    auto& slot = m_impl->slots[e.index];
    const int col = slot.archetype->find_column(id);
    return (col < 0) ? nullptr : slot.archetype->column_at(col, slot.row);
}

const void* World::get_component(Entity e, ComponentId id) const {
    if (!is_alive(e)) return nullptr;
    const auto& slot = m_impl->slots[e.index];
    const int col = slot.archetype->find_column(id);
    return (col < 0) ? nullptr : slot.archetype->column_at(col, slot.row);
}

bool World::has_component(Entity e, ComponentId id) const {
    if (!is_alive(e)) return false;
    const auto& slot = m_impl->slots[e.index];
    return slot.archetype->find_column(id) >= 0;
}

// ---- Stats ------------------------------------------------------------------

u32 World::entity_count() const {
    u32 n = 0;
    for (const auto& s : m_impl->slots) if (s.alive) ++n;
    return n;
}

u32 World::archetype_count() const {
    return static_cast<u32>(m_impl->archetypes.size());
}

void World::iterate_alive_raw(EntityVisitor fn, void* user) const {
    if (!fn) return;
    for (u32 i = 0; i < m_impl->slots.size(); ++i) {
        const auto& slot = m_impl->slots[i];
        if (slot.alive) fn(Entity{i, slot.generation}, user);
    }
}

void World::iterate_roots_raw(EntityVisitorRaw fn, void* user) const {
    if (!fn) return;
    // Walk roots_order. Skip dead slots defensively (destroy_entity
    // removes; this guards against any stale state from external
    // mutations / future bugs).
    for (u32 idx : m_impl->roots_order) {
        if (idx >= m_impl->slots.size()) continue;
        const auto& slot = m_impl->slots[idx];
        if (!slot.alive) continue;
        fn(Entity{idx, slot.generation}, user);
    }
}

void World::iterate_component_types_raw(ComponentTypeVisitor fn, void* user) const {
    if (!fn) return;
    for (const auto& t : m_impl->types) fn(t.id, t, user);
}

// ---- Queries ----------------------------------------------------------------

void World::iterate_query(const ComponentId* required, u32 n_required,
                          const ComponentId* excluded, u32 n_excluded,
                          QueryRowFn fn, void* user) {
    if (!fn) return;

    // Reused scratch buffers across archetypes — sized once, refilled per arch.
    std::vector<int>   cols(n_required);
    std::vector<void*> ptrs(n_required);

    // First pass: collect every (archetype, entity) pair that matches the
    // required/excluded filter. We snapshot the entity list rather than
    // iterating arch->entities directly because the user's callback may
    // destroy or migrate entities (Add/Remove component, DestroyEntity)
    // mid-walk -- both swap_remove the entity out of the archetype and
    // shift later rows down, which makes a naive `for (row=0; row<count)`
    // either skip entities or read past the shrunk vector. Snapshotting
    // costs one allocation per query but trades correctness for it.
    struct Hit { Archetype* arch; Entity e; };
    std::vector<Hit> hits;

    for (auto& [key, arch_uptr] : m_impl->archetypes) {
        Archetype* arch = arch_uptr.get();
        if (arch->count() == 0) continue;

        // Required: every required column must be present.
        bool ok = true;
        for (u32 i = 0; i < n_required; ++i) {
            int c = arch->find_column(required[i]);
            if (c < 0) { ok = false; break; }
        }
        if (!ok) continue;

        // Excluded: none of these may be present.
        for (u32 i = 0; i < n_excluded; ++i) {
            if (arch->find_column(excluded[i]) >= 0) { ok = false; break; }
        }
        if (!ok) continue;

        for (Entity e : arch->entities) hits.push_back({arch, e});
    }

    // Second pass: walk the snapshot. Re-look up the slot for each entity
    // so callbacks that migrated us to a different archetype (Add<T> in
    // the body) see the new column layout. Entities destroyed since the
    // snapshot was taken just fail the is_alive check and skip.
    for (const Hit& h : hits) {
        if (!is_alive(h.e)) continue;
        const auto& slot = m_impl->slots[h.e.index];
        Archetype* arch = slot.archetype;
        if (!arch) continue;
        // Resolve columns against the LIVE archetype, not the snapshot's.
        // This way Add<T> mid-walk doesn't crash on a stale column index.
        bool ok = true;
        for (u32 i = 0; i < n_required; ++i) {
            int c = arch->find_column(required[i]);
            if (c < 0) { ok = false; break; }
            cols[i] = c;
        }
        if (!ok) continue;
        for (u32 i = 0; i < n_excluded; ++i) {
            if (arch->find_column(excluded[i]) >= 0) { ok = false; break; }
        }
        if (!ok) continue;
        for (u32 i = 0; i < n_required; ++i) {
            ptrs[i] = arch->column_at(cols[i], slot.row);
        }
        fn(user, h.e, ptrs.data(), n_required);
    }
}

// ---- Systems + tick ---------------------------------------------------------

SystemHandle World::add_system(const char* name, Phase phase,
                                SystemFn fn, void* user, SystemDomain domain) {
    if (!fn) return {phase, 0};
    const u32 id = m_impl->next_system_id++;
    auto& bucket = m_impl->systems[static_cast<size_t>(phase)];
    SystemEntry e{};
    e.id      = id;
    e.name    = name ? std::string(name) : std::string{};
    e.fn      = fn;
    e.user    = user;
    e.domain  = domain;
    e.enabled = true;
    bucket.push_back(std::move(e));
    return {phase, id};
}

bool World::remove_system(SystemHandle h) {
    if (!h.is_valid()) return false;
    auto& bucket = m_impl->systems[static_cast<size_t>(h.phase)];
    auto it = std::find_if(bucket.begin(), bucket.end(),
                           [&](const SystemEntry& s) { return s.id == h.id; });
    if (it == bucket.end()) return false;
    bucket.erase(it);
    return true;
}

bool World::set_system_enabled(SystemHandle h, bool enabled) {
    if (!h.is_valid()) return false;
    auto& bucket = m_impl->systems[static_cast<size_t>(h.phase)];
    for (auto& s : bucket) {
        if (s.id == h.id) { s.enabled = enabled; return true; }
    }
    return false;
}

void     World::set_tick_mode(TickMode m) { m_impl->tick_mode = m; }
TickMode World::tick_mode()        const  { return m_impl->tick_mode; }

namespace {
    // A system runs iff enabled AND (domain == Both OR matches current mode).
    bool should_run(const SystemEntry& s, TickMode mode) {
        if (!s.enabled) return false;
        if (s.domain == SystemDomain::Both) return true;
        return (mode == TickMode::Edit && s.domain == SystemDomain::Editor)
            || (mode == TickMode::Play && s.domain == SystemDomain::Game);
    }
}

void World::tick(float dt) {
    const TickMode mode = m_impl->tick_mode;
    for (size_t p = 0; p < PHASE_COUNT; ++p) {
        for (auto& s : m_impl->systems[p]) {
            if (s.fn && should_run(s, mode)) s.fn(*this, dt, s.user);
        }
    }
}

void World::tick_phase(Phase phase, float dt) {
    const TickMode mode = m_impl->tick_mode;
    for (auto& s : m_impl->systems[static_cast<size_t>(phase)]) {
        if (s.fn && should_run(s, mode)) s.fn(*this, dt, s.user);
    }
}

u32 World::system_count() const {
    u32 n = 0;
    for (const auto& bucket : m_impl->systems) n += static_cast<u32>(bucket.size());
    return n;
}

u32 World::system_count(Phase phase) const {
    return static_cast<u32>(m_impl->systems[static_cast<size_t>(phase)].size());
}

void World::iterate_systems_raw(SystemVisitor fn, void* user) const {
    if (!fn) return;
    for (size_t p = 0; p < PHASE_COUNT; ++p) {
        for (const auto& s : m_impl->systems[p]) {
            SystemInfo info{};
            info.handle  = {static_cast<Phase>(p), s.id};
            info.name    = s.name.c_str();
            info.phase   = static_cast<Phase>(p);
            info.domain  = s.domain;
            info.enabled = s.enabled;
            fn(info, user);
        }
    }
}

// ---- Reset / serialization --------------------------------------------------

void World::clear() {
    // Walk a snapshot of slots so destroying doesn't invalidate iteration.
    const u32 n = static_cast<u32>(m_impl->slots.size());
    for (u32 i = 0; i < n; ++i) {
        if (m_impl->slots[i].alive) {
            destroy_entity({i, m_impl->slots[i].generation});
        }
    }
    // Reset the auto-name counter so a Ctrl+R doesn't produce
    // entity_36, _37, _38... when only 5 entities exist.
    m_impl->next_name_index = 0;
    // Drop singleton tracking; the plugin's on_load re-runs ensure_singleton
    // for every [Singleton] component after a load and re-fills this map.
    m_impl->singletons.clear();
    // Roots are tracked explicitly; destroy_entity already pulled each
    // entry but clear it defensively in case any stragglers remain.
    m_impl->roots_order.clear();
    m_impl->unknowns.clear();
    ++m_impl->archetype_version;
}

// ---- Singleton bookkeeping --------------------------------------------------

Entity World::ensure_singleton(ComponentId id) {
    if (id == INVALID_COMPONENT_ID || id > m_impl->types.size()) return NULL_ENTITY;

    // Honor an existing record if it's still valid. Validity = the entity
    // is alive AND still carries the component. Either condition failing
    // (manual destroy, manual remove_component) drops us back to "create
    // a fresh designated entity".
    auto it = m_impl->singletons.find(id);
    if (it != m_impl->singletons.end()) {
        const Entity prev = it->second;
        if (is_alive(prev) && has_component(prev, id)) return prev;
        m_impl->singletons.erase(it);
    }

    // Adopt: if the world already contains an entity carrying this
    // component (typical after world load — the singleton was serialized
    // as a regular entity), claim the lowest-index match instead of
    // creating a duplicate. Keeps "save → load → ensure" idempotent.
    for (u32 i = 0; i < m_impl->slots.size(); ++i) {
        const auto& slot = m_impl->slots[i];
        if (!slot.alive || !slot.archetype) continue;
        if (slot.archetype->find_column(id) < 0) continue;
        const Entity adopted{i, slot.generation};
        m_impl->singletons[id] = adopted;
        return adopted;
    }

    const Entity e = create_entity();
    add_component(e, id, nullptr);   // zero-init / default prototype
    m_impl->singletons[id] = e;

    // Stamp the entity with a default name based on the component type so
    // the editor's Hierarchy "Globals" section reads as
    // "GameManager (singleton)" instead of "entity_27".
    if (m_impl->name_id != INVALID_COMPONENT_ID) {
        if (auto* nm = static_cast<Engine::components::Name*>(
                get_component(e, m_impl->name_id))) {
            const auto& tdesc = m_impl->types[id - 1];
            const char* base = tdesc.name ? tdesc.name : "Singleton";
            std::snprintf(nm->value, sizeof(nm->value), "%s (singleton)", base);
        }
    }
    return e;
}

Entity World::find_singleton(ComponentId id) const {
    auto it = m_impl->singletons.find(id);
    if (it == m_impl->singletons.end()) return NULL_ENTITY;
    const Entity e = it->second;
    if (!is_alive(e))           return NULL_ENTITY;
    if (!has_component(e, id))  return NULL_ENTITY;
    return e;
}

u64 World::archetype_version() const {
    return m_impl->archetype_version;
}

namespace {
    constexpr u32 ZUES_SAVE_MAGIC   = 0x53455553u;   // 'Z','U','E','S' little-endian: S,E,U,Z
    constexpr u32 ZUES_SAVE_VERSION = 1;

    inline void w_bytes(std::vector<u8>& out, const void* data, size_t n) {
        const u8* b = static_cast<const u8*>(data);
        out.insert(out.end(), b, b + n);
    }
    inline void w_u32(std::vector<u8>& out, u32 v)  { w_bytes(out, &v, sizeof(v)); }
    inline void w_str(std::vector<u8>& out, const std::string& s) {
        w_u32(out, static_cast<u32>(s.size()));
        if (!s.empty()) w_bytes(out, s.data(), s.size());
    }

    struct Reader {
        const u8*   data;
        std::size_t total;
        std::size_t off = 0;
        bool ok = true;

        bool read(void* dst, size_t n) {
            if (!ok || off + n > total) { ok = false; return false; }
            std::memcpy(dst, data + off, n);
            off += n;
            return true;
        }
        bool u32_(u32& v) { return read(&v, sizeof(v)); }
        bool str_(std::string& s) {
            u32 len = 0;
            if (!u32_(len)) return false;
            s.resize(len);
            if (len > 0) return read(s.data(), len);
            return true;
        }
    };
}

std::vector<u8> World::save_binary() const {
    std::vector<u8> out;
    out.reserve(4096);

    w_u32(out, ZUES_SAVE_MAGIC);
    w_u32(out, ZUES_SAVE_VERSION);

    // Component types
    w_u32(out, static_cast<u32>(m_impl->types.size()));
    for (const auto& t : m_impl->types) {
        w_str(out, std::string(t.name ? t.name : ""));
        w_u32(out, t.size);
        w_u32(out, t.align);
        w_u32(out, t.id);
    }

    // Slots (just generations — alive flag derives from archetype membership)
    w_u32(out, static_cast<u32>(m_impl->slots.size()));
    for (const auto& s : m_impl->slots) {
        w_u32(out, s.generation);
    }

    // Archetypes that have at least one entity
    u32 arch_count = 0;
    for (auto& kv : m_impl->archetypes) if (kv.second->count() > 0) ++arch_count;
    w_u32(out, arch_count);

    for (auto& kv : m_impl->archetypes) {
        Archetype* a = kv.second.get();
        if (a->count() == 0) continue;

        w_u32(out, static_cast<u32>(a->component_ids.size()));
        for (auto cid : a->component_ids) w_u32(out, cid);

        w_u32(out, a->count());
        for (auto e : a->entities) { w_u32(out, e.index); w_u32(out, e.generation); }

        for (size_t i = 0; i < a->component_ids.size(); ++i) {
            const auto& col = a->column_bytes[i];
            if (!col.empty()) w_bytes(out, col.data(), col.size());
        }
    }

    return out;
}

Result World::load_binary(const u8* data, std::size_t size) {
    if (!data || size < 8) return Result::InvalidArgument;

    Reader r{data, size};
    u32 magic = 0, version = 0;
    if (!r.u32_(magic) || !r.u32_(version))      return Result::Error;
    if (magic   != ZUES_SAVE_MAGIC)              return Result::Error;
    if (version != ZUES_SAVE_VERSION)            return Result::Error;

    // Read saved component types and build saved_id -> current_id remap.
    u32 type_count = 0;
    if (!r.u32_(type_count)) return Result::Error;

    std::vector<ComponentId> remap(type_count + 1, INVALID_COMPONENT_ID);
    for (u32 i = 0; i < type_count; ++i) {
        std::string name;
        u32 saved_size = 0, saved_align = 0, saved_id = 0;
        if (!r.str_(name) || !r.u32_(saved_size) ||
            !r.u32_(saved_align) || !r.u32_(saved_id)) return Result::Error;

        ComponentId cur = find_component_id(name.c_str());
        if (cur == INVALID_COMPONENT_ID) return Result::NotFound;

        const ComponentType* desc = get_component_type(cur);
        if (!desc) return Result::Error;
        if (desc->size != saved_size || desc->align != saved_align) return Result::AbiMismatch;

        if (saved_id < remap.size()) remap[saved_id] = cur;
    }

    // Wipe current state and rebuild.
    clear();
    m_impl->slots.clear();
    m_impl->free_indices.clear();
    // Drop all archetype contents (keep map keys; they'll be reused/recreated).
    for (auto& kv : m_impl->archetypes) {
        kv.second->entities.clear();
        for (auto& col : kv.second->column_bytes) col.clear();
    }

    // Slots
    u32 slot_count = 0;
    if (!r.u32_(slot_count)) return Result::Error;
    m_impl->slots.resize(slot_count);
    for (u32 i = 0; i < slot_count; ++i) {
        u32 gen = 0;
        if (!r.u32_(gen)) return Result::Error;
        m_impl->slots[i].generation = gen;
        m_impl->slots[i].alive      = false;
        m_impl->slots[i].archetype  = nullptr;
        m_impl->slots[i].row        = 0;
    }

    // Archetypes
    u32 arch_count = 0;
    if (!r.u32_(arch_count)) return Result::Error;

    for (u32 ai = 0; ai < arch_count; ++ai) {
        u32 col_count = 0;
        if (!r.u32_(col_count)) return Result::Error;

        std::vector<ComponentId> ids_save_order(col_count);
        for (u32 c = 0; c < col_count; ++c) {
            u32 sid = 0;
            if (!r.u32_(sid)) return Result::Error;
            if (sid >= remap.size() || remap[sid] == INVALID_COMPONENT_ID) return Result::Error;
            ids_save_order[c] = remap[sid];
        }
        std::vector<ComponentId> ids_sorted = ids_save_order;
        std::sort(ids_sorted.begin(), ids_sorted.end());

        Archetype* arch = m_impl->get_or_create_archetype(ids_sorted);

        u32 row_count = 0;
        if (!r.u32_(row_count)) return Result::Error;

        std::vector<Entity> ents(row_count);
        for (u32 row = 0; row < row_count; ++row) {
            if (!r.u32_(ents[row].index) || !r.u32_(ents[row].generation)) return Result::Error;
        }

        // Pre-size all columns.
        for (size_t c = 0; c < arch->component_ids.size(); ++c) {
            arch->column_bytes[c].resize(static_cast<size_t>(arch->column_types[c]->size) * row_count);
        }
        // Read column blocks IN SAVE ORDER, route to the correct archetype column.
        for (size_t save_col = 0; save_col < col_count; ++save_col) {
            const ComponentId target = ids_save_order[save_col];
            const int arch_col = arch->find_column(target);
            if (arch_col < 0) return Result::Error;
            const u32 size_per_row = arch->column_types[arch_col]->size;
            const size_t total = static_cast<size_t>(size_per_row) * row_count;
            if (total > 0 && !r.read(arch->column_bytes[arch_col].data(), total)) return Result::Error;
        }

        // Populate the entities array, link slots back to this archetype.
        arch->entities = std::move(ents);
        for (u32 row = 0; row < arch->entities.size(); ++row) {
            const Entity e = arch->entities[row];
            if (e.index >= m_impl->slots.size()) return Result::Error;
            auto& slot = m_impl->slots[e.index];
            slot.alive     = true;
            slot.archetype = arch;
            slot.row       = row;
            // generation in entity must match slot.generation we already set
            if (slot.generation != e.generation) return Result::Error;
        }
    }

    // Free list = every slot that no archetype claimed.
    for (u32 i = 0; i < m_impl->slots.size(); ++i) {
        if (!m_impl->slots[i].alive) m_impl->free_indices.push_back(i);
    }

    // Rebuild roots_order from scratch. clear() emptied it, and the
    // archetype/slot reconstruction above doesn't go through
    // create_entity / set_parent so the maintenance hooks never fired.
    // Without this the editor's Hierarchy panel (which walks
    // iterate_roots, not iterate_alive) shows zero entities post-Stop
    // even though they're alive in the world. Walk slots in index order
    // so the order is deterministic; slip in only entities that have
    // no Parent component (i.e. real roots).
    m_impl->roots_order.clear();
    for (u32 i = 0; i < m_impl->slots.size(); ++i) {
        const auto& slot = m_impl->slots[i];
        if (!slot.alive || !slot.archetype) continue;
        if (m_impl->parent_id != INVALID_COMPONENT_ID &&
            slot.archetype->find_column(m_impl->parent_id) >= 0) continue;
        m_impl->roots_order.push_back(i);
    }

    if (!r.ok) return Result::Error;
    return Result::Ok;
}

// ---- JSON serialization -----------------------------------------------------
// Format: same shape as save_binary but expressed via FieldInfo so values
// read as named fields ({"hp": 100} etc.). Diff/merge friendly. Implemented
// without nlohmann/json to keep core lib free of that dep - hand-written
// emit + a tiny tolerant scanner. Schema:
//
//   {
//     "version": 1,
//     "next_name_index": 12,
//     "entities": [
//       { "index": 1, "generation": 1, "components": [
//           {"type": "Transform2D", "data": {"position":[10,20], ...}},
//           ...
//       ]},
//       ...
//     ]
//   }
//
// Only entities with at least one component are emitted; bare slots are
// reconstructed by load_json from the highest entity index seen.
namespace {

constexpr u32 ZUES_JSON_VERSION = 1;

// ---- Emit helpers (no string-quoting library; we control the input) -------
inline void j_str(std::string& o, const char* s) {
    o += '"';
    for (const char* p = s; p && *p; ++p) {
        switch (*p) {
            case '"' : o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default  : o += *p;     break;
        }
    }
    o += '"';
}
inline void j_kv_open(std::string& o, const char* k) {
    j_str(o, k); o += ':';
}

inline void emit_field_value(std::string& o,
                              const ecs::FieldInfo& f,
                              const u8* base) {
    using K = ecs::FieldKind;
    const u8* p = base + f.offset;
    char buf[64];
    auto emit_int = [&](long long v) {
        std::snprintf(buf, sizeof(buf), "%lld", v); o += buf;
    };
    auto emit_uint = [&](unsigned long long v) {
        std::snprintf(buf, sizeof(buf), "%llu", v); o += buf;
    };
    auto emit_float = [&](double v) {
        // %.9g preserves single-precision exactly; %.17g for double.
        std::snprintf(buf, sizeof(buf), "%.9g", v); o += buf;
    };
    auto emit_vec = [&](int n) {
        const float* fp = reinterpret_cast<const float*>(p);
        o += '[';
        for (int i = 0; i < n; ++i) {
            if (i) o += ',';
            emit_float(fp[i]);
        }
        o += ']';
    };
    switch (f.kind) {
        case K::Bool:    o += (*p ? "true" : "false"); break;
        case K::I8:      emit_int(static_cast<i8>(*p)); break;
        case K::I16:     emit_int(*reinterpret_cast<const i16*>(p)); break;
        case K::I32:     emit_int(*reinterpret_cast<const i32*>(p)); break;
        case K::I64:     emit_int(*reinterpret_cast<const i64*>(p)); break;
        case K::U8:      emit_uint(*p); break;
        case K::U16:     emit_uint(*reinterpret_cast<const u16*>(p)); break;
        case K::U32:     emit_uint(*reinterpret_cast<const u32*>(p)); break;
        case K::U64:     emit_uint(*reinterpret_cast<const u64*>(p)); break;
        case K::F32:     emit_float(*reinterpret_cast<const float*>(p)); break;
        case K::F64:     emit_float(*reinterpret_cast<const double*>(p)); break;
        case K::Vec2:    emit_vec(2); break;
        case K::Vec3:    emit_vec(3); break;
        case K::Vec4:    emit_vec(4); break;
        case K::Color:   emit_vec(4); break;
        case K::Entity:
        case K::EntityRef: {
            // { u32 index, u32 generation }. Emit as 2-elem array.
            const u32* ip = reinterpret_cast<const u32*>(p);
            std::snprintf(buf, sizeof(buf), "[%u,%u]", ip[0], ip[1]);
            o += buf;
            break;
        }
        case K::Handle: {
            // Same wire layout as Entity but with an optional third
            // element: the asset's stable GUID hex when the editor /
            // host registered one via AssetRegistry::bind_runtime_handle.
            // The guid is what survives a relaunch -- the (idx, gen)
            // pair is just a hint that's correct when nothing has
            // moved since save. Loaders prefer the guid.
            //
            // Probe Texture first since it's by far the most common
            // Handle field (Sprite::texture); fall back to Font.
            const u32* ip = reinterpret_cast<const u32*>(p);
            Guid g = AssetRegistry::instance()
                .guid_for_runtime_handle(AssetKind::Texture, ip[0]);
            if (g.is_null()) {
                g = AssetRegistry::instance()
                    .guid_for_runtime_handle(AssetKind::Font, ip[0]);
            }
            if (g.is_null()) {
                std::snprintf(buf, sizeof(buf), "[%u,%u]", ip[0], ip[1]);
                o += buf;
            } else {
                const std::string hex = guid_to_hex(g);
                std::snprintf(buf, sizeof(buf), "[%u,%u,\"%s\"]",
                              ip[0], ip[1], hex.c_str());
                o += buf;
            }
            break;
        }
        case K::PrefabRef:
        case K::SpriteRef:
        case K::TextureRef:
        case K::AudioRef:
        case K::FontRef:
        case K::AnimationRef:
        case K::AudioCueRef: {
            // Guid serialised as 32-char lowercase hex. Empty guids emit "" so
            // diffs stay readable (instead of 32 zero chars).
            Guid g{};
            std::memcpy(&g, p, sizeof(Guid));
            if (g.is_null()) {
                o += "\"\"";
            } else {
                std::string s = guid_to_hex(g);
                o += '"'; o += s; o += '"';
            }
            break;
        }
        case K::CharBuffer: {
            // Up to f.size chars; treat as null-terminated.
            const char* s = reinterpret_cast<const char*>(p);
            j_str(o, s);
            break;
        }
        case K::Enum: {
            int v = 0; std::memcpy(&v, p, f.size);
            emit_int(v);
            break;
        }
        case K::Unknown:
        default:
            o += "null";
            break;
    }
}

// ---- Tolerant single-pass JSON reader (just enough for our schema) -------
// Skips whitespace, supports "string", number, true/false/null, [array],
// {object}. No unicode escapes; control chars in strings are not expected.
struct JR {
    const char* p;
    const char* end;
    bool        ok = true;

    void skip() { while (p < end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) ++p; }
    bool eof()  { skip(); return p >= end; }
    char peek() { skip(); return p < end ? *p : 0; }
    bool match(char c) { skip(); if (p < end && *p==c) { ++p; return true; } return false; }
    void expect(char c) { if (!match(c)) ok = false; }

    std::string read_string() {
        skip();
        std::string out;
        if (p >= end || *p != '"') { ok = false; return out; }
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '\\': out += '\\'; break;
                    case '"': out += '"'; break;
                    default:  out += *p; break;
                }
                ++p;
            } else {
                out += *p++;
            }
        }
        if (p < end && *p == '"') ++p;
        else ok = false;
        return out;
    }
    double read_number() {
        skip();
        char* endp = nullptr;
        const double v = std::strtod(p, &endp);
        if (endp == p) { ok = false; return 0; }
        p = endp;
        return v;
    }
    bool read_bool() {
        skip();
        if (p + 4 <= end && std::memcmp(p, "true", 4) == 0)  { p += 4; return true; }
        if (p + 5 <= end && std::memcmp(p, "false", 5) == 0) { p += 5; return false; }
        ok = false;
        return false;
    }
    void skip_value() {
        skip();
        if (p >= end) { ok = false; return; }
        if (*p == '"') { (void)read_string(); return; }
        if (*p == '[') {
            ++p;
            while (peek() != ']' && ok) skip_value();
            if (!match(']')) ok = false;
            return;
        }
        if (*p == '{') {
            ++p;
            while (peek() != '}' && ok) {
                (void)read_string();
                expect(':');
                skip_value();
            }
            if (!match('}')) ok = false;
            return;
        }
        // number / true / false / null
        if ((*p>='0'&&*p<='9')||*p=='-'||*p=='+') { (void)read_number(); return; }
        if (*p=='t'||*p=='f') { (void)read_bool(); return; }
        if (p + 4 <= end && std::memcmp(p,"null",4)==0) { p += 4; return; }
        ok = false;
    }
};

// Read one field's bytes from `r` into `dst+f.offset`. Returns false on
// shape mismatch (caller may keep going - the bytes for that field stay
// zero-initialised so the entity load doesn't fail outright).
bool read_field(JR& r, const ecs::FieldInfo& f, u8* dst_base) {
    using K = ecs::FieldKind;
    u8* p = dst_base + f.offset;

    // Schema-tolerant: when the JSON value's shape doesn't match what
    // the new schema expects (e.g. field type changed from `int` to
    // `Vec2`, or `Vec3` -> `int`), we don't want a single field to
    // wreck the whole load. Save the cursor and `r.ok` BEFORE attempting
    // the parse; on any failure rewind + skip_value() so the cursor
    // lands cleanly on the next field, leaving `bytes` zero-initialised
    // for the mismatched slot. This is what lets the user freely change
    // a field's type between reloads without losing the entity.
    const char* save_p   = r.p;
    const bool  save_ok  = r.ok;
    auto recover = [&]() {
        r.p  = save_p;
        r.ok = save_ok;
        r.skip_value();   // consume the value cleanly
        // Reported as success: we didn't store anything (bytes stay 0)
        // but the cursor advanced exactly one JSON value, which is the
        // contract the caller relies on.
        return true;
    };

    // Schema-tolerant vector reader (used for Vec2/Vec3/Vec4/Color).
    // Reads JSON until `]`, writes only the first `n` floats, silently
    // drops extras, leaves missing slots at zero. Returns true even if
    // the JSON had a different element count - that's a survivable
    // change and we'd rather keep the entity than fail the whole load.
    auto rd_vec = [&](int n) {
        if (!r.match('[')) return false;
        int i = 0;
        while (r.peek() != ']' && r.ok) {
            const float v = (float)r.read_number();
            if (i < n) {
                std::memcpy(p + i*sizeof(float), &v, sizeof(float));
            }
            ++i;
        }
        if (!r.match(']')) return false;
        return r.ok;
    };

    switch (f.kind) {
        case K::Bool:   { const bool v = r.read_bool(); *p = v ? 1 : 0;
                          if (!r.ok) return recover(); return true; }
        case K::I8:     { i8  v = (i8)  r.read_number();  *p = (u8)v;
                          if (!r.ok) return recover(); return true; }
        case K::I16:    { i16 v = (i16) r.read_number();  std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::I32:    { i32 v = (i32) r.read_number();  std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::I64:    { i64 v = (i64) r.read_number();  std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::U8:     { u8  v = (u8)  r.read_number();  *p = v;
                          if (!r.ok) return recover(); return true; }
        case K::U16:    { u16 v = (u16) r.read_number();  std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::U32:    { u32 v = (u32) r.read_number();  std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::U64:    { u64 v = (u64) r.read_number();  std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::F32:    { float  v = (float) r.read_number(); std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::F64:    { double v =          r.read_number(); std::memcpy(p,&v,sizeof(v));
                          if (!r.ok) return recover(); return true; }
        case K::Vec2:   if (!rd_vec(2)) return recover(); return true;
        case K::Vec3:   if (!rd_vec(3)) return recover(); return true;
        case K::Vec4:   if (!rd_vec(4)) return recover(); return true;
        case K::Color:  if (!rd_vec(4)) return recover(); return true;
        case K::Entity:
        case K::EntityRef: {
            if (!r.match('[')) return recover();
            const u32 idx = (u32) r.read_number();
            const u32 gen = (u32) r.read_number();
            std::memcpy(p,                 &idx, sizeof(u32));
            std::memcpy(p + sizeof(u32),   &gen, sizeof(u32));
            if (!r.match(']') || !r.ok) return recover();
            return true;
        }
        case K::Handle: {
            // Wire format: [idx, gen] (legacy) or [idx, gen, "guid_hex"]
            // (new). When the guid is present we ignore the saved idx
            // -- those are just hints, often stale across runs -- and
            // ask the registry for the live handle. Falls back to the
            // (idx, gen) pair when no guid is on disk.
            if (!r.match('[')) return recover();
            const u32 saved_idx = (u32) r.read_number();
            const u32 saved_gen = (u32) r.read_number();
            u32 final_idx = saved_idx;
            u32 final_gen = saved_gen;
            // The wire format is `[idx, gen]` (legacy) or
            // `[idx, gen, "guid_hex"]` (new). JR::skip() consumes
            // commas in addition to whitespace, so peek() never
            // returns `,` -- check for the start of a string (`"`)
            // OR for any non-`]` value, which is what tells us the
            // optional third element is present.
            if (r.peek() == '"') {
                std::string hex = r.read_string();
                if (r.ok && !hex.empty()) {
                    Guid g = guid_from_hex(hex);
                    if (!g.is_null()) {
                        // Try Texture first (Sprite::texture is the
                        // dominant Handle field), then Font.
                        u32 h = AssetRegistry::instance()
                            .runtime_handle_for_guid(AssetKind::Texture, g);
                        if (h == 0) {
                            h = AssetRegistry::instance()
                                .runtime_handle_for_guid(AssetKind::Font, g);
                        }
                        if (h != 0) { final_idx = h; final_gen = 1; }
                        // h == 0 means the asset isn't loaded yet --
                        // keep the saved (idx, gen) and let the editor
                        // fix it after the renderer loads the texture
                        // (sprite_registry sync path).
                    }
                }
            }
            std::memcpy(p,                 &final_idx, sizeof(u32));
            std::memcpy(p + sizeof(u32),   &final_gen, sizeof(u32));
            if (!r.match(']') || !r.ok) return recover();
            return true;
        }
        case K::PrefabRef:
        case K::SpriteRef:
        case K::TextureRef:
        case K::AudioRef:
        case K::FontRef:
        case K::AnimationRef:
        case K::AudioCueRef: {
            std::string s = r.read_string();
            if (!r.ok) return recover();
            Guid g = s.empty() ? NULL_GUID : guid_from_hex(s);
            std::memcpy(p, &g, sizeof(Guid));
            return true;
        }
        case K::CharBuffer: {
            std::string s = r.read_string();
            if (!r.ok) return recover();
            const std::size_t n = std::min<std::size_t>(s.size(), f.size - 1);
            std::memcpy(p, s.data(), n);
            p[n] = 0;
            return true;
        }
        case K::Enum: {
            const i32 v = (i32) r.read_number();
            if (!r.ok) return recover();
            std::memcpy(p, &v, f.size);
            return true;
        }
        case K::Unknown:
        default:
            r.skip_value();
            return false;
    }
}

}  // namespace

std::string World::save_json() const {
    std::string o;
    o.reserve(8192);
    o += "{\n";
    j_kv_open(o, "version");          o += std::to_string(ZUES_JSON_VERSION);  o += ",\n";
    j_kv_open(o, "next_name_index");  o += std::to_string(m_impl->next_name_index); o += ",\n";
    // Explicit root ordering — preserved across save/load so the editor's
    // hierarchy tree shows roots in the user's chosen order, not slot
    // allocation order.
    j_kv_open(o, "root_order");       o += "[";
    {
        bool first_r = true;
        for (u32 idx : m_impl->roots_order) {
            if (idx >= m_impl->slots.size()) continue;
            const auto& slot = m_impl->slots[idx];
            if (!slot.alive) continue;
            if (!first_r) o += ",";
            first_r = false;
            o += std::to_string(idx);
        }
    }
    o += "],\n";
    j_kv_open(o, "entities");         o += "[\n";

    bool first_entity = true;
    for (u32 i = 0; i < m_impl->slots.size(); ++i) {
        const auto& slot = m_impl->slots[i];
        if (!slot.alive || !slot.archetype) continue;
        Archetype* a = slot.archetype;
        if (!first_entity) o += ",\n";
        first_entity = false;

        o += "  {";
        j_kv_open(o, "index");      o += std::to_string(i);                o += ", ";
        j_kv_open(o, "generation"); o += std::to_string(slot.generation);  o += ", ";
        j_kv_open(o, "components"); o += "[\n";

        bool first_c = true;
        for (size_t ci = 0; ci < a->component_ids.size(); ++ci) {
            const ComponentId cid = a->component_ids[ci];
            if (cid == INVALID_COMPONENT_ID || cid > m_impl->types.size()) continue;
            const auto& type = m_impl->types[cid - 1];
            const u8* row = a->column_bytes[ci].data() + slot.row * type.size;

            if (!first_c) o += ",\n";
            first_c = false;
            o += "    {";
            j_kv_open(o, "type"); j_str(o, type.name ? type.name : ""); o += ", ";
            j_kv_open(o, "data"); o += "{";
            for (u32 fi = 0; fi < type.field_count; ++fi) {
                if (fi) o += ", ";
                j_kv_open(o, type.fields[fi].name);
                emit_field_value(o, type.fields[fi], row);
            }
            o += "}}";
        }
        // Append unknown components round-tripped from a previous
        // load. Their `data` is the raw JSON text we captured at load
        // time, written back unmodified so a save+load with the type
        // still unregistered is byte-stable. Once the type registers
        // (project DLL hot-reload), adopt_unknown_components turns
        // these into real component rows on the next save.
        if (auto it = m_impl->unknowns.find(i); it != m_impl->unknowns.end()) {
            for (const auto& blob : it->second) {
                if (!first_c) o += ",\n";
                first_c = false;
                o += "    {";
                j_kv_open(o, "type"); j_str(o, blob.type_name.c_str());
                o += ", ";
                j_kv_open(o, "data"); o += blob.data_json;
                o += "}";
            }
        }
        o += "\n  ]}";
    }
    o += "\n]\n}\n";
    return o;
}

Result World::load_json(const char* text, std::size_t size) {
    if (!text || size == 0) return Result::InvalidArgument;
    JR r{text, text + size};
    clear();

    if (!r.match('{')) return Result::Error;
    u32 next_name = 0;

    // saved_idx -> created_idx remap. Saved entities don't keep their
    // numeric indices (create_entity allocates fresh ones); the map lets
    // us translate the saved root_order array into live indices for the
    // freshly-built world.
    std::unordered_map<u32, u32> idx_remap;
    std::vector<u32>             saved_root_order;
    bool                         have_root_order = false;

    // Track every component type the snapshot mentions that we couldn't
    // resolve (find_component_id == INVALID). Without this audit a
    // load looks "successful" but actually drops every instance of an
    // un-registered type silently -- e.g. the user's RigidBody-like data
    // disappears because the project DLL hasn't re-registered the type
    // yet. We accumulate (name -> dropped_count) and log a single Warn
    // at the end.
    std::unordered_map<std::string, u32> dropped_by_type;

    // Parse top-level keys until the matching '}'.
    while (r.peek() != '}' && r.ok) {
        const std::string key = r.read_string();
        r.expect(':');
        if (key == "version") {
            (void)r.read_number();   // accept any; bump strictness later
        } else if (key == "next_name_index") {
            next_name = (u32) r.read_number();
        } else if (key == "root_order") {
            if (!r.match('[')) return Result::Error;
            while (r.peek() != ']' && r.ok) {
                saved_root_order.push_back((u32) r.read_number());
            }
            if (!r.match(']')) return Result::Error;
            have_root_order = true;
        } else if (key == "entities") {
            if (!r.match('[')) return Result::Error;
            while (r.peek() != ']' && r.ok) {
                if (!r.match('{')) return Result::Error;
                u32 idx = 0, gen = 1;
                std::vector<std::pair<ComponentId, std::vector<u8>>> comps;
                // Components whose `type` didn't resolve (project DLL not
                // loaded yet, type renamed, etc.). Stashed by type-name
                // + raw data JSON so the next save round-trips them and
                // adopt_unknown_components can re-materialise them once
                // the type registers.
                std::vector<Impl::UnknownComponentBlob> pending_unknowns;
                while (r.peek() != '}' && r.ok) {
                    const std::string k = r.read_string();
                    r.expect(':');
                    if (k == "index")           { idx = (u32) r.read_number(); }
                    else if (k == "generation") { gen = (u32) r.read_number(); }
                    else if (k == "components") {
                        if (!r.match('[')) return Result::Error;
                        while (r.peek() != ']' && r.ok) {
                            if (!r.match('{')) return Result::Error;
                            std::string type_name;
                            ComponentId cid = INVALID_COMPONENT_ID;
                            std::vector<u8> bytes;
                            // For unknown component types we preserve the
                            // raw `data` text instead of dropping it. These
                            // two cursors bracket the data value when it
                            // appears AFTER an unresolved type (we capture
                            // immediately during the data branch). For the
                            // "data before type" case we use the same
                            // deferred_data_* pair below.
                            const char* unknown_data_start = nullptr;
                            const char* unknown_data_end   = nullptr;
                            // The "data" object's start cursor when we
                            // encounter `data` BEFORE `type`. Some JSON
                            // emitters (notably nlohmann/json with sorted
                            // keys) write `{"data": {...}, "type": "..."}`,
                            // and we need to defer field parsing until
                            // `type` is known. Stash the cursor + skip the
                            // object on first pass; replay it once cid is
                            // resolved.
                            const char* deferred_data_start = nullptr;
                            const char* deferred_data_end   = nullptr;
                            while (r.peek() != '}' && r.ok) {
                                const std::string ck = r.read_string();
                                r.expect(':');
                                if (ck == "type") {
                                    type_name = r.read_string();
                                    cid = find_component_id(type_name.c_str());
                                    if (cid != INVALID_COMPONENT_ID) {
                                        bytes.resize(m_impl->types[cid - 1].size, 0);
                                    }
                                    // Unresolved type names fall through:
                                    // when we see `data` next we'll
                                    // capture the raw JSON text into
                                    // unknown_data_start/_end and stash
                                    // the blob on the entity below.
                                } else if (ck == "data") {
                                    if (cid == INVALID_COMPONENT_ID) {
                                        // Type not seen yet OR type
                                        // unknown -- either way we don't
                                        // know what to do with the bytes
                                        // until `type` resolves. Record
                                        // the slice + skip; we'll either
                                        // parse it (deferred path) or
                                        // stash it (unknown path) below.
                                        deferred_data_start = r.p;
                                        r.skip_value();
                                        deferred_data_end   = r.p;
                                        continue;
                                    }
                                    if (!r.match('{')) { r.ok = false; break; }
                                    while (r.peek() != '}' && r.ok) {
                                        const std::string fk = r.read_string();
                                        r.expect(':');
                                        if (cid == INVALID_COMPONENT_ID) { r.skip_value(); continue; }
                                        const auto& tdesc = m_impl->types[cid - 1];
                                        const ecs::FieldInfo* match = nullptr;
                                        for (u32 fi = 0; fi < tdesc.field_count; ++fi)
                                            if (fk == tdesc.fields[fi].name) { match = &tdesc.fields[fi]; break; }
                                        if (match) (void)read_field(r, *match, bytes.data());
                                        else        r.skip_value();
                                    }
                                    if (!r.match('}')) return Result::Error;
                                } else {
                                    r.skip_value();
                                }
                            }
                            if (!r.match('}')) return Result::Error;
                            // Replay the deferred data object now that we
                            // know which component this is. Save + restore
                            // the cursor so the outer entity-record parser
                            // continues from the right spot.
                            if (deferred_data_start &&
                                cid != INVALID_COMPONENT_ID && !bytes.empty()) {
                                const char* save_p  = r.p;
                                const bool  save_ok = r.ok;
                                r.p  = deferred_data_start;
                                r.ok = true;
                                if (r.match('{')) {
                                    while (r.peek() != '}' && r.ok) {
                                        const std::string fk = r.read_string();
                                        r.expect(':');
                                        const auto& tdesc = m_impl->types[cid - 1];
                                        const ecs::FieldInfo* match = nullptr;
                                        for (u32 fi = 0; fi < tdesc.field_count; ++fi)
                                            if (fk == tdesc.fields[fi].name) { match = &tdesc.fields[fi]; break; }
                                        if (match) (void)read_field(r, *match, bytes.data());
                                        else        r.skip_value();
                                    }
                                    (void)r.match('}');
                                }
                                r.p  = save_p;
                                r.ok = save_ok;
                            }
                            if (cid != INVALID_COMPONENT_ID && !bytes.empty())
                                comps.emplace_back(cid, std::move(bytes));
                            else if (cid == INVALID_COMPONENT_ID &&
                                     !type_name.empty() &&
                                     deferred_data_start && deferred_data_end &&
                                     deferred_data_end > deferred_data_start) {
                                Impl::UnknownComponentBlob blob;
                                blob.type_name = type_name;
                                blob.data_json.assign(
                                    deferred_data_start,
                                    static_cast<size_t>(deferred_data_end - deferred_data_start));
                                pending_unknowns.push_back(std::move(blob));
                            }
                        }
                        if (!r.match(']')) return Result::Error;
                    } else {
                        r.skip_value();
                    }
                }
                if (!r.match('}')) return Result::Error;

                // Materialise this entity into the world. Slots / generations
                // don't need to be preserved exactly across load; we use
                // create_entity() which gives us a fresh index. Record the
                // saved → new mapping so root_order can be translated below.
                if (!comps.empty() || !pending_unknowns.empty()) {
                    const Entity e = create_entity();
                    idx_remap[idx] = e.index;
                    (void)gen;
                    for (auto& [cid, bytes] : comps) {
                        add_component(e, cid, bytes.data());
                    }
                    if (!pending_unknowns.empty()) {
                        m_impl->unknowns[e.index] = std::move(pending_unknowns);
                    }
                }
            }
            if (!r.match(']')) return Result::Error;
        } else {
            r.skip_value();
        }
    }
    if (!r.match('}')) return Result::Error;

    if (next_name > m_impl->next_name_index) m_impl->next_name_index = next_name;

    // Audit: anything we silently dropped because the type wasn't
    // registered. This is the common cause of "my component data
    // disappeared after rebuild" -- we surface it loudly so the user
    // can act on it instead of discovering missing fields in the
    // inspector. One Warn per type, with the dropped instance count.
    if (!dropped_by_type.empty()) {
        for (const auto& [name, n] : dropped_by_type) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "world.load: component type \"%s\" not registered -- "
                "dropped %u instance%s. (Project DLL ABI mismatch? Type "
                "renamed?)",
                name.c_str(), n, n == 1 ? "" : "s");
            log_write(LogLevel::Warn, "world", buf);
        }
    }

    // ---- Entity/EntityRef remap pass --------------------------------------
    // The component bytes we wrote via add_component above contain raw
    // (saved_idx, saved_gen) pairs for every Entity/EntityRef field --
    // including the engine's own Parent / FirstChild / NextSibling. Saved
    // indices don't match the freshly-allocated live indices, so without
    // this pass children are orphaned (Parent.value.idx points to a slot
    // that may belong to some other entity, or to nothing alive). Walk
    // every live entity, find every Entity/EntityRef field, and rewrite
    // it through idx_remap → live slot index + current generation.
    //
    // Refs that don't resolve (saved entity wasn't part of this load --
    // e.g. cross-world references) are zeroed to NULL_ENTITY rather than
    // left dangling.
    for (u32 i = 0; i < (u32)m_impl->slots.size(); ++i) {
        auto& slot = m_impl->slots[i];
        if (!slot.alive || !slot.archetype) continue;
        for (size_t ci = 0; ci < slot.archetype->component_ids.size(); ++ci) {
            const ComponentId cid = slot.archetype->component_ids[ci];
            if (cid == INVALID_COMPONENT_ID || cid > m_impl->types.size()) continue;
            const auto& type = m_impl->types[cid - 1];
            u8* row = slot.archetype->column_at((int)ci, slot.row);
            for (u32 fi = 0; fi < type.field_count; ++fi) {
                const auto& f = type.fields[fi];
                if (f.kind != ecs::FieldKind::Entity &&
                    f.kind != ecs::FieldKind::EntityRef) continue;
                u32* ip = reinterpret_cast<u32*>(row + f.offset);
                const u32 saved_idx = ip[0];
                const u32 saved_gen = ip[1];
                if (saved_idx == 0 && saved_gen == 0) continue;  // null ref
                auto it = idx_remap.find(saved_idx);
                if (it == idx_remap.end()) {
                    // Reference points outside this load -- zero it.
                    ip[0] = 0; ip[1] = 0;
                    continue;
                }
                const u32 live_idx = it->second;
                if (live_idx >= m_impl->slots.size()) {
                    ip[0] = 0; ip[1] = 0;
                    continue;
                }
                ip[0] = live_idx;
                ip[1] = m_impl->slots[live_idx].generation;
            }
        }
    }

    // Rebuild roots_order from scratch. create_entity blindly appended new
    // indices during the entity loop, but a freshly-loaded entity may have
    // a saved Parent component (set via add_component, which writes raw
    // bytes — it doesn't run set_parent's bookkeeping). The "real" root
    // set is therefore "alive entities with no Parent component" — and
    // their order should match the saved root_order if one was present.
    m_impl->roots_order.clear();
    if (have_root_order) {
        for (u32 saved_idx : saved_root_order) {
            auto it = idx_remap.find(saved_idx);
            if (it == idx_remap.end()) continue;
            const u32 new_idx = it->second;
            if (new_idx >= m_impl->slots.size()) continue;
            const auto& slot = m_impl->slots[new_idx];
            if (!slot.alive) continue;
            // Only include if it's actually a root in the loaded data.
            if (m_impl->parent_id != INVALID_COMPONENT_ID &&
                slot.archetype && slot.archetype->find_column(m_impl->parent_id) >= 0)
                continue;
            m_impl->roots_order.push_back(new_idx);
        }
    }
    // Append any leftover roots not mentioned in saved_root_order (e.g.
    // legacy worlds saved before this field existed, or entities the
    // editor created post-load without going through create_entity's
    // append). Walk slots in index order so the fallback is deterministic.
    for (u32 i = 0; i < m_impl->slots.size(); ++i) {
        const auto& slot = m_impl->slots[i];
        if (!slot.alive || !slot.archetype) continue;
        if (m_impl->parent_id != INVALID_COMPONENT_ID &&
            slot.archetype->find_column(m_impl->parent_id) >= 0) continue;
        if (std::find(m_impl->roots_order.begin(),
                      m_impl->roots_order.end(), i) != m_impl->roots_order.end())
            continue;
        m_impl->roots_order.push_back(i);
    }

    return r.ok ? Result::Ok : Result::Error;
}

// ---- Subtree (prefab) serialisation ----------------------------------------
//
// Entity / EntityRef fields are remapped to dense indices [0..N) on emit so
// the file is self-contained. References that point OUT of the subtree
// collapse to NULL — they wouldn't survive the trip anyway, since the target
// entity doesn't exist in the destination world.

namespace {

// DFS the hierarchy starting at `root` and collect entities in pre-order.
// Returns a map src_entity -> dense_idx.
struct SubtreeCollect {
    std::vector<Entity>                              ordered;
    std::unordered_map<Entity, u32, ecs::EntityHash> dense;
};

void collect_subtree(const ecs::World& w, ecs::Entity e, SubtreeCollect& out) {
    if (e.is_null()) return;
    if (out.dense.count(e)) return;   // shouldn't happen, defensive
    out.dense[e] = static_cast<u32>(out.ordered.size());
    out.ordered.push_back(e);
    w.iterate_children(e, [&](ecs::Entity child) {
        collect_subtree(w, child, out);
    });
}

// Like emit_field_value but with the EntityRef/Entity remap. Asset refs
// flow through unchanged (guids are world-independent).
void emit_field_value_remapped(std::string& o,
                                const ecs::FieldInfo& f,
                                const u8* base,
                                const SubtreeCollect& sub) {
    using K = ecs::FieldKind;
    if (f.kind == K::Entity || f.kind == K::EntityRef) {
        const u32* ip = reinterpret_cast<const u32*>(base + f.offset);
        Entity src{ip[0], ip[1]};
        char buf[64];
        if (src.is_null()) { o += "[0,0]"; return; }
        auto it = sub.dense.find(src);
        if (it == sub.dense.end()) {
            o += "[0,0]";   // out of subtree
        } else {
            std::snprintf(buf, sizeof(buf), "[%u,1]", it->second);
            o += buf;
        }
        return;
    }
    emit_field_value(o, f, base);
}

}  // namespace

std::string World::save_entity_subtree_json(Entity root) const {
    if (!is_alive(root)) return "{}";

    SubtreeCollect sub;
    collect_subtree(*this, root, sub);

    std::string o;
    o.reserve(2048);
    o += "{\n";
    j_kv_open(o, "version"); o += std::to_string(ZUES_JSON_VERSION); o += ",\n";
    j_kv_open(o, "root");    o += "0,\n";   // dense_idx of root is always 0
    j_kv_open(o, "entities"); o += "[\n";

    bool first_entity = true;
    for (u32 di = 0; di < sub.ordered.size(); ++di) {
        const Entity e   = sub.ordered[di];
        const auto&  s   = m_impl->slots[e.index];
        Archetype*   a   = s.archetype;
        if (!a) continue;

        if (!first_entity) o += ",\n";
        first_entity = false;

        o += "  {";
        j_kv_open(o, "index");      o += std::to_string(di);  o += ", ";
        j_kv_open(o, "components"); o += "[\n";

        bool first_c = true;
        for (size_t ci = 0; ci < a->component_ids.size(); ++ci) {
            const ComponentId cid = a->component_ids[ci];
            if (cid == INVALID_COMPONENT_ID || cid > m_impl->types.size()) continue;
            const auto& type = m_impl->types[cid - 1];
            const u8* row = a->column_bytes[ci].data() + s.row * type.size;

            // Don't emit the root's Parent — the caller decides where to
            // attach the instantiated subtree. Internal Parents (di > 0)
            // must stay so the hierarchy reconstructs.
            if (di == 0 &&
                m_impl->parent_id != INVALID_COMPONENT_ID &&
                cid == m_impl->parent_id) continue;
            // Also drop NextSibling on the root — sibling chain is owned by
            // whatever parent the caller drops us into.
            if (di == 0 &&
                m_impl->next_sibling_id != INVALID_COMPONENT_ID &&
                cid == m_impl->next_sibling_id) continue;

            if (!first_c) o += ",\n";
            first_c = false;
            o += "    {";
            j_kv_open(o, "type"); j_str(o, type.name ? type.name : ""); o += ", ";
            j_kv_open(o, "data"); o += "{";
            for (u32 fi = 0; fi < type.field_count; ++fi) {
                if (fi) o += ", ";
                j_kv_open(o, type.fields[fi].name);
                emit_field_value_remapped(o, type.fields[fi], row, sub);
            }
            o += "}}";
        }
        o += "\n  ]}";
    }
    o += "\n]\n}\n";
    return o;
}

Entity World::instantiate_entity_subtree_json(const char* text, std::size_t size) {
    if (!text || size == 0) return NULL_ENTITY;
    JR r{text, text + size};

    if (!r.match('{')) return NULL_ENTITY;
    u32 root_dense = 0;

    // saved_dense_idx -> created Entity
    std::vector<Entity> by_dense;
    // (entity, component_id, raw_bytes) — applied after all entities exist
    // so remap can happen against the final by_dense table.
    struct Pending { Entity e; ComponentId cid; std::vector<u8> bytes; };
    std::vector<Pending> pending;

    while (r.peek() != '}' && r.ok) {
        const std::string key = r.read_string();
        r.expect(':');
        if (key == "version")        { (void)r.read_number(); }
        else if (key == "root")      { root_dense = (u32) r.read_number(); }
        else if (key == "entities") {
            if (!r.match('[')) return NULL_ENTITY;
            while (r.peek() != ']' && r.ok) {
                if (!r.match('{')) return NULL_ENTITY;
                u32 dense_idx = 0;
                std::vector<std::pair<ComponentId, std::vector<u8>>> comps;
                while (r.peek() != '}' && r.ok) {
                    const std::string k = r.read_string();
                    r.expect(':');
                    if (k == "index") { dense_idx = (u32) r.read_number(); }
                    else if (k == "components") {
                        if (!r.match('[')) return NULL_ENTITY;
                        while (r.peek() != ']' && r.ok) {
                            if (!r.match('{')) return NULL_ENTITY;
                            std::string type_name;
                            ComponentId cid = INVALID_COMPONENT_ID;
                            std::vector<u8> bytes;
                            // Same order-tolerance trick as World::load_json:
                            // nlohmann::json sorts object keys alphabetically
                            // when serialising, so prefab JSON has `"data"`
                            // BEFORE `"type"`. Without this stash-and-replay,
                            // the data parser hits cid=INVALID and skips
                            // every field, producing zero-byte components
                            // that spawn invisible (zero-size sprite, empty
                            // Name, default tint with alpha=0, etc).
                            const char* deferred_data_start = nullptr;
                            while (r.peek() != '}' && r.ok) {
                                const std::string ck = r.read_string();
                                r.expect(':');
                                if (ck == "type") {
                                    type_name = r.read_string();
                                    cid = find_component_id(type_name.c_str());
                                    if (cid != INVALID_COMPONENT_ID)
                                        bytes.resize(m_impl->types[cid-1].size, 0);
                                } else if (ck == "data") {
                                    if (cid == INVALID_COMPONENT_ID) {
                                        // Type not seen yet — record the
                                        // cursor and skip the data object;
                                        // we'll come back once we know cid.
                                        deferred_data_start = r.p;
                                        r.skip_value();
                                        continue;
                                    }
                                    if (!r.match('{')) { r.ok = false; break; }
                                    while (r.peek() != '}' && r.ok) {
                                        const std::string fk = r.read_string();
                                        r.expect(':');
                                        const auto& tdesc = m_impl->types[cid-1];
                                        const ecs::FieldInfo* match = nullptr;
                                        for (u32 fi = 0; fi < tdesc.field_count; ++fi)
                                            if (fk == tdesc.fields[fi].name) { match = &tdesc.fields[fi]; break; }
                                        if (match) (void)read_field(r, *match, bytes.data());
                                        else        r.skip_value();
                                    }
                                    if (!r.match('}')) return NULL_ENTITY;
                                } else {
                                    r.skip_value();
                                }
                            }
                            if (!r.match('}')) return NULL_ENTITY;
                            // Replay the deferred data block now that cid
                            // is known. Same parser, restored cursor.
                            if (deferred_data_start && cid != INVALID_COMPONENT_ID) {
                                JR r2{deferred_data_start, r.end};
                                if (r2.match('{')) {
                                    while (r2.peek() != '}' && r2.ok) {
                                        const std::string fk = r2.read_string();
                                        r2.expect(':');
                                        const auto& tdesc = m_impl->types[cid-1];
                                        const ecs::FieldInfo* match = nullptr;
                                        for (u32 fi = 0; fi < tdesc.field_count; ++fi)
                                            if (fk == tdesc.fields[fi].name) { match = &tdesc.fields[fi]; break; }
                                        if (match) (void)read_field(r2, *match, bytes.data());
                                        else        r2.skip_value();
                                    }
                                }
                            }
                            if (cid != INVALID_COMPONENT_ID && !bytes.empty())
                                comps.emplace_back(cid, std::move(bytes));
                        }
                        if (!r.match(']')) return NULL_ENTITY;
                    } else {
                        r.skip_value();
                    }
                }
                if (!r.match('}')) return NULL_ENTITY;

                // Materialise a fresh entity at this dense slot.
                if (by_dense.size() <= dense_idx) by_dense.resize(dense_idx + 1, NULL_ENTITY);
                const Entity e = create_entity();
                by_dense[dense_idx] = e;
                for (auto& [cid, bytes] : comps)
                    pending.push_back({e, cid, std::move(bytes)});
            }
            if (!r.match(']')) return NULL_ENTITY;
        } else {
            r.skip_value();
        }
    }
    if (!r.match('}')) return NULL_ENTITY;
    if (!r.ok)         return NULL_ENTITY;

    // Apply components, remapping any Entity/EntityRef fields against the
    // freshly-created table. Out-of-subtree refs (which were saved as
    // [0,0]) stay null. The saved generation slot was always 1 — purely a
    // sentinel to distinguish "real ref to dense 0" from "null".
    auto remap_entity_fields = [&](u8* bytes, const ComponentType& type) {
        for (u32 fi = 0; fi < type.field_count; ++fi) {
            const auto& f = type.fields[fi];
            if (f.kind != ecs::FieldKind::Entity &&
                f.kind != ecs::FieldKind::EntityRef) continue;
            u32* ip = reinterpret_cast<u32*>(bytes + f.offset);
            const u32 idx = ip[0];
            const u32 gen = ip[1];
            if (gen == 0) { ip[0] = 0; ip[1] = 0; continue; }
            if (idx >= by_dense.size() || by_dense[idx].is_null()) {
                ip[0] = 0; ip[1] = 0;
                continue;
            }
            const Entity tgt = by_dense[idx];
            ip[0] = tgt.index;
            ip[1] = tgt.generation;
        }
    };

    for (auto& p : pending) {
        if (p.cid == INVALID_COMPONENT_ID || p.cid > m_impl->types.size()) continue;
        const auto& type = m_impl->types[p.cid - 1];
        remap_entity_fields(p.bytes.data(), type);
        add_component(p.e, p.cid, p.bytes.data());
    }

    // Children of the instantiated subtree got pushed onto roots_order by
    // create_entity() during the materialise loop above (every fresh
    // entity is provisionally a root). Now that Parent components have
    // been written, scrub any entity that ended up parented out of
    // roots_order so the Hierarchy panel doesn't show them BOTH as
    // children of their parent AND as top-level roots.
    if (m_impl->parent_id != INVALID_COMPONENT_ID) {
        for (Entity ent : by_dense) {
            if (ent.is_null() || ent.index >= m_impl->slots.size()) continue;
            const auto& slot = m_impl->slots[ent.index];
            if (!slot.alive || !slot.archetype) continue;
            if (slot.archetype->find_column(m_impl->parent_id) >= 0) {
                auto& roots = m_impl->roots_order;
                auto it = std::find(roots.begin(), roots.end(), ent.index);
                if (it != roots.end()) roots.erase(it);
            }
        }
    }

    if (root_dense >= by_dense.size()) return NULL_ENTITY;
    return by_dense[root_dense];
}

// ---- Built-in components ----------------------------------------------------

// ---- Unknown component preservation -------------------------------------

void World::iterate_unknown_components(Entity e,
                                         UnknownVisitor fn, void* user) const {
    if (!fn) return;
    if (!is_alive(e)) return;
    auto it = m_impl->unknowns.find(e.index);
    if (it == m_impl->unknowns.end()) return;
    for (const auto& b : it->second) {
        fn(e, b.type_name.c_str(), b.data_json.c_str(), user);
    }
}

void World::remove_unknown_component(Entity e, const char* type_name) {
    if (!type_name) return;
    if (!is_alive(e)) return;
    auto it = m_impl->unknowns.find(e.index);
    if (it == m_impl->unknowns.end()) return;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
        [&](const Impl::UnknownComponentBlob& b) {
            return b.type_name == type_name;
        }), v.end());
    if (v.empty()) m_impl->unknowns.erase(it);
}

u32 World::adopt_unknown_components(const char* type_name) {
    if (!type_name) return 0;
    const ComponentId cid = find_component_id(type_name);
    if (cid == INVALID_COMPONENT_ID) return 0;

    const auto& tdesc = m_impl->types[cid - 1];
    u32 adopted = 0;

    // Walk a snapshot of the keys -- the inner add_component / erase
    // mutates m_impl->unknowns, which would invalidate the outer iterator.
    std::vector<u32> entity_indices;
    entity_indices.reserve(m_impl->unknowns.size());
    for (auto& kv : m_impl->unknowns) entity_indices.push_back(kv.first);

    for (u32 idx : entity_indices) {
        auto it = m_impl->unknowns.find(idx);
        if (it == m_impl->unknowns.end()) continue;
        if (idx >= m_impl->slots.size()) { m_impl->unknowns.erase(it); continue; }
        const auto& slot = m_impl->slots[idx];
        if (!slot.alive) { m_impl->unknowns.erase(it); continue; }
        const Entity e{ idx, slot.generation };

        auto& blobs = it->second;
        for (auto bi = blobs.begin(); bi != blobs.end(); ) {
            if (bi->type_name != type_name) { ++bi; continue; }

            // Parse the stashed JSON `data` text into bytes via the same
            // path load_json uses, so schema-tolerant field walking +
            // type-mismatch fallback all behave identically.
            std::vector<u8> bytes(tdesc.size, 0);
            JR r{ bi->data_json.data(),
                   bi->data_json.data() + bi->data_json.size() };
            if (r.match('{')) {
                while (r.peek() != '}' && r.ok) {
                    const std::string fk = r.read_string();
                    r.expect(':');
                    const ecs::FieldInfo* match = nullptr;
                    for (u32 fi = 0; fi < tdesc.field_count; ++fi)
                        if (fk == tdesc.fields[fi].name) {
                            match = &tdesc.fields[fi]; break;
                        }
                    if (match) (void)read_field(r, *match, bytes.data());
                    else        r.skip_value();
                }
                (void)r.match('}');
            }

            add_component(e, cid, bytes.data());
            bi = blobs.erase(bi);
            ++adopted;
        }
        if (blobs.empty()) m_impl->unknowns.erase(it);
    }
    return adopted;
}

void World::register_builtins() {
    using namespace Engine::components;

    const ComponentId xform_id  = register_component<Transform2D>("Transform2D");
    const ComponentId parent_id = register_component<Parent>     ("Parent");
    const ComponentId fc_id     = register_component<FirstChild> ("FirstChild");
    const ComponentId ns_id     = register_component<NextSibling>("NextSibling");
    const ComponentId name_id   = register_component<Name>       ("Name");
    const ComponentId cam_id    = register_component<Camera2D>   ("Camera2D");
    const ComponentId sprite_id = register_component<Sprite>     ("Sprite");
    const ComponentId text_id   = register_component<Text>       ("Text");
    const ComponentId rb_id     = register_component<RigidBody>     ("RigidBody");
    const ComponentId boxc_id   = register_component<BoxCollider>   ("BoxCollider");
    const ComponentId circc_id  = register_component<CircleCollider>("CircleCollider");
    const ComponentId uia_id    = register_component<UIAnchor>      ("UIAnchor");
    const ComponentId anim_id   = register_component<Animator>      ("Animator");
    const ComponentId parts_id  = register_component<Particles>     ("Particles");
    const ComponentId asrc_id   = register_component<AudioSource>   ("AudioSource");
    const ComponentId alis_id   = register_component<AudioListener> ("AudioListener");

    // Categories drive the editor's Add Component picker only. Names stay
    // plain (e.g. the Inspector header still says "Sprite", not the path).
    set_component_category(xform_id,  "Engine/Core");
    set_component_category(parent_id, "Engine/Hierarchy");
    set_component_category(fc_id,     "Engine/Hierarchy");
    set_component_category(ns_id,     "Engine/Hierarchy");
    set_component_category(name_id,   "Engine/Core");
    set_component_category(cam_id,    "Engine/Render");
    set_component_category(sprite_id, "Engine/Render");
    set_component_category(text_id,   "Engine/Render");
    set_component_category(rb_id,     "Engine/Physics");
    set_component_category(boxc_id,   "Engine/Physics");
    set_component_category(circc_id,  "Engine/Physics");
    set_component_category(uia_id,    "Engine/UI");
    set_component_category(anim_id,   "Engine/Render");
    set_component_category(parts_id,  "Engine/Render");
    set_component_category(asrc_id,   "Engine/Audio");
    set_component_category(alis_id,   "Engine/Audio");

    m_impl->parent_id       = find_component_id("Parent");
    m_impl->first_child_id  = find_component_id("FirstChild");
    m_impl->next_sibling_id = find_component_id("NextSibling");
    m_impl->transform2d_id  = find_component_id("Transform2D");
    m_impl->name_id         = find_component_id("Name");

    // Capture how many types the engine itself registered. Project DLLs
    // append; unload_project_types() truncates back to this watermark on
    // hot-reload so stale project components don't accumulate.
    m_impl->builtin_type_count = static_cast<u32>(m_impl->types.size());
}

void World::unload_project_types() {
    if (m_impl->types.size() <= m_impl->builtin_type_count) return;
    // Forget any name -> id entry above the watermark. Iterate by id since
    // by_name maps name -> id; collect names to erase first.
    std::vector<std::string> drop;
    for (const auto& [n, id] : m_impl->by_name) {
        if (id > m_impl->builtin_type_count) drop.push_back(n);
    }
    for (auto& n : drop) m_impl->by_name.erase(n);
    // Same for by_typeid (used by C++ register_component<T>).
    std::vector<std::string> drop_t;
    for (const auto& [n, id] : m_impl->by_typeid) {
        if (id > m_impl->builtin_type_count) drop_t.push_back(n);
    }
    for (auto& n : drop_t) m_impl->by_typeid.erase(n);
    m_impl->types.resize(m_impl->builtin_type_count);
}

// ---- Hierarchy helpers ------------------------------------------------------

Entity World::parent_of(Entity e) const {
    if (m_impl->parent_id == INVALID_COMPONENT_ID) return NULL_ENTITY;
    auto* p = static_cast<const Engine::components::Parent*>(
        get_component(e, m_impl->parent_id));
    return p ? p->value : NULL_ENTITY;
}

World::WorldTransform2D World::world_transform_2d(Entity e) const {
    // Walk the chain root -> e, composing TRS into accumulating world.
    // Building chain in reverse (e -> root) then iterating root -> e keeps
    // the multiply order parent-first without recursion. Depth is small
    // in practice; a stack-allocated 16-slot scratch covers nearly every
    // realistic hierarchy without heap traffic.
    //
    // Hard depth cap of 64 protects against pathological inputs where a
    // Parent component points back at an ancestor (cycle). set_parent
    // refuses cycles, but a corrupt save file or a hot-reload that
    // remapped Entity refs incorrectly could in theory produce one --
    // and a cycle would otherwise spin this loop forever, freezing or
    // OOMing the editor. Caps the walk at a depth that's already 4x
    // anything real-world hierarchies use.
    constexpr int kMaxDepth = 64;
    constexpr int kStack    = 16;
    Entity stack[kStack];
    int n = 0;
    Entity cur = e;
    while (!cur.is_null() && n < kStack) {
        stack[n++] = cur;
        cur = parent_of(cur);
    }
    std::vector<Entity> overflow;
    if (n >= kStack && !cur.is_null()) {
        for (int i = 0; i < kStack; ++i) overflow.push_back(stack[i]);
        int safety = 0;
        while (!cur.is_null() && safety++ < kMaxDepth) {
            overflow.push_back(cur);
            cur = parent_of(cur);
        }
    }

    WorldTransform2D w{0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    auto compose_one = [&](Entity ent) {
        if (m_impl->transform2d_id == INVALID_COMPONENT_ID) return;
        auto* t = static_cast<const Engine::components::Transform2D*>(
            get_component(ent, m_impl->transform2d_id));
        if (!t) return;   // identity step
        // Local transform applies in parent (current world) frame.
        const float c = std::cos(w.rot);
        const float s = std::sin(w.rot);
        const float lx = t->position.x * w.scale_x;
        const float ly = t->position.y * w.scale_y;
        const float wx = c * lx - s * ly;
        const float wy = s * lx + c * ly;
        w.pos_x  += wx;
        w.pos_y  += wy;
        w.rot    += t->rotation;
        w.scale_x *= t->scale.x;
        w.scale_y *= t->scale.y;
    };

    if (overflow.empty()) {
        for (int i = n - 1; i >= 0; --i) compose_one(stack[i]);
    } else {
        for (int i = (int)overflow.size() - 1; i >= 0; --i) compose_one(overflow[i]);
    }
    return w;
}

Entity World::first_child_of(Entity e) const {
    if (m_impl->first_child_id == INVALID_COMPONENT_ID) return NULL_ENTITY;
    auto* fc = static_cast<const Engine::components::FirstChild*>(
        get_component(e, m_impl->first_child_id));
    return fc ? fc->value : NULL_ENTITY;
}

Entity World::next_sibling_of(Entity e) const {
    if (m_impl->next_sibling_id == INVALID_COMPONENT_ID) return NULL_ENTITY;
    auto* ns = static_cast<const Engine::components::NextSibling*>(
        get_component(e, m_impl->next_sibling_id));
    return ns ? ns->value : NULL_ENTITY;
}

u32 World::child_count(Entity parent) const {
    u32 n = 0;
    for (Entity e = first_child_of(parent); !e.is_null(); e = next_sibling_of(e)) ++n;
    return n;
}

namespace {
    // Removes `child` from `parent`'s child linked list. Doesn't touch
    // `child`'s own Parent / NextSibling components.
    void remove_from_chain(World& world, ComponentId first_child_id,
                           ComponentId next_sibling_id,
                           Entity parent, Entity child) {
        Entity head = world.first_child_of(parent);
        if (head.is_null()) return;

        if (head == child) {
            Entity next = world.next_sibling_of(child);
            if (next.is_null()) {
                world.remove_component(parent, first_child_id);
            } else {
                Engine::components::FirstChild fc{next};
                world.add_component(parent, first_child_id, &fc);   // overwrite
            }
            return;
        }

        Entity prev = head;
        Entity cur  = world.next_sibling_of(prev);
        while (!cur.is_null() && cur != child) {
            prev = cur;
            cur  = world.next_sibling_of(cur);
        }
        if (cur != child) return;   // wasn't actually in this parent's list

        Entity next = world.next_sibling_of(child);
        if (next.is_null()) {
            world.remove_component(prev, next_sibling_id);
        } else {
            Engine::components::NextSibling ns{next};
            world.add_component(prev, next_sibling_id, &ns);
        }
    }
}  // namespace

void World::unparent(Entity child) {
    if (!is_alive(child))                                   return;
    if (m_impl->parent_id == INVALID_COMPONENT_ID)          return;

    Entity old_parent = parent_of(child);
    if (old_parent.is_null()) return;

    remove_from_chain(*this, m_impl->first_child_id, m_impl->next_sibling_id,
                      old_parent, child);
    remove_component(child, m_impl->next_sibling_id);
    remove_component(child, m_impl->parent_id);
    // Newly a root — append to the explicit order so the editor sees
    // it at the bottom of the root list.
    roots_append_unique(m_impl->roots_order, child.index);
}

void World::set_parent(Entity child, Entity new_parent) {
    if (!is_alive(child))                                   return;
    if (m_impl->parent_id == INVALID_COMPONENT_ID)          return;

    Entity old_parent = parent_of(child);
    if (old_parent == new_parent) return;   // no-op

    if (!old_parent.is_null()) {
        remove_from_chain(*this, m_impl->first_child_id, m_impl->next_sibling_id,
                          old_parent, child);
        remove_component(child, m_impl->next_sibling_id);
    }

    if (new_parent.is_null()) {
        remove_component(child, m_impl->parent_id);
        // Becoming a root.
        roots_append_unique(m_impl->roots_order, child.index);
        return;
    }
    // Was a root, no longer is.
    roots_remove(m_impl->roots_order, child.index);

    // Insert at head of new_parent's children list.
    Entity old_head = first_child_of(new_parent);

    Engine::components::NextSibling ns{old_head};
    add_component(child, m_impl->next_sibling_id, &ns);

    Engine::components::FirstChild fc{child};
    add_component(new_parent, m_impl->first_child_id, &fc);  // overwrite if present

    Engine::components::Parent par{new_parent};
    add_component(child, m_impl->parent_id, &par);           // overwrite if present
}

// ---- Sibling reorder --------------------------------------------------------
//
// Both helpers funnel into the same chain mutation:
//   1. Refuse self-move, cycle (sibling under child), or unloaded hierarchy.
//   2. Detach child from its current chain (if any).
//   3. Set child.parent to sibling.parent (covers root → child, root → root,
//      child → root, etc).
//   4. Splice child into the new chain at the requested position relative
//      to sibling.

void World::move_before(Entity child, Entity sibling) {
    if (!is_alive(child) || !is_alive(sibling))           return;
    if (child == sibling)                                 return;
    if (m_impl->parent_id == INVALID_COMPONENT_ID)        return;

    // Cycle check: if `child` is an ancestor of `sibling`, moving child
    // under sibling's parent would create a loop (sibling could be a
    // descendant of child).
    for (Entity a = parent_of(sibling); !a.is_null(); a = parent_of(a)) {
        if (a == child) return;
    }

    const Entity new_parent = parent_of(sibling);

    // Detach from old chain.
    Entity old_parent = parent_of(child);
    if (!old_parent.is_null()) {
        remove_from_chain(*this, m_impl->first_child_id, m_impl->next_sibling_id,
                          old_parent, child);
    }
    remove_component(child, m_impl->next_sibling_id);
    if (old_parent != new_parent) {
        if (new_parent.is_null()) {
            remove_component(child, m_impl->parent_id);
        } else {
            Engine::components::Parent par{new_parent};
            add_component(child, m_impl->parent_id, &par);
        }
    }

    // Splice in at sibling's position. Two cases: sibling is the head of
    // its chain (or a root + sibling has no recorded parent) — child
    // becomes the new head. Otherwise find the predecessor and rewrite
    // its NextSibling to point at child.
    if (new_parent.is_null()) {
        // Both child and sibling are roots — splice within the explicit
        // roots_order vector. Drop child's existing entry first (might
        // not be present if it was just unparented above the move),
        // then insert immediately before sibling.
        roots_remove(m_impl->roots_order, child.index);
        auto it = std::find(m_impl->roots_order.begin(),
                            m_impl->roots_order.end(), sibling.index);
        if (it == m_impl->roots_order.end()) {
            // Sibling somehow not in roots — defensive append.
            m_impl->roots_order.push_back(child.index);
        } else {
            m_impl->roots_order.insert(it, child.index);
        }
        return;
    }

    const Entity head = first_child_of(new_parent);
    if (head == sibling) {
        // Insert at head: child -> sibling -> rest
        Engine::components::NextSibling ns{sibling};
        add_component(child, m_impl->next_sibling_id, &ns);
        Engine::components::FirstChild fc{child};
        add_component(new_parent, m_impl->first_child_id, &fc);
    } else {
        // Walk to the predecessor of sibling.
        Entity prev = head;
        while (!prev.is_null() && next_sibling_of(prev) != sibling) {
            prev = next_sibling_of(prev);
        }
        if (prev.is_null()) return;   // sibling not in chain (shouldn't happen)
        Engine::components::NextSibling ns_child{sibling};
        add_component(child, m_impl->next_sibling_id, &ns_child);
        Engine::components::NextSibling ns_prev{child};
        add_component(prev, m_impl->next_sibling_id, &ns_prev);
    }
}

void World::move_after(Entity child, Entity sibling) {
    if (!is_alive(child) || !is_alive(sibling))           return;
    if (child == sibling)                                 return;
    if (m_impl->parent_id == INVALID_COMPONENT_ID)        return;

    for (Entity a = parent_of(sibling); !a.is_null(); a = parent_of(a)) {
        if (a == child) return;
    }

    const Entity new_parent = parent_of(sibling);

    Entity old_parent = parent_of(child);
    if (!old_parent.is_null()) {
        remove_from_chain(*this, m_impl->first_child_id, m_impl->next_sibling_id,
                          old_parent, child);
    }
    remove_component(child, m_impl->next_sibling_id);
    if (old_parent != new_parent) {
        if (new_parent.is_null()) {
            remove_component(child, m_impl->parent_id);
        } else {
            Engine::components::Parent par{new_parent};
            add_component(child, m_impl->parent_id, &par);
        }
    }

    if (new_parent.is_null()) {
        // Roots case — insert child immediately AFTER sibling in
        // roots_order. Mirror of move_before's roots branch.
        roots_remove(m_impl->roots_order, child.index);
        auto it = std::find(m_impl->roots_order.begin(),
                            m_impl->roots_order.end(), sibling.index);
        if (it == m_impl->roots_order.end()) {
            m_impl->roots_order.push_back(child.index);
        } else {
            m_impl->roots_order.insert(it + 1, child.index);
        }
        return;
    }

    // child becomes sibling's NextSibling; sibling's old NextSibling
    // becomes child's NextSibling.
    const Entity old_next = next_sibling_of(sibling);
    if (old_next.is_null()) {
        remove_component(child, m_impl->next_sibling_id);
    } else {
        Engine::components::NextSibling ns{old_next};
        add_component(child, m_impl->next_sibling_id, &ns);
    }
    Engine::components::NextSibling ns_sib{child};
    add_component(sibling, m_impl->next_sibling_id, &ns_sib);
}

}  // namespace Engine::ecs
