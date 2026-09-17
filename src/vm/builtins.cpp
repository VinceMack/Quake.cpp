// builtins.cpp -- QuakeC engine built-in functions and dispatch table
#include "vm/builtins.hpp"
#include "vm/interpreter.hpp"
#include "vm/program.hpp"
#include "vm/edict.hpp"
#include "core/cvar.hpp"
#include "core/cmd.hpp"
#include "core/msg.hpp"
#include "core/string_utils.hpp"
#include "host/host.hpp"
#include "core/print.hpp"
#include "world/model.hpp"
#include "platform/crt_compat.hpp"
#include "world/collision.hpp"
#include "server/physics.hpp"
#include "server/world.hpp"
#include "server/sv_send.hpp"
#include "network/protocol.hpp"
#include "server/server_types.hpp"
#include "server/server.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace VM {

cvar_t nomonsters = { "nomonsters", "0", { }, { }, { }, { } };
cvar_t gamecfg = { "gamecfg", "0", { }, { }, { }, { } };
cvar_t scratch1 = { "scratch1", "0", { }, { }, { }, { } };
cvar_t scratch2 = { "scratch2", "0", { }, { }, { }, { } };
cvar_t scratch3 = { "scratch3", "0", { }, { }, { }, { } };
cvar_t scratch4 = { "scratch4", "0", { }, { }, { }, { } };
cvar_t savedgamecfg = { "savedgamecfg", "0", true, { }, { }, { } };
cvar_t saved1 = { "saved1", "0", true, { }, { }, { } };
cvar_t saved2 = { "saved2", "0", true, { }, { }, { } };
cvar_t saved3 = { "saved3", "0", true, { }, { }, { } };
cvar_t saved4 = { "saved4", "0", true, { }, { }, { } };
cvar_t sv_aim = { "sv_aim", "0.93" };

void PR_Init(void)
{
    Cmd::AddCommand("edict", ED_PrintEdict_f);
    Cmd::AddCommand("edicts", ED_PrintEdicts);
    Cmd::AddCommand("edictcount", ED_Count);
    Cmd::AddCommand("profile", PR_Profile_f);
    Cvar::Register(&nomonsters);
    Cvar::Register(&gamecfg);
    Cvar::Register(&scratch1);
    Cvar::Register(&scratch2);
    Cvar::Register(&scratch3);
    Cvar::Register(&scratch4);
    Cvar::Register(&savedgamecfg);
    Cvar::Register(&saved1);
    Cvar::Register(&saved2);
    Cvar::Register(&saved3);
    Cvar::Register(&saved4);
}

#define RETURN_EDICT(e) (((int*)pr_globals)[OFS_RETURN] = static_cast<int>(EDICT_TO_PROG(e)))

static char* PF_VarString(int first)
{
    static char out[256];
    out[0] = 0;
    for (int i = first; i < pr_argc; i++) {
        strcat_s(out, sizeof(out), G_STRING((OFS_PARM0 + i * 3)));
    }
    return out;
}

void PF_error(void)
{
    char* s = PF_VarString(0);
    Console::Con_Printf("======SERVER ERROR in %s:\n%s\n", PR_GetString(pr_xfunction->s_name), s);
    edict_t* ed = PROG_TO_EDICT(pr_global_struct->self);
    ED_Print(ed);
    Host::Host_Error("Program error");
}

void PF_objerror(void)
{
    char* s = PF_VarString(0);
    Console::Con_Printf("======OBJECT ERROR in %s:\n%s\n", PR_GetString(pr_xfunction->s_name), s);
    edict_t* ed = PROG_TO_EDICT(pr_global_struct->self);
    ED_Print(ed);
    ED_Free(ed);
    Host::Host_Error("Program error");
}

void PF_makevectors(void)
{
    Math::AngleVectors(
        G_VECTOR(OFS_PARM0), pr_global_struct->v_forward, pr_global_struct->v_right, pr_global_struct->v_up);
}

void PF_setorigin(void)
{
    edict_t* e = G_EDICT(OFS_PARM0);
    float* org = G_VECTOR(OFS_PARM1);
    VectorCopy(org, e->v.origin);
    Server::SV_LinkEdict(e, false);
}

