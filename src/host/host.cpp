// host.cpp -- Central engine host orchestration (loop, state, error handling, dispatch)
#include "quakedef.hpp"
#include "host/host.hpp"
#include "render/renderer.hpp"
#include "render/software/sw_renderer.hpp"

#include <algorithm>
#include <fstream>
#include <limits>

using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;

namespace Host {

quakeparms_t host_parms;
qboolean host_initialized;
double host_frametime, host_time, realtime, oldrealtime;
int host_framecount, host_hunklevel, minimum_memory;
client_t* host_client;
byte *host_basepal, *host_colormap;

cvar_t host_framerate = { "host_framerate", "0", {}, {}, {}, {} };
cvar_t host_speeds    = { "host_speeds", "0", {}, {}, {}, {} };
cvar_t sys_ticrate    = { "sys_ticrate", "0.05", {}, {}, {}, {} };
cvar_t serverprofile  = { "serverprofile", "0", {}, {}, {}, {} };
cvar_t samelevel      = { "samelevel", "0", {}, {}, {}, {} };
cvar_t noexit         = { "noexit", "0", false, true, {}, {} };
cvar_t developer      = { "developer", "0", {}, {}, {}, {} };
cvar_t pausable       = { "pausable", "1", {}, {}, {}, {} };
cvar_t temp1          = { "temp1", "0", {}, {}, {}, {} };

[[noreturn]] void Host_EndGame(const char* message, ...) {
    va_list argptr; char string[1024];
    va_start(argptr, message); vsprintf_s(string, sizeof(string), message, argptr); va_end(argptr);
    Con_DPrintf("Host_EndGame: %s\n", string);
    if (sv.active) Host_ShutdownServer(false);
    if (cls.state == ca_dedicated) Sys_Error("Host_EndGame: %s\n", string);
    if (cls.demonum != -1) CL_NextDemo(); else CL_Disconnect();
    Sys_Error("Host_EndGame: %s\n", string);
}

[[noreturn]] void Host_Error(const char* error, ...) {
    va_list argptr; char string[1024]; static qboolean inerror = false;
    if (inerror) Sys_Error("Host_Error: recursively entered");
    inerror = true; Screen::GetScreenSystem().EndLoadingPlaque();
    va_start(argptr, error); vsprintf_s(string, sizeof(string), error, argptr); va_end(argptr);
    Con_Printf("Host_Error: %s\n", string);
    Sys_Printf("Host_Error: %s\n", string);
    if (sv.active) Host_ShutdownServer(false);
    if (cls.state == ca_dedicated) Sys_Error("Host_Error: %s\n", string);
    CL_Disconnect(); cls.demonum = -1; inerror = false;
    Sys_Error("Host_Error: %s\n", string);
}

void Host_FindMaxClients() {
    svs.maxclients = 1;
    if (int i = COM_CheckParm("-dedicated")) {
        cls.state = ca_dedicated; svs.maxclients = (i != com_argc - 1) ? Q_atoi(com_argv[i + 1]) : 8;
    } else cls.state = ca_disconnected;

    if (int i = COM_CheckParm("-listen")) {
        if (cls.state == ca_dedicated) Sys_Error("Only one of -dedicated or -listen can be specified");
        svs.maxclients = (i != com_argc - 1) ? Q_atoi(com_argv[i + 1]) : 8;
    }
    svs.maxclients = eastl::clamp(svs.maxclients, 1, MAX_SCOREBOARD);
    svs.maxclientslimit = eastl::max(4, svs.maxclients);
    svs.resize_clients(svs.maxclientslimit);
    Cvar::SetValue("deathmatch", (svs.maxclients > 1) ? 1.0 : 0.0);
}

void Host_InitLocal() {
    Host_InitCommands();
    for (auto* c : { &host_framerate, &host_speeds, &sys_ticrate, &serverprofile, &fraglimit,
                    &timelimit, &teamplay, &samelevel, &noexit, &skill, &developer, &deathmatch, &coop, &pausable, &temp1 }) Cvar::Register(c);
    Host_FindMaxClients(); host_time = 1.0;
}

void Host_WriteConfiguration() {
    if (host_initialized && !isDedicated) {
        std::ofstream f((eastl::string(com_gamedir) + "/config.cfg").c_str());
        if (f.is_open()) { Key_WriteBindings(f); Cvar::WriteVariables(f); }
        else Con_Printf("Couldn't write config.cfg.\n");
    }
}

void Host_ClientCommands(const char* fmt, ...) {
    va_list argptr; char string[1024];
    va_start(argptr, fmt); vsprintf_s(string, sizeof(string), fmt, argptr); va_end(argptr);
    MSG_WriteByte(&host_client->message, svc_stufftext); MSG_WriteString(&host_client->message, string);
}

void Host_ShutdownServer(qboolean crash) {
    if (!sv.active) return;
    sv.active = false;
    if (cls.state == ca_connected) CL_Disconnect();
    double start = Sys_FloatTime(); int count;
    do {
        count = 0;
        for (int i = 0; i < svs.maxclients; i++) {
            host_client = &svs.clients[i];
            if (host_client->active && host_client->message.cursize) {
                if (NET_CanSendMessage(host_client->netconnection)) {
                    NET_SendMessage(host_client->netconnection, &host_client->message); SZ_Clear(&host_client->message);
                } else { NET_GetMessage(host_client->netconnection); count++; }
            }
        }
        if ((Sys_FloatTime() - start) > 3.0) break;
    } while (count);

    char message[4]; sizebuf_t buf{}; buf.data = (byte*)message; buf.maxsize = 4; buf.cursize = 0;
    MSG_WriteByte(&buf, svc_disconnect); count = NET_SendToAll(&buf, 5);
    if (count) Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);
    for (int i = 0; i < svs.maxclients; i++) { host_client = &svs.clients[i]; if (host_client->active) SV_DropClient(crash); }
    sv = {}; for (int i = 0; i < svs.maxclientslimit; i++) svs.clients[i] = {};
}

