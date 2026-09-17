// host.cpp -- Central engine host orchestration (loop, state, error handling, dispatch)
#include "host/host.hpp"
#include "render/renderer.hpp"
#include "render/software/sw_renderer.hpp"
#include "platform/crt_compat.hpp"
#include "core/wad.hpp"
#include "render/software/sw_vid.hpp"
#include "audio/audio_main.hpp"
#include "core/math.hpp"
#include "vm/program.hpp"
#include "ui/screen.hpp"
#include "core/cvar.hpp"
#include "core/string_utils.hpp"
#include "network/net_main.hpp"
#include "client/client_types.hpp"
#include "render/software/sw_surf.hpp"
#include "core/msg.hpp"
#include "ui/menu.hpp"
#include "server/server.hpp"
#include "core/types.hpp"
#include "ui/hud.hpp"
#include "network/socket.hpp"
#include "core/filesystem.hpp"
#include "quakedef.hpp"
#include "core/cmd.hpp"
#include "world/model.hpp"
#include "render/render_types.hpp"
#include "platform/system.hpp"
#include "vm/edict.hpp"
#include "render/software/sw_main.hpp"
#include "server/sv_send.hpp"
#include "client/view.hpp"
#include "vm/interpreter.hpp"
#include "ui/console.hpp"
#include "server/server_types.hpp"
#include "world/collision.hpp"
#include "client/input.hpp"
#include "render/software/sw_local.hpp"
#include "server/world.hpp"
#include "network/protocol.hpp"
#include "client/cl_tent.hpp"
#include "client/cl_main.hpp"
#include "server/physics.hpp"
#include "client/chase.hpp"
#include "client/cl_demo.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>
#include <cstring>

namespace Host {

quakeparms_t host_parms;
qboolean host_initialized;
double host_frametime, host_time, realtime, oldrealtime;
int host_framecount;
client_t* host_client;
byte *host_basepal, *host_colormap;
static std::vector<byte> palette_data, colormap_data;

cvar_t host_framerate = { "host_framerate", "0", { }, { }, { }, { } };
cvar_t host_speeds = { "host_speeds", "0", { }, { }, { }, { } };
cvar_t sys_ticrate = { "sys_ticrate", "0.05", { }, { }, { }, { } };
cvar_t serverprofile = { "serverprofile", "0", { }, { }, { }, { } };
cvar_t samelevel = { "samelevel", "0", { }, { }, { }, { } };
cvar_t noexit = { "noexit", "0", false, true, { }, { } };
cvar_t developer = { "developer", "0", { }, { }, { }, { } };
cvar_t pausable = { "pausable", "1", { }, { }, { }, { } };
cvar_t temp1 = { "temp1", "0", { }, { }, { }, { } };

[[noreturn]] void Host_EndGame(const char* message, ...)
{
    va_list argptr;
    char string[1024];
    va_start(argptr, message);
    vsprintf_s(string, sizeof(string), message, argptr);
    va_end(argptr);
    Console::Con_DPrintf("Host_EndGame: %s\n", string);
    if (Server::sv.active) Host_ShutdownServer(false);
    if (Client::cls.state == ca_dedicated) Common::Sys_Error("Host_EndGame: %s\n", string);
    if (Client::cls.demonum != -1)
        Client::CL_NextDemo();
    else
        Client::CL_Disconnect();
    throw HostAbort { };
}

[[noreturn]] void Host_Error(const char* error, ...)
{
    va_list argptr;
    char string[1024];
    static qboolean inerror = false;
    if (inerror) Common::Sys_Error("Host_Error: recursively entered");
    inerror = true;
    Screen::GetScreenSystem().EndLoadingPlaque();
    va_start(argptr, error);
    vsprintf_s(string, sizeof(string), error, argptr);
    va_end(argptr);
    Console::Con_Printf("Host_Error: %s\n", string);
    Common::Sys_Printf("Host_Error: %s\n", string);
    if (Server::sv.active) Host_ShutdownServer(false);
    if (Client::cls.state == ca_dedicated) Common::Sys_Error("Host_Error: %s\n", string);
    Client::CL_Disconnect();
    Client::cls.demonum = -1;
    VM::PR_ResetExecutionState();
    inerror = false;
    throw HostAbort { };
}

void Host_FindMaxClients()
{
    Server::svs.maxclients = 1;
    if (int i = Common::COM_CheckParm("-dedicated")) {
        Client::cls.state = ca_dedicated;
        Server::svs.maxclients = (i != Common::com_argc - 1) ? Common::Q_atoi(Common::com_argv[i + 1]) : 8;
    } else
        Client::cls.state = ca_disconnected;

    if (int i = Common::COM_CheckParm("-listen")) {
        if (Client::cls.state == ca_dedicated) Common::Sys_Error("Only one of -dedicated or -listen can be specified");
        Server::svs.maxclients = (i != Common::com_argc - 1) ? Common::Q_atoi(Common::com_argv[i + 1]) : 8;
    }
    Server::svs.maxclients = std::clamp(Server::svs.maxclients, 1, MAX_SCOREBOARD);
    Server::svs.maxclientslimit = std::max(4, Server::svs.maxclients);
    Server::svs.resize_clients(Server::svs.maxclientslimit);
    Cvar::SetValue("deathmatch", (Server::svs.maxclients > 1) ? 1.0 : 0.0);
}

void Host_InitLocal()
{
    Host_InitCommands();
    for (auto* c : { &host_framerate, &host_speeds, &sys_ticrate, &serverprofile, &Server::fraglimit,
             &Server::timelimit, &Server::teamplay, &samelevel, &noexit, &Server::skill, &developer,
             &Server::deathmatch, &Server::coop, &pausable, &temp1 })
        Cvar::Register(c);
    Host_FindMaxClients();
    host_time = 1.0;
}

void Host_WriteConfiguration()
{
    if (host_initialized && !isDedicated) {
        std::ofstream f((std::string(Common::com_gamedir) + "/config.cfg").c_str());
        if (f.is_open()) {
            Keys::Key_WriteBindings(f);
            Cvar::WriteVariables(f);
        } else
            Console::Con_Printf("Couldn't write config.cfg.\n");
    }
}

void Host_ClientCommands(const char* fmt, ...)
{
    va_list argptr;
    char string[1024];
    va_start(argptr, fmt);
    vsprintf_s(string, sizeof(string), fmt, argptr);
    va_end(argptr);
    Common::MSG_WriteByte(&host_client->message, svc_stufftext);
    Common::MSG_WriteString(&host_client->message, string);
}

void Host_ShutdownServer(qboolean crash)
{
    if (!Server::sv.active) return;
    Server::sv.active = false;
    if (Client::cls.state == ca_connected) Client::CL_Disconnect();
    double start = Common::Sys_FloatTime();
    int count;
    do {
        count = 0;
        for (int i = 0; i < Server::svs.maxclients; i++) {
            host_client = &Server::svs.clients[i];
            if (host_client->active && host_client->message.cursize) {
                if (Net::NET_CanSendMessage(host_client->netconnection)) {
                    Net::NET_SendMessage(host_client->netconnection, &host_client->message);
                    Common::SZ_Clear(&host_client->message);
                } else {
                    Net::NET_GetMessage(host_client->netconnection);
                    count++;
                }
            }
        }
        if ((Common::Sys_FloatTime() - start) > 3.0) break;
    } while (count);

    char message[4];
    sizebuf_t buf { };
    buf.data = (byte*)message;
    buf.maxsize = 4;
    buf.cursize = 0;
    Common::MSG_WriteByte(&buf, svc_disconnect);
    count = Net::NET_SendToAll(&buf, 5);
    if (count) Console::Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);
    for (int i = 0; i < Server::svs.maxclients; i++) {
        host_client = &Server::svs.clients[i];
        if (host_client->active) Server::SV_DropClient(crash);
    }
    Server::sv = { };
    for (int i = 0; i < Server::svs.maxclientslimit; i++) Server::svs.clients[i] = { };
}

