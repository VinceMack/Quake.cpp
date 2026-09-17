// collision.hpp -- World collision, BSP hull tracing & raycasting
#pragma once

#include <cstdint>
#include "world/bsp_format.hpp"
#include "world/model.hpp"
#include "core/types.hpp"
#include "core/math.hpp"

struct edict_s;
using edict_t = edict_s;

//=============================================================================
// Collision Types
//=============================================================================

struct plane_t {
    Vector3 normal{};
    float dist{0.0f};
};

struct trace_t {
    qboolean allsolid{false};
    qboolean startsolid{false};
    qboolean inopen{false};
    qboolean inwater{false};
    float fraction{1.0f};
    Vector3 endpos{};
    plane_t plane{};
    edict_t* ent{nullptr};
};

enum class MoveMode : int {
    Normal = 0,
    NoMonsters = 1,
    Missile = 2
};

constexpr int MOVE_NORMAL = 0;
constexpr int MOVE_NOMONSTERS = 1;
constexpr int MOVE_MISSILE = 2;

namespace Collision {

void InitBoxHull();
hull_t* HullForBox(const Vector3& mins, const Vector3& maxs);
int HullPointContents(hull_t* hull, int num, const Vector3& p);
qboolean RecursiveHullCheck(hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace);

} // namespace Collision

// Backward compatibility inline forwarding
inline void SV_InitBoxHull() {
    Collision::InitBoxHull();
}

inline hull_t* SV_HullForBox(const Vector3& mins, const Vector3& maxs) {
    return Collision::HullForBox(mins, maxs);
}

inline int SV_HullPointContents(hull_t* hull, int num, const Vector3& p) {
    return Collision::HullPointContents(hull, num, p);
}

inline qboolean SV_RecursiveHullCheck(hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace) {
    return Collision::RecursiveHullCheck(hull, num, p1f, p2f, p1, p2, trace);
}
