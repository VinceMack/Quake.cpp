// host_cmd.cpp -- console command implementations for host management

#include <cstring>
#include "quakedef.hpp"

using namespace CDAudio;
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

extern cvar_t pausable;

int current_skill;

/*
==================
Host_Quit_f
==================
*/

void Host_Quit_f(void)
{
    if (key_dest != key_console && cls.state != ca_dedicated) {
        M_Menu_Quit_f();

        return;
    }

    CL_Disconnect();
    Host_ShutdownServer(false);

    Sys_Quit();
}

/*
==================
Host_Status_f
==================
*/
void Host_Status_f(void)
{
    client_t* client;
    int seconds;
    int minutes;
    int hours = 0;
    int j;
    void (*print)(const char* fmt, ...);

    if (Cmd::state.source == Cmd::Source::Command) {
        if (!sv.active) {
            Cmd::ForwardToServer();

            return;
        }

        print = Con_Printf;
    } else {
        print = SV_ClientPrintf;
    }

    print("host:    %s\n", Cvar::VariableString("hostname"));
    print("version: %4.2f\n", VERSION);
    if (tcpipAvailable) {
        print("tcp/ip:  %s\n", my_tcpip_address);
    }

    if (ipxAvailable) {
        print("ipx:     %s\n", my_ipx_address);
    }

    print("map:     %s\n", sv.name);
    print("players: %i active (%i max)\n\n", net_activeconnections,
        svs.maxclients);
    for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
        if (!client->active) {
            continue;
        }

        seconds = (int)(net_time - client->netconnection->connecttime);
        minutes = seconds / 60;
        if (minutes) {
            seconds -= (minutes * 60);
            hours = minutes / 60;
            if (hours) {
                minutes -= (hours * 60);
            }
        } else {
            hours = 0;
        }

        print("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j + 1, client->name,
            (int)client->edict->v.frags, hours, minutes, seconds);
        print("   %s\n", client->netconnection->address);
    }
}

/*
==================
Host_God_f

Sets client to godmode
==================
*/
void Host_God_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) {
        return;
    }

    sv_player->v.flags = static_cast<float>((int)sv_player->v.flags ^ FL_GODMODE);
    if (!((int)sv_player->v.flags & FL_GODMODE)) {
        SV_ClientPrintf("godmode OFF\n");
    } else {
        SV_ClientPrintf("godmode ON\n");
    }
}

void Host_Notarget_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) {
        return;
    }

    sv_player->v.flags = static_cast<float>((int)sv_player->v.flags ^ FL_NOTARGET);
    if (!((int)sv_player->v.flags & FL_NOTARGET)) {
        SV_ClientPrintf("notarget OFF\n");
    } else {
        SV_ClientPrintf("notarget ON\n");
    }
}

qboolean noclip_anglehack;

void Host_Noclip_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) {
        return;
    }

    if (sv_player->v.movetype != MOVETYPE_NOCLIP) {
        noclip_anglehack = true;
        sv_player->v.movetype = MOVETYPE_NOCLIP;
        SV_ClientPrintf("noclip ON\n");
    } else {
        noclip_anglehack = false;
        sv_player->v.movetype = MOVETYPE_WALK;
        SV_ClientPrintf("noclip OFF\n");
    }
}

/*
==================
Host_Fly_f

Sets client to flymode
==================
*/
void Host_Fly_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) {
        return;
    }

    if (sv_player->v.movetype != MOVETYPE_FLY) {
        sv_player->v.movetype = MOVETYPE_FLY;
        SV_ClientPrintf("flymode ON\n");
    } else {
        sv_player->v.movetype = MOVETYPE_WALK;
        SV_ClientPrintf("flymode OFF\n");
    }
}

/*
==================
Host_Ping_f

==================
*/
void Host_Ping_f(void)
{
    int i, j;
    float total;
    client_t* client;

    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    SV_ClientPrintf("Client ping times:\n");
    for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {
        if (!client->active) {
            continue;
        }

        total = 0;
        for (j = 0; j < NUM_PING_TIMES; j++) {
            total += client->ping_times[j];
        }
        total /= NUM_PING_TIMES;
        SV_ClientPrintf("%4i %s\n", (int)(total * 1000), client->name);
    }
}

/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/

