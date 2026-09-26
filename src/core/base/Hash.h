#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace cad {

// 64-bit FNV-1a over bytes, finished with a splitmix64 avalanche. Used for
// content-addressed caching of recompute results.
inline uint64_t hashBytes(const void *data, size_t size, uint64_t seed = 0xcbf29ce484222325ull) {
    const auto *p = static_cast<const unsigned char *>(data);
    uint64_t h = seed;
    for(size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

inline uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

inline uint64_t hashString(std::string_view s, uint64_t seed = 0xcbf29ce484222325ull) {
    return mix64(hashBytes(s.data(), s.size(), seed));
}

inline uint64_t hashCombine(uint64_t a, uint64_t b) {
    return mix64(a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2)));
}

inline uint64_t hashDouble(double v) {
    if(v == 0.0) v = 0.0; // fold -0.0 into +0.0
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return mix64(bits);
}

} // namespace cad