static void SetMinMaxSize(edict_t* e, const float* min, const float* max, qboolean rotate)
{
    Vector3 rmin, rmax;
    float bounds[2][3];
    float xvector[2], yvector[2];
    Vector3 base, transformed;

    for (int i = 0; i < 3; i++) {
        if (min[i] > max[i]) PR_RunError("backwards mins/maxs");
    }

    rotate = false;

    if (!rotate) {
        rmin = Vector3(min);
        rmax = Vector3(max);
    } else {
        float* angles = e->v.angles;
        float a = angles[1] / 180.0f * static_cast<float>(M_PI);

        xvector[0] = std::cos(a);
        xvector[1] = std::sin(a);
        yvector[0] = -std::sin(a);
        yvector[1] = std::cos(a);

        VectorCopy(min, bounds[0]);
        VectorCopy(max, bounds[1]);

        rmin[0] = rmin[1] = rmin[2] = 9999;
        rmax[0] = rmax[1] = rmax[2] = -9999;

        for (int i = 0; i <= 1; i++) {
            base[0] = bounds[i][0];
            for (int j = 0; j <= 1; j++) {
                base[1] = bounds[j][1];
                for (int k = 0; k <= 1; k++) {
                    base[2] = bounds[k][2];
                    transformed[0] = xvector[0] * base[0] + yvector[0] * base[1];
                    transformed[1] = xvector[1] * base[0] + yvector[1] * base[1];
                    transformed[2] = base[2];

                    for (int l = 0; l < 3; l++) {
                        if (transformed[l] < rmin[l]) rmin[l] = transformed[l];
                        if (transformed[l] > rmax[l]) rmax[l] = transformed[l];
                    }
                }
            }
        }
    }

    e->v.mins = rmin;
    e->v.maxs = rmax;
    e->v.size = Vector3(max) - Vector3(min);

    Server::SV_LinkEdict(e, false);
}

void PF_setsize(void)
{
    edict_t* e = G_EDICT(OFS_PARM0);
    float* min = G_VECTOR(OFS_PARM1);
    float* max = G_VECTOR(OFS_PARM2);
    SetMinMaxSize(e, min, max, false);
}

void PF_setmodel(void)
{
    edict_t* e = G_EDICT(OFS_PARM0);
    char* m = G_STRING(OFS_PARM1);
    char** check;
    model_t* mod;
    int i;

    for (i = 0, check = Server::sv.model_precache.data(); *check; i++, check++) {
        if (!std::strcmp(*check, m)) break;
    }

    if (!*check) PR_RunError("no precache: %s\n", m);

    e->v.model = PR_SetString(m);
    e->v.modelindex = static_cast<float>(i);

    mod = Server::sv.models[(int)e->v.modelindex];
    if (mod)
        SetMinMaxSize(e, mod->mins, mod->maxs, true);
    else
        SetMinMaxSize(e, Math::vec3_origin, Math::vec3_origin, true);
}

void PF_bprint(void)
{
    char* s = PF_VarString(0);
    Server::SV_BroadcastPrintf("%s", s);
}

void PF_sprint(void)
{
    int entnum = G_EDICTNUM(OFS_PARM0);
    char* s = PF_VarString(1);

    if (entnum < 1 || entnum > Server::svs.maxclients) {
        Console::Con_Printf("tried to sprint to a non-client\n");
        return;
    }

    client_t* client = &Server::svs.clients[entnum - 1];
    Common::MSG_WriteChar(&client->message, svc_print);
    Common::MSG_WriteString(&client->message, s);
}

void PF_centerprint(void)
{
    int entnum = G_EDICTNUM(OFS_PARM0);
    char* s = PF_VarString(1);

    if (entnum < 1 || entnum > Server::svs.maxclients) {
        Console::Con_Printf("tried to sprint to a non-client\n");
        return;
    }

    client_t* client = &Server::svs.clients[entnum - 1];
    Common::MSG_WriteChar(&client->message, svc_centerprint);
    Common::MSG_WriteString(&client->message, s);
}