/*
======================
Host_Map_f

handle a
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
void Host_Map_f(void)
{
    int i;
    char name[MAX_QPATH];

    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    cls.demonum = -1; // stop demo loop in case this fails

    CL_Disconnect();
    Host_ShutdownServer(false);

    key_dest = key_game; // remove console or menu
    SCR_BeginLoadingPlaque();

    cls.mapstring[0] = 0;
    for (i = 0; i < Cmd::Argc(); i++) {
        Q_strcat(cls.mapstring, Cmd::Argv(i));
        Q_strcat(cls.mapstring, " ");
    }
    strcat_s(cls.mapstring, sizeof(cls.mapstring), "\n");

    svs.serverflags = 0; // haven't completed an episode yet
    Q_strcpy(name, Cmd::Argv(1));
    SV_SpawnServer(name);
    if (!sv.active) {
        return;
    }

    if (cls.state != ca_dedicated) {
        strcpy_s(cls.spawnparms, sizeof(cls.spawnparms), "");

        for (i = 2; i < Cmd::Argc(); i++) {
            Q_strcat(cls.spawnparms, Cmd::Argv(i));
            Q_strcat(cls.spawnparms, " ");
        }

        Cmd::ExecuteString("connect local", Cmd::Source::Command);
    }
}

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
void Host_Changelevel_f(void)
{
    char level[MAX_QPATH];

    if (Cmd::Argc() != 2) {
        Con_Printf("changelevel <levelname> : continue game on a new level\n");

        return;
    }

    if (!sv.active || cls.demoplayback) {
        Con_Printf("Only the server may changelevel\n");

        return;
    }

    SV_SaveSpawnparms();
    Q_strcpy(level, Cmd::Argv(1));
    SV_SpawnServer(level);
}

/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
void Host_Restart_f(void)
{
    char mapname[MAX_QPATH];

    if (cls.demoplayback || !sv.active) {
        return;
    }

    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    strcpy_s(mapname, sizeof(mapname), sv.name); // must copy out, because it gets cleared
                                                  // in sv_spawnserver
    SV_SpawnServer(mapname);
}

/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels
==================
*/
void Host_Reconnect_f(void)
{
    SCR_BeginLoadingPlaque();
    cls.signon = 0; // need new connection messages
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
void Host_Connect_f(void)
{
    char name[MAX_QPATH];

    cls.demonum = -1; // stop demo loop in case this fails
    if (cls.demoplayback) {
        CL_StopPlayback();
        CL_Disconnect();
    }

    Q_strcpy(name, Cmd::Argv(1));
    CL_EstablishConnection(name);
    Host_Reconnect_f();
}

/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define SAVEGAME_VERSION 5

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
void Host_SavegameComment(char* text)
{
    int i;
    char kills[20];

    for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++) {
        text[i] = ' ';
    }
    memcpy(text, cl.levelname, strlen(cl.levelname));
    sprintf_s(kills, sizeof(kills), "kills:%3i/%3i", cl.stats[STAT_MONSTERS],
        cl.stats[STAT_TOTALMONSTERS]);
    memcpy(text + 22, kills, strlen(kills));
    // convert space to _ to make stdio happy
    for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++) {
        if (text[i] == ' ') {
            text[i] = '_';
        }
    }
    text[SAVEGAME_COMMENT_LENGTH] = '\0';
}

/*
===============
Host_Savegame_f
===============
*/
void Host_Savegame_f(void)
{
    char name[256];
    FILE* f;
    int i;
    char comment[SAVEGAME_COMMENT_LENGTH + 1];

    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    if (!sv.active) {
        Con_Printf("Not playing a local game.\n");

        return;
    }

    if (cl.intermission) {
        Con_Printf("Can't save in intermission.\n");

        return;
    }

    if (svs.maxclients != 1) {
        Con_Printf("Can't save multiplayer games.\n");

        return;
    }

    if (Cmd::Argc() != 2) {
        Con_Printf("save <savename> : save a game\n");

        return;
    }

    if (Cmd::Argv(1).find("..") != std::string_view::npos) {
        Con_Printf("Relative pathnames are not allowed.\n");

        return;
    }

    for (i = 0; i < svs.maxclients; i++) {
        if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0)) {
            Con_Printf("Can't savegame with a dead player\n");

            return;
        }
    }

    sprintf_s(name, sizeof(name), "%s/%s", com_gamedir, std::string(Cmd::Argv(1)).c_str());
    COM_DefaultExtension(name, ".sav");

    Con_Printf("Saving game to %s...\n", name);
    fopen_s(&f, name, "w");
    if (!f) {
        Con_Printf("ERROR: couldn't open.\n");

        return;
    }

    fprintf(f, "%i\n", SAVEGAME_VERSION);
    Host_SavegameComment(comment);
    fprintf(f, "%s\n", comment);
    for (i = 0; i < NUM_SPAWN_PARMS; i++) {
        fprintf(f, "%f\n", svs.clients->spawn_parms[i]);
    }
    fprintf(f, "%d\n", current_skill);
    fprintf(f, "%s\n", sv.name);
    fprintf(f, "%f\n", sv.time);

    // write the light styles

    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (sv.lightstyles[i]) {
            fprintf(f, "%s\n", sv.lightstyles[i]);
        } else {
            fprintf(f, "m\n");
        }
    }

    ED_WriteGlobals(f);
    for (i = 0; i < sv.num_edicts; i++) {
        ED_Write(f, EDICT_NUM(i));
        fflush(f);
    }
    fclose(f);
    Con_Printf("done.\n");
}

