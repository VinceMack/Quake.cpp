// client.cpp -- client subsystem (merged from cl_main.cpp, cl_input.cpp, cl_demo.cpp, cl_parse.cpp, cl_tent.cpp)

#include "quakedef.hpp"
#include <array>
#include <string>
#include <string_view>
#include <algorithm>
#include <span>
#include <cmath>

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

namespace Client {

//============================================================================
// Global variable definitions
//============================================================================

// from cl_main.cpp
cvar_t cl_name = { "_cl_name", "player", true, {}, {}, {} };
cvar_t cl_color = { "_cl_color", "0", true, {}, {}, {} };
cvar_t cl_shownet = { "cl_shownet", "0", {}, {}, {}, {} };
cvar_t cl_nolerp = { "cl_nolerp", "0", {}, {}, {}, {} };
cvar_t lookspring = { "lookspring", "0", true, {}, {}, {} };
cvar_t lookstrafe = { "lookstrafe", "0", true, {}, {}, {} };
cvar_t sensitivity = { "sensitivity", "3", true, {}, {}, {} };
cvar_t m_pitch = { "m_pitch", "0.022", true, {}, {}, {} };
cvar_t m_yaw = { "m_yaw", "0.022", true, {}, {}, {} };
cvar_t m_forward = { "m_forward", "1", true, {}, {}, {} };
cvar_t m_side = { "m_side", "0.8", true, {}, {}, {} };

ClientSubsystem& GetClientSubsystem()
{
    static ClientSubsystem subsystem;
    return subsystem;
}

int cl_numvisedicts;
entity_t* cl_visedicts[MAX_VISEDICTS];

// from cl_input.cpp
kbutton_t in_mlook, in_klook;
kbutton_t in_left, in_right, in_forward, in_back;
kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t in_strafe, in_speed, in_use, in_jump, in_attack;
kbutton_t in_up, in_down;
int in_impulse;

cvar_t cl_upspeed = { "cl_upspeed", "200", {}, {}, {}, {} };
cvar_t cl_forwardspeed = { "cl_forwardspeed", "200", true, {}, {}, {} };
cvar_t cl_backspeed = { "cl_backspeed", "200", true, {}, {}, {} };
cvar_t cl_sidespeed = { "cl_sidespeed", "350", {}, {}, {}, {} };
cvar_t cl_movespeedkey = { "cl_movespeedkey", "2.0", {}, {}, {}, {} };
cvar_t cl_yawspeed = { "cl_yawspeed", "140", {}, {}, {}, {} };
cvar_t cl_pitchspeed = { "cl_pitchspeed", "150", {}, {}, {}, {} };
cvar_t cl_anglespeedkey = { "cl_anglespeedkey", "1.5", {}, {}, {}, {} };

// from cl_parse.cpp
constexpr auto svc_strings = std::array{
    "svc_bad", "svc_nop", "svc_disconnect", "svc_updatestat",
    "svc_version",   // [long] server version
    "svc_setview",   // [short] entity number
    "svc_sound",     // <see code>
    "svc_time",      // [float] server time
    "svc_print",     // [string] null terminated string
    "svc_stufftext", // [string] stuffed into client's console buffer
    "svc_setangle",  // [vec3] set the view angle to this absolute value
    "svc_serverinfo", // [long] version
    "svc_lightstyle",   // [byte] [string]
    "svc_updatename",   // [byte] [string]
    "svc_updatefrags",  // [byte] [short]
    "svc_clientdata",   // <shortbits + data>
    "svc_stopsound",    // <see code>
    "svc_updatecolors", // [byte] [byte]
    "svc_particle",     // [vec3] <variable>
    "svc_damage",       // [byte] impact [byte] blood [vec3] from
    "svc_spawnstatic", "OBSOLETE svc_spawnbinary", "svc_spawnbaseline",
    "svc_temp_entity", // <variable>
    "svc_setpause", "svc_signonnum", "svc_centerprint", "svc_killedmonster",
    "svc_foundsecret", "svc_spawnstaticsound", "svc_intermission",
    "svc_finale",  // [string] music [string] text
    "svc_cdtrack", // [byte] track [byte] looptrack
    "svc_sellscreen", "svc_cutscene"
};
std::array<int, 16> bitcounts{};

// from cl_tent.cpp
int num_temp_entities;
sfx_t* cl_sfx_wizhit;
sfx_t* cl_sfx_knighthit;
sfx_t* cl_sfx_tink1;
sfx_t* cl_sfx_ric1;
sfx_t* cl_sfx_ric2;
sfx_t* cl_sfx_ric3;
sfx_t* cl_sfx_r_exp3;

//============================================================================
// cl_main.cpp contents
//============================================================================

/*
=====================
CL_ClearState
=====================
*/
void CL_ClearState()
{
    if (!sv.active) {
        Host_ClearMemory();
    }

    // wipe the entire cl structure
    cl = {};

    SZ_Clear(&cls.message);

    // clear other arrays using modern C++
    cl_efrags.fill({});
    cl_entities.fill({});
    cl_static_entities.fill({});
    cl_lightstyle.fill({});
    cl_temp_entities.fill({});
    cl_beams.fill({});
    cl_dlights.fill({});

    // allocate the efrags and chain together into a free list
    cl.free_efrags = cl_efrags.data();
    size_t i;
    for (i = 0; i < MAX_EFRAGS - 1; ++i) {
        cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];
    }
    cl.free_efrags[i].entnext = nullptr;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect()
{
    // stop sounds (especially looping!)
    S_StopAllSounds(true);

    // if running a local server, shut it down
    if (cls.demoplayback) {
        CL_StopPlayback();
    } else if (cls.state == ca_connected) {
        if (cls.demorecording) {
            CL_Stop_f();
        }

        Con_DPrintf("Sending clc_disconnect\n");
        SZ_Clear(&cls.message);
        MSG_WriteByte(&cls.message, clc_disconnect);
        NET_SendUnreliableMessage(cls.netcon, &cls.message);
        SZ_Clear(&cls.message);
        NET_Close(cls.netcon);

        cls.state = ca_disconnected;
        if (sv.active) {
            Host_ShutdownServer(false);
        }
    }

    cls.demoplayback = cls.timedemo = false;
    cls.signon = 0;
}

void CL_Disconnect_f()
{
    CL_Disconnect();
    if (sv.active) {
        Host_ShutdownServer(false);
    }
}

/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection(const char* host)
{
    if (cls.state == ca_dedicated) {
        return;
    }

    if (cls.demoplayback) {
        return;
    }

    CL_Disconnect();

    cls.netcon = NET_Connect(host);
    if (!cls.netcon) {
        Host_Error("CL_Connect: connect failed\n");
    }

    Con_DPrintf("CL_EstablishConnection: connected to %s\n", host);

    cls.demonum = -1; // not in the demo loop now
    cls.state = ca_connected;
    cls.signon = 0; // need all the signon messages before playing
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply()
{
    Con_DPrintf("CL_SignonReply: %i\n", cls.signon);

    switch (cls.signon) {
    case 1:
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, "prespawn");
        break;

    case 2: {
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, va("name \"%s\"\n", cl_name.string.c_str()));

        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message,
            va("color %i %i\n", static_cast<int>(cl_color.value) >> 4,
                static_cast<int>(cl_color.value) & 15));

        MSG_WriteByte(&cls.message, clc_stringcmd);
        std::string spawnCmd = "spawn " + std::string(cls.spawnparms);
        MSG_WriteString(&cls.message, spawnCmd.c_str());
        break;
    }

    case 3:
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, "begin");
        Cache_Report(); // print remaining memory
        break;

    case 4:
        Screen::GetScreenSystem().EndLoadingPlaque(); // allow normal screen updates
        break;
    }
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo()
{
    if (cls.demonum == -1) {
        return; // don't play demos
    }

    Screen::GetScreenSystem().BeginLoadingPlaque();

    if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS) {
        cls.demonum = 0;
        if (!cls.demos[cls.demonum][0]) {
            Con_Printf("No demos listed with startdemos\n");
            cls.demonum = -1;

            return;
        }
    }

    std::string demoCmd = "playdemo " + std::string(cls.demos[cls.demonum]) + "\n";
    Cmd::BufferInsertText(demoCmd);
    cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f()
{
    int i = 0;
    for (const auto& ent : std::span(cl_entities.data(), cl.num_entities)) {
        Con_Printf("%3i:", i++);
        if (!ent.model) {
            Con_Printf("EMPTY\n");
            continue;
        }

        Con_Printf("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n",
            ent.model->name, ent.frame, ent.origin[0], ent.origin[1],
            ent.origin[2], ent.angles[0], ent.angles[1], ent.angles[2]);
    }
}