void Host_ClearMemory()
{
    Console::Con_DPrintf("Clearing memory\n");
    Render::D_FlushCaches();
    Model::Mod_ClearAll();
    Client::cls.signon = 0;
    Server::sv = { };
    Client::cl = { };
}

qboolean Host_FilterTime(float time)
{
    realtime += time;
    if (!Client::cls.timedemo && realtime - oldrealtime < 1.0 / 72.0) return false;
    host_frametime = realtime - oldrealtime;
    oldrealtime = realtime;
    if (host_framerate.value > 0)
        host_frametime = host_framerate.value;
    else
        host_frametime = std::clamp(host_frametime, 0.001, 0.1);
    return true;
}

void Host_GetConsoleCommands()
{
    while (char* cmd = Common::Sys_ConsoleInput()) Cmd::BufferAddText(cmd);
}

void Host_ServerFrame()
{
    VM::pr_global_struct->frametime = static_cast<float>(host_frametime);
    Common::SZ_Clear(&Server::sv.datagram);
    Server::SV_CheckForNewClients();
    Server::SV_RunClients();
    if (!Server::sv.paused && (Server::svs.maxclients > 1 || Keys::key_dest == Keys::key_game)) Server::SV_Physics();
    Server::SV_SendClientMessages();
}

void _Host_Frame(float time)
{
    static double time1 = 0, time2 = 0, time3 = 0;
    rand();
    if (!Host_FilterTime(time)) return;
    Common::Sys_SendKeyEvents();
    Input::IN_Commands();
    Cmd::BufferExecute();
    Net::NET_Poll();
    if (Server::sv.active) Client::CL_SendCmd();
    Host_GetConsoleCommands();
    if (Server::sv.active) Host_ServerFrame();
    if (!Server::sv.active) Client::CL_SendCmd();
    host_time += host_frametime;
    if (Client::cls.state == ca_connected) Client::CL_ReadFromServer();
    if (host_speeds.value) time1 = Common::Sys_FloatTime();
    Screen::GetScreenSystem().UpdateScreen();
    if (host_speeds.value) time2 = Common::Sys_FloatTime();
    if (Client::cls.signon == SIGNONS) {
        Audio::S_Update(Render::r_origin, Render::vpn, Render::vright, Render::vup);
        Client::CL_DecayLights();
    } else
        Audio::S_Update(Math::vec3_origin, Math::vec3_origin, Math::vec3_origin, Math::vec3_origin);
    if (host_speeds.value) {
        int p1 = static_cast<int>((time1 - time3) * 1000), p2 = static_cast<int>((time2 - time1) * 1000);
        time3 = Common::Sys_FloatTime();
        int p3 = static_cast<int>((time3 - time2) * 1000);
        Console::Con_Printf("%3i tot %3i server %3i gfx %3i snd\n", p1 + p2 + p3, p1, p2, p3);
    }
    host_framecount++;
}

void Host_Frame(float time)
{
    static double timetotal = 0.0;
    static int timecount = 0;

    try {
        if (!serverprofile.value) {
            _Host_Frame(time);
            return;
        }
        double time1 = Common::Sys_FloatTime();
        _Host_Frame(time);
        double time2 = Common::Sys_FloatTime();
        timetotal += time2 - time1;
        timecount++;
    } catch (const HostAbort&) {
        // Host_Error already tore down the server and client; resume at the next frame.
        return;
    }

    if (timecount < 1000) return;
    int m = static_cast<int>(timetotal * 1000 / timecount);
    timecount = 0;
    timetotal = 0;
    int c = static_cast<int>(std::count_if(Server::svs.clients, Server::svs.clients + Server::svs.maxclients,
        [](const client_t& cl) { return cl.active; }));
    Console::Con_Printf("serverprofile: %2i clients %2i msec\n", c, m);
}