/*
===============
Host_Loadgame_f
===============
*/
void Host_Loadgame_f(void)
{
    char name[MAX_OSPATH];
    FILE* f;
    char mapname[MAX_QPATH];
    float time, tfloat;
    char str[32768], *start;
    int i, r;
    edict_t* ent;
    int entnum;
    int version;
    float spawn_parms[NUM_SPAWN_PARMS];

    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    if (Cmd::Argc() != 2) {
        Con_Printf("load <savename> : load a game\n");

        return;
    }

    cls.demonum = -1; // stop demo loop in case this fails

    sprintf_s(name, sizeof(name), "%s/%s", com_gamedir, std::string(Cmd::Argv(1)).c_str());
    COM_DefaultExtension(name, ".sav");

    // we can't call SCR_BeginLoadingPlaque, because too much stack space has
    // been used.  The menu calls it before stuffing loadgame command
    //	SCR_BeginLoadingPlaque ();

    Con_Printf("Loading game from %s...\n", name);
    fopen_s(&f, name, "r");
    if (!f) {
        Con_Printf("ERROR: couldn't open.\n");

        return;
    }

    fscanf_s(f, "%i\n", &version);
    if (version != SAVEGAME_VERSION) {
        fclose(f);
        Con_Printf("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);

        return;
    }

    fscanf_s(f, "%s\n", str, (unsigned)sizeof(str));
    for (i = 0; i < NUM_SPAWN_PARMS; i++) {
        fscanf_s(f, "%f\n", &spawn_parms[i]);
    }
    // this silliness is so we can load 1.06 save files, which have float skill values
    fscanf_s(f, "%f\n", &tfloat);
    current_skill = (int)(tfloat + 0.1);
    Cvar::SetValue("skill", (float)current_skill);


    fscanf_s(f, "%s\n", mapname, (unsigned)sizeof(mapname));
    fscanf_s(f, "%f\n", &time);

    CL_Disconnect_f();

    SV_SpawnServer(mapname);
    if (!sv.active) {
        Con_Printf("Couldn't load map\n");

        return;
    }

    sv.paused = true; // pause until all clients connect
    sv.loadgame = true;

    // load the light styles

    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
        fscanf_s(f, "%s\n", str, (unsigned)sizeof(str));
        sv.lightstyles[i] = (char *) Hunk_Alloc((int)strlen(str) + 1);
        strcpy_s(sv.lightstyles[i], strlen(str) + 1, str);
    }

    // load the edicts out of the savegame file
    entnum = -1; // -1 is the globals
    while (!feof(f)) {
        for (i = 0; i < static_cast<int>(sizeof(str) - 1); i++) {
            r = fgetc(f);
            if (r == EOF || !r) {
                break;
            }

            str[i] = static_cast<char>(r);
            if (r == '}') {
                i++;
                break;
            }
        }
        if (i == sizeof(str) - 1) {
            Sys_Error("Loadgame buffer overflow");
        }

        str[i] = 0;
        start = COM_Parse(str);
        if (!com_token[0]) {
            break; // end of file
        }

        if (strcmp(com_token, "{")) {
            Sys_Error("First token isn't a brace");
        }

        if (entnum == -1) { // parse the global vars
            ED_ParseGlobals(start);
        } else { // parse an edict

            ent = EDICT_NUM(entnum);
            std::memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4);
            ent->free = false;
            ED_ParseEdict(start, ent);

            // link it into the bsp tree
            if (!ent->free) {
                SV_LinkEdict(ent, false);
            }
        }

        entnum++;
    }

    sv.num_edicts = entnum;
    sv.time = time;

    fclose(f);

    for (i = 0; i < NUM_SPAWN_PARMS; i++) {
        svs.clients->spawn_parms[i] = spawn_parms[i];
    }

    if (cls.state != ca_dedicated) {
        CL_EstablishConnection("local");
        Host_Reconnect_f();
    }
}


//============================================================================