void PF_normalize(void)
{
    float* value1 = G_VECTOR(OFS_PARM0);
    Vector3 newvalue;
    float new_val = value1[0] * value1[0] + value1[1] * value1[1] + value1[2] * value1[2];
    new_val = std::sqrt(new_val);

    if (new_val == 0) {
        newvalue[0] = newvalue[1] = newvalue[2] = 0;
    } else {
        new_val = 1 / new_val;
        newvalue[0] = value1[0] * new_val;
        newvalue[1] = value1[1] * new_val;
        newvalue[2] = value1[2] * new_val;
    }

    VectorCopy(newvalue, G_VECTOR(OFS_RETURN));
}

void PF_vlen(void)
{
    float* value1 = G_VECTOR(OFS_PARM0);
    float new_val = value1[0] * value1[0] + value1[1] * value1[1] + value1[2] * value1[2];
    G_FLOAT(OFS_RETURN) = std::sqrt(new_val);
}

void PF_vectoyaw(void)
{
    float* value1 = G_VECTOR(OFS_PARM0);
    float yaw;

    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
    } else {
        yaw = static_cast<float>(static_cast<int>(std::atan2(value1[1], value1[0]) * 180.0 / M_PI));
        if (yaw < 0) yaw += 360;
    }

    G_FLOAT(OFS_RETURN) = yaw;
}

void PF_vectoangles(void)
{
    float* value1 = G_VECTOR(OFS_PARM0);
    float forward, yaw, pitch;

    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
        pitch = (value1[2] > 0) ? 90.0f : 270.0f;
    } else {
        yaw = static_cast<float>(static_cast<int>(std::atan2(value1[1], value1[0]) * 180.0 / M_PI));
        if (yaw < 0) yaw += 360;

        forward = std::sqrt(value1[0] * value1[0] + value1[1] * value1[1]);
        pitch = static_cast<float>(static_cast<int>(std::atan2(value1[2], forward) * 180.0 / M_PI));
        if (pitch < 0) pitch += 360;
    }

    G_FLOAT(OFS_RETURN + 0) = pitch;
    G_FLOAT(OFS_RETURN + 1) = yaw;
    G_FLOAT(OFS_RETURN + 2) = 0;
}

void PF_random(void)
{
    G_FLOAT(OFS_RETURN) = (std::rand() & 0x7fff) / ((float)0x7fff);
}

void PF_particle(void)
{
    float* org = G_VECTOR(OFS_PARM0);
    float* dir = G_VECTOR(OFS_PARM1);
    float color = G_FLOAT(OFS_PARM2);
    float count = G_FLOAT(OFS_PARM3);
    Server::SV_StartParticle(org, dir, static_cast<int>(color), static_cast<int>(count));
}

void PF_ambientsound(void)
{
    char** check;
    char* samp = G_STRING(OFS_PARM1);
    float* pos = G_VECTOR(OFS_PARM0);
    float vol = G_FLOAT(OFS_PARM2);
    float attenuation = G_FLOAT(OFS_PARM3);
    int i, soundnum;

    for (soundnum = 0, check = Server::sv.sound_precache.data(); *check; check++, soundnum++) {
        if (!std::strcmp(*check, samp)) break;
    }

    if (!*check) {
        Console::Con_Printf("no precache: %s\n", samp);
        return;
    }

    Common::MSG_WriteByte(&Server::sv.signon, svc_spawnstaticsound);
    for (i = 0; i < 3; i++) Common::MSG_WriteCoord(&Server::sv.signon, pos[i]);

    Common::MSG_WriteByte(&Server::sv.signon, soundnum);
    Common::MSG_WriteByte(&Server::sv.signon, static_cast<int>(vol * 255.0f));
    Common::MSG_WriteByte(&Server::sv.signon, static_cast<int>(attenuation * 64.0f));
}

