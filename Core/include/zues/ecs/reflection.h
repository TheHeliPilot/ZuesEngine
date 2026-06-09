#pragma once

// Component reflection metadata. Two paths populate ComponentFieldsOf<T>:
//
//  1. ZUES_HAS_REFLECTION = 1: clang-p2996 reflects the type automatically via
//     std::meta. The primary template introspects T's non-static data members
//     and maps each member's type to a FieldKind through field_kind_of.
//     ZUES_COMPONENT_FIELDS / _TAG macros become no-ops.
//
//  2. Otherwise: the user calls ZUES_COMPONENT_FIELDS(T, member_names...) at
//     file scope. Each member's kind is auto-deduced via field_kind_of<decltype>
//     — no per-field type spelling required.
//
// Either path produces the same FieldInfo array. The inspector switches on
// FieldKind (no string matching, no size guessing) so behaviour is identical
// regardless of which path generated the metadata.
//
// Adding support for a new editable type:
//   1. Add an entry to the FieldKind enum below.
//   2. Add a field_kind_of specialization mapping the C++ type to the kind.
//   3. Add a case to draw_field() in Editor/src/panel_inspector.cpp.
// Once those three places are touched, every component using the type lights
// up across the editor.

#include <zues/types.h>

#include <cstddef>      // offsetof, size_t
#include <type_traits>  // is_enum_v, enable_if_t

#if defined(ZUES_HAS_REFLECTION)
    #include <experimental/meta>
    #include <array>
#endif

// Forward decls — specialised below so reflection.h stays light on includes.
// Asset-ref specialisations live next to their type in zues/asset.h.
namespace Engine::math { struct vec2; struct color; }
namespace Engine::ecs  { struct Entity; struct EntityRef; }