/*
======================
Host_Name_f
======================
*/
void Host_Name_f(void)
{
    char newName[64];

    if (Cmd::Argc() == 1) {
        Con_Printf("\"name\" is \"%s\"\n", cl_name.string);

        return;
    }

    if (Cmd::Argc() == 2) {
        Q_strncpy(newName, std::string(Cmd::Argv(1)).c_str(), sizeof(newName) - 1);
    } else {
        Q_strncpy(newName, std::string(Cmd::Args()).c_str(), sizeof(newName) - 1);
    }

    newName[15] = 0;

    if (Cmd::state.source == Cmd::Source::Command) {
        if (Q_strcmp(cl_name.string, newName) == 0) {
            return;
        }

        Cvar::Set("_cl_name", newName);
        if (cls.state == ca_connected) {
            Cmd::ForwardToServer();
        }

        return;
    }

    if (host_client->name[0] && strcmp(host_client->name, "unconnected")) {
        if (Q_strcmp(host_client->name, newName) != 0) {
            Con_Printf("%s renamed to %s\n", host_client->name, newName);
        }
    }

    Q_strcpy(host_client->name, newName);
    host_client->edict->v.netname = PR_SetString(host_client->name);

    // send notification to all clients

    MSG_WriteByte(&sv.reliable_datagram, svc_updatename);
    MSG_WriteByte(&sv.reliable_datagram, static_cast<int>(host_client - svs.clients));
    MSG_WriteString(&sv.reliable_datagram, host_client->name);
}

void Host_Version_f(void)
{
    Con_Printf("Version %4.2f\n", VERSION);
    Con_Printf(
        "Exe: " __TIME__
        " " __DATE__
        "\n");
}


void Host_Say(qboolean teamonly)
{
    client_t* client;
    client_t* save;
    int j;
    unsigned char text[64];
    qboolean fromServer = false;

    if (Cmd::state.source == Cmd::Source::Command) {
        if (cls.state == ca_dedicated) {
            fromServer = true;
            teamonly = false;
        } else {
            Cmd::ForwardToServer();

            return;
        }
    }

    if (Cmd::Argc() < 2) {
        return;
    }

    save = host_client;

    std::string arg_str(Cmd::Args());
    // remove quotes if present
    if (!arg_str.empty() && arg_str.front() == '"') {
        arg_str = arg_str.substr(1);
        if (!arg_str.empty() && arg_str.back() == '"') {
            arg_str.pop_back();
        }
    }

    // turn on color set 1
    if (!fromServer) {
        sprintf_s((char*)text, sizeof(text), "%c%s: ", 1, save->name);
    } else {
        sprintf_s((char*)text, sizeof(text), "%c<%s> ", 1, hostname.string);
    }

    j = sizeof(text) - 2 - Q_strlen((char*)text); // -2 for /n and null terminator
    if (arg_str.length() > (size_t)j) {
        arg_str.resize(j);
    }
    const char* p = arg_str.c_str();

    strcat_s((char*)text, sizeof(text), p);
    strcat_s((char*)text, sizeof(text), "\n");

    for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
        if (!client || !client->active || !client->spawned) {
            continue;
        }

        if (teamplay.value && teamonly && client->edict->v.team != save->edict->v.team) {
            continue;
        }

        host_client = client;
        SV_ClientPrintf("%s", text);
    }
    host_client = save;

    Sys_Printf("%s", &text[1]);
}

void Host_Tell_f(void)
{
    client_t* client;
    client_t* save;
    int j;
    char text[64];

    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (Cmd::Argc() < 3) {
        return;
    }

    Q_strcpy(text, host_client->name);
    Q_strcat(text, ": ");

    std::string arg_str(Cmd::Args());

    // remove quotes if present
    if (!arg_str.empty() && arg_str.front() == '"') {
        arg_str = arg_str.substr(1);
        if (!arg_str.empty() && arg_str.back() == '"') {
            arg_str.pop_back();
        }
    }

    // check length & truncate if necessary
    j = sizeof(text) - 2 - Q_strlen(text); // -2 for /n and null terminator
    if (arg_str.length() > (size_t)j) {
        arg_str.resize(j);
    }
    const char* p = arg_str.c_str();

    strcat_s(text, sizeof(text), p);
    strcat_s(text, sizeof(text), "\n");

    save = host_client;
    for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
        if (!client->active || !client->spawned) {
            continue;
        }

        if (Q_strcasecmp(client->name, Cmd::Argv(1))) {
            continue;
        }

        host_client = client;
        SV_ClientPrintf("%s", text);
        break;
    }
    host_client = save;
}

