// world.h -- collision detection structures (trace, plane, hull)
#pragma once

typedef struct plane_s {
    Vector3 normal = {};
    float dist = 0.0f;
} plane_t;

typedef struct trace_s {
    qboolean allsolid = false;   // if true, plane is not valid
    qboolean startsolid = false; // if true, the initial point was in a solid area
    qboolean inopen = false, inwater = false;
    float fraction = 1.0f; // time completed, 1.0 = didn't hit anything
    Vector3 endpos = {};  // final position
    plane_t plane = {};  // surface normal at impact
    edict_t* ent = nullptr;   // entity the surface is on
} trace_t;

#define MOVE_NORMAL 0
#define MOVE_NOMONSTERS 1
#define MOVE_MISSILE 2

namespace Server {

void SV_ClearWorld(void);
// called after the world model has been loaded, before linking any entities

void SV_UnlinkEdict(edict_t* ent);
// call before removing an entity, and before trying to move one,
// so it doesn't clip against itself
// flags ent->v.modified

void SV_LinkEdict(edict_t* ent, qboolean touch_triggers);
// Needs to be called any time an entity changes origin, mins, maxs, or solid
// flags ent->v.modified
// sets ent->v.absmin and ent->v.absmax
// if touchtriggers, calls prog functions for the intersected triggers

int SV_PointContents(const Vector3& p);
// returns the CONTENTS_* value from the world at the given point.
// does not check any entities at all
// the non-true version remaps the water current contents to content_water

edict_t* SV_TestEntityPosition(edict_t* ent);

trace_t SV_Move(const Vector3& start,
    const Vector3& mins,
    const Vector3& maxs,
    const Vector3& end,
    int type,
    edict_t* passedict);
// mins and maxs are reletive

// if the entire move stays in a solid volume, trace.allsolid will be set

// if the starting point is in a solid, it will be allowed to move out
// to an open area

// nomonsters is used for line of sight or edge testing, where mosnters
// shouldn't be considered solid objects

// passedict is explicitly excluded from clipping checks (normally NULL)
qboolean SV_RecursiveHullCheck(const hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace);

} // namespace Server
