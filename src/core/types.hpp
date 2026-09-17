// types.hpp -- Core foundational types, intrusive list nodes, and numeric constants
#pragma once

#include <cstdint>
#include <cstddef>
#include <cassert>


//============================================================================
// Foundational Type Aliases
//============================================================================

#if !defined(BYTE_DEFINED)
using byte = unsigned char;
#define BYTE_DEFINED 1
#endif

using qboolean = bool;

//============================================================================
// Intrusive Doubly-Linked List Node
//============================================================================

struct link_t {
    link_t *prev = nullptr, *next = nullptr;
    constexpr void clear() noexcept { prev = next = this; }
    void remove() noexcept { if (next && prev) { next->prev = prev; prev->next = next; } }
    void insert_before(link_t* before) noexcept {
        if (!before) return;
        next = before; prev = before->prev;
        if (prev) prev->next = this;
        before->prev = this;
    }
};

#define STRUCT_FROM_LINK(l, t, m) ((t*)((byte*)l - (intptr_t)&(((t*)0)->m)))

namespace Common {

inline void ClearLink(link_t* l) { if (l) l->clear(); }
inline void RemoveLink(link_t* l) { if (l) l->remove(); }
inline void InsertLinkBefore(link_t* l, link_t* before) { if (l) l->insert_before(before); }

} // namespace Common

//============================================================================
// Numerical Bounds
//============================================================================

inline constexpr char Q_MAXCHAR = 0x7f, Q_MINCHAR = static_cast<char>(0x80);
inline constexpr short Q_MAXSHORT = 0x7fff, Q_MINSHORT = static_cast<short>(0x8000);
inline constexpr int Q_MAXINT = 0x7fffffff, Q_MININT = static_cast<int>(0x80000000);
inline constexpr int Q_MAXLONG = 0x7fffffff, Q_MINLONG = static_cast<int>(0x80000000);
inline constexpr int Q_MAXFLOAT = 0x7fffffff, Q_MINFLOAT = static_cast<int>(0x7fffffff);

inline constexpr int MAX_QPATH = 64;
inline constexpr int MAX_OSPATH = 128;

inline constexpr int MAX_EDICTS = 600;
inline constexpr int MAX_LIGHTSTYLES = 64;
inline constexpr int MAX_MODELS = 256;
inline constexpr int MAX_SOUNDS = 256;
inline constexpr int MAX_STYLESTRING = 64;

inline constexpr int MAX_SCOREBOARD = 16;
inline constexpr int MAX_SCOREBOARDNAME = 32;
inline constexpr int MAX_CL_STATS = 32;

