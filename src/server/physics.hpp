// physics.hpp -- Server Physics, Entity Movement, Collisions, and Monster Stepping
#pragma once

#include "server/server_types.hpp"

namespace Server {

void SV_CheckVelocity(edict_t* ent);
qboolean SV_RunThink(edict_t* ent);
void SV_Impact(edict_t* e1, edict_t* e2);
int ClipVelocity(const Vector3& in, const Vector3& normal, Vector3& out, float overbounce);
int SV_FlyMove(edict_t* ent, float time, trace_t* steptrace);
void SV_AddGravity(edict_t* ent);
trace_t SV_PushEntity(edict_t* ent, const Vector3& push);
void SV_PushMove(edict_t* pusher, float movetime);
void SV_Physics_Pusher(edict_t* ent);
void SV_CheckStuck(edict_t* ent);
qboolean SV_CheckWater(edict_t* ent);
void SV_WallFriction(edict_t* ent, trace_t* trace);
int SV_TryUnstick(edict_t* ent, const Vector3& oldvel);
void SV_WalkMove(edict_t* ent);
void SV_Physics_Client(edict_t* ent, int num);
void SV_Physics_None(edict_t* ent);
void SV_Physics_Noclip(edict_t* ent);
void SV_CheckWaterTransition(edict_t* ent);
void SV_Physics_Toss(edict_t* ent);
void SV_Physics_Step(edict_t* ent);
void SV_Physics();
[[nodiscard]] bool SV_CheckBottom(edict_t* ent);
bool SV_movestep(edict_t* ent, const Vector3& move, bool relink);
qboolean SV_StepDirection(edict_t* ent, float yaw, float dist);
void SV_FixCheckBottom(edict_t* ent);
void SV_NewChaseDir(edict_t* actor, edict_t* enemy, float dist);
qboolean SV_CloseEnough(edict_t* ent, edict_t* goal, float dist);
void SV_MoveToGoal();

} // namespace Server
