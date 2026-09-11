// physics.cpp -- Server Physics, Entity Movement, Collisions, and Monster Stepping Implementation
#include "quakedef.hpp"
#include "server/physics.hpp"
#include "server/world.hpp"

using namespace Common;
using namespace Console;
using namespace VM;
using namespace Host;
using namespace Math;

namespace VM {
void PF_changeyaw();
}

namespace Server {

static int c_yes = 0;
static int c_no = 0;

#define STEPSIZE 18

void SV_CheckVelocity(edict_t* ent)
{
    for (int i = 0; i < 3; ++i) {
        if (IS_NAN(ent->v.velocity[i])) {
            Con_Printf("Got a NaN velocity on %s\n", PR_GetString(ent->v.classname));
            ent->v.velocity[i] = 0.0f;
        }

        if (IS_NAN(ent->v.origin[i])) {
            Con_Printf("Got a NaN origin on %s\n", PR_GetString(ent->v.classname));
            ent->v.origin[i] = 0.0f;
        }

        if (ent->v.velocity[i] > sv_maxvelocity.value) ent->v.velocity[i] = sv_maxvelocity.value;
        else if (ent->v.velocity[i] < -sv_maxvelocity.value) ent->v.velocity[i] = -sv_maxvelocity.value;
    }
}

qboolean SV_RunThink(edict_t* ent)
{
    float thinktime = ent->v.nextthink;
    if (thinktime <= 0.0f || thinktime > sv.time + host_frametime) return true;

    if (thinktime < sv.time) thinktime = static_cast<float>(sv.time);

    ent->v.nextthink = 0;
    pr_global_struct->time = thinktime;
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
    pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    PR_ExecuteProgram(ent->v.think);

    return !ent->free;
}

void SV_Impact(edict_t* e1, edict_t* e2)
{
    const int old_self = pr_global_struct->self;
    const int old_other = pr_global_struct->other;

    pr_global_struct->time = static_cast<float>(sv.time);
    if (e1->v.touch && e1->v.solid != SOLID_NOT) {
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(e1));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(e2));
        PR_ExecuteProgram(e1->v.touch);
    }

    if (e2->v.touch && e2->v.solid != SOLID_NOT) {
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(e2));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(e1));
        PR_ExecuteProgram(e2->v.touch);
    }

    pr_global_struct->self = old_self;
    pr_global_struct->other = old_other;
}

constexpr float STOP_EPSILON = 0.1f;

int ClipVelocity(const Vector3& in, const Vector3& normal, Vector3& out, float overbounce)
{
    int blocked = 0;
    if (normal.z > 0.0f) blocked |= 1;
    if (normal.z == 0.0f) blocked |= 2;

    const float backoff = in.dot(normal) * overbounce;
    out = in - normal * backoff;
    for (int i = 0; i < 3; ++i) {
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) out[i] = 0.0f;
    }

    return blocked;
}

constexpr int MAX_CLIP_PLANES = 5;

int SV_FlyMove(edict_t* ent, float time, trace_t* steptrace)
{
    constexpr int numbumps = 4;
    int blocked = 0;
    Vector3 original_velocity = ent->v.velocity;
    Vector3 primal_velocity = ent->v.velocity;
    int numplanes = 0;
    eastl::array<Vector3, MAX_CLIP_PLANES> planes{};

    float time_left = time;

    for (int bumpcount = 0; bumpcount < numbumps; ++bumpcount) {
        if (ent->v.velocity == vec3_origin) break;

        const Vector3 end = ent->v.origin + ent->v.velocity * time_left;
        trace_t trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

        if (trace.allsolid) {
            ent->v.velocity = vec3_origin;
            return 3;
        }

        if (trace.fraction > 0.0f) {
            ent->v.origin = trace.endpos;
            original_velocity = ent->v.velocity;
            numplanes = 0;
        }

        if (trace.fraction == 1.0f) break;
        if (!trace.ent) Sys_Error("SV_FlyMove: !trace.ent");

        if (trace.plane.normal.z > 0.7f) {
            blocked |= 1;
            if (trace.ent->v.solid == SOLID_BSP) {
                ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
                ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));
            }
        }

        if (trace.plane.normal.z == 0.0f) {
            blocked |= 2;
            if (steptrace) *steptrace = trace;
        }

        SV_Impact(ent, trace.ent);
        if (ent->free) break;

        time_left -= time_left * trace.fraction;

        if (numplanes >= MAX_CLIP_PLANES) {
            ent->v.velocity = vec3_origin;
            return 3;
        }

        planes[static_cast<size_t>(numplanes++)] = trace.plane.normal;

        int i = 0;
        Vector3 new_velocity{};
        for (; i < numplanes; ++i) {
            ClipVelocity(original_velocity, planes[static_cast<size_t>(i)], new_velocity, 1.0f);
            int j = 0;
            for (; j < numplanes; ++j) {
                if (j != i) {
                    if (new_velocity.dot(planes[static_cast<size_t>(j)]) < 0.0f) break;
                }
            }
            if (j == numplanes) break;
        }

        if (i != numplanes) {
            ent->v.velocity = new_velocity;
        } else {
            if (numplanes != 2) {
                ent->v.velocity = vec3_origin;
                return 7;
            }

            const Vector3 dir = planes[0].cross(planes[1]);
            const float d = dir.dot(ent->v.velocity);
            ent->v.velocity = dir * d;
        }

        if (ent->v.velocity.dot(primal_velocity) <= 0.0f) {
            ent->v.velocity = vec3_origin;
            return blocked;
        }
    }

    return blocked;
}