void PF_sound(void)
{
    edict_t* entity = G_EDICT(OFS_PARM0);
    int channel = static_cast<int>(G_FLOAT(OFS_PARM1));
    char* sample = G_STRING(OFS_PARM2);
    int vol = (int)(G_FLOAT(OFS_PARM3) * 255);
    float attenuation = G_FLOAT(OFS_PARM4);

    if (vol < 0 || vol > 255) Common::Sys_Error("SV_StartSound: volume = %i", vol);
    if (attenuation < 0 || attenuation > 4) Common::Sys_Error("SV_StartSound: attenuation = %f", attenuation);
    if (channel < 0 || channel > 7) Common::Sys_Error("SV_StartSound: channel = %i", channel);

    Server::SV_StartSound(entity, channel, sample, vol, attenuation);
}

void PF_break(void)
{
    Console::Con_Printf("break statement\n");
    *(int*)-4 = 0;
}

void PF_traceline(void)
{
    float* v1 = G_VECTOR(OFS_PARM0);
    float* v2 = G_VECTOR(OFS_PARM1);
    int no_monsters = static_cast<int>(G_FLOAT(OFS_PARM2));
    edict_t* ent = G_EDICT(OFS_PARM3);

    trace_t trace = Server::SV_Move(v1, Math::vec3_origin, Math::vec3_origin, v2, no_monsters, ent);

    pr_global_struct->trace_allsolid = trace.allsolid;
    pr_global_struct->trace_startsolid = trace.startsolid;
    pr_global_struct->trace_fraction = trace.fraction;
    pr_global_struct->trace_inwater = trace.inwater;
    pr_global_struct->trace_inopen = trace.inopen;
    VectorCopy(trace.endpos, pr_global_struct->trace_endpos);
    VectorCopy(trace.plane.normal, pr_global_struct->trace_plane_normal);
    pr_global_struct->trace_plane_dist = trace.plane.dist;
    if (trace.ent) {
        pr_global_struct->trace_ent = static_cast<int>(EDICT_TO_PROG(trace.ent));
    } else {
        pr_global_struct->trace_ent = static_cast<int>(EDICT_TO_PROG(Server::sv.edicts));
    }
}

static byte checkpvs[MAX_MAP_LEAFS / 8];

static int PF_newcheckclient(int check)
{
    int i;
    edict_t* ent;

    if (check < 1) check = 1;
    if (check > Server::svs.maxclients) check = Server::svs.maxclients;
    if (check == Server::svs.maxclients)
        i = 1;
    else
        i = check + 1;

    for (;; i++) {
        if (i == Server::svs.maxclients + 1) i = 1;

        ent = EDICT_NUM(i);
        if (i == check) break;
        if (ent->free || ent->v.health <= 0 || (static_cast<int>(ent->v.flags) & FL_NOTARGET)) continue;
        break;
    }

    Vector3 org = ent->v.origin + ent->v.view_ofs;
    mleaf_t* leaf = Model::Mod_PointInLeaf(org, Server::sv.worldmodel);
    byte* pvs = Model::Mod_LeafPVS(leaf, Server::sv.worldmodel);
    std::memcpy(checkpvs, pvs, (Server::sv.worldmodel->numleafs + 7) >> 3);

    return i;
}

static int c_invis, c_notvis;

void PF_checkclient(void)
{
    if (Server::sv.time - Server::sv.lastchecktime >= 0.1) {
        Server::sv.lastcheck = PF_newcheckclient(Server::sv.lastcheck);
        Server::sv.lastchecktime = Server::sv.time;
    }

    edict_t* ent = EDICT_NUM(Server::sv.lastcheck);
    if (ent->free || ent->v.health <= 0) {
        RETURN_EDICT(Server::sv.edicts);
        return;
    }

    edict_t* self = PROG_TO_EDICT(pr_global_struct->self);
    Vector3 view = self->v.origin + self->v.view_ofs;
    mleaf_t* leaf = Model::Mod_PointInLeaf(view, Server::sv.worldmodel);
    int l = static_cast<int>((leaf - Server::sv.worldmodel->leafs) - 1);
    if ((l < 0) || !(checkpvs[l >> 3] & (1 << (l & 7)))) {
        c_notvis++;
        RETURN_EDICT(Server::sv.edicts);
        return;
    }

    c_invis++;
    RETURN_EDICT(ent);
}

