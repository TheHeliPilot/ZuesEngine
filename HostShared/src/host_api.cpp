#include <zues/host/host_api.h>
#include <zues/host/host_context.h>
#include <zues/host/particle_system.h>
#include <zues/host/audio_system.h>

#include <zues/asset.h>
#include <zues/components/render.h>
#include <zues/guid.h>
#include <zues/components/transform.h>
#include <zues/ecs/world.h>
#include <zues/engine.h>
#include <zues/log.h>
#include <zues/service.h>
#include <zues/services/input.h>
#include <zues/services/renderer_2d.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <utility>   // std::swap
#include <vector>

// Forward decl for the prefab instantiator. Body lives in HostShared's
// prefab_runtime.cpp; the editor builds a thin wrapper on top that adds
// undo + selection.
namespace Engine::host {
    Engine::ecs::Entity prefab_instantiate_runtime(
        Engine::ecs::World& world, const std::string& abs_path,
        Engine::math::vec2 world_pos);
}

namespace Engine::host {

namespace {
    // Sentinel engine handle. The editor hosts a single global engine; the
    // handle is opaque to the project. Non-null fixed pointer so null-checks
    // inside the project work correctly.
    constexpr std::uintptr_t ENGINE_HANDLE_SENTINEL = 0xE7C1E7C1u;

    // The active host context. Embedder (editor or runtime) constructs one
    // and hands it over via set_host_context. The world pointer + project
    // dir live inside it; nulls make every world-touching thunk a safe
    // no-op. Replaces the older "active world + editor state" pair the
    // host_api used to carry.
    HostContext* g_host = nullptr;
    // Convenience accessors so call sites don't have to check g_host every
    // time. The world pointer mirrors g_host->world for the hot path.
    ecs::World* g_world = nullptr;

    // Persistent string storage for component names handed to us by the
    // project DLL. The Engine::ecs::ComponentType keeps a `const char*` —
    // it must outlive the project DLL's stack/static memory.
    std::vector<std::unique_ptr<std::string>> g_component_names;

    // Persistent storage for project-side field metadata. Each registered
    // component owns one entry: a vector of FieldInfo (host's enum form) plus
    // a vector of name strings. Both must outlive the project DLL because
    // ComponentType.fields aliases into the FieldInfo vector and each
    // FieldInfo.name aliases into the name vector.
    struct FieldStorage {
        std::vector<ecs::FieldInfo>              infos;
        std::vector<std::unique_ptr<std::string>> names;
        std::vector<u8>                           default_bytes;   // deep copy of project default
    };
    std::vector<std::unique_ptr<FieldStorage>> g_field_storage;

    // Per-system closure: project-side fn + user. World stores a stable
    // pointer to this; the registered thunk dereferences it to call the
    // project's fn. Closures live as long as the editor process — that's OK
    // because unregister_project_systems removes them from the world before
    // the project DLL unloads (the closure objects themselves stay).
    struct ProjectSystemClosure {
        ZuesSystemFn fn;
        void*        user;
    };
    std::vector<std::unique_ptr<ProjectSystemClosure>> g_system_closures;

    // Track every system we registered into the world so unregister can
    // remove them. ecs::SystemHandle = {phase, id} — both needed.
    std::vector<ecs::SystemHandle> g_system_handles;

    // ---- Trivial component fns (POD only for now) --------------------------
    // Engine::ecs::ComponentType requires move_ctor + dtor function pointers,
    // but the World implementation never actually CALLS them — it uses the
    // stored size for direct memcpy. We still set them to no-op stubs so that
    // any future code that does call them is well-defined.
    void noop_move(void* /*dst*/, void* /*src*/) {}
    void noop_dtor(void* /*p*/)                  {}

    // ---- Service + log -----------------------------------------------------

    void* host_get_service(ZuesEngine*, const char* id, uint32_t version) {
        auto* sr = Engine::services();
        return sr ? sr->get_service(id, version) : nullptr;
    }

    void host_log(ZuesEngine*, ZuesLogLevel level, const char* msg) {
        // ZuesLogLevel and Engine::LogLevel share the same numeric layout.
        Engine::log_write(static_cast<Engine::LogLevel>(level), "project", msg);
    }

    // ---- Components --------------------------------------------------------

    ZuesComponentId host_register_component(ZuesEngine*, const char* name,
                                             uint32_t size, uint32_t align,
                                             const void* fields_data, uint32_t fields_count,
                                             const void* default_bytes) {
        if (!g_world || !name) return 0;

        // Deep-copy the type name so it survives the project DLL.
        g_component_names.push_back(std::make_unique<std::string>(name));
        const char* persistent_name = g_component_names.back()->c_str();

        ecs::ComponentType t{};
        t.name      = persistent_name;
        t.size      = size;
        t.align     = align;
        t.move_ctor = noop_move;
        t.dtor      = noop_dtor;

        // Always allocate FieldStorage so we have a stable home for field
        // metadata AND default bytes. Both must outlive the project DLL.
        auto storage = std::make_unique<FieldStorage>();

        // Deep-copy the field array (if any). ZuesFieldKind is laid out
        // numerically identical to ecs::FieldKind, so we cast 1:1 on the
        // kind. Field names also belong to the project DLL — deep-copy them.
        if (fields_data && fields_count > 0) {
            const auto* src = static_cast<const ZuesFieldInfo*>(fields_data);
            storage->infos.reserve(fields_count);
            storage->names.reserve(fields_count);
            for (uint32_t i = 0; i < fields_count; ++i) {
                storage->names.push_back(std::make_unique<std::string>(
                    src[i].name ? src[i].name : ""));
                ecs::FieldInfo fi{};
                fi.name   = storage->names.back()->c_str();
                fi.kind   = static_cast<ecs::FieldKind>(src[i].kind);
                fi.offset = src[i].offset;
                fi.size   = src[i].size;
                storage->infos.push_back(fi);
            }
            t.fields      = storage->infos.data();
            t.field_count = fields_count;
        }

        // Deep-copy the default-value prototype so it survives DLL unload.
        if (default_bytes && size > 0) {
            const auto* src = static_cast<const u8*>(default_bytes);
            storage->default_bytes.assign(src, src + size);
            t.default_data      = storage->default_bytes.data();
            t.default_data_size = size;
        }

        g_field_storage.push_back(std::move(storage));
        return g_world->register_component_type(t);
    }