namespace Engine::ecs {

// -----------------------------------------------------------------------------
// FieldKind — strong typing for inspector rendering.
// -----------------------------------------------------------------------------

enum class FieldKind : u8 {
    Unknown = 0,
    Bool,
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Vec2, Vec3, Vec4,
    Color,
    Entity,
    EntityRef,
    Handle,
    CharBuffer,
    Enum,
    // Asset references. All are 16-byte Guid wrappers but render distinct
    // drag-targets so a PrefabRef slot only accepts .zprefab files.
    PrefabRef,
    SpriteRef,
    TextureRef,
    AudioRef,
    FontRef,
    AnimationRef,
    AudioCueRef,
};

constexpr const char* kind_name(FieldKind k) {
    switch (k) {
        case FieldKind::Bool:       return "bool";
        case FieldKind::I8:         return "i8";
        case FieldKind::I16:        return "i16";
        case FieldKind::I32:        return "i32";
        case FieldKind::I64:        return "i64";
        case FieldKind::U8:         return "u8";
        case FieldKind::U16:        return "u16";
        case FieldKind::U32:        return "u32";
        case FieldKind::U64:        return "u64";
        case FieldKind::F32:        return "f32";
        case FieldKind::F64:        return "f64";
        case FieldKind::Vec2:       return "vec2";
        case FieldKind::Vec3:       return "vec3";
        case FieldKind::Vec4:       return "vec4";
        case FieldKind::Color:      return "color";
        case FieldKind::Entity:     return "Entity";
        case FieldKind::EntityRef:  return "EntityRef";
        case FieldKind::Handle:     return "Handle";
        case FieldKind::PrefabRef:  return "PrefabRef";
        case FieldKind::SpriteRef:  return "SpriteRef";
        case FieldKind::TextureRef: return "TextureRef";
        case FieldKind::AudioRef:   return "AudioRef";
        case FieldKind::FontRef:    return "FontRef";
        case FieldKind::AnimationRef: return "AnimationRef";
        case FieldKind::AudioCueRef:  return "AudioCueRef";
        case FieldKind::CharBuffer: return "char[N]";
        case FieldKind::Enum:       return "enum";
        case FieldKind::Unknown:
        default:                    return "?";
    }
}

// -----------------------------------------------------------------------------
// field_kind_of<T> — auto-deduces FieldKind from a member's declared type.
// Primary returns Unknown; partial specialisations cover enums + char[N];
// explicit specialisations cover all primitives and engine value types. Adding
// a new mapping is a one-liner.
// -----------------------------------------------------------------------------

template <class T, class = void>
struct field_kind_of { static constexpr FieldKind value = FieldKind::Unknown; };

// Any enum -> FieldKind::Enum (rendered as the underlying integer with a tag).
template <class T>
struct field_kind_of<T, std::enable_if_t<std::is_enum_v<T>>> {
    static constexpr FieldKind value = FieldKind::Enum;
};

// Fixed-size char buffers -> text input.
template <std::size_t N>
struct field_kind_of<char[N]> { static constexpr FieldKind value = FieldKind::CharBuffer; };

// Primitives.
template<> struct field_kind_of<bool> { static constexpr FieldKind value = FieldKind::Bool; };
template<> struct field_kind_of<i8>   { static constexpr FieldKind value = FieldKind::I8;   };
template<> struct field_kind_of<i16>  { static constexpr FieldKind value = FieldKind::I16;  };
template<> struct field_kind_of<i32>  { static constexpr FieldKind value = FieldKind::I32;  };
template<> struct field_kind_of<i64>  { static constexpr FieldKind value = FieldKind::I64;  };
template<> struct field_kind_of<u8>   { static constexpr FieldKind value = FieldKind::U8;   };
template<> struct field_kind_of<u16>  { static constexpr FieldKind value = FieldKind::U16;  };
template<> struct field_kind_of<u32>  { static constexpr FieldKind value = FieldKind::U32;  };
template<> struct field_kind_of<u64>  { static constexpr FieldKind value = FieldKind::U64;  };
template<> struct field_kind_of<f32>  { static constexpr FieldKind value = FieldKind::F32;  };
template<> struct field_kind_of<f64>  { static constexpr FieldKind value = FieldKind::F64;  };

// Engine value types. Forward declarations above are sufficient — the trait
// only needs the type identity, not the layout. The component declaration
// site already has the full definition (it's the one calling sizeof/offsetof).
template<> struct field_kind_of<Engine::math::vec2>  { static constexpr FieldKind value = FieldKind::Vec2;   };
template<> struct field_kind_of<Engine::math::color> { static constexpr FieldKind value = FieldKind::Color;  };
template<> struct field_kind_of<Engine::ecs::Entity>    { static constexpr FieldKind value = FieldKind::Entity;    };
template<> struct field_kind_of<Engine::ecs::EntityRef> { static constexpr FieldKind value = FieldKind::EntityRef; };
template<> struct field_kind_of<Engine::Handle>         { static constexpr FieldKind value = FieldKind::Handle;    };

template <class T>
inline constexpr FieldKind field_kind_of_v = field_kind_of<T>::value;

// -----------------------------------------------------------------------------
// EnumOptionInfo — one enumerator entry for inspector combo display.
// Populated by ZUES_REGISTER_ENUM; null/0 for unregistered enums.
// -----------------------------------------------------------------------------

struct EnumOptionInfo {
    const char* name;   // e.g. "OrderOnly"
    int         value;  // underlying integer value
};

// Trait: maps an enum type to its option list. Primary returns nullptr/0 for
// all types (non-enum or unregistered). ZUES_REGISTER_ENUM provides a
// specialisation that fills in real data.
template <typename T, typename = void>
struct enum_options_of {
    static constexpr const EnumOptionInfo* data  = nullptr;
    static constexpr u32                   count = 0;
};

// -----------------------------------------------------------------------------
// FieldInfo — one entry per field of a component.
// -----------------------------------------------------------------------------

struct FieldInfo {
    const char*            name;
    FieldKind              kind;
    u32                    offset;
    u32                    size;
    const EnumOptionInfo*  enum_options       = nullptr;
    u32                    enum_option_count  = 0;
};

// =============================================================================
// Reflection-driven path (ZUES_HAS_REFLECTION = 1, clang-p2996)
// =============================================================================

#if defined(ZUES_HAS_REFLECTION)

namespace internal {

// Build the FieldInfo array for T at compile time via P2996 std::meta. The
// member's kind is mapped through field_kind_of by splicing the reflected
// type back into the trait — same mapping table the macro path uses.
template <typename T>
consteval auto reflect_fields_of() {
    constexpr int N = [] consteval {
        return std::meta::nonstatic_data_members_of(
            ^^T, std::meta::access_context::unchecked()).size();
    }();

    std::array<FieldInfo, N> out{};
    if constexpr (N > 0) {
        auto members = std::meta::nonstatic_data_members_of(
            ^^T, std::meta::access_context::unchecked());
        for (int i = 0; i < N; ++i) {
            using MemberT = typename [: std::meta::type_of(members[i]) :];
            out[i].name              = std::meta::identifier_of(members[i]).data();
            out[i].kind              = field_kind_of_v<MemberT>;
            out[i].offset            = static_cast<u32>(std::meta::offset_of(members[i]).bytes);
            out[i].size              = static_cast<u32>(std::meta::size_of(members[i]));
            out[i].enum_options      = enum_options_of<MemberT>::data;
            out[i].enum_option_count = enum_options_of<MemberT>::count;
        }
    }
    return out;
}

}  // namespace internal

template <typename T>
struct ComponentFieldsOf {
    static constexpr auto _arr = internal::reflect_fields_of<T>();
    static constexpr const FieldInfo* fields = _arr.data();
    static constexpr u32 count = static_cast<u32>(_arr.size());
    static constexpr bool is_declared = true;
};

#else  // !ZUES_HAS_REFLECTION

template <typename T>
struct ComponentFieldsOf {};

#endif  // ZUES_HAS_REFLECTION

}  // namespace Engine::ecs