void Host_ClearMemory() {
    Con_DPrintf("Clearing memory\n"); D_FlushCaches(); Mod_ClearAll();
    if (host_hunklevel) Hunk_FreeToLowMark(host_hunklevel);
    cls.signon = 0; sv = {}; cl = {};
}

qboolean Host_FilterTime(float time) {
    realtime += time;
    if (!cls.timedemo && realtime - oldrealtime < 1.0 / 72.0) return false;
    host_frametime = realtime - oldrealtime; oldrealtime = realtime;
    if (host_framerate.value > 0) host_frametime = host_framerate.value;
    else host_frametime = eastl::clamp(host_frametime, 0.001, 0.1);
    return true;
}

void Host_GetConsoleCommands() { while (char* cmd = Sys_ConsoleInput()) Cmd::BufferAddText(cmd); }

void Host_ServerFrame() {
    pr_global_struct->frametime = static_cast<float>(host_frametime);
    SZ_Clear(&sv.datagram); SV_CheckForNewClients(); SV_RunClients();
    if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) SV_Physics();
    SV_SendClientMessages();
}

void _Host_Frame(float time) {
    static double time1 = 0, time2 = 0, time3 = 0;
    rand(); if (!Host_FilterTime(time)) return;
    Sys_SendKeyEvents(); IN_Commands(); Cmd::BufferExecute(); NET_Poll();
    if (sv.active) CL_SendCmd();
    Host_GetConsoleCommands();
    if (sv.active) Host_ServerFrame();
    if (!sv.active) CL_SendCmd();
    host_time += host_frametime;
    if (cls.state == ca_connected) CL_ReadFromServer();
    if (host_speeds.value) time1 = Sys_FloatTime();
    Screen::GetScreenSystem().UpdateScreen();
    if (host_speeds.value) time2 = Sys_FloatTime();
    if (cls.signon == SIGNONS) { S_Update(r_origin, vpn, vright, vup); CL_DecayLights(); }
    else S_Update(vec3_origin, vec3_origin, vec3_origin, vec3_origin);
    if (host_speeds.value) {
        int p1 = static_cast<int>((time1 - time3) * 1000), p2 = static_cast<int>((time2 - time1) * 1000);
        time3 = Sys_FloatTime(); int p3 = static_cast<int>((time3 - time2) * 1000);
        Con_Printf("%3i tot %3i server %3i gfx %3i snd\n", p1 + p2 + p3, p1, p2, p3);
    }
    host_framecount++;
}

void Host_Frame(float time) {
    static double timetotal; static int timecount;
    if (!serverprofile.value) { _Host_Frame(time); return; }
    double time1 = Sys_FloatTime(); _Host_Frame(time); double time2 = Sys_FloatTime();
    timetotal += time2 - time1; timecount++; if (timecount < 1000) return;
    int m = static_cast<int>(timetotal * 1000 / timecount); timecount = 0; timetotal = 0;
    int c = static_cast<int>(eastl::count_if(svs.clients, svs.clients + svs.maxclients, [](const client_t& cl) { return cl.active; }));
    Con_Printf("serverprofile: %2i clients %2i msec\n", c, m);
}

void Host_Init(quakeparms_t* parms) {
    minimum_memory = standard_quake ? MINIMUM_MEMORY : MINIMUM_MEMORY_LEVELPAK;
    if (COM_CheckParm("-minmemory")) parms->memsize = minimum_memory;
    host_parms = *parms;
    if (parms->memsize < minimum_memory) Sys_Error("Only %4.1f megs of memory available, can't execute game", parms->memsize / (float)0x100000);
    com_argc = parms->argc; com_argv = parms->argv;
    Memory_Init(parms->membase, parms->memsize);
    Cmd::BufferInit(); Cmd::Init(); V_Init(); Chase_Init(); COM_Init(); Host_InitLocal();
    W_LoadWadFile("gfx.wad"); Key_Init(); GetConsoleSystem().Init(); M_Init(); PR_Init(); Mod_Init(); NET_Init(); SV_Init();
    Con_Printf("Exe: " __TIME__ " " __DATE__ "\n%4.1f megabyte heap\n", parms->memsize / (1024 * 1024.0));
    R_InitTextures();
    if (cls.state != ca_dedicated) {
        host_basepal = (byte*)COM_LoadHunkFile("gfx/palette.lmp"); if (!host_basepal) Sys_Error("Couldn't load gfx/palette.lmp");
        host_colormap = (byte*)COM_LoadHunkFile("gfx/colormap.lmp"); if (!host_colormap) Sys_Error("Couldn't load gfx/colormap.lmp");
        static Render::SoftwareRenderer sw_renderer;
        Render::SetRenderer(&sw_renderer);
        VID_Init(host_basepal); Draw_Init(); Screen::GetScreenSystem().Init(); Render::GetRenderer()->Init(); S_Init(); Sbar_Init(); CL_Init(); IN_Init();
    }
    Cmd::BufferInsertText("exec quake.rc\n"); Hunk_Alloc(0, "-HOST_HUNKLEVEL-");
    host_hunklevel = Hunk_LowMark(); host_initialized = true; Sys_Printf("========Quake Initialized=========\n");
}