void PF_stuffcmd(void)
{
    int entnum = G_EDICTNUM(OFS_PARM0);
    if (entnum < 1 || entnum > Server::svs.maxclients) PR_RunError("Parm 0 not a client");

    char* str = G_STRING(OFS_PARM1);
    client_t* old = Host::host_client;
    Host::host_client = &Server::svs.clients[entnum - 1];
    Host::Host_ClientCommands("%s", str);
    Host::host_client = old;
}

void PF_localcmd(void)
{
    char* str = G_STRING(OFS_PARM0);
    Cmd::BufferAddText(str);
}

void PF_cvar(void)
{
    char* str = G_STRING(OFS_PARM0);
    G_FLOAT(OFS_RETURN) = Cvar::VariableValue(str);
}

void PF_cvar_set(void)
{
    char* var = G_STRING(OFS_PARM0);
    char* val = G_STRING(OFS_PARM1);
    Cvar::Set(var, val);
}

void PF_findradius(void)
{
    edict_t *ent, *chain = (edict_t*)Server::sv.edicts;
    float rad = G_FLOAT(OFS_PARM1);
    float* org = G_VECTOR(OFS_PARM0);
    Vector3 eorg;

    ent = NEXT_EDICT(Server::sv.edicts);
    for (int i = 1; i < Server::sv.num_edicts; i++, ent = NEXT_EDICT(ent)) {
        if (ent->free || ent->v.solid == SOLID_NOT) continue;

        eorg = Vector3(org) - (ent->v.origin + (ent->v.mins + ent->v.maxs) * 0.5f);
        if (eorg.length() > rad) continue;

        ent->v.chain = static_cast<int>(EDICT_TO_PROG(chain));
        chain = ent;
    }

    RETURN_EDICT(chain);
}

void PF_dprint(void)
{
    Console::Con_DPrintf("%s", PF_VarString(0));
}

static char pr_string_temp[128];

void PF_ftos(void)
{
    float v = G_FLOAT(OFS_PARM0);
    if (v == static_cast<int>(v))
        sprintf_s(pr_string_temp, sizeof(pr_string_temp), "%d", static_cast<int>(v));
    else
        sprintf_s(pr_string_temp, sizeof(pr_string_temp), "%5.1f", v);
    G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);
}

void PF_fabs(void)
{
    G_FLOAT(OFS_RETURN) = static_cast<float>(std::fabs(G_FLOAT(OFS_PARM0)));
}

void PF_vtos(void)
{
    sprintf_s(pr_string_temp, sizeof(pr_string_temp), "'%5.1f %5.1f %5.1f'", G_VECTOR(OFS_PARM0)[0],
        G_VECTOR(OFS_PARM0)[1], G_VECTOR(OFS_PARM0)[2]);
    G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);
}

void PF_Spawn(void)
{
    edict_t* ed = ED_Alloc();
    RETURN_EDICT(ed);
}

void PF_Remove(void)
{
    edict_t* ed = G_EDICT(OFS_PARM0);
    ED_Free(ed);
}

void PF_Find(void)
{
    int e = G_EDICTNUM(OFS_PARM0);
    int f = G_INT(OFS_PARM1);
    char* s = G_STRING(OFS_PARM2);
    edict_t* ed;

    if (!s) PR_RunError("PF_Find: bad search string");

    for (e++; e < Server::sv.num_edicts; e++) {
        ed = EDICT_NUM(e);
        if (ed->free) continue;

        char* t = E_STRING(ed, f);
        if (!t) continue;

        if (!std::strcmp(t, s)) {
            RETURN_EDICT(ed);
            return;
        }
    }

    RETURN_EDICT(Server::sv.edicts);
}

static void PR_CheckEmptyString(char* s)
{
    if (s[0] <= ' ') PR_RunError("Bad string");
}

void PF_precache_file(void)
{
    G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
}

void PF_precache_sound(void)
{
    if (Server::sv.state != ss_loading) PR_RunError("PF_Precache_*: Precache can only be done in spawn functions");

    char* s = G_STRING(OFS_PARM0);
    G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
    PR_CheckEmptyString(s);

    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (!Server::sv.sound_precache[i]) {
            Server::sv.sound_precache[i] = s;
            return;
        }
        if (!std::strcmp(Server::sv.sound_precache[i], s)) return;
    }
    PR_RunError("PF_precache_sound: overflow");
}