/*
===============
CL_AllocDlight
===============
*/
dlight_t* CL_AllocDlight(int key)
{
    // first look for an exact key match
    if (key) {
        for (auto& dl : cl_dlights) {
            if (dl.key == key) {
                dl = {};
                dl.key = key;
                return &dl;
            }
        }
    }

    // then look for anything else
    for (auto& dl : cl_dlights) {
        if (dl.die < cl.time) {
            dl = {};
            dl.key = key;
            return &dl;
        }
    }

    auto& dl = cl_dlights[0];
    dl = {};
    dl.key = key;
    return &dl;
}

/*
===============
CL_DecayLights
===============
*/
void CL_DecayLights()
{
    const float time = static_cast<float>(cl.time - cl.oldtime);

    for (auto& dl : cl_dlights) {
        if (dl.die < cl.time || !dl.radius) {
            continue;
        }

        dl.radius -= time * dl.decay;
        if (dl.radius < 0.0f) {
            dl.radius = 0.0f;
        }
    }
}

/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float CL_LerpPoint()
{
    float f = static_cast<float>(cl.mtime[0] - cl.mtime[1]);

    if (!f || cl_nolerp.value || cls.timedemo || sv.active) {
        cl.time = cl.mtime[0];
        return 1.0f;
    }

    if (f > 0.1f) { // dropped packet, or start of demo
        cl.mtime[1] = cl.mtime[0] - 0.1;
        f = 0.1f;
    }

    float frac = static_cast<float>((cl.time - cl.mtime[1]) / f);
    if (frac < 0.0f) {
        if (frac < -0.01f) {
            cl.time = cl.mtime[1];
        }
        frac = 0.0f;
    } else if (frac > 1.0f) {
        if (frac > 1.01f) {
            cl.time = cl.mtime[0];
        }
        frac = 1.0f;
    }

    return frac;
}

/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities()
{
    // determine partial update time
    const float frac = CL_LerpPoint();

    cl_numvisedicts = 0;

    // interpolate player info
    cl.velocity = cl.mvelocity[1] + (cl.mvelocity[0] - cl.mvelocity[1]) * frac;

    if (cls.demoplayback) {
        // interpolate the angles
        for (int j = 0; j < 3; ++j) {
            float d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
            if (d > 180.0f) {
                d -= 360.0f;
            } else if (d < -180.0f) {
                d += 360.0f;
            }

            cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
        }
    }

    const float bobjrotate = anglemod(static_cast<float>(100.0 * cl.time));

    // start on the entity after the world
    if (cl.num_entities > 1) {
        int i = 1;
        for (auto& ent : std::span(cl_entities.data() + 1, cl.num_entities - 1)) {
            if (!ent.model) { // empty slot
                if (ent.forcelink) {
                    R_RemoveEfrags(&ent); // just became empty
                }
                ++i;
                continue;
            }

            // if the object wasn't included in the last packet, remove it
            if (ent.msgtime != cl.mtime[0]) {
                ent.model = nullptr;
                ++i;
                continue;
            }

            const Vector3 oldorg = ent.origin;

            if (ent.forcelink) { // the entity was not updated in the last message
                // so move to the final spot
                ent.origin = ent.msg_origins[0];
                ent.angles = ent.msg_angles[0];
            } else { // if the delta is large, assume a teleport and don't lerp
                float f = frac;
                Vector3 delta = ent.msg_origins[0] - ent.msg_origins[1];
                for (int j = 0; j < 3; ++j) {
                    if (delta[j] > 100.0f || delta[j] < -100.0f) {
                        f = 1.0f; // assume a teleportation, not a motion
                    }
                }

                // interpolate the origin and angles
                ent.origin = ent.msg_origins[1] + delta * f;
                for (int j = 0; j < 3; ++j) {
                    float d = ent.msg_angles[0][j] - ent.msg_angles[1][j];
                    if (d > 180.0f) {
                        d -= 360.0f;
                    } else if (d < -180.0f) {
                        d += 360.0f;
                    }

                    ent.angles[j] = ent.msg_angles[1][j] + f * d;
                }
            }

            // rotate binary objects locally
            if (ent.model->flags & EF_ROTATE) {
                ent.angles[1] = bobjrotate;
            }

            if (ent.effects & EF_BRIGHTFIELD) {
                R_EntityParticles(&ent);
            }

            if (ent.effects & EF_MUZZLEFLASH) {
                if (auto* dl = CL_AllocDlight(i)) {
                    dl->origin = ent.origin;
                    dl->origin.z += 16.0f;
                    Vector3 fv, rv, uv;
                    AngleVectors(ent.angles, fv, rv, uv);

                    dl->origin += fv * 18.0f;
                    dl->radius = static_cast<float>(200 + (rand() & 31));
                    dl->minlight = 32.0f;
                    dl->die = static_cast<float>(cl.time + 0.1);
                }
            }

            if (ent.effects & EF_BRIGHTLIGHT) {
                if (auto* dl = CL_AllocDlight(i)) {
                    dl->origin = ent.origin;
                    dl->origin.z += 16.0f;
                    dl->radius = static_cast<float>(400 + (rand() & 31));
                    dl->die = static_cast<float>(cl.time + 0.001);
                }
            }

            if (ent.effects & EF_DIMLIGHT) {
                if (auto* dl = CL_AllocDlight(i)) {
                    dl->origin = ent.origin;
                    dl->radius = static_cast<float>(200 + (rand() & 31));
                    dl->die = static_cast<float>(cl.time + 0.001);
                }
            }

            if (ent.model->flags & EF_GIB) {
                R_RocketTrail(oldorg, ent.origin, 2);
            } else if (ent.model->flags & EF_ZOMGIB) {
                R_RocketTrail(oldorg, ent.origin, 4);
            } else if (ent.model->flags & EF_TRACER) {
                R_RocketTrail(oldorg, ent.origin, 3);
            } else if (ent.model->flags & EF_TRACER2) {
                R_RocketTrail(oldorg, ent.origin, 5);
            } else if (ent.model->flags & EF_ROCKET) {
                R_RocketTrail(oldorg, ent.origin, 0);
                if (auto* dl = CL_AllocDlight(i)) {
                    dl->origin = ent.origin;
                    dl->radius = 200.0f;
                    dl->die = static_cast<float>(cl.time + 0.01);
                }
            } else if (ent.model->flags & EF_GRENADE) {
                R_RocketTrail(oldorg, ent.origin, 1);
            } else if (ent.model->flags & EF_TRACER3) {
                R_RocketTrail(oldorg, ent.origin, 6);
            }

            ent.forcelink = false;

            if (i != cl.viewentity || chase_active.value) {
                if (cl_numvisedicts < MAX_VISEDICTS) {
                    cl_visedicts[cl_numvisedicts] = &ent;
                    cl_numvisedicts++;
                }
            }
            ++i;
        }
    }
}