// =============================================================================
// FOR_EACH plumbing — supports up to 48 fields. Used only by the macro path.
// =============================================================================

#define ZUES__FE1(F, T, a)        F(T, a)
#define ZUES__FE2(F, T, a, ...)   F(T, a), ZUES__FE1(F, T, __VA_ARGS__)
#define ZUES__FE3(F, T, a, ...)   F(T, a), ZUES__FE2(F, T, __VA_ARGS__)
#define ZUES__FE4(F, T, a, ...)   F(T, a), ZUES__FE3(F, T, __VA_ARGS__)
#define ZUES__FE5(F, T, a, ...)   F(T, a), ZUES__FE4(F, T, __VA_ARGS__)
#define ZUES__FE6(F, T, a, ...)   F(T, a), ZUES__FE5(F, T, __VA_ARGS__)
#define ZUES__FE7(F, T, a, ...)   F(T, a), ZUES__FE6(F, T, __VA_ARGS__)
#define ZUES__FE8(F, T, a, ...)   F(T, a), ZUES__FE7(F, T, __VA_ARGS__)
#define ZUES__FE9(F, T, a, ...)   F(T, a), ZUES__FE8(F, T, __VA_ARGS__)
#define ZUES__FE10(F, T, a, ...)  F(T, a), ZUES__FE9(F, T, __VA_ARGS__)
#define ZUES__FE11(F, T, a, ...)  F(T, a), ZUES__FE10(F, T, __VA_ARGS__)
#define ZUES__FE12(F, T, a, ...)  F(T, a), ZUES__FE11(F, T, __VA_ARGS__)
#define ZUES__FE13(F, T, a, ...)  F(T, a), ZUES__FE12(F, T, __VA_ARGS__)
#define ZUES__FE14(F, T, a, ...)  F(T, a), ZUES__FE13(F, T, __VA_ARGS__)
#define ZUES__FE15(F, T, a, ...)  F(T, a), ZUES__FE14(F, T, __VA_ARGS__)
#define ZUES__FE16(F, T, a, ...)  F(T, a), ZUES__FE15(F, T, __VA_ARGS__)
#define ZUES__FE17(F, T, a, ...)  F(T, a), ZUES__FE16(F, T, __VA_ARGS__)
#define ZUES__FE18(F, T, a, ...)  F(T, a), ZUES__FE17(F, T, __VA_ARGS__)
#define ZUES__FE19(F, T, a, ...)  F(T, a), ZUES__FE18(F, T, __VA_ARGS__)
#define ZUES__FE20(F, T, a, ...)  F(T, a), ZUES__FE19(F, T, __VA_ARGS__)
#define ZUES__FE21(F, T, a, ...)  F(T, a), ZUES__FE20(F, T, __VA_ARGS__)
#define ZUES__FE22(F, T, a, ...)  F(T, a), ZUES__FE21(F, T, __VA_ARGS__)
#define ZUES__FE23(F, T, a, ...)  F(T, a), ZUES__FE22(F, T, __VA_ARGS__)
#define ZUES__FE24(F, T, a, ...)  F(T, a), ZUES__FE23(F, T, __VA_ARGS__)
#define ZUES__FE25(F, T, a, ...)  F(T, a), ZUES__FE24(F, T, __VA_ARGS__)
#define ZUES__FE26(F, T, a, ...)  F(T, a), ZUES__FE25(F, T, __VA_ARGS__)
#define ZUES__FE27(F, T, a, ...)  F(T, a), ZUES__FE26(F, T, __VA_ARGS__)
#define ZUES__FE28(F, T, a, ...)  F(T, a), ZUES__FE27(F, T, __VA_ARGS__)
#define ZUES__FE29(F, T, a, ...)  F(T, a), ZUES__FE28(F, T, __VA_ARGS__)
#define ZUES__FE30(F, T, a, ...)  F(T, a), ZUES__FE29(F, T, __VA_ARGS__)
#define ZUES__FE31(F, T, a, ...)  F(T, a), ZUES__FE30(F, T, __VA_ARGS__)
#define ZUES__FE32(F, T, a, ...)  F(T, a), ZUES__FE31(F, T, __VA_ARGS__)
#define ZUES__FE33(F, T, a, ...)  F(T, a), ZUES__FE32(F, T, __VA_ARGS__)
#define ZUES__FE34(F, T, a, ...)  F(T, a), ZUES__FE33(F, T, __VA_ARGS__)
#define ZUES__FE35(F, T, a, ...)  F(T, a), ZUES__FE34(F, T, __VA_ARGS__)
#define ZUES__FE36(F, T, a, ...)  F(T, a), ZUES__FE35(F, T, __VA_ARGS__)
#define ZUES__FE37(F, T, a, ...)  F(T, a), ZUES__FE36(F, T, __VA_ARGS__)
#define ZUES__FE38(F, T, a, ...)  F(T, a), ZUES__FE37(F, T, __VA_ARGS__)
#define ZUES__FE39(F, T, a, ...)  F(T, a), ZUES__FE38(F, T, __VA_ARGS__)
#define ZUES__FE40(F, T, a, ...)  F(T, a), ZUES__FE39(F, T, __VA_ARGS__)
#define ZUES__FE41(F, T, a, ...)  F(T, a), ZUES__FE40(F, T, __VA_ARGS__)
#define ZUES__FE42(F, T, a, ...)  F(T, a), ZUES__FE41(F, T, __VA_ARGS__)
#define ZUES__FE43(F, T, a, ...)  F(T, a), ZUES__FE42(F, T, __VA_ARGS__)
#define ZUES__FE44(F, T, a, ...)  F(T, a), ZUES__FE43(F, T, __VA_ARGS__)
#define ZUES__FE45(F, T, a, ...)  F(T, a), ZUES__FE44(F, T, __VA_ARGS__)
#define ZUES__FE46(F, T, a, ...)  F(T, a), ZUES__FE45(F, T, __VA_ARGS__)
#define ZUES__FE47(F, T, a, ...)  F(T, a), ZUES__FE46(F, T, __VA_ARGS__)
#define ZUES__FE48(F, T, a, ...)  F(T, a), ZUES__FE47(F, T, __VA_ARGS__)