void SV_AddGravity(edict_t* ent)
{
    float ent_gravity = 1.0f;
    const eval_t* val = GetEdictFieldValue(ent, "gravity");
    if (val && val->_float) ent_gravity = val->_float;

    ent->v.velocity[2] -= static_cast<float>(ent_gravity * sv_gravity.value * host_frametime);
}

trace_t SV_PushEntity(edict_t* ent, const Vector3& push)
{
    const Vector3 end = ent->v.origin + push;
    trace_t trace{};

    if (ent->v.movetype == MOVETYPE_FLYMISSILE) {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
    } else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT) {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
    } else {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
    }

    ent->v.origin = trace.endpos;
    SV_LinkEdict(ent, true);

    if (trace.ent) SV_Impact(ent, trace.ent);

    return trace;
}

void SV_PushMove(edict_t* pusher, float movetime)
{
    if (pusher->v.velocity == vec3_origin) {
        pusher->v.ltime += movetime;
        return;
    }

    const Vector3 move = pusher->v.velocity * movetime;
    const Vector3 mins = pusher->v.absmin + move;
    const Vector3 maxs = pusher->v.absmax + move;
    const Vector3 pushorig = pusher->v.origin;

    pusher->v.origin += move;
    pusher->v.ltime += movetime;
    SV_LinkEdict(pusher, false);

    int num_moved = 0;
    eastl::array<edict_t*, MAX_EDICTS> moved_edict{};
    eastl::array<Vector3, MAX_EDICTS> moved_from{};

    edict_t* check = NEXT_EDICT(sv.edicts);
    for (int e = 1; e < sv.num_edicts; ++e, check = NEXT_EDICT(check)) {
        if (check->free) continue;
        if (check->v.movetype == MOVETYPE_PUSH || check->v.movetype == MOVETYPE_NONE || check->v.movetype == MOVETYPE_NOCLIP) continue;

        if (!((static_cast<int>(check->v.flags) & FL_ONGROUND) && PROG_TO_EDICT(check->v.groundentity) == pusher)) {
            if (check->v.absmin.x >= maxs.x || check->v.absmin.y >= maxs.y || check->v.absmin.z >= maxs.z ||
                check->v.absmax.x <= mins.x || check->v.absmax.y <= mins.y || check->v.absmax.z <= mins.z) {
                continue;
            }
            if (!SV_TestEntityPosition(check)) continue;
        }

        if (check->v.movetype != MOVETYPE_WALK) {
            check->v.flags = static_cast<float>(static_cast<int>(check->v.flags) & ~FL_ONGROUND);
        }

        const Vector3 entorig = check->v.origin;
        moved_from[static_cast<size_t>(num_moved)] = check->v.origin;
        moved_edict[static_cast<size_t>(num_moved)] = check;
        num_moved++;

        pusher->v.solid = SOLID_NOT;
        SV_PushEntity(check, move);
        pusher->v.solid = SOLID_BSP;

        edict_t* block = SV_TestEntityPosition(check);
        if (block) {
            if (check->v.mins.x == check->v.maxs.x) continue;
            if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER) {
                check->v.mins.x = check->v.mins.y = 0;
                check->v.maxs = check->v.mins;
                continue;
            }

            check->v.origin = entorig;
            SV_LinkEdict(check, true);

            pusher->v.origin = pushorig;
            SV_LinkEdict(pusher, false);
            pusher->v.ltime -= movetime;

            if (pusher->v.blocked) {
                pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(pusher));
                pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(check));
                PR_ExecuteProgram(pusher->v.blocked);
            }

            for (int i = 0; i < num_moved; ++i) {
                moved_edict[static_cast<size_t>(i)]->v.origin = moved_from[static_cast<size_t>(i)];
                SV_LinkEdict(moved_edict[static_cast<size_t>(i)], false);
            }
            return;
        }
    }
}