void Host_Init(quakeparms_t* parms)
{
    host_parms = *parms;
    Common::com_argc = parms->argc;
    Common::com_argv = parms->argv;
    Cmd::BufferInit();
    Cmd::Init();
    View::V_Init();
    Client::Chase_Init();
    Common::COM_Init(parms->basedir);
    Host_InitLocal();
    Wad::W_LoadWadFile("gfx.wad");
    Keys::Key_Init();
    Console::GetConsoleSystem().Init();
    Menu::M_Init();
    VM::PR_Init();
    Model::Mod_Init();
    Net::NET_Init();
    Server::SV_Init();
    Console::Con_Printf("Exe: " __TIME__ " " __DATE__ "\n");
    Render::R_InitTextures();
    if (Client::cls.state != ca_dedicated) {
        palette_data = Common::COM_LoadFile("gfx/palette.lmp");
        if (palette_data.empty()) Common::Sys_Error("Couldn't load gfx/palette.lmp");
        host_basepal = palette_data.data();
        colormap_data = Common::COM_LoadFile("gfx/colormap.lmp");
        if (colormap_data.empty()) Common::Sys_Error("Couldn't load gfx/colormap.lmp");
        host_colormap = colormap_data.data();
        static Render::SoftwareRenderer sw_renderer;
        Render::SetRenderer(&sw_renderer);
        Vid::VID_Init(host_basepal);
        Draw::Draw_Init();
        Screen::GetScreenSystem().Init();
        Render::GetRenderer()->Init();
        Audio::S_Init();
        Sbar::Sbar_Init();
        Client::CL_Init();
        Input::IN_Init();
    }
    Cmd::BufferInsertText("exec quake.rc\n");
    host_initialized = true;
    Common::Sys_Printf("========Quake Initialized=========\n");
}

void Host_Shutdown()
{
    static qboolean isdown = false;
    if (isdown) {
        printf("recursive shutdown\n");
        return;
    }
    isdown = true;
    Screen::GetScreenSystem().SetDisabledForLoading(true);
    Host_WriteConfiguration();
    Net::NET_Shutdown();
    Audio::S_Shutdown();
    Input::IN_Shutdown();
    if (Client::cls.state != ca_dedicated) Vid::VID_Shutdown();
}

namespace {

void HashBytes(uint64_t& hash, const void* data, size_t length)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL; // FNV-1a prime
    }
}

} // namespace

uint64_t Host_StateHash()
{
    uint64_t hash = 1469598103934665603ULL; // FNV-1a offset basis

    const unsigned char server_active = Server::sv.active ? 1 : 0;
    HashBytes(hash, &server_active, sizeof(server_active));
    if (Server::sv.active && VM::progs) {
        HashBytes(hash, &Server::sv.time, sizeof(Server::sv.time));
        HashBytes(hash, &Server::sv.num_edicts, sizeof(Server::sv.num_edicts));
        for (int i = 0; i < Server::sv.num_edicts; ++i) {
            const edict_t* ent = VM::EDICT_NUM(i);
            const unsigned char is_free = ent->free ? 1 : 0;
            HashBytes(hash, &is_free, sizeof(is_free));
            HashBytes(hash, &ent->v, static_cast<size_t>(VM::progs->entityfields) * 4);
        }
        HashBytes(hash, VM::pr_globals, static_cast<size_t>(VM::progs->numglobals) * 4);
    }

    if (Client::cls.state != ca_dedicated) {
        HashBytes(hash, &Client::cl.time, sizeof(Client::cl.time));
        HashBytes(hash, &Client::cl.num_entities, sizeof(Client::cl.num_entities));
        for (int i = 0; i < Client::cl.num_entities; ++i) {
            const entity_t& ent = Client::cl_entities[i];
            HashBytes(hash, &ent.origin, sizeof(ent.origin));
            HashBytes(hash, &ent.angles, sizeof(ent.angles));
            HashBytes(hash, &ent.frame, sizeof(ent.frame));
            HashBytes(hash, &ent.skinnum, sizeof(ent.skinnum));
            HashBytes(hash, &ent.effects, sizeof(ent.effects));
        }
        if (Vid::vid.buffer) {
            for (unsigned y = 0; y < Vid::vid.height; ++y) {
                HashBytes(hash, Vid::vid.buffer + y * Vid::vid.rowbytes, Vid::vid.width);
            }
        }
    }
    return hash;
}

void Host_PrintStateHash()
{
    std::printf("STATEHASH frame=%d edicts=%d hash=%016llx\n", host_framecount, Server::sv.num_edicts,
        static_cast<unsigned long long>(Host_StateHash()));
    std::fflush(stdout);
}

int current_skill;
void Host_Quit_f()
{
    if (Keys::key_dest != Keys::key_console && Client::cls.state != ca_dedicated) {
        Menu::M_Menu_Quit_f();
        return;
    }
    Client::CL_Disconnect();
    Host_ShutdownServer(false);
    Common::Sys_Quit();
}

void Host_Status_f()
{
    void (*print)(const char*, ...) = Server::SV_ClientPrintf;
    if (Cmd::state.source == Cmd::Source::Command) {
        if (!Server::sv.active) {
            Cmd::ForwardToServer();
            return;
        }
        print = Console::Con_Printf;
    }

    print("host:    %s\nversion: %4.2f\n", Net::hostname.string.c_str(), VERSION);
    if (Net::tcpipAvailable) print("tcp/ip:  %s\n", Net::my_tcpip_address);
    print("map:     %s\nplayers: %i active (%i max)\n\n", Server::sv.name.data(), Net::net_activeconnections,
        Server::svs.maxclients);
    for (int j = 0; j < Server::svs.maxclients; j++) {
        const client_t* client = &Server::svs.clients[j];
        if (!client->active) continue;
        int seconds = static_cast<int>(Net::net_time - client->netconnection->connecttime);
        int minutes = seconds / 60;
        int hours = minutes / 60;
        seconds %= 60;
        minutes %= 60;
        print("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n   %s\n", j + 1, client->name.data(),
            static_cast<int>(client->edict->v.frags), hours, minutes, seconds, client->netconnection->address);
    }
}

static inline void Host_ToggleCheatFlag(int flag, const char* name)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (VM::pr_global_struct->deathmatch && !host_client->privileged) return;
    Server::sv_player->v.flags = static_cast<float>(static_cast<int>(Server::sv_player->v.flags) ^ flag);
    Server::SV_ClientPrintf("%s %s\n", name, (static_cast<int>(Server::sv_player->v.flags) & flag) ? "ON" : "OFF");
}

