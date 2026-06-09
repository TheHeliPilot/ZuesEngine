#pragma once

// 128-bit GUIDs for asset and reference identity. Stable across rename, move,
// and merge — the inverse of "identify by path", which breaks the moment a
// file moves. Stored as two u64s for trivial hashing/equality; serialized as
// 32-char lowercase hex so it diffs nicely in version control.
//
// One GUID per asset. For asset types we own (.zprefab/.zsprite/.zworld) the
// GUID lives at top level inside the JSON. For binary types we don't own
// (.png/.wav/.ttf) it lives in a `<asset>.meta` sidecar.
//
// Generation uses a thread-local SplitMix64 seeded from a one-time mix of
// std::random_device + high_resolution_clock + thread id. Not cryptographic —
// we just need enough collision resistance that a project of millions of
// assets has effectively zero risk of overlap.

#include <zues/api.h>
#include <zues/types.h>

#include <cstddef>
#include <cstring>
#include <string>

namespace Engine {

struct Guid {
    u64 hi = 0;
    u64 lo = 0;

    constexpr bool is_null() const { return hi == 0 && lo == 0; }
    constexpr bool operator==(const Guid&) const = default;
};

constexpr Guid NULL_GUID = {};

struct GuidHash {
    std::size_t operator()(Guid g) const noexcept {
        // FNV mixed split halves; std::hash on u64 is identity on libc++.
        u64 x = g.hi ^ (g.lo + 0x9e3779b97f4a7c15ULL + (g.hi << 6) + (g.hi >> 2));
        return static_cast<std::size_t>(x);
    }
};

// Random new GUID. Thread-safe.
ZUES_API Guid guid_new();

// Hex form: 32 lowercase chars, no dashes ("a3f2c1...").
ZUES_API std::string guid_to_hex(Guid g);

// Returns NULL_GUID on parse failure (wrong length, non-hex chars).
ZUES_API Guid guid_from_hex(const char* hex, std::size_t len);
inline   Guid guid_from_hex(const std::string& s) { return guid_from_hex(s.data(), s.size()); }

}  // namespace Engine
