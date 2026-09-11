// endian.hpp -- Endian detection and byte swapping primitives
#pragma once

#include "core/types.hpp"
#include <bit>

namespace Common {

extern bool bigendien;
extern short (*BigShort)(short l);
extern short (*LittleShort)(short l);
extern int (*BigLong)(int l);
extern int (*LittleLong)(int l);
extern float (*BigFloat)(float l);
extern float (*LittleFloat)(float l);

inline short ShortSwap(short l) {
    uint16_t u = static_cast<uint16_t>(l);
    return static_cast<short>((u >> 8) | (u << 8));
}
inline short ShortNoSwap(short l) { return l; }

inline int LongSwap(int l) {
    uint32_t u = static_cast<uint32_t>(l);
    return static_cast<int>(((u & 0xff000000u) >> 24) | ((u & 0x00ff0000u) >> 8) | ((u & 0x0000ff00u) << 8) | ((u & 0x000000ffu) << 24));
}
inline int LongNoSwap(int l) { return l; }

inline float FloatSwap(float f) {
    uint32_t u = LongSwap(std::bit_cast<int>(f));
    return std::bit_cast<float>(u);
}
inline float FloatNoSwap(float f) { return f; }

} // namespace Common