void Host_God_f()
{
    Host_ToggleCheatFlag(FL_GODMODE, "godmode");
}
void Host_Notarget_f()
{
    Host_ToggleCheatFlag(FL_NOTARGET, "notarget");
}
qboolean noclip_anglehack;
void Host_Noclip_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (VM::pr_global_struct->deathmatch && !host_client->privileged) return;
    noclip_anglehack = (Server::sv_player->v.movetype != MOVETYPE_NOCLIP);
    Server::sv_player->v.movetype = noclip_anglehack ? MOVETYPE_NOCLIP : MOVETYPE_WALK;
    Server::SV_ClientPrintf("noclip %s\n", noclip_anglehack ? "ON" : "OFF");
}
void Host_Fly_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (VM::pr_global_struct->deathmatch && !host_client->privileged) return;
    bool fly = (Server::sv_player->v.movetype != MOVETYPE_FLY);
    Server::sv_player->v.movetype = fly ? MOVETYPE_FLY : MOVETYPE_WALK;
    Server::SV_ClientPrintf("flymode %s\n", fly ? "ON" : "OFF");
}
void Host_Ping_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    Server::SV_ClientPrintf("Client ping times:\n");
    for (int i = 0; i < Server::svs.maxclients; i++) {
        client_t* client = &Server::svs.clients[i];
        if (!client->active) continue;
        float total = 0.0f;
        for (float p : client->ping_times) total += p;
        Server::SV_ClientPrintf("%4i %s\n", static_cast<int>(total * 1000.0f / NUM_PING_TIMES), client->name.data());
    }
}

void Host_Map_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    Client::cls.demonum = -1;
    Client::CL_Disconnect();
    Host_ShutdownServer(false);
    Keys::key_dest = Keys::key_game;
    Screen::GetScreenSystem().BeginLoadingPlaque();
    std::string mapstring;
    for (int i = 0; i < Cmd::Argc(); i++) mapstring += std::string(Cmd::Argv(i)) + " ";
    strcpy_s(Client::cls.mapstring.data(), Client::cls.mapstring.size(), (mapstring + "\n").c_str());
    Server::svs.serverflags = 0;
    char name[MAX_QPATH];
    Common::Q_strncpy(name, Cmd::Argv(1), sizeof(name) - 1);
    Server::SV_SpawnServer(name);
    if (!Server::sv.active || Client::cls.state == ca_dedicated) return;
    std::string spawnparms;
    for (int i = 2; i < Cmd::Argc(); i++) spawnparms += std::string(Cmd::Argv(i)) + " ";
    strcpy_s(Client::cls.spawnparms.data(), Client::cls.spawnparms.size(), spawnparms.c_str());
    Cmd::ExecuteString("connect local", Cmd::Source::Command);
}

void Host_Changelevel_f()
{
    if (Cmd::Argc() != 2) {
        Console::Con_Printf("changelevel <levelname> : continue game on a new level\n");
        return;
    }
    if (!Server::sv.active || Client::cls.demoplayback) {
        Console::Con_Printf("Only the server may changelevel\n");
        return;
    }
    Server::SV_SaveSpawnparms();
    char level[MAX_QPATH];
    Common::Q_strcpy(level, Cmd::Argv(1));
    Server::SV_SpawnServer(level);
}

void Host_Restart_f()
{
    if (Client::cls.demoplayback || !Server::sv.active || Cmd::state.source != Cmd::Source::Command) return;
    char mapname[MAX_QPATH];
    strcpy_s(mapname, sizeof(mapname), Server::sv.name.data());
    Server::SV_SpawnServer(mapname);
}

void Host_Reconnect_f()
{
    Screen::GetScreenSystem().BeginLoadingPlaque();
    Client::cls.signon = 0;
}
void Host_Connect_f()
{
    Client::cls.demonum = -1;
    if (Client::cls.demoplayback) {
        Client::CL_StopPlayback();
        Client::CL_Disconnect();
    }
    std::string_view args = Cmd::Args();
    while (!args.empty() && (args.front() == ' ' || args.front() == '\t')) args.remove_prefix(1);
    while (!args.empty() && (args.back() == ' ' || args.back() == '\t' || args.back() == '\r' || args.back() == '\n'))
        args.remove_suffix(1);
    char name[MAX_QPATH];
    if (!args.empty()) {
        Common::Q_strncpy(name, std::string(args.data(), args.length()).c_str(), sizeof(name) - 1);
    } else {
        Common::Q_strcpy(name, Cmd::Argv(1));
    }
    Client::CL_EstablishConnection(name);
    Host_Reconnect_f();
}

constexpr int SAVEGAME_VERSION = 5;

std::string Host_SavegameComment()
{
    std::string text(SAVEGAME_COMMENT_LENGTH, '_');
    std::string levelname = Client::cl.levelname.data();
    if (levelname.length() > 22) levelname = levelname.substr(0, 22);
    text.replace(0, levelname.length(), levelname);
    char kills[20];
    sprintf_s(
        kills, sizeof(kills), "kills:%3i/%3i", Client::cl.stats[STAT_MONSTERS], Client::cl.stats[STAT_TOTALMONSTERS]);
    std::string kills_str = kills;
    if (kills_str.length() > (SAVEGAME_COMMENT_LENGTH - 22))
        kills_str = kills_str.substr(0, SAVEGAME_COMMENT_LENGTH - 22);
    text.replace(22, kills_str.length(), kills_str);
    for (char& c : text) {
        if (c == ' ') c = '_';
    }
    return text;
}

void Host_Savegame_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (!Server::sv.active) {
        Console::Con_Printf("Not playing a local game.\n");
        return;
    }
    if (Client::cl.intermission || Server::svs.maxclients != 1) {
        Console::Con_Printf(
            Client::cl.intermission ? "Can't save in intermission.\n" : "Can't save multiplayer games.\n");
        return;
    }
    if (Cmd::Argc() != 2 || Cmd::Argv(1).find("..") != std::string_view::npos) {
        Console::Con_Printf(
            Cmd::Argc() != 2 ? "save <savename> : save a game\n" : "Relative pathnames are not allowed.\n");
        return;
    }
    for (int i = 0; i < Server::svs.maxclients; i++) {
        if (Server::svs.clients[i].active && Server::svs.clients[i].edict->v.health <= 0) {
            Console::Con_Printf("Can't savegame with a dead player\n");
            return;
        }
    }
    char name[256];
    sprintf_s(name, sizeof(name), "%s/%.*s", Common::com_gamedir, static_cast<int>(Cmd::Argv(1).length()),
        Cmd::Argv(1).data());
    Common::COM_DefaultExtension(name, ".sav");
    Console::Con_Printf("Saving game to %s...\n", name);
    std::ofstream f(name);
    if (!f.is_open()) {
        Console::Con_Printf("ERROR: couldn't open.\n");
        return;
    }
    f << SAVEGAME_VERSION << "\n" << Host_SavegameComment().c_str() << "\n";
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) f << Server::svs.clients->spawn_parms[i] << "\n";
    f << current_skill << "\n" << Server::sv.name.data() << "\n" << Server::sv.time << "\n";
    for (int i = 0; i < MAX_LIGHTSTYLES; i++)
        f << (Server::sv.lightstyles[i] ? Server::sv.lightstyles[i] : "m") << "\n";
    VM::ED_WriteGlobals(f);
    for (int i = 0; i < Server::sv.num_edicts; i++) {
        VM::ED_Write(f, VM::EDICT_NUM(i));
        f.flush();
    }
    Console::Con_Printf("done.\n");
}

