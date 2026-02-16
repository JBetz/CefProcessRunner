#pragma once

#include <rpc.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <functional>

inline bool operator<(const UUID& a, const UUID& b) {
  return std::memcmp(&a, &b, sizeof(UUID)) < 0;
}

namespace std {
template <>
struct hash<UUID> {
  size_t operator()(const UUID& g) const {
    // FNV-1a 64-bit
    constexpr uint64_t FNV_offset = 1469598103934665603ULL;
    constexpr uint64_t FNV_prime = 1099511628211ULL;
    uint64_t h = FNV_offset;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&g);
    for (size_t i = 0; i < sizeof(UUID); ++i) {
      h ^= static_cast<uint64_t>(p[i]);
      h *= FNV_prime;
    }
    return static_cast<size_t>(h);
  }
};
}  // namespace std