void Host_Shutdown() {
    static qboolean isdown = false; if (isdown) { printf("recursive shutdown\n"); return; }
    isdown = true; Screen::GetScreenSystem().SetDisabledForLoading(true); Host_WriteConfiguration();
    NET_Shutdown(); S_Shutdown(); IN_Shutdown(); if (cls.state != ca_dedicated) VID_Shutdown();
}

int current_skill;
void Host_Quit_f() { if (key_dest != key_console && cls.state != ca_dedicated) { M_Menu_Quit_f(); return; } CL_Disconnect(); Host_ShutdownServer(false); Sys_Quit(); }

void Host_Status_f() {
    auto print = (Cmd::state.source == Cmd::Source::Command) ? (sv.active ? Con_Printf : (Cmd::ForwardToServer(), (void(*)(const char*,...))nullptr)) : SV_ClientPrintf;
    if (!print) return;
    print("host:    %s\nversion: %4.2f\n", Cvar::VariableString("hostname"), VERSION);
    if (tcpipAvailable) print("tcp/ip:  %s\n", my_tcpip_address);
    if (ipxAvailable) print("ipx:     %s\n", my_ipx_address);
    print("map:     %s\nplayers: %i active (%i max)\n\n", sv.name, net_activeconnections, svs.maxclients);
    for (int j = 0; j < svs.maxclients; j++) {
        client_t* client = &svs.clients[j]; if (!client->active) continue;
        int seconds = static_cast<int>(net_time - client->netconnection->connecttime), minutes = seconds / 60, hours = minutes / 60;
        seconds %= 60; minutes %= 60;
        print("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n   %s\n", j + 1, client->name.data(), static_cast<int>(client->edict->v.frags), hours, minutes, seconds, client->netconnection->address);
    }
}

static inline void Host_ToggleCheatFlag(int flag, const char* name) {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (pr_global_struct->deathmatch && !host_client->privileged) return;
    sv_player->v.flags = static_cast<float>(static_cast<int>(sv_player->v.flags) ^ flag);
    SV_ClientPrintf("%s %s\n", name, (static_cast<int>(sv_player->v.flags) & flag) ? "ON" : "OFF");
}

void Host_God_f() { Host_ToggleCheatFlag(FL_GODMODE, "godmode"); }
void Host_Notarget_f() { Host_ToggleCheatFlag(FL_NOTARGET, "notarget"); }
qboolean noclip_anglehack;
void Host_Noclip_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (pr_global_struct->deathmatch && !host_client->privileged) return;
    noclip_anglehack = (sv_player->v.movetype != MOVETYPE_NOCLIP);
    sv_player->v.movetype = noclip_anglehack ? MOVETYPE_NOCLIP : MOVETYPE_WALK;
    SV_ClientPrintf("noclip %s\n", noclip_anglehack ? "ON" : "OFF");
}
void Host_Fly_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (pr_global_struct->deathmatch && !host_client->privileged) return;
    bool fly = (sv_player->v.movetype != MOVETYPE_FLY);
    sv_player->v.movetype = fly ? MOVETYPE_FLY : MOVETYPE_WALK;
    SV_ClientPrintf("flymode %s\n", fly ? "ON" : "OFF");
}
void Host_Ping_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    SV_ClientPrintf("Client ping times:\n");
    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i]; if (!client->active) continue;
        float total = 0.0f; for (float p : client->ping_times) total += p;
        SV_ClientPrintf("%4i %s\n", static_cast<int>(total * 1000.0f / NUM_PING_TIMES), client->name.data());
    }
}

void Host_Map_f() {
    if (Cmd::state.source != Cmd::Source::Command) return;
    cls.demonum = -1; CL_Disconnect(); Host_ShutdownServer(false); key_dest = key_game; Screen::GetScreenSystem().BeginLoadingPlaque();
    eastl::string mapstring; for (int i = 0; i < Cmd::Argc(); i++) mapstring += eastl::string(Cmd::Argv(i)) + " ";
    strcpy_s(cls.mapstring.data(), cls.mapstring.size(), (mapstring + "\n").c_str()); svs.serverflags = 0;
    char name[MAX_QPATH]; Q_strncpy(name, Cmd::Argv(1), sizeof(name) - 1); SV_SpawnServer(name);
    if (!sv.active || cls.state == ca_dedicated) return;
    eastl::string spawnparms; for (int i = 2; i < Cmd::Argc(); i++) spawnparms += eastl::string(Cmd::Argv(i)) + " ";
    strcpy_s(cls.spawnparms.data(), cls.spawnparms.size(), spawnparms.c_str()); Cmd::ExecuteString("connect local", Cmd::Source::Command);
}

