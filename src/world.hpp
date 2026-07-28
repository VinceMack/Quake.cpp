// world.hpp -- collision detection structures (trace, plane, hull)
#pragma once

#include "quakedef.hpp"

struct plane_t {
    Vector3 normal{};
    float dist{0.0f};
};

struct trace_t {
    qboolean allsolid{false};   // if true, plane is not valid
    qboolean startsolid{false}; // if true, the initial point was in a solid area
    qboolean inopen{false};
    qboolean inwater{false};
    float fraction{1.0f};       // time completed, 1.0 = didn't hit anything
    Vector3 endpos{};           // final position
    plane_t plane{};            // surface normal at impact
    edict_t* ent{nullptr};      // entity the surface is on
};

enum class MoveMode : int {
    Normal = 0,
    NoMonsters = 1,
    Missile = 2
};

constexpr int MOVE_NORMAL = 0;
constexpr int MOVE_NOMONSTERS = 1;
constexpr int MOVE_MISSILE = 2;

namespace Server {

void SV_ClearWorld();
void SV_UnlinkEdict(edict_t* ent);
void SV_LinkEdict(edict_t* ent, qboolean touch_triggers);

[[nodiscard]] int SV_PointContents(const Vector3& p);
[[nodiscard]] edict_t* SV_TestEntityPosition(edict_t* ent);

trace_t SV_Move(const Vector3& start,
    const Vector3& mins,
    const Vector3& maxs,
    const Vector3& end,
    int type,
    edict_t* passedict);

qboolean SV_RecursiveHullCheck(hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace);

} // namespace Server

