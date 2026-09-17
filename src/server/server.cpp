// server.cpp -- Server Lifecycle and Map Spawning Implementation
#include "quakedef.hpp"
#include "server/server.hpp"
#include "server/world.hpp"
#include "server/physics.hpp"
#include "server/sv_send.hpp"

using namespace Common;
using namespace Console;
using namespace Cvar;
using namespace Cmd;
using namespace Host;
using namespace VM;
using namespace Model;
using namespace Net;
using namespace Client;

namespace Server {

cvar_t teamplay = { "teamplay", "0", false, true };
cvar_t skill = { "skill", "1" };
cvar_t deathmatch = { "deathmatch", "0" };
cvar_t coop = { "coop", "0" };
cvar_t fraglimit = { "fraglimit", "0", false, true };
cvar_t timelimit = { "timelimit", "0", false, true };

cvar_t sv_friction = { "sv_friction", "4", false, true };
cvar_t sv_stopspeed = { "sv_stopspeed", "100" };
cvar_t sv_gravity = { "sv_gravity", "800", false, true };
cvar_t sv_maxvelocity = { "sv_maxvelocity", "2000" };
cvar_t sv_nostep = { "sv_nostep", "0" };
cvar_t sv_idealpitchscale = { "sv_idealpitchscale", "0.8" };
cvar_t sv_maxspeed = { "sv_maxspeed", "320", false, true };
cvar_t sv_accelerate = { "sv_accelerate", "10" };
cvar_t sv_edgefriction = { "edgefriction", "2" };

ServerSubsystem& GetServerSubsystem() noexcept
{
    static ServerSubsystem subsystem;
    return subsystem;
}

static std::array<std::array<char, 5>, MAX_MODELS> localmodels{};

void SV_Init()
{
    cvar_t* cvars[] = {
        &sv_friction, &sv_stopspeed, &sv_gravity, &sv_maxvelocity,
        &sv_nostep, &sv_idealpitchscale, &sv_maxspeed, &sv_accelerate, &sv_edgefriction
    };
    for (auto* cvar : cvars) Cvar::Register(cvar);

    for (size_t i = 0; i < localmodels.size(); ++i) {
        sprintf_s(localmodels[i].data(), localmodels[i].size(), "*%i", static_cast<int>(i));
    }
}

int SV_ModelIndex(const char* name)
{
    if (!name || !name[0]) return 0;

    for (size_t i = 0; i < sv.model_precache.size() && sv.model_precache[i]; ++i) {
        if (!strcmp(sv.model_precache[i], name)) return static_cast<int>(i);
    }

    Sys_Error("SV_ModelIndex: model %s not precached", name);
    return 0;
}

void SV_CreateBaseline()
{
    for (int entnum = 0; entnum < sv.num_edicts; ++entnum) {
        edict_t* svent = EDICT_NUM(entnum);
        if (svent->free) continue;
        if (entnum > svs.maxclients && !svent->v.modelindex) continue;

        VectorCopy(svent->v.origin, svent->baseline.origin);
        VectorCopy(svent->v.angles, svent->baseline.angles);
        svent->baseline.frame = static_cast<int>(svent->v.frame);
        svent->baseline.skin = static_cast<int>(svent->v.skin);
        if (entnum > 0 && entnum <= svs.maxclients) {
            svent->baseline.colormap = entnum;
            svent->baseline.modelindex = SV_ModelIndex("progs/player.mdl");
        } else {
            svent->baseline.colormap = 0;
            svent->baseline.modelindex = SV_ModelIndex(PR_GetString(svent->v.model));
        }

        MSG_WriteByte(&sv.signon, svc_spawnbaseline);
        MSG_WriteShort(&sv.signon, entnum);
        MSG_WriteByte(&sv.signon, svent->baseline.modelindex);
        MSG_WriteByte(&sv.signon, svent->baseline.frame);
        MSG_WriteByte(&sv.signon, svent->baseline.colormap);
        MSG_WriteByte(&sv.signon, svent->baseline.skin);
        for (int i = 0; i < 3; ++i) {
            MSG_WriteCoord(&sv.signon, svent->baseline.origin[i]);
            MSG_WriteAngle(&sv.signon, svent->baseline.angles[i]);
        }
    }
}

void SV_SendReconnect()
{
    std::array<char, 128> data{};
    sizebuf_t msg{};

    msg.data = reinterpret_cast<byte*>(data.data());
    msg.cursize = 0;
    msg.maxsize = static_cast<int>(data.size());

    MSG_WriteChar(&msg, svc_stufftext);
    MSG_WriteString(&msg, "reconnect\n");
    NET_SendToAll(&msg, 5);

    if (cls.state != ca_dedicated) Cmd::ExecuteString("reconnect\n", Cmd::Source::Command);
}

void SV_SaveSpawnparms()
{
    svs.serverflags = static_cast<int>(pr_global_struct->serverflags);

    for (auto& client : svs.GetClients()) {
        host_client = &client;
        if (!host_client->active) continue;

        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(host_client->edict));
        PR_ExecuteProgram(pr_global_struct->SetChangeParms);
        for (int j = 0; j < NUM_SPAWN_PARMS; ++j) {
            host_client->spawn_parms[static_cast<size_t>(j)] = (&pr_global_struct->parm1)[j];
        }
    }
}