void Host_Changelevel_f() {
    if (Cmd::Argc() != 2) { Con_Printf("changelevel <levelname> : continue game on a new level\n"); return; }
    if (!sv.active || cls.demoplayback) { Con_Printf("Only the server may changelevel\n"); return; }
    SV_SaveSpawnparms(); char level[MAX_QPATH]; Q_strcpy(level, Cmd::Argv(1)); SV_SpawnServer(level);
}

void Host_Restart_f() {
    if (cls.demoplayback || !sv.active || Cmd::state.source != Cmd::Source::Command) return;
    char mapname[MAX_QPATH]; strcpy_s(mapname, sizeof(mapname), sv.name.data()); SV_SpawnServer(mapname);
}

void Host_Reconnect_f() { Screen::GetScreenSystem().BeginLoadingPlaque(); cls.signon = 0; }
void Host_Connect_f() {
    cls.demonum = -1; if (cls.demoplayback) { CL_StopPlayback(); CL_Disconnect(); }
    eastl::string_view args = Cmd::Args();
    while (!args.empty() && (args.front() == ' ' || args.front() == '\t')) args.remove_prefix(1);
    while (!args.empty() && (args.back() == ' ' || args.back() == '\t' || args.back() == '\r' || args.back() == '\n')) args.remove_suffix(1);
    char name[MAX_QPATH];
    if (!args.empty()) {
        Q_strncpy(name, eastl::string(args.data(), args.length()).c_str(), sizeof(name) - 1);
    } else {
        Q_strcpy(name, Cmd::Argv(1));
    }
    CL_EstablishConnection(name); Host_Reconnect_f();
}

constexpr int SAVEGAME_VERSION = 5;

eastl::string Host_SavegameComment() {
    eastl::string text(SAVEGAME_COMMENT_LENGTH, '_');
    eastl::string levelname = cl.levelname.data(); if (levelname.length() > 22) levelname = levelname.substr(0, 22);
    text.replace(0, levelname.length(), levelname);
    char kills[20]; sprintf_s(kills, sizeof(kills), "kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
    eastl::string kills_str = kills; if (kills_str.length() > (SAVEGAME_COMMENT_LENGTH - 22)) kills_str = kills_str.substr(0, SAVEGAME_COMMENT_LENGTH - 22);
    text.replace(22, kills_str.length(), kills_str);
    for (char& c : text) { if (c == ' ') c = '_'; }
    return text;
}

void Host_Savegame_f() {
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (!sv.active) { Con_Printf("Not playing a local game.\n"); return; }
    if (cl.intermission || svs.maxclients != 1) { Con_Printf(cl.intermission ? "Can't save in intermission.\n" : "Can't save multiplayer games.\n"); return; }
    if (Cmd::Argc() != 2 || Cmd::Argv(1).find("..") != eastl::string_view::npos) { Con_Printf(Cmd::Argc() != 2 ? "save <savename> : save a game\n" : "Relative pathnames are not allowed.\n"); return; }
    for (int i = 0; i < svs.maxclients; i++) { if (svs.clients[i].active && svs.clients[i].edict->v.health <= 0) { Con_Printf("Can't savegame with a dead player\n"); return; } }
    char name[256]; sprintf_s(name, sizeof(name), "%s/%.*s", com_gamedir, static_cast<int>(Cmd::Argv(1).length()), Cmd::Argv(1).data()); COM_DefaultExtension(name, ".sav");
    Con_Printf("Saving game to %s...\n", name);
    std::ofstream f(name); if (!f.is_open()) { Con_Printf("ERROR: couldn't open.\n"); return; }
    f << SAVEGAME_VERSION << "\n" << Host_SavegameComment().c_str() << "\n";
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) f << svs.clients->spawn_parms[i] << "\n";
    f << current_skill << "\n" << sv.name.data() << "\n" << sv.time << "\n";
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) f << (sv.lightstyles[i] ? sv.lightstyles[i] : "m") << "\n";
    ED_WriteGlobals(f);
    for (int i = 0; i < sv.num_edicts; i++) { ED_Write(f, EDICT_NUM(i)); f.flush(); }
    Con_Printf("done.\n");
}