    ZuesComponentId host_find_component_id(ZuesEngine*, const char* name) {
        return g_world ? g_world->find_component_id(name) : 0;
    }

    // Persistent storage for category strings — the world holds a const char*
    // and expects it to outlive the registration. Project memory is volatile,
    // so we deep-copy here.
    //
    // CRITICAL: must NOT be std::vector<std::string>. Short strings (like
    // "Project") use SSO -- the buffer lives INSIDE the std::string struct,
    // not on the heap. A vector reallocation moves the strings to new
    // memory and the old `c_str()` pointers handed to the world dangle.
    // The dangling memory then gets reused for the next std::string
    // allocation (we observed it being repurposed for ImGui's ini path),
    // which makes the Add Component picker render path strings as if
    // they were component categories.
    //
    // unique_ptr<std::string> heap-allocates the string struct itself, so
    // the SSO buffer's address is stable across container growth. The
    // unique_ptr objects inside the vector move on realloc, but each
    // points to the same heap std::string -- c_str() stays valid.
    std::vector<std::unique_ptr<std::string>> g_category_storage;

    void host_set_component_category(ZuesEngine*, ZuesComponentId id,
                                      const char* category) {
        if (!g_world || id == 0) return;
        if (!category || !*category) {
            g_world->set_component_category(id, "");
            return;
        }
        g_category_storage.push_back(std::make_unique<std::string>(category));
        g_world->set_component_category(id, g_category_storage.back()->c_str());
    }

    // ---- Entities ----------------------------------------------------------

    ZuesEntity host_create_entity(ZuesEngine*) {
        if (!g_world) return ZuesEntity{0, 0};
        const auto e = g_world->create_entity();
        return ZuesEntity{e.index, e.generation};
    }

    void host_destroy_entity(ZuesEngine*, ZuesEntity e) {
        if (g_world) g_world->destroy_entity(ecs::Entity{e.index, e.generation});
    }

    int host_is_entity_alive(ZuesEngine*, ZuesEntity e) {
        if (!g_world) return 0;
        return g_world->is_alive(ecs::Entity{e.index, e.generation}) ? 1 : 0;
    }

    void* host_add_component(ZuesEngine*, ZuesEntity e,
                              ZuesComponentId id, const void* initial) {
        if (!g_world) return nullptr;
        return g_world->add_component(ecs::Entity{e.index, e.generation}, id, initial);
    }

    void host_remove_component(ZuesEngine*, ZuesEntity e, ZuesComponentId id) {
        if (g_world) g_world->remove_component(ecs::Entity{e.index, e.generation}, id);
    }

    void* host_get_component(ZuesEngine*, ZuesEntity e, ZuesComponentId id) {
        if (!g_world) return nullptr;
        return g_world->get_component(ecs::Entity{e.index, e.generation}, id);
    }

    int host_has_component(ZuesEngine*, ZuesEntity e, ZuesComponentId id) {
        if (!g_world) return 0;
        return g_world->has_component(ecs::Entity{e.index, e.generation}, id) ? 1 : 0;
    }

    // ---- Systems -----------------------------------------------------------

    // Bridge from World's SystemFn signature to the project's signature.
    // `user` is the ProjectSystemClosure*; we forward to project's fn with
    // the engine handle + project's own user pointer.
    void thunk_into_project_system(ecs::World&, float dt, void* user) {
        auto* psc = static_cast<ProjectSystemClosure*>(user);
        psc->fn(engine_handle(), dt, psc->user);
    }

    void host_add_system(ZuesEngine*, const char* name, ZuesPhase phase,
                          ZuesSystemFn fn, void* user) {
        if (!g_world || !fn) return;
        g_system_closures.push_back(std::make_unique<ProjectSystemClosure>(
            ProjectSystemClosure{fn, user}));
        auto* psc = g_system_closures.back().get();
        const auto handle = g_world->add_system(name,
            static_cast<ecs::Phase>(phase), thunk_into_project_system, psc);
        g_system_handles.push_back(handle);
    }

    void host_add_system_with_domain(ZuesEngine*, const char* name,
                                      ZuesPhase phase, ZuesSystemDomain domain,
                                      ZuesSystemFn fn, void* user) {
        if (!g_world || !fn) return;
        g_system_closures.push_back(std::make_unique<ProjectSystemClosure>(
            ProjectSystemClosure{fn, user}));
        auto* psc = g_system_closures.back().get();
        const auto handle = g_world->add_system(name,
            static_cast<ecs::Phase>(phase), thunk_into_project_system, psc,
            static_cast<ecs::SystemDomain>(domain));
        g_system_handles.push_back(handle);
    }

    // ---- Built-in component helpers ----------------------------------------

    void host_add_sprite_default(ZuesEngine*, ZuesEntity e,
                                  float w, float h,
                                  float r, float g, float b, float a) {
        if (!g_world) return;
        const auto sprite_id = g_world->find_component_id("Sprite");
        if (!sprite_id) return;
        components::Sprite sp{};
        sp.size = {w, h};
        sp.tint = Engine::math::color{r, g, b, a};
        // texture stays default (handle 0); renderer substitutes 1×1 white.
        // pivot stays default (0.5, 0.5); flip flags off.
        g_world->add_component(ecs::Entity{e.index, e.generation}, sprite_id, &sp);
    }

    // ---- Transform2D helpers ----------------------------------------------
    // Auto-attached on create_entity, but projects need a way to position
    // entities without mirroring the engine struct's byte layout.