/*
==================
Host_Color_f
==================
*/
void Host_Color_f(void)
{
    int top, bottom;
    int pcolor;

    if (Cmd::Argc() == 1) {
        Con_Printf("\"color\" is \"%i %i\"\n", ((int)cl_color.value) >> 4,
            ((int)cl_color.value) & 0x0f);
        Con_Printf("color <0-13> [0-13]\n");

        return;
    }

    if (Cmd::Argc() == 2) {
        top = bottom = Q_atoi(Cmd::Argv(1));
    } else {
        top = Q_atoi(Cmd::Argv(1));
        bottom = Q_atoi(Cmd::Argv(2));
    }

    top &= 15;
    if (top > 13) {
        top = 13;
    }

    bottom &= 15;
    if (bottom > 13) {
        bottom = 13;
    }

    pcolor = top * 16 + bottom;

    if (Cmd::state.source == Cmd::Source::Command) {
        Cvar::SetValue("_cl_color", static_cast<float>(pcolor));
        if (cls.state == ca_connected) {
            Cmd::ForwardToServer();
        }

        return;
    }

    host_client->colors = pcolor;
    host_client->edict->v.team = static_cast<float>(bottom + 1);

    // send notification to all clients
    MSG_WriteByte(&sv.reliable_datagram, svc_updatecolors);
    MSG_WriteByte(&sv.reliable_datagram, static_cast<int>(host_client - svs.clients));
    MSG_WriteByte(&sv.reliable_datagram, host_client->colors);
}

/*
==================
Host_Kill_f
==================
*/
void Host_Kill_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (sv_player->v.health <= 0) {
        SV_ClientPrintf("Can't suicide -- allready dead!\n");

        return;
    }

    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv_player));
    PR_ExecuteProgram(pr_global_struct->ClientKill);
}

/*
==================
Host_Pause_f
==================
*/
void Host_Pause_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (!pausable.value) {
        SV_ClientPrintf("Pause not allowed.\n");
    } else {
        sv.paused ^= 1;

        if (sv.paused) {
            SV_BroadcastPrintf("%s paused the game\n",
                PR_GetString(sv_player->v.netname));
        } else {
            SV_BroadcastPrintf("%s unpaused the game\n",
                PR_GetString(sv_player->v.netname));
        }

        // send notification to all clients
        MSG_WriteByte(&sv.reliable_datagram, svc_setpause);
        MSG_WriteByte(&sv.reliable_datagram, sv.paused);
    }
}

//===========================================================================

/*
==================
Host_PreSpawn_f
==================
*/
void Host_PreSpawn_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Con_Printf("prespawn is not valid from the console\n");

        return;
    }

    if (host_client->spawned) {
        Con_Printf("prespawn not valid -- allready spawned\n");

        return;
    }

    SZ_Write(&host_client->message, sv.signon.data, sv.signon.cursize);
    MSG_WriteByte(&host_client->message, svc_signonnum);
    MSG_WriteByte(&host_client->message, 2);
    host_client->sendsignon = true;
}