void Host_Loadgame_f() {
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (Cmd::Argc() != 2) { Con_Printf("load <savename> : load a game\n"); return; }
    cls.demonum = -1;
    char name[MAX_OSPATH]; sprintf_s(name, sizeof(name), "%s/%.*s", com_gamedir, static_cast<int>(Cmd::Argv(1).length()), Cmd::Argv(1).data()); COM_DefaultExtension(name, ".sav");
    Con_Printf("Loading game from %s...\n", name); std::ifstream f(name); if (!f.is_open()) { Con_Printf("ERROR: couldn't open.\n"); return; }
    int version = 0; if (!(f >> version)) { Con_Printf("ERROR: read error.\n"); return; }
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    if (version != SAVEGAME_VERSION) { Con_Printf("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION); return; }
    std::string temp_line; if (!std::getline(f, temp_line)) { Con_Printf("ERROR: read error.\n"); return; }
    float spawn_parms[NUM_SPAWN_PARMS]; for (int i = 0; i < NUM_SPAWN_PARMS; i++) { if (!(f >> spawn_parms[i])) { Con_Printf("ERROR: read error.\n"); return; } }
    float tfloat = 0.0f; if (!(f >> tfloat)) { Con_Printf("ERROR: read error.\n"); return; }
    current_skill = static_cast<int>(tfloat + 0.1f); Cvar::SetValue("skill", static_cast<float>(current_skill));
    char mapname[MAX_QPATH]; if (!(f >> mapname)) { Con_Printf("ERROR: read error.\n"); return; }
    float time = 0.0f; if (!(f >> time)) { Con_Printf("ERROR: read error.\n"); return; }
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    CL_Disconnect_f(); SV_SpawnServer(mapname); if (!sv.active) { Con_Printf("Couldn't load map\n"); return; }
    sv.paused = sv.loadgame = true;
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (!std::getline(f, temp_line)) { Con_Printf("ERROR: read error.\n"); return; }
        sv.lightstyles[i] = static_cast<char*>(Hunk_Alloc(static_cast<int>(temp_line.length()) + 1));
        strcpy_s(sv.lightstyles[i], temp_line.length() + 1, temp_line.c_str());
    }
    int entnum = -1;
    while (true) {
        eastl::string entity_str; char r;
        while (f.get(r)) { if (r == '\0') break; entity_str.push_back(r); if (r == '}') break; }
        if (entity_str.empty()) break;
        const char* start = COM_Parse(entity_str.c_str()); if (!com_token[0]) break;
        if (strcmp(com_token, "{") != 0) Sys_Error("First token isn't a brace");
        if (entnum == -1) ED_ParseGlobals(entity_str.data() + (start - entity_str.c_str()));
        else {
            edict_t* ent = EDICT_NUM(entnum); memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4); ent->free = false;
            ED_ParseEdict(entity_str.data() + (start - entity_str.c_str()), ent);
            if (!ent->free) SV_LinkEdict(ent, false);
        }
        entnum++;
    }
    sv.num_edicts = entnum; sv.time = time;
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) svs.clients->spawn_parms[i] = spawn_parms[i];
    if (cls.state != ca_dedicated) { CL_EstablishConnection("local"); Host_Reconnect_f(); }
}

void Host_Name_f() {
    char newName[64];
    if (Cmd::Argc() == 1) { Con_Printf("\"name\" is \"%s\"\n", cl_name.string.c_str()); return; }
    Q_strncpy(newName, (Cmd::Argc() == 2) ? Cmd::Argv(1) : Cmd::Args(), sizeof(newName) - 1); newName[15] = 0;
    if (Cmd::state.source == Cmd::Source::Command) {
        if (Q_strcmp(cl_name.string.c_str(), newName) == 0) return;
        Cvar::Set("_cl_name", newName); if (cls.state == ca_connected) Cmd::ForwardToServer(); return;
    }
    if (host_client->name[0] && strcmp(host_client->name.data(), "unconnected") != 0 && Q_strcmp(host_client->name.data(), newName) != 0) {
        Con_Printf("%s renamed to %s\n", host_client->name.data(), newName);
    }
    Q_strcpy(host_client->name.data(), newName); host_client->edict->v.netname = PR_SetString(host_client->name.data());
    MSG_WriteByte(&sv.reliable_datagram, svc_updatename); MSG_WriteByte(&sv.reliable_datagram, static_cast<int>(host_client - svs.clients));
    MSG_WriteString(&sv.reliable_datagram, host_client->name.data());
}

void Host_Version_f() { Con_Printf("Version %4.2f\nExe: " __TIME__ " " __DATE__ "\n", VERSION); }

void Host_Say(qboolean teamonly) {
    if (Cmd::state.source == Cmd::Source::Command && cls.state != ca_dedicated) { Cmd::ForwardToServer(); return; }
    if (Cmd::Argc() < 2) return;
    client_t* save = host_client; eastl::string arg_str(Cmd::Args().data(), Cmd::Args().length());
    if (!arg_str.empty() && arg_str.front() == '"') { arg_str = arg_str.substr(1); if (!arg_str.empty() && arg_str.back() == '"') arg_str.pop_back(); }
    eastl::string text_str = (Cmd::state.source == Cmd::Source::Command && cls.state == ca_dedicated)
        ? (eastl::string(1, '\x01') + "<" + hostname.string.c_str() + "> ")
        : (eastl::string(1, '\x01') + save->name.data() + ": ");
    int j = 64 - 2 - static_cast<int>(text_str.length());
    if (j > 0 && arg_str.length() > static_cast<size_t>(j)) arg_str.resize(j);
    text_str += arg_str + "\n";
    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active || !client->spawned || (teamplay.value && teamonly && client->edict->v.team != save->edict->v.team)) continue;
        host_client = client; SV_ClientPrintf("%s", text_str.c_str());
    }
    host_client = save; Sys_Printf("%s", &text_str[1]);
}

