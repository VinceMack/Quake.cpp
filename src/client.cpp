// client.cpp -- client subsystem (merged from cl_main.cpp, cl_input.cpp, cl_demo.cpp, cl_parse.cpp, cl_tent.cpp)

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
const char* svc_strings[] = {
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
int bitcounts[16];

// from cl_tent.cpp
int num_temp_entities;
// cl_temp_entities and cl_beams are encapsulated in ClientSubsystem
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
void CL_ClearState(void)
{
    int i;

    if (!sv.active) {
        Host_ClearMemory();
    }

    // wipe the entire cl structure
    cl = {};

    SZ_Clear(&cls.message);

    // clear other arrays
    memset(cl_efrags, 0, sizeof(cl_efrags));
    for (auto& e : cl_entities) e = {};
    for (auto& dl : cl_dlights) dl = {};
    memset(cl_lightstyle, 0, sizeof(cl_lightstyle));
    for (auto& e : cl_temp_entities) e = {};
    for (auto& b : cl_beams) b = {};

    //
    // allocate the efrags and chain together into a free list
    //
    cl.free_efrags = cl_efrags;
    for (i = 0; i < MAX_EFRAGS - 1; i++) {
        cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];
    }
    cl.free_efrags[i].entnext = NULL;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect(void)
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

void CL_Disconnect_f(void)
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
void CL_SignonReply(void)
{
    char str[8192];

    Con_DPrintf("CL_SignonReply: %i\n", cls.signon);

    switch (cls.signon) {
    case 1:
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, "prespawn");
        break;

    case 2:
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, va("name \"%s\"\n", cl_name.string));

        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message,
            va("color %i %i\n", ((int)cl_color.value) >> 4,
                ((int)cl_color.value) & 15));

        MSG_WriteByte(&cls.message, clc_stringcmd);
        sprintf_s(str, sizeof(str), "spawn %s", cls.spawnparms);
        MSG_WriteString(&cls.message, str);
        break;

    case 3:
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, "begin");
        Cache_Report(); // print remaining memory
        break;

    case 4:
        SCR_EndLoadingPlaque(); // allow normal screen updates
        break;
    }
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo(void)
{
    char str[1024];

    if (cls.demonum == -1) {
        return; // don't play demos
    }

    SCR_BeginLoadingPlaque();

    if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS) {
        cls.demonum = 0;
        if (!cls.demos[cls.demonum][0]) {
            Con_Printf("No demos listed with startdemos\n");
            cls.demonum = -1;

            return;
        }
    }

    sprintf_s(str, sizeof(str), "playdemo %s\n", cls.demos[cls.demonum]);
    Cmd::BufferInsertText(str);
    cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f(void)
{
    entity_t* ent;
    int i;

    for (i = 0, ent = cl_entities; i < cl.num_entities; i++, ent++) {
        Con_Printf("%3i:", i);
        if (!ent->model) {
            Con_Printf("EMPTY\n");
            continue;
        }

        Con_Printf("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n",
            ent->model->name, ent->frame, ent->origin[0], ent->origin[1],
            ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
    }
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t* CL_AllocDlight(int key)
{
    int i;
    dlight_t* dl;

    // first look for an exact key match
    if (key) {
        dl = cl_dlights;
        for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
            if (dl->key == key) {
                *dl = {};
                dl->key = key;

                return dl;
            }
        }
    }

    // then look for anything else
    dl = cl_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
        if (dl->die < cl.time) {
            *dl = {};
            dl->key = key;

            return dl;
        }
    }

    dl = &cl_dlights[0];
    *dl = {};
    dl->key = key;

    return dl;
}

