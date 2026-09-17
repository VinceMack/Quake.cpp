// world.hpp -- Server World, Area Nodes, Entity Linking, and Collision
#pragma once

#include "server/server_types.hpp"

namespace Server {

void SV_ClearWorld();
void SV_UnlinkEdict(edict_t* ent);
void SV_LinkEdict(edict_t* ent, qboolean touch_triggers);
[[nodiscard]] int SV_PointContents(const Vector3& p);
[[nodiscard]] edict_t* SV_TestEntityPosition(edict_t* ent);

hull_t* SV_HullForEntity(edict_t* ent, const Vector3& mins, const Vector3& maxs, Vector3& offset);
trace_t SV_ClipMoveToEntity(
    edict_t* ent, const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end);
void SV_MoveBounds(const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end, Vector3& boxmins,
    Vector3& boxmaxs);
trace_t SV_Move(
    const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end, int type, edict_t* passedict);

} // namespace Server