void Host_Tell_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (Cmd::Argc() < 3) return;
    eastl::string text_str = eastl::string(host_client->name.data()) + ": ";
    eastl::string arg_str(Cmd::Args().data(), Cmd::Args().length());
    if (!arg_str.empty() && arg_str.front() == '"') { arg_str = arg_str.substr(1); if (!arg_str.empty() && arg_str.back() == '"') arg_str.pop_back(); }
    int j = 64 - 2 - static_cast<int>(text_str.length());
    if (j > 0 && arg_str.length() > static_cast<size_t>(j)) arg_str.resize(j);
    text_str += arg_str + "\n"; client_t* save = host_client;
    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active || !client->spawned || Q_strcasecmp(client->name.data(), Cmd::Argv(1)) != 0) continue;
        host_client = client; SV_ClientPrintf("%s", text_str.c_str()); break;
    }
    host_client = save;
}

void Host_Color_f() {
    if (Cmd::Argc() == 1) { Con_Printf("\"color\" is \"%i %i\"\ncolor <0-13> [0-13]\n", static_cast<int>(cl_color.value) >> 4, static_cast<int>(cl_color.value) & 0x0f); return; }
    int top = Q_atoi(Cmd::Argv(1)), bottom = (Cmd::Argc() == 2) ? top : Q_atoi(Cmd::Argv(2));
    top = eastl::clamp(top & 15, 0, 13); bottom = eastl::clamp(bottom & 15, 0, 13);
    int pcolor = top * 16 + bottom;
    if (Cmd::state.source == Cmd::Source::Command) {
        Cvar::SetValue("_cl_color", static_cast<float>(pcolor)); if (cls.state == ca_connected) Cmd::ForwardToServer(); return;
    }
    host_client->colors = pcolor; host_client->edict->v.team = static_cast<float>(bottom + 1);
    MSG_WriteByte(&sv.reliable_datagram, svc_updatecolors); MSG_WriteByte(&sv.reliable_datagram, static_cast<int>(host_client - svs.clients));
    MSG_WriteByte(&sv.reliable_datagram, host_client->colors);
}

void Host_Kill_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (sv_player->v.health <= 0) { SV_ClientPrintf("Can't suicide -- allready dead!\n"); return; }
    pr_global_struct->time = static_cast<float>(sv.time); pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv_player));
    PR_ExecuteProgram(pr_global_struct->ClientKill);
}

void Host_Pause_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (!pausable.value) SV_ClientPrintf("Pause not allowed.\n");
    else {
        sv.paused ^= 1; SV_BroadcastPrintf("%s %spaused the game\n", PR_GetString(sv_player->v.netname), sv.paused ? "" : "un");
        MSG_WriteByte(&sv.reliable_datagram, svc_setpause); MSG_WriteByte(&sv.reliable_datagram, sv.paused);
    }
}

void Host_PreSpawn_f() {
    if (Cmd::state.source == Cmd::Source::Command || host_client->spawned) { Con_Printf(host_client->spawned ? "prespawn not valid -- allready spawned\n" : "prespawn is not valid from the console\n"); return; }
    SZ_Write(&host_client->message, sv.signon.data, sv.signon.cursize);
    MSG_WriteByte(&host_client->message, svc_signonnum); MSG_WriteByte(&host_client->message, 2); host_client->sendsignon = true;
}

void Host_Spawn_f() {
    if (Cmd::state.source == Cmd::Source::Command || host_client->spawned) { Con_Printf(host_client->spawned ? "Spawn not valid -- allready spawned\n" : "spawn is not valid from the console\n"); return; }
    if (sv.loadgame) sv.paused = false;
    else {
        edict_t* ent = host_client->edict; memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4);
        ent->v.colormap = static_cast<float>(NUM_FOR_EDICT(ent)); ent->v.team = static_cast<float>((host_client->colors & 15) + 1);
        ent->v.netname = PR_SetString(host_client->name.data());
        for (int i = 0; i < NUM_SPAWN_PARMS; i++) (&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
        pr_global_struct->time = static_cast<float>(sv.time); pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv_player));
        PR_ExecuteProgram(pr_global_struct->ClientConnect);
        if ((Sys_FloatTime() - host_client->netconnection->connecttime) <= sv.time) Sys_Printf("%s entered the game\n", host_client->name.data());
        PR_ExecuteProgram(pr_global_struct->PutClientInServer);
    }
    SZ_Clear(&host_client->message); MSG_WriteByte(&host_client->message, svc_time); MSG_WriteFloat(&host_client->message, static_cast<float>(sv.time));
    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        MSG_WriteByte(&host_client->message, svc_updatename); MSG_WriteByte(&host_client->message, i); MSG_WriteString(&host_client->message, client->name.data());
        MSG_WriteByte(&host_client->message, svc_updatefrags); MSG_WriteByte(&host_client->message, i); MSG_WriteShort(&host_client->message, client->old_frags);
        MSG_WriteByte(&host_client->message, svc_updatecolors); MSG_WriteByte(&host_client->message, i); MSG_WriteByte(&host_client->message, client->colors);
    }
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        MSG_WriteByte(&host_client->message, svc_lightstyle); MSG_WriteByte(&host_client->message, static_cast<char>(i)); MSG_WriteString(&host_client->message, sv.lightstyles[i]);
    }
    auto WriteStat = [](int idx, int val) { MSG_WriteByte(&host_client->message, svc_updatestat); MSG_WriteByte(&host_client->message, idx); MSG_WriteLong(&host_client->message, val); };
    WriteStat(STAT_TOTALSECRETS, static_cast<int>(pr_global_struct->total_secrets)); WriteStat(STAT_TOTALMONSTERS, static_cast<int>(pr_global_struct->total_monsters));
    WriteStat(STAT_SECRETS, static_cast<int>(pr_global_struct->found_secrets)); WriteStat(STAT_MONSTERS, static_cast<int>(pr_global_struct->killed_monsters));
    edict_t* ent = EDICT_NUM(1 + static_cast<int>(host_client - svs.clients));
    MSG_WriteByte(&host_client->message, svc_setangle); for (int i = 0; i < 2; i++) MSG_WriteAngle(&host_client->message, ent->v.angles[i]); MSG_WriteAngle(&host_client->message, 0);
    SV_WriteClientdataToMessage(sv_player, &host_client->message); MSG_WriteByte(&host_client->message, svc_signonnum); MSG_WriteByte(&host_client->message, 3);
    host_client->sendsignon = true;
}