/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer()
{
    cl.oldtime = cl.time;
    cl.time += host_frametime;

    int ret;
    do {
        ret = CL_GetMessage();
        if (ret == -1) {
            Host_Error("CL_ReadFromServer: lost server connection");
        }

        if (!ret) {
            break;
        }

        cl.last_received_message = static_cast<float>(realtime);
        CL_ParseServerMessage();
    } while (ret && cls.state == ca_connected);

    if (cl_shownet.value) {
        Con_Printf("\n");
    }

    CL_RelinkEntities();
    CL_UpdateTEnts();

    return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd()
{
    usercmd_t cmd;

    if (cls.state != ca_connected) {
        return;
    }

    if (cls.signon == SIGNONS) {
        // get basic movement from keyboard
        CL_BaseMove(&cmd);

        // allow mice or other external controllers to add to the move
        IN_Move(&cmd);

        // send the unreliable message
        CL_SendMove(&cmd);
    }

    if (cls.demoplayback) {
        SZ_Clear(&cls.message);
        return;
    }

    // send the reliable message
    if (!cls.message.cursize) {
        return; // no message at all
    }

    if (!NET_CanSendMessage(cls.netcon)) {
        Con_DPrintf("CL_WriteToServer: can't send\n");
        return;
    }

    if (NET_SendMessage(cls.netcon, &cls.message) == -1) {
        Host_Error("CL_WriteToServer: lost server connection");
    }

    SZ_Clear(&cls.message);
}

/*
=================
CL_Init
=================
*/
void CL_Init()
{
    SZ_Alloc(&cls.message, 1024);

    CL_InitInput();
    CL_InitTEnts();

    // register our commands
    Cvar::Register(&cl_name);
    Cvar::Register(&cl_color);
    Cvar::Register(&cl_upspeed);
    Cvar::Register(&cl_forwardspeed);
    Cvar::Register(&cl_backspeed);
    Cvar::Register(&cl_sidespeed);
    Cvar::Register(&cl_movespeedkey);
    Cvar::Register(&cl_yawspeed);
    Cvar::Register(&cl_pitchspeed);
    Cvar::Register(&cl_anglespeedkey);
    Cvar::Register(&cl_shownet);
    Cvar::Register(&cl_nolerp);
    Cvar::Register(&lookspring);
    Cvar::Register(&lookstrafe);
    Cvar::Register(&sensitivity);

    Cvar::Register(&m_pitch);
    Cvar::Register(&m_yaw);
    Cvar::Register(&m_forward);
    Cvar::Register(&m_side);

    Cmd::AddCommand("entities", CL_PrintEntities_f);
    Cmd::AddCommand("disconnect", CL_Disconnect_f);
    Cmd::AddCommand("record", CL_Record_f);
    Cmd::AddCommand("stop", CL_Stop_f);
    Cmd::AddCommand("playdemo", CL_PlayDemo_f);
    Cmd::AddCommand("timedemo", CL_TimeDemo_f);
}

//============================================================================
// cl_input.cpp contents
//============================================================================

/*
===============
KeyDown
===============
*/
void KeyDown(kbutton_t* b)
{
    int k;
    std::string_view c = Cmd::Argv(1);
    if (!c.empty()) {
        k = Q_atoi(c);
    } else {
        k = -1; // typed manually at the console for continuous down
    }

    if (k == b->down[0] || k == b->down[1]) {
        return; // repeating key
    }

    if (!b->down[0]) {
        b->down[0] = k;
    } else if (!b->down[1]) {
        b->down[1] = k;
    } else {
        Con_Printf("Three keys down for a button!\n");
        return;
    }

    if (b->state & 1) {
        return; // still down
    }

    b->state |= 1 + 2; // down + impulse down
}

void KeyUp(kbutton_t* b)
{
    int k;
    std::string_view c = Cmd::Argv(1);
    if (!c.empty()) {
        k = Q_atoi(c);
    } else { // typed manually at the console, assume for unsticking, so clear all
        b->down[0] = b->down[1] = 0;
        b->state = 4; // impulse up
        return;
    }

    if (b->down[0] == k) {
        b->down[0] = 0;
    } else if (b->down[1] == k) {
        b->down[1] = 0;
    } else {
        return; // key up without coresponding down (menu pass through)
    }

    if (b->down[0] || b->down[1]) {
        return; // some other key is still holding it down
    }

    if (!(b->state & 1)) {
        return; // still up (this should not happen)
    }

    b->state &= ~1; // now up
    b->state |= 4;  // impulse up
}

/*
===============
CL_KeyState

Returns 0.25 if a key was pressed and released during the frame,
0.5 if it was pressed and held
0 if held then released, and
1.0 if held for the entire time
===============
*/
float CL_KeyState(kbutton_t* key)
{
    float val = 0.0f;
    const bool impulsedown = (key->state & 2) != 0;
    const bool impulseup = (key->state & 4) != 0;
    const bool down = (key->state & 1) != 0;

    if (impulsedown && !impulseup) {
        if (down) {
            val = 0.5f; // pressed and held this frame
        }
    }

    if (impulseup && !impulsedown) {
        if (!down) {
            val = 0.0f; // released this frame
        }
    }

    if (!impulsedown && !impulseup) {
        if (down) {
            val = 1.0f; // held the entire frame
        }
    }

    if (impulsedown && impulseup) {
        if (down) {
            val = 0.75f; // released and re-pressed this frame
        } else {
            val = 0.25f; // pressed and released this frame
        }
    }

    key->state &= 1; // clear impulses

    return val;
}

//==========================================================================

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles()
{
    float speed;

    if (in_speed.state & 1) {
        speed = static_cast<float>(host_frametime * cl_anglespeedkey.value);
    } else {
        speed = static_cast<float>(host_frametime);
    }

    if (!(in_strafe.state & 1)) {
        cl.viewangles[YAW] -= speed * cl_yawspeed.value * CL_KeyState(&in_right);
        cl.viewangles[YAW] += speed * cl_yawspeed.value * CL_KeyState(&in_left);
        cl.viewangles[YAW] = anglemod(cl.viewangles[YAW]);
    }

    if (in_klook.state & 1) {
        V_StopPitchDrift();
        cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * CL_KeyState(&in_forward);
        cl.viewangles[PITCH] += speed * cl_pitchspeed.value * CL_KeyState(&in_back);
    }

    const float up = CL_KeyState(&in_lookup);
    const float down = CL_KeyState(&in_lookdown);

    cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * up;
    cl.viewangles[PITCH] += speed * cl_pitchspeed.value * down;

    if (up || down) {
        V_StopPitchDrift();
    }

    if (cl.viewangles[PITCH] > 80.0f) {
        cl.viewangles[PITCH] = 80.0f;
    }

    if (cl.viewangles[PITCH] < -70.0f) {
        cl.viewangles[PITCH] = -70.0f;
    }

    if (cl.viewangles[ROLL] > 50.0f) {
        cl.viewangles[ROLL] = 50.0f;
    }

    if (cl.viewangles[ROLL] < -50.0f) {
        cl.viewangles[ROLL] = -50.0f;
    }
}

/*
================
CL_BaseMove

Send the intended movement message to the server
================
*/
void CL_BaseMove(usercmd_t* cmd)
{
    if (cls.signon != SIGNONS) {
        return;
    }

    CL_AdjustAngles();

    *cmd = {};

    if (in_strafe.state & 1) {
        cmd->sidemove += cl_sidespeed.value * CL_KeyState(&in_right);
        cmd->sidemove -= cl_sidespeed.value * CL_KeyState(&in_left);
    }

    cmd->sidemove += cl_sidespeed.value * CL_KeyState(&in_moveright);
    cmd->sidemove -= cl_sidespeed.value * CL_KeyState(&in_moveleft);

    cmd->upmove += cl_upspeed.value * CL_KeyState(&in_up);
    cmd->upmove -= cl_upspeed.value * CL_KeyState(&in_down);

    if (!(in_klook.state & 1)) {
        cmd->forwardmove += cl_forwardspeed.value * CL_KeyState(&in_forward);
        cmd->forwardmove -= cl_backspeed.value * CL_KeyState(&in_back);
    }

    // adjust for speed key
    if (in_speed.state & 1) {
        cmd->forwardmove *= cl_movespeedkey.value;
        cmd->sidemove *= cl_movespeedkey.value;
        cmd->upmove *= cl_movespeedkey.value;
    }
}

/*
==============
CL_SendMove
==============
*/
void CL_SendMove(usercmd_t* cmd)
{
    sizebuf_t buf;
    std::array<byte, 128> data{};

    buf.maxsize = 128;
    buf.cursize = 0;
    buf.data = data.data();

    cl.cmd = *cmd;

    // send the movement message
    MSG_WriteByte(&buf, clc_move);
    MSG_WriteFloat(&buf, static_cast<float>(cl.mtime[0])); // so server can get ping times

    for (int i = 0; i < 3; ++i) {
        MSG_WriteAngle(&buf, cl.viewangles[i]);
    }

    MSG_WriteShort(&buf, static_cast<int>(cmd->forwardmove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->sidemove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->upmove));

    // send button bits
    int bits = 0;

    if (in_attack.state & 3) {
        bits |= 1;
    }
    in_attack.state &= ~2;

    if (in_jump.state & 3) {
        bits |= 2;
    }
    in_jump.state &= ~2;

    MSG_WriteByte(&buf, bits);

    MSG_WriteByte(&buf, in_impulse);
    in_impulse = 0;

    // deliver the message
    if (cls.demoplayback) {
        return;
    }

    // always dump the first two message, because it may contain leftover inputs from the last level
    if (++cl.movemessages <= 2) {
        return;
    }

    if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
        Con_Printf("CL_SendMove: lost server connection\n");
        CL_Disconnect();
    }
}

