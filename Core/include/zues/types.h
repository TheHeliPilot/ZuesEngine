#pragma once
#include <cstdint>

namespace Engine {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

// Generational handle. index + generation. Safe against use-after-free.
struct Handle {
    u32 index      = 0;
    u32 generation = 0;

    constexpr bool is_valid() const { return generation != 0; }
    constexpr bool operator==(const Handle&) const = default;
};

// DLL-boundary-safe result. Exceptions must not cross the module boundary.
enum class Result : u32 {
    Ok = 0,
    Error,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    OutOfMemory,
    AbiMismatch,
    NotImplemented,
};

}  // namespace Engine