void SV_Physics_Pusher(edict_t* ent)
{
    float thinktime = ent->v.nextthink;
    float oldltime = ent->v.ltime;
    float movetime;

    if (thinktime < ent->v.ltime + host_frametime) {
        movetime = thinktime - ent->v.ltime;
        if (movetime < 0) movetime = 0;
    } else {
        movetime = static_cast<float>(host_frametime);
    }

    if (movetime) SV_PushMove(ent, movetime);

    if (thinktime > oldltime && thinktime <= ent->v.ltime) {
        ent->v.nextthink = 0;
        pr_global_struct->time = static_cast<float>(sv.time);
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(sv.edicts));
        PR_ExecuteProgram(ent->v.think);
        if (ent->free) return;
    }
}

void SV_CheckStuck(edict_t* ent)
{
    int i, j, z;
    Vector3 org;

    if (!SV_TestEntityPosition(ent)) {
        ent->v.oldorigin = ent->v.origin;
        return;
    }

    org = ent->v.origin;
    ent->v.origin = ent->v.oldorigin;
    if (!SV_TestEntityPosition(ent)) {
        Con_DPrintf("Unstuck.\n");
        SV_LinkEdict(ent, true);
        return;
    }

    for (z = 0; z < 18; z++) {
        for (i = -1; i <= 1; i++) {
            for (j = -1; j <= 1; j++) {
                ent->v.origin.x = org.x + i;
                ent->v.origin.y = org.y + j;
                ent->v.origin.z = org.z + z;
                if (!SV_TestEntityPosition(ent)) {
                    Con_DPrintf("Unstuck.\n");
                    SV_LinkEdict(ent, true);
                    return;
                }
            }
        }
    }

    ent->v.origin = org;
    Con_DPrintf("player is stuck.\n");
}

qboolean SV_CheckWater(edict_t* ent)
{
    Vector3 point(ent->v.origin.x, ent->v.origin.y, ent->v.origin.z + ent->v.mins.z + 1);

    ent->v.waterlevel = 0;
    ent->v.watertype = CONTENTS_EMPTY;
    int cont = SV_PointContents(point);
    if (cont <= CONTENTS_WATER) {
        ent->v.watertype = static_cast<float>(cont);
        ent->v.waterlevel = 1;
        point.z = ent->v.origin.z + (ent->v.mins.z + ent->v.maxs.z) * 0.5f;
        cont = SV_PointContents(point);
        if (cont <= CONTENTS_WATER) {
            ent->v.waterlevel = 2;
            point.z = ent->v.origin.z + ent->v.view_ofs.z;
            cont = SV_PointContents(point);
            if (cont <= CONTENTS_WATER) {
                ent->v.waterlevel = 3;
            }
        }
    }

    return ent->v.waterlevel > 1;
}

void SV_WallFriction(edict_t* ent, trace_t* trace)
{
    Vector3 forward, right, up, into, side;
    float d, i;

    AngleVectors(ent->v.v_angle, forward, right, up);
    d = trace->plane.normal.dot(forward);

    d += 0.5f;
    if (d >= 0) return;

    i = trace->plane.normal.dot(ent->v.velocity);
    into = trace->plane.normal * i;
    side = ent->v.velocity - into;

    ent->v.velocity.x = side.x * (1 + d);
    ent->v.velocity.y = side.y * (1 + d);
}