void PF_precache_model(void)
{
    if (Server::sv.state != ss_loading) PR_RunError("PF_Precache_*: Precache can only be done in spawn functions");

    char* s = G_STRING(OFS_PARM0);
    G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
    PR_CheckEmptyString(s);

    for (int i = 0; i < MAX_MODELS; i++) {
        if (!Server::sv.model_precache[i]) {
            Server::sv.model_precache[i] = s;
            Server::sv.models[i] = Model::Mod_ForName(s, true);
            return;
        }
        if (!std::strcmp(Server::sv.model_precache[i], s)) return;
    }
    PR_RunError("PF_precache_model: overflow");
}

void PF_coredump(void)
{
    ED_PrintEdicts();
}
void PF_traceon(void)
{
    pr_trace = true;
}
void PF_traceoff(void)
{
    pr_trace = false;
}
void PF_eprint(void)
{
    ED_PrintNum(G_EDICTNUM(OFS_PARM0));
}

void PF_walkmove(void)
{
    edict_t* ent = PROG_TO_EDICT(pr_global_struct->self);
    float yaw = G_FLOAT(OFS_PARM0);
    float dist = G_FLOAT(OFS_PARM1);
    Vector3 move;

    if (!(static_cast<int>(ent->v.flags) & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        G_FLOAT(OFS_RETURN) = 0;
        return;
    }

    yaw = yaw * static_cast<float>(M_PI) * 2.0f / 360.0f;
    move.x = std::cos(yaw) * dist;
    move.y = std::sin(yaw) * dist;
    move.z = 0;

    dfunction_t* oldf = pr_xfunction;
    int oldself = pr_global_struct->self;

    G_FLOAT(OFS_RETURN) = Server::SV_movestep(ent, move, true);

    pr_xfunction = oldf;
    pr_global_struct->self = oldself;
}

void PF_droptofloor(void)
{
    edict_t* ent = PROG_TO_EDICT(pr_global_struct->self);
    Vector3 end = ent->v.origin;
    end.z -= 256;

    trace_t trace = Server::SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

    if (trace.fraction == 1 || trace.allsolid) {
        G_FLOAT(OFS_RETURN) = 0;
    } else {
        ent->v.origin = trace.endpos;
        Server::SV_LinkEdict(ent, false);
        ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
        ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));
        G_FLOAT(OFS_RETURN) = 1;
    }
}

void PF_lightstyle(void)
{
    int style = static_cast<int>(G_FLOAT(OFS_PARM0));
    char* val = G_STRING(OFS_PARM1);

    Server::sv.lightstyles[style] = val;

    if (Server::sv.state != ss_active) return;

    for (int j = 0; j < Server::svs.maxclients; j++) {
        client_t* client = Server::svs.clients + j;
        if (client->active || client->spawned) {
            Common::MSG_WriteChar(&client->message, svc_lightstyle);
            Common::MSG_WriteChar(&client->message, style);
            Common::MSG_WriteString(&client->message, val);
        }
    }
}

void PF_rint(void)
{
    float f = G_FLOAT(OFS_PARM0);
    if (f > 0)
        G_FLOAT(OFS_RETURN) = static_cast<float>(static_cast<int>(f + 0.5f));
    else
        G_FLOAT(OFS_RETURN) = static_cast<float>(static_cast<int>(f - 0.5f));
}

void PF_floor(void)
{
    G_FLOAT(OFS_RETURN) = std::floor(G_FLOAT(OFS_PARM0));
}
void PF_ceil(void)
{
    G_FLOAT(OFS_RETURN) = std::ceil(G_FLOAT(OFS_PARM0));
}

void PF_checkbottom(void)
{
    edict_t* ent = G_EDICT(OFS_PARM0);
    G_FLOAT(OFS_RETURN) = Server::SV_CheckBottom(ent);
}

void PF_pointcontents(void)
{
    float* v = G_VECTOR(OFS_PARM0);
    G_FLOAT(OFS_RETURN) = static_cast<float>(Server::SV_PointContents(v));
}

