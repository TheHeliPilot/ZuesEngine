// Zues C++ ergonomics layer for project DLLs.
//
// Mirrors the Lync plugin's auto-injected names so C++ projects feel the
// same to write. Without C++26 reflection we can't auto-emit per-type
// `AddVelocity` / `EachVelocity`, so the equivalent uses templates:
//
//   zues::Add(e, Velocity{1.0f, 0.0f});
//   zues::Has<Velocity>(e);
//   zues::Get<Velocity>(e);     // returns Velocity*
//   zues::Remove<Velocity>(e);
//   zues::Each<Velocity>([](int e, Velocity* v) { ... });
//
// Boilerplate gone:
//
//   ZUES_REGISTER_COMPONENT(Velocity, "Combat/Stats");
//
// inside the project DLL adds Velocity to a global registration list.
// In your `on_load`, call `zues::register_all_components(eng, host)`
// once and every ZUES_REGISTER_COMPONENT'd type is registered with the
// engine + categorised.

#ifndef ZUES_CPP_HELPERS_H
#define ZUES_CPP_HELPERS_H

#ifdef __cplusplus

#include <zues/project_api.h>

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace zues {

// ---- Globals: bound by the project's on_load via zues::bind() ----------
inline ZuesEngine*        g_engine = nullptr;
inline const ZuesHostApi* g_host   = nullptr;

inline void bind(ZuesEngine* eng, const ZuesHostApi* host) {
    g_engine = eng;
    g_host   = host;
}

// Per-type stable name registered via ZUES_REGISTER_COMPONENT. Never
// instantiated for unregistered types - calling Add<T> / Has<T> / etc.
// on an unregistered type triggers a clean compile error.
template <typename T> struct component_name {
    static_assert(sizeof(T) == 0,
        "ZUES_REGISTER_COMPONENT(T, ...) must be invoked for T before "
        "calling zues::Add / Has / Get / Remove / Each on it.");
};

// ---- Auto-registration list --------------------------------------------
struct ComponentSpec {
    const char*          name;
    std::uint32_t        size;
    std::uint32_t        align;
    const ZuesFieldInfo* fields;
    std::uint32_t        fields_count;
    const char*          category;     // nullable = no category
    const void*          default_data; // nullable = zero-init
};

// Function-local static so registration order at startup is well-defined.
inline std::vector<ComponentSpec>& registry() {
    static std::vector<ComponentSpec> r;
    return r;
}

// Call this once from the project's on_load. Walks the registry and
// registers every ZUES_REGISTER_COMPONENT'd type with the engine, applying
// categories.
inline void register_all_components(ZuesEngine* eng, const ZuesHostApi* host) {
    for (const auto& s : registry()) {
        const ZuesComponentId id = host->register_component(eng,
            s.name, s.size, s.align, s.fields, s.fields_count, s.default_data);
        if (s.category && host->set_component_category)
            host->set_component_category(eng, id, s.category);
    }
}

// ---- Per-type helpers (mirror the Lync auto-injected family) ----------
namespace detail {
    inline ZuesEntity make_entity(int e_idx) {
        return ZuesEntity{static_cast<std::uint32_t>(e_idx), 1u};
    }
    template <typename T>
    inline ZuesComponentId id_of() {
        if (!g_host || !g_engine) return 0;
        return g_host->find_component_id(g_engine, component_name<T>::value);
    }
}

// Add: pass the whole struct value. Replaces lync's per-field AddX.
template <typename T>
inline void Add(int e_idx, const T& value) {
    if (!g_host) return;
    const auto id = detail::id_of<T>();
    if (id == 0) return;
    g_host->add_component(g_engine, detail::make_entity(e_idx), id, &value);
}

// Has -> bool. Returns false if T isn't registered (instead of crashing).
template <typename T>
inline bool Has(int e_idx) {
    if (!g_host) return false;
    const auto id = detail::id_of<T>();
    if (id == 0) return false;
    return g_host->has_component(g_engine, detail::make_entity(e_idx), id) != 0;
}

// Get -> T* into the engine's storage. Null if missing.
template <typename T>
inline T* Get(int e_idx) {
    if (!g_host) return nullptr;
    const auto id = detail::id_of<T>();
    if (id == 0) return nullptr;
    return static_cast<T*>(
        g_host->get_component(g_engine, detail::make_entity(e_idx), id));
}

template <typename T>
inline void Remove(int e_idx) {
    if (!g_host) return;
    const auto id = detail::id_of<T>();
    if (id == 0) return;
    g_host->remove_component(g_engine, detail::make_entity(e_idx), id);
}

// Each<T>(callback). Callback signature: void(int e_idx, T* component).
// Wraps the host's query_each, which uses an opaque void**; we adapt.
template <typename T, typename Fn>
inline void Each(Fn&& cb) {
    if (!g_host) return;
    const auto id = detail::id_of<T>();
    if (id == 0) return;
    static thread_local Fn* s_cb = nullptr;
    s_cb = &cb;
    auto thunk = +[](ZuesEntity e, void** cols, std::uint32_t /*n*/, void* /*user*/) {
        if (s_cb && cols && cols[0])
            (*s_cb)(static_cast<int>(e.index), static_cast<T*>(cols[0]));
    };
    ZuesComponentId ids[1] = { id };
    g_host->query_each(g_engine, ids, 1, nullptr, 0, thunk, nullptr);
    s_cb = nullptr;
}

}  // namespace zues

// ---- Macros ------------------------------------------------------------
//
// ZUES_REGISTER_COMPONENT(Type, "Category")
//
//   - Specialises zues::component_name<Type> with the type's name as a
//     C-string (used by all helpers + the registration call).
//   - Adds an entry to zues::registry() at static-init time. The entry
//     references zues_fields_of_<Type> + zues_fields_count_<Type> which
//     ZUES_PROJECT_FIELDS produced earlier.
//   - Pass nullptr (or "") for Category to skip the categorisation hint.
//
#define ZUES_REGISTER_COMPONENT(T, CategoryStr)                            \
    template <> struct ::zues::component_name<T> {                         \
        static constexpr const char* value = #T;                           \
    };                                                                     \
    namespace {                                                            \
        struct ZuesAutoReg_##T {                                           \
            ZuesAutoReg_##T() {                                            \
                ::zues::registry().push_back(::zues::ComponentSpec{        \
                    #T,                                                    \
                    static_cast<std::uint32_t>(sizeof(T)),                 \
                    static_cast<std::uint32_t>(alignof(T)),                \
                    zues_fields_of_##T,                                    \
                    zues_fields_count_##T,                                 \
                    (CategoryStr),                                         \
                    nullptr                                                \
                });                                                        \
            }                                                              \
        };                                                                 \
        static ZuesAutoReg_##T zues_auto_reg_##T;                          \
    }

#endif  // __cplusplus
#endif  // ZUES_CPP_HELPERS_H