int SV_TryUnstick(edict_t* ent, const Vector3& oldvel)
{
    int i, clip;
    Vector3 oldorg, dir;
    trace_t steptrace;

    oldorg = ent->v.origin;
    dir = vec3_origin;

    for (i = 0; i < 8; i++) {
        switch (i) {
        case 0: dir.x = 2; dir.y = 0; break;
        case 1: dir.x = 0; dir.y = 2; break;
        case 2: dir.x = -2; dir.y = 0; break;
        case 3: dir.x = 0; dir.y = -2; break;
        case 4: dir.x = 2; dir.y = 2; break;
        case 5: dir.x = -2; dir.y = 2; break;
        case 6: dir.x = 2; dir.y = -2; break;
        case 7: dir.x = -2; dir.y = -2; break;
        }

        SV_PushEntity(ent, dir);
        ent->v.velocity = Vector3(oldvel.x, oldvel.y, 0.0f);
        clip = SV_FlyMove(ent, 0.1f, &steptrace);

        if (fabs(oldorg.y - ent->v.origin.y) > 4 || fabs(oldorg.x - ent->v.origin.x) > 4) {
            return clip;
        }

        ent->v.origin = oldorg;
    }

    ent->v.velocity = vec3_origin;
    return 7;
}

void SV_WalkMove(edict_t* ent)
{
    Vector3 upmove, downmove, oldorg, oldvel, nosteporg, nostepvel;
    int clip, oldonground;
    trace_t steptrace, downtrace;

    oldonground = static_cast<int>(ent->v.flags) & FL_ONGROUND;
    ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) & ~FL_ONGROUND);

    oldorg = ent->v.origin;
    oldvel = ent->v.velocity;

    clip = SV_FlyMove(ent, static_cast<float>(host_frametime), &steptrace);

    if (!(clip & 2)) return;
    if (!oldonground && ent->v.waterlevel == 0) return;
    if (ent->v.movetype != MOVETYPE_WALK) return;
    if (sv_nostep.value) return;
    if (static_cast<int>(sv_player->v.flags) & FL_WATERJUMP) return;

    nosteporg = ent->v.origin;
    nostepvel = ent->v.velocity;

    ent->v.origin = oldorg;

    upmove = vec3_origin;
    downmove = vec3_origin;
    upmove.z = STEPSIZE;
    downmove.z = static_cast<float>(-STEPSIZE + oldvel.z * host_frametime);

    SV_PushEntity(ent, upmove);

    ent->v.velocity = Vector3(oldvel.x, oldvel.y, 0.0f);
    clip = SV_FlyMove(ent, static_cast<float>(host_frametime), &steptrace);

    if (clip) {
        if (fabs(oldorg.y - ent->v.origin.y) < 0.03125 && fabs(oldorg.x - ent->v.origin.x) < 0.03125) {
            clip = SV_TryUnstick(ent, oldvel);
        }
    }

    if (clip & 2) SV_WallFriction(ent, &steptrace);

    downtrace = SV_PushEntity(ent, downmove);

    if (downtrace.plane.normal.z > 0.7) {
        if (ent->v.solid == SOLID_BSP) {
            ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
            ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(downtrace.ent));
        }
    } else {
        ent->v.origin = nosteporg;
        ent->v.velocity = nostepvel;
    }
}

void SV_Physics_Client(edict_t* ent, int num)
{
    if (!svs.GetClients()[static_cast<size_t>(num - 1)].active) return;

    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
    PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

    SV_CheckVelocity(ent);

    switch (static_cast<int>(ent->v.movetype)) {
    case MOVETYPE_NONE:
        if (!SV_RunThink(ent)) return;
        break;

    case MOVETYPE_WALK:
        if (!SV_RunThink(ent)) return;
        if (!SV_CheckWater(ent) && !(static_cast<int>(ent->v.flags) & FL_WATERJUMP)) {
            SV_AddGravity(ent);
        }
        SV_CheckStuck(ent);
        SV_WalkMove(ent);
        break;

    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
        SV_Physics_Toss(ent);
        break;

    case MOVETYPE_FLY:
        if (!SV_RunThink(ent)) return;
        SV_FlyMove(ent, static_cast<float>(host_frametime), nullptr);
        break;

    case MOVETYPE_NOCLIP:
        if (!SV_RunThink(ent)) return;
        VectorMA(ent->v.origin, static_cast<float>(host_frametime), ent->v.velocity, ent->v.origin);
        break;

    default:
        Sys_Error("SV_Physics_client: bad movetype %i", static_cast<int>(ent->v.movetype));
    }

    SV_LinkEdict(ent, true);
    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
}