void PF_nextent(void)
{
    int i = G_EDICTNUM(OFS_PARM0);
    while (1) {
        i++;
        if (i == Server::sv.num_edicts) {
            RETURN_EDICT(Server::sv.edicts);
            return;
        }

        edict_t* ent = EDICT_NUM(i);
        if (!ent->free) {
            RETURN_EDICT(ent);
            return;
        }
    }
}

void PF_aim(void)
{
    edict_t* ent = G_EDICT(OFS_PARM0);
    Vector3 start = ent->v.origin;
    start.z += 20;

    Vector3 dir = pr_global_struct->v_forward;
    Vector3 end = start + dir * 2048.0f;
    trace_t tr = Server::SV_Move(start, Math::vec3_origin, Math::vec3_origin, end, false, ent);
    if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM
        && (!Server::teamplay.value || ent->v.team <= 0 || ent->v.team != tr.ent->v.team)) {
        VectorCopy(pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
        return;
    }

    Vector3 bestdir = dir;
    float bestdist = sv_aim.value;
    edict_t* bestent = nullptr;

    edict_t* check = NEXT_EDICT(Server::sv.edicts);
    for (int i = 1; i < Server::sv.num_edicts; i++, check = NEXT_EDICT(check)) {
        if (check->v.takedamage != DAMAGE_AIM) continue;
        if (check == ent) continue;
        if (Server::teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team) continue;

        end = check->v.origin + (check->v.mins + check->v.maxs) * 0.5f;
        dir = end - start;
        dir.normalize();
        float dist = dir.dot(pr_global_struct->v_forward);
        if (dist < bestdist) continue;

        tr = Server::SV_Move(start, Math::vec3_origin, Math::vec3_origin, end, false, ent);
        if (tr.ent == check) {
            bestdist = dist;
            bestent = check;
        }
    }

    if (bestent) {
        dir = bestent->v.origin - ent->v.origin;
        float dist = dir.dot(pr_global_struct->v_forward);
        end = pr_global_struct->v_forward * dist;
        end.z = dir.z;
        end.normalize();
        VectorCopy(end, G_VECTOR(OFS_RETURN));
    } else {
        VectorCopy(bestdir, G_VECTOR(OFS_RETURN));
    }
}

void PF_changeyaw(void)
{
    edict_t* ent = PROG_TO_EDICT(pr_global_struct->self);
    float current = Math::anglemod(ent->v.angles[1]);
    float ideal = ent->v.ideal_yaw;
    float speed = ent->v.yaw_speed;

    if (current == ideal) return;

    float move = ideal - current;
    if (ideal > current) {
        if (move >= 180) move = move - 360;
    } else {
        if (move <= -180) move = move + 360;
    }

    if (move > 0) {
        if (move > speed) move = speed;
    } else {
        if (move < -speed) move = -speed;
    }

    ent->v.angles[1] = Math::anglemod(current + move);
}

#define MSG_BROADCAST 0
#define MSG_ONE 1
#define MSG_ALL 2
#define MSG_INIT 3

static sizebuf_t* WriteDest(void)
{
    int dest = static_cast<int>(G_FLOAT(OFS_PARM0));
    switch (dest) {
    case MSG_BROADCAST:
        return &Server::sv.datagram;

    case MSG_ONE: {
        edict_t* ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
        int entnum = NUM_FOR_EDICT(ent);
        if (entnum < 1 || entnum > Server::svs.maxclients) {
            PR_RunError("WriteDest: not a client");
        }
        return &Server::svs.clients[entnum - 1].message;
    }

    case MSG_ALL:
        return &Server::sv.reliable_datagram;

    case MSG_INIT:
        return &Server::sv.signon;

    default:
        PR_RunError("WriteDest: bad destination");
    }
}