#define ZUES__FE_GET( \
    _1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, \
    _17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32, \
    _33,_34,_35,_36,_37,_38,_39,_40,_41,_42,_43,_44,_45,_46,_47,_48, \
    N,...) ZUES__FE##N

#define ZUES__FOR_EACH(F, T, ...) \
    ZUES__FE_GET(__VA_ARGS__, \
        48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33, \
        32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17, \
        16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)(F, T, __VA_ARGS__)

// Each field entry: deduce the kind from decltype(member) via field_kind_of.
// All operands are constant expressions, so the resulting `fields[]` array can
// stay `static constexpr` — no static-init, no runtime cost.
#define ZUES__FIELD_ENTRY(T, member)                                                  \
    ::Engine::ecs::FieldInfo{                                                         \
        #member,                                                                      \
        ::Engine::ecs::field_kind_of_v<                                               \
            std::remove_reference_t<decltype(static_cast<T*>(nullptr)->member)>>,     \
        static_cast<::Engine::u32>(offsetof(T, member)),                              \
        static_cast<::Engine::u32>(sizeof(static_cast<T*>(nullptr)->member)),         \
        ::Engine::ecs::enum_options_of<                                               \
            std::remove_reference_t<decltype(static_cast<T*>(nullptr)->member)>>::data,  \
        ::Engine::ecs::enum_options_of<                                               \
            std::remove_reference_t<decltype(static_cast<T*>(nullptr)->member)>>::count  \
    }