void SV_Physics_None(edict_t* ent)
{
    SV_RunThink(ent);
}

void SV_Physics_Noclip(edict_t* ent)
{
    if (!SV_RunThink(ent)) return;

    VectorMA(ent->v.angles, static_cast<float>(host_frametime), ent->v.avelocity, ent->v.angles);
    VectorMA(ent->v.origin, static_cast<float>(host_frametime), ent->v.velocity, ent->v.origin);
    SV_LinkEdict(ent, false);
}

void SV_CheckWaterTransition(edict_t* ent)
{
    const int cont = SV_PointContents(ent->v.origin);
    if (!ent->v.watertype) {
        ent->v.watertype = static_cast<float>(cont);
        ent->v.waterlevel = 1;
        return;
    }

    if (cont <= CONTENTS_WATER) {
        if (ent->v.watertype == CONTENTS_EMPTY) SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        ent->v.watertype = static_cast<float>(cont);
        ent->v.waterlevel = 1;
    } else {
        if (ent->v.watertype != CONTENTS_EMPTY) SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        ent->v.watertype = static_cast<float>(CONTENTS_EMPTY);
        ent->v.waterlevel = static_cast<float>(cont);
    }
}

void SV_Physics_Toss(edict_t* ent)
{
    if (!SV_RunThink(ent)) return;
    if (static_cast<int>(ent->v.flags) & FL_ONGROUND) return;

    SV_CheckVelocity(ent);

    if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE) {
        SV_AddGravity(ent);
    }

    ent->v.angles += ent->v.avelocity * static_cast<float>(host_frametime);

    const Vector3 move = ent->v.velocity * static_cast<float>(host_frametime);
    const trace_t trace = SV_PushEntity(ent, move);
    if (trace.fraction == 1.0f || ent->free) return;

    const float backoff = (ent->v.movetype == MOVETYPE_BOUNCE) ? 1.5f : 1.0f;
    ClipVelocity(ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

    if (trace.plane.normal.z > 0.7f) {
        if (ent->v.velocity.z < 60.0f || ent->v.movetype != MOVETYPE_BOUNCE) {
            ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
            ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));
            ent->v.velocity = vec3_origin;
            ent->v.avelocity = vec3_origin;
        }
    }

    SV_CheckWaterTransition(ent);
}