void Host_Begin_f() { if (Cmd::state.source != Cmd::Source::Command) host_client->spawned = true; else Con_Printf("begin is not valid from the console\n"); }

void Host_Kick_f() {
    if (Cmd::state.source == Cmd::Source::Command) { if (!sv.active) { Cmd::ForwardToServer(); return; } }
    else if (pr_global_struct->deathmatch && !host_client->privileged) return;
    client_t* save = host_client; bool byNumber = false; int i = 0;
    if (Cmd::Argc() > 2 && Q_strcmp(Cmd::Argv(1), "#") == 0) {
        i = static_cast<int>(Q_atof(Cmd::Argv(2)) - 1);
        if (i < 0 || i >= svs.maxclients || !svs.clients[i].active) return;
        host_client = &svs.clients[i]; byNumber = true;
    } else {
        for (i = 0; i < svs.maxclients; i++) {
            host_client = &svs.clients[i]; if (!host_client->active) continue;
            if (Q_strcasecmp(host_client->name.data(), Cmd::Argv(1)) == 0) break;
        }
    }
    if (i < svs.maxclients) {
        const char* who = (Cmd::state.source == Cmd::Source::Command) ? (cls.state == ca_dedicated ? "Console" : cl_name.string.c_str()) : save->name.data();
        if (host_client == save) return;
        const char* message = nullptr; eastl::string args_holder;
        if (Cmd::Argc() > 2) {
            args_holder = eastl::string(Cmd::Args().data(), Cmd::Args().length());
            const char* ptr = COM_Parse(args_holder.c_str()); if (byNumber) ptr = COM_Parse(ptr);
            while (*ptr == ' ') ptr++; if (*ptr != '\0') message = ptr;
        }
        SV_ClientPrintf(message ? "Kicked by %s: %s\n" : "Kicked by %s\n", who, message); SV_DropClient(false);
    }
    host_client = save;
}

void Host_Give_f() {
    if (Cmd::state.source == Cmd::Source::Command) { Cmd::ForwardToServer(); return; }
    if (pr_global_struct->deathmatch && !host_client->privileged) return;
    eastl::string_view t = Cmd::Argv(1); int v = Q_atoi(Cmd::Argv(2)); if (t.empty()) return;
    auto GiveAmmo = [](const char* name, float v_val, float& std_val) {
        if (rogue) { if (eval_t* val = GetEdictFieldValue(sv_player, name)) val->_float = v_val; }
        std_val = v_val;
    };
    switch (t[0]) {
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        if (hipnotic) {
            if (t[0] == '6') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | ((t.size() > 1 && t[1] == 'a') ? HIT_PROXIMITY_GUN : IT_GRENADE_LAUNCHER));
            else if (t[0] == '9') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | HIT_LASER_CANNON);
            else if (t[0] == '0') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | HIT_MJOLNIR);
            else if (t[0] >= '2') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | (IT_SHOTGUN << (t[0] - '2')));
        } else if (t[0] >= '2') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | (IT_SHOTGUN << (t[0] - '2')));
        break;
    case 's': GiveAmmo("ammo_shells1", static_cast<float>(v), sv_player->v.ammo_shells); break;
    case 'n': GiveAmmo("ammo_nails1", static_cast<float>(v), sv_player->v.ammo_nails); break;
    case 'l': if (rogue) { if (eval_t* val = GetEdictFieldValue(sv_player, "ammo_lava_nails")) { val->_float = static_cast<float>(v); if (sv_player->v.weapon > IT_LIGHTNING) sv_player->v.ammo_nails = static_cast<float>(v); } } break;
    case 'r': GiveAmmo("ammo_rockets1", static_cast<float>(v), sv_player->v.ammo_rockets); break;
    case 'm': if (rogue) { if (eval_t* val = GetEdictFieldValue(sv_player, "ammo_multi_rockets")) { val->_float = static_cast<float>(v); if (sv_player->v.weapon > IT_LIGHTNING) sv_player->v.ammo_rockets = static_cast<float>(v); } } break;
    case 'h': sv_player->v.health = static_cast<float>(v); break;
    case 'c': GiveAmmo("ammo_cells1", static_cast<float>(v), sv_player->v.ammo_cells); break;
    case 'p': if (rogue) { if (eval_t* val = GetEdictFieldValue(sv_player, "ammo_plasma")) { val->_float = static_cast<float>(v); if (sv_player->v.weapon > IT_LIGHTNING) sv_player->v.ammo_cells = static_cast<float>(v); } } break;
    }
}