inline void PF_WriteByte(void)
{
    Common::MSG_WriteByte(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}
inline void PF_WriteChar(void)
{
    Common::MSG_WriteChar(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}
inline void PF_WriteShort(void)
{
    Common::MSG_WriteShort(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}
inline void PF_WriteLong(void)
{
    Common::MSG_WriteLong(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}
inline void PF_WriteAngle(void)
{
    Common::MSG_WriteAngle(WriteDest(), G_FLOAT(OFS_PARM1));
}
inline void PF_WriteCoord(void)
{
    Common::MSG_WriteCoord(WriteDest(), G_FLOAT(OFS_PARM1));
}
inline void PF_WriteString(void)
{
    Common::MSG_WriteString(WriteDest(), G_STRING(OFS_PARM1));
}
inline void PF_WriteEntity(void)
{
    Common::MSG_WriteShort(WriteDest(), G_EDICTNUM(OFS_PARM1));
}

void PF_makestatic(void)
{
    edict_t* ent = G_EDICT(OFS_PARM0);
    Common::MSG_WriteByte(&Server::sv.signon, svc_spawnstatic);
    Common::MSG_WriteByte(&Server::sv.signon, Server::SV_ModelIndex(PR_GetString(ent->v.model)));
    Common::MSG_WriteByte(&Server::sv.signon, static_cast<int>(ent->v.frame));
    Common::MSG_WriteByte(&Server::sv.signon, static_cast<int>(ent->v.colormap));
    Common::MSG_WriteByte(&Server::sv.signon, static_cast<int>(ent->v.skin));
    for (int i = 0; i < 3; i++) {
        Common::MSG_WriteCoord(&Server::sv.signon, ent->v.origin[i]);
        Common::MSG_WriteAngle(&Server::sv.signon, ent->v.angles[i]);
    }
    ED_Free(ent);
}

void PF_setspawnparms(void)
{
    edict_t* ent = G_EDICT(OFS_PARM0);
    int i = NUM_FOR_EDICT(ent);
    if (i < 1 || i > Server::svs.maxclients) PR_RunError("Entity is not a client");

    client_t* client = Server::svs.clients + (i - 1);
    for (i = 0; i < NUM_SPAWN_PARMS; i++) {
        (&pr_global_struct->parm1)[i] = client->spawn_parms[i];
    }
}

void PF_changelevel(void)
{
    if (Server::svs.changelevel_issued) return;
    Server::svs.changelevel_issued = true;
    char* s = G_STRING(OFS_PARM0);
    Cmd::BufferAddText(Common::va("changelevel %s\n", s));
}

inline void PF_Fixme(void)
{
    PR_RunError("unimplemented bulitin");
}

builtin_t pr_builtin[] = { PF_Fixme, PF_makevectors, PF_setorigin, PF_setmodel, PF_setsize, PF_Fixme, PF_break,
    PF_random, PF_sound, PF_normalize, PF_error, PF_objerror, PF_vlen, PF_vectoyaw, PF_Spawn, PF_Remove, PF_traceline,
    PF_checkclient, PF_Find, PF_precache_sound, PF_precache_model, PF_stuffcmd, PF_findradius, PF_bprint, PF_sprint,
    PF_dprint, PF_ftos, PF_vtos, PF_coredump, PF_traceon, PF_traceoff, PF_eprint, PF_walkmove, PF_Fixme, PF_droptofloor,
    PF_lightstyle, PF_rint, PF_floor, PF_ceil, PF_Fixme, PF_checkbottom, PF_pointcontents, PF_Fixme, PF_fabs, PF_aim,
    PF_cvar, PF_localcmd, PF_nextent, PF_particle, PF_changeyaw, PF_Fixme, PF_vectoangles, PF_WriteByte, PF_WriteChar,
    PF_WriteShort, PF_WriteLong, PF_WriteCoord, PF_WriteAngle, PF_WriteString, PF_WriteEntity, PF_Fixme, PF_Fixme,
    PF_Fixme, PF_Fixme, PF_Fixme, PF_Fixme, PF_Fixme, Server::SV_MoveToGoal, PF_precache_file, PF_makestatic,
    PF_changelevel, PF_Fixme, PF_cvar_set, PF_centerprint, PF_ambientsound, PF_precache_model, PF_precache_sound,
    PF_precache_file, PF_setspawnparms };

builtin_t* pr_builtins = pr_builtin;
int pr_numbuiltins = sizeof(pr_builtin) / sizeof(pr_builtin[0]);

} // namespace VM