// =============================================================================
// Public macros
// =============================================================================
//
// With ZUES_HAS_REFLECTION the macros are no-ops — the primary template
// auto-derives. With reflection off, the macros emit explicit specialisations
// of ComponentFieldsOf<T> with constexpr field tables.

#if defined(ZUES_HAS_REFLECTION)

    #define ZUES_COMPONENT_FIELDS(T, ...) static_assert(true, "")
    #define ZUES_COMPONENT_TAG(T)         static_assert(true, "")

#else

    // Components with one or more fields. List members by name only — kinds
    // are deduced automatically from the C++ types via field_kind_of.
    //   struct Position { float x, y; };
    //   ZUES_COMPONENT_FIELDS(Position, x, y);
    #define ZUES_COMPONENT_FIELDS(T, ...)                                              \
        namespace Engine::ecs {                                                        \
            template<> struct ComponentFieldsOf<T> {                                   \
                static constexpr ::Engine::ecs::FieldInfo fields[] = {                 \
                    ZUES__FOR_EACH(ZUES__FIELD_ENTRY, T, __VA_ARGS__)                  \
                };                                                                     \
                static constexpr ::Engine::u32 count = sizeof(fields) / sizeof(fields[0]); \
                static constexpr bool is_declared = true;                              \
            };                                                                         \
        }

    // Tag (zero-byte) components.
    //   struct Tag_Player {};
    //   ZUES_COMPONENT_TAG(Tag_Player);
    #define ZUES_COMPONENT_TAG(T)                                                      \
        namespace Engine::ecs {                                                        \
            template<> struct ComponentFieldsOf<T> {                                   \
                static constexpr const ::Engine::ecs::FieldInfo* fields = nullptr;     \
                static constexpr ::Engine::u32 count = 0;                              \
                static constexpr bool is_declared = true;                              \
            };                                                                         \
        }

#endif  // ZUES_HAS_REFLECTION

// =============================================================================
// ZUES_REGISTER_ENUM — attach named enumerator metadata to an enum type so the
// inspector renders it as a combo instead of a raw integer.
//
// Usage (at file scope, BEFORE ZUES_COMPONENT_FIELDS for types that use it):
//   ZUES_REGISTER_ENUM(MyNs::MyEnum,
//       ZUES_ENUM_OPTION("First",  0),
//       ZUES_ENUM_OPTION("Second", 1));
//
// Only enums with explicit registration get combo display. Unregistered enums
// fall back to the DragInt + "(enum)" tag.
// =============================================================================

// Wraps a single enumerator entry. Parentheses protect the inner comma from
// being parsed as a macro argument separator.
#define ZUES_ENUM_OPTION(label, val) \
    ::Engine::ecs::EnumOptionInfo{label, val}

// Specialises enum_options_of<EnumType> with the provided option list.
// EnumType must be fully qualified (e.g. Engine::components::SortMode).
#define ZUES_REGISTER_ENUM(EnumType, ...)                                        \
    namespace Engine::ecs {                                                       \
        template<> struct enum_options_of<EnumType> {                             \
            static constexpr ::Engine::ecs::EnumOptionInfo data_arr[] =           \
                { __VA_ARGS__ };                                                   \
            static constexpr const ::Engine::ecs::EnumOptionInfo* data =          \
                data_arr;                                                          \
            static constexpr ::Engine::u32 count =                                \
                static_cast<::Engine::u32>(                                        \
                    sizeof(data_arr) / sizeof(data_arr[0]));                       \
        };                                                                         \
    }