void SV_SpawnServer(const char* server)
{
    if (hostname.string.empty()) Cvar::Set("hostname", "UNNAMED");

    Screen::GetScreenSystem().SetCentertimeOff(0.0f);
    Con_DPrintf("SpawnServer: %s\n", server);
    svs.changelevel_issued = false;

    if (sv.active) SV_SendReconnect();

    if (coop.value) Cvar::SetValue("deathmatch", 0);

    current_skill = static_cast<int>(skill.value + 0.5f);
    if (current_skill < 0) current_skill = 0;
    if (current_skill > 3) current_skill = 3;
    Cvar::SetValue("skill", static_cast<float>(current_skill));

    Host_ClearMemory();

    sv = server_t{};
    sv.SetName(server);

    PR_LoadProgs();

    sv.max_edicts = MAX_EDICTS;
    sv.edicts_storage.assign(static_cast<size_t>(sv.max_edicts) * pr_edict_size, 0);
    sv.edicts = reinterpret_cast<edict_t*>(sv.edicts_storage.data());

    sv.datagram.maxsize = static_cast<int>(sv.datagram_buf.size());
    sv.datagram.cursize = 0;
    sv.datagram.data = sv.datagram_buf.data();

    sv.reliable_datagram.maxsize = static_cast<int>(sv.reliable_datagram_buf.size());
    sv.reliable_datagram.cursize = 0;
    sv.reliable_datagram.data = sv.reliable_datagram_buf.data();

    sv.signon.maxsize = static_cast<int>(sv.signon_buf.size());
    sv.signon.cursize = 0;
    sv.signon.data = sv.signon_buf.data();

    sv.num_edicts = svs.maxclients + 1;
    auto clients = svs.GetClients();
    for (size_t i = 0; i < clients.size(); ++i) {
        edict_t* ent = EDICT_NUM(static_cast<int>(i) + 1);
        clients[i].edict = ent;
    }

    sv.state = ss_loading;
    sv.paused = false;

    sprintf_s(sv.modelname.data(), sv.modelname.size(), "maps/%s.bsp", server);
    sv.worldmodel = Mod_ForName(sv.modelname.data(), false);
    if (!sv.worldmodel) {
        Con_Printf("Couldn't spawn server %s\n", sv.modelname.data());
        sv.active = false;
        return;
    }

    sv.models[1] = sv.worldmodel;

    SV_ClearWorld();

    sv.sound_precache[0] = pr_strings;
    sv.model_precache[0] = pr_strings;
    sv.model_precache[1] = sv.modelname.data();
    for (int i = 1; i < sv.worldmodel->numsubmodels; ++i) {
        sv.model_precache[1 + i] = localmodels[static_cast<size_t>(i)].data();
        sv.models[1 + i] = Mod_ForName(localmodels[static_cast<size_t>(i)].data(), false);
    }

    edict_t* ent = EDICT_NUM(0);
    ED_ClearEdict(ent);
    ent->v.model = PR_SetString(sv.worldmodel->name);
    ent->v.modelindex = 1;
    ent->v.solid = SOLID_BSP;
    ent->v.movetype = MOVETYPE_PUSH;

    if (coop.value) pr_global_struct->coop = coop.value;
    else pr_global_struct->deathmatch = deathmatch.value;

    pr_global_struct->mapname = PR_SetString(sv.name.data());
    pr_global_struct->serverflags = static_cast<float>(svs.serverflags);

    ED_LoadFromFile(sv.worldmodel->entities);

    sv.active = true;
    sv.state = ss_active;

    host_frametime = 0.1;
    SV_Physics();
    SV_Physics();

    SV_CreateBaseline();

    for (auto& client : clients) {
        host_client = &client;
        if (host_client->active) SV_SendServerinfo(host_client);
    }

    Con_DPrintf("Server spawned.\n");
}

} // namespace Server