edict_t* FindViewthing() {
    for (int i = 0; i < sv.num_edicts; i++) {
        edict_t* e = EDICT_NUM(i); if (strcmp(PR_GetString(e->v.classname), "viewthing") == 0) return e;
    }
    Con_Printf("No viewthing on map\n"); return nullptr;
}

void Host_Viewmodel_f() {
    edict_t* e = FindViewthing(); if (!e) return;
    eastl::string arg1(Cmd::Argv(1).data(), Cmd::Argv(1).length()); model_t* m = Mod_ForName(arg1.c_str(), false);
    if (!m) { Con_Printf("Can't load %s\n", arg1.c_str()); return; }
    e->v.frame = 0; cl.model_precache[static_cast<int>(e->v.modelindex)] = m;
}

void Host_Viewframe_f() {
    edict_t* e = FindViewthing(); if (!e) return;
    model_t* m = cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = static_cast<float>(eastl::min(Q_atoi(Cmd::Argv(1)), m->numframes - 1));
}

void PrintFrameName(model_t* m, int frame) {
    if (aliashdr_t* hdr = static_cast<aliashdr_t*>(Mod_Extradata(m))) Con_Printf("frame %i: %s\n", frame, hdr->frames[frame].name);
}

void Host_Viewnext_f() {
    edict_t* e = FindViewthing(); if (!e) return;
    model_t* m = cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = eastl::min<float>(e->v.frame + 1.0f, static_cast<float>(m->numframes - 1));
    PrintFrameName(m, static_cast<int>(e->v.frame));
}

void Host_Viewprev_f() {
    edict_t* e = FindViewthing(); if (!e) return;
    model_t* m = cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = eastl::max<float>(e->v.frame - 1.0f, 0.0f);
    PrintFrameName(m, static_cast<int>(e->v.frame));
}

void Host_Startdemos_f() {
    if (cls.state == ca_dedicated) { if (!sv.active) Cmd::BufferAddText("map start\n"); return; }
    int c = eastl::min<int>(Cmd::Argc() - 1, MAX_DEMOS); Con_Printf("%i demo(s) in loop\n", c);
    for (int i = 0; i < MAX_DEMOS; i++) cls.demos[i][0] = 0;
    for (int i = 1; i <= c; i++) {
        eastl::string_view arg = Cmd::Argv(i);
        sprintf_s(cls.demos[i - 1].data(), cls.demos[i - 1].size(), "%.*s", static_cast<int>(arg.length()), arg.data());
    }
    if (!sv.active && cls.state != ca_connected && !cls.demoplayback) { cls.demonum = 0; CL_NextDemo(); } else cls.demonum = -1;
}

void Host_Demos_f() { if (cls.state != ca_dedicated) { if (cls.demonum == -1) cls.demonum = 1; CL_Disconnect_f(); CL_NextDemo(); } }
void Host_Stopdemo_f() { if (cls.state != ca_dedicated && cls.demoplayback) { CL_StopPlayback(); CL_Disconnect(); } }

struct CmdPair { const char* name; void (*fn)(); };

void Host_InitCommands() {
    constexpr CmdPair cmds[] = {
        {"status", Host_Status_f}, {"quit", Host_Quit_f}, {"god", Host_God_f}, {"notarget", Host_Notarget_f},
        {"fly", Host_Fly_f}, {"map", Host_Map_f}, {"restart", Host_Restart_f}, {"changelevel", Host_Changelevel_f},
        {"connect", Host_Connect_f}, {"reconnect", Host_Reconnect_f}, {"name", Host_Name_f}, {"noclip", Host_Noclip_f},
        {"version", Host_Version_f}, {"say", []() { Host_Say(false); }}, {"say_team", []() { Host_Say(true); }},
        {"tell", Host_Tell_f}, {"color", Host_Color_f}, {"kill", Host_Kill_f}, {"pause", Host_Pause_f},
        {"spawn", Host_Spawn_f}, {"begin", Host_Begin_f}, {"prespawn", Host_PreSpawn_f}, {"kick", Host_Kick_f},
        {"ping", Host_Ping_f}, {"load", Host_Loadgame_f}, {"save", Host_Savegame_f}, {"give", Host_Give_f},
        {"startdemos", Host_Startdemos_f}, {"demos", Host_Demos_f}, {"stopdemo", Host_Stopdemo_f},
        {"viewmodel", Host_Viewmodel_f}, {"viewframe", Host_Viewframe_f}, {"viewnext", Host_Viewnext_f},
        {"viewprev", Host_Viewprev_f}, {"mcache", Mod_Print}
    };
    for (auto [name, fn] : cmds) Cmd::AddCommand(name, fn);
}

} // namespace Host