/*
============
CL_InitInput
============
*/
void CL_InitInput()
{
    Cmd::AddCommand("+moveup", []() { KeyDown(&in_up); });
    Cmd::AddCommand("-moveup", []() { KeyUp(&in_up); });
    Cmd::AddCommand("+movedown", []() { KeyDown(&in_down); });
    Cmd::AddCommand("-movedown", []() { KeyUp(&in_down); });
    Cmd::AddCommand("+left", []() { KeyDown(&in_left); });
    Cmd::AddCommand("-left", []() { KeyUp(&in_left); });
    Cmd::AddCommand("+right", []() { KeyDown(&in_right); });
    Cmd::AddCommand("-right", []() { KeyUp(&in_right); });
    Cmd::AddCommand("+forward", []() { KeyDown(&in_forward); });
    Cmd::AddCommand("-forward", []() { KeyUp(&in_forward); });
    Cmd::AddCommand("+back", []() { KeyDown(&in_back); });
    Cmd::AddCommand("-back", []() { KeyUp(&in_back); });
    Cmd::AddCommand("+lookup", []() { KeyDown(&in_lookup); });
    Cmd::AddCommand("-lookup", []() { KeyUp(&in_lookup); });
    Cmd::AddCommand("+lookdown", []() { KeyDown(&in_lookdown); });
    Cmd::AddCommand("-lookdown", []() { KeyUp(&in_lookdown); });
    Cmd::AddCommand("+strafe", []() { KeyDown(&in_strafe); });
    Cmd::AddCommand("-strafe", []() { KeyUp(&in_strafe); });
    Cmd::AddCommand("+moveleft", []() { KeyDown(&in_moveleft); });
    Cmd::AddCommand("-moveleft", []() { KeyUp(&in_moveleft); });
    Cmd::AddCommand("+moveright", []() { KeyDown(&in_moveright); });
    Cmd::AddCommand("-moveright", []() { KeyUp(&in_moveright); });
    Cmd::AddCommand("+speed", []() { KeyDown(&in_speed); });
    Cmd::AddCommand("-speed", []() { KeyUp(&in_speed); });
    Cmd::AddCommand("+attack", []() { KeyDown(&in_attack); });
    Cmd::AddCommand("-attack", []() { KeyUp(&in_attack); });
    Cmd::AddCommand("+use", []() { KeyDown(&in_use); });
    Cmd::AddCommand("-use", []() { KeyUp(&in_use); });
    Cmd::AddCommand("+jump", []() { KeyDown(&in_jump); });
    Cmd::AddCommand("-jump", []() { KeyUp(&in_jump); });
    Cmd::AddCommand("impulse", []() { in_impulse = Q_atoi(Cmd::Argv(1)); });
    Cmd::AddCommand("+klook", []() { KeyDown(&in_klook); });
    Cmd::AddCommand("-klook", []() { KeyUp(&in_klook); });
    Cmd::AddCommand("+mlook", []() { KeyDown(&in_mlook); });
    Cmd::AddCommand("-mlook", []() { KeyUp(&in_mlook); if (!(in_mlook.state & 1) && lookspring.value) V_StartPitchDrift(); });
}

//============================================================================
// cl_demo.cpp contents
//============================================================================