/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights(void)
{
    int i;
    dlight_t* dl;
    float time;

    time = static_cast<float>(cl.time - cl.oldtime);

    dl = cl_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
        if (dl->die < cl.time || !dl->radius) {
            continue;
        }

        dl->radius -= time * dl->decay;
        if (dl->radius < 0) {
            dl->radius = 0;
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
float CL_LerpPoint(void)
{
    float f, frac;

    f = static_cast<float>(cl.mtime[0] - cl.mtime[1]);

    if (!f || cl_nolerp.value || cls.timedemo || sv.active) {
        cl.time = cl.mtime[0];

        return 1;
    }

    if (f > 0.1) { // dropped packet, or start of demo
        cl.mtime[1] = cl.mtime[0] - 0.1;
        f = 0.1f;
    }

    frac = static_cast<float>((cl.time - cl.mtime[1]) / f);
    //Con_Printf ("frac: %f\n",frac);
    if (frac < 0) {
        if (frac < -0.01) {
            cl.time = cl.mtime[1];
            //				Con_Printf ("low frac\n");
        }

        frac = 0;
    } else if (frac > 1) {
        if (frac > 1.01) {
            cl.time = cl.mtime[0];
            //				Con_Printf ("high frac\n");
        }

        frac = 1;
    }

    return frac;
}

/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities(void)
{
    entity_t* ent;
    int i, j;
    float frac, f, d;
    Vector3 delta;
    float bobjrotate;
    Vector3 oldorg;
    dlight_t* dl;

    // determine partial update time
    frac = CL_LerpPoint();

    cl_numvisedicts = 0;

    //
    // interpolate player info
    //
    cl.velocity = cl.mvelocity[1] + (cl.mvelocity[0] - cl.mvelocity[1]) * frac;

    if (cls.demoplayback) {
        // interpolate the angles
        for (j = 0; j < 3; j++) {
            d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
            if (d > 180) {
                d -= 360;
            } else if (d < -180) {
                d += 360;
            }

            cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
        }
    }

    bobjrotate = anglemod(static_cast<float>(100 * cl.time));

    // start on the entity after the world
    for (i = 1, ent = cl_entities + 1; i < cl.num_entities; i++, ent++) {
        if (!ent->model) { // empty slot
            if (ent->forcelink) {
                R_RemoveEfrags(ent); // just became empty
            }

            continue;
        }

        // if the object wasn't included in the last packet, remove it
        if (ent->msgtime != cl.mtime[0]) {
            ent->model = NULL;
            continue;
        }

        oldorg = ent->origin;

        if (ent->forcelink) { // the entity was not updated in the last message
            // so move to the final spot
            ent->origin = ent->msg_origins[0];
            ent->angles = ent->msg_angles[0];
        } else { // if the delta is large, assume a teleport and don't lerp
            f = frac;
            delta = ent->msg_origins[0] - ent->msg_origins[1];
            for (j = 0; j < 3; j++) {
                if (delta[j] > 100 || delta[j] < -100) {
                    f = 1; // assume a teleportation, not a motion
                }
            }

            // interpolate the origin and angles
            ent->origin = ent->msg_origins[1] + delta * f;
            for (j = 0; j < 3; j++) {
                d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
                if (d > 180) {
                    d -= 360;
                } else if (d < -180) {
                    d += 360;
                }

                ent->angles[j] = ent->msg_angles[1][j] + f * d;
            }
        }

        // rotate binary objects locally
        if (ent->model->flags & EF_ROTATE) {
            ent->angles[1] = bobjrotate;
        }

        if (ent->effects & EF_BRIGHTFIELD) {
            R_EntityParticles(ent);
        }

        if (ent->effects & EF_MUZZLEFLASH) {
            Vector3 fv, rv, uv;

            dl = CL_AllocDlight(i);
            dl->origin = ent->origin;
            dl->origin.z += 16;
            AngleVectors(ent->angles, fv, rv, uv);

            dl->origin += fv * 18;
            dl->radius = static_cast<float>(200 + (rand() & 31));
            dl->minlight = 32;
            dl->die = static_cast<float>(cl.time + 0.1);
        }

        if (ent->effects & EF_BRIGHTLIGHT) {
            dl = CL_AllocDlight(i);
            VectorCopy(ent->origin, dl->origin);
            dl->origin[2] += 16;
            dl->radius = static_cast<float>(400 + (rand() & 31));
            dl->die = static_cast<float>(cl.time + 0.001);
        }

        if (ent->effects & EF_DIMLIGHT) {
            dl = CL_AllocDlight(i);
            VectorCopy(ent->origin, dl->origin);
            dl->radius = static_cast<float>(200 + (rand() & 31));
            dl->die = static_cast<float>(cl.time + 0.001);
        }


        if (ent->model->flags & EF_GIB) {
            R_RocketTrail(oldorg, ent->origin, 2);
        } else if (ent->model->flags & EF_ZOMGIB) {
            R_RocketTrail(oldorg, ent->origin, 4);
        } else if (ent->model->flags & EF_TRACER) {
            R_RocketTrail(oldorg, ent->origin, 3);
        } else if (ent->model->flags & EF_TRACER2) {
            R_RocketTrail(oldorg, ent->origin, 5);
        } else if (ent->model->flags & EF_ROCKET) {
            R_RocketTrail(oldorg, ent->origin, 0);
            dl = CL_AllocDlight(i);
            VectorCopy(ent->origin, dl->origin);
            dl->radius = 200.0f;
            dl->die = static_cast<float>(cl.time + 0.01);
        } else if (ent->model->flags & EF_GRENADE) {
            R_RocketTrail(oldorg, ent->origin, 1);
        } else if (ent->model->flags & EF_TRACER3) {
            R_RocketTrail(oldorg, ent->origin, 6);
        }

        ent->forcelink = false;

        if (i == cl.viewentity && !chase_active.value) {
            continue;
        }

        if (cl_numvisedicts < MAX_VISEDICTS) {
            cl_visedicts[cl_numvisedicts] = ent;
            cl_numvisedicts++;
        }
    }
}

/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer(void)
{
    int ret;

    cl.oldtime = cl.time;
    cl.time += host_frametime;

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
void CL_SendCmd(void)
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
void CL_Init(void)
{
    SZ_Alloc(&cls.message, 1024);

    CL_InitInput();
    CL_InitTEnts();

    //
    // register our commands
    //
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

    //	Cvar::Register (&cl_autofire);

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
    std::string_view c;

    c = Cmd::Argv(1);
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
    std::string_view c;

    c = Cmd::Argv(1);
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
    float val;
    qboolean impulsedown, impulseup, down;

    impulsedown = key->state & 2;
    impulseup = key->state & 4;
    down = key->state & 1;
    val = 0;

    if (impulsedown && !impulseup) {
        if (down) {
            val = 0.5; // pressed and held this frame
        } else {
            val = 0; //	I_Error ();
        }
    }

    if (impulseup && !impulsedown) {
        if (down) {
            val = 0; //	I_Error ();
        } else {
            val = 0; // released this frame
        }
    }

    if (!impulsedown && !impulseup) {
        if (down) {
            val = 1.0; // held the entire frame
        } else {
            val = 0; // up the entire frame
        }
    }

    if (impulsedown && impulseup) {
        if (down) {
            val = 0.75; // released and re-pressed this frame
        } else {
            val = 0.25; // pressed and released this frame
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
void CL_AdjustAngles(void)
{
    float speed;
    float up, down;

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

    up = CL_KeyState(&in_lookup);
    down = CL_KeyState(&in_lookdown);

    cl.viewangles[PITCH] -= speed * cl_pitchspeed.value * up;
    cl.viewangles[PITCH] += speed * cl_pitchspeed.value * down;

    if (up || down) {
        V_StopPitchDrift();
    }

    if (cl.viewangles[PITCH] > 80) {
        cl.viewangles[PITCH] = 80;
    }

    if (cl.viewangles[PITCH] < -70) {
        cl.viewangles[PITCH] = -70;
    }

    if (cl.viewangles[ROLL] > 50) {
        cl.viewangles[ROLL] = 50;
    }

    if (cl.viewangles[ROLL] < -50) {
        cl.viewangles[ROLL] = -50;
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

    Q_memset(cmd, 0, sizeof(*cmd));

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

    //
    // adjust for speed key
    //
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
    int i;
    int bits;
    sizebuf_t buf;
    byte data[128];

    buf.maxsize = 128;
    buf.cursize = 0;
    buf.data = data;

    cl.cmd = *cmd;

    //
    // send the movement message
    //
    MSG_WriteByte(&buf, clc_move);

    MSG_WriteFloat(&buf, static_cast<float>(cl.mtime[0])); // so server can get ping times

    for (i = 0; i < 3; i++) {
        MSG_WriteAngle(&buf, cl.viewangles[i]);
    }

    MSG_WriteShort(&buf, static_cast<int>(cmd->forwardmove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->sidemove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->upmove));

    //
    // send button bits
    //
    bits = 0;

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


    //
    // deliver the message
    //
    if (cls.demoplayback) {
        return;
    }

    //
    // allways dump the first two message, because it may contain leftover inputs
    // from the last level
    //
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
void CL_InitInput(void)
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

void CL_FinishTimeDemo(void);

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback(void)
{
    if (!cls.demoplayback) {
        return;
    }

    fclose(cls.demofile);
    cls.demoplayback = false;
    cls.demofile = NULL;
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
void CL_WriteDemoMessage(void)
{
    int len;
    int i;
    float f;

    len = LittleLong(net_message.cursize);
    fwrite(&len, 4, 1, cls.demofile);
    for (i = 0; i < 3; i++) {
        f = LittleFloat(cl.viewangles[i]);
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
int CL_GetMessage(void)
{
    int r, i;
    float f;

    if (cls.demoplayback) {
        // decide if it is time to grab the next message
        if (cls.signon == SIGNONS) // allways grab until fully connected
        {
            if (cls.timedemo) {
                if (host_framecount == cls.td_lastframe) {
                    return 0; // allready read this frame's message
                }

                cls.td_lastframe = host_framecount;
                // if this is the second frame, grab the real td_starttime
                // so the bogus time on the first frame doesn't count
                if (host_framecount == cls.td_startframe + 1) {
                    cls.td_starttime = static_cast<float>(realtime);
                }
            } else if (/* cl.time > 0 && */ cl.time <= cl.mtime[0]) {
                return 0; // don't need another message yet
            }
        }

        // get the next message
        fread(&net_message.cursize, 4, 1, cls.demofile);
        VectorCopy(cl.mviewangles[0], cl.mviewangles[1]);
        for (i = 0; i < 3; i++) {
            fread(&f, 4, 1, cls.demofile);
            cl.mviewangles[0][i] = LittleFloat(f);
        }

        net_message.cursize = LittleLong(net_message.cursize);
        if (net_message.cursize > MAX_MSGLEN) {
            Sys_Error("Demo message > MAX_MSGLEN");
        }

        r = (int)fread(net_message.data, net_message.cursize, 1, cls.demofile);
        if (r != 1) {
            CL_StopPlayback();

            return 0;
        }

        return 1;
    }

    while (1) {
        r = NET_GetMessage(cls.netcon);

        if (r != 1 && r != 2) {
            return r;
        }

        // discard nop keepalive message
        if (net_message.cursize == 1 && net_message.data[0] == svc_nop) {
            Con_Printf("<-- server to client keepalive\n");
        } else {
            break;
        }
    }

    if (cls.demorecording) {
        CL_WriteDemoMessage();
    }

    return r;
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f(void)
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
    cls.demofile = NULL;
    cls.demorecording = false;
    Con_Printf("Completed demo\n");
}

/*
====================
CL_Record_f

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f(void)
{
    int c;
    char name[MAX_OSPATH];
    int track;

    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    c = Cmd::Argc();
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
    if (c == 4) {
        track = Q_atoi(Cmd::Argv(3));
        Con_Printf("Forcing CD track to %i\n", cls.forcetrack);
    } else {
        track = -1;
    }

    sprintf_s(name, sizeof(name), "%s/%s", com_gamedir, std::string(Cmd::Argv(1)).c_str());

    //
    // start the map up
    //
    if (c > 2) {
        Cmd::ExecuteString(va("map %s", std::string(Cmd::Argv(2)).c_str()), Cmd::Source::Command);
    }

    //
    // open the demo file
    //
    COM_DefaultExtension(name, ".dem");

    Con_Printf("recording to %s.\n", name);
    fopen_s(&cls.demofile, name, "wb");
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
void CL_PlayDemo_f(void)
{
    char name[256];
    int c;
    qboolean neg = false;

    if (Cmd::state.source != Cmd::Source::Command) {
        return;
    }

    if (Cmd::Argc() != 2) {
        Con_Printf("play <demoname> : plays a demo\n");

        return;
    }

    //
    // disconnect from server
    //
    CL_Disconnect();

    //
    // open the demo file
    //
    Q_strcpy(name, Cmd::Argv(1));
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
void CL_FinishTimeDemo(void)
{
    int frames;
    float time;

    cls.timedemo = false;

    // the first frame didn't count
    frames = (host_framecount - cls.td_startframe) - 1;
    time = static_cast<float>(realtime - cls.td_starttime);
    if (!time) {
        time = 1;
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
void CL_TimeDemo_f(void)
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

//=============================================================================

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
void CL_ParseStartSoundPacket(void)
{
    Vector3 pos;
    int channel, ent;
    int sound_num;
    int packet_vol;
    int field_mask;
    float attenuation;

    field_mask = MSG_ReadByte();

    if (field_mask & SND_VOLUME) {
        packet_vol = MSG_ReadByte();
    } else {
        packet_vol = DEFAULT_SOUND_PACKET_VOLUME;
    }

    if (field_mask & SND_ATTENUATION) {
        attenuation = MSG_ReadByte() / 64.0f;
    } else {
        attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;
    }

    channel = MSG_ReadShort();
    sound_num = MSG_ReadByte();

    ent = channel >> 3;
    channel &= 7;

    if (ent > MAX_EDICTS) {
        Host_Error("CL_ParseStartSoundPacket: ent = %i", ent);
    }

    pos.x = MSG_ReadCoord();
    pos.y = MSG_ReadCoord();
    pos.z = MSG_ReadCoord();

    S_StartSound(ent, channel, cl.sound_precache[sound_num], pos, packet_vol / 255.0f, attenuation);
}

/*
==================
CL_KeepaliveMessage

When the client is taking a long time to load stuff, send keepalive messages
so the server doesn't disconnect.
==================
*/
void CL_KeepaliveMessage(void)
{
    float time;
    static float lastmsg;
    int ret;
    sizebuf_t old;
    byte olddata[8192];

    if (sv.active) {
        return; // no need if server is local
    }

    if (cls.demoplayback) {
        return;
    }

    // read messages from server, should just be nops
    old = net_message;
    memcpy(olddata, net_message.data, net_message.cursize);

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
    memcpy(net_message.data, olddata, net_message.cursize);

    // check time
    time = static_cast<float>(Sys_FloatTime());
    if (time - lastmsg < 5) {
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
void CL_ParseServerInfo(void)
{
    char* str;
    int i;
    int nummodels, numsounds;
    char model_precache[MAX_MODELS][MAX_QPATH];
    char sound_precache[MAX_SOUNDS][MAX_QPATH];

    Con_DPrintf("Serverinfo packet received.\n");
    //
    // wipe the client_state_t struct
    //
    CL_ClearState();

    // parse protocol version number
    i = MSG_ReadLong();
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

    cl.scores = (scoreboard_t *) Hunk_Alloc(cl.maxclients * sizeof(*cl.scores), "scores");

    // parse gametype
    cl.gametype = MSG_ReadByte();

    // parse signon message
    str = MSG_ReadString();
    strncpy_s(cl.levelname, sizeof(cl.levelname), str, _TRUNCATE);

    // seperate the printfs so the server message can have a color
    Con_Printf(
        "\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
        "\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
    Con_Printf("%c%s\n", 2, str);

    //
    // first we go through and touch all of the precache data that still
    // happens to be in the cache, so precaching something else doesn't
    // needlessly purge it
    //

    // precache models
    memset(cl.model_precache, 0, sizeof(cl.model_precache));
    for (nummodels = 1;; nummodels++) {
        str = MSG_ReadString();
        if (!str[0]) {
            break;
        }

        if (nummodels == MAX_MODELS) {
            Con_Printf("Server sent too many model precaches\n");

            return;
        }

        strcpy_s(model_precache[nummodels], sizeof(model_precache[nummodels]), str);
        Mod_TouchModel(str);
    }

    // precache sounds
    memset(cl.sound_precache, 0, sizeof(cl.sound_precache));
    for (numsounds = 1;; numsounds++) {
        str = MSG_ReadString();
        if (!str[0]) {
            break;
        }

        if (numsounds == MAX_SOUNDS) {
            Con_Printf("Server sent too many sound precaches\n");

            return;
        }

        strcpy_s(sound_precache[numsounds], sizeof(sound_precache[numsounds]), str);
        S_TouchSound(str);
    }

    //
    // now we try to load everything else until a cache allocation fails
    //

    for (i = 1; i < nummodels; i++) {
        cl.model_precache[i] = Mod_ForName(model_precache[i], false);
        if (cl.model_precache[i] == NULL) {
            Con_Printf("Model %s not found\n", model_precache[i]);

            return;
        }

        CL_KeepaliveMessage();
    }

    S_BeginPrecaching();
    for (i = 1; i < numsounds; i++) {
        cl.sound_precache[i] = S_PrecacheSound(sound_precache[i]);
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
    int i;
    model_t* model;
    int modnum;
    qboolean forcelink;
    entity_t* ent;
    int num;

    if (cls.signon == SIGNONS - 1) { // first update is the final signon stage
        cls.signon = SIGNONS;
        CL_SignonReply();
    }

    if (bits & U_MOREBITS) {
        i = MSG_ReadByte();
        bits |= (i << 8);
    }

    if (bits & U_LONGENTITY) {
        num = MSG_ReadShort();
    } else {
        num = MSG_ReadByte();
    }

    ent = CL_EntityNum(num);

    for (i = 0; i < 16; i++) {
        if (bits & (1 << i)) {
            bitcounts[i]++;
        }
    }

    if (ent->msgtime != cl.mtime[1]) {
        forcelink = true; // no previous frame to lerp from
    } else {
        forcelink = false;
    }

    ent->msgtime = cl.mtime[0];

    if (bits & U_MODEL) {
        modnum = MSG_ReadByte();
        if (modnum >= MAX_MODELS) {
            Host_Error("CL_ParseModel: bad modnum");
        }
    } else {
        modnum = ent->baseline.modelindex;
    }

    model = cl.model_precache[modnum];
    if (model != ent->model) {
        ent->model = model;
        // automatic animation (torches, etc) can be either all together
        // or randomized
        if (model) {
            if (model->synctype == ST_RAND) {
                ent->syncbase = (float)(rand() & 0x7fff) / 0x7fff;
            } else {
                ent->syncbase = 0.0;
            }
        } else {
            forcelink = true; // hack to make null model players work
        }


    }

    if (bits & U_FRAME) {
        ent->frame = MSG_ReadByte();
    } else {
        ent->frame = ent->baseline.frame;
    }

    if (bits & U_COLORMAP) {
        i = MSG_ReadByte();
    } else {
        i = ent->baseline.colormap;
    }

    if (!i) {
        ent->colormap = vid.colormap;
    } else {
        if (i > cl.maxclients) {
            Sys_Error("i >= cl.maxclients");
        }

        ent->colormap = cl.scores[i - 1].translations;
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
    VectorCopy(ent->msg_origins[0], ent->msg_origins[1]);
    VectorCopy(ent->msg_angles[0], ent->msg_angles[1]);

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

    if (forcelink) { // didn't have an update last message
        VectorCopy(ent->msg_origins[0], ent->msg_origins[1]);
        VectorCopy(ent->msg_origins[0], ent->origin);
        VectorCopy(ent->msg_angles[0], ent->msg_angles[1]);
        VectorCopy(ent->msg_angles[0], ent->angles);
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
    int i;

    ent->baseline.modelindex = MSG_ReadByte();
    ent->baseline.frame = MSG_ReadByte();
    ent->baseline.colormap = MSG_ReadByte();
    ent->baseline.skin = MSG_ReadByte();
    for (i = 0; i < 3; i++) {
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
    int i, j;

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

    VectorCopy(cl.mvelocity[0], cl.mvelocity[1]);
    for (i = 0; i < 3; i++) {
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

    // [always sent]	if (bits & SU_ITEMS)
    i = MSG_ReadLong();

    if (cl.items != i) { // set flash times
        Sbar_Changed();
        for (j = 0; j < 32; j++) {
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

    if (bits & SU_ARMOR) {
        i = MSG_ReadByte();
    } else {
        i = 0;
    }

    if (cl.stats[STAT_ARMOR] != i) {
        cl.stats[STAT_ARMOR] = i;
        Sbar_Changed();
    }

    if (bits & SU_WEAPON) {
        i = MSG_ReadByte();
    } else {
        i = 0;
    }

    if (cl.stats[STAT_WEAPON] != i) {
        cl.stats[STAT_WEAPON] = i;
        Sbar_Changed();
    }

    i = MSG_ReadShort();
    if (cl.stats[STAT_HEALTH] != i) {
        cl.stats[STAT_HEALTH] = i;
        Sbar_Changed();
    }

    i = MSG_ReadByte();
    if (cl.stats[STAT_AMMO] != i) {
        cl.stats[STAT_AMMO] = i;
        Sbar_Changed();
    }

    for (i = 0; i < 4; i++) {
        j = MSG_ReadByte();
        if (cl.stats[STAT_SHELLS + i] != j) {
            cl.stats[STAT_SHELLS + i] = j;
            Sbar_Changed();
        }
    }

    i = MSG_ReadByte();

    if (standard_quake) {
        if (cl.stats[STAT_ACTIVEWEAPON] != i) {
            cl.stats[STAT_ACTIVEWEAPON] = i;
            Sbar_Changed();
        }
    } else {
        if (cl.stats[STAT_ACTIVEWEAPON] != (1 << i)) {
            cl.stats[STAT_ACTIVEWEAPON] = (1 << i);
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
    int i, j;
    int top, bottom;
    byte *dest, *source;

    if (slot > cl.maxclients) {
        Sys_Error("CL_NewTranslation: slot > cl.maxclients");
    }

    dest = cl.scores[slot].translations;
    source = vid.colormap;
    memcpy(dest, vid.colormap, sizeof(cl.scores[slot].translations));
    top = cl.scores[slot].colors & 0xf0;
    bottom = (cl.scores[slot].colors & 15) << 4;


    for (i = 0; i < VID_GRADES; i++, dest += 256, source += 256) {
        if (top < 128) { // the artists made some backwards ranges.  sigh.
            memcpy(dest + TOP_RANGE, source + top, 16);
        } else {
            for (j = 0; j < 16; j++) {
                dest[TOP_RANGE + j] = source[top + 15 - j];
            }
        }

        if (bottom < 128) {
            memcpy(dest + BOTTOM_RANGE, source + bottom, 16);
        } else {
            for (j = 0; j < 16; j++) {
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
void CL_ParseStatic(void)
{
    entity_t* ent;
    int i;

    i = cl.num_statics;
    if (i >= MAX_STATIC_ENTITIES) {
        Host_Error("Too many static entities");
    }

    ent = &cl_static_entities[i];
    cl.num_statics++;
    CL_ParseBaseline(ent);

    // copy it to the current state
    ent->model = cl.model_precache[ent->baseline.modelindex];
    ent->frame = ent->baseline.frame;
    ent->colormap = vid.colormap;
    ent->skinnum = ent->baseline.skin;
    ent->effects = ent->baseline.effects;

    VectorCopy(ent->baseline.origin, ent->origin);
    VectorCopy(ent->baseline.angles, ent->angles);
    R_AddEfrags(ent);
}

/*
===================
CL_ParseStaticSound
===================
*/
void CL_ParseStaticSound(void)
{
    Vector3 org;
    int sound_num, vol, atten;

    org.x = MSG_ReadCoord();
    org.y = MSG_ReadCoord();
    org.z = MSG_ReadCoord();

    sound_num = MSG_ReadByte();
    vol = MSG_ReadByte();
    atten = MSG_ReadByte();

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
void CL_ParseServerMessage(void)
{
    int cmd;
    int i;

    //
    // if recording demos, copy the message out
    //
    if (cl_shownet.value == 1) {
        Con_Printf("%i ", net_message.cursize);
    } else if (cl_shownet.value == 2) {
        Con_Printf("------------------\n");
    }

    cl.onground = false; // unless the server says otherwise
                         //
                         // parse the message
                         //
    MSG_BeginReading();

    while (1) {
        if (msg_badread) {
            Host_Error("CL_ParseServerMessage: Bad server message");
        }

        cmd = MSG_ReadByte();

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
            //			Con_Printf ("svc_nop\n");
            break;

        case svc_time:
            cl.mtime[1] = cl.mtime[0];
            cl.mtime[0] = MSG_ReadFloat();
            break;

        case svc_clientdata:
            i = MSG_ReadShort();
            CL_ParseClientdata(i);
            break;

        case svc_version:
            i = MSG_ReadLong();
            if (i != PROTOCOL_VERSION) {
                Host_Error(
                    "CL_ParseServerMessage: Server is protocol %i instead of %i\n", i,
                    PROTOCOL_VERSION);
            }

            break;

        case svc_disconnect:
            Host_EndGame("Server disconnected\n");

        case svc_print:
            Con_Printf("%s", MSG_ReadString());
            break;

        case svc_centerprint:
            SCR_CenterPrint(MSG_ReadString());
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
            for (i = 0; i < 3; i++) {
                cl.viewangles[i] = MSG_ReadAngle();
            }
            break;

        case svc_setview:
            cl.viewentity = MSG_ReadShort();
            break;

        case svc_lightstyle:
            i = MSG_ReadByte();
            if (i >= MAX_LIGHTSTYLES) {
                Sys_Error("svc_lightstyle > MAX_LIGHTSTYLES");
            }

            Q_strcpy(cl_lightstyle[i].map, MSG_ReadString());
            cl_lightstyle[i].length = Q_strlen(cl_lightstyle[i].map);
            break;

        case svc_sound:
            CL_ParseStartSoundPacket();
            break;

        case svc_stopsound:
            i = MSG_ReadShort();
            S_StopSound(i >> 3, i & 7);
            break;

        case svc_updatename:
            Sbar_Changed();
            i = MSG_ReadByte();
            if (i >= cl.maxclients) {
                Host_Error("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
            }

            strcpy_s(cl.scores[i].name, sizeof(cl.scores[i].name), MSG_ReadString());
            break;

        case svc_updatefrags:
            Sbar_Changed();
            i = MSG_ReadByte();
            if (i >= cl.maxclients) {
                Host_Error("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
            }

            cl.scores[i].frags = MSG_ReadShort();
            break;

        case svc_updatecolors:
            Sbar_Changed();
            i = MSG_ReadByte();
            if (i >= cl.maxclients) {
                Host_Error(
                    "CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
            }

            cl.scores[i].colors = MSG_ReadByte();
            CL_NewTranslation(i);
            break;

        case svc_particle:
            R_ParseParticleEffect();
            break;

        case svc_spawnbaseline:
            i = MSG_ReadShort();
            // must use CL_EntityNum() to force cl.num_entities up
            CL_ParseBaseline(CL_EntityNum(i));
            break;
        case svc_spawnstatic:
            CL_ParseStatic();
            break;
        case svc_temp_entity:
            CL_ParseTEnt();
            break;

        case svc_setpause: {
            cl.paused = MSG_ReadByte();

            if (cl.paused) {
                CDAudio_Pause();
                VID_HandlePause();
            } else {
                CDAudio_Resume();
                VID_HandlePause();
            }
        } break;

        case svc_signonnum:
            i = MSG_ReadByte();
            if (i <= cls.signon) {
                Host_Error("Received signon %i when at %i", i, cls.signon);
            }

            cls.signon = i;
            CL_SignonReply();
            break;

        case svc_killedmonster:
            cl.stats[STAT_MONSTERS]++;
            break;

        case svc_foundsecret:
            cl.stats[STAT_SECRETS]++;
            break;

        case svc_updatestat:
            i = MSG_ReadByte();
            if (i < 0 || i >= MAX_CL_STATS) {
                Sys_Error("svc_updatestat: %i is invalid", i);
            }

            cl.stats[i] = MSG_ReadLong();
            ;
            break;

        case svc_spawnstaticsound:
            CL_ParseStaticSound();
            break;

        case svc_cdtrack:
            cl.cdtrack = MSG_ReadByte();
            cl.looptrack = MSG_ReadByte();
            if ((cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1)) {
                CDAudio_Play((byte)cls.forcetrack, true);
            } else {
                CDAudio_Play((byte)cl.cdtrack, true);
            }

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
            SCR_CenterPrint(MSG_ReadString());
            break;

        case svc_cutscene:
            cl.intermission = 3;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true; // go to full screen
            SCR_CenterPrint(MSG_ReadString());
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
void CL_InitTEnts(void)
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
    int ent;
    Vector3 start, end;
    beam_t* b;
    int i;

    ent = MSG_ReadShort();

    start.x = MSG_ReadCoord();
    start.y = MSG_ReadCoord();
    start.z = MSG_ReadCoord();

    end.x = MSG_ReadCoord();
    end.y = MSG_ReadCoord();
    end.z = MSG_ReadCoord();

    // override any beam with the same entity
    for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++) {
        if (b->entity == ent) {
            b->entity = ent;
            b->model = m;
            b->endtime = static_cast<float>(cl.time + 0.2);
            b->start = start;
            b->end = end;

            return;
        }
    }

    // find a free beam
    for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++) {
        if (!b->model || b->endtime < cl.time) {
            b->entity = ent;
            b->model = m;
            b->endtime = static_cast<float>(cl.time + 0.2);
            b->start = start;
            b->end = end;

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
void CL_ParseTEnt(void)
{
    int type;
    Vector3 pos;
    dlight_t* dl;
    int rnd;
    int colorStart, colorLength;

    type = MSG_ReadByte();
    switch (type) {
    case TE_WIZSPIKE: // spike hitting wall
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_RunParticleEffect(pos, vec3_origin, 20, 30);
        S_StartSound(-1, 0, cl_sfx_wizhit, pos, 1, 1);
        break;

    case TE_KNIGHTSPIKE: // spike hitting wall
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_RunParticleEffect(pos, vec3_origin, 226, 20);
        S_StartSound(-1, 0, cl_sfx_knighthit, pos, 1, 1);
        break;

    case TE_SPIKE: // spike hitting wall
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_RunParticleEffect(pos, vec3_origin, 0, 10);
        if (rand() % 5) {
            S_StartSound(-1, 0, cl_sfx_tink1, pos, 1, 1);
        } else {
            rnd = rand() & 3;
            if (rnd == 1) {
                S_StartSound(-1, 0, cl_sfx_ric1, pos, 1, 1);
            } else if (rnd == 2) {
                S_StartSound(-1, 0, cl_sfx_ric2, pos, 1, 1);
            } else {
                S_StartSound(-1, 0, cl_sfx_ric3, pos, 1, 1);
            }
        }

        break;
    case TE_SUPERSPIKE: // super spike hitting wall
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_RunParticleEffect(pos, vec3_origin, 0, 20);

        if (rand() % 5) {
            S_StartSound(-1, 0, cl_sfx_tink1, pos, 1, 1);
        } else {
            rnd = rand() & 3;
            if (rnd == 1) {
                S_StartSound(-1, 0, cl_sfx_ric1, pos, 1, 1);
            } else if (rnd == 2) {
                S_StartSound(-1, 0, cl_sfx_ric2, pos, 1, 1);
            } else {
                S_StartSound(-1, 0, cl_sfx_ric3, pos, 1, 1);
            }
        }

        break;

    case TE_GUNSHOT: // bullet hitting wall
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_RunParticleEffect(pos, vec3_origin, 0, 20);
        break;

    case TE_EXPLOSION: // rocket explosion
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_ParticleExplosion(pos);
        dl = CL_AllocDlight(0);
        dl->origin = pos;
        dl->radius = 350.0f;
        dl->die = static_cast<float>(cl.time + 0.5);
        dl->decay = 300.0f;
        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;

    case TE_TAREXPLOSION: // tarbaby explosion
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_BlobExplosion(pos);

        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;

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

    case TE_LAVASPLASH:
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_LavaSplash(pos);
        break;

    case TE_TELEPORT:
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        R_TeleportSplash(pos);
        break;

    case TE_EXPLOSION2: // color mapped explosion
        pos.x = MSG_ReadCoord();
        pos.y = MSG_ReadCoord();
        pos.z = MSG_ReadCoord();
        colorStart = MSG_ReadByte();
        colorLength = MSG_ReadByte();
        R_ParticleExplosion2(pos, colorStart, colorLength);
        dl = CL_AllocDlight(0);
        dl->origin = pos;
        dl->radius = 350.0f;
        dl->die = static_cast<float>(cl.time + 0.5);
        dl->decay = 300.0f;
        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;


    default:
        Sys_Error("CL_ParseTEnt: bad type");
    }
}

/*
=================
CL_NewTempEntity
=================
*/
entity_t* CL_NewTempEntity(void)
{
    entity_t* ent;

    if (cl_numvisedicts == MAX_VISEDICTS) {
        return NULL;
    }

    if (num_temp_entities == MAX_TEMP_ENTITIES) {
        return NULL;
    }

    ent = &cl_temp_entities[num_temp_entities];
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
void CL_UpdateTEnts(void)
{
    int i;
    beam_t* b;
    Vector3 dist, org;
    float d;
    entity_t* ent;
    float yaw, pitch;
    float forward;

    num_temp_entities = 0;

    // update lightning
    for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++) {
        if (!b->model || b->endtime < cl.time) {
            continue;
        }

        // if coming from the player, update the start position
        if (b->entity == cl.viewentity) {
            b->start = cl_entities[cl.viewentity].origin;
        }

        // calculate pitch and yaw
        dist = b->end - b->start;

        if (dist.y == 0 && dist.x == 0) {
            yaw = 0;
            if (dist.z > 0) {
                pitch = 90;
            } else {
                pitch = 270;
            }
        } else {
            yaw = static_cast<float>(atan2(dist.y, dist.x) * 180 / M_PI);
            if (yaw < 0) {
                yaw += 360;
            }

            forward = sqrt(dist.x * dist.x + dist.y * dist.y);
            pitch = static_cast<float>(atan2(dist.z, forward) * 180 / M_PI);
            if (pitch < 0) {
                pitch += 360;
            }
        }

        // add new entities for the lightning
        org = b->start;
        d = dist.normalize();
        while (d > 0) {
            ent = CL_NewTempEntity();
            if (!ent) {
                return;
            }

            ent->origin = org;
            ent->model = b->model;
            ent->angles[0] = pitch;
            ent->angles[1] = yaw;
            ent->angles[2] = static_cast<float>(rand() % 360);

            org += dist * 30;
            d -= 30;
        }
    }
}

} // namespace Client