void Host_Loadgame_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (Cmd::Argc() != 2) {
        Console::Con_Printf("load <savename> : load a game\n");
        return;
    }
    Client::cls.demonum = -1;
    char name[MAX_OSPATH];
    sprintf_s(name, sizeof(name), "%s/%.*s", Common::com_gamedir, static_cast<int>(Cmd::Argv(1).length()),
        Cmd::Argv(1).data());
    Common::COM_DefaultExtension(name, ".sav");
    Console::Con_Printf("Loading game from %s...\n", name);
    std::ifstream f(name);
    if (!f.is_open()) {
        Console::Con_Printf("ERROR: couldn't open.\n");
        return;
    }
    int version = 0;
    if (!(f >> version)) {
        Console::Con_Printf("ERROR: read error.\n");
        return;
    }
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    if (version != SAVEGAME_VERSION) {
        Console::Con_Printf("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
        return;
    }
    std::string temp_line;
    if (!std::getline(f, temp_line)) {
        Console::Con_Printf("ERROR: read error.\n");
        return;
    }
    float spawn_parms[NUM_SPAWN_PARMS];
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) {
        if (!(f >> spawn_parms[i])) {
            Console::Con_Printf("ERROR: read error.\n");
            return;
        }
    }
    float tfloat = 0.0f;
    if (!(f >> tfloat)) {
        Console::Con_Printf("ERROR: read error.\n");
        return;
    }
    current_skill = static_cast<int>(tfloat + 0.1f);
    Cvar::SetValue("skill", static_cast<float>(current_skill));
    char mapname[MAX_QPATH];
    if (!(f >> mapname)) {
        Console::Con_Printf("ERROR: read error.\n");
        return;
    }
    float time = 0.0f;
    if (!(f >> time)) {
        Console::Con_Printf("ERROR: read error.\n");
        return;
    }
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    Client::CL_Disconnect_f();
    Server::SV_SpawnServer(mapname);
    if (!Server::sv.active) {
        Console::Con_Printf("Couldn't load map\n");
        return;
    }
    Server::sv.paused = Server::sv.loadgame = true;
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (!std::getline(f, temp_line)) {
            Console::Con_Printf("ERROR: read error.\n");
            return;
        }
        Server::sv.loaded_lightstyles[i] = temp_line;
        Server::sv.lightstyles[i] = Server::sv.loaded_lightstyles[i].c_str();
    }
    int entnum = -1;
    while (true) {
        std::string entity_str;
        char r;
        while (f.get(r)) {
            if (r == '\0') break;
            entity_str.push_back(r);
            if (r == '}') break;
        }
        if (entity_str.empty()) break;
        const char* start = Common::COM_Parse(entity_str.c_str());
        if (!Common::com_token[0]) break;
        if (strcmp(Common::com_token, "{") != 0) Common::Sys_Error("First token isn't a brace");
        if (entnum == -1)
            VM::ED_ParseGlobals(entity_str.data() + (start - entity_str.c_str()));
        else {
            edict_t* ent = VM::EDICT_NUM(entnum);
            memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(VM::progs->entityfields) * 4);
            ent->free = false;
            VM::ED_ParseEdict(entity_str.data() + (start - entity_str.c_str()), ent);
            if (!ent->free) Server::SV_LinkEdict(ent, false);
        }
        entnum++;
    }
    Server::sv.num_edicts = entnum;
    Server::sv.time = time;
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) Server::svs.clients->spawn_parms[i] = spawn_parms[i];
    if (Client::cls.state != ca_dedicated) {
        Client::CL_EstablishConnection("local");
        Host_Reconnect_f();
    }
}

void Host_Name_f()
{
    char newName[64];
    if (Cmd::Argc() == 1) {
        Console::Con_Printf("\"name\" is \"%s\"\n", Client::cl_name.string.c_str());
        return;
    }
    Common::Q_strncpy(newName, (Cmd::Argc() == 2) ? Cmd::Argv(1) : Cmd::Args(), sizeof(newName) - 1);
    newName[15] = 0;
    if (Cmd::state.source == Cmd::Source::Command) {
        if (Common::Q_strcmp(Client::cl_name.string.c_str(), newName) == 0) return;
        Cvar::Set("_cl_name", newName);
        if (Client::cls.state == ca_connected) Cmd::ForwardToServer();
        return;
    }
    if (host_client->name[0] && strcmp(host_client->name.data(), "unconnected") != 0
        && Common::Q_strcmp(host_client->name.data(), newName) != 0) {
        Console::Con_Printf("%s renamed to %s\n", host_client->name.data(), newName);
    }
    Common::Q_strcpy(host_client->name.data(), newName);
    host_client->edict->v.netname = VM::PR_SetString(host_client->name.data());
    Common::MSG_WriteByte(&Server::sv.reliable_datagram, svc_updatename);
    Common::MSG_WriteByte(&Server::sv.reliable_datagram, static_cast<int>(host_client - Server::svs.clients));
    Common::MSG_WriteString(&Server::sv.reliable_datagram, host_client->name.data());
}

void Host_Version_f()
{
    Console::Con_Printf("Version %4.2f\nExe: " __TIME__ " " __DATE__ "\n", VERSION);
}

