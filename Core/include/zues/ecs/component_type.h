#pragma once
#include <zues/api.h>
#include <zues/types.h>
#include <zues/ecs/reflection.h>

#include <cstring>
#include <type_traits>

namespace Engine::ecs {

using ComponentId = u32;
constexpr ComponentId INVALID_COMPONENT_ID = 0;   // ids start at 1

// Runtime descriptor for a component type. World owns one per registered type.
struct ComponentType {
    ComponentId       id          = INVALID_COMPONENT_ID;
    const char*       name        = "";
    u32               size        = 0;
    u32               align       = 0;

    // Field metadata — populated from ComponentFieldsOf<T>. `fields` is null
    // and `field_count` is 0 for tag types.
    const FieldInfo*  fields      = nullptr;
    u32               field_count = 0;

    // Move `size` bytes from src to dst. For our trivially-copyable
    // components this is memcpy; reflection will allow richer types later
    // without changing this struct.
    void (*move_ctor)(void* dst, void* src) = nullptr;
    void (*dtor)     (void* p)              = nullptr;

    // Prototype instance for default-initialization. Points to a static T{}
    // (engine builtins) or a deep-copied buffer (project-registered types).
    // null = fall back to memset(0). default_data_size must equal `size`.
    const void* default_data      = nullptr;
    u32         default_data_size = 0;

    // Optional menu/category path for the editor's Add Component picker.
    // Slash-separated, e.g. "Engine/Render", "Project/UI/Buttons". Empty
    // string = editor falls back to "Engine" for built-ins (set explicitly
    // by the editor at register time) or "Project" for everything else.
    // The component's `name` stays plain — category is purely UI metadata.
    const char* category = "";
};

// Build a ComponentType descriptor for T.
//
// Rules:
//  - Components must be trivially copyable + trivially destructible (POD).
//    Use fixed-size buffers + handles. No std::string / std::vector / owning
//    pointers. Keeps cross-DLL + hot-reload safe by construction.
//  - T must be declared via ZUES_COMPONENT_FIELDS or ZUES_COMPONENT_TAG (or
//    auto-reflected when ZUES_HAS_REFLECTION is on). The is_declared check
//    surfaces the missing declaration at compile time.
template <typename T>
ComponentType make_component_type(const char* name) {
    static_assert(std::is_trivially_copyable_v<T>,
        "Zues components must be trivially copyable. Use POD + handles only.");
    static_assert(std::is_trivially_destructible_v<T>,
        "Zues components must be trivially destructible.");
    static_assert(requires { ComponentFieldsOf<T>::is_declared; },
        "Component type T must be declared via ZUES_COMPONENT_FIELDS(T, ...) or "
        "ZUES_COMPONENT_TAG(T). When ZUES_HAS_REFLECTION is enabled, this is "
        "auto-generated from the type definition.");

    ComponentType t{};
    t.name        = name;
    t.size        = static_cast<u32>(sizeof(T));
    t.align       = static_cast<u32>(alignof(T));
    t.fields      = ComponentFieldsOf<T>::fields;
    t.field_count = ComponentFieldsOf<T>::count;
    t.move_ctor   = [](void* dst, void* src) { std::memcpy(dst, src, sizeof(T)); };
    t.dtor        = [](void*) { /* trivial */ };

    // Static prototype — value-initialized T so member defaults apply
    // (e.g. Sprite::tint = white, Camera2D::ortho_size = 10).
    static const T defaults{};
    t.default_data      = &defaults;
    t.default_data_size = sizeof(T);
    return t;
}

}  // namespace Engine::ecs