/*
==================
Host_Spawn_f
==================
*/
void Host_Spawn_f(void)
{
    int i;
    client_t* client;
    edict_t* ent;

    if (Cmd::state.source == Cmd::Source::Command) {
        Con_Printf("spawn is not valid from the console\n");

        return;
    }

    if (host_client->spawned) {
        Con_Printf("Spawn not valid -- allready spawned\n");

        return;
    }

    // run the entrance script
    if (sv.loadgame) { // loaded games are fully inited allready
        // if this is the last client to be connected, unpause
        sv.paused = false;
    } else {
        // set up the edict
        ent = host_client->edict;

        std::memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4);
        ent->v.colormap = static_cast<float>(NUM_FOR_EDICT(ent));
        ent->v.team = static_cast<float>((host_client->colors & 15) + 1);
        ent->v.netname = PR_SetString(host_client->name);

        // copy spawn parms out of the client_t

        for (i = 0; i < NUM_SPAWN_PARMS; i++) {
            (&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
        }

        // call the spawn function

        pr_global_struct->time = static_cast<float>(sv.time);
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv_player));
        PR_ExecuteProgram(pr_global_struct->ClientConnect);

        if ((Sys_FloatTime() - host_client->netconnection->connecttime) <= sv.time) {
            Sys_Printf("%s entered the game\n", host_client->name);
        }

        PR_ExecuteProgram(pr_global_struct->PutClientInServer);
    }

    // send all current names, colors, and frag counts
    SZ_Clear(&host_client->message);

    // send time of update
    MSG_WriteByte(&host_client->message, svc_time);
    MSG_WriteFloat(&host_client->message, static_cast<float>(sv.time));

    for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {
        MSG_WriteByte(&host_client->message, svc_updatename);
        MSG_WriteByte(&host_client->message, i);
        MSG_WriteString(&host_client->message, client->name);
        MSG_WriteByte(&host_client->message, svc_updatefrags);
        MSG_WriteByte(&host_client->message, i);
        MSG_WriteShort(&host_client->message, client->old_frags);
        MSG_WriteByte(&host_client->message, svc_updatecolors);
        MSG_WriteByte(&host_client->message, i);
        MSG_WriteByte(&host_client->message, client->colors);
    }

    // send all current light styles
    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
        MSG_WriteByte(&host_client->message, svc_lightstyle);
        MSG_WriteByte(&host_client->message, (char)i);
        MSG_WriteString(&host_client->message, sv.lightstyles[i]);
    }

    //
    // send some stats
    //
    MSG_WriteByte(&host_client->message, svc_updatestat);
    MSG_WriteByte(&host_client->message, STAT_TOTALSECRETS);
    MSG_WriteLong(&host_client->message, static_cast<int>(pr_global_struct->total_secrets));

    MSG_WriteByte(&host_client->message, svc_updatestat);
    MSG_WriteByte(&host_client->message, STAT_TOTALMONSTERS);
    MSG_WriteLong(&host_client->message, static_cast<int>(pr_global_struct->total_monsters));

    MSG_WriteByte(&host_client->message, svc_updatestat);
    MSG_WriteByte(&host_client->message, STAT_SECRETS);
    MSG_WriteLong(&host_client->message, static_cast<int>(pr_global_struct->found_secrets));

    MSG_WriteByte(&host_client->message, svc_updatestat);
    MSG_WriteByte(&host_client->message, STAT_MONSTERS);
    MSG_WriteLong(&host_client->message, static_cast<int>(pr_global_struct->killed_monsters));

    //
    // send a fixangle
    // Never send a roll angle, because savegames can catch the server
    // in a state where it is expecting the client to correct the angle
    // and it won't happen if the game was just loaded, so you wind up
    // with a permanent head tilt
    ent = EDICT_NUM(1 + static_cast<int>(host_client - svs.clients));
    MSG_WriteByte(&host_client->message, svc_setangle);
    for (i = 0; i < 2; i++) {
        MSG_WriteAngle(&host_client->message, ent->v.angles[i]);
    }
    MSG_WriteAngle(&host_client->message, 0);

    SV_WriteClientdataToMessage(sv_player, &host_client->message);

    MSG_WriteByte(&host_client->message, svc_signonnum);
    MSG_WriteByte(&host_client->message, 3);
    host_client->sendsignon = true;
}

/*
==================
Host_Begin_f
==================
*/
void Host_Begin_f(void)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Con_Printf("begin is not valid from the console\n");

        return;
    }

    host_client->spawned = true;
}

//===========================================================================

