#include <zues/guid.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace Engine {

namespace {

// SplitMix64 — short fast PRNG, good enough for ID generation.
struct SplitMix64 {
    u64 state;
    u64 next() {
        u64 z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

u64 seed_value() {
    std::random_device rd;
    u64 a = (u64(rd()) << 32) | rd();
    u64 t = static_cast<u64>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    u64 tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return a ^ t ^ (tid * 0x9E3779B97F4A7C15ULL);
}

thread_local SplitMix64 tls_rng{seed_value()};

}  // namespace

Guid guid_new() {
    Guid g;
    g.hi = tls_rng.next();
    g.lo = tls_rng.next();
    // Force non-null. The 0/0 case is reserved as a sentinel; one in 2^128
    // chance but bookkeeping is cheap.
    if (g.hi == 0 && g.lo == 0) g.lo = 1;
    return g;
}

std::string guid_to_hex(Guid g) {
    static constexpr char tab[] = "0123456789abcdef";
    std::string out;
    out.resize(32);
    auto write_u64 = [&](u64 v, std::size_t off) {
        for (int i = 15; i >= 0; --i) {
            out[off + i] = tab[v & 0xF];
            v >>= 4;
        }
    };
    write_u64(g.hi, 0);
    write_u64(g.lo, 16);
    return out;
}

Guid guid_from_hex(const char* hex, std::size_t len) {
    if (len != 32 || !hex) return NULL_GUID;
    auto nybble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    u64 hi = 0, lo = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        int n = nybble(hex[i]);
        if (n < 0) return NULL_GUID;
        hi = (hi << 4) | static_cast<u64>(n);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        int n = nybble(hex[16 + i]);
        if (n < 0) return NULL_GUID;
        lo = (lo << 4) | static_cast<u64>(n);
    }
    return {hi, lo};
}

}  // namespace Engine