    void host_set_transform(ZuesEngine*, ZuesEntity e,
                            float x, float y, float rotation,
                            float sx, float sy) {
        if (!g_world) return;
        const auto tid = g_world->find_component_id("Transform2D");
        if (!tid) return;
        const ecs::Entity ee{e.index, e.generation};
        auto* t = static_cast<components::Transform2D*>(
            g_world->get_component(ee, tid));
        if (!t) return;     // entity dead or somehow lost its Transform2D
        t->position = {x, y};
        t->rotation = rotation;
        t->scale    = {sx, sy};
    }

    void host_set_transform_position(ZuesEngine*, ZuesEntity e,
                                      float x, float y) {
        if (!g_world) return;
        const auto tid = g_world->find_component_id("Transform2D");
        if (!tid) return;
        auto* t = static_cast<components::Transform2D*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, tid));
        if (!t) return;
        t->position = {x, y};
    }

    // ---- Input thunks -----------------------------------------------------
    // Resolved lazily so the host API works even if the input module hasn't
    // registered yet (early init). After first successful resolve the pointer
    // is cached for the rest of the editor's lifetime.

    ::IInput_v1* input_svc() {
        static ::IInput_v1* cached = nullptr;
        if (cached) return cached;
        auto* sr = Engine::services();
        cached = sr ? static_cast<::IInput_v1*>(
            sr->get_service(ZUES_SERVICE_INPUT, ZUES_SERVICE_INPUT_VERSION))
            : nullptr;
        return cached;
    }

    // ---- Renderer service (lazy) ------------------------------------------
    // Mirroring input_svc — resolved once after the module registers.
    ::IRenderer_2D_v1* renderer_svc() {
        static ::IRenderer_2D_v1* cached = nullptr;
        if (cached) return cached;
        auto* sr = Engine::services();
        cached = sr ? static_cast<::IRenderer_2D_v1*>(
            sr->get_service(ZUES_SERVICE_RENDERER_2D, ZUES_SERVICE_RENDERER_2D_VERSION))
            : nullptr;
        return cached;
    }

    // ---- v6: texture loading + textured sprites ----------------------------

    ZuesTextureHandle host_load_texture(ZuesEngine*, const char* path) {
        // NOTE: the renderer does NOT dedup by path in v1 — each call may
        // upload a new GL texture. Deduplication is a follow-up task.
        auto* r = renderer_svc();
        if (!r || !r->load_texture_from_file || !path) return 0;
        return r->load_texture_from_file(r, path);
    }

    void host_add_sprite_textured(ZuesEngine*, ZuesEntity e,
                                   float w, float h,
                                   ZuesTextureHandle tex,
                                   float r, float g, float b, float a) {
        if (!g_world) return;
        const auto sprite_id = g_world->find_component_id("Sprite");
        if (!sprite_id) return;
        components::Sprite sp{};
        sp.size              = {w, h};
        sp.tint              = Engine::math::color{r, g, b, a};
        sp.texture.index      = tex;  // GL id stored in Handle::index
        sp.texture.generation = 1;    // generation != 0 → Handle::is_valid()
        g_world->add_component(ecs::Entity{e.index, e.generation}, sprite_id, &sp);
    }

    int host_is_key_down    (ZuesEngine*, int k) { auto* s = input_svc(); return s ? s->is_key_down(s, k)     : 0; }
    int host_is_key_pressed (ZuesEngine*, int k) { auto* s = input_svc(); return s ? s->is_key_pressed(s, k)  : 0; }
    int host_is_key_released(ZuesEngine*, int k) { auto* s = input_svc(); return s ? s->is_key_released(s, k) : 0; }

    void host_mouse_pos(ZuesEngine*, float* ox, float* oy) {
        auto* s = input_svc();
        if (s) s->mouse_pos(s, ox, oy);
        else { if (ox) *ox = 0; if (oy) *oy = 0; }
    }

    int host_is_mouse_down    (ZuesEngine*, int b) { auto* s = input_svc(); return s ? s->is_mouse_down(s, b)     : 0; }
    int host_is_mouse_pressed (ZuesEngine*, int b) { auto* s = input_svc(); return s ? s->is_mouse_pressed(s, b)  : 0; }
    int host_is_mouse_released(ZuesEngine*, int b) { auto* s = input_svc(); return s ? s->is_mouse_released(s, b) : 0; }
    float host_mouse_wheel(ZuesEngine*) { auto* s = input_svc(); return s ? s->mouse_wheel(s) : 0.0f; }

    void host_get_transform(ZuesEngine*, ZuesEntity e,
                            float* ox, float* oy, float* orot,
                            float* osx, float* osy) {
        if (!g_world) return;
        const auto tid = g_world->find_component_id("Transform2D");
        if (!tid) return;
        const auto* t = static_cast<const components::Transform2D*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, tid));
        if (!t) return;
        if (ox)   *ox   = t->position.x;
        if (oy)   *oy   = t->position.y;
        if (orot) *orot = t->rotation;
        if (osx)  *osx  = t->scale.x;
        if (osy)  *osy  = t->scale.y;
    }

    // ---- Queries -----------------------------------------------------------

    struct QueryThunkCtx {
        ZuesQueryFn fn;
        void*       user;
    };

    void query_thunk(void* user, ecs::Entity e, void** col_ptrs, u32 n_cols) {
        auto* ctx = static_cast<QueryThunkCtx*>(user);
        ctx->fn(ZuesEntity{e.index, e.generation}, col_ptrs, n_cols, ctx->user);
    }

    // Resolve a project-relative prefab path through the editor state and
    // hand it to the editor's prefab loader. Returns 0 on any failure —
    // missing project, missing world, file not found, parse error. The
    // resulting entity index is stable for the rest of the play session.
    ZuesEntity host_instantiate_prefab(ZuesEngine*, const char* prefab_path,
                                        float x, float y) {
        ZuesEntity z{0, 0};
        if (!g_host || !g_world || !prefab_path) return z;
        std::string abs;
        if (g_host->project_loaded && !g_host->project_dir.empty()) {
            abs = g_host->project_dir + "/" + prefab_path;
        } else {
            abs = prefab_path;
        }
        const ecs::Entity e = prefab_instantiate_runtime(*g_world, abs, {x, y});
        if (e.is_null()) return z;
        z.index      = e.index;
        z.generation = e.generation;
        return z;
    }

    // Singleton lookups + cache invalidation. ensure_singleton creates on
    // demand; find_singleton is read-only. Both translate the host's
    // ecs::Entity into the C-stable ZuesEntity layout (same shape) on the
    // way back. world_version exposes World's archetype counter so the
    // plugin's cached getters can detect "anything moved since last call".
    // Tracks every component id the project DLL has asked for as a
    // singleton, so the editor can re-run ensure_singleton AFTER world.load
    // to repopulate the world's singletons map. Without this re-run, the
    // sequence
    //     register_builtins -> project on_load (ensure singleton, creates entity)
    //     -> world.clear (destroys entity AND singletons map)
    //     -> world.load_json (restores entity, but singletons map stays empty)
    // leaves "Hierarchy / Globals" empty until the first user code call to
    // Singleton<T>() lazily adopts. With this list, we can adopt eagerly
    // right after autoload completes.
    std::vector<ZuesComponentId> g_singleton_ids;

    ZuesEntity host_ensure_singleton(ZuesEngine*, ZuesComponentId id) {
        ZuesEntity z{0, 0};
        if (!g_world) return z;
        // Remember the id (dedup) so post-load can re-run.
        bool seen = false;
        for (auto cid : g_singleton_ids) if (cid == id) { seen = true; break; }
        if (!seen) g_singleton_ids.push_back(id);
        const ecs::Entity e = g_world->ensure_singleton(id);
        if (e.is_null()) return z;
        z.index      = e.index;
        z.generation = e.generation;
        return z;
    }
    ZuesEntity host_find_singleton(ZuesEngine*, ZuesComponentId id) {
        ZuesEntity z{0, 0};
        if (!g_world) return z;
        const ecs::Entity e = g_world->find_singleton(id);
        if (e.is_null()) return z;
        z.index      = e.index;
        z.generation = e.generation;
        return z;
    }
    uint64_t host_world_version(ZuesEngine*) {
        return g_world ? g_world->archetype_version() : 0;
    }

    // Guid-keyed prefab instantiate. Resolves through AssetRegistry to a
    // path, then hands off to the same loader the string-path overload
    // uses. Keeps two surfaces but one implementation.
    ZuesEntity host_instantiate_prefab_guid(ZuesEngine*,
                                             uint64_t hi, uint64_t lo,
                                             float x, float y) {
        ZuesEntity z{0, 0};
        if (!g_host || !g_world) return z;
        const Guid g{hi, lo};
        if (g.is_null()) return z;
        const char* rel = AssetRegistry::instance().path_for(g);
        if (!rel) return z;
        // Registry stores paths relative to `<project>/assets/`, so prepend
        // both segments.
        std::string abs;
        if (g_host->project_loaded && !g_host->project_dir.empty()) {
            abs = g_host->project_dir + "/assets/" + rel;
        } else {
            abs = rel;
        }
        const ecs::Entity e = prefab_instantiate_runtime(*g_world, abs, {x, y});
        if (e.is_null()) return z;
        z.index      = e.index;
        z.generation = e.generation;
        return z;
    }

    void host_query_each(ZuesEngine*,
                          const ZuesComponentId* required, uint32_t n_required,
                          const ZuesComponentId* excluded, uint32_t n_excluded,
                          ZuesQueryFn fn, void* user) {
        if (!g_world || !fn) return;
        QueryThunkCtx ctx{fn, user};
        g_world->iterate_query(required, n_required, excluded, n_excluded,
                                query_thunk, &ctx);
    }

    // ---- Timers --------------------------------------------------------------
    //
    // Single global list. Handle = (slot << 16) | generation; generation bumps
    // on free so a stale handle from a fired/cancelled timer can't accidentally
    // match a recycled slot. Both fields are 16-bit which gives us 65k live
    // timers and 65k generations per slot — fine for game-tick lifetimes.
    struct TimerEntry {
        bool   alive    = false;
        bool   repeating = false;
        float  remaining = 0.0f;
        float  period    = 0.0f;   // re-arm value for repeating
        void (*cb)(void*) = nullptr;
        void*  user      = nullptr;
        uint16_t generation = 1;
    };
    std::vector<TimerEntry> g_timers;
    // Walk-through cursor for re-arm so a 0-second interval timer doesn't
    // starve the rest of the list within a single tick.

    uint32_t timer_make_handle(uint16_t slot, uint16_t gen) {
        return ((uint32_t)slot << 16) | (uint32_t)gen;
    }
    void timer_decode(uint32_t handle, uint16_t& slot, uint16_t& gen) {
        slot = (uint16_t)(handle >> 16);
        gen  = (uint16_t)(handle & 0xFFFFu);
    }

    uint32_t timer_alloc(float seconds, bool repeating,
                          void (*cb)(void*), void* user) {
        if (!cb) return 0;
        // Reuse the first dead slot. Linear scan — timer lists are tiny in
        // practice (tens, not thousands).
        for (size_t i = 0; i < g_timers.size(); ++i) {
            if (!g_timers[i].alive) {
                g_timers[i].alive    = true;
                g_timers[i].repeating = repeating;
                g_timers[i].remaining = seconds;
                g_timers[i].period    = seconds;
                g_timers[i].cb        = cb;
                g_timers[i].user      = user;
                if (g_timers[i].generation == 0) g_timers[i].generation = 1;
                return timer_make_handle((uint16_t)i, g_timers[i].generation);
            }
        }
        if (g_timers.size() >= 0xFFFFu) return 0;  // 65k cap
        TimerEntry t;
        t.alive = true; t.repeating = repeating;
        t.remaining = seconds; t.period = seconds;
        t.cb = cb; t.user = user; t.generation = 1;
        g_timers.push_back(t);
        return timer_make_handle((uint16_t)(g_timers.size() - 1), 1);
    }

    uint32_t host_set_timeout(ZuesEngine*, float seconds,
                                void (*cb)(void*), void* user) {
        return timer_alloc(seconds, false, cb, user);
    }
    uint32_t host_set_interval(ZuesEngine*, float seconds,
                                 void (*cb)(void*), void* user) {
        return timer_alloc(seconds, true, cb, user);
    }
    int host_cancel_timer(ZuesEngine*, uint32_t handle) {
        if (handle == 0) return 0;
        uint16_t slot, gen; timer_decode(handle, slot, gen);
        if (slot >= g_timers.size()) return 0;
        TimerEntry& t = g_timers[slot];
        if (!t.alive || t.generation != gen) return 0;
        t.alive = false;
        // Bump generation so stale handles bounce off the recycled slot.
        ++t.generation; if (t.generation == 0) t.generation = 1;
        t.cb = nullptr; t.user = nullptr;
        return 1;
    }

    // ---- Random --------------------------------------------------------------
    //
    // mt19937_64 — seeded once at first use from steady_clock; user can reseed
    // for deterministic runs. We hold it as a function-static so initialization
    // ordering with translation units doesn't bite.
    std::mt19937_64& rng() {
        static std::mt19937_64 g{
            (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()};
        return g;
    }
    float host_random_float(ZuesEngine*) {
        // [0, 1) — divide by 2^53 to fit double precision then narrow.
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        return d(rng());
    }
    float host_random_range(ZuesEngine*, float lo, float hi) {
        if (hi <= lo) return lo;
        std::uniform_real_distribution<float> d(lo, hi);
        return d(rng());
    }
    int host_random_int(ZuesEngine*, int lo, int hi) {
        if (hi < lo) std::swap(lo, hi);
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng());
    }
    void host_random_seed(ZuesEngine*, uint64_t seed) {
        rng().seed(seed);
    }

    // ---- Hierarchy queries -----------------------------------------------
    //
    // All return ZuesEntity{0,0} on miss, mirroring Engine::ecs::Entity's
    // null sentinel. Cheap O(1) for parent / first_child / next_sibling
    // (component lookup); O(idx) for get_child_at (linked-list walk).
    ZuesEntity host_get_parent(ZuesEngine*, ZuesEntity e) {
        ZuesEntity z{0, 0};
        if (!g_world) return z;
        const ecs::Entity p = g_world->parent_of(ecs::Entity{e.index, e.generation});
        if (p.is_null()) return z;
        return ZuesEntity{p.index, p.generation};
    }
    ZuesEntity host_get_first_child(ZuesEngine*, ZuesEntity e) {
        ZuesEntity z{0, 0};
        if (!g_world) return z;
        const ecs::Entity c = g_world->first_child_of(ecs::Entity{e.index, e.generation});
        if (c.is_null()) return z;
        return ZuesEntity{c.index, c.generation};
    }
    ZuesEntity host_get_next_sibling(ZuesEngine*, ZuesEntity e) {
        ZuesEntity z{0, 0};
        if (!g_world) return z;
        const ecs::Entity n = g_world->next_sibling_of(ecs::Entity{e.index, e.generation});
        if (n.is_null()) return z;
        return ZuesEntity{n.index, n.generation};
    }
    uint32_t host_get_child_count(ZuesEngine*, ZuesEntity e) {
        if (!g_world) return 0;
        uint32_t n = 0;
        g_world->iterate_children(ecs::Entity{e.index, e.generation},
            [&](ecs::Entity) { ++n; });
        return n;
    }
    void host_add_text_default(ZuesEngine*, ZuesEntity e,
                                const char* utf8, float size_px,
                                float r, float g, float b, float a) {
        if (!g_world) return;
        const auto text_id = g_world->find_component_id("Text");
        if (!text_id) return;
        components::Text t{};
        if (utf8) {
            std::strncpy(t.utf8, utf8, sizeof(t.utf8) - 1);
            t.utf8[sizeof(t.utf8) - 1] = '\0';
        }
        t.size_px = size_px > 0.0f ? size_px : 16.0f;
        t.color   = Engine::math::color{r, g, b, a};
        g_world->add_component(ecs::Entity{e.index, e.generation}, text_id, &t);
    }
    void host_set_text(ZuesEngine*, ZuesEntity e, const char* utf8) {
        if (!g_world || !utf8) return;
        const auto text_id = g_world->find_component_id("Text");
        if (!text_id) return;
        auto* t = static_cast<components::Text*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, text_id));
        if (!t) return;
        std::strncpy(t->utf8, utf8, sizeof(t->utf8) - 1);
        t->utf8[sizeof(t->utf8) - 1] = '\0';
    }
    void host_set_text_color(ZuesEngine*, ZuesEntity e,
                              float r, float g, float b, float a) {
        if (!g_world) return;
        const auto text_id = g_world->find_component_id("Text");
        if (!text_id) return;
        auto* t = static_cast<components::Text*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, text_id));
        if (!t) return;
        t->color = Engine::math::color{r, g, b, a};
    }

    // ---- v15: Animator playback ---------------------------------------
    // Walk Animator.clips (newline-separated "name<TAB>guid_hex" rows
    // populated by the editor's clip table) and find the matching name.
    // On hit, set Animator.current to that index, reset time to 0, set
    // playing = 1, and mirror the resolved guid into Animator.animation
    // so the runtime animator system picks it up next tick.
    int host_animator_play_by_name(ZuesEngine*, ZuesEntity e, const char* name) {
        if (!g_world || !name) return 0;
        const auto anim_id = g_world->find_component_id("Animator");
        if (!anim_id) return 0;
        auto* an = static_cast<components::Animator*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, anim_id));
        if (!an) return 0;

        // Parse rows from an->clips. Same format the inspector writes:
        // "<name>\t<32hex>\n". Tolerates blank lines + missing tabs.
        const char* p   = an->clips;
        const char* end = an->clips +
            ::strnlen(an->clips, components::ANIMATOR_CLIPS_BUF);
        int idx = 0;
        while (p < end) {
            const char* line_end = p;
            while (line_end < end && *line_end != '\n') ++line_end;
            const char* tab = p;
            while (tab < line_end && *tab != '\t') ++tab;
            const std::size_t name_len = (std::size_t)(tab - p);
            if (name_len == std::strlen(name) &&
                std::strncmp(p, name, name_len) == 0) {
                an->current = idx;
                an->time    = 0.0f;
                an->playing = true;
                if (tab < line_end && (line_end - tab - 1) == 32) {
                    Engine::Guid g = Engine::guid_from_hex(tab + 1, 32);
                    an->animation.guid = g;
                }
                return 1;
            }
            // Advance to next row. Skip blank lines too.
            if (!(name_len == 0 && tab == line_end)) ++idx;
            p = (line_end < end) ? line_end + 1 : line_end;
        }
        return 0;
    }
    void host_animator_set_playing(ZuesEngine*, ZuesEntity e, int playing) {
        if (!g_world) return;
        const auto anim_id = g_world->find_component_id("Animator");
        if (!anim_id) return;
        auto* an = static_cast<components::Animator*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, anim_id));
        if (an) an->playing = (playing != 0);
    }
    void host_animator_seek(ZuesEngine*, ZuesEntity e, float seconds) {
        if (!g_world) return;
        const auto anim_id = g_world->find_component_id("Animator");
        if (!anim_id) return;
        auto* an = static_cast<components::Animator*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, anim_id));
        if (an) an->time = seconds;
    }

    // ---- v16: Particles control ---------------------------------------
    // Burst is layered ON TOP of normal emission: we set burst_count +
    // burst_time = current age so the next sim tick fires it once. The
    // ParticleSystem honours burst_count/period; setting period to 0
    // makes it a one-shot. This keeps the API stateless from Lync's
    // perspective -- no emitter handle to track.
    void host_particles_emit_burst(ZuesEngine*, ZuesEntity e, int count) {
        if (!g_world) return;
        const auto pid = g_world->find_component_id("Particles");
        if (!pid) return;
        auto* p = static_cast<components::Particles*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, pid));
        if (!p || count <= 0) return;
        // Schedule a one-shot burst at the next tick.
        p->burst_count  = count;
        p->burst_time   = p->age;
        p->burst_period = 0.0f;
        p->playing      = 1;
    }
    void host_particles_set_playing(ZuesEngine*, ZuesEntity e, int playing) {
        if (!g_world) return;
        const auto pid = g_world->find_component_id("Particles");
        if (!pid) return;
        auto* p = static_cast<components::Particles*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, pid));
        if (p) p->playing = (playing != 0) ? 1 : 0;
    }
    void host_particles_restart(ZuesEngine*, ZuesEntity e) {
        if (!g_world) return;
        const auto pid = g_world->find_component_id("Particles");
        if (!pid) return;
        auto* p = static_cast<components::Particles*>(
            g_world->get_component(ecs::Entity{e.index, e.generation}, pid));
        if (!p) return;
        p->age      = 0.0f;
        p->playing  = 1;
    }

    // ---- v17: Particle pool inspection / extra-field access ----------
    int host_particles_count(ZuesEngine*, ZuesEntity e) {
        return host::particles_api::count(ecs::Entity{e.index, e.generation});
    }
    void host_particles_get_pos(ZuesEngine*, ZuesEntity e, int idx,
                                 float* out_x, float* out_y) {
        host::particles_api::get_pos(ecs::Entity{e.index, e.generation}, idx,
                                      out_x, out_y);
    }
    void host_particles_set_pos(ZuesEngine*, ZuesEntity e, int idx,
                                 float x, float y) {
        host::particles_api::set_pos(ecs::Entity{e.index, e.generation}, idx,
                                      x, y);
    }
    void host_particles_get_vel(ZuesEngine*, ZuesEntity e, int idx,
                                 float* out_vx, float* out_vy) {
        host::particles_api::get_vel(ecs::Entity{e.index, e.generation}, idx,
                                      out_vx, out_vy);
    }
    void host_particles_set_vel(ZuesEngine*, ZuesEntity e, int idx,
                                 float vx, float vy) {
        host::particles_api::set_vel(ecs::Entity{e.index, e.generation}, idx,
                                      vx, vy);
    }
    float host_particles_get_field(ZuesEngine*, ZuesEntity e, int idx,
                                    const char* field) {
        return host::particles_api::get_field(
            ecs::Entity{e.index, e.generation}, idx, field);
    }
    void host_particles_set_field(ZuesEngine*, ZuesEntity e, int idx,
                                   const char* field, float value) {
        host::particles_api::set_field(
            ecs::Entity{e.index, e.generation}, idx, field, value);
    }
    void host_particles_kill(ZuesEngine*, ZuesEntity e, int idx) {
        host::particles_api::kill(ecs::Entity{e.index, e.generation}, idx);
    }

    // ---- v18: spatial query / bulk movement / batched iterator ------
    int host_particles_nearest_neighbor(ZuesEngine*, ZuesEntity target_e,
                                         float x, float y, float max_radius) {
        return host::particles_api::nearest_neighbor(
            ecs::Entity{target_e.index, target_e.generation},
            x, y, max_radius);
    }
    int host_particles_step_toward(ZuesEngine*, ZuesEntity e, int idx,
                                    float tx, float ty,
                                    float max_speed, float dt) {
        return host::particles_api::step_toward(
            ecs::Entity{e.index, e.generation}, idx, tx, ty, max_speed, dt);
    }
    // Raw SoA slice thunks. Trivial passthrough.
    float* host_particles_slice_px(ZuesEngine*, ZuesEntity e) {
        return host::particles_api::slice_px(ecs::Entity{e.index, e.generation});
    }
    float* host_particles_slice_py(ZuesEngine*, ZuesEntity e) {
        return host::particles_api::slice_py(ecs::Entity{e.index, e.generation});
    }
    float* host_particles_slice_vx(ZuesEngine*, ZuesEntity e) {
        return host::particles_api::slice_vx(ecs::Entity{e.index, e.generation});
    }
    float* host_particles_slice_vy(ZuesEngine*, ZuesEntity e) {
        return host::particles_api::slice_vy(ecs::Entity{e.index, e.generation});
    }
    float* host_particles_slice_age(ZuesEngine*, ZuesEntity e) {
        return host::particles_api::slice_age(ecs::Entity{e.index, e.generation});
    }
    float* host_particles_slice_field(ZuesEngine*, ZuesEntity e,
                                        const char* field) {
        return host::particles_api::slice_field(
            ecs::Entity{e.index, e.generation}, field);
    }

    void host_particles_for_each(ZuesEngine*, ZuesEntity e,
                                  ZuesParticleEachFn cb, void* user) {
        // Trampoline ecs::Entity -> ZuesEntity. The dt argument is
        // 0.0 here because the engine doesn't know the project's
        // current per-system dt -- the Lync plugin's wrapper
        // substitutes its own __zues_dt before invoking user code.
        struct Ctx { ZuesParticleEachFn cb; void* user; ZuesEntity e; };
        Ctx ctx{cb, user, e};
        auto trampoline = +[](ecs::Entity /*em*/, int idx, float dt, void* u) {
            auto* c = static_cast<Ctx*>(u);
            c->cb(c->e, idx, dt, c->user);
        };
        host::particles_api::for_each(
            ecs::Entity{e.index, e.generation}, trampoline,
            /*dt=*/0.0f, &ctx);
    }

    ZuesEntity host_get_child_at(ZuesEngine*, ZuesEntity e, uint32_t idx) {
        ZuesEntity z{0, 0};
        if (!g_world) return z;
        // Manual walk so we can stop at idx without iterating all children.
        ecs::Entity cur = g_world->first_child_of(ecs::Entity{e.index, e.generation});
        for (uint32_t i = 0; !cur.is_null(); ++i) {
            if (i == idx) return ZuesEntity{cur.index, cur.generation};
            cur = g_world->next_sibling_of(cur);
        }
        return z;
    }

    // Tick — called via the public Engine::editor::tick_timers forwarder
    // below from the editor main loop, once per frame between PreUpdate and
    // Update. Snapshots size at entry so callbacks that schedule new timers
    // don't re-fire within the same tick.
    void timer_tick_impl(float dt) {
        const size_t n = g_timers.size();
        for (size_t i = 0; i < n; ++i) {
            TimerEntry& t = g_timers[i];
            if (!t.alive) continue;
            t.remaining -= dt;
            if (t.remaining > 0.0f) continue;
            // Fire. Save the cb/user before modifying the entry — a one-shot's
            // callback might schedule another timer that grows g_timers and
            // invalidates our reference; copy to locals is cheap insurance.
            auto* cb = t.cb;
            void* u  = t.user;
            if (t.repeating) {
                // Re-arm by adding period (rather than `remaining = period`)
                // so cumulative drift is bounded by a single dt rather than
                // growing with frame rate variance.
                t.remaining += t.period;
                if (t.remaining <= 0.0f) t.remaining = t.period;  // period <= 0 fallback
            } else {
                t.alive = false;
                ++t.generation; if (t.generation == 0) t.generation = 1;
                t.cb = nullptr; t.user = nullptr;
            }
            if (cb) cb(u);
        }
    }
}