/*
==================
Host_Kick_f

Kicks a user off of the server
==================
*/
void Host_Kick_f(void)
{
    const char* who;
    const char* message = NULL;
    client_t* save;
    int i;
    qboolean byNumber = false;
    std::string args_holder;

    if (Cmd::state.source == Cmd::Source::Command) {
        if (!sv.active) {
            Cmd::ForwardToServer();

            return;
        }
    } else if (pr_global_struct->deathmatch && !host_client->privileged) {
        return;
    }

    save = host_client;

    if (Cmd::Argc() > 2 && Q_strcmp(Cmd::Argv(1), "#") == 0) {
        i = static_cast<int>(Q_atof(Cmd::Argv(2)) - 1);
        if (i < 0 || i >= svs.maxclients) {
            return;
        }

        if (!svs.clients[i].active) {
            return;
        }

        host_client = &svs.clients[i];
        byNumber = true;
    } else {
        for (i = 0, host_client = svs.clients; i < svs.maxclients;
            i++, host_client++) {
            if (!host_client->active) {
                continue;
            }

            if (Q_strcasecmp(host_client->name, Cmd::Argv(1)) == 0) {
                break;
            }
        }
    }

    if (i < svs.maxclients) {
        if (Cmd::state.source == Cmd::Source::Command) {
            if (cls.state == ca_dedicated) {
                who = "Console";
            } else {
                who = cl_name.string;
            }
        } else {
            who = save->name;
        }

        // can't kick yourself!
        if (host_client == save) {
            return;
        }

        if (Cmd::Argc() > 2) {
            args_holder = Cmd::Args();
            message = COM_Parse(args_holder.c_str());
            if (byNumber) {
                message++;                // skip the #
                while (*message == ' ') { // skip white space
                    message++;
                }
                message += Cmd::Argv(2).length(); // skip the number
            }

            while (*message && *message == ' ') {
                message++;
            }
        }

        if (message) {
            SV_ClientPrintf("Kicked by %s: %s\n", who, message);
        } else {
            SV_ClientPrintf("Kicked by %s\n", who);
        }

        SV_DropClient(false);
    }

    host_client = save;
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

/*
==================
Host_Give_f
==================
*/
void Host_Give_f(void)
{
    std::string_view t;
    int v;
    eval_t* val;

    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();

        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) {
        return;
    }

    t = Cmd::Argv(1);
    v = Q_atoi(Cmd::Argv(2));

    if (t.empty()) {
        return;
    }

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
        // MED 01/04/97 added hipnotic give stuff
        if (hipnotic) {
            if (t[0] == '6') {
                if (t.size() > 1 && t[1] == 'a') {
                    sv_player->v.items = static_cast<float>((int)sv_player->v.items | HIT_PROXIMITY_GUN);
                } else {
                    sv_player->v.items = static_cast<float>((int)sv_player->v.items | IT_GRENADE_LAUNCHER);
                }
            } else if (t[0] == '9') {
                sv_player->v.items = static_cast<float>((int)sv_player->v.items | HIT_LASER_CANNON);
            } else if (t[0] == '0') {
                sv_player->v.items = static_cast<float>((int)sv_player->v.items | HIT_MJOLNIR);
            } else if (t[0] >= '2') {
                sv_player->v.items = static_cast<float>((int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2')));
            }
        } else {
            if (t[0] >= '2') {
                sv_player->v.items = static_cast<float>((int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2')));
            }
        }

        break;

    case 's':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_shells1");
            if (val) {
                val->_float = static_cast<float>(v);
            }
        }

        sv_player->v.ammo_shells = static_cast<float>(v);
        break;
    case 'n':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_nails1");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon <= IT_LIGHTNING) {
                    sv_player->v.ammo_nails = static_cast<float>(v);
                }
            }
        } else {
            sv_player->v.ammo_nails = static_cast<float>(v);
        }

        break;
    case 'l':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_lava_nails");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon > IT_LIGHTNING) {
                    sv_player->v.ammo_nails = static_cast<float>(v);
                }
            }
        }

        break;
    case 'r':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_rockets1");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon <= IT_LIGHTNING) {
                    sv_player->v.ammo_rockets = static_cast<float>(v);
                }
            }
        } else {
            sv_player->v.ammo_rockets = static_cast<float>(v);
        }

        break;
    case 'm':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_multi_rockets");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon > IT_LIGHTNING) {
                    sv_player->v.ammo_rockets = static_cast<float>(v);
                }
            }
        }

        break;
    case 'h':
        sv_player->v.health = static_cast<float>(v);
        break;
    case 'c':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_cells1");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon <= IT_LIGHTNING) {
                    sv_player->v.ammo_cells = static_cast<float>(v);
                }
            }
        } else {
            sv_player->v.ammo_cells = static_cast<float>(v);
        }

        break;
    case 'p':
        if (rogue) {
            val = GetEdictFieldValue(sv_player, "ammo_plasma");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon > IT_LIGHTNING) {
                    sv_player->v.ammo_cells = static_cast<float>(v);
                }
            }
        }

        break;
    }
}

edict_t* FindViewthing(void)
{
    int i;
    edict_t* e;

    for (i = 0; i < sv.num_edicts; i++) {
        e = EDICT_NUM(i);
        if (!strcmp(PR_GetString(e->v.classname), "viewthing")) {
            return e;
        }
    }
    Con_Printf("No viewthing on map\n");

    return NULL;
}

/*
==================
Host_Viewmodel_f
==================
*/
void Host_Viewmodel_f(void)
{
    edict_t* e;
    model_t* m;

    e = FindViewthing();
    if (!e) {
        return;
    }

    m = Mod_ForName(std::string(Cmd::Argv(1)).c_str(), false);
    if (!m) {
        Con_Printf("Can't load %s\n", Cmd::Argv(1));

        return;
    }

    e->v.frame = 0;
    cl.model_precache[(int)e->v.modelindex] = m;
}

/*
==================
Host_Viewframe_f
==================
*/
void Host_Viewframe_f(void)
{
    edict_t* e;
    int f;
    model_t* m;

    e = FindViewthing();
    if (!e) {
        return;
    }

    m = cl.model_precache[(int)e->v.modelindex];

    f = Q_atoi(Cmd::Argv(1));
    if (f >= m->numframes) {
        f = m->numframes - 1;
    }

    e->v.frame = static_cast<float>(f);
}