void Host_Say(qboolean teamonly)
{
    if (Cmd::state.source == Cmd::Source::Command && Client::cls.state != ca_dedicated) {
        Cmd::ForwardToServer();
        return;
    }
    if (Cmd::Argc() < 2) return;
    client_t* save = host_client;
    std::string arg_str(Cmd::Args().data(), Cmd::Args().length());
    if (!arg_str.empty() && arg_str.front() == '"') {
        arg_str = arg_str.substr(1);
        if (!arg_str.empty() && arg_str.back() == '"') arg_str.pop_back();
    }
    std::string text_str = (Cmd::state.source == Cmd::Source::Command && Client::cls.state == ca_dedicated)
        ? (std::string(1, '\x01') + "<" + Net::hostname.string.c_str() + "> ")
        : (std::string(1, '\x01') + save->name.data() + ": ");
    int j = 64 - 2 - static_cast<int>(text_str.length());
    if (j > 0 && arg_str.length() > static_cast<size_t>(j)) arg_str.resize(j);
    text_str += arg_str + "\n";
    for (int i = 0; i < Server::svs.maxclients; i++) {
        client_t* client = &Server::svs.clients[i];
        if (!client->active || !client->spawned
            || (Server::teamplay.value && teamonly && client->edict->v.team != save->edict->v.team))
            continue;
        host_client = client;
        Server::SV_ClientPrintf("%s", text_str.c_str());
    }
    host_client = save;
    Common::Sys_Printf("%s", &text_str[1]);
}

void Host_Tell_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (Cmd::Argc() < 3) return;
    std::string text_str = std::string(host_client->name.data()) + ": ";
    std::string arg_str(Cmd::Args().data(), Cmd::Args().length());
    if (!arg_str.empty() && arg_str.front() == '"') {
        arg_str = arg_str.substr(1);
        if (!arg_str.empty() && arg_str.back() == '"') arg_str.pop_back();
    }
    int j = 64 - 2 - static_cast<int>(text_str.length());
    if (j > 0 && arg_str.length() > static_cast<size_t>(j)) arg_str.resize(j);
    text_str += arg_str + "\n";
    client_t* save = host_client;
    for (int i = 0; i < Server::svs.maxclients; i++) {
        client_t* client = &Server::svs.clients[i];
        if (!client->active || !client->spawned || Common::Q_strcasecmp(client->name.data(), Cmd::Argv(1)) != 0)
            continue;
        host_client = client;
        Server::SV_ClientPrintf("%s", text_str.c_str());
        break;
    }
    host_client = save;
}

void Host_Color_f()
{
    if (Cmd::Argc() == 1) {
        Console::Con_Printf("\"color\" is \"%i %i\"\ncolor <0-13> [0-13]\n",
            static_cast<int>(Client::cl_color.value) >> 4, static_cast<int>(Client::cl_color.value) & 0x0f);
        return;
    }
    int top = Common::Q_atoi(Cmd::Argv(1)), bottom = (Cmd::Argc() == 2) ? top : Common::Q_atoi(Cmd::Argv(2));
    top = std::clamp(top & 15, 0, 13);
    bottom = std::clamp(bottom & 15, 0, 13);
    int pcolor = top * 16 + bottom;
    if (Cmd::state.source == Cmd::Source::Command) {
        Cvar::SetValue("_cl_color", static_cast<float>(pcolor));
        if (Client::cls.state == ca_connected) Cmd::ForwardToServer();
        return;
    }
    host_client->colors = pcolor;
    host_client->edict->v.team = static_cast<float>(bottom + 1);
    Common::MSG_WriteByte(&Server::sv.reliable_datagram, svc_updatecolors);
    Common::MSG_WriteByte(&Server::sv.reliable_datagram, static_cast<int>(host_client - Server::svs.clients));
    Common::MSG_WriteByte(&Server::sv.reliable_datagram, host_client->colors);
}

void Host_Kill_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (Server::sv_player->v.health <= 0) {
        Server::SV_ClientPrintf("Can't suicide -- allready dead!\n");
        return;
    }
    VM::pr_global_struct->time = static_cast<float>(Server::sv.time);
    VM::pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(Server::sv_player));
    VM::PR_ExecuteProgram(VM::pr_global_struct->ClientKill);
}

void Host_Pause_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (!pausable.value)
        Server::SV_ClientPrintf("Pause not allowed.\n");
    else {
        Server::sv.paused ^= 1;
        Server::SV_BroadcastPrintf(
            "%s %spaused the game\n", VM::PR_GetString(Server::sv_player->v.netname), Server::sv.paused ? "" : "un");
        Common::MSG_WriteByte(&Server::sv.reliable_datagram, svc_setpause);
        Common::MSG_WriteByte(&Server::sv.reliable_datagram, Server::sv.paused);
    }
}

void Host_PreSpawn_f()
{
    if (Cmd::state.source == Cmd::Source::Command || host_client->spawned) {
        Console::Con_Printf(host_client->spawned ? "prespawn not valid -- allready spawned\n"
                                                 : "prespawn is not valid from the console\n");
        return;
    }
    Common::SZ_Write(&host_client->message, Server::sv.signon.data, Server::sv.signon.cursize);
    Common::MSG_WriteByte(&host_client->message, svc_signonnum);
    Common::MSG_WriteByte(&host_client->message, 2);
    host_client->sendsignon = true;
}