void CL_FinishTimeDemo();

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback()
{
    if (!cls.demoplayback) {
        return;
    }

    fclose(cls.demofile);
    cls.demoplayback = false;
    cls.demofile = nullptr;
    cls.state = ca_disconnected;

    if (cls.timedemo) {
        CL_FinishTimeDemo();
    }
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length and view angles
====================
*/
void CL_WriteDemoMessage()
{
    int len = LittleLong(net_message.cursize);
    fwrite(&len, 4, 1, cls.demofile);
    for (int i = 0; i < 3; ++i) {
        float f = LittleFloat(cl.viewangles[i]);
        fwrite(&f, 4, 1, cls.demofile);
    }
    fwrite(net_message.data, net_message.cursize, 1, cls.demofile);
    fflush(cls.demofile);
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage()
{
    if (cls.demoplayback) {
        // decide if it is time to grab the next message
        if (cls.signon == SIGNONS) // always grab until fully connected
        {
            if (cls.timedemo) {
                if (host_framecount == cls.td_lastframe) {
                    return 0; // already read this frame's message
                }

                cls.td_lastframe = host_framecount;
                // if this is the second frame, grab the real td_starttime
                // so the bogus time on the first frame doesn't count
                if (host_framecount == cls.td_startframe + 1) {
                    cls.td_starttime = static_cast<float>(realtime);
                }
            } else if (cl.time <= cl.mtime[0]) {
                return 0; // don't need another message yet
            }
        }

        // get the next message
        fread(&net_message.cursize, 4, 1, cls.demofile);
        cl.mviewangles[1] = cl.mviewangles[0];
        for (int i = 0; i < 3; ++i) {
            float f = 0.0f;
            fread(&f, 4, 1, cls.demofile);
            cl.mviewangles[0][i] = LittleFloat(f);
        }

        net_message.cursize = LittleLong(net_message.cursize);
        if (net_message.cursize > MAX_MSGLEN) {
            Sys_Error("Demo message > MAX_MSGLEN");
        }

        const int r = static_cast<int>(fread(net_message.data, net_message.cursize, 1, cls.demofile));
        if (r != 1) {
            CL_StopPlayback();
            return 0;
        }

        return 1;
    }

    while (true) {
        const int r = NET_GetMessage(cls.netcon);

        if (r != 1 && r != 2) {
            return r;
        }

        // discard nop keepalive message
        if (net_message.cursize == 1 && net_message.data[0] == svc_nop) {
            Con_Printf("<-- server to client keepalive\n");
        } else {
            return r;
        }
    }

    if (cls.demorecording) {
        CL_WriteDemoMessage();
    }

    return 1;
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f()
{
    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    if (!cls.demorecording) {
        Con_Printf("Not recording a demo.\n");
        return;
    }

    // write a disconnect message to the demo file
    SZ_Clear(&net_message);
    MSG_WriteByte(&net_message, svc_disconnect);
    CL_WriteDemoMessage();

    // finish up
    fclose(cls.demofile);
    cls.demofile = nullptr;
    cls.demorecording = false;
    Con_Printf("Completed demo\n");
}

/*
====================
CL_Record_f

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f()
{
    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    const int c = Cmd::Argc();
    if (c != 2 && c != 3 && c != 4) {
        Con_Printf("record <demoname> [<map> [cd track]]\n");
        return;
    }

    if (Cmd::Argv(1).find("..") != std::string_view::npos) {
        Con_Printf("Relative pathnames are not allowed.\n");
        return;
    }

    if (c == 2 && cls.state == ca_connected) {
        Con_Printf(
            "Can not record - already connected to server\nClient demo recording "
            "must be started before connecting\n");
        return;
    }

    // write the forced cd track number, or -1
    int track = -1;
    if (c == 4) {
        track = Q_atoi(Cmd::Argv(3));
        Con_Printf("Forcing CD track to %i\n", cls.forcetrack);
    }

    std::string name = std::string(com_gamedir) + "/" + std::string(Cmd::Argv(1));

    // start the map up
    if (c > 2) {
        Cmd::ExecuteString(va("map %s", std::string(Cmd::Argv(2)).c_str()), Cmd::Source::Command);
    }

    // open the demo file
    char name_buffer[MAX_OSPATH];
    strcpy_s(name_buffer, sizeof(name_buffer), name.c_str());
    COM_DefaultExtension(name_buffer, ".dem");

    Con_Printf("recording to %s.\n", name_buffer);
    fopen_s(&cls.demofile, name_buffer, "wb");
    if (!cls.demofile) {
        Con_Printf("ERROR: couldn't open.\n");
        return;
    }

    cls.forcetrack = track;
    fprintf(cls.demofile, "%i\n", cls.forcetrack);

    cls.demorecording = true;
}

/*
====================
CL_PlayDemo_f

play [demoname]
====================
*/
void CL_PlayDemo_f()
{
    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    if (Cmd::Argc() != 2) {
        Con_Printf("play <demoname> : plays a demo\n");
        return;
    }

    // disconnect from server
    CL_Disconnect();

    // open the demo file
    char name[256];
    strcpy_s(name, sizeof(name), std::string(Cmd::Argv(1)).c_str());
    COM_DefaultExtension(name, ".dem");

    Con_Printf("Playing demo from %s.\n", name);
    COM_FOpenFile(name, &cls.demofile);
    if (!cls.demofile) {
        Con_Printf("ERROR: couldn't open.\n");
        cls.demonum = -1; // stop demo loop
        return;
    }

    cls.demoplayback = true;
    cls.state = ca_connected;
    cls.forcetrack = 0;

    int c;
    bool neg = false;
    while ((c = getc(cls.demofile)) != '\n') {
        if (c == '-') {
            neg = true;
        } else {
            cls.forcetrack = cls.forcetrack * 10 + (c - '0');
        }
    }

    if (neg) {
        cls.forcetrack = -cls.forcetrack;
    }
}

/*
====================
CL_FinishTimeDemo
====================
*/
void CL_FinishTimeDemo()
{
    cls.timedemo = false;

    // the first frame didn't count
    const int frames = (host_framecount - cls.td_startframe) - 1;
    float time = static_cast<float>(realtime - cls.td_starttime);
    if (!time) {
        time = 1.0f;
    }

    Con_Printf("%i frames %5.1f seconds %5.1f fps\n", frames, time,
        frames / time);
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void CL_TimeDemo_f()
{
    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    if (Cmd::Argc() != 2) {
        Con_Printf("timedemo <demoname> : gets demo speeds\n");
        return;
    }

    CL_PlayDemo_f();

    // cls.td_starttime will be grabbed at the second frame of the demo, so
    // all the loading time doesn't get counted
    cls.timedemo = true;
    cls.td_startframe = host_framecount;
    cls.td_lastframe = -1; // get a new message this frame
}

//============================================================================
// cl_parse.cpp contents
//============================================================================

/*
===============
CL_EntityNum

This error checks and tracks the total number of entities
===============
*/
entity_t* CL_EntityNum(int num)
{
    if (num >= cl.num_entities) {
        if (num >= MAX_EDICTS) {
            Host_Error("CL_EntityNum: %i is an invalid number", num);
        }

        while (cl.num_entities <= num) {
            cl_entities[cl.num_entities].colormap = vid.colormap;
            cl.num_entities++;
        }
    }

    return &cl_entities[num];
}

/*
==================
CL_ParseStartSoundPacket
==================
*/
void CL_ParseStartSoundPacket()
{
    int packet_vol = DEFAULT_SOUND_PACKET_VOLUME;
    float attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

    const int field_mask = MSG_ReadByte();

    if (field_mask & SND_VOLUME) {
        packet_vol = MSG_ReadByte();
    }

    if (field_mask & SND_ATTENUATION) {
        attenuation = MSG_ReadByte() / 64.0f;
    }

    int channel = MSG_ReadShort();
    const int sound_num = MSG_ReadByte();

    const int ent = channel >> 3;
    channel &= 7;

    if (ent > MAX_EDICTS) {
        Host_Error("CL_ParseStartSoundPacket: ent = %i", ent);
    }

    const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };

    S_StartSound(ent, channel, cl.sound_precache[sound_num], pos, packet_vol / 255.0f, attenuation);
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
void CL_KeepaliveMessage()
{
    if (sv.active) {
        return; // no need if server is local
    }

    if (cls.demoplayback) {
        return;
    }

    // read messages from server, should just be nops
    sizebuf_t old = net_message;
    std::array<byte, 8192> olddata;
    std::copy_n(net_message.data, std::min(static_cast<int>(olddata.size()), net_message.cursize), olddata.begin());

    int ret;
    do {
        ret = CL_GetMessage();
        switch (ret) {
        default:
            Host_Error("CL_KeepaliveMessage: CL_GetMessage failed");
        case 0:
            break; // nothing waiting
        case 1:
            Host_Error("CL_KeepaliveMessage: received a message");
            break;
        case 2:
            if (MSG_ReadByte() != svc_nop) {
                Host_Error("CL_KeepaliveMessage: datagram wasn't a nop");
            }
            break;
        }
    } while (ret);

    net_message = old;
    std::copy_n(olddata.begin(), std::min(static_cast<int>(olddata.size()), net_message.cursize), net_message.data);

    // check time
    const float time = static_cast<float>(Sys_FloatTime());
    static float lastmsg = 0.0f;
    if (time - lastmsg < 5.0f) {
        return;
    }

    lastmsg = time;

    // write out a nop
    Con_Printf("--> client to server keepalive\n");

    MSG_WriteByte(&cls.message, clc_nop);
    NET_SendMessage(cls.netcon, &cls.message);
    SZ_Clear(&cls.message);
}

/*
==================
CL_ParseServerInfo
==================
*/
void CL_ParseServerInfo()
{
    Con_DPrintf("Serverinfo packet received.\n");
    // wipe the client_state_t struct
    CL_ClearState();

    // parse protocol version number
    const int i = MSG_ReadLong();
    if (i != PROTOCOL_VERSION) {
        Con_Printf("Server returned version %i, not %i", i, PROTOCOL_VERSION);
        return;
    }

    // parse maxclients
    cl.maxclients = MSG_ReadByte();
    if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD) {
        Con_Printf("Bad maxclients (%u) from server\n", cl.maxclients);
        return;
    }

    cl.scores = static_cast<scoreboard_t*>(Hunk_Alloc(cl.maxclients * sizeof(*cl.scores), "scores"));

    // parse gametype
    cl.gametype = MSG_ReadByte();

    // parse signon message
    const char* str = MSG_ReadString();
    strncpy_s(cl.levelname, sizeof(cl.levelname), str, _TRUNCATE);

    // separate the printfs so the server message can have a color
    Con_Printf(
        "\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
        "\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
    Con_Printf("%c%s\n", 2, str);

    // touch all of the precache data that still happens to be in the cache
    std::array<std::string, MAX_MODELS> model_precache_names{};
    std::array<std::string, MAX_SOUNDS> sound_precache_names{};

    // precache models
    cl.model_precache.fill(nullptr);
    int nummodels = 1;
    while (true) {
        char* model_str = MSG_ReadString();
        if (!model_str[0]) {
            break;
        }

        if (nummodels == MAX_MODELS) {
            Con_Printf("Server sent too many model precaches\n");
            return;
        }

        model_precache_names[nummodels] = model_str;
        Mod_TouchModel(model_str);
        nummodels++;
    }

    // precache sounds
    cl.sound_precache.fill(nullptr);
    int numsounds = 1;
    while (true) {
        char* sound_str = MSG_ReadString();
        if (!sound_str[0]) {
            break;
        }

        if (numsounds == MAX_SOUNDS) {
            Con_Printf("Server sent too many sound precaches\n");
            return;
        }

        sound_precache_names[numsounds] = sound_str;
        S_TouchSound(sound_str);
        numsounds++;
    }

    // now try to load everything else
    for (int idx = 1; idx < nummodels; ++idx) {
        cl.model_precache[idx] = Mod_ForName(model_precache_names[idx].c_str(), false);
        if (cl.model_precache[idx] == nullptr) {
            Con_Printf("Model %s not found\n", model_precache_names[idx].c_str());
            return;
        }

        CL_KeepaliveMessage();
    }

    S_BeginPrecaching();
    for (int idx = 1; idx < numsounds; ++idx) {
        cl.sound_precache[idx] = S_PrecacheSound(sound_precache_names[idx].c_str());
        CL_KeepaliveMessage();
    }
    S_EndPrecaching();

    // local state
    cl_entities[0].model = cl.worldmodel = cl.model_precache[1];

    R_NewMap();

    Hunk_Check(); // make sure nothing is hurt

    noclip_anglehack = false; // noclip is turned off at start
}

/*
==================
CL_ParseUpdate

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
void CL_ParseUpdate(int bits)
{
    if (cls.signon == SIGNONS - 1) { // first update is the final signon stage
        cls.signon = SIGNONS;
        CL_SignonReply();
    }

    if (bits & U_MOREBITS) {
        const int i = MSG_ReadByte();
        bits |= (i << 8);
    }

    int num;
    if (bits & U_LONGENTITY) {
        num = MSG_ReadShort();
    } else {
        num = MSG_ReadByte();
    }

    entity_t* ent = CL_EntityNum(num);

    for (int i = 0; i < 16; ++i) {
        if (bits & (1 << i)) {
            bitcounts[i]++;
        }
    }

    const bool forcelink = (ent->msgtime != cl.mtime[1]);

    ent->msgtime = cl.mtime[0];

    int modnum;
    if (bits & U_MODEL) {
        modnum = MSG_ReadByte();
        if (modnum >= MAX_MODELS) {
            Host_Error("CL_ParseModel: bad modnum");
        }
    } else {
        modnum = ent->baseline.modelindex;
    }

    model_t* model = cl.model_precache[modnum];
    if (model != ent->model) {
        ent->model = model;
        // automatic animation (torches, etc) can be either all together or randomized
        if (model) {
            if (model->synctype == synctype_t::ST_RAND) {
                ent->syncbase = static_cast<float>(rand() & 0x7fff) / 0x7fff;
            } else {
                ent->syncbase = 0.0f;
            }
        }
    }

    if (bits & U_FRAME) {
        ent->frame = MSG_ReadByte();
    } else {
        ent->frame = ent->baseline.frame;
    }

    int colormap_idx;
    if (bits & U_COLORMAP) {
        colormap_idx = MSG_ReadByte();
    } else {
        colormap_idx = ent->baseline.colormap;
    }

    if (!colormap_idx) {
        ent->colormap = vid.colormap;
    } else {
        if (colormap_idx > cl.maxclients) {
            Sys_Error("colormap_idx >= cl.maxclients");
        }

        ent->colormap = cl.scores[colormap_idx - 1].translations;
    }

    if (bits & U_SKIN) {
        ent->skinnum = MSG_ReadByte();
    } else {
        ent->skinnum = ent->baseline.skin;
    }

    if (bits & U_EFFECTS) {
        ent->effects = MSG_ReadByte();
    } else {
        ent->effects = ent->baseline.effects;
    }

    // shift the known values for interpolation
    ent->msg_origins[1] = ent->msg_origins[0];
    ent->msg_angles[1] = ent->msg_angles[0];

    if (bits & U_ORIGIN1) {
        ent->msg_origins[0][0] = MSG_ReadCoord();
    } else {
        ent->msg_origins[0][0] = ent->baseline.origin[0];
    }

    if (bits & U_ANGLE1) {
        ent->msg_angles[0][0] = MSG_ReadAngle();
    } else {
        ent->msg_angles[0][0] = ent->baseline.angles[0];
    }

    if (bits & U_ORIGIN2) {
        ent->msg_origins[0][1] = MSG_ReadCoord();
    } else {
        ent->msg_origins[0][1] = ent->baseline.origin[1];
    }

    if (bits & U_ANGLE2) {
        ent->msg_angles[0][1] = MSG_ReadAngle();
    } else {
        ent->msg_angles[0][1] = ent->baseline.angles[1];
    }

    if (bits & U_ORIGIN3) {
        ent->msg_origins[0][2] = MSG_ReadCoord();
    } else {
        ent->msg_origins[0][2] = ent->baseline.origin[2];
    }

    if (bits & U_ANGLE3) {
        ent->msg_angles[0][2] = MSG_ReadAngle();
    } else {
        ent->msg_angles[0][2] = ent->baseline.angles[2];
    }

    if (bits & U_NOLERP) {
        ent->forcelink = true;
    }

    if (forcelink || ent->forcelink) { // didn't have an update last message
        ent->msg_origins[1] = ent->msg_origins[0];
        ent->origin = ent->msg_origins[0];
        ent->msg_angles[1] = ent->msg_angles[0];
        ent->angles = ent->msg_angles[0];
        ent->forcelink = true;
    }
}

/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline(entity_t* ent)
{
    ent->baseline.modelindex = MSG_ReadByte();
    ent->baseline.frame = MSG_ReadByte();
    ent->baseline.colormap = MSG_ReadByte();
    ent->baseline.skin = MSG_ReadByte();
    for (int i = 0; i < 3; ++i) {
        ent->baseline.origin[i] = MSG_ReadCoord();
        ent->baseline.angles[i] = MSG_ReadAngle();
    }
}

/*
==================
CL_ParseClientdata

Server information pertaining to this client only
==================
*/
void CL_ParseClientdata(int bits)
{
    if (bits & SU_VIEWHEIGHT) {
        cl.viewheight = static_cast<float>(MSG_ReadChar());
    } else {
        cl.viewheight = static_cast<float>(DEFAULT_VIEWHEIGHT);
    }

    if (bits & SU_IDEALPITCH) {
        cl.idealpitch = static_cast<float>(MSG_ReadChar());
    } else {
        cl.idealpitch = 0.0f;
    }

    cl.mvelocity[1] = cl.mvelocity[0];
    for (int i = 0; i < 3; ++i) {
        if (bits & (SU_PUNCH1 << i)) {
            cl.punchangle[i] = static_cast<float>(MSG_ReadChar());
        } else {
            cl.punchangle[i] = 0.0f;
        }

        if (bits & (SU_VELOCITY1 << i)) {
            cl.mvelocity[0][i] = static_cast<float>(MSG_ReadChar() * 16);
        } else {
            cl.mvelocity[0][i] = 0.0f;
        }
    }

    const int i = MSG_ReadLong();

    if (cl.items != i) { // set flash times
        Sbar_Changed();
        for (int j = 0; j < 32; ++j) {
            if ((i & (1 << j)) && !(cl.items & (1 << j))) {
                cl.item_gettime[j] = static_cast<float>(cl.time);
            }
        }
        cl.items = i;
    }

    cl.onground = (bits & SU_ONGROUND) != 0;
    cl.inwater = (bits & SU_INWATER) != 0;

    if (bits & SU_WEAPONFRAME) {
        cl.stats[STAT_WEAPONFRAME] = MSG_ReadByte();
    } else {
        cl.stats[STAT_WEAPONFRAME] = 0;
    }

    int armor_val = 0;
    if (bits & SU_ARMOR) {
        armor_val = MSG_ReadByte();
    }

    if (cl.stats[STAT_ARMOR] != armor_val) {
        cl.stats[STAT_ARMOR] = armor_val;
        Sbar_Changed();
    }

    int weapon_val = 0;
    if (bits & SU_WEAPON) {
        weapon_val = MSG_ReadByte();
    }

    if (cl.stats[STAT_WEAPON] != weapon_val) {
        cl.stats[STAT_WEAPON] = weapon_val;
        Sbar_Changed();
    }

    const int health_val = MSG_ReadShort();
    if (cl.stats[STAT_HEALTH] != health_val) {
        cl.stats[STAT_HEALTH] = health_val;
        Sbar_Changed();
    }

    const int ammo_val = MSG_ReadByte();
    if (cl.stats[STAT_AMMO] != ammo_val) {
        cl.stats[STAT_AMMO] = ammo_val;
        Sbar_Changed();
    }

    for (int idx = 0; idx < 4; ++idx) {
        const int shells_val = MSG_ReadByte();
        if (cl.stats[STAT_SHELLS + idx] != shells_val) {
            cl.stats[STAT_SHELLS + idx] = shells_val;
            Sbar_Changed();
        }
    }

    const int active_weapon_val = MSG_ReadByte();

    if (standard_quake) {
        if (cl.stats[STAT_ACTIVEWEAPON] != active_weapon_val) {
            cl.stats[STAT_ACTIVEWEAPON] = active_weapon_val;
            Sbar_Changed();
        }
    } else {
        if (cl.stats[STAT_ACTIVEWEAPON] != (1 << active_weapon_val)) {
            cl.stats[STAT_ACTIVEWEAPON] = (1 << active_weapon_val);
            Sbar_Changed();
        }
    }
}

/*
=====================
CL_NewTranslation
=====================
*/
void CL_NewTranslation(int slot)
{
    if (slot > cl.maxclients) {
        Sys_Error("CL_NewTranslation: slot > cl.maxclients");
    }

    byte* dest = cl.scores[slot].translations;
    const byte* source = vid.colormap;
    std::copy_n(vid.colormap, sizeof(cl.scores[slot].translations), dest);
    const int top = cl.scores[slot].colors & 0xf0;
    const int bottom = (cl.scores[slot].colors & 15) << 4;

    for (int i = 0; i < VID_GRADES; ++i, dest += 256, source += 256) {
        if (top < 128) { // the artists made some backwards ranges.  sigh.
            std::copy_n(source + top, 16, dest + TOP_RANGE);
        } else {
            for (int j = 0; j < 16; ++j) {
                dest[TOP_RANGE + j] = source[top + 15 - j];
            }
        }

        if (bottom < 128) {
            std::copy_n(source + bottom, 16, dest + BOTTOM_RANGE);
        } else {
            for (int j = 0; j < 16; ++j) {
                dest[BOTTOM_RANGE + j] = source[bottom + 15 - j];
            }
        }
    }
}

/*
=====================
CL_ParseStatic
=====================
*/
void CL_ParseStatic()
{
    const int i = cl.num_statics;
    if (i >= MAX_STATIC_ENTITIES) {
        Host_Error("Too many static entities");
    }

    entity_t* ent = &cl_static_entities[i];
    cl.num_statics++;
    CL_ParseBaseline(ent);

    // copy it to the current state
    ent->model = cl.model_precache[ent->baseline.modelindex];
    ent->frame = ent->baseline.frame;
    ent->colormap = vid.colormap;
    ent->skinnum = ent->baseline.skin;
    ent->effects = ent->baseline.effects;

    ent->origin = ent->baseline.origin;
    ent->angles = ent->baseline.angles;
    R_AddEfrags(ent);
}

/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound()
{
    const Vector3 org{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    const int sound_num = MSG_ReadByte();
    const int vol = MSG_ReadByte();
    const int atten = MSG_ReadByte();

    S_StaticSound(cl.sound_precache[sound_num], org, static_cast<float>(vol), static_cast<float>(atten));
}

#define SHOWNET(x)             \
    if (cl_shownet.value == 2) \
        Con_Printf("%3i:%s\n", msg_readcount - 1, x);

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage()
{
    if (cl_shownet.value == 1) {
        Con_Printf("%i ", net_message.cursize);
    } else if (cl_shownet.value == 2) {
        Con_Printf("------------------\n");
    }

    cl.onground = false; // unless the server says otherwise

    MSG_BeginReading();

    while (true) {
        if (msg_badread) {
            Host_Error("CL_ParseServerMessage: Bad server message");
        }

        const int cmd = MSG_ReadByte();

        if (cmd == -1) {
            SHOWNET("END OF MESSAGE");
            return; // end of message
        }

        // if the high bit of the command byte is set, it is a fast update
        if (cmd & 128) {
            SHOWNET("fast update");
            CL_ParseUpdate(cmd & 127);
            continue;
        }

        SHOWNET(svc_strings[cmd]);

        // other commands
        switch (cmd) {
        default:
            Host_Error("CL_ParseServerMessage: Illegible server message\n");
            break;

        case svc_nop:
            break;

        case svc_time:
            cl.mtime[1] = cl.mtime[0];
            cl.mtime[0] = MSG_ReadFloat();
            break;

        case svc_clientdata: {
            const int i = MSG_ReadShort();
            CL_ParseClientdata(i);
            break;
        }

        case svc_version: {
            const int i = MSG_ReadLong();
            if (i != PROTOCOL_VERSION) {
                Host_Error(
                    "CL_ParseServerMessage: Server is protocol %i instead of %i\n", i,
                    PROTOCOL_VERSION);
            }
            break;
        }

        case svc_disconnect:
            Host_EndGame("Server disconnected\n");
            break;

        case svc_print:
            Con_Printf("%s", MSG_ReadString());
            break;

        case svc_centerprint:
            Screen::GetScreenSystem().CenterPrint(MSG_ReadString());
            break;

        case svc_stufftext:
            Cmd::BufferAddText(MSG_ReadString());
            break;

        case svc_damage:
            V_ParseDamage();
            break;

        case svc_serverinfo:
            CL_ParseServerInfo();
            vid.recalc_refdef = true; // leave intermission full screen
            break;

        case svc_setangle:
            for (int i = 0; i < 3; ++i) {
                cl.viewangles[i] = MSG_ReadAngle();
            }
            break;

        case svc_setview:
            cl.viewentity = MSG_ReadShort();
            break;

        case svc_lightstyle: {
            const int i = MSG_ReadByte();
            if (i >= MAX_LIGHTSTYLES) {
                Sys_Error("svc_lightstyle > MAX_LIGHTSTYLES");
            }

            Q_strcpy(cl_lightstyle[i].map, MSG_ReadString());
            cl_lightstyle[i].length = Q_strlen(cl_lightstyle[i].map);
            break;
        }

        case svc_sound:
            CL_ParseStartSoundPacket();
            break;

        case svc_stopsound: {
            const int i = MSG_ReadShort();
            S_StopSound(i >> 3, i & 7);
            break;
        }

        case svc_updatename: {
            Sbar_Changed();
            const int i = MSG_ReadByte();
            if (i >= cl.maxclients) {
                Host_Error("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
            }

            strcpy_s(cl.scores[i].name, sizeof(cl.scores[i].name), MSG_ReadString());
            break;
        }

        case svc_updatefrags: {
            Sbar_Changed();
            const int i = MSG_ReadByte();
            if (i >= cl.maxclients) {
                Host_Error("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
            }

            cl.scores[i].frags = MSG_ReadShort();
            break;
        }

        case svc_updatecolors: {
            Sbar_Changed();
            const int i = MSG_ReadByte();
            if (i >= cl.maxclients) {
                Host_Error(
                    "CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
            }

            cl.scores[i].colors = MSG_ReadByte();
            CL_NewTranslation(i);
            break;
        }

        case svc_particle:
            R_ParseParticleEffect();
            break;

        case svc_spawnbaseline: {
            const int i = MSG_ReadShort();
            // must use CL_EntityNum() to force cl.num_entities up
            CL_ParseBaseline(CL_EntityNum(i));
            break;
        }
        case svc_spawnstatic:
            CL_ParseStatic();
            break;
        case svc_temp_entity:
            CL_ParseTEnt();
            break;

        case svc_setpause:
            cl.paused = MSG_ReadByte();
            VID_HandlePause();
            break;

        case svc_signonnum: {
            const int i = MSG_ReadByte();
            if (i <= cls.signon) {
                Host_Error("Received signon %i when at %i", i, cls.signon);
            }

            cls.signon = i;
            CL_SignonReply();
            break;
        }

        case svc_killedmonster:
            cl.stats[STAT_MONSTERS]++;
            break;

        case svc_foundsecret:
            cl.stats[STAT_SECRETS]++;
            break;

        case svc_updatestat: {
            const int i = MSG_ReadByte();
            if (i < 0 || i >= MAX_CL_STATS) {
                Sys_Error("svc_updatestat: %i is invalid", i);
            }

            cl.stats[i] = MSG_ReadLong();
            break;
        }

        case svc_spawnstaticsound:
            CL_ParseStaticSound();
            break;

        case svc_cdtrack:
            cl.cdtrack = MSG_ReadByte();
            cl.looptrack = MSG_ReadByte();
            break;

        case svc_intermission:
            cl.intermission = 1;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true; // go to full screen
            break;

        case svc_finale:
            cl.intermission = 2;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true; // go to full screen
            Screen::GetScreenSystem().CenterPrint(MSG_ReadString());
            break;

        case svc_cutscene:
            cl.intermission = 3;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true; // go to full screen
            Screen::GetScreenSystem().CenterPrint(MSG_ReadString());
            break;

        case svc_sellscreen:
            Cmd::ExecuteString("help", Cmd::Source::Command);
            break;
        }
    }
}

//============================================================================
// cl_tent.cpp contents
//============================================================================

/*
=================
CL_ParseTEnt
=================
*/
void CL_InitTEnts()
{
    cl_sfx_wizhit = S_PrecacheSound("wizard/hit.wav");
    cl_sfx_knighthit = S_PrecacheSound("hknight/hit.wav");
    cl_sfx_tink1 = S_PrecacheSound("weapons/tink1.wav");
    cl_sfx_ric1 = S_PrecacheSound("weapons/ric1.wav");
    cl_sfx_ric2 = S_PrecacheSound("weapons/ric2.wav");
    cl_sfx_ric3 = S_PrecacheSound("weapons/ric3.wav");
    cl_sfx_r_exp3 = S_PrecacheSound("weapons/r_exp3.wav");
}

/*
=================
CL_ParseBeam
=================
*/
void CL_ParseBeam(model_t* m)
{
    const int ent = MSG_ReadShort();
    const Vector3 start{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    const Vector3 end{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };

    // override any beam with the same entity
    for (auto& b : cl_beams) {
        if (b.entity == ent) {
            b.entity = ent;
            b.model = m;
            b.endtime = static_cast<float>(cl.time + 0.2);
            b.start = start;
            b.end = end;
            return;
        }
    }

    // find a free beam
    for (auto& b : cl_beams) {
        if (!b.model || b.endtime < cl.time) {
            b.entity = ent;
            b.model = m;
            b.endtime = static_cast<float>(cl.time + 0.2);
            b.start = start;
            b.end = end;
            return;
        }
    }
    Con_Printf("beam list overflow!\n");
}

/*
=================
CL_ParseTEnt
=================
*/
void CL_ParseTEnt()
{
    const int type = MSG_ReadByte();
    switch (type) {
    case TE_WIZSPIKE: { // spike hitting wall
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 20, 30);
        S_StartSound(-1, 0, cl_sfx_wizhit, pos, 1, 1);
        break;
    }

    case TE_KNIGHTSPIKE: { // spike hitting wall
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 226, 20);
        S_StartSound(-1, 0, cl_sfx_knighthit, pos, 1, 1);
        break;
    }

    case TE_SPIKE: { // spike hitting wall
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 0, 10);
        if (rand() % 5) {
            S_StartSound(-1, 0, cl_sfx_tink1, pos, 1, 1);
        } else {
            const int rnd = rand() & 3;
            if (rnd == 1) {
                S_StartSound(-1, 0, cl_sfx_ric1, pos, 1, 1);
            } else if (rnd == 2) {
                S_StartSound(-1, 0, cl_sfx_ric2, pos, 1, 1);
            } else {
                S_StartSound(-1, 0, cl_sfx_ric3, pos, 1, 1);
            }
        }
        break;
    }

    case TE_SUPERSPIKE: { // super spike hitting wall
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 0, 20);

        if (rand() % 5) {
            S_StartSound(-1, 0, cl_sfx_tink1, pos, 1, 1);
        } else {
            const int rnd = rand() & 3;
            if (rnd == 1) {
                S_StartSound(-1, 0, cl_sfx_ric1, pos, 1, 1);
            } else if (rnd == 2) {
                S_StartSound(-1, 0, cl_sfx_ric2, pos, 1, 1);
            } else {
                S_StartSound(-1, 0, cl_sfx_ric3, pos, 1, 1);
            }
        }
        break;
    }

    case TE_GUNSHOT: { // bullet hitting wall
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 0, 20);
        break;
    }

    case TE_EXPLOSION: { // rocket explosion
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_ParticleExplosion(pos);
        if (auto* dl = CL_AllocDlight(0)) {
            dl->origin = pos;
            dl->radius = 350.0f;
            dl->die = static_cast<float>(cl.time + 0.5);
            dl->decay = 300.0f;
        }
        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;
    }

    case TE_TAREXPLOSION: { // tarbaby explosion
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_BlobExplosion(pos);
        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;
    }

    case TE_LIGHTNING1: // lightning bolts
        CL_ParseBeam(Mod_ForName("progs/bolt.mdl", true));
        break;

    case TE_LIGHTNING2: // lightning bolts
        CL_ParseBeam(Mod_ForName("progs/bolt2.mdl", true));
        break;

    case TE_LIGHTNING3: // lightning bolts
        CL_ParseBeam(Mod_ForName("progs/bolt3.mdl", true));
        break;

    // PGM 01/21/97
    case TE_BEAM: // grappling hook beam
        CL_ParseBeam(Mod_ForName("progs/beam.mdl", true));
        break;
        // PGM 01/21/97

    case TE_LAVASPLASH: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_LavaSplash(pos);
        break;
    }

    case TE_TELEPORT: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_TeleportSplash(pos);
        break;
    }

    case TE_EXPLOSION2: { // color mapped explosion
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        const int colorStart = MSG_ReadByte();
        const int colorLength = MSG_ReadByte();
        R_ParticleExplosion2(pos, colorStart, colorLength);
        if (auto* dl = CL_AllocDlight(0)) {
            dl->origin = pos;
            dl->radius = 350.0f;
            dl->die = static_cast<float>(cl.time + 0.5);
            dl->decay = 300.0f;
        }
        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;
    }

    default:
        Sys_Error("CL_ParseTEnt: bad type");
    }
}

/*
=================
CL_NewTempEntity
=================
*/
entity_t* CL_NewTempEntity()
{
    if (cl_numvisedicts == MAX_VISEDICTS) {
        return nullptr;
    }

    if (num_temp_entities == MAX_TEMP_ENTITIES) {
        return nullptr;
    }

    entity_t* ent = &cl_temp_entities[num_temp_entities];
    *ent = {};
    num_temp_entities++;
    cl_visedicts[cl_numvisedicts] = ent;
    cl_numvisedicts++;

    ent->colormap = vid.colormap;

    return ent;
}

/*
=================
CL_UpdateTEnts
=================
*/
void CL_UpdateTEnts()
{
    num_temp_entities = 0;

    // update lightning
    int i = 0;
    for (auto& b : cl_beams) {
        if (!b.model || b.endtime < cl.time) {
            ++i;
            continue;
        }

        // if coming from the player, update the start position
        if (b.entity == cl.viewentity) {
            b.start = cl_entities[cl.viewentity].origin;
        }

        // calculate pitch and yaw
        Vector3 dist = b.end - b.start;
        float yaw, pitch;

        if (dist.y == 0.0f && dist.x == 0.0f) {
            yaw = 0.0f;
            if (dist.z > 0.0f) {
                pitch = 90.0f;
            } else {
                pitch = 270.0f;
            }
        } else {
            yaw = static_cast<float>(atan2(dist.y, dist.x) * 180.0 / M_PI);
            if (yaw < 0.0f) {
                yaw += 360.0f;
            }

            const float forward = sqrt(dist.x * dist.x + dist.y * dist.y);
            pitch = static_cast<float>(atan2(dist.z, forward) * 180.0 / M_PI);
            if (pitch < 0.0f) {
                pitch += 360.0f;
            }
        }

        // add new entities for the lightning
        Vector3 org = b.start;
        float d = dist.normalize();
        while (d > 0.0f) {
            entity_t* ent = CL_NewTempEntity();
            if (!ent) {
                return;
            }

            ent->origin = org;
            ent->model = b.model;
            ent->angles[0] = pitch;
            ent->angles[1] = yaw;
            ent->angles[2] = static_cast<float>(rand() % 360);

            org += dist * 30.0f;
            d -= 30.0f;
        }
        ++i;
    }
}

} // namespace Client