void SV_Physics_Step(edict_t* ent)
{
    if (!(static_cast<int>(ent->v.flags) & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        const bool hitsound = (ent->v.velocity.z < sv_gravity.value * -0.1f);

        SV_AddGravity(ent);
        SV_CheckVelocity(ent);
        SV_FlyMove(ent, static_cast<float>(host_frametime), nullptr);
        SV_LinkEdict(ent, true);

        if (static_cast<int>(ent->v.flags) & FL_ONGROUND) {
            if (hitsound) SV_StartSound(ent, 0, "demon/dland2.wav", 255, 1);
        }
    }

    SV_RunThink(ent);
    SV_CheckWaterTransition(ent);
}

void SV_Physics()
{
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    pr_global_struct->time = static_cast<float>(sv.time);
    PR_ExecuteProgram(pr_global_struct->StartFrame);

    edict_t* ent = sv.edicts;
    for (int i = 0; i < sv.num_edicts; ++i, ent = NEXT_EDICT(ent)) {
        if (ent->free) continue;
        if (pr_global_struct->force_retouch) SV_LinkEdict(ent, true);

        if (i > 0 && i <= svs.maxclients) {
            SV_Physics_Client(ent, i);
        } else if (ent->v.movetype == MOVETYPE_PUSH) {
            SV_Physics_Pusher(ent);
        } else if (ent->v.movetype == MOVETYPE_NONE) {
            SV_Physics_None(ent);
        } else if (ent->v.movetype == MOVETYPE_NOCLIP) {
            SV_Physics_Noclip(ent);
        } else if (ent->v.movetype == MOVETYPE_STEP) {
            SV_Physics_Step(ent);
        } else if (ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_BOUNCE
            || ent->v.movetype == MOVETYPE_FLY || ent->v.movetype == MOVETYPE_FLYMISSILE) {
            SV_Physics_Toss(ent);
        } else {
            Sys_Error("SV_Physics: bad movetype %i", static_cast<int>(ent->v.movetype));
        }
    }

    if (pr_global_struct->force_retouch) pr_global_struct->force_retouch--;

    sv.time += host_frametime;
}

bool SV_CheckBottom(edict_t* ent)
{
    Vector3 mins = ent->v.origin + ent->v.mins;
    Vector3 maxs = ent->v.origin + ent->v.maxs;
    Vector3 start{}, stop{};

    start.z = mins.z - 1.0f;
    for (int x = 0; x <= 1; ++x) {
        for (int y = 0; y <= 1; ++y) {
            start.x = x ? maxs.x : mins.x;
            start.y = y ? maxs.y : mins.y;
            if (SV_PointContents(start) != CONTENTS_SOLID) goto realcheck;
        }
    }

    c_yes++;
    return true;

realcheck:
    c_no++;
    start.z = mins.z;
    start.x = stop.x = (mins.x + maxs.x) * 0.5f;
    start.y = stop.y = (mins.y + maxs.y) * 0.5f;
    stop.z = start.z - 2.0f * STEPSIZE;
    trace_t trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, ent);

    if (trace.fraction == 1.0f) return false;

    const float mid = trace.endpos.z;
    float bottom = trace.endpos.z;

    for (int x = 0; x <= 1; ++x) {
        for (int y = 0; y <= 1; ++y) {
            start.x = stop.x = x ? maxs.x : mins.x;
            start.y = stop.y = y ? maxs.y : mins.y;

            trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, ent);

            if (trace.fraction != 1.0f && trace.endpos.z > bottom) bottom = trace.endpos.z;
            if (trace.fraction == 1.0f || mid - trace.endpos.z > STEPSIZE) return false;
        }
    }

    c_yes++;
    return true;
}

bool SV_movestep(edict_t* ent, const Vector3& move, bool relink)
{
    const Vector3 oldorg = ent->v.origin;
    Vector3 neworg = ent->v.origin + move;

    if (static_cast<int>(ent->v.flags) & (FL_SWIM | FL_FLY)) {
        for (int i = 0; i < 2; ++i) {
            neworg = ent->v.origin + move;
            const edict_t* enemy = PROG_TO_EDICT(ent->v.enemy);
            if (i == 0 && enemy != sv.edicts) {
                const float dz = ent->v.origin.z - PROG_TO_EDICT(ent->v.enemy)->v.origin.z;
                if (dz > 40.0f) neworg.z -= 8.0f;
                if (dz < 30.0f) neworg.z += 8.0f;
            }

            trace_t trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, neworg, false, ent);

            if (trace.fraction == 1.0f) {
                if ((static_cast<int>(ent->v.flags) & FL_SWIM) && SV_PointContents(trace.endpos) == CONTENTS_EMPTY) {
                    return false;
                }

                ent->v.origin = trace.endpos;
                if (relink) SV_LinkEdict(ent, true);
                return true;
            }

            if (enemy == sv.edicts) break;
        }

        return false;
    }

    neworg.z += STEPSIZE;
    Vector3 end = neworg;
    end.z -= STEPSIZE * 2;

    trace_t trace = SV_Move(neworg, ent->v.mins, ent->v.maxs, end, false, ent);

    if (trace.allsolid) return false;

    if (trace.startsolid) {
        neworg.z -= STEPSIZE;
        trace = SV_Move(neworg, ent->v.mins, ent->v.maxs, end, false, ent);
        if (trace.allsolid || trace.startsolid) return false;
    }

    if (trace.fraction == 1.0f) {
        if (static_cast<int>(ent->v.flags) & FL_PARTIALGROUND) {
            ent->v.origin += move;
            if (relink) SV_LinkEdict(ent, true);
            ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) & ~FL_ONGROUND);
            return true;
        }

        return false;
    }

    ent->v.origin = trace.endpos;

    if (!SV_CheckBottom(ent)) {
        if (static_cast<int>(ent->v.flags) & FL_PARTIALGROUND) {
            if (relink) SV_LinkEdict(ent, true);
            return true;
        }

        ent->v.origin = oldorg;
        return false;
    }

    if (static_cast<int>(ent->v.flags) & FL_PARTIALGROUND) {
        ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) & ~FL_PARTIALGROUND);
    }

    ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));

    if (relink) SV_LinkEdict(ent, true);

    return true;
}