void Host_Spawn_f()
{
    if (Cmd::state.source == Cmd::Source::Command || host_client->spawned) {
        Console::Con_Printf(
            host_client->spawned ? "Spawn not valid -- allready spawned\n" : "spawn is not valid from the console\n");
        return;
    }
    if (Server::sv.loadgame)
        Server::sv.paused = false;
    else {
        edict_t* ent = host_client->edict;
        memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(VM::progs->entityfields) * 4);
        ent->v.colormap = static_cast<float>(VM::NUM_FOR_EDICT(ent));
        ent->v.team = static_cast<float>((host_client->colors & 15) + 1);
        ent->v.netname = VM::PR_SetString(host_client->name.data());
        for (int i = 0; i < NUM_SPAWN_PARMS; i++) (&VM::pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
        VM::pr_global_struct->time = static_cast<float>(Server::sv.time);
        VM::pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(Server::sv_player));
        VM::PR_ExecuteProgram(VM::pr_global_struct->ClientConnect);
        if ((Common::Sys_FloatTime() - host_client->netconnection->connecttime) <= Server::sv.time)
            Common::Sys_Printf("%s entered the game\n", host_client->name.data());
        VM::PR_ExecuteProgram(VM::pr_global_struct->PutClientInServer);
    }
    Common::SZ_Clear(&host_client->message);
    Common::MSG_WriteByte(&host_client->message, svc_time);
    Common::MSG_WriteFloat(&host_client->message, static_cast<float>(Server::sv.time));
    for (int i = 0; i < Server::svs.maxclients; i++) {
        client_t* client = &Server::svs.clients[i];
        Common::MSG_WriteByte(&host_client->message, svc_updatename);
        Common::MSG_WriteByte(&host_client->message, i);
        Common::MSG_WriteString(&host_client->message, client->name.data());
        Common::MSG_WriteByte(&host_client->message, svc_updatefrags);
        Common::MSG_WriteByte(&host_client->message, i);
        Common::MSG_WriteShort(&host_client->message, client->old_frags);
        Common::MSG_WriteByte(&host_client->message, svc_updatecolors);
        Common::MSG_WriteByte(&host_client->message, i);
        Common::MSG_WriteByte(&host_client->message, client->colors);
    }
    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        Common::MSG_WriteByte(&host_client->message, svc_lightstyle);
        Common::MSG_WriteByte(&host_client->message, static_cast<char>(i));
        Common::MSG_WriteString(&host_client->message, Server::sv.lightstyles[i]);
    }
    auto WriteStat = [](int idx, int val) {
        Common::MSG_WriteByte(&host_client->message, svc_updatestat);
        Common::MSG_WriteByte(&host_client->message, idx);
        Common::MSG_WriteLong(&host_client->message, val);
    };
    WriteStat(STAT_TOTALSECRETS, static_cast<int>(VM::pr_global_struct->total_secrets));
    WriteStat(STAT_TOTALMONSTERS, static_cast<int>(VM::pr_global_struct->total_monsters));
    WriteStat(STAT_SECRETS, static_cast<int>(VM::pr_global_struct->found_secrets));
    WriteStat(STAT_MONSTERS, static_cast<int>(VM::pr_global_struct->killed_monsters));
    edict_t* ent = VM::EDICT_NUM(1 + static_cast<int>(host_client - Server::svs.clients));
    Common::MSG_WriteByte(&host_client->message, svc_setangle);
    for (int i = 0; i < 2; i++) Common::MSG_WriteAngle(&host_client->message, ent->v.angles[i]);
    Common::MSG_WriteAngle(&host_client->message, 0);
    Server::SV_WriteClientdataToMessage(Server::sv_player, &host_client->message);
    Common::MSG_WriteByte(&host_client->message, svc_signonnum);
    Common::MSG_WriteByte(&host_client->message, 3);
    host_client->sendsignon = true;
}

void Host_Begin_f()
{
    if (Cmd::state.source != Cmd::Source::Command)
        host_client->spawned = true;
    else
        Console::Con_Printf("begin is not valid from the console\n");
}

void Host_Kick_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        if (!Server::sv.active) {
            Cmd::ForwardToServer();
            return;
        }
    } else if (VM::pr_global_struct->deathmatch && !host_client->privileged)
        return;
    client_t* save = host_client;
    bool byNumber = false;
    int i = 0;
    if (Cmd::Argc() > 2 && Common::Q_strcmp(Cmd::Argv(1), "#") == 0) {
        i = static_cast<int>(Common::Q_atof(Cmd::Argv(2)) - 1);
        if (i < 0 || i >= Server::svs.maxclients || !Server::svs.clients[i].active) return;
        host_client = &Server::svs.clients[i];
        byNumber = true;
    } else {
        for (i = 0; i < Server::svs.maxclients; i++) {
            host_client = &Server::svs.clients[i];
            if (!host_client->active) continue;
            if (Common::Q_strcasecmp(host_client->name.data(), Cmd::Argv(1)) == 0) break;
        }
    }
    if (i < Server::svs.maxclients) {
        const char* who = (Cmd::state.source == Cmd::Source::Command)
            ? (Client::cls.state == ca_dedicated ? "Console" : Client::cl_name.string.c_str())
            : save->name.data();
        if (host_client == save) return;
        const char* message = nullptr;
        std::string args_holder;
        if (Cmd::Argc() > 2) {
            args_holder = std::string(Cmd::Args().data(), Cmd::Args().length());
            const char* ptr = Common::COM_Parse(args_holder.c_str());
            if (byNumber) ptr = Common::COM_Parse(ptr);
            while (*ptr == ' ') ptr++;
            if (*ptr != '\0') message = ptr;
        }
        Server::SV_ClientPrintf(message ? "Kicked by %s: %s\n" : "Kicked by %s\n", who, message);
        Server::SV_DropClient(false);
    }
    host_client = save;
}

void Host_Give_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }
    if (VM::pr_global_struct->deathmatch && !host_client->privileged) return;
    std::string_view t = Cmd::Argv(1);
    int v = Common::Q_atoi(Cmd::Argv(2));
    if (t.empty()) return;
    auto GiveAmmo = [](const char* name, float v_val, float& std_val) {
        if (Common::rogue) {
            if (eval_t* val = VM::GetEdictFieldValue(Server::sv_player, name)) val->_float = v_val;
        }
        std_val = v_val;
    };
    switch (t[0]) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        if (Common::hipnotic) {
            if (t[0] == '6')
                Server::sv_player->v.items = static_cast<float>(static_cast<int>(Server::sv_player->v.items)
                    | ((t.size() > 1 && t[1] == 'a') ? HIT_PROXIMITY_GUN : IT_GRENADE_LAUNCHER));
            else if (t[0] == '9')
                Server::sv_player->v.items
                    = static_cast<float>(static_cast<int>(Server::sv_player->v.items) | HIT_LASER_CANNON);
            else if (t[0] == '0')
                Server::sv_player->v.items
                    = static_cast<float>(static_cast<int>(Server::sv_player->v.items) | HIT_MJOLNIR);
            else if (t[0] >= '2')
                Server::sv_player->v.items
                    = static_cast<float>(static_cast<int>(Server::sv_player->v.items) | (IT_SHOTGUN << (t[0] - '2')));
        } else if (t[0] >= '2')
            Server::sv_player->v.items
                = static_cast<float>(static_cast<int>(Server::sv_player->v.items) | (IT_SHOTGUN << (t[0] - '2')));
        break;
    case 's':
        GiveAmmo("ammo_shells1", static_cast<float>(v), Server::sv_player->v.ammo_shells);
        break;
    case 'n':
        GiveAmmo("ammo_nails1", static_cast<float>(v), Server::sv_player->v.ammo_nails);
        break;
    case 'l':
        if (Common::rogue) {
            if (eval_t* val = VM::GetEdictFieldValue(Server::sv_player, "ammo_lava_nails")) {
                val->_float = static_cast<float>(v);
                if (Server::sv_player->v.weapon > IT_LIGHTNING) Server::sv_player->v.ammo_nails = static_cast<float>(v);
            }
        }
        break;
    case 'r':
        GiveAmmo("ammo_rockets1", static_cast<float>(v), Server::sv_player->v.ammo_rockets);
        break;
    case 'm':
        if (Common::rogue) {
            if (eval_t* val = VM::GetEdictFieldValue(Server::sv_player, "ammo_multi_rockets")) {
                val->_float = static_cast<float>(v);
                if (Server::sv_player->v.weapon > IT_LIGHTNING)
                    Server::sv_player->v.ammo_rockets = static_cast<float>(v);
            }
        }
        break;
    case 'h':
        Server::sv_player->v.health = static_cast<float>(v);
        break;
    case 'c':
        GiveAmmo("ammo_cells1", static_cast<float>(v), Server::sv_player->v.ammo_cells);
        break;
    case 'p':
        if (Common::rogue) {
            if (eval_t* val = VM::GetEdictFieldValue(Server::sv_player, "ammo_plasma")) {
                val->_float = static_cast<float>(v);
                if (Server::sv_player->v.weapon > IT_LIGHTNING) Server::sv_player->v.ammo_cells = static_cast<float>(v);
            }
        }
        break;
    }
}