// =============================================================================
// Public API
// =============================================================================

ZuesHostApi build_host_api() {
    ZuesHostApi api{};
    api.abi_version         = ZUES_PROJECT_API_VERSION;
    api.get_service         = host_get_service;
    api.log                 = host_log;
    api.register_component  = host_register_component;
    api.find_component_id   = host_find_component_id;
    api.set_component_category = host_set_component_category;
    api.create_entity       = host_create_entity;
    api.destroy_entity      = host_destroy_entity;
    api.is_entity_alive     = host_is_entity_alive;
    api.add_component       = host_add_component;
    api.remove_component    = host_remove_component;
    api.get_component       = host_get_component;
    api.has_component       = host_has_component;
    api.add_system          = host_add_system;
    api.query_each          = host_query_each;
    api.add_system_with_domain = host_add_system_with_domain;
    api.add_sprite_default     = host_add_sprite_default;
    api.set_transform          = host_set_transform;
    api.set_transform_position = host_set_transform_position;
    api.get_transform          = host_get_transform;
    api.is_key_down            = host_is_key_down;
    api.is_key_pressed         = host_is_key_pressed;
    api.is_key_released        = host_is_key_released;
    api.mouse_pos              = host_mouse_pos;
    api.is_mouse_down          = host_is_mouse_down;
    api.is_mouse_pressed       = host_is_mouse_pressed;
    api.is_mouse_released      = host_is_mouse_released;
    api.mouse_wheel            = host_mouse_wheel;
    api.load_texture           = host_load_texture;
    api.add_sprite_textured    = host_add_sprite_textured;
    api.instantiate_prefab     = host_instantiate_prefab;
    api.ensure_singleton        = host_ensure_singleton;
    api.find_singleton          = host_find_singleton;
    api.world_version           = host_world_version;
    api.instantiate_prefab_guid = host_instantiate_prefab_guid;
    // v12 — timers + random
    api.set_timeout    = host_set_timeout;
    api.set_interval   = host_set_interval;
    api.cancel_timer   = host_cancel_timer;
    api.random_float   = host_random_float;
    api.random_range   = host_random_range;
    api.random_int     = host_random_int;
    api.random_seed    = host_random_seed;
    // v13 — hierarchy queries
    api.get_parent       = host_get_parent;
    api.get_child_count  = host_get_child_count;
    api.get_child_at     = host_get_child_at;
    api.get_first_child  = host_get_first_child;
    api.get_next_sibling = host_get_next_sibling;
    // v14 — text/HUD helpers
    api.add_text_default = host_add_text_default;
    api.set_text         = host_set_text;
    api.set_text_color   = host_set_text_color;
    // v15 -- Animator playback
    api.animator_play_by_name = host_animator_play_by_name;
    api.animator_set_playing  = host_animator_set_playing;
    api.animator_seek         = host_animator_seek;
    // v16 -- Particles
    api.particles_emit_burst  = host_particles_emit_burst;
    api.particles_set_playing = host_particles_set_playing;
    api.particles_restart     = host_particles_restart;
    // v17 -- pool inspection + extra-field access
    api.particles_count     = host_particles_count;
    api.particles_get_pos   = host_particles_get_pos;
    api.particles_set_pos   = host_particles_set_pos;
    api.particles_get_vel   = host_particles_get_vel;
    api.particles_set_vel   = host_particles_set_vel;
    api.particles_get_field = host_particles_get_field;
    api.particles_set_field = host_particles_set_field;
    api.particles_kill      = host_particles_kill;
    // v18 -- spatial query / bulk movement / batched iterator
    api.particles_nearest_neighbor = host_particles_nearest_neighbor;
    api.particles_step_toward      = host_particles_step_toward;
    api.particles_for_each         = host_particles_for_each;
    api.particles_slice_px         = host_particles_slice_px;
    api.particles_slice_py         = host_particles_slice_py;
    api.particles_slice_vx         = host_particles_slice_vx;
    api.particles_slice_vy         = host_particles_slice_vy;
    api.particles_slice_age        = host_particles_slice_age;
    api.particles_slice_field      = host_particles_slice_field;
    // v19 -- Audio
    api.audio_play_one_shot     = +[](ZuesEngine*, const char* path,
                                      float volume, float pitch) -> uint32_t {
        return audio_api::play_one_shot_path(path, volume, pitch);
    };
    api.audio_play_one_shot_at  = +[](ZuesEngine*, const char* path,
                                      float x, float y,
                                      float min_d, float max_d,
                                      float volume) -> uint32_t {
        return audio_api::play_one_shot_at(path, x, y, min_d, max_d, volume);
    };
    api.audio_stop_voice        = +[](ZuesEngine*, uint32_t v) {
        audio_api::stop_voice(v);
    };
    api.audio_source_play       = +[](ZuesEngine*, ZuesEntity e) {
        audio_api::source_play(ecs::Entity{e.index, e.generation});
    };
    api.audio_source_stop       = +[](ZuesEngine*, ZuesEntity e) {
        audio_api::source_stop(ecs::Entity{e.index, e.generation});
    };
    api.audio_source_pause      = +[](ZuesEngine*, ZuesEntity e, int p) {
        audio_api::source_pause(ecs::Entity{e.index, e.generation}, p);
    };
    api.audio_source_is_playing = +[](ZuesEngine*, ZuesEntity e) -> int {
        return audio_api::source_is_playing(ecs::Entity{e.index, e.generation});
    };
    api.audio_set_master_volume = +[](ZuesEngine*, float v) {
        audio_api::set_master_volume(v);
    };
    api.audio_master_volume     = +[](ZuesEngine*) -> float {
        return audio_api::master_volume();
    };
    api.audio_set_muted         = +[](ZuesEngine*, int m) {
        audio_api::set_muted(m);
    };
    api.audio_spawn             = +[](ZuesEngine*, uint64_t hi, uint64_t lo)
                                       -> ZuesEntity {
        Engine::Guid g{hi, lo};
        const ecs::Entity e = audio_api::spawn_for_cue(g);
        return ZuesEntity{ e.index, e.generation };
    };
    api.audio_spawn_3d          = +[](ZuesEngine*, uint64_t hi, uint64_t lo,
                                        float x, float y, float max_d)
                                       -> ZuesEntity {
        Engine::Guid g{hi, lo};
        const ecs::Entity e = audio_api::spawn_for_cue_3d(g, x, y, max_d);
        return ZuesEntity{ e.index, e.generation };
    };
    return api;
}