void PrintFrameName(model_t* m, int frame)
{
    aliashdr_t* hdr;
    maliasframedesc_t* pframedesc;

    hdr = (aliashdr_t*)Mod_Extradata(m);
    if (!hdr) {
        return;
    }

    pframedesc = &hdr->frames[frame];

    Con_Printf("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
void Host_Viewnext_f(void)
{
    edict_t* e;
    model_t* m;

    e = FindViewthing();
    if (!e) {
        return;
    }
    m = cl.model_precache[(int)e->v.modelindex];

    e->v.frame = e->v.frame + 1;
    if (e->v.frame >= m->numframes) {
        e->v.frame = static_cast<float>(m->numframes - 1);
    }

    PrintFrameName(m, static_cast<int>(e->v.frame));
}

/*

==================
Host_Viewprev_f
==================
*/
void Host_Viewprev_f(void)
{
    edict_t* e;
    model_t* m;

    e = FindViewthing();
    if (!e) {
        return;
    }

    m = cl.model_precache[(int)e->v.modelindex];

    e->v.frame = e->v.frame - 1;
    if (e->v.frame < 0) {
        e->v.frame = 0;
    }

    PrintFrameName(m, static_cast<int>(e->v.frame));
}

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/

/*
==================
Host_Startdemos_f
==================
*/
void Host_Startdemos_f(void)
{
    int i, c;

    if (cls.state == ca_dedicated) {
        if (!sv.active) {
            Cmd::BufferAddText("map start\n");
        }

        return;
    }

    c = Cmd::Argc() - 1;
    if (c > MAX_DEMOS) {
        Con_Printf("Max %i demos in demoloop\n", MAX_DEMOS);
        c = MAX_DEMOS;
    }

    Con_Printf("%i demo(s) in loop\n", c);

    for (i = 1; i < c + 1; i++) {
        Q_strncpy(cls.demos[i - 1], std::string(Cmd::Argv(i)).c_str(), sizeof(cls.demos[0]) - 1);
    }

    if (!sv.active && cls.demonum != -1 && !cls.demoplayback) {
        cls.demonum = 0;
        CL_NextDemo();
    } else {
        cls.demonum = -1;
    }
}

/*
==================
Host_Demos_f

Return to looping demos
==================
*/
void Host_Demos_f(void)
{
    if (cls.state == ca_dedicated) {
        return;
    }

    if (cls.demonum == -1) {
        cls.demonum = 1;
    }

    CL_Disconnect_f();
    CL_NextDemo();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
void Host_Stopdemo_f(void)
{
    if (cls.state == ca_dedicated) {
        return;
    }

    if (!cls.demoplayback) {
        return;
    }

    CL_StopPlayback();
    CL_Disconnect();
}

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void Host_InitCommands(void)
{
    Cmd::AddCommand("status", Host_Status_f);
    Cmd::AddCommand("quit", Host_Quit_f);
    Cmd::AddCommand("god", Host_God_f);
    Cmd::AddCommand("notarget", Host_Notarget_f);
    Cmd::AddCommand("fly", Host_Fly_f);
    Cmd::AddCommand("map", Host_Map_f);
    Cmd::AddCommand("restart", Host_Restart_f);
    Cmd::AddCommand("changelevel", Host_Changelevel_f);
    Cmd::AddCommand("connect", Host_Connect_f);
    Cmd::AddCommand("reconnect", Host_Reconnect_f);
    Cmd::AddCommand("name", Host_Name_f);
    Cmd::AddCommand("noclip", Host_Noclip_f);
    Cmd::AddCommand("version", Host_Version_f);
    Cmd::AddCommand("say", []() { Host_Say(false); });
    Cmd::AddCommand("say_team", []() { Host_Say(true); });
    Cmd::AddCommand("tell", Host_Tell_f);
    Cmd::AddCommand("color", Host_Color_f);
    Cmd::AddCommand("kill", Host_Kill_f);
    Cmd::AddCommand("pause", Host_Pause_f);
    Cmd::AddCommand("spawn", Host_Spawn_f);
    Cmd::AddCommand("begin", Host_Begin_f);
    Cmd::AddCommand("prespawn", Host_PreSpawn_f);
    Cmd::AddCommand("kick", Host_Kick_f);
    Cmd::AddCommand("ping", Host_Ping_f);
    Cmd::AddCommand("load", Host_Loadgame_f);
    Cmd::AddCommand("save", Host_Savegame_f);
    Cmd::AddCommand("give", Host_Give_f);

    Cmd::AddCommand("startdemos", Host_Startdemos_f);
    Cmd::AddCommand("demos", Host_Demos_f);
    Cmd::AddCommand("stopdemo", Host_Stopdemo_f);

    Cmd::AddCommand("viewmodel", Host_Viewmodel_f);
    Cmd::AddCommand("viewframe", Host_Viewframe_f);
    Cmd::AddCommand("viewnext", Host_Viewnext_f);
    Cmd::AddCommand("viewprev", Host_Viewprev_f);

    Cmd::AddCommand("mcache", Mod_Print);
}

} // namespace Host