edict_t* FindViewthing()
{
    for (int i = 0; i < Server::sv.num_edicts; i++) {
        edict_t* e = VM::EDICT_NUM(i);
        if (strcmp(VM::PR_GetString(e->v.classname), "viewthing") == 0) return e;
    }
    Console::Con_Printf("No viewthing on map\n");
    return nullptr;
}

void Host_Viewmodel_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;
    std::string arg1(Cmd::Argv(1).data(), Cmd::Argv(1).length());
    model_t* m = Model::Mod_ForName(arg1.c_str(), false);
    if (!m) {
        Console::Con_Printf("Can't load %s\n", arg1.c_str());
        return;
    }
    e->v.frame = 0;
    Client::cl.model_precache[static_cast<int>(e->v.modelindex)] = m;
}

void Host_Viewframe_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;
    model_t* m = Client::cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = static_cast<float>(std::min(Common::Q_atoi(Cmd::Argv(1)), m->numframes - 1));
}

void PrintFrameName(model_t* m, int frame)
{
    if (aliashdr_t* hdr = static_cast<aliashdr_t*>(Model::Mod_Extradata(m)))
        Console::Con_Printf("frame %i: %s\n", frame, hdr->frames[frame].name);
}

void Host_Viewnext_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;
    model_t* m = Client::cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = std::min<float>(e->v.frame + 1.0f, static_cast<float>(m->numframes - 1));
    PrintFrameName(m, static_cast<int>(e->v.frame));
}

void Host_Viewprev_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;
    model_t* m = Client::cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = std::max<float>(e->v.frame - 1.0f, 0.0f);
    PrintFrameName(m, static_cast<int>(e->v.frame));
}

void Host_Startdemos_f()
{
    if (Client::cls.state == ca_dedicated) {
        if (!Server::sv.active) Cmd::BufferAddText("map start\n");
        return;
    }
    int c = std::min<int>(Cmd::Argc() - 1, MAX_DEMOS);
    Console::Con_Printf("%i demo(s) in loop\n", c);
    for (int i = 0; i < MAX_DEMOS; i++) Client::cls.demos[i][0] = 0;
    for (int i = 1; i <= c; i++) {
        std::string_view arg = Cmd::Argv(i);
        sprintf_s(Client::cls.demos[i - 1].data(), Client::cls.demos[i - 1].size(), "%.*s",
            static_cast<int>(arg.length()), arg.data());
    }
    if (!Server::sv.active && Client::cls.state != ca_connected && !Client::cls.demoplayback) {
        Client::cls.demonum = 0;
        Client::CL_NextDemo();
    } else
        Client::cls.demonum = -1;
}

void Host_Demos_f()
{
    if (Client::cls.state != ca_dedicated) {
        if (Client::cls.demonum == -1) Client::cls.demonum = 1;
        Client::CL_Disconnect_f();
        Client::CL_NextDemo();
    }
}
void Host_Stopdemo_f()
{
    if (Client::cls.state != ca_dedicated && Client::cls.demoplayback) {
        Client::CL_StopPlayback();
        Client::CL_Disconnect();
    }
}

struct CmdPair {
    const char* name;
    void (*fn)();
};

void Host_InitCommands()
{
    constexpr CmdPair cmds[] = { { "status", Host_Status_f }, { "quit", Host_Quit_f }, { "god", Host_God_f },
        { "notarget", Host_Notarget_f }, { "fly", Host_Fly_f }, { "map", Host_Map_f }, { "restart", Host_Restart_f },
        { "changelevel", Host_Changelevel_f }, { "connect", Host_Connect_f }, { "reconnect", Host_Reconnect_f },
        { "name", Host_Name_f }, { "noclip", Host_Noclip_f }, { "version", Host_Version_f },
        { "say", []() { Host_Say(false); } }, { "say_team", []() { Host_Say(true); } }, { "tell", Host_Tell_f },
        { "color", Host_Color_f }, { "kill", Host_Kill_f }, { "pause", Host_Pause_f }, { "spawn", Host_Spawn_f },
        { "begin", Host_Begin_f }, { "prespawn", Host_PreSpawn_f }, { "kick", Host_Kick_f }, { "ping", Host_Ping_f },
        { "load", Host_Loadgame_f }, { "save", Host_Savegame_f }, { "give", Host_Give_f },
        { "startdemos", Host_Startdemos_f }, { "demos", Host_Demos_f }, { "stopdemo", Host_Stopdemo_f },
        { "viewmodel", Host_Viewmodel_f }, { "viewframe", Host_Viewframe_f }, { "viewnext", Host_Viewnext_f },
        { "viewprev", Host_Viewprev_f }, { "mcache", Model::Mod_Print } };
    for (auto [name, fn] : cmds) Cmd::AddCommand(name, fn);
}

} // namespace Host
