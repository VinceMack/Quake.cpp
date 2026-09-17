// types.hpp -- Core foundational types, intrusive list nodes, and numeric constants
#pragma once

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <utility>

#include <EASTL/variant.h>
#include <EASTL/span.h>

//============================================================================
// Expected Result Type (using EASTL variant)
//============================================================================

template <typename T, typename E>
class Expected {
public:
    constexpr Expected(const T& val) : data_(val) {}
    constexpr Expected(T&& val) : data_(std::move(val)) {}
    constexpr Expected(const E& err) : data_(err) {}
    constexpr Expected(E&& err) : data_(std::move(err)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return eastl::holds_alternative<T>(data_); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr const T& value() const& { assert(has_value()); return eastl::get<T>(data_); }
    [[nodiscard]] constexpr T& value() & { assert(has_value()); return eastl::get<T>(data_); }
    [[nodiscard]] constexpr const E& error() const& { assert(!has_value()); return eastl::get<E>(data_); }
    [[nodiscard]] constexpr E& error() & { assert(!has_value()); return eastl::get<E>(data_); }

    [[nodiscard]] constexpr const T& operator*() const& { return value(); }
    [[nodiscard]] constexpr T& operator*() & { return value(); }
    [[nodiscard]] constexpr const T* operator->() const { return &value(); }
    [[nodiscard]] constexpr T* operator->() { return &value(); }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& def) const& {
        return has_value() ? eastl::get<T>(data_) : static_cast<T>(std::forward<U>(def));
    }
    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& def) && {
        return has_value() ? std::move(eastl::get<T>(data_)) : static_cast<T>(std::forward<U>(def));
    }

private:
    eastl::variant<T, E> data_;
};

template <typename E>
class Expected<void, E> {
public:
    constexpr Expected() = default;
    constexpr Expected(const E& err) : error_(err), has_value_(false) {}
    constexpr Expected(E&& err) : error_(std::move(err)), has_value_(false) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value_; }
    [[nodiscard]] constexpr const E& error() const { assert(!has_value_); return error_; }

private:
    E error_{};
    bool has_value_{true};
};

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

enum class HunkType { Zone = 0, Hunk = 1, HunkTemp = 2, Cache = 3, Stack = 4 };

} // namespace Common

// Cache user handle
struct cache_user_s { void* data = nullptr; };
using cache_user_t = cache_user_s;

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