qboolean SV_StepDirection(edict_t* ent, float yaw, float dist)
{
    ent->v.ideal_yaw = yaw;
    VM::PF_changeyaw();

    const float rad_yaw = static_cast<float>(yaw * M_PI * 2.0 / 360.0);
    const Vector3 move(cosf(rad_yaw) * dist, sinf(rad_yaw) * dist, 0.0f);

    const Vector3 oldorigin = ent->v.origin;
    if (SV_movestep(ent, move, false)) {
        const float delta = ent->v.angles[YAW] - ent->v.ideal_yaw;
        if (delta > 45.0f && delta < 315.0f) {
            ent->v.origin = oldorigin;
        }

        SV_LinkEdict(ent, true);
        return true;
    }

    SV_LinkEdict(ent, true);
    return false;
}

void SV_FixCheckBottom(edict_t* ent)
{
    ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_PARTIALGROUND);
}

constexpr float DI_NODIR = -1.0f;

void SV_NewChaseDir(edict_t* actor, edict_t* enemy, float dist)
{
    eastl::array<float, 3> d{0.0f, DI_NODIR, DI_NODIR};

    const float olddir = anglemod(static_cast<float>(static_cast<int>(actor->v.ideal_yaw / 45.0f) * 45));
    const float turnaround = anglemod(olddir - 180.0f);

    const float deltax = enemy->v.origin[0] - actor->v.origin[0];
    const float deltay = enemy->v.origin[1] - actor->v.origin[1];
    if (deltax > 10.0f) d[1] = 0.0f;
    else if (deltax < -10.0f) d[1] = 180.0f;
    else d[1] = DI_NODIR;

    if (deltay < -10.0f) d[2] = 270.0f;
    else if (deltay > 10.0f) d[2] = 90.0f;
    else d[2] = DI_NODIR;

    if (d[1] != DI_NODIR && d[2] != DI_NODIR) {
        const float tdir = (d[1] == 0.0f) ? ((d[2] == 90.0f) ? 45.0f : 315.0f) : ((d[2] == 90.0f) ? 135.0f : 215.0f);
        if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) return;
    }

    if (((rand() & 3) & 1) || fabs(deltay) > fabs(deltax)) {
        const float tdir = d[1];
        d[1] = d[2];
        d[2] = tdir;
    }

    if (d[1] != DI_NODIR && d[1] != turnaround && SV_StepDirection(actor, d[1], dist)) return;
    if (d[2] != DI_NODIR && d[2] != turnaround && SV_StepDirection(actor, d[2], dist)) return;
    if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist)) return;

    if (rand() & 1) {
        for (float tdir = 0.0f; tdir <= 315.0f; tdir += 45.0f) {
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) return;
        }
    } else {
        for (float tdir = 315.0f; tdir >= 0.0f; tdir -= 45.0f) {
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) return;
        }
    }

    if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist)) return;

    actor->v.ideal_yaw = olddir;
    if (!SV_CheckBottom(actor)) SV_FixCheckBottom(actor);
}

qboolean SV_CloseEnough(edict_t* ent, edict_t* goal, float dist)
{
    for (int i = 0; i < 3; ++i) {
        if (goal->v.absmin[i] > ent->v.absmax[i] + dist) return false;
        if (goal->v.absmax[i] < ent->v.absmin[i] - dist) return false;
    }
    return true;
}

void SV_MoveToGoal()
{
    edict_t* ent = PROG_TO_EDICT(pr_global_struct->self);
    edict_t* goal = PROG_TO_EDICT(ent->v.goalentity);
    const float dist = G_FLOAT(OFS_PARM0);

    if (!(static_cast<int>(ent->v.flags) & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        G_FLOAT(OFS_RETURN) = 0.0f;
        return;
    }

    if (PROG_TO_EDICT(ent->v.enemy) != sv.edicts && SV_CloseEnough(ent, goal, dist)) return;

    if ((rand() & 3) == 1 || !SV_StepDirection(ent, ent->v.ideal_yaw, dist)) {
        SV_NewChaseDir(ent, goal, dist);
    }
}

} // namespace Server