// Public timer tick. Editor main loop calls once per frame, between the
// PreUpdate and Update phase dispatches. Forwards to the anon-namespace impl
// that owns the timer list.
void tick_timers(float dt) { timer_tick_impl(dt); }

// Repopulate the world's singleton map by re-running ensure_singleton for
// every component id the project DLL previously registered as a singleton.
// Call this right after world.load_json (or any other path that wipes
// singletons) so the Hierarchy "Globals" section is correct from frame 1
// instead of waiting for the first Singleton<T>() call to adopt lazily.
void resync_singletons() {
    if (!g_world) return;
    for (auto id : g_singleton_ids) {
        (void)g_world->ensure_singleton(id);
    }
}

// Drop every live timer. Called from ProjectDllLoader::unload BEFORE the
// project DLL is FreeLibrary'd, because every timer's user-pointer is a
// function pointer INTO that DLL -- letting one fire after unload would
// dispatch into freed memory and silently terminate the process.
//
// Same reasoning as unregister_project_systems but for the timer table.
// Hot-reload then re-runs project on_load, which re-schedules whatever
// timers it wants from a clean slate.
void clear_timers() {
    for (auto& t : g_timers) {
        t.alive = false;
        t.cb    = nullptr;
        t.user  = nullptr;
    }
    g_timers.clear();
    // Singleton ids are tied to the project's component registrations;
    // those get cleared by world.unload_project_types(). Drop the cached
    // ids list too so a hot-reload doesn't try to ensure_singleton a
    // ComponentId that no longer exists.
    g_singleton_ids.clear();
}

ZuesEngine* engine_handle() {
    return reinterpret_cast<ZuesEngine*>(ENGINE_HANDLE_SENTINEL);
}

void set_host_context(HostContext* ctx) {
    g_host  = ctx;
    g_world = ctx ? ctx->world : nullptr;
}

HostContext* get_host_context() {
    return g_host;
}

void unregister_project_systems() {
    if (g_world) {
        for (const auto& h : g_system_handles) {
            g_world->remove_system(h);
        }
    }
    g_system_handles.clear();
    // Closures stay in g_system_closures — they're tiny POD and dropping the
    // unique_ptrs while the world might still hold the raw pointer would be
    // a use-after-free. Hot reload (4.x.b) will re-use the freed slots.
}

}  // namespace Engine::host
