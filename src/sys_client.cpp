// sys_client.cpp -- Subsystem Client Implementation
// Contains: client state, host frame loop, console commands, key bindings, console text buffer, menu system, chase camera

#include "quakedef.hpp"
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/algorithm.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <EASTL/numeric_limits.h>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <numbers>

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

extern int vcrFile;

namespace Client {

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

constexpr auto svc_strings = std::array{
    "svc_bad", "svc_nop", "svc_disconnect", "svc_updatestat",
    "svc_version", "svc_setview", "svc_sound", "svc_time", "svc_print",
    "svc_stufftext", "svc_setangle", "svc_serverinfo", "svc_lightstyle",
    "svc_updatename", "svc_updatefrags", "svc_clientdata", "svc_stopsound",
    "svc_updatecolors", "svc_particle", "svc_damage", "svc_spawnstatic",
    "OBSOLETE svc_spawnbinary", "svc_spawnbaseline", "svc_temp_entity",
    "svc_setpause", "svc_signonnum", "svc_centerprint", "svc_killedmonster",
    "svc_foundsecret", "svc_spawnstaticsound", "svc_intermission",
    "svc_finale", "svc_cdtrack", "svc_sellscreen", "svc_cutscene"
};
std::array<int, 16> bitcounts{};

int num_temp_entities;
sfx_t* cl_sfx_wizhit;
sfx_t* cl_sfx_knighthit;
sfx_t* cl_sfx_tink1;
sfx_t* cl_sfx_ric1;
sfx_t* cl_sfx_ric2;
sfx_t* cl_sfx_ric3;
sfx_t* cl_sfx_r_exp3;

void CL_ClearState()
{
    if (!sv.active) Host_ClearMemory();
    cl = {};
    SZ_Clear(&cls.message);
    cl_efrags.fill({});
    cl_entities.fill({});
    cl_static_entities.fill({});
    cl_lightstyle.fill({});
    cl_temp_entities.fill({});
    cl_beams.fill({});
    cl_dlights.fill({});

    cl.free_efrags = cl_efrags.data();
    size_t i;
    for (i = 0; i < MAX_EFRAGS - 1; ++i) {
        cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];
    }
    cl.free_efrags[i].entnext = nullptr;
}

void CL_Disconnect()
{
    S_StopAllSounds(true);
    if (cls.demoplayback) {
        CL_StopPlayback();
    } else if (cls.state == ca_connected) {
        if (cls.demorecording) CL_Stop_f();
        Con_DPrintf("Sending clc_disconnect\n");
        SZ_Clear(&cls.message);
        MSG_WriteByte(&cls.message, clc_disconnect);
        NET_SendUnreliableMessage(cls.netcon, &cls.message);
        SZ_Clear(&cls.message);
        NET_Close(cls.netcon);
        cls.state = ca_disconnected;
        if (sv.active) Host_ShutdownServer(false);
    }

    cls.demoplayback = cls.timedemo = false;
    cls.signon = 0;
}

void CL_Disconnect_f()
{
    CL_Disconnect();
    if (sv.active) Host_ShutdownServer(false);
}

void CL_EstablishConnection(const char* host)
{
    if (cls.state == ca_dedicated || cls.demoplayback) return;
    CL_Disconnect();
    cls.netcon = NET_Connect(host);
    if (!cls.netcon) Host_Error("CL_Connect: connect failed\n");
    Con_DPrintf("CL_EstablishConnection: connected to %s\n", host);
    cls.demonum = -1;
    cls.state = ca_connected;
    cls.signon = 0;
}

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
        MSG_WriteString(&cls.message, va("color %i %i\n", static_cast<int>(cl_color.value) >> 4, static_cast<int>(cl_color.value) & 15));
        MSG_WriteByte(&cls.message, clc_stringcmd);
        std::string spawnCmd = "spawn " + std::string(cls.spawnparms);
        MSG_WriteString(&cls.message, spawnCmd.c_str());
        break;
    }
    case 3:
        MSG_WriteByte(&cls.message, clc_stringcmd);
        MSG_WriteString(&cls.message, "begin");
        Cache_Report();
        break;
    case 4:
        Screen::GetScreenSystem().EndLoadingPlaque();
        break;
    }
}

void CL_NextDemo()
{
    if (cls.demonum == -1) return;
    Screen::GetScreenSystem().BeginLoadingPlaque();

    if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS) {
        cls.demonum = 0;
        if (!cls.demos[cls.demonum][0]) {
            Con_Printf("No demos listed with startdemos\n");
            cls.demonum = -1;
            return;
        }
    }

    eastl::string demoCmd = "playdemo ";
    demoCmd += cls.demos[cls.demonum];
    demoCmd += "\n";
    Cmd::BufferInsertText(demoCmd.c_str());
    cls.demonum++;
}

void CL_PrintEntities_f()
{
    int i = 0;
    for (const auto& ent : eastl::span(cl_entities.data(), cl.num_entities)) {
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

dlight_t* CL_AllocDlight(int key)
{
    if (key) {
        for (auto& dl : cl_dlights) {
            if (dl.key == key) {
                dl = {};
                dl.key = key;
                return &dl;
            }
        }
    }

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

void CL_DecayLights()
{
    const float time = static_cast<float>(cl.time - cl.oldtime);
    for (auto& dl : cl_dlights) {
        if (dl.die < cl.time || !dl.radius) continue;
        dl.radius -= time * dl.decay;
        if (dl.radius < 0.0f) dl.radius = 0.0f;
    }
}

float CL_LerpPoint()
{
    float f = static_cast<float>(cl.mtime[0] - cl.mtime[1]);
    if (!f || cl_nolerp.value || cls.timedemo || sv.active) {
        cl.time = cl.mtime[0];
        return 1.0f;
    }

    if (f > 0.1f) {
        cl.mtime[1] = cl.mtime[0] - 0.1;
        f = 0.1f;
    }

    float frac = static_cast<float>((cl.time - cl.mtime[1]) / f);
    if (frac < 0.0f) {
        if (frac < -0.01f) cl.time = cl.mtime[1];
        frac = 0.0f;
    } else if (frac > 1.0f) {
        if (frac > 1.01f) cl.time = cl.mtime[0];
        frac = 1.0f;
    }

    return frac;
}

void CL_RelinkEntities()
{
    const float frac = CL_LerpPoint();
    cl_numvisedicts = 0;
    cl.velocity = cl.mvelocity[1] + (cl.mvelocity[0] - cl.mvelocity[1]) * frac;

    if (cls.demoplayback) {
        for (int j = 0; j < 3; ++j) {
            float d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
            if (d > 180.0f) d -= 360.0f;
            else if (d < -180.0f) d += 360.0f;
            cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
        }
    }

    const float bobjrotate = anglemod(static_cast<float>(100.0 * cl.time));

    if (cl.num_entities > 1) {
        int i = 1;
        for (auto& ent : eastl::span(cl_entities.data() + 1, cl.num_entities - 1)) {
            if (!ent.model) {
                if (ent.forcelink) R_RemoveEfrags(&ent);
                ++i;
                continue;
            }

            if (ent.msgtime != cl.mtime[0]) {
                ent.model = nullptr;
                ++i;
                continue;
            }

            const Vector3 oldorg = ent.origin;

            if (ent.forcelink) {
                ent.origin = ent.msg_origins[0];
                ent.angles = ent.msg_angles[0];
            } else {
                float f = frac;
                Vector3 delta = ent.msg_origins[0] - ent.msg_origins[1];
                for (int j = 0; j < 3; ++j) {
                    if (delta[j] > 100.0f || delta[j] < -100.0f) f = 1.0f;
                }

                ent.origin = ent.msg_origins[1] + delta * f;
                for (int j = 0; j < 3; ++j) {
                    float d = ent.msg_angles[0][j] - ent.msg_angles[1][j];
                    if (d > 180.0f) d -= 360.0f;
                    else if (d < -180.0f) d += 360.0f;
                    ent.angles[j] = ent.msg_angles[1][j] + f * d;
                }
            }

            if (ent.model->flags & EF_ROTATE) ent.angles[1] = bobjrotate;
            if (ent.effects & EF_BRIGHTFIELD) R_EntityParticles(&ent);

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

            if (ent.model->flags & EF_GIB) R_RocketTrail(oldorg, ent.origin, 2);
            else if (ent.model->flags & EF_ZOMGIB) R_RocketTrail(oldorg, ent.origin, 4);
            else if (ent.model->flags & EF_TRACER) R_RocketTrail(oldorg, ent.origin, 3);
            else if (ent.model->flags & EF_TRACER2) R_RocketTrail(oldorg, ent.origin, 5);
            else if (ent.model->flags & EF_ROCKET) {
                R_RocketTrail(oldorg, ent.origin, 0);
                if (auto* dl = CL_AllocDlight(i)) {
                    dl->origin = ent.origin;
                    dl->radius = 200.0f;
                    dl->die = static_cast<float>(cl.time + 0.01);
                }
            } else if (ent.model->flags & EF_GRENADE) R_RocketTrail(oldorg, ent.origin, 1);
            else if (ent.model->flags & EF_TRACER3) R_RocketTrail(oldorg, ent.origin, 6);

            ent.forcelink = false;

            if (i != cl.viewentity || chase_active.value) {
                if (cl_numvisedicts < MAX_VISEDICTS) {
                    cl_visedicts[cl_numvisedicts++] = &ent;
                }
            }
            ++i;
        }
    }
}

int CL_ReadFromServer()
{
    cl.oldtime = cl.time;
    cl.time += host_frametime;

    int ret;
    do {
        ret = CL_GetMessage();
        if (ret == -1) Host_Error("CL_ReadFromServer: lost server connection");
        if (!ret) break;

        cl.last_received_message = static_cast<float>(realtime);
        CL_ParseServerMessage();
    } while (ret && cls.state == ca_connected);

    if (cl_shownet.value) Con_Printf("\n");

    CL_RelinkEntities();
    CL_UpdateTEnts();

    return 0;
}

void CL_SendCmd()
{
    usercmd_t cmd;
    if (cls.state != ca_connected) return;

    if (cls.signon == SIGNONS) {
        CL_BaseMove(&cmd);
        IN_Move(&cmd);
        CL_SendMove(&cmd);
    }

    if (cls.demoplayback) {
        SZ_Clear(&cls.message);
        return;
    }

    if (!cls.message.cursize) return;

    if (!NET_CanSendMessage(cls.netcon)) {
        Con_DPrintf("CL_WriteToServer: can't send\n");
        return;
    }

    if (NET_SendMessage(cls.netcon, &cls.message) == -1) {
        Host_Error("CL_WriteToServer: lost server connection");
    }

    SZ_Clear(&cls.message);
}

void CL_Init()
{
    SZ_Alloc(&cls.message, 1024);

    CL_InitInput();
    CL_InitTEnts();

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

void KeyDown(kbutton_t* b)
{
    int k;
    eastl::string_view c = Cmd::Argv(1);
    if (!c.empty()) k = Q_atoi(c);
    else k = -1;

    if (k == b->down[0] || k == b->down[1]) return;

    if (!b->down[0]) b->down[0] = k;
    else if (!b->down[1]) b->down[1] = k;
    else {
        Con_Printf("Three keys down for a button!\n");
        return;
    }

    if (b->state & 1) return;
    b->state |= 1 + 2;
}

void KeyUp(kbutton_t* b)
{
    int k;
    eastl::string_view c = Cmd::Argv(1);
    if (!c.empty()) {
        k = Q_atoi(c);
    } else {
        b->down[0] = b->down[1] = 0;
        b->state = 4;
        return;
    }

    if (b->down[0] == k) b->down[0] = 0;
    else if (b->down[1] == k) b->down[1] = 0;
    else return;

    if (b->down[0] || b->down[1]) return;
    if (!(b->state & 1)) return;

    b->state &= ~1;
    b->state |= 4;
}

float CL_KeyState(kbutton_t* key)
{
    float val = 0.0f;
    const bool impulsedown = (key->state & 2) != 0;
    const bool impulseup = (key->state & 4) != 0;
    const bool down = (key->state & 1) != 0;

    if (impulsedown && !impulseup && down) val = 0.5f;
    if (impulseup && !impulsedown && !down) val = 0.0f;
    if (!impulsedown && !impulseup && down) val = 1.0f;

    if (impulsedown && impulseup) {
        if (down) val = 0.75f;
        else val = 0.25f;
    }

    key->state &= 1;
    return val;
}

void CL_AdjustAngles()
{
    float speed = (in_speed.state & 1) ? static_cast<float>(host_frametime * cl_anglespeedkey.value) : static_cast<float>(host_frametime);

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

    if (up || down) V_StopPitchDrift();

    if (cl.viewangles[PITCH] > 80.0f) cl.viewangles[PITCH] = 80.0f;
    if (cl.viewangles[PITCH] < -70.0f) cl.viewangles[PITCH] = -70.0f;
    if (cl.viewangles[ROLL] > 50.0f) cl.viewangles[ROLL] = 50.0f;
    if (cl.viewangles[ROLL] < -50.0f) cl.viewangles[ROLL] = -50.0f;
}

void CL_BaseMove(usercmd_t* cmd)
{
    if (cls.signon != SIGNONS) return;
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

    if (in_speed.state & 1) {
        cmd->forwardmove *= cl_movespeedkey.value;
        cmd->sidemove *= cl_movespeedkey.value;
        cmd->upmove *= cl_movespeedkey.value;
    }
}

void CL_SendMove(usercmd_t* cmd)
{
    sizebuf_t buf;
    std::array<byte, 128> data{};
    buf.maxsize = 128;
    buf.cursize = 0;
    buf.data = data.data();

    cl.cmd = *cmd;

    MSG_WriteByte(&buf, clc_move);
    MSG_WriteFloat(&buf, static_cast<float>(cl.mtime[0]));

    for (int i = 0; i < 3; ++i) MSG_WriteAngle(&buf, cl.viewangles[i]);

    MSG_WriteShort(&buf, static_cast<int>(cmd->forwardmove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->sidemove));
    MSG_WriteShort(&buf, static_cast<int>(cmd->upmove));

    int bits = 0;
    if (in_attack.state & 3) bits |= 1;
    in_attack.state &= ~2;

    if (in_jump.state & 3) bits |= 2;
    in_jump.state &= ~2;

    MSG_WriteByte(&buf, bits);
    MSG_WriteByte(&buf, in_impulse);
    in_impulse = 0;

    if (cls.demoplayback) return;
    if (++cl.movemessages <= 2) return;

    if (NET_SendUnreliableMessage(cls.netcon, &buf) == -1) {
        Con_Printf("CL_SendMove: lost server connection\n");
        CL_Disconnect();
    }
}

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

void CL_FinishTimeDemo();

void CL_StopPlayback()
{
    if (!cls.demoplayback) return;
    fclose(cls.demofile);
    cls.demoplayback = false;
    cls.demofile = nullptr;
    cls.state = ca_disconnected;
    if (cls.timedemo) CL_FinishTimeDemo();
}

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

int CL_GetMessage()
{
    if (cls.demoplayback) {
        if (cls.signon == SIGNONS) {
            if (cls.timedemo) {
                if (host_framecount == cls.td_lastframe) return 0;
                cls.td_lastframe = host_framecount;
                if (host_framecount == cls.td_startframe + 1) {
                    cls.td_starttime = static_cast<float>(realtime);
                }
            } else if (cl.time <= cl.mtime[0]) return 0;
        }

        fread(&net_message.cursize, 4, 1, cls.demofile);
        cl.mviewangles[1] = cl.mviewangles[0];
        for (int i = 0; i < 3; ++i) {
            float f = 0.0f;
            fread(&f, 4, 1, cls.demofile);
            cl.mviewangles[0][i] = LittleFloat(f);
        }

        net_message.cursize = LittleLong(net_message.cursize);
        if (net_message.cursize > MAX_MSGLEN) Sys_Error("Demo message > MAX_MSGLEN");

        const int r = static_cast<int>(fread(net_message.data, net_message.cursize, 1, cls.demofile));
        if (r != 1) {
            CL_StopPlayback();
            return 0;
        }
        return 1;
    }

    while (true) {
        const int r = NET_GetMessage(cls.netcon);
        if (r != 1 && r != 2) return r;
        if (net_message.cursize == 1 && net_message.data[0] == svc_nop) {
            Con_Printf("<-- server to client keepalive\n");
        } else {
            return r;
        }
    }

    if (cls.demorecording) CL_WriteDemoMessage();
    return 1;
}

void CL_Stop_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (!cls.demorecording) {
        Con_Printf("Not recording a demo.\n");
        return;
    }

    SZ_Clear(&net_message);
    MSG_WriteByte(&net_message, svc_disconnect);
    CL_WriteDemoMessage();

    fclose(cls.demofile);
    cls.demofile = nullptr;
    cls.demorecording = false;
    Con_Printf("Completed demo\n");
}

void CL_Record_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;

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
        Con_Printf("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
        return;
    }

    int track = -1;
    if (c == 4) {
        track = Q_atoi(Cmd::Argv(3));
        Con_Printf("Forcing CD track to %i\n", cls.forcetrack);
    }

    eastl::string_view arg1 = Cmd::Argv(1);
    eastl::string name = eastl::string(com_gamedir) + "/" + eastl::string(arg1.data(), arg1.length());

    if (c > 2) {
        eastl::string_view arg2 = Cmd::Argv(2);
        eastl::string map_cmd = "map " + eastl::string(arg2.data(), arg2.length());
        Cmd::ExecuteString(map_cmd.c_str(), Cmd::Source::Command);
    }

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

void CL_PlayDemo_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (Cmd::Argc() != 2) {
        Con_Printf("play <demoname> : plays a demo\n");
        return;
    }

    CL_Disconnect();

    char name[256];
    eastl::string_view demo_arg = Cmd::Argv(1);
    eastl::string demo_name(demo_arg.data(), demo_arg.length());
    strcpy_s(name, sizeof(name), demo_name.c_str());
    COM_DefaultExtension(name, ".dem");

    Con_Printf("Playing demo from %s.\n", name);
    COM_FOpenFile(name, &cls.demofile);
    if (!cls.demofile) {
        Con_Printf("ERROR: couldn't open.\n");
        cls.demonum = -1;
        return;
    }

    cls.demoplayback = true;
    cls.state = ca_connected;
    cls.forcetrack = 0;

    int c;
    bool neg = false;
    while ((c = getc(cls.demofile)) != '\n') {
        if (c == '-') neg = true;
        else cls.forcetrack = cls.forcetrack * 10 + (c - '0');
    }

    if (neg) cls.forcetrack = -cls.forcetrack;
}

void CL_FinishTimeDemo()
{
    cls.timedemo = false;
    const int frames = (host_framecount - cls.td_startframe) - 1;
    float time = static_cast<float>(realtime - cls.td_starttime);
    if (!time) time = 1.0f;

    Con_Printf("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames / time);
}

void CL_TimeDemo_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (Cmd::Argc() != 2) {
        Con_Printf("timedemo <demoname> : gets demo speeds\n");
        return;
    }

    CL_PlayDemo_f();
    cls.timedemo = true;
    cls.td_startframe = host_framecount;
    cls.td_lastframe = -1;
}

entity_t* CL_EntityNum(int num)
{
    if (num >= cl.num_entities) {
        if (num >= MAX_EDICTS) Host_Error("CL_EntityNum: %i is an invalid number", num);
        while (cl.num_entities <= num) {
            cl_entities[cl.num_entities].colormap = vid.colormap;
            cl.num_entities++;
        }
    }
    return &cl_entities[num];
}

void CL_ParseStartSoundPacket()
{
    int packet_vol = DEFAULT_SOUND_PACKET_VOLUME;
    float attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;
    const int field_mask = MSG_ReadByte();

    if (field_mask & SND_VOLUME) packet_vol = MSG_ReadByte();
    if (field_mask & SND_ATTENUATION) attenuation = MSG_ReadByte() / 64.0f;

    int channel = MSG_ReadShort();
    const int sound_num = MSG_ReadByte();

    const int ent = channel >> 3;
    channel &= 7;

    if (ent > MAX_EDICTS) Host_Error("CL_ParseStartSoundPacket: ent = %i", ent);

    const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    S_StartSound(ent, channel, cl.sound_precache[sound_num], pos, packet_vol / 255.0f, attenuation);
}

void CL_KeepaliveMessage()
{
    if (sv.active || cls.demoplayback) return;

    sizebuf_t old = net_message;
    eastl::array<byte, 8192> olddata;
    eastl::copy_n(net_message.data, eastl::min(static_cast<int>(olddata.size()), net_message.cursize), olddata.begin());

    int ret;
    do {
        ret = CL_GetMessage();
        switch (ret) {
        default: Host_Error("CL_KeepaliveMessage: CL_GetMessage failed");
        case 0: break;
        case 1: Host_Error("CL_KeepaliveMessage: received a message"); break;
        case 2:
            if (MSG_ReadByte() != svc_nop) Host_Error("CL_KeepaliveMessage: datagram wasn't a nop");
            break;
        }
    } while (ret);

    net_message = old;
    eastl::copy_n(olddata.begin(), eastl::min(static_cast<int>(olddata.size()), net_message.cursize), net_message.data);

    const float time = static_cast<float>(Sys_FloatTime());
    static float lastmsg = 0.0f;
    if (time - lastmsg < 5.0f) return;

    lastmsg = time;
    Con_Printf("--> client to server keepalive\n");
    MSG_WriteByte(&cls.message, clc_nop);
    NET_SendMessage(cls.netcon, &cls.message);
    SZ_Clear(&cls.message);
}

void CL_ParseServerInfo()
{
    Con_DPrintf("Serverinfo packet received.\n");
    CL_ClearState();

    const int i = MSG_ReadLong();
    if (i != PROTOCOL_VERSION) {
        Con_Printf("Server returned version %i, not %i", i, PROTOCOL_VERSION);
        return;
    }

    cl.maxclients = MSG_ReadByte();
    if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD) {
        Con_Printf("Bad maxclients (%u) from server\n", cl.maxclients);
        return;
    }

    cl.scores = static_cast<scoreboard_t*>(Hunk_Alloc(cl.maxclients * sizeof(*cl.scores), "scores"));
    cl.gametype = MSG_ReadByte();

    const char* str = MSG_ReadString();
    strncpy_s(cl.levelname, sizeof(cl.levelname), str, _TRUNCATE);

    Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
    Con_Printf("%c%s\n", 2, str);

    std::array<std::string, MAX_MODELS> model_precache_names{};
    std::array<std::string, MAX_SOUNDS> sound_precache_names{};

    cl.model_precache.fill(nullptr);
    int nummodels = 1;
    while (true) {
        char* model_str = MSG_ReadString();
        if (!model_str[0]) break;
        if (nummodels == MAX_MODELS) {
            Con_Printf("Server sent too many model precaches\n");
            return;
        }
        model_precache_names[nummodels] = model_str;
        Mod_TouchModel(model_str);
        nummodels++;
    }

    cl.sound_precache.fill(nullptr);
    int numsounds = 1;
    while (true) {
        char* sound_str = MSG_ReadString();
        if (!sound_str[0]) break;
        if (numsounds == MAX_SOUNDS) {
            Con_Printf("Server sent too many sound precaches\n");
            return;
        }
        sound_precache_names[numsounds] = sound_str;
        S_TouchSound(sound_str);
        numsounds++;
    }

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

    cl_entities[0].model = cl.worldmodel = cl.model_precache[1];
    R_NewMap();
    Hunk_Check();
    noclip_anglehack = false;
}

void CL_ParseUpdate(int bits)
{
    if (cls.signon == SIGNONS - 1) {
        cls.signon = SIGNONS;
        CL_SignonReply();
    }

    if (bits & U_MOREBITS) bits |= (MSG_ReadByte() << 8);

    int num = (bits & U_LONGENTITY) ? MSG_ReadShort() : MSG_ReadByte();
    entity_t* ent = CL_EntityNum(num);

    for (int i = 0; i < 16; ++i) {
        if (bits & (1 << i)) bitcounts[i]++;
    }

    const bool forcelink = (ent->msgtime != cl.mtime[1]);
    ent->msgtime = cl.mtime[0];

    int modnum = (bits & U_MODEL) ? MSG_ReadByte() : ent->baseline.modelindex;
    if (modnum >= MAX_MODELS) Host_Error("CL_ParseModel: bad modnum");

    model_t* model = cl.model_precache[modnum];
    if (model != ent->model) {
        ent->model = model;
        if (model) {
            ent->syncbase = (model->synctype == synctype_t::ST_RAND) ? (static_cast<float>(rand() & 0x7fff) / 0x7fff) : 0.0f;
        }
    }

    ent->frame = (bits & U_FRAME) ? MSG_ReadByte() : ent->baseline.frame;

    int colormap_idx = (bits & U_COLORMAP) ? MSG_ReadByte() : ent->baseline.colormap;
    if (!colormap_idx) {
        ent->colormap = vid.colormap;
    } else {
        if (colormap_idx > cl.maxclients) Sys_Error("colormap_idx >= cl.maxclients");
        ent->colormap = cl.scores[colormap_idx - 1].translations;
    }

    ent->skinnum = (bits & U_SKIN) ? MSG_ReadByte() : ent->baseline.skin;
    ent->effects = (bits & U_EFFECTS) ? MSG_ReadByte() : ent->baseline.effects;

    ent->msg_origins[1] = ent->msg_origins[0];
    ent->msg_angles[1] = ent->msg_angles[0];

    ent->msg_origins[0][0] = (bits & U_ORIGIN1) ? MSG_ReadCoord() : ent->baseline.origin[0];
    ent->msg_angles[0][0] = (bits & U_ANGLE1) ? MSG_ReadAngle() : ent->baseline.angles[0];
    ent->msg_origins[0][1] = (bits & U_ORIGIN2) ? MSG_ReadCoord() : ent->baseline.origin[1];
    ent->msg_angles[0][1] = (bits & U_ANGLE2) ? MSG_ReadAngle() : ent->baseline.angles[1];
    ent->msg_origins[0][2] = (bits & U_ORIGIN3) ? MSG_ReadCoord() : ent->baseline.origin[2];
    ent->msg_angles[0][2] = (bits & U_ANGLE3) ? MSG_ReadAngle() : ent->baseline.angles[2];

    if (bits & U_NOLERP) ent->forcelink = true;

    if (forcelink || ent->forcelink) {
        ent->msg_origins[1] = ent->msg_origins[0];
        ent->origin = ent->msg_origins[0];
        ent->msg_angles[1] = ent->msg_angles[0];
        ent->angles = ent->msg_angles[0];
        ent->forcelink = true;
    }
}

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

void CL_ParseClientdata(int bits)
{
    cl.viewheight = (bits & SU_VIEWHEIGHT) ? static_cast<float>(MSG_ReadChar()) : static_cast<float>(DEFAULT_VIEWHEIGHT);
    cl.idealpitch = (bits & SU_IDEALPITCH) ? static_cast<float>(MSG_ReadChar()) : 0.0f;

    cl.mvelocity[1] = cl.mvelocity[0];
    for (int i = 0; i < 3; ++i) {
        cl.punchangle[i] = (bits & (SU_PUNCH1 << i)) ? static_cast<float>(MSG_ReadChar()) : 0.0f;
        cl.mvelocity[0][i] = (bits & (SU_VELOCITY1 << i)) ? static_cast<float>(MSG_ReadChar() * 16) : 0.0f;
    }

    const int i = MSG_ReadLong();
    if (cl.items != i) {
        Sbar_Changed();
        for (int j = 0; j < 32; ++j) {
            if ((i & (1 << j)) && !(cl.items & (1 << j))) cl.item_gettime[j] = static_cast<float>(cl.time);
        }
        cl.items = i;
    }

    cl.onground = (bits & SU_ONGROUND) != 0;
    cl.inwater = (bits & SU_INWATER) != 0;
    cl.stats[STAT_WEAPONFRAME] = (bits & SU_WEAPONFRAME) ? MSG_ReadByte() : 0;

    int armor_val = (bits & SU_ARMOR) ? MSG_ReadByte() : 0;
    if (cl.stats[STAT_ARMOR] != armor_val) {
        cl.stats[STAT_ARMOR] = armor_val;
        Sbar_Changed();
    }

    int weapon_val = (bits & SU_WEAPON) ? MSG_ReadByte() : 0;
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

void CL_NewTranslation(int slot)
{
    if (slot > cl.maxclients) Sys_Error("CL_NewTranslation: slot > cl.maxclients");

    byte* dest = cl.scores[slot].translations;
    const byte* source = vid.colormap;
    eastl::copy_n(vid.colormap, sizeof(cl.scores[slot].translations), dest);
    const int top = cl.scores[slot].colors & 0xf0;
    const int bottom = (cl.scores[slot].colors & 15) << 4;

    for (int i = 0; i < VID_GRADES; ++i, dest += 256, source += 256) {
        if (top < 128) {
            eastl::copy_n(source + top, 16, dest + TOP_RANGE);
        } else {
            for (int j = 0; j < 16; ++j) dest[TOP_RANGE + j] = source[top + 15 - j];
        }

        if (bottom < 128) {
            eastl::copy_n(source + bottom, 16, dest + BOTTOM_RANGE);
        } else {
            for (int j = 0; j < 16; ++j) dest[BOTTOM_RANGE + j] = source[bottom + 15 - j];
        }
    }
}

void CL_ParseStatic()
{
    const int i = cl.num_statics;
    if (i >= MAX_STATIC_ENTITIES) Host_Error("Too many static entities");

    entity_t* ent = &cl_static_entities[i];
    cl.num_statics++;
    CL_ParseBaseline(ent);

    ent->model = cl.model_precache[ent->baseline.modelindex];
    ent->frame = ent->baseline.frame;
    ent->colormap = vid.colormap;
    ent->skinnum = ent->baseline.skin;
    ent->effects = ent->baseline.effects;
    ent->origin = ent->baseline.origin;
    ent->angles = ent->baseline.angles;
    R_AddEfrags(ent);
}

void CL_ParseStaticSound()
{
    const Vector3 org{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    const int sound_num = MSG_ReadByte();
    const int vol = MSG_ReadByte();
    const int atten = MSG_ReadByte();
    S_StaticSound(cl.sound_precache[sound_num], org, static_cast<float>(vol), static_cast<float>(atten));
}

#define SHOWNET(x) if (cl_shownet.value == 2) Con_Printf("%3i:%s\n", msg_readcount - 1, x);

void CL_ParseServerMessage()
{
    if (cl_shownet.value == 1) Con_Printf("%i ", net_message.cursize);
    else if (cl_shownet.value == 2) Con_Printf("------------------\n");

    cl.onground = false;
    MSG_BeginReading();

    while (true) {
        if (msg_badread) Host_Error("CL_ParseServerMessage: Bad server message");

        const int cmd = MSG_ReadByte();
        if (cmd == -1) {
            SHOWNET("END OF MESSAGE");
            return;
        }

        if (cmd & 128) {
            SHOWNET("fast update");
            CL_ParseUpdate(cmd & 127);
            continue;
        }

        SHOWNET(svc_strings[cmd]);

        switch (cmd) {
        default: Host_Error("CL_ParseServerMessage: Illegible server message\n"); break;
        case svc_nop: break;
        case svc_time:
            cl.mtime[1] = cl.mtime[0];
            cl.mtime[0] = MSG_ReadFloat();
            break;
        case svc_clientdata: CL_ParseClientdata(MSG_ReadShort()); break;
        case svc_version:
            if (MSG_ReadLong() != PROTOCOL_VERSION) Host_Error("CL_ParseServerMessage: Server version mismatch\n");
            break;
        case svc_disconnect: Host_EndGame("Server disconnected\n"); break;
        case svc_print: Con_Printf("%s", MSG_ReadString()); break;
        case svc_centerprint: Screen::GetScreenSystem().CenterPrint(MSG_ReadString()); break;
        case svc_stufftext: Cmd::BufferAddText(MSG_ReadString()); break;
        case svc_damage: V_ParseDamage(); break;
        case svc_serverinfo:
            CL_ParseServerInfo();
            vid.recalc_refdef = true;
            break;
        case svc_setangle:
            for (int i = 0; i < 3; ++i) cl.viewangles[i] = MSG_ReadAngle();
            break;
        case svc_setview: cl.viewentity = MSG_ReadShort(); break;
        case svc_lightstyle: {
            const int i = MSG_ReadByte();
            if (i >= MAX_LIGHTSTYLES) Sys_Error("svc_lightstyle > MAX_LIGHTSTYLES");
            Q_strcpy(cl_lightstyle[i].map, MSG_ReadString());
            cl_lightstyle[i].length = Q_strlen(cl_lightstyle[i].map);
            break;
        }
        case svc_sound: CL_ParseStartSoundPacket(); break;
        case svc_stopsound: {
            const int i = MSG_ReadShort();
            S_StopSound(i >> 3, i & 7);
            break;
        }
        case svc_updatename: {
            Sbar_Changed();
            const int i = MSG_ReadByte();
            if (i >= cl.maxclients) Host_Error("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
            strcpy_s(cl.scores[i].name, sizeof(cl.scores[i].name), MSG_ReadString());
            break;
        }
        case svc_updatefrags: {
            Sbar_Changed();
            const int i = MSG_ReadByte();
            if (i >= cl.maxclients) Host_Error("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
            cl.scores[i].frags = MSG_ReadShort();
            break;
        }
        case svc_updatecolors: {
            Sbar_Changed();
            const int i = MSG_ReadByte();
            if (i >= cl.maxclients) Host_Error("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
            cl.scores[i].colors = MSG_ReadByte();
            CL_NewTranslation(i);
            break;
        }
        case svc_particle: R_ParseParticleEffect(); break;
        case svc_spawnbaseline: CL_ParseBaseline(CL_EntityNum(MSG_ReadShort())); break;
        case svc_spawnstatic: CL_ParseStatic(); break;
        case svc_temp_entity: CL_ParseTEnt(); break;
        case svc_setpause:
            cl.paused = MSG_ReadByte();
            VID_HandlePause();
            break;
        case svc_signonnum: {
            const int i = MSG_ReadByte();
            if (i <= cls.signon) Host_Error("Received signon %i when at %i", i, cls.signon);
            cls.signon = i;
            CL_SignonReply();
            break;
        }
        case svc_killedmonster: cl.stats[STAT_MONSTERS]++; break;
        case svc_foundsecret: cl.stats[STAT_SECRETS]++; break;
        case svc_updatestat: {
            const int i = MSG_ReadByte();
            if (i < 0 || i >= MAX_CL_STATS) Sys_Error("svc_updatestat: %i is invalid", i);
            cl.stats[i] = MSG_ReadLong();
            break;
        }
        case svc_spawnstaticsound: CL_ParseStaticSound(); break;
        case svc_cdtrack:
            cl.cdtrack = MSG_ReadByte();
            cl.looptrack = MSG_ReadByte();
            break;
        case svc_intermission:
            cl.intermission = 1;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true;
            break;
        case svc_finale:
            cl.intermission = 2;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true;
            Screen::GetScreenSystem().CenterPrint(MSG_ReadString());
            break;
        case svc_cutscene:
            cl.intermission = 3;
            cl.completed_time = static_cast<int>(cl.time);
            vid.recalc_refdef = true;
            Screen::GetScreenSystem().CenterPrint(MSG_ReadString());
            break;
        case svc_sellscreen: Cmd::ExecuteString("help", Cmd::Source::Command); break;
        }
    }
}

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

void CL_ParseBeam(model_t* m)
{
    const int ent = MSG_ReadShort();
    const Vector3 start{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    const Vector3 end{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };

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

void CL_ParseTEnt()
{
    const int type = MSG_ReadByte();
    switch (type) {
    case TE_WIZSPIKE: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 20, 30);
        S_StartSound(-1, 0, cl_sfx_wizhit, pos, 1, 1);
        break;
    }
    case TE_KNIGHTSPIKE: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 226, 20);
        S_StartSound(-1, 0, cl_sfx_knighthit, pos, 1, 1);
        break;
    }
    case TE_SPIKE: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 0, 10);
        if (rand() % 5) S_StartSound(-1, 0, cl_sfx_tink1, pos, 1, 1);
        else {
            const int rnd = rand() & 3;
            if (rnd == 1) S_StartSound(-1, 0, cl_sfx_ric1, pos, 1, 1);
            else if (rnd == 2) S_StartSound(-1, 0, cl_sfx_ric2, pos, 1, 1);
            else S_StartSound(-1, 0, cl_sfx_ric3, pos, 1, 1);
        }
        break;
    }
    case TE_SUPERSPIKE: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 0, 20);
        if (rand() % 5) S_StartSound(-1, 0, cl_sfx_tink1, pos, 1, 1);
        else {
            const int rnd = rand() & 3;
            if (rnd == 1) S_StartSound(-1, 0, cl_sfx_ric1, pos, 1, 1);
            else if (rnd == 2) S_StartSound(-1, 0, cl_sfx_ric2, pos, 1, 1);
            else S_StartSound(-1, 0, cl_sfx_ric3, pos, 1, 1);
        }
        break;
    }
    case TE_GUNSHOT: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_RunParticleEffect(pos, vec3_origin, 0, 20);
        break;
    }
    case TE_EXPLOSION: {
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
    case TE_TAREXPLOSION: {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        R_BlobExplosion(pos);
        S_StartSound(-1, 0, cl_sfx_r_exp3, pos, 1, 1);
        break;
    }
    case TE_LIGHTNING1: CL_ParseBeam(Mod_ForName("progs/bolt.mdl", true)); break;
    case TE_LIGHTNING2: CL_ParseBeam(Mod_ForName("progs/bolt2.mdl", true)); break;
    case TE_LIGHTNING3: CL_ParseBeam(Mod_ForName("progs/bolt3.mdl", true)); break;
    case TE_BEAM: CL_ParseBeam(Mod_ForName("progs/beam.mdl", true)); break;
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
    case TE_EXPLOSION2: {
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
    default: Sys_Error("CL_ParseTEnt: bad type");
    }
}

entity_t* CL_NewTempEntity()
{
    if (cl_numvisedicts == MAX_VISEDICTS || num_temp_entities == MAX_TEMP_ENTITIES) return nullptr;

    entity_t* ent = &cl_temp_entities[num_temp_entities++];
    *ent = {};
    cl_visedicts[cl_numvisedicts++] = ent;
    ent->colormap = vid.colormap;
    return ent;
}

void CL_UpdateTEnts()
{
    num_temp_entities = 0;
    int i = 0;
    for (auto& b : cl_beams) {
        if (!b.model || b.endtime < cl.time) {
            ++i;
            continue;
        }

        if (b.entity == cl.viewentity) b.start = cl_entities[cl.viewentity].origin;

        Vector3 dist = b.end - b.start;
        float yaw, pitch;

        if (dist.y == 0.0f && dist.x == 0.0f) {
            yaw = 0.0f;
            pitch = (dist.z > 0.0f) ? 90.0f : 270.0f;
        } else {
            yaw = static_cast<float>(atan2(dist.y, dist.x) * 180.0 / M_PI);
            if (yaw < 0.0f) yaw += 360.0f;

            const float forward = sqrt(dist.x * dist.x + dist.y * dist.y);
            pitch = static_cast<float>(atan2(dist.z, forward) * 180.0 / M_PI);
            if (pitch < 0.0f) pitch += 360.0f;
        }

        Vector3 org = b.start;
        float d = dist.normalize();
        while (d > 0.0f) {
            entity_t* ent = CL_NewTempEntity();
            if (!ent) return;

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

namespace {
cvar_t chase_back = { "chase_back", "100", {}, {}, {}, {} };
cvar_t chase_up = { "chase_up", "16", {}, {}, {}, {} };
cvar_t chase_right = { "chase_right", "0", {}, {}, {}, {} };
Vector3 chase_dest;

void TraceLine(const Vector3& start, const Vector3& end, Vector3& impact)
{
    trace_t trace{};
    SV_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);
    impact = trace.endpos;
}
}

cvar_t chase_active = { "chase_active", "0", {}, {}, {}, {} };

void Chase_Init()
{
    Cvar::Register(&chase_back);
    Cvar::Register(&chase_up);
    Cvar::Register(&chase_right);
    Cvar::Register(&chase_active);
}

void Chase_Update()
{
    Vector3 forward, up, right;
    AngleVectors(cl.viewangles, forward, right, up);

    chase_dest = r_refdef.vieworg - forward * chase_back.value - right * chase_right.value;
    chase_dest.z = r_refdef.vieworg.z + chase_up.value;

    Vector3 dest = r_refdef.vieworg + forward * 4096.0f;
    Vector3 stop;
    TraceLine(r_refdef.vieworg, dest, stop);

    stop = stop - r_refdef.vieworg;
    float dist = stop.dot(forward);
    if (dist < 1.0f) dist = 1.0f;

    r_refdef.viewangles[PITCH] = static_cast<float>(-std::atan(stop.z / dist) / std::numbers::pi * 180.0f);
    r_refdef.vieworg = chase_dest;
}

} // namespace Client

namespace Host {

quakeparms_t host_parms;
qboolean host_initialized;
double host_frametime;
double host_time;
double realtime;
double oldrealtime;
int host_framecount;
int host_hunklevel;
int minimum_memory;
client_t* host_client;
byte* host_basepal;
byte* host_colormap;

cvar_t host_framerate = { "host_framerate", "0", {}, {}, {}, {} };
cvar_t host_speeds = { "host_speeds", "0", {}, {}, {}, {} };
cvar_t sys_ticrate = { "sys_ticrate", "0.05", {}, {}, {}, {} };
cvar_t serverprofile = { "serverprofile", "0", {}, {}, {}, {} };
cvar_t samelevel = { "samelevel", "0", {}, {}, {}, {} };
cvar_t noexit = { "noexit", "0", false, true, {}, {} };
cvar_t developer = { "developer", "0", {}, {}, {}, {} };
cvar_t pausable = { "pausable", "1", {}, {}, {}, {} };
cvar_t temp1 = { "temp1", "0", {}, {}, {}, {} };

[[noreturn]] void Host_EndGame(const char* message, ...)
{
    va_list argptr;
    char string[1024];
    va_start(argptr, message);
    vsprintf_s(string, sizeof(string), message, argptr);
    va_end(argptr);
    Con_DPrintf("Host_EndGame: %s\n", string);

    if (sv.active) Host_ShutdownServer(false);
    if (cls.state == ca_dedicated) Sys_Error("Host_EndGame: %s\n", string);
    if (cls.demonum != -1) CL_NextDemo();
    else CL_Disconnect();

    Sys_Error("Host_EndGame: %s\n", string);
}

[[noreturn]] void Host_Error(const char* error, ...)
{
    va_list argptr;
    char string[1024];
    static qboolean inerror = false;

    if (inerror) Sys_Error("Host_Error: recursively entered");
    inerror = true;

    Screen::GetScreenSystem().EndLoadingPlaque();

    va_start(argptr, error);
    vsprintf_s(string, sizeof(string), error, argptr);
    va_end(argptr);
    Con_Printf("Host_Error: %s\n", string);

    if (sv.active) Host_ShutdownServer(false);
    if (cls.state == ca_dedicated) Sys_Error("Host_Error: %s\n", string);

    CL_Disconnect();
    cls.demonum = -1;
    inerror = false;
    Sys_Error("Host_Error: %s\n", string);
}

void Host_FindMaxClients()
{
    svs.maxclients = 1;
    int i = COM_CheckParm("-dedicated");
    if (i) {
        cls.state = ca_dedicated;
        if (i != (com_argc - 1)) svs.maxclients = Q_atoi(com_argv[i + 1]);
        else svs.maxclients = 8;
    } else {
        cls.state = ca_disconnected;
    }

    i = COM_CheckParm("-listen");
    if (i) {
        if (cls.state == ca_dedicated) Sys_Error("Only one of -dedicated or -listen can be specified");
        if (i != (com_argc - 1)) svs.maxclients = Q_atoi(com_argv[i + 1]);
        else svs.maxclients = 8;
    }

    if (svs.maxclients < 1) svs.maxclients = 8;
    else if (svs.maxclients > MAX_SCOREBOARD) svs.maxclients = MAX_SCOREBOARD;

    svs.maxclientslimit = svs.maxclients;
    if (svs.maxclientslimit < 4) svs.maxclientslimit = 4;
    svs.resize_clients(svs.maxclientslimit);

    if (svs.maxclients > 1) Cvar::SetValue("deathmatch", 1.0);
    else Cvar::SetValue("deathmatch", 0.0);
}

void Host_InitLocal()
{
    Host_InitCommands();

    Cvar::Register(&host_framerate);
    Cvar::Register(&host_speeds);
    Cvar::Register(&sys_ticrate);
    Cvar::Register(&serverprofile);
    Cvar::Register(&fraglimit);
    Cvar::Register(&timelimit);
    Cvar::Register(&teamplay);
    Cvar::Register(&samelevel);
    Cvar::Register(&noexit);
    Cvar::Register(&skill);
    Cvar::Register(&developer);
    Cvar::Register(&deathmatch);
    Cvar::Register(&coop);
    Cvar::Register(&pausable);
    Cvar::Register(&temp1);

    Host_FindMaxClients();
    host_time = 1.0;
}

void Host_WriteConfiguration()
{
    if (host_initialized && !isDedicated) {
        std::string config_path = std::string(com_gamedir) + "/config.cfg";
        std::ofstream f(config_path);
        if (!f.is_open()) {
            Con_Printf("Couldn't write config.cfg.\n");
            return;
        }

        Key_WriteBindings(f);
        Cvar::WriteVariables(f);
    }
}

void Host_ClientCommands(const char* fmt, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, fmt);
    vsprintf_s(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    MSG_WriteByte(&host_client->message, svc_stufftext);
    MSG_WriteString(&host_client->message, string);
}

void Host_ShutdownServer(qboolean crash)
{
    if (!sv.active) return;
    sv.active = false;

    if (cls.state == ca_connected) CL_Disconnect();

    double start = Sys_FloatTime();
    int count;
    do {
        count = 0;
        for (int i = 0; i < svs.maxclients; i++) {
            host_client = &svs.clients[i];
            if (host_client->active && host_client->message.cursize) {
                if (NET_CanSendMessage(host_client->netconnection)) {
                    NET_SendMessage(host_client->netconnection, &host_client->message);
                    SZ_Clear(&host_client->message);
                } else {
                    NET_GetMessage(host_client->netconnection);
                    count++;
                }
            }
        }
        if ((Sys_FloatTime() - start) > 3.0) break;
    } while (count);

    char message[4];
    sizebuf_t buf;
    buf.data = (byte*)message;
    buf.maxsize = 4;
    buf.cursize = 0;
    MSG_WriteByte(&buf, svc_disconnect);
    count = NET_SendToAll(&buf, 5);
    if (count) Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

    for (int i = 0; i < svs.maxclients; i++) {
        host_client = &svs.clients[i];
        if (host_client->active) SV_DropClient(crash);
    }

    memset(&sv, 0, sizeof(sv));
    for (int i = 0; i < svs.maxclientslimit; i++) svs.clients[i] = {};
}

void Host_ClearMemory()
{
    Con_DPrintf("Clearing memory\n");
    D_FlushCaches();
    Mod_ClearAll();
    if (host_hunklevel) Hunk_FreeToLowMark(host_hunklevel);

    cls.signon = 0;
    memset(&sv, 0, sizeof(sv));
    cl = {};
}

qboolean Host_FilterTime(float time)
{
    realtime += time;
    if (!cls.timedemo && realtime - oldrealtime < 1.0 / 72.0) return false;

    host_frametime = realtime - oldrealtime;
    oldrealtime = realtime;

    if (host_framerate.value > 0) {
        host_frametime = host_framerate.value;
    } else {
        if (host_frametime > 0.1) host_frametime = 0.1;
        if (host_frametime < 0.001) host_frametime = 0.001;
    }

    return true;
}

void Host_GetConsoleCommands()
{
    while (1) {
        char* cmd = Sys_ConsoleInput();
        if (!cmd) break;
        Cmd::BufferAddText(cmd);
    }
}

void Host_ServerFrame()
{
    pr_global_struct->frametime = static_cast<float>(host_frametime);
    SZ_Clear(&sv.datagram);
    SV_CheckForNewClients();
    SV_RunClients();

    if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
        SV_Physics();
    }

    SV_SendClientMessages();
}

void _Host_Frame(float time)
{
    static double time1 = 0, time2 = 0, time3 = 0;

    rand();
    if (!Host_FilterTime(time)) return;

    Sys_SendKeyEvents();
    IN_Commands();
    Cmd::BufferExecute();
    NET_Poll();

    if (sv.active) CL_SendCmd();

    Host_GetConsoleCommands();

    if (sv.active) Host_ServerFrame();

    if (!sv.active) CL_SendCmd();

    host_time += host_frametime;

    if (cls.state == ca_connected) CL_ReadFromServer();

    if (host_speeds.value) time1 = Sys_FloatTime();

    Screen::GetScreenSystem().UpdateScreen();

    if (host_speeds.value) time2 = Sys_FloatTime();

    if (cls.signon == SIGNONS) {
        S_Update(r_origin, vpn, vright, vup);
        CL_DecayLights();
    } else {
        S_Update(vec3_origin, vec3_origin, vec3_origin, vec3_origin);
    }

    if (host_speeds.value) {
        int pass1 = static_cast<int>((time1 - time3) * 1000);
        time3 = Sys_FloatTime();
        int pass2 = static_cast<int>((time2 - time1) * 1000);
        int pass3 = static_cast<int>((time3 - time2) * 1000);
        Con_Printf("%3i tot %3i server %3i gfx %3i snd\n", pass1 + pass2 + pass3, pass1, pass2, pass3);
    }

    host_framecount++;
}

void Host_Frame(float time)
{
    static double timetotal;
    static int timecount;

    if (!serverprofile.value) {
        _Host_Frame(time);
        return;
    }

    double time1 = Sys_FloatTime();
    _Host_Frame(time);
    double time2 = Sys_FloatTime();

    timetotal += time2 - time1;
    timecount++;

    if (timecount < 1000) return;

    int m = static_cast<int>(timetotal * 1000 / timecount);
    timecount = 0; timetotal = 0;
    int c = 0;
    for (int i = 0; i < svs.maxclients; i++) {
        if (svs.clients[i].active) c++;
    }

    Con_Printf("serverprofile: %2i clients %2i msec\n", c, m);
}

#define VCR_SIGNATURE 0x56435231

void Host_InitVCR(quakeparms_t* parms)
{
    if (COM_CheckParm("-playback")) {
        if (com_argc != 2) Sys_Error("No other parameters allowed with -playback\n");

        Sys_FileOpenRead("quake.vcr", &vcrFile);
        if (vcrFile == -1) Sys_Error("playback file not found\n");

        int signature = 0;
        Sys_FileRead(vcrFile, &signature, sizeof(int));
        if (signature != VCR_SIGNATURE) Sys_Error("Invalid signature in vcr file\n");

        Sys_FileRead(vcrFile, &com_argc, sizeof(int));
        com_argv = static_cast<char**>(malloc(com_argc * sizeof(char*)));
        com_argv[0] = parms->argv[0];
        for (int i = 0; i < com_argc; i++) {
            int len = 0;
            Sys_FileRead(vcrFile, &len, sizeof(int));
            char* p = static_cast<char*>(malloc(len));
            Sys_FileRead(vcrFile, p, len);
            com_argv[i + 1] = p;
        }
        com_argc++;
        parms->argc = com_argc;
        parms->argv = com_argv;
    }

    int n = COM_CheckParm("-record");
    if (n != 0) {
        vcrFile = Sys_FileOpenWrite("quake.vcr");
        int signature = VCR_SIGNATURE;
        Sys_FileWrite(vcrFile, &signature, sizeof(int));
        int count = com_argc - 1;
        Sys_FileWrite(vcrFile, &count, sizeof(int));
        for (int i = 1; i < com_argc; i++) {
            if (i == n) {
                int len = 10;
                Sys_FileWrite(vcrFile, &len, sizeof(int));
                Sys_FileWrite(vcrFile, "-playback", len);
                continue;
            }

            int len = Q_strlen(com_argv[i]) + 1;
            Sys_FileWrite(vcrFile, &len, sizeof(int));
            Sys_FileWrite(vcrFile, com_argv[i], len);
        }
    }
}

void Host_Init(quakeparms_t* parms)
{
    if (standard_quake) minimum_memory = MINIMUM_MEMORY;
    else minimum_memory = MINIMUM_MEMORY_LEVELPAK;

    if (COM_CheckParm("-minmemory")) parms->memsize = minimum_memory;

    host_parms = *parms;

    if (parms->memsize < minimum_memory) {
        Sys_Error("Only %4.1f megs of memory available, can't execute game", parms->memsize / (float)0x100000);
    }

    com_argc = parms->argc;
    com_argv = parms->argv;

    Memory_Init(parms->membase, parms->memsize);
    Cmd::BufferInit();
    Cmd::Init();
    V_Init();
    Chase_Init();
    Host_InitVCR(parms);
    COM_Init();
    Host_InitLocal();
    W_LoadWadFile("gfx.wad");
    Key_Init();
    GetConsoleSystem().Init();
    M_Init();
    PR_Init();
    Mod_Init();
    NET_Init();
    SV_Init();

    Con_Printf("Exe: " __TIME__ " " __DATE__ "\n");
    Con_Printf("%4.1f megabyte heap\n", parms->memsize / (1024 * 1024.0));

    R_InitTextures();

    if (cls.state != ca_dedicated) {
        host_basepal = (byte*)COM_LoadHunkFile("gfx/palette.lmp");
        if (!host_basepal) Sys_Error("Couldn't load gfx/palette.lmp");

        host_colormap = (byte*)COM_LoadHunkFile("gfx/colormap.lmp");
        if (!host_colormap) Sys_Error("Couldn't load gfx/colormap.lmp");

        VID_Init(host_basepal);
        Draw_Init();
        Screen::GetScreenSystem().Init();
        R_Init();
        S_Init();
        Sbar_Init();
        CL_Init();
        IN_Init();
    }

    Cmd::BufferInsertText("exec quake.rc\n");

    Hunk_Alloc(0, "-HOST_HUNKLEVEL-");
    host_hunklevel = Hunk_LowMark();
    host_initialized = true;

    Sys_Printf("========Quake Initialized=========\n");
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

    NET_Shutdown();
    S_Shutdown();
    IN_Shutdown();

    if (cls.state != ca_dedicated) VID_Shutdown();
}

int current_skill;

void Host_Quit_f()
{
    if (key_dest != key_console && cls.state != ca_dedicated) {
        M_Menu_Quit_f();
        return;
    }

    CL_Disconnect();
    Host_ShutdownServer(false);
    Sys_Quit();
}

void Host_Status_f()
{
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
    if (tcpipAvailable) print("tcp/ip:  %s\n", my_tcpip_address);
    if (ipxAvailable) print("ipx:     %s\n", my_ipx_address);

    print("map:     %s\n", sv.name);
    print("players: %i active (%i max)\n\n", net_activeconnections, svs.maxclients);

    for (int j = 0; j < svs.maxclients; j++) {
        client_t* client = &svs.clients[j];
        if (!client->active) continue;

        int seconds = static_cast<int>(net_time - client->netconnection->connecttime);
        int minutes = seconds / 60;
        int hours = 0;
        if (minutes) {
            seconds -= (minutes * 60);
            hours = minutes / 60;
            if (hours) minutes -= (hours * 60);
        }

        print("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j + 1, client->name.data(),
            static_cast<int>(client->edict->v.frags), hours, minutes, seconds);
        print("   %s\n", client->netconnection->address);
    }
}

void Host_God_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) return;

    sv_player->v.flags = static_cast<float>(static_cast<int>(sv_player->v.flags) ^ FL_GODMODE);
    if (!(static_cast<int>(sv_player->v.flags) & FL_GODMODE)) SV_ClientPrintf("godmode OFF\n");
    else SV_ClientPrintf("godmode ON\n");
}

void Host_Notarget_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) return;

    sv_player->v.flags = static_cast<float>(static_cast<int>(sv_player->v.flags) ^ FL_NOTARGET);
    if (!(static_cast<int>(sv_player->v.flags) & FL_NOTARGET)) SV_ClientPrintf("notarget OFF\n");
    else SV_ClientPrintf("notarget ON\n");
}

qboolean noclip_anglehack;

void Host_Noclip_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) return;

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

void Host_Fly_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) return;

    if (sv_player->v.movetype != MOVETYPE_FLY) {
        sv_player->v.movetype = MOVETYPE_FLY;
        SV_ClientPrintf("flymode ON\n");
    } else {
        sv_player->v.movetype = MOVETYPE_WALK;
        SV_ClientPrintf("flymode OFF\n");
    }
}

void Host_Ping_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    SV_ClientPrintf("Client ping times:\n");
    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active) continue;

        float total = 0.0f;
        for (int j = 0; j < NUM_PING_TIMES; j++) total += client->ping_times[j];
        total /= NUM_PING_TIMES;
        SV_ClientPrintf("%4i %s\n", static_cast<int>(total * 1000.0f), client->name.data());
    }
}

void Host_Map_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;

    cls.demonum = -1;
    CL_Disconnect();
    Host_ShutdownServer(false);

    key_dest = key_game;
    Screen::GetScreenSystem().BeginLoadingPlaque();

    eastl::string mapstring;
    for (int i = 0; i < Cmd::Argc(); i++) {
        eastl::string_view arg = Cmd::Argv(i);
        mapstring.append(arg.data(), arg.length());
        mapstring += " ";
    }
    mapstring += "\n";
    strcpy_s(cls.mapstring, sizeof(cls.mapstring), mapstring.c_str());

    svs.serverflags = 0;
    
    char name[MAX_QPATH];
    eastl::string_view arg1 = Cmd::Argv(1);
    Q_strncpy(name, arg1, sizeof(name) - 1);
    SV_SpawnServer(name);
    if (!sv.active) return;

    if (cls.state != ca_dedicated) {
        eastl::string spawnparms;
        for (int i = 2; i < Cmd::Argc(); i++) {
            eastl::string_view arg = Cmd::Argv(i);
            spawnparms.append(arg.data(), arg.length());
            spawnparms += " ";
        }
        strcpy_s(cls.spawnparms, sizeof(cls.spawnparms), spawnparms.c_str());
        Cmd::ExecuteString("connect local", Cmd::Source::Command);
    }
}

void Host_Changelevel_f()
{
    if (Cmd::Argc() != 2) {
        Con_Printf("changelevel <levelname> : continue game on a new level\n");
        return;
    }

    if (!sv.active || cls.demoplayback) {
        Con_Printf("Only the server may changelevel\n");
        return;
    }

    SV_SaveSpawnparms();
    char level[MAX_QPATH];
    Q_strcpy(level, Cmd::Argv(1));
    SV_SpawnServer(level);
}

void Host_Restart_f()
{
    if (cls.demoplayback || !sv.active || Cmd::state.source != Cmd::Source::Command) return;

    char mapname[MAX_QPATH];
    strcpy_s(mapname, sizeof(mapname), sv.name.data());
    SV_SpawnServer(mapname);
}

void Host_Reconnect_f()
{
    Screen::GetScreenSystem().BeginLoadingPlaque();
    cls.signon = 0;
}

void Host_Connect_f()
{
    cls.demonum = -1;
    if (cls.demoplayback) {
        CL_StopPlayback();
        CL_Disconnect();
    }

    char name[MAX_QPATH];
    Q_strcpy(name, Cmd::Argv(1));
    CL_EstablishConnection(name);
    Host_Reconnect_f();
}

#define SAVEGAME_VERSION 5

std::string Host_SavegameComment()
{
    std::string text(SAVEGAME_COMMENT_LENGTH, ' ');
    std::string levelname = cl.levelname;
    if (levelname.length() > 22) levelname = levelname.substr(0, 22);
    text.replace(0, levelname.length(), levelname);

    char kills[20];
    sprintf_s(kills, sizeof(kills), "kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
    std::string kills_str = kills;
    if (kills_str.length() > (SAVEGAME_COMMENT_LENGTH - 22)) kills_str = kills_str.substr(0, SAVEGAME_COMMENT_LENGTH - 22);
    text.replace(22, kills_str.length(), kills_str);

    for (char& c : text) {
        if (c == ' ') c = '_';
    }
    return text;
}

void Host_Savegame_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (!sv.active) { Con_Printf("Not playing a local game.\n"); return; }
    if (cl.intermission) { Con_Printf("Can't save in intermission.\n"); return; }
    if (svs.maxclients != 1) { Con_Printf("Can't save multiplayer games.\n"); return; }
    if (Cmd::Argc() != 2) { Con_Printf("save <savename> : save a game\n"); return; }

    if (Cmd::Argv(1).find("..") != std::string_view::npos) {
        Con_Printf("Relative pathnames are not allowed.\n");
        return;
    }

    for (int i = 0; i < svs.maxclients; i++) {
        if (svs.clients[i].active && (svs.clients[i].edict->v.health <= 0)) {
            Con_Printf("Can't savegame with a dead player\n");
            return;
        }
    }

    char name[256];
    eastl::string_view save_arg = Cmd::Argv(1);
    sprintf_s(name, sizeof(name), "%s/%.*s", com_gamedir, static_cast<int>(save_arg.length()), save_arg.data());
    COM_DefaultExtension(name, ".sav");

    Con_Printf("Saving game to %s...\n", name);
    std::ofstream f(name);
    if (!f.is_open()) {
        Con_Printf("ERROR: couldn't open.\n");
        return;
    }

    f << SAVEGAME_VERSION << "\n";
    f << Host_SavegameComment() << "\n";
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) f << svs.clients->spawn_parms[i] << "\n";
    f << current_skill << "\n";
    f << sv.name.data() << "\n";
    f << sv.time << "\n";

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (sv.lightstyles[i]) f << sv.lightstyles[i] << "\n";
        else f << "m\n";
    }

    ED_WriteGlobals(f);
    for (int i = 0; i < sv.num_edicts; i++) {
        ED_Write(f, EDICT_NUM(i));
        f.flush();
    }
    Con_Printf("done.\n");
}

void Host_Loadgame_f()
{
    if (Cmd::state.source != Cmd::Source::Command) return;
    if (Cmd::Argc() != 2) { Con_Printf("load <savename> : load a game\n"); return; }

    cls.demonum = -1;

    char name[MAX_OSPATH];
    eastl::string_view load_arg = Cmd::Argv(1);
    sprintf_s(name, sizeof(name), "%s/%.*s", com_gamedir, static_cast<int>(load_arg.length()), load_arg.data());
    COM_DefaultExtension(name, ".sav");

    Con_Printf("Loading game from %s...\n", name);
    std::ifstream f(name);
    if (!f.is_open()) {
        Con_Printf("ERROR: couldn't open.\n");
        return;
    }

    int version = 0;
    if (!(f >> version)) { Con_Printf("ERROR: read error.\n"); return; }
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (version != SAVEGAME_VERSION) {
        Con_Printf("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
        return;
    }

    std::string str;
    if (!std::getline(f, str)) { Con_Printf("ERROR: read error.\n"); return; }

    float spawn_parms[NUM_SPAWN_PARMS];
    for (int i = 0; i < NUM_SPAWN_PARMS; i++) {
        if (!(f >> spawn_parms[i])) { Con_Printf("ERROR: read error.\n"); return; }
    }

    float tfloat = 0.0f;
    if (!(f >> tfloat)) { Con_Printf("ERROR: read error.\n"); return; }
    current_skill = static_cast<int>(tfloat + 0.1f);
    Cvar::SetValue("skill", static_cast<float>(current_skill));

    char mapname[MAX_QPATH];
    if (!(f >> mapname)) { Con_Printf("ERROR: read error.\n"); return; }

    float time = 0.0f;
    if (!(f >> time)) { Con_Printf("ERROR: read error.\n"); return; }
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    CL_Disconnect_f();
    SV_SpawnServer(mapname);
    if (!sv.active) { Con_Printf("Couldn't load map\n"); return; }

    sv.paused = true;
    sv.loadgame = true;

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (!std::getline(f, str)) { Con_Printf("ERROR: read error.\n"); return; }
        sv.lightstyles[i] = static_cast<char*>(Hunk_Alloc(static_cast<int>(str.length()) + 1));
        strcpy_s(sv.lightstyles[i], str.length() + 1, str.c_str());
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

        const char* start = COM_Parse(entity_str.c_str());
        if (!com_token[0]) break;
        if (strcmp(com_token, "{") != 0) Sys_Error("First token isn't a brace");

        if (entnum == -1) {
            ED_ParseGlobals(entity_str.data() + (start - entity_str.c_str()));
        } else {
            edict_t* ent = EDICT_NUM(entnum);
            memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4);
            ent->free = false;
            ED_ParseEdict(entity_str.data() + (start - entity_str.c_str()), ent);
            if (!ent->free) SV_LinkEdict(ent, false);
        }
        entnum++;
    }

    sv.num_edicts = entnum;
    sv.time = time;

    for (int i = 0; i < NUM_SPAWN_PARMS; i++) svs.clients->spawn_parms[i] = spawn_parms[i];

    if (cls.state != ca_dedicated) {
        CL_EstablishConnection("local");
        Host_Reconnect_f();
    }
}

void Host_Name_f()
{
    char newName[64];
    if (Cmd::Argc() == 1) {
        Con_Printf("\"name\" is \"%s\"\n", cl_name.string.c_str());
        return;
    }

    if (Cmd::Argc() == 2) {
        eastl::string_view arg1 = Cmd::Argv(1);
        Q_strncpy(newName, arg1, sizeof(newName) - 1);
    } else {
        eastl::string_view args = Cmd::Args();
        Q_strncpy(newName, args, sizeof(newName) - 1);
    }

    newName[15] = 0;

    if (Cmd::state.source == Cmd::Source::Command) {
        if (Q_strcmp(cl_name.string.c_str(), newName) == 0) return;
        Cvar::Set("_cl_name", newName);
        if (cls.state == ca_connected) Cmd::ForwardToServer();
        return;
    }

    if (host_client->name[0] && strcmp(host_client->name.data(), "unconnected") != 0) {
        if (Q_strcmp(host_client->name.data(), newName) != 0) {
            Con_Printf("%s renamed to %s\n", host_client->name.data(), newName);
        }
    }

    Q_strcpy(host_client->name.data(), newName);
    host_client->edict->v.netname = PR_SetString(host_client->name.data());

    MSG_WriteByte(&sv.reliable_datagram, svc_updatename);
    MSG_WriteByte(&sv.reliable_datagram, static_cast<int>(host_client - svs.clients));
    MSG_WriteString(&sv.reliable_datagram, host_client->name.data());
}

void Host_Version_f()
{
    Con_Printf("Version %4.2f\n", VERSION);
    Con_Printf("Exe: " __TIME__ " " __DATE__ "\n");
}

void Host_Say(qboolean teamonly)
{
    if (Cmd::state.source == Cmd::Source::Command) {
        if (cls.state != ca_dedicated) {
            Cmd::ForwardToServer();
            return;
        }
    }

    if (Cmd::Argc() < 2) return;

    client_t* save = host_client;
    eastl::string_view arg_sv = Cmd::Args();
    eastl::string arg_str(arg_sv.data(), arg_sv.length());

    if (!arg_str.empty() && arg_str.front() == '"') {
        arg_str = arg_str.substr(1);
        if (!arg_str.empty() && arg_str.back() == '"') arg_str.pop_back();
    }

    eastl::string text_str;
    if (Cmd::state.source == Cmd::Source::Command && cls.state == ca_dedicated) {
        text_str = eastl::string(1, '\x01') + "<" + hostname.string.c_str() + "> ";
    } else {
        text_str = eastl::string(1, '\x01') + save->name.data() + ": ";
    }

    int j = 64 - 2 - static_cast<int>(text_str.length());
    if (j > 0 && arg_str.length() > static_cast<size_t>(j)) arg_str.resize(j);

    text_str += arg_str;
    text_str += "\n";

    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active || !client->spawned) continue;
        if (teamplay.value && teamonly && client->edict->v.team != save->edict->v.team) continue;

        host_client = client;
        SV_ClientPrintf("%s", text_str.c_str());
    }
    host_client = save;
    Sys_Printf("%s", &text_str[1]);
}

void Host_Tell_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (Cmd::Argc() < 3) return;

    eastl::string text_str = eastl::string(host_client->name.data()) + ": ";
    eastl::string_view arg_sv = Cmd::Args();
    eastl::string arg_str(arg_sv.data(), arg_sv.length());

    if (!arg_str.empty() && arg_str.front() == '"') {
        arg_str = arg_str.substr(1);
        if (!arg_str.empty() && arg_str.back() == '"') arg_str.pop_back();
    }

    int j = 64 - 2 - static_cast<int>(text_str.length());
    if (j > 0 && arg_str.length() > static_cast<size_t>(j)) arg_str.resize(j);

    text_str += arg_str;
    text_str += "\n";

    client_t* save = host_client;
    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active || !client->spawned) continue;
        if (Q_strcasecmp(client->name.data(), Cmd::Argv(1)) != 0) continue;

        host_client = client;
        SV_ClientPrintf("%s", text_str.c_str());
        break;
    }
    host_client = save;
}

void Host_Color_f()
{
    if (Cmd::Argc() == 1) {
        Con_Printf("\"color\" is \"%i %i\"\n", static_cast<int>(cl_color.value) >> 4, static_cast<int>(cl_color.value) & 0x0f);
        Con_Printf("color <0-13> [0-13]\n");
        return;
    }

    int top = 0, bottom = 0;
    if (Cmd::Argc() == 2) top = bottom = Q_atoi(Cmd::Argv(1));
    else { top = Q_atoi(Cmd::Argv(1)); bottom = Q_atoi(Cmd::Argv(2)); }

    top &= 15; if (top > 13) top = 13;
    bottom &= 15; if (bottom > 13) bottom = 13;

    int pcolor = top * 16 + bottom;

    if (Cmd::state.source == Cmd::Source::Command) {
        Cvar::SetValue("_cl_color", static_cast<float>(pcolor));
        if (cls.state == ca_connected) Cmd::ForwardToServer();
        return;
    }

    host_client->colors = pcolor;
    host_client->edict->v.team = static_cast<float>(bottom + 1);

    MSG_WriteByte(&sv.reliable_datagram, svc_updatecolors);
    MSG_WriteByte(&sv.reliable_datagram, static_cast<int>(host_client - svs.clients));
    MSG_WriteByte(&sv.reliable_datagram, host_client->colors);
}

void Host_Kill_f()
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

void Host_Pause_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (!pausable.value) {
        SV_ClientPrintf("Pause not allowed.\n");
    } else {
        sv.paused ^= 1;
        if (sv.paused) SV_BroadcastPrintf("%s paused the game\n", PR_GetString(sv_player->v.netname));
        else SV_BroadcastPrintf("%s unpaused the game\n", PR_GetString(sv_player->v.netname));

        MSG_WriteByte(&sv.reliable_datagram, svc_setpause);
        MSG_WriteByte(&sv.reliable_datagram, sv.paused);
    }
}

void Host_PreSpawn_f()
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

void Host_Spawn_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Con_Printf("spawn is not valid from the console\n");
        return;
    }

    if (host_client->spawned) {
        Con_Printf("Spawn not valid -- allready spawned\n");
        return;
    }

    if (sv.loadgame) {
        sv.paused = false;
    } else {
        edict_t* ent = host_client->edict;
        memset(reinterpret_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4);
        ent->v.colormap = static_cast<float>(NUM_FOR_EDICT(ent));
        ent->v.team = static_cast<float>((host_client->colors & 15) + 1);
        ent->v.netname = PR_SetString(host_client->name.data());

        for (int i = 0; i < NUM_SPAWN_PARMS; i++) (&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];

        pr_global_struct->time = static_cast<float>(sv.time);
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv_player));
        PR_ExecuteProgram(pr_global_struct->ClientConnect);

        if ((Sys_FloatTime() - host_client->netconnection->connecttime) <= sv.time) {
            Sys_Printf("%s entered the game\n", host_client->name.data());
        }

        PR_ExecuteProgram(pr_global_struct->PutClientInServer);
    }

    SZ_Clear(&host_client->message);
    MSG_WriteByte(&host_client->message, svc_time);
    MSG_WriteFloat(&host_client->message, static_cast<float>(sv.time));

    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        MSG_WriteByte(&host_client->message, svc_updatename);
        MSG_WriteByte(&host_client->message, i);
        MSG_WriteString(&host_client->message, client->name.data());
        MSG_WriteByte(&host_client->message, svc_updatefrags);
        MSG_WriteByte(&host_client->message, i);
        MSG_WriteShort(&host_client->message, client->old_frags);
        MSG_WriteByte(&host_client->message, svc_updatecolors);
        MSG_WriteByte(&host_client->message, i);
        MSG_WriteByte(&host_client->message, client->colors);
    }

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        MSG_WriteByte(&host_client->message, svc_lightstyle);
        MSG_WriteByte(&host_client->message, static_cast<char>(i));
        MSG_WriteString(&host_client->message, sv.lightstyles[i]);
    }

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

    edict_t* ent = EDICT_NUM(1 + static_cast<int>(host_client - svs.clients));
    MSG_WriteByte(&host_client->message, svc_setangle);
    for (int i = 0; i < 2; i++) MSG_WriteAngle(&host_client->message, ent->v.angles[i]);
    MSG_WriteAngle(&host_client->message, 0);

    SV_WriteClientdataToMessage(sv_player, &host_client->message);

    MSG_WriteByte(&host_client->message, svc_signonnum);
    MSG_WriteByte(&host_client->message, 3);
    host_client->sendsignon = true;
}

void Host_Begin_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Con_Printf("begin is not valid from the console\n");
        return;
    }
    host_client->spawned = true;
}

void Host_Kick_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        if (!sv.active) {
            Cmd::ForwardToServer();
            return;
        }
    } else if (pr_global_struct->deathmatch && !host_client->privileged) return;

    client_t* save = host_client;
    bool byNumber = false;
    int i = 0;

    if (Cmd::Argc() > 2 && Q_strcmp(Cmd::Argv(1), "#") == 0) {
        i = static_cast<int>(Q_atof(Cmd::Argv(2)) - 1);
        if (i < 0 || i >= svs.maxclients || !svs.clients[i].active) return;
        host_client = &svs.clients[i];
        byNumber = true;
    } else {
        for (i = 0; i < svs.maxclients; i++) {
            host_client = &svs.clients[i];
            if (!host_client->active) continue;
            if (Q_strcasecmp(host_client->name.data(), Cmd::Argv(1)) == 0) break;
        }
    }

    if (i < svs.maxclients) {
        const char* who = (Cmd::state.source == Cmd::Source::Command)
            ? (cls.state == ca_dedicated ? "Console" : cl_name.string.c_str())
            : save->name.data();

        if (host_client == save) return;

        const char* message = nullptr;
        eastl::string args_holder;
        if (Cmd::Argc() > 2) {
            eastl::string_view args_sv = Cmd::Args();
            args_holder = eastl::string(args_sv.data(), args_sv.length());
            const char* ptr = args_holder.c_str();
            ptr = COM_Parse(ptr);
            if (byNumber) ptr = COM_Parse(ptr);
            while (*ptr == ' ') ptr++;
            if (*ptr != '\0') message = ptr;
        }

        if (message) SV_ClientPrintf("Kicked by %s: %s\n", who, message);
        else SV_ClientPrintf("Kicked by %s\n", who);

        SV_DropClient(false);
    }
    host_client = save;
}

void Host_Give_f()
{
    if (Cmd::state.source == Cmd::Source::Command) {
        Cmd::ForwardToServer();
        return;
    }

    if (pr_global_struct->deathmatch && !host_client->privileged) return;

    eastl::string_view t = Cmd::Argv(1);
    int v = Q_atoi(Cmd::Argv(2));
    if (t.empty()) return;

    switch (t[0]) {
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        if (hipnotic) {
            if (t[0] == '6') {
                if (t.size() > 1 && t[1] == 'a') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | HIT_PROXIMITY_GUN);
                else sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | IT_GRENADE_LAUNCHER);
            } else if (t[0] == '9') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | HIT_LASER_CANNON);
            else if (t[0] == '0') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | HIT_MJOLNIR);
            else if (t[0] >= '2') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | (IT_SHOTGUN << (t[0] - '2')));
        } else {
            if (t[0] >= '2') sv_player->v.items = static_cast<float>(static_cast<int>(sv_player->v.items) | (IT_SHOTGUN << (t[0] - '2')));
        }
        break;

    case 's':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_shells1");
            if (val) val->_float = static_cast<float>(v);
        }
        sv_player->v.ammo_shells = static_cast<float>(v);
        break;

    case 'n':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_nails1");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon <= IT_LIGHTNING) sv_player->v.ammo_nails = static_cast<float>(v);
            }
        } else sv_player->v.ammo_nails = static_cast<float>(v);
        break;

    case 'l':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_lava_nails");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon > IT_LIGHTNING) sv_player->v.ammo_nails = static_cast<float>(v);
            }
        }
        break;

    case 'r':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_rockets1");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon <= IT_LIGHTNING) sv_player->v.ammo_rockets = static_cast<float>(v);
            }
        } else sv_player->v.ammo_rockets = static_cast<float>(v);
        break;

    case 'm':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_multi_rockets");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon > IT_LIGHTNING) sv_player->v.ammo_rockets = static_cast<float>(v);
            }
        }
        break;

    case 'h': sv_player->v.health = static_cast<float>(v); break;

    case 'c':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_cells1");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon <= IT_LIGHTNING) sv_player->v.ammo_cells = static_cast<float>(v);
            }
        } else sv_player->v.ammo_cells = static_cast<float>(v);
        break;

    case 'p':
        if (rogue) {
            eval_t* val = GetEdictFieldValue(sv_player, "ammo_plasma");
            if (val) {
                val->_float = static_cast<float>(v);
                if (sv_player->v.weapon > IT_LIGHTNING) sv_player->v.ammo_cells = static_cast<float>(v);
            }
        }
        break;
    }
}

edict_t* FindViewthing()
{
    for (int i = 0; i < sv.num_edicts; i++) {
        edict_t* e = EDICT_NUM(i);
        if (strcmp(PR_GetString(e->v.classname), "viewthing") == 0) return e;
    }
    Con_Printf("No viewthing on map\n");
    return nullptr;
}

void Host_Viewmodel_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;

    eastl::string_view arg1 = Cmd::Argv(1);
    eastl::string arg1_str(arg1.data(), arg1.length());
    model_t* m = Mod_ForName(arg1_str.c_str(), false);
    if (!m) {
        Con_Printf("Can't load %.*s\n", static_cast<int>(arg1.length()), arg1.data());
        return;
    }

    e->v.frame = 0;
    cl.model_precache[static_cast<int>(e->v.modelindex)] = m;
}

void Host_Viewframe_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;

    model_t* m = cl.model_precache[static_cast<int>(e->v.modelindex)];
    int f = Q_atoi(Cmd::Argv(1));
    if (f >= m->numframes) f = m->numframes - 1;

    e->v.frame = static_cast<float>(f);
}

void PrintFrameName(model_t* m, int frame)
{
    aliashdr_t* hdr = static_cast<aliashdr_t*>(Mod_Extradata(m));
    if (!hdr) return;

    maliasframedesc_t* pframedesc = &hdr->frames[frame];
    Con_Printf("frame %i: %s\n", frame, pframedesc->name);
}

void Host_Viewnext_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;
    model_t* m = cl.model_precache[static_cast<int>(e->v.modelindex)];

    e->v.frame = e->v.frame + 1;
    if (e->v.frame >= m->numframes) e->v.frame = static_cast<float>(m->numframes - 1);

    PrintFrameName(m, static_cast<int>(e->v.frame));
}

void Host_Viewprev_f()
{
    edict_t* e = FindViewthing();
    if (!e) return;

    model_t* m = cl.model_precache[static_cast<int>(e->v.modelindex)];
    e->v.frame = e->v.frame - 1;
    if (e->v.frame < 0) e->v.frame = 0;

    PrintFrameName(m, static_cast<int>(e->v.frame));
}

void Host_Startdemos_f()
{
    if (cls.state == ca_dedicated) {
        if (!sv.active) Cmd::BufferAddText("map start\n");
        return;
    }

    int c = Cmd::Argc() - 1;
    if (c > MAX_DEMOS) {
        Con_Printf("Max %i demos in demoloop\n", MAX_DEMOS);
        c = MAX_DEMOS;
    }

    Con_Printf("%i demo(s) in loop\n", c);
    for (int i = 1; i < c + 1; i++) {
        eastl::string_view demo_arg = Cmd::Argv(i);
        Q_strncpy(cls.demos[i - 1], demo_arg, sizeof(cls.demos[0]) - 1);
    }

    if (!sv.active && cls.demonum != -1 && !cls.demoplayback) {
        cls.demonum = 0;
        CL_NextDemo();
    } else cls.demonum = -1;
}

void Host_Demos_f()
{
    if (cls.state == ca_dedicated) return;
    if (cls.demonum == -1) cls.demonum = 1;

    CL_Disconnect_f();
    CL_NextDemo();
}

void Host_Stopdemo_f()
{
    if (cls.state == ca_dedicated || !cls.demoplayback) return;
    CL_StopPlayback();
    CL_Disconnect();
}

void Host_InitCommands()
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

namespace Keys {

eastl::array<eastl::array<char, MAXCMDLINE>, 32> key_lines;
int key_linepos;
int edit_line = 0;
int history_line = 0;
eastl::array<char, 32> chat_buffer;
bool team_message = false;
int shift_down = false;
int key_lastpress;
keydest_t key_dest;
int key_count;

eastl::array<eastl::string, 256> keybindings;
eastl::array<bool, 256> consolekeys;
eastl::array<bool, 256> menubound;
eastl::array<int, 256> keyshift;
eastl::array<int, 256> key_repeats;
eastl::array<bool, 256> keydown;

struct keyname_t {
    const char* name;
    int keynum;
};

keyname_t keynames[] = {
    { "TAB", K_TAB }, { "ENTER", K_ENTER }, { "ESCAPE", K_ESCAPE }, { "SPACE", K_SPACE },
    { "BACKSPACE", K_BACKSPACE }, { "UPARROW", K_UPARROW }, { "DOWNARROW", K_DOWNARROW },
    { "LEFTARROW", K_LEFTARROW }, { "RIGHTARROW", K_RIGHTARROW },
    { "ALT", K_ALT }, { "CTRL", K_CTRL }, { "SHIFT", K_SHIFT },
    { "F1", K_F1 }, { "F2", K_F2 }, { "F3", K_F3 }, { "F4", K_F4 }, { "F5", K_F5 },
    { "F6", K_F6 }, { "F7", K_F7 }, { "F8", K_F8 }, { "F9", K_F9 }, { "F10", K_F10 },
    { "F11", K_F11 }, { "F12", K_F12 },
    { "INS", K_INS }, { "DEL", K_DEL }, { "PGDN", K_PGDN }, { "PGUP", K_PGUP },
    { "HOME", K_HOME }, { "END", K_END },
    { "MOUSE1", K_MOUSE1 }, { "MOUSE2", K_MOUSE2 }, { "MOUSE3", K_MOUSE3 },
    { "JOY1", K_JOY1 }, { "JOY2", K_JOY2 }, { "JOY3", K_JOY3 }, { "JOY4", K_JOY4 },
    { "AUX1", K_AUX1 }, { "AUX2", K_AUX2 }, { "AUX3", K_AUX3 }, { "AUX4", K_AUX4 },
    { "AUX5", K_AUX5 }, { "AUX6", K_AUX6 }, { "AUX7", K_AUX7 }, { "AUX8", K_AUX8 },
    { "AUX9", K_AUX9 }, { "AUX10", K_AUX10 }, { "AUX11", K_AUX11 }, { "AUX12", K_AUX12 },
    { "AUX13", K_AUX13 }, { "AUX14", K_AUX14 }, { "AUX15", K_AUX15 }, { "AUX16", K_AUX16 },
    { "AUX17", K_AUX17 }, { "AUX18", K_AUX18 }, { "AUX19", K_AUX19 }, { "AUX20", K_AUX20 },
    { "AUX21", K_AUX21 }, { "AUX22", K_AUX22 }, { "AUX23", K_AUX23 }, { "AUX24", K_AUX24 },
    { "AUX25", K_AUX25 }, { "AUX26", K_AUX26 }, { "AUX27", K_AUX27 }, { "AUX28", K_AUX28 },
    { "AUX29", K_AUX29 }, { "AUX30", K_AUX30 }, { "AUX31", K_AUX31 }, { "AUX32", K_AUX32 },
    { "PAUSE", K_PAUSE }, { "MWHEELUP", K_MWHEELUP }, { "MWHEELDOWN", K_MWHEELDOWN },
    { "SEMICOLON", ';' }, { nullptr, 0 }
};

void Key_Console(int key)
{
    if (key == K_ENTER) {
        Cmd::BufferAddText(key_lines[edit_line].data() + 1);
        Cmd::BufferAddText("\n");
        Con_Printf("%s\n", key_lines[edit_line].data());
        edit_line = (edit_line + 1) & 31;
        history_line = edit_line;
        key_lines[edit_line][0] = ']';
        key_linepos = 1;
        if (cls.state == ca_disconnected) Screen::GetScreenSystem().UpdateScreen();
        return;
    }

    if (key == K_TAB) {
        eastl::string_view cmd_view = Cmd::CompleteCommand(key_lines[edit_line].data() + 1);
        if (cmd_view.empty()) cmd_view = Cvar::CompleteVariable(key_lines[edit_line].data() + 1);

        if (!cmd_view.empty()) {
            eastl::string cmd_str(cmd_view.data(), cmd_view.length());
            Q_strcpy(key_lines[edit_line].data() + 1, cmd_str.c_str());
            key_linepos = Q_strlen(cmd_str.c_str()) + 1;
            key_lines[edit_line][key_linepos++] = ' ';
            key_lines[edit_line][key_linepos] = 0;
            return;
        }
    }

    if (key == K_BACKSPACE || key == K_LEFTARROW) {
        if (key_linepos > 1) key_linepos--;
        return;
    }

    if (key == K_UPARROW) {
        do { history_line = (history_line - 1) & 31; } while (history_line != edit_line && !key_lines[history_line][1]);
        if (history_line == edit_line) history_line = (edit_line + 1) & 31;
        Q_strcpy(key_lines[edit_line].data(), key_lines[history_line].data());
        key_linepos = Q_strlen(key_lines[edit_line].data());
        return;
    }

    if (key == K_DOWNARROW) {
        if (history_line == edit_line) return;
        do { history_line = (history_line + 1) & 31; } while (history_line != edit_line && !key_lines[history_line][1]);
        if (history_line == edit_line) {
            key_lines[edit_line][0] = ']';
            key_linepos = 1;
        } else {
            Q_strcpy(key_lines[edit_line].data(), key_lines[history_line].data());
            key_linepos = Q_strlen(key_lines[edit_line].data());
        }
        return;
    }

    auto& con = GetConsoleSystem();
    if (key == K_PGUP || key == K_MWHEELUP) {
        con.SetBackscroll(con.GetBackscroll() + 2);
        int max_scroll = con.GetTotalLines() - (int)(vid.height >> 3) - 1;
        if (con.GetBackscroll() > max_scroll) con.SetBackscroll(max_scroll);
        return;
    }

    if (key == K_PGDN || key == K_MWHEELDOWN) {
        con.SetBackscroll(con.GetBackscroll() - 2);
        if (con.GetBackscroll() < 0) con.SetBackscroll(0);
        return;
    }

    if (key == K_HOME) { con.SetBackscroll(con.GetTotalLines() - (vid.height >> 3) - 1); return; }
    if (key == K_END) { con.SetBackscroll(0); return; }

    if (key < 32 || key > 127) return;

    if (key_linepos < MAXCMDLINE - 1) {
        key_lines[edit_line][key_linepos++] = static_cast<char>(key);
        key_lines[edit_line][key_linepos] = 0;
    }
}

void Key_Message(int key)
{
    static int chat_bufferlen = 0;

    if (key == K_ENTER) {
        if (team_message) Cmd::BufferAddText("say_team \"");
        else Cmd::BufferAddText("say \"");
        Cmd::BufferAddText(chat_buffer.data());
        Cmd::BufferAddText("\"\n");

        key_dest = key_game;
        chat_bufferlen = 0;
        chat_buffer[0] = 0;
        return;
    }

    if (key == K_ESCAPE) {
        key_dest = key_game;
        chat_bufferlen = 0;
        chat_buffer[0] = 0;
        return;
    }

    if (key < 32 || key > 127) return;

    if (key == K_BACKSPACE) {
        if (chat_bufferlen) {
            chat_bufferlen--;
            chat_buffer[chat_bufferlen] = 0;
        }
        return;
    }

    if (chat_bufferlen == 31) return;
    chat_buffer[chat_bufferlen++] = static_cast<char>(key);
    chat_buffer[chat_bufferlen] = 0;
}

int Key_StringToKeynum(eastl::string_view str)
{
    if (str.empty()) return -1;
    if (str.size() == 1) return str[0];

    for (keyname_t* kn = keynames; kn->name; kn++) {
        if (!Q_strcasecmp(str, kn->name)) return kn->keynum;
    }
    return -1;
}

const char* Key_KeynumToString(int keynum)
{
    static char tinystr[2];
    if (keynum == -1) return "<KEY NOT FOUND>";
    if (keynum > 32 && keynum < 127) {
        tinystr[0] = static_cast<char>(keynum);
        tinystr[1] = 0;
        return tinystr;
    }

    for (keyname_t* kn = keynames; kn->name; kn++) {
        if (keynum == kn->keynum) return kn->name;
    }
    return "<UNKNOWN KEYNUM>";
}

void Key_SetBinding(int keynum, const char* binding)
{
    if (keynum < 0 || keynum >= 256) return;
    keybindings[keynum] = binding;
}

void Key_Unbind_f()
{
    if (Cmd::Argc() != 2) { Con_Printf("unbind <key> : remove commands from a key\n"); return; }
    int b = Key_StringToKeynum(Cmd::Argv(1));
    if (b == -1) { Con_Printf("\"%s\" isn't a valid key\n", Cmd::Argv(1)); return; }
    Key_SetBinding(b, "");
}

void Key_Unbindall_f()
{
    for (int i = 0; i < 256; i++) {
        if (!keybindings[i].empty()) keybindings[i].clear();
    }
}

void Key_Bind_f()
{
    int c = Cmd::Argc();
    if (c != 2 && c != 3) { Con_Printf("bind <key> [command] : attach a command to a key\n"); return; }

    int b = Key_StringToKeynum(Cmd::Argv(1));
    if (b == -1) { Con_Printf("\"%s\" isn't a valid key\n", Cmd::Argv(1)); return; }

    if (c == 2) {
        if (!keybindings[b].empty()) Con_Printf("\"%s\" = \"%s\"\n", Cmd::Argv(1), keybindings[b].c_str());
        else Con_Printf("\"%s\" is not bound\n", Cmd::Argv(1));
        return;
    }

    char cmd[1024] = "";
    for (int i = 2; i < c; i++) {
        if (i > 2) Q_strcat(cmd, " ");
        Q_strcat(cmd, Cmd::Argv(i));
    }
    Key_SetBinding(b, cmd);
}

void Key_WriteBindings(std::ostream& f)
{
    for (int i = 0; i < 256; i++) {
        if (!keybindings[i].empty()) {
            f << "bind \"" << Key_KeynumToString(i) << "\" \"" << keybindings[i].c_str() << "\"\n";
        }
    }
}

void Key_Init()
{
    int i;
    for (i = 0; i < 32; i++) {
        key_lines[i][0] = ']';
        key_lines[i][1] = 0;
    }
    key_linepos = 1;

    for (i = 32; i < 128; i++) consolekeys[i] = true;
    consolekeys[K_ENTER] = true; consolekeys[K_TAB] = true;
    consolekeys[K_LEFTARROW] = true; consolekeys[K_RIGHTARROW] = true;
    consolekeys[K_UPARROW] = true; consolekeys[K_DOWNARROW] = true;
    consolekeys[K_BACKSPACE] = true; consolekeys[K_PGUP] = true;
    consolekeys[K_PGDN] = true; consolekeys[K_SHIFT] = true;
    consolekeys[K_MWHEELUP] = true; consolekeys[K_MWHEELDOWN] = true;
    consolekeys['`'] = false; consolekeys['~'] = false;

    for (i = 0; i < 256; i++) keyshift[i] = i;
    for (i = 'a'; i <= 'z'; i++) keyshift[i] = i - 'a' + 'A';
    keyshift['1'] = '!'; keyshift['2'] = '@'; keyshift['3'] = '#'; keyshift['4'] = '$';
    keyshift['5'] = '%'; keyshift['6'] = '^'; keyshift['7'] = '&'; keyshift['8'] = '*';
    keyshift['9'] = '('; keyshift['0'] = ')'; keyshift['-'] = '_'; keyshift['='] = '+';
    keyshift[','] = '<'; keyshift['.'] = '>'; keyshift['/'] = '?'; keyshift[';'] = ':';
    keyshift['\''] = '"'; keyshift['['] = '{'; keyshift[']'] = '}'; keyshift['`'] = '~';
    keyshift['\\'] = '|';

    menubound[K_ESCAPE] = true;
    for (i = 0; i < 12; i++) menubound[K_F1 + i] = true;

    Cmd::AddCommand("bind", Key_Bind_f);
    Cmd::AddCommand("unbind", Key_Unbind_f);
    Cmd::AddCommand("unbindall", Key_Unbindall_f);
}

void Key_Event(int key, bool down)
{
    char cmd[1024];
    keydown[key] = down;

    if (!down) key_repeats[key] = 0;

    key_lastpress = key;
    key_count++;
    if (key_count <= 0) return;

    if (down) {
        key_repeats[key]++;
        if (key != K_BACKSPACE && key != K_PAUSE && key_repeats[key] > 1) return;
        if (key >= 200 && keybindings[key].empty()) {
            Con_Printf("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key));
        }
    }

    if (key == K_SHIFT) shift_down = down;

    if (key == K_ESCAPE) {
        if (!down) return;
        switch (key_dest) {
        case key_message: Key_Message(key); break;
        case key_menu: M_Keydown(key); break;
        case key_game: case key_console: M_ToggleMenu_f(); break;
        default: Sys_Error("Bad key_dest");
        }
        return;
    }

    if (!down) {
        const auto& kb = keybindings[key];
        if (!kb.empty() && kb[0] == '+') {
            sprintf_s(cmd, sizeof(cmd), "-%s %i\n", kb.c_str() + 1, key);
            Cmd::BufferAddText(cmd);
        }

        if (keyshift[key] != key) {
            const auto& kb_shift = keybindings[keyshift[key]];
            if (!kb_shift.empty() && kb_shift[0] == '+') {
                sprintf_s(cmd, sizeof(cmd), "-%s %i\n", kb_shift.c_str() + 1, key);
                Cmd::BufferAddText(cmd);
            }
        }
        return;
    }

    if (cls.demoplayback && down && consolekeys[key] && key_dest == key_game) {
        M_ToggleMenu_f();
        return;
    }

    if ((key_dest == key_menu && menubound[key]) || (key_dest == key_console && !consolekeys[key]) || (key_dest == key_game && (!GetConsoleSystem().IsForcedUp() || !consolekeys[key]))) {
        const auto& kb = keybindings[key];
        if (!kb.empty()) {
            if (kb[0] == '+') {
                sprintf_s(cmd, sizeof(cmd), "%s %i\n", kb.c_str(), key);
                Cmd::BufferAddText(cmd);
            } else {
                Cmd::BufferAddText(kb.c_str());
                Cmd::BufferAddText("\n");
            }
        }
        return;
    }

    if (!down) return;

    if (shift_down) key = keyshift[key];

    switch (key_dest) {
    case key_message: Key_Message(key); break;
    case key_menu: M_Keydown(key); break;
    case key_game: case key_console: Key_Console(key); break;
    default: Sys_Error("Bad key_dest");
    }
}

} // namespace Keys

namespace Console {

constexpr int CON_TEXTSIZE = 16384;
constexpr int NUM_CON_TIMES = 4;
constexpr int MAXPRINTMSG = 4096;

static cvar_t con_notifytime = { "con_notifytime", "3", {}, {}, {}, {} };

ConsoleSystem::ConsoleSystem()
    : text_(CON_TEXTSIZE, ' '), times_(NUM_CON_TIMES, 0.0f)
{}

ConsoleSystem& GetConsoleSystem()
{
    static ConsoleSystem instance;
    return instance;
}

void ConsoleSystem::ToggleConsole()
{
    if (key_dest == key_console) {
        if (cls.state == ca_connected) {
            key_dest = key_game;
            key_lines[edit_line][1] = 0;
            key_linepos = 1;
        } else M_Menu_Main_f();
    } else key_dest = key_console;

    Screen::GetScreenSystem().EndLoadingPlaque();
    eastl::fill(times_.begin(), times_.end(), 0.0f);
}

void ConsoleSystem::Clear()
{
    eastl::fill(text_.begin(), text_.end(), ' ');
}

void ConsoleSystem::ClearNotify()
{
    eastl::fill(times_.begin(), times_.end(), 0.0f);
}

static void Con_MessageMode_f(void)
{
    key_dest = key_message;
    team_message = false;
}

static void Con_MessageMode2_f(void)
{
    key_dest = key_message;
    team_message = true;
}

void ConsoleSystem::CheckResize()
{
    int width = (vid.width >> 3) - 2;

    if (width == linewidth_) return;

    if (width < 1) {
        linewidth_ = 38;
        totallines_ = CON_TEXTSIZE / linewidth_;
        eastl::fill(text_.begin(), text_.end(), ' ');
    } else {
        int oldwidth = linewidth_;
        linewidth_ = width;
        int oldtotallines = totallines_;
        totallines_ = CON_TEXTSIZE / linewidth_;
        int numlines = oldtotallines;

        if (totallines_ < numlines) numlines = totallines_;

        int numchars = oldwidth;
        if (linewidth_ < numchars) numchars = linewidth_;

        eastl::vector<char> tbuf = text_;
        eastl::fill(text_.begin(), text_.end(), ' ');

        for (int i = 0; i < numlines; i++) {
            for (int j = 0; j < numchars; j++) {
                text_[(totallines_ - 1 - i) * linewidth_ + j] = 
                    tbuf[((current_ - i + oldtotallines) % oldtotallines) * oldwidth + j];
            }
        }

        ClearNotify();
    }

    backscroll_ = 0;
    current_ = totallines_ - 1;
}

void ConsoleSystem::Init()
{
    debuglog_ = COM_CheckParm("-condebug") != 0;

    if (debuglog_) {
        eastl::string log_path = eastl::string(com_gamedir) + "/qconsole.log";
        std::error_code ec;
        std::filesystem::remove(log_path.c_str(), ec);
    }

    eastl::fill(text_.begin(), text_.end(), ' ');
    linewidth_ = -1;
    CheckResize();

    Printf("Console initialized.\n");

    Cvar::Register(&con_notifytime);

    Cmd::AddCommand("toggleconsole", ConsoleSystem::ToggleConsole_f);
    Cmd::AddCommand("messagemode", Con_MessageMode_f);
    Cmd::AddCommand("messagemode2", Con_MessageMode2_f);
    Cmd::AddCommand("clear", ConsoleSystem::Clear_f);
    initialized_ = true;
}

void ConsoleSystem::Linefeed()
{
    if (!initialized_) return;
    x_ = 0;
    current_++;
    eastl::fill(text_.begin() + (current_ % totallines_) * linewidth_,
              text_.begin() + (current_ % totallines_) * linewidth_ + linewidth_,
              ' ');
}

void ConsoleSystem::Print(eastl::string_view txt)
{
    if (!initialized_) return;
    backscroll_ = 0;

    int mask = 0;
    size_t index = 0;
    if (!txt.empty() && txt[0] == 1) {
        mask = 128;
        S_LocalSound("misc/talk.wav");
        index = 1;
    } else if (!txt.empty() && txt[0] == 2) {
        mask = 128;
        index = 1;
    }

    static bool cr = false;

    while (index < txt.length()) {
        char c = txt[index];

        int l;
        for (l = 0; l < linewidth_; l++) {
            if (index + l >= txt.length() || txt[index + l] <= ' ') break;
        }

        if (l != linewidth_ && (x_ + l > linewidth_)) x_ = 0;

        index++;

        if (cr) {
            current_--;
            cr = false;
        }

        if (x_ == 0) {
            Linefeed();
            if (current_ >= 0) times_[current_ % NUM_CON_TIMES] = static_cast<float>(realtime);
        }

        switch (c) {
        case '\n': x_ = 0; break;
        case '\r': x_ = 0; cr = true; break;
        default:
            int y = current_ % totallines_;
            text_[y * linewidth_ + x_] = static_cast<char>(c | mask);
            x_++;
            if (x_ >= linewidth_) x_ = 0;
            break;
        }
    }
}

void ConsoleSystem::DebugLog(eastl::string_view file, eastl::string_view text)
{
    std::ofstream log_file(file.data(), std::ios::app | std::ios::binary);
    if (log_file) log_file.write(text.data(), text.size());
}

void ConsoleSystem::Printf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];
    static bool inupdate = false;

    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Sys_Printf("%s", msg);

    if (debuglog_) {
        eastl::string log_file_path = eastl::string(com_gamedir) + "/qconsole.log";
        DebugLog(log_file_path.c_str(), msg);
    }

    if (!initialized_ || cls.state == ca_dedicated) return;

    Print(msg);

    if (cls.signon != SIGNONS && !Screen::GetScreenSystem().GetDisabledForLoading()) {
        if (!inupdate) {
            inupdate = true;
            Screen::GetScreenSystem().UpdateScreen();
            inupdate = false;
        }
    }
}

void ConsoleSystem::DPrintf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];

    if (!developer.value) return;

    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Printf("%s", msg);
}

void ConsoleSystem::DrawInput()
{
    if (key_dest != key_console && !forcedup_) return;

    char* text = key_lines[edit_line].data();
    text[key_linepos] = static_cast<char>(10 + ((int)(realtime * cursorspeed_) & 1));

    for (int i = key_linepos + 1; i < linewidth_; i++) text[i] = ' ';

    char* text_ptr = text;
    if (key_linepos >= linewidth_) text_ptr += 1 + key_linepos - linewidth_;

    for (int i = 0; i < linewidth_; i++) {
        Draw_Character((i + 1) << 3, vislines_ - 16, text_ptr[i]);
    }

    key_lines[edit_line][key_linepos] = 0;
}

void ConsoleSystem::DrawNotify()
{
    int v = 0;
    for (int i = current_ - NUM_CON_TIMES + 1; i <= current_; i++) {
        if (i < 0) continue;

        float time = times_[i % NUM_CON_TIMES];
        if (time == 0.0f) continue;

        time = static_cast<float>(realtime - time);
        if (time > con_notifytime.value) continue;

        char* text_ptr = text_.data() + (i % totallines_) * linewidth_;

        Screen::GetScreenSystem().SetClearnotify(0);
        Screen::GetScreenSystem().SetCopytop(1);

        for (int x = 0; x < linewidth_; x++) Draw_Character((x + 1) << 3, v, text_ptr[x]);
        v += 8;
    }

    if (key_dest == key_message) {
        Screen::GetScreenSystem().SetClearnotify(0);
        Screen::GetScreenSystem().SetCopytop(1);

        int x = 0;
        Draw_String(8, v, "say:");
        while (chat_buffer[x]) {
            Draw_Character((x + 5) << 3, v, chat_buffer[x]);
            x++;
        }
        Draw_Character((x + 5) << 3, v, static_cast<char>(10 + ((int)(realtime * cursorspeed_) & 1)));
        v += 8;
    }

    if (v > notifylines_) notifylines_ = v;
}

void ConsoleSystem::DrawConsole(int lines, bool drawinput)
{
    if (lines <= 0) return;
    Draw_ConsoleBackground(lines);

    vislines_ = lines;
    int rows = (lines - 16) >> 3;
    int y = lines - 16 - (rows << 3);

    for (int i = current_ - rows + 1; i <= current_; i++, y += 8) {
        int j = i - backscroll_;
        if (j < 0) j = 0;

        char* text_ptr = text_.data() + (j % totallines_) * linewidth_;
        for (int x = 0; x < linewidth_; x++) Draw_Character((x + 1) << 3, y, text_ptr[x]);
    }

    if (drawinput) DrawInput();
}

void ConsoleSystem::Clear_f() { GetConsoleSystem().Clear(); }
void ConsoleSystem::ToggleConsole_f() { GetConsoleSystem().ToggleConsole(); }

void Con_Printf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];
    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    GetConsoleSystem().Printf("%s", msg);
}

void Con_DPrintf(const char* fmt, ...)
{
    va_list argptr;
    char msg[MAXPRINTMSG];
    va_start(argptr, fmt);
    vsprintf_s(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    GetConsoleSystem().DPrintf("%s", msg);
}

} // namespace Console

namespace Vid {
void (*vid_menudrawfn)() = nullptr;
void (*vid_menukeyfn)(int key) = nullptr;
}

namespace Menu {

MenuState m_state = MenuState::None;

void M_Menu_Main_f();
void M_Menu_SinglePlayer_f();
void M_Menu_Load_f();
void M_Menu_Save_f();
void M_Menu_MultiPlayer_f();
void M_Menu_Setup_f();
void M_Menu_Net_f();
void M_Menu_Options_f();
void M_Menu_Keys_f();
void M_Menu_Video_f();
void M_Menu_Help_f();
void M_Menu_Quit_f();
void M_Menu_SerialConfig_f();
void M_Menu_ModemConfig_f();
void M_Menu_LanConfig_f();
void M_Menu_GameOptions_f();
void M_Menu_Search_f();
void M_Menu_ServerList_f();

void M_Main_Draw();
void M_SinglePlayer_Draw();
void M_Load_Draw();
void M_Save_Draw();
void M_MultiPlayer_Draw();
void M_Setup_Draw();
void M_Net_Draw();
void M_Options_Draw();
void M_Keys_Draw();
void M_Video_Draw();
void M_Help_Draw();
void M_SerialConfig_Draw();
void M_ModemConfig_Draw();
void M_LanConfig_Draw();
void M_GameOptions_Draw();
void M_Search_Draw();
void M_ServerList_Draw();

void M_Main_Key(int key);
void M_SinglePlayer_Key(int key);
void M_Load_Key(int key);
void M_Save_Key(int key);
void M_MultiPlayer_Key(int key);
void M_Setup_Key(int key);
void M_Net_Key(int key);
void M_Options_Key(int key);
void M_Keys_Key(int key);
void M_Video_Key(int key);
void M_Help_Key(int key);
void M_SerialConfig_Key(int key);
void M_ModemConfig_Key(int key);
void M_LanConfig_Key(int key);
void M_GameOptions_Key(int key);
void M_Search_Key();
void M_ServerList_Key(int key);

bool m_entersound; // play after drawing a frame, so caching won't disrupt the sound
bool m_recursiveDraw;

MenuState m_return_state = MenuState::None;
bool m_return_onerror = false;
eastl::string m_return_reason;

int m_multiplayer_cursor = 0;
int m_net_cursor = 0;

inline bool StartingGame() { return m_multiplayer_cursor == 1; }
inline bool JoiningGame() { return m_multiplayer_cursor == 0; }
inline bool SerialConfig() { return m_net_cursor == 0; }
inline bool DirectConfig() { return m_net_cursor == 1; }
inline bool IPXConfig() { return m_net_cursor == 2; }
inline bool TCPIPConfig() { return m_net_cursor == 3; }

void M_ConfigureNetSubsystem();

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
inline void M_DrawCharacter(int cx, int line, int num)
{
    Draw_Character(cx + ((vid.width - 320) >> 1), line, num);
}

void M_Print(int cx, int cy, eastl::string_view str)
{
    for (char c : str) {
        M_DrawCharacter(cx, cy, static_cast<unsigned char>(c) + 128);
        cx += 8;
    }
}

void M_PrintWhite(int cx, int cy, eastl::string_view str)
{
    for (char c : str) {
        M_DrawCharacter(cx, cy, static_cast<unsigned char>(c));
        cx += 8;
    }
}

inline void M_DrawTransPic(int x, int y, qpic_t* pic)
{
    Draw_TransPic(x + ((vid.width - 320) >> 1), y, pic);
}

void M_DrawPic(int x, int y, qpic_t* pic)
{
    Draw_Pic(x + ((vid.width - 320) >> 1), y, pic);
}

eastl::array<byte, 256> identityTable;
eastl::array<byte, 256> translationTable;

void M_BuildTranslationTable(int top, int bottom)
{
    for (int j = 0; j < 256; j++) {
        identityTable[j] = static_cast<byte>(j);
    }
    translationTable = identityTable;

    if (top < 128) { // the artists made some backwards ranges.  sigh.
        eastl::copy_n(identityTable.begin() + top, 16, translationTable.begin() + TOP_RANGE);
    } else {
        for (int j = 0; j < 16; j++) {
            translationTable[TOP_RANGE + j] = identityTable[top + 15 - j];
        }
    }

    if (bottom < 128) {
        eastl::copy_n(identityTable.begin() + bottom, 16, translationTable.begin() + BOTTOM_RANGE);
    } else {
        for (int j = 0; j < 16; j++) {
            translationTable[BOTTOM_RANGE + j] = identityTable[bottom + 15 - j];
        }
    }
}

inline void M_DrawTransPicTranslate(int x, int y, qpic_t* pic)
{
    Draw_TransPicTranslate(x + ((vid.width - 320) >> 1), y, pic,
        translationTable.data());
}

inline void M_DrawTextBox(int x, int y, int width, int lines)
{
    qpic_t* p;
    int cx, cy;
    int n;

    // draw left side
    cx = x;
    cy = y;
    p = Draw_CachePic("gfx/box_tl.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_ml.lmp");
    for (n = 0; n < lines; n++) {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_bl.lmp");
    M_DrawTransPic(cx, cy + 8, p);

    // draw middle
    cx += 8;
    while (width > 0) {
        cy = y;
        p = Draw_CachePic("gfx/box_tm.lmp");
        M_DrawTransPic(cx, cy, p);
        p = Draw_CachePic("gfx/box_mm.lmp");
        for (n = 0; n < lines; n++) {
            cy += 8;
            if (n == 1) {
                p = Draw_CachePic("gfx/box_mm2.lmp");
            }

            M_DrawTransPic(cx, cy, p);
        }
        p = Draw_CachePic("gfx/box_bm.lmp");
        M_DrawTransPic(cx, cy + 8, p);
        width -= 2;
        cx += 16;
    }

    // draw right side
    cy = y;
    p = Draw_CachePic("gfx/box_tr.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_mr.lmp");
    for (n = 0; n < lines; n++) {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_br.lmp");
    M_DrawTransPic(cx, cy + 8, p);
}

//=============================================================================

int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu_f()
{
    m_entersound = true;

    if (key_dest == key_menu) {
        if (m_state != MenuState::Main) {
            M_Menu_Main_f();

            return;
        }

        key_dest = key_game;
        m_state = MenuState::None;

        return;
    }

    if (key_dest == key_console) {
        ConsoleSystem::ToggleConsole_f();
    } else {
        M_Menu_Main_f();
    }
}

//=============================================================================
/* MAIN MENU */

int m_main_cursor;
constexpr int MAIN_ITEMS = 5;

void M_Menu_Main_f()
{
    if (key_dest != key_menu) {
        m_save_demonum = cls.demonum;
        cls.demonum = -1;
    }

    key_dest = key_menu;
    m_state = MenuState::Main;
    m_entersound = true;
}

void M_Main_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_main.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mainmenu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + m_main_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_Main_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        key_dest = key_game;
        m_state = MenuState::None;
        cls.demonum = m_save_demonum;
        if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected) {
            CL_NextDemo();
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_main_cursor >= MAIN_ITEMS) {
            m_main_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_main_cursor < 0) {
            m_main_cursor = MAIN_ITEMS - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;

        switch (m_main_cursor) {
        case 0:
            M_Menu_SinglePlayer_f();
            break;

        case 1:
            M_Menu_MultiPlayer_f();
            break;

        case 2:
            M_Menu_Options_f();
            break;

        case 3:
            M_Menu_Help_f();
            break;

        case 4:
            M_Menu_Quit_f();
            break;
        }
    }
}

//=============================================================================
/* SINGLE PLAYER MENU */

int m_singleplayer_cursor;
constexpr int SINGLEPLAYER_ITEMS = 3;

void M_Menu_SinglePlayer_f()
{
    key_dest = key_menu;
    m_state = MenuState::SinglePlayer;
    m_entersound = true;
}

void M_SinglePlayer_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_sgl.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/sp_menu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + m_singleplayer_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_SinglePlayer_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS) {
            m_singleplayer_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_singleplayer_cursor < 0) {
            m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;

        switch (m_singleplayer_cursor) {
        case 0:
            if (sv.active) {
                if (!Screen::GetScreenSystem().ModalMessage(
                        "Are you sure you want to\nstart a new game?\n")) {
                    break;
                }
            }

            key_dest = key_game;
            if (sv.active) {
                Cmd::BufferAddText("disconnect\n");
            }

            Cmd::BufferAddText("maxplayers 1\n");
            Cmd::BufferAddText("map start\n");
            break;

        case 1:
            M_Menu_Load_f();
            break;

        case 2:
            M_Menu_Save_f();
            break;
        }
    }
}

//=============================================================================
/* LOAD/SAVE MENU */

int load_cursor; // 0 < load_cursor < MAX_SAVEGAMES

constexpr int MAX_SAVEGAMES = 12;
eastl::array<eastl::string, MAX_SAVEGAMES> m_filenames;
eastl::array<bool, MAX_SAVEGAMES> loadable;

void M_ScanSaves()
{
    for (int i = 0; i < MAX_SAVEGAMES; i++) {
        m_filenames[i] = "--- UNUSED SLOT ---";
        loadable[i] = false;
        char name[MAX_OSPATH];
        sprintf_s(name, sizeof(name), "%s/s%i.sav", com_gamedir, i);
        std::ifstream f(name);
        if (!f.is_open()) {
            continue;
        }

        int version = 0;
        if (!(f >> version)) {
            continue;
        }
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::string comment;
        if (!std::getline(f, comment)) {
            continue;
        }

        if (!comment.empty() && comment.back() == '\r') {
            comment.pop_back();
        }

        for (char& c : comment) {
            if (c == '_') {
                c = ' ';
            }
        }

        if (comment.length() > SAVEGAME_COMMENT_LENGTH) {
            comment = comment.substr(0, SAVEGAME_COMMENT_LENGTH);
        }

        m_filenames[i] = comment.c_str();
        loadable[i] = true;
    }
}

void M_Menu_Load_f()
{
    m_entersound = true;
    m_state = MenuState::Load;
    key_dest = key_menu;
    M_ScanSaves();
}

void M_Menu_Save_f()
{
    if (!sv.active) {
        return;
    }

    if (cl.intermission) {
        return;
    }

    if (svs.maxclients != 1) {
        return;
    }

    m_entersound = true;
    m_state = MenuState::Save;
    key_dest = key_menu;
    M_ScanSaves();
}

void M_Load_Draw()
{
    int i;
    qpic_t* p;

    p = Draw_CachePic("gfx/p_load.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < MAX_SAVEGAMES; i++) {
        M_Print(16, 32 + 8 * i, m_filenames[i]);
    }

    // line cursor
    M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Save_Draw()
{
    int i;
    qpic_t* p;

    p = Draw_CachePic("gfx/p_save.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < MAX_SAVEGAMES; i++) {
        M_Print(16, 32 + 8 * i, m_filenames[i]);
    }

    // line cursor
    M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Load_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_SinglePlayer_f();
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        if (!loadable[load_cursor]) {
            return;
        }

        m_state = MenuState::None;
        key_dest = key_game;

        // Host_Loadgame_f can't bring up the loading plaque because too much
        // stack space has been used, so do it now
        Screen::GetScreenSystem().BeginLoadingPlaque();

        // issue the load command
        Cmd::BufferAddText(va("load s%i\n", load_cursor));

        return;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor--;
        if (load_cursor < 0) {
            load_cursor = MAX_SAVEGAMES - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor++;
        if (load_cursor >= MAX_SAVEGAMES) {
            load_cursor = 0;
        }

        break;
    }
}

void M_Save_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_SinglePlayer_f();
        break;

    case K_ENTER:
        m_state = MenuState::None;
        key_dest = key_game;
        Cmd::BufferAddText(va("save s%i\n", load_cursor));

        return;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor--;
        if (load_cursor < 0) {
            load_cursor = MAX_SAVEGAMES - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor++;
        if (load_cursor >= MAX_SAVEGAMES) {
            load_cursor = 0;
        }

        break;
    }
}

//=============================================================================
/* MULTIPLAYER MENU */

constexpr int MULTIPLAYER_ITEMS = 3;

void M_Menu_MultiPlayer_f()
{
    key_dest = key_menu;
    m_state = MenuState::MultiPlayer;
    m_entersound = true;
}

void M_MultiPlayer_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mp_menu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + m_multiplayer_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));

    if (serialAvailable || ipxAvailable || tcpipAvailable) {
        return;
    }

    M_PrintWhite((320 / 2) - ((27 * 8) / 2), 148, "No Communications Available");
}

void M_MultiPlayer_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS) {
            m_multiplayer_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_multiplayer_cursor < 0) {
            m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;
        switch (m_multiplayer_cursor) {
        case 0:
            if (serialAvailable || ipxAvailable || tcpipAvailable) {
                M_Menu_Net_f();
            }

            break;

        case 1:
            if (serialAvailable || ipxAvailable || tcpipAvailable) {
                M_Menu_Net_f();
            }

            break;

        case 2:
            M_Menu_Setup_f();
            break;
        }
    }
}

//=============================================================================
/* SETUP MENU */

int setup_cursor = 4;
const std::array<int, 5> setup_cursor_table = { 40, 56, 80, 104, 140 };

eastl::string setup_hostname;
eastl::string setup_myname;
int setup_oldtop;
int setup_oldbottom;
int setup_top;
int setup_bottom;

constexpr int NUM_SETUP_CMDS = 5;

void M_Menu_Setup_f()
{
    key_dest = key_menu;
    m_state = MenuState::Setup;
    m_entersound = true;
    setup_myname = cl_name.string;
    setup_hostname = hostname.string;
    setup_top = setup_oldtop = ((int)cl_color.value) >> 4;
    setup_bottom = setup_oldbottom = ((int)cl_color.value) & 15;
}

void M_Setup_Draw()
{
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(64, 40, "Hostname");
    M_DrawTextBox(160, 32, 16, 1);
    M_Print(168, 40, setup_hostname.c_str());

    M_Print(64, 56, "Your name");
    M_DrawTextBox(160, 48, 16, 1);
    M_Print(168, 56, setup_myname.c_str());

    M_Print(64, 80, "Shirt color");
    M_Print(64, 104, "Pants color");

    M_DrawTextBox(64, 140 - 8, 14, 1);
    M_Print(72, 140, "Accept Changes");

    p = Draw_CachePic("gfx/bigbox.lmp");
    M_DrawTransPic(160, 64, p);
    p = Draw_CachePic("gfx/menuplyr.lmp");
    M_BuildTranslationTable(setup_top * 16, setup_bottom * 16);
    M_DrawTransPicTranslate(172, 72, p);

    M_DrawCharacter(56, setup_cursor_table[setup_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (setup_cursor == 0) {
        M_DrawCharacter(168 + 8 * (int)setup_hostname.length(),
            setup_cursor_table[setup_cursor],
            10 + ((int)(realtime * 4) & 1));
    }

    if (setup_cursor == 1) {
        M_DrawCharacter(168 + 8 * (int)setup_myname.length(),
            setup_cursor_table[setup_cursor],
            10 + ((int)(realtime * 4) & 1));
    }
}

void M_Setup_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_MultiPlayer_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        setup_cursor--;
        if (setup_cursor < 0) {
            setup_cursor = NUM_SETUP_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        setup_cursor++;
        if (setup_cursor >= NUM_SETUP_CMDS) {
            setup_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        if (setup_cursor < 2) {
            return;
        }

        S_LocalSound("misc/menu3.wav");
        if (setup_cursor == 2) {
            setup_top = setup_top - 1;
        }

        if (setup_cursor == 3) {
            setup_bottom = setup_bottom - 1;
        }

        break;
    case K_RIGHTARROW:
        if (setup_cursor < 2) {
            return;
        }

    forward:
        S_LocalSound("misc/menu3.wav");
        if (setup_cursor == 2) {
            setup_top = setup_top + 1;
        }

        if (setup_cursor == 3) {
            setup_bottom = setup_bottom + 1;
        }

        break;

    case K_ENTER:
        if (setup_cursor == 0 || setup_cursor == 1) {
            return;
        }

        if (setup_cursor == 2 || setup_cursor == 3) {
            goto forward;
        }

        // setup_cursor == 4 (OK)
        if (cl_name.string != setup_myname) {
            Cmd::BufferAddText(va("name \"%s\"\n", setup_myname.c_str()));
        }

        if (hostname.string != setup_hostname) {
            Cvar::Set("hostname", setup_hostname.c_str());
        }

        if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom) {
            Cmd::BufferAddText(va("color %i %i\n", setup_top, setup_bottom));
        }

        m_entersound = true;
        M_Menu_MultiPlayer_f();
        break;

    case K_BACKSPACE:
        if (setup_cursor == 0) {
            if (!setup_hostname.empty()) {
                setup_hostname.pop_back();
            }
        }

        if (setup_cursor == 1) {
            if (!setup_myname.empty()) {
                setup_myname.pop_back();
            }
        }

        break;

    default:
        if (k < 32 || k > 127) {
            break;
        }

        if (setup_cursor == 0) {
            if (setup_hostname.length() < 15) {
                setup_hostname.push_back(static_cast<char>(k));
            }
        }

        if (setup_cursor == 1) {
            if (setup_myname.length() < 15) {
                setup_myname.push_back(static_cast<char>(k));
            }
        }
    }

    if (setup_top > 13) {
        setup_top = 0;
    }

    if (setup_top < 0) {
        setup_top = 13;
    }

    if (setup_bottom > 13) {
        setup_bottom = 0;
    }

    if (setup_bottom < 0) {
        setup_bottom = 13;
    }
}

//=============================================================================
/* NET MENU */

int m_net_items;
int m_net_saveHeight;

const eastl::array<eastl::string_view, 16> net_helpMessage = {
    /* .........1.........2.... */
    "                        ", " Two computers connected",
    "   through two modems.  ", "                        ",

    "                        ", " Two computers connected",
    " by a null-modem cable. ", "                        ",

    " Novell network LANs    ", " or Windows 95 DOS-box. ",
    "                        ", "(LAN=Local Area Network)",

    " Commonly used to play  ", " over the Internet, but ",
    " also used on a Local   ", " Area Network.          "
};

void M_Menu_Net_f()
{
    key_dest = key_menu;
    m_state = MenuState::Net;
    m_entersound = true;
    m_net_items = 4;

    if (m_net_cursor >= m_net_items) {
        m_net_cursor = 0;
    }

    m_net_cursor--;
    M_Net_Key(K_DOWNARROW);
}

void M_Net_Draw()
{
    int f;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    int basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    f = 32;

    if (serialAvailable) {
        p = Draw_CachePic("gfx/netmen1.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_modm.lmp");
    }

    if (p) {
        M_DrawTransPic(72, f, p);
    }

    f += 19;

    if (serialAvailable) {
        p = Draw_CachePic("gfx/netmen2.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_drct.lmp");
    }

    if (p) {
        M_DrawTransPic(72, f, p);
    }

    f += 19;
    if (ipxAvailable) {
        p = Draw_CachePic("gfx/netmen3.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_ipx.lmp");
    }

    M_DrawTransPic(72, f, p);

    f += 19;
    if (tcpipAvailable) {
        p = Draw_CachePic("gfx/netmen4.lmp");
    } else {
        p = Draw_CachePic("gfx/dim_tcp.lmp");
    }

    M_DrawTransPic(72, f, p);

    if (m_net_items == 5) // JDC, could just be removed
    {
        f += 19;
        p = Draw_CachePic("gfx/netmen5.lmp");
        M_DrawTransPic(72, f, p);
    }

    f = (320 - 26 * 8) / 2;
    M_DrawTextBox(f, 134, 24, 4);
    f += 8;
    M_Print(f, 142, net_helpMessage[m_net_cursor * 4 + 0]);
    M_Print(f, 150, net_helpMessage[m_net_cursor * 4 + 1]);
    M_Print(f, 158, net_helpMessage[m_net_cursor * 4 + 2]);
    M_Print(f, 166, net_helpMessage[m_net_cursor * 4 + 3]);

    f = (int)(host_time * 10) % 6;
    M_DrawTransPic(54, 32 + m_net_cursor * 20,
        Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_Net_Key(int k)
{
again:
    switch (k) {
    case K_ESCAPE:
        M_Menu_MultiPlayer_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++m_net_cursor >= m_net_items) {
            m_net_cursor = 0;
        }

        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--m_net_cursor < 0) {
            m_net_cursor = m_net_items - 1;
        }

        break;

    case K_ENTER:
        m_entersound = true;

        switch (m_net_cursor) {
        case 0:
            M_Menu_SerialConfig_f();
            break;

        case 1:
            M_Menu_SerialConfig_f();
            break;

        case 2:
            M_Menu_LanConfig_f();
            break;

        case 3:
            M_Menu_LanConfig_f();
            break;

        case 4:
            // multiprotocol
            break;
        }
    }

    if (m_net_cursor == 0 && !serialAvailable) {
        goto again;
    }

    if (m_net_cursor == 1 && !serialAvailable) {
        goto again;
    }

    if (m_net_cursor == 2 && !ipxAvailable) {
        goto again;
    }

    if (m_net_cursor == 3 && !tcpipAvailable) {
        goto again;
    }
}

//=============================================================================
/* OPTIONS MENU */

constexpr int OPTIONS_ITEMS = 13;
constexpr int SLIDER_RANGE = 10;

int options_cursor;

void M_Menu_Options_f()
{
    key_dest = key_menu;
    m_state = MenuState::Options;
    m_entersound = true;
}

void M_AdjustSliders(int dir)
{
    S_LocalSound("misc/menu3.wav");

    switch (options_cursor) {
    case 3: // screen size
        Screen::GetScreenSystem().GetViewsize().value += dir * 10;
        if (Screen::GetScreenSystem().GetViewsize().value < 30) {
            Screen::GetScreenSystem().GetViewsize().value = 30;
        }

        if (Screen::GetScreenSystem().GetViewsize().value > 120) {
            Screen::GetScreenSystem().GetViewsize().value = 120;
        }

        Cvar::SetValue("viewsize", Screen::GetScreenSystem().GetViewsize().value);
        break;
    case 4: // gamma
        v_gamma.value -= static_cast<float>(dir * 0.05);
        if (v_gamma.value < 0.5) {
            v_gamma.value = 0.5;
        }

        if (v_gamma.value > 1) {
            v_gamma.value = 1;
        }

        Cvar::SetValue("gamma", v_gamma.value);
        break;
    case 5: // mouse speed
        sensitivity.value += static_cast<float>(dir * 0.5);
        if (sensitivity.value < 1) {
            sensitivity.value = 1;
        }

        if (sensitivity.value > 11) {
            sensitivity.value = 11;
        }

        Cvar::SetValue("sensitivity", sensitivity.value);
        break;
    case 6: // music volume
        bgmvolume.value += static_cast<float>(dir * 0.1);
        if (bgmvolume.value < 0) {
            bgmvolume.value = 0;
        }

        if (bgmvolume.value > 1) {
            bgmvolume.value = 1;
        }

        Cvar::SetValue("bgmvolume", bgmvolume.value);
        break;
    case 7: // sfx volume
        volume.value += static_cast<float>(dir * 0.1);
        if (volume.value < 0) {
            volume.value = 0;
        }

        if (volume.value > 1) {
            volume.value = 1;
        }

        Cvar::SetValue("volume", volume.value);
        break;

    case 8: // allways run
        if (cl_forwardspeed.value > 200) {
            Cvar::SetValue("cl_forwardspeed", static_cast<float>(200));
            Cvar::SetValue("cl_backspeed", static_cast<float>(200));
        } else {
            Cvar::SetValue("cl_forwardspeed", static_cast<float>(400));
            Cvar::SetValue("cl_backspeed", static_cast<float>(400));
        }

        break;

    case 9: // invert mouse
        Cvar::SetValue("m_pitch", -m_pitch.value);
        break;

    case 10: // lookspring
        Cvar::SetValue("lookspring", static_cast<float>(!lookspring.value));
        break;

    case 11: // lookstrafe
        Cvar::SetValue("lookstrafe", static_cast<float>(!lookstrafe.value));
        break;
    }
}

inline void M_DrawSlider(int x, int y, float range)
{
    int i;

    if (range < 0) {
        range = 0;
    }

    if (range > 1) {
        range = 1;
    }

    M_DrawCharacter(x - 8, y, 128);
    for (i = 0; i < SLIDER_RANGE; i++) {
        M_DrawCharacter(x + i * 8, y, 129);
    }
    M_DrawCharacter(x + i * 8, y, 130);
    M_DrawCharacter(x + static_cast<int>((SLIDER_RANGE - 1) * 8 * range), y, 131);
}

inline void M_DrawCheckbox(int x, int y, int on)
{
    if (on) {
        M_Print(x, y, "on");
    } else {
        M_Print(x, y, "off");
    }
}

void M_Options_Draw()
{
    float r;
    qpic_t* p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_option.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(16, 32, "    Customize controls");
    M_Print(16, 40, "         Go to console");
    M_Print(16, 48, "     Reset to defaults");

    M_Print(16, 56, "           Screen size");
    r = (Screen::GetScreenSystem().GetViewsize().value - 30) / (120 - 30);
    M_DrawSlider(220, 56, r);

    M_Print(16, 64, "            Brightness");
    r = static_cast<float>((1.0 - v_gamma.value) / 0.5);
    M_DrawSlider(220, 64, r);

    M_Print(16, 72, "           Mouse Speed");
    r = (sensitivity.value - 1) / 10;
    M_DrawSlider(220, 72, r);

    M_Print(16, 80, "       CD Music Volume");
    r = bgmvolume.value;
    M_DrawSlider(220, 80, r);

    M_Print(16, 88, "          Sound Volume");
    r = volume.value;
    M_DrawSlider(220, 88, r);

    M_Print(16, 96, "            Always Run");
    M_DrawCheckbox(220, 96, cl_forwardspeed.value > 200);

    M_Print(16, 104, "          Invert Mouse");
    M_DrawCheckbox(220, 104, m_pitch.value < 0);

    M_Print(16, 112, "            Lookspring");
    M_DrawCheckbox(220, 112, static_cast<int>(lookspring.value));

    M_Print(16, 120, "            Lookstrafe");
    M_DrawCheckbox(220, 120, static_cast<int>(lookstrafe.value));

    if (vid_menudrawfn) {
        M_Print(16, 128, "         Video Options");
    }

    // cursor
    M_DrawCharacter(200, 32 + options_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Options_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_ENTER:
        m_entersound = true;
        switch (options_cursor) {
        case 0:
            M_Menu_Keys_f();
            break;
        case 1:
            m_state = MenuState::None;
            ConsoleSystem::ToggleConsole_f();
            break;
        case 2:
            Cmd::BufferAddText("exec default.cfg\n");
            break;
        case 12:
            M_Menu_Video_f();
            break;
        default:
            M_AdjustSliders(1);
            break;
        }

        return;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        options_cursor--;
        if (options_cursor < 0) {
            options_cursor = OPTIONS_ITEMS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        options_cursor++;
        if (options_cursor >= OPTIONS_ITEMS) {
            options_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        M_AdjustSliders(-1);
        break;

    case K_RIGHTARROW:
        M_AdjustSliders(1);
        break;
    }

    if (options_cursor == 12 && vid_menudrawfn == nullptr) {
        if (k == K_UPARROW) {
            options_cursor = 11;
        } else {
            options_cursor = 0;
        }
    }
}

//=============================================================================
/* KEYS MENU */

struct BindName {
    eastl::string_view command;
    eastl::string_view description;
};

const eastl::array<BindName, 18> bindnames = {{
    { "+attack", "attack" }, { "impulse 10", "change weapon" },
    { "+jump", "jump / swim up" }, { "+forward", "walk forward" },
    { "+back", "backpedal" }, { "+left", "turn left" },
    { "+right", "turn right" }, { "+speed", "run" },
    { "+moveleft", "step left" }, { "+moveright", "step right" },
    { "+strafe", "sidestep" }, { "+lookup", "look up" },
    { "+lookdown", "look down" }, { "centerview", "center view" },
    { "+mlook", "mouse look" }, { "+klook", "keyboard look" },
    { "+moveup", "swim up" }, { "+movedown", "swim down" }
}};

int keys_cursor;
bool bind_grab;

void M_Menu_Keys_f()
{
    key_dest = key_menu;
    m_state = MenuState::Keys;
    m_entersound = true;
}

void M_FindKeysForCommand(eastl::string_view command, eastl::array<int, 2>& twokeys)
{
    twokeys[0] = twokeys[1] = -1;
    int count = 0;

    for (int j = 0; j < 256; j++) {
        if (keybindings[j].empty()) {
            continue;
        }

        if (keybindings[j] == command) {
            twokeys[count] = j;
            count++;
            if (count == 2) {
                break;
            }
        }
    }
}

void M_UnbindCommand(eastl::string_view command)
{
    for (int j = 0; j < 256; j++) {
        if (keybindings[j].empty()) {
            continue;
        }

        if (keybindings[j] == command) {
            Key_SetBinding(j, "");
        }
    }
}

void M_Keys_Draw()
{
    int y;
    eastl::array<int, 2> keys;
    const char* name;
    qpic_t* p;

    p = Draw_CachePic("gfx/ttl_cstm.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    if (bind_grab) {
        M_Print(12, 32, "Press a key or button for this action");
    } else {
        M_Print(18, 32, "Enter to change, backspace to clear");
    }

    // search for known bindings
    for (size_t i = 0; i < bindnames.size(); i++) {
        y = 48 + 8 * static_cast<int>(i);

        M_Print(16, y, bindnames[i].description);

        M_FindKeysForCommand(bindnames[i].command, keys);

        if (keys[0] == -1) {
            M_Print(140, y, "???");
        } else {
            name = Key_KeynumToString(keys[0]);
            M_Print(140, y, name);
            int x = (int)strlen(name) * 8;
            if (keys[1] != -1) {
                M_Print(140 + x + 8, y, "or");
                M_Print(140 + x + 32, y, Key_KeynumToString(keys[1]));
            }
        }
    }

    if (bind_grab) {
        M_DrawCharacter(130, 48 + keys_cursor * 8, '=');
    } else {
        M_DrawCharacter(130, 48 + keys_cursor * 8, 12 + ((int)(realtime * 4) & 1));
    }
}

void M_Keys_Key(int k)
{
    char cmd[80];
    eastl::array<int, 2> keys;

    if (bind_grab) { // defining a key
        S_LocalSound("misc/menu1.wav");
        if (k == K_ESCAPE) {
            bind_grab = false;
        } else if (k != '`') {
            sprintf_s(cmd, sizeof(cmd), "bind \"%s\" \"%.70s\"\n", Key_KeynumToString(k),
                bindnames[keys_cursor].command.data());
            Cmd::BufferInsertText(cmd);
        }

        bind_grab = false;

        return;
    }

    switch (k) {
    case K_ESCAPE:
        M_Menu_Options_f();
        break;

    case K_LEFTARROW:
    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        keys_cursor--;
        if (keys_cursor < 0) {
            keys_cursor = static_cast<int>(bindnames.size()) - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        keys_cursor++;
        if (static_cast<size_t>(keys_cursor) >= bindnames.size()) {
            keys_cursor = 0;
        }

        break;

    case K_ENTER: // go into bind mode
        M_FindKeysForCommand(bindnames[keys_cursor].command, keys);
        S_LocalSound("misc/menu2.wav");
        if (keys[1] != -1) {
            M_UnbindCommand(bindnames[keys_cursor].command);
        }

        bind_grab = true;
        break;

    case K_BACKSPACE: // delete bindings
    case K_DEL:       // delete bindings
        S_LocalSound("misc/menu2.wav");
        M_UnbindCommand(bindnames[keys_cursor].command);
        break;
    }
}

//=============================================================================
/* VIDEO MENU */

void M_Menu_Video_f()
{
    key_dest = key_menu;
    m_state = MenuState::Video;
    m_entersound = true;
}

void M_Video_Draw()
{
    if (vid_menudrawfn) {
        (*vid_menudrawfn)();
    }
}

void M_Video_Key(int key)
{
    if (vid_menukeyfn) {
        (*vid_menukeyfn)(key);
    }
}

//=============================================================================
/* HELP MENU */

int help_page;
constexpr int NUM_HELP_PAGES = 6;

void M_Menu_Help_f()
{
    key_dest = key_menu;
    m_state = MenuState::Help;
    m_entersound = true;
    help_page = 0;
}

void M_Help_Draw()
{
    M_DrawPic(0, 0, Draw_CachePic(va("gfx/help%i.lmp", help_page)));
}

void M_Help_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_UPARROW:
    case K_RIGHTARROW:
        m_entersound = true;
        if (++help_page >= NUM_HELP_PAGES) {
            help_page = 0;
        }

        break;

    case K_DOWNARROW:
    case K_LEFTARROW:
        m_entersound = true;
        if (--help_page < 0) {
            help_page = NUM_HELP_PAGES - 1;
        }

        break;
    }
}

//=============================================================================
/* QUIT MENU */

void M_Menu_Quit_f()
{
    key_dest = key_console;
    Host_Quit_f();
}

//=============================================================================
/* SERIAL CONFIG MENU */

int serialConfig_cursor;
const eastl::array<int, 6> serialConfig_cursor_table = { 48, 64, 80, 96, 112, 132 };
constexpr int NUM_SERIALCONFIG_CMDS = 6;

static const eastl::array<int, 4> ISA_uarts = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static const eastl::array<int, 4> ISA_IRQs = { 4, 3, 4, 3 };
static const eastl::array<int, 6> serialConfig_baudrate = { 9600, 14400, 19200, 28800, 38400, 57600 };

int serialConfig_comport;
int serialConfig_irq;
int serialConfig_baud;
eastl::string serialConfig_phone;

void M_Menu_SerialConfig_f()
{
    int n;
    int port;
    int baudrate;
    qboolean useModem;

    key_dest = key_menu;
    m_state = MenuState::SerialConfig;
    m_entersound = true;
    if (JoiningGame() && SerialConfig()) {
        serialConfig_cursor = 4;
    } else {
        serialConfig_cursor = 5;
    }

    (*GetComPortConfig)(0, &port, &serialConfig_irq, &baudrate, &useModem);

    // map uart's port to COMx
    for (n = 0; n < 4; n++) {
        if (ISA_uarts[n] == port) {
            break;
        }
    }
    if (n == 4) {
        n = 0;
        serialConfig_irq = 4;
    }

    serialConfig_comport = n + 1;

    // map baudrate to index
    for (n = 0; n < 6; n++) {
        if (serialConfig_baudrate[n] == baudrate) {
            break;
        }
    }
    if (n == 6) {
        n = 5;
    }

    serialConfig_baud = n;

    m_return_onerror = false;
    m_return_reason.clear();
}

void M_SerialConfig_Draw()
{
    qpic_t* p;
    int basex;
    const char* startJoin;
    const char* directModem;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    if (StartingGame()) {
        startJoin = "New Game";
    } else {
        startJoin = "Join Game";
    }

    if (SerialConfig()) {
        directModem = "Modem";
    } else {
        directModem = "Direct Connect";
    }

    M_Print(basex, 32, va("%s - %s", startJoin, directModem));
    basex += 8;

    M_Print(basex, serialConfig_cursor_table[0], "Port");
    M_DrawTextBox(160, 40, 4, 1);
    M_Print(168, serialConfig_cursor_table[0], va("COM%u", serialConfig_comport));

    M_Print(basex, serialConfig_cursor_table[1], "IRQ");
    M_DrawTextBox(160, serialConfig_cursor_table[1] - 8, 1, 1);
    M_Print(168, serialConfig_cursor_table[1], va("%u", serialConfig_irq));

    M_Print(basex, serialConfig_cursor_table[2], "Baud");
    M_DrawTextBox(160, serialConfig_cursor_table[2] - 8, 5, 1);
    M_Print(168, serialConfig_cursor_table[2],
        va("%u", serialConfig_baudrate[serialConfig_baud]));

    if (SerialConfig()) {
        M_Print(basex, serialConfig_cursor_table[3], "Modem Setup...");
        if (JoiningGame()) {
            M_Print(basex, serialConfig_cursor_table[4], "Phone number");
            M_DrawTextBox(160, serialConfig_cursor_table[4] - 8, 16, 1);
            M_Print(168, serialConfig_cursor_table[4], serialConfig_phone);
        }
    }

    if (JoiningGame()) {
        M_DrawTextBox(basex, serialConfig_cursor_table[5] - 8, 7, 1);
        M_Print(basex + 8, serialConfig_cursor_table[5], "Connect");
    } else {
        M_DrawTextBox(basex, serialConfig_cursor_table[5] - 8, 2, 1);
        M_Print(basex + 8, serialConfig_cursor_table[5], "OK");
    }

    M_DrawCharacter(basex - 8, serialConfig_cursor_table[serialConfig_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (serialConfig_cursor == 4) {
        M_DrawCharacter(168 + 8 * (int)serialConfig_phone.length(),
            serialConfig_cursor_table[serialConfig_cursor],
            10 + ((int)(realtime * 4) & 1));
    }

    if (!m_return_reason.empty()) {
        M_PrintWhite(basex, 148, m_return_reason.c_str());
    }
}

void M_SerialConfig_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        serialConfig_cursor--;
        if (serialConfig_cursor < 0) {
            serialConfig_cursor = NUM_SERIALCONFIG_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        serialConfig_cursor++;
        if (serialConfig_cursor >= NUM_SERIALCONFIG_CMDS) {
            serialConfig_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        if (serialConfig_cursor > 2) {
            break;
        }

        S_LocalSound("misc/menu3.wav");

        if (serialConfig_cursor == 0) {
            serialConfig_comport--;
            if (serialConfig_comport == 0) {
                serialConfig_comport = 4;
            }

            serialConfig_irq = ISA_IRQs[serialConfig_comport - 1];
        }

        if (serialConfig_cursor == 1) {
            serialConfig_irq--;
            if (serialConfig_irq == 6) {
                serialConfig_irq = 5;
            }

            if (serialConfig_irq == 1) {
                serialConfig_irq = 7;
            }
        }

        if (serialConfig_cursor == 2) {
            serialConfig_baud--;
            if (serialConfig_baud < 0) {
                serialConfig_baud = 5;
            }
        }

        break;

    case K_RIGHTARROW:
        if (serialConfig_cursor > 2) {
            break;
        }

    forward:
        S_LocalSound("misc/menu3.wav");

        if (serialConfig_cursor == 0) {
            serialConfig_comport++;
            if (serialConfig_comport > 4) {
                serialConfig_comport = 1;
            }

            serialConfig_irq = ISA_IRQs[serialConfig_comport - 1];
        }

        if (serialConfig_cursor == 1) {
            serialConfig_irq++;
            if (serialConfig_irq == 6) {
                serialConfig_irq = 7;
            }

            if (serialConfig_irq == 8) {
                serialConfig_irq = 2;
            }
        }

        if (serialConfig_cursor == 2) {
            serialConfig_baud++;
            if (serialConfig_baud > 5) {
                serialConfig_baud = 0;
            }
        }

        break;

    case K_ENTER:
        if (serialConfig_cursor < 3) {
            goto forward;
        }

        m_entersound = true;

        if (serialConfig_cursor == 3) {
            (*SetComPortConfig)(
                0, ISA_uarts[serialConfig_comport - 1], serialConfig_irq,
                serialConfig_baudrate[serialConfig_baud], SerialConfig());

            M_Menu_ModemConfig_f();
            break;
        }

        if (serialConfig_cursor == 4) {
            serialConfig_cursor = 5;
            break;
        }

        // serialConfig_cursor == 5 (OK/CONNECT)
        (*SetComPortConfig)(
            0, ISA_uarts[serialConfig_comport - 1], serialConfig_irq,
            serialConfig_baudrate[serialConfig_baud], SerialConfig());

        M_ConfigureNetSubsystem();

        if (StartingGame()) {
            M_Menu_GameOptions_f();
            break;
        }

        m_return_state = m_state;
        m_return_onerror = true;
        key_dest = key_game;
        m_state = MenuState::None;

        if (SerialConfig()) {
            Cmd::BufferAddText(va("connect \"%s\"\n", serialConfig_phone.c_str()));
        } else {
            Cmd::BufferAddText("connect\n");
        }

        break;

    case K_BACKSPACE:
        if (serialConfig_cursor == 4) {
            if (!serialConfig_phone.empty()) {
                serialConfig_phone.pop_back();
            }
        }

        break;

    default:
        if (key < 32 || key > 127) {
            break;
        }

        if (serialConfig_cursor == 4) {
            if (serialConfig_phone.length() < 15) {
                serialConfig_phone.push_back(static_cast<char>(key));
            }
        }
    }

    if (DirectConfig() && (serialConfig_cursor == 3 || serialConfig_cursor == 4)) {
        if (key == K_UPARROW) {
            serialConfig_cursor = 2;
        } else {
            serialConfig_cursor = 5;
        }
    }

    if (SerialConfig() && StartingGame() && serialConfig_cursor == 4) {
        if (key == K_UPARROW) {
            serialConfig_cursor = 3;
        } else {
            serialConfig_cursor = 5;
        }
    }
}

//=============================================================================
/* MODEM CONFIG MENU */

int modemConfig_cursor;
const std::array<int, 5> modemConfig_cursor_table = { 40, 56, 88, 120, 156 };
constexpr int NUM_MODEMCONFIG_CMDS = 5;

char modemConfig_dialing;
std::array<char, 16> modemConfig_clear;
std::array<char, 32> modemConfig_init;
std::array<char, 16> modemConfig_hangup;

void M_Menu_ModemConfig_f()
{
    key_dest = key_menu;
    m_state = MenuState::ModemConfig;
    m_entersound = true;
    (*GetModemConfig)(0, &modemConfig_dialing, modemConfig_clear.data(),
        modemConfig_init.data(), modemConfig_hangup.data());
}

void M_ModemConfig_Draw()
{
    qpic_t* p;
    int basex;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);
    basex += 8;

    if (modemConfig_dialing == 'P') {
        M_Print(basex, modemConfig_cursor_table[0], "Pulse Dialing");
    } else {
        M_Print(basex, modemConfig_cursor_table[0], "Touch Tone Dialing");
    }

    M_Print(basex, modemConfig_cursor_table[1], "Clear");
    M_DrawTextBox(basex, modemConfig_cursor_table[1] + 4, 16, 1);
    M_Print(basex + 8, modemConfig_cursor_table[1] + 12, modemConfig_clear.data());
    if (modemConfig_cursor == 1) {
        M_DrawCharacter(basex + 8 + 8 * (int)strlen(modemConfig_clear.data()),
            modemConfig_cursor_table[1] + 12,
            10 + ((int)(realtime * 4) & 1));
    }

    M_Print(basex, modemConfig_cursor_table[2], "Init");
    M_DrawTextBox(basex, modemConfig_cursor_table[2] + 4, 30, 1);
    M_Print(basex + 8, modemConfig_cursor_table[2] + 12, modemConfig_init.data());
    if (modemConfig_cursor == 2) {
        M_DrawCharacter(basex + 8 + 8 * (int)strlen(modemConfig_init.data()),
            modemConfig_cursor_table[2] + 12,
            10 + ((int)(realtime * 4) & 1));
    }

    M_Print(basex, modemConfig_cursor_table[3], "Hangup");
    M_DrawTextBox(basex, modemConfig_cursor_table[3] + 4, 16, 1);
    M_Print(basex + 8, modemConfig_cursor_table[3] + 12, modemConfig_hangup.data());
    if (modemConfig_cursor == 3) {
        M_DrawCharacter(basex + 8 + 8 * (int)strlen(modemConfig_hangup.data()),
            modemConfig_cursor_table[3] + 12,
            10 + ((int)(realtime * 4) & 1));
    }

    M_DrawTextBox(basex, modemConfig_cursor_table[4] - 8, 2, 1);
    M_Print(basex + 8, modemConfig_cursor_table[4], "OK");

    M_DrawCharacter(basex - 8, modemConfig_cursor_table[modemConfig_cursor],
        12 + ((int)(realtime * 4) & 1));
}

void M_ModemConfig_Key(int key)
{
    int l;

    switch (key) {
    case K_ESCAPE:
        M_Menu_SerialConfig_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        modemConfig_cursor--;
        if (modemConfig_cursor < 0) {
            modemConfig_cursor = NUM_MODEMCONFIG_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        modemConfig_cursor++;
        if (modemConfig_cursor >= NUM_MODEMCONFIG_CMDS) {
            modemConfig_cursor = 0;
        }

        break;

    case K_LEFTARROW:
    case K_RIGHTARROW:
        if (modemConfig_cursor == 0) {
            if (modemConfig_dialing == 'P') {
                modemConfig_dialing = 'T';
            } else {
                modemConfig_dialing = 'P';
            }

            S_LocalSound("misc/menu1.wav");
        }

        break;

    case K_ENTER:
        if (modemConfig_cursor == 0) {
            if (modemConfig_dialing == 'P') {
                modemConfig_dialing = 'T';
            } else {
                modemConfig_dialing = 'P';
            }

            m_entersound = true;
        }

        if (modemConfig_cursor == 4) {
            (*SetModemConfig)(0, va("%c", modemConfig_dialing), modemConfig_clear.data(),
                modemConfig_init.data(), modemConfig_hangup.data());
            m_entersound = true;
            M_Menu_SerialConfig_f();
        }

        break;

    case K_BACKSPACE:
        if (modemConfig_cursor == 1) {
            if (strlen(modemConfig_clear.data())) {
                modemConfig_clear[strlen(modemConfig_clear.data()) - 1] = 0;
            }
        }

        if (modemConfig_cursor == 2) {
            if (strlen(modemConfig_init.data())) {
                modemConfig_init[strlen(modemConfig_init.data()) - 1] = 0;
            }
        }

        if (modemConfig_cursor == 3) {
            if (strlen(modemConfig_hangup.data())) {
                modemConfig_hangup[strlen(modemConfig_hangup.data()) - 1] = 0;
            }
        }

        break;

    default:
        if (key < 32 || key > 127) {
            break;
        }

        if (modemConfig_cursor == 1) {
            l = (int)strlen(modemConfig_clear.data());
            if (l < 15) {
                modemConfig_clear[l + 1] = 0;
                modemConfig_clear[l] = static_cast<char>(key);
            }
        }

        if (modemConfig_cursor == 2) {
            l = (int)strlen(modemConfig_init.data());
            if (l < 29) {
                modemConfig_init[l + 1] = 0;
                modemConfig_init[l] = static_cast<char>(key);
            }
        }

        if (modemConfig_cursor == 3) {
            l = (int)strlen(modemConfig_hangup.data());
            if (l < 15) {
                modemConfig_hangup[l + 1] = 0;
                modemConfig_hangup[l] = static_cast<char>(key);
            }
        }
    }
}

//=============================================================================
/* LAN CONFIG MENU */

int lanConfig_cursor = -1;
const eastl::array<int, 3> lanConfig_cursor_table = { 72, 92, 124 };
constexpr int NUM_LANCONFIG_CMDS = 3;

int lanConfig_port;
eastl::string lanConfig_portname;
eastl::string lanConfig_joinname;

void M_Menu_LanConfig_f()
{
    key_dest = key_menu;
    m_state = MenuState::LanConfig;
    m_entersound = true;
    if (lanConfig_cursor == -1) {
        if (JoiningGame()) {
            lanConfig_cursor = 2;
        } else {
            lanConfig_cursor = 1;
        }
    }

    if (StartingGame() && lanConfig_cursor == 2) {
        lanConfig_cursor = 1;
    }

    lanConfig_port = DEFAULTnet_hostport;
    lanConfig_portname = eastl::to_string(lanConfig_port);

    m_return_onerror = false;
    m_return_reason.clear();
}

void M_LanConfig_Draw()
{
    qpic_t* p;
    int basex;
    const char* startJoin;
    const char* protocol;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    if (StartingGame()) {
        startJoin = "New Game";
    } else {
        startJoin = "Join Game";
    }

    if (IPXConfig()) {
        protocol = "IPX";
    } else {
        protocol = "TCP/IP";
    }

    M_Print(basex, 32, va("%s - %s", startJoin, protocol));
    basex += 8;

    M_Print(basex, 52, "Address:");
    if (IPXConfig()) {
        M_Print(basex + 9 * 8, 52, my_ipx_address);
    } else {
        M_Print(basex + 9 * 8, 52, my_tcpip_address);
    }

    M_Print(basex, lanConfig_cursor_table[0], "Port");
    M_DrawTextBox(basex + 8 * 8, lanConfig_cursor_table[0] - 8, 6, 1);
    M_Print(basex + 9 * 8, lanConfig_cursor_table[0], lanConfig_portname);

    if (JoiningGame()) {
        M_Print(basex, lanConfig_cursor_table[1], "Search for local games...");
        M_Print(basex, 108, "Join game at:");
        M_DrawTextBox(basex + 8, lanConfig_cursor_table[2] - 8, 22, 1);
        M_Print(basex + 16, lanConfig_cursor_table[2], lanConfig_joinname);
    } else {
        M_DrawTextBox(basex, lanConfig_cursor_table[1] - 8, 2, 1);
        M_Print(basex + 8, lanConfig_cursor_table[1], "OK");
    }

    M_DrawCharacter(basex - 8, lanConfig_cursor_table[lanConfig_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (lanConfig_cursor == 0) {
        M_DrawCharacter(basex + 9 * 8 + 8 * (int)lanConfig_portname.length(),
            lanConfig_cursor_table[0], 10 + ((int)(realtime * 4) & 1));
    }

    if (lanConfig_cursor == 2) {
        M_DrawCharacter(basex + 16 + 8 * (int)lanConfig_joinname.length(),
            lanConfig_cursor_table[2], 10 + ((int)(realtime * 4) & 1));
    }

    if (!m_return_reason.empty()) {
        M_PrintWhite(basex, 148, m_return_reason.c_str());
    }
}

void M_LanConfig_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        lanConfig_cursor--;
        if (lanConfig_cursor < 0) {
            lanConfig_cursor = NUM_LANCONFIG_CMDS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        lanConfig_cursor++;
        if (lanConfig_cursor >= NUM_LANCONFIG_CMDS) {
            lanConfig_cursor = 0;
        }

        break;

    case K_ENTER:
        if (lanConfig_cursor == 0) {
            break;
        }

        m_entersound = true;

        M_ConfigureNetSubsystem();

        if (lanConfig_cursor == 1) {
            if (StartingGame()) {
                M_Menu_GameOptions_f();
                break;
            }

            M_Menu_Search_f();
            break;
        }

        if (lanConfig_cursor == 2) {
            m_return_state = m_state;
            m_return_onerror = true;
            key_dest = key_game;
            m_state = MenuState::None;
            Cmd::BufferAddText(va("connect \"%s\"\n", lanConfig_joinname.c_str()));
            break;
        }

        break;

    case K_BACKSPACE:
        if (lanConfig_cursor == 0) {
            if (!lanConfig_portname.empty()) {
                lanConfig_portname.pop_back();
            }
        }

        if (lanConfig_cursor == 2) {
            if (!lanConfig_joinname.empty()) {
                lanConfig_joinname.pop_back();
            }
        }

        break;

    default:
        if (key < 32 || key > 127) {
            break;
        }

        if (lanConfig_cursor == 2) {
            if (lanConfig_joinname.length() < 21) {
                lanConfig_joinname.push_back(static_cast<char>(key));
            }
        }

        if (key < '0' || key > '9') {
            break;
        }

        if (lanConfig_cursor == 0) {
            if (lanConfig_portname.length() < 5) {
                lanConfig_portname.push_back(static_cast<char>(key));
            }
        }
    }

    if (StartingGame() && lanConfig_cursor == 2) {
        if (key == K_UPARROW) {
            lanConfig_cursor = 1;
        } else {
            lanConfig_cursor = 0;
        }
    }

    int l = Q_atoi(lanConfig_portname.c_str());
    if (l <= 65535) {
        lanConfig_port = l;
    }

    lanConfig_portname = std::to_string(lanConfig_port).c_str();
}

//=============================================================================
/* GAME OPTIONS MENU */

struct level_t {
    const char* name;
    const char* description;
};

static constexpr std::array<level_t, 38> levels = {{
    { "start", "Entrance" }, // 0

    { "e1m1", "Slipgate Complex" }, // 1
    { "e1m2", "Castle of the Damned" },
    { "e1m3", "The Necropolis" },
    { "e1m4", "The Grisly Grotto" },
    { "e1m5", "Gloom Keep" },
    { "e1m6", "The Door To Chthon" },
    { "e1m7", "The House of Chthon" },
    { "e1m8", "Ziggurat Vertigo" },

    { "e2m1", "The Installation" }, // 9
    { "e2m2", "Ogre Citadel" },
    { "e2m3", "Crypt of Decay" },
    { "e2m4", "The Ebon Fortress" },
    { "e2m5", "The Wizard's Manse" },
    { "e2m6", "The Dismal Oubliette" },
    { "e2m7", "Underearth" },

    { "e3m1", "Termination Central" }, // 16
    { "e3m2", "The Vaults of Zin" },
    { "e3m3", "The Tomb of Terror" },
    { "e3m4", "Satan's Dark Delight" },
    { "e3m5", "Wind Tunnels" },
    { "e3m6", "Chambers of Torment" },
    { "e3m7", "The Haunted Halls" },

    { "e4m1", "The Sewage System" }, // 23
    { "e4m2", "The Tower of Despair" },
    { "e4m3", "The Elder God Shrine" },
    { "e4m4", "The Palace of Hate" },
    { "e4m5", "Hell's Atrium" },
    { "e4m6", "The Pain Maze" },
    { "e4m7", "Azure Agony" },
    { "e4m8", "The Nameless City" },

    { "end", "Shub-Niggurath's Pit" }, // 31

    { "dm1", "Place of Two Deaths" }, // 32
    { "dm2", "Claustrophobopolis" },
    { "dm3", "The Abandoned Base" },
    { "dm4", "The Bad Place" },
    { "dm5", "The Cistern" },
    { "dm6", "The Dark Zone" }
}};

static constexpr std::array<level_t, 18> hipnoticlevels = {{
    { "start", "Command HQ" }, // 0

    { "hip1m1", "The Pumping Station" }, // 1
    { "hip1m2", "Storage Facility" },
    { "hip1m3", "The Lost Mine" },
    { "hip1m4", "Research Facility" },
    { "hip1m5", "Military Complex" },

    { "hip2m1", "Ancient Realms" }, // 6
    { "hip2m2", "The Black Cathedral" },
    { "hip2m3", "The Catacombs" },
    { "hip2m4", "The Crypt" },
    { "hip2m5", "Mortum's Keep" },
    { "hip2m6", "The Gremlin's Domain" },

    { "hip3m1", "Tur Torment" }, // 12
    { "hip3m2", "Pandemonium" },
    { "hip3m3", "Limbo" },
    { "hip3m4", "The Gauntlet" },

    { "hipend", "Armagon's Lair" }, // 16

    { "hipdm1", "The Edge of Oblivion" } // 17
}};

static constexpr std::array<level_t, 17> roguelevels = {{
    { "start", "Split Decision" }, { "r1m1", "Deviant's Domain" },
    { "r1m2", "Dread Portal" }, { "r1m3", "Judgement Call" },
    { "r1m4", "Cave of Death" }, { "r1m5", "Towers of Wrath" },
    { "r1m6", "Temple of Pain" }, { "r1m7", "Tomb of the Overlord" },
    { "r2m1", "Tempus Fugit" }, { "r2m2", "Elemental Fury I" },
    { "r2m3", "Elemental Fury II" }, { "r2m4", "Curse of Osiris" },
    { "r2m5", "Wizard's Keep" }, { "r2m6", "Blood Sacrifice" },
    { "r2m7", "Last Bastion" }, { "r2m8", "Source of Evil" },
    { "ctf1", "Division of Change" }
}};

struct episode_t {
    const char* description;
    int firstLevel;
    int levels;
};

static constexpr std::array<episode_t, 7> episodes = {{
    { "Welcome to Quake", 0, 1 }, { "Doomed Dimension", 1, 8 },
    { "Realm of Black Magic", 9, 7 }, { "Netherworld", 16, 7 },
    { "The Elder World", 23, 8 }, { "Final Level", 31, 1 },
    { "Deathmatch Arena", 32, 6 }
}};

static constexpr std::array<episode_t, 6> hipnoticepisodes = {{
    { "Scourge of Armagon", 0, 1 }, { "Fortress of the Dead", 1, 5 },
    { "Dominion of Darkness", 6, 6 }, { "The Rift", 12, 4 },
    { "Final Level", 16, 1 }, { "Deathmatch Arena", 17, 1 }
}};

static constexpr std::array<episode_t, 4> rogueepisodes = {{
    { "Introduction", 0, 1 },
    { "Hell's Fortress", 1, 7 },
    { "Corridors of Time", 8, 8 },
    { "Deathmatch Arena", 16, 1 }
}};

int startepisode;
int startlevel;
int maxplayers;
bool m_serverInfoMessage = false;
double m_serverInfoMessageTime;

void M_Menu_GameOptions_f()
{
    key_dest = key_menu;
    m_state = MenuState::GameOptions;
    m_entersound = true;
    if (maxplayers == 0) {
        maxplayers = svs.maxclients;
    }

    if (maxplayers < 2) {
        maxplayers = svs.maxclientslimit;
    }
}

const std::array<int, 9> gameoptions_cursor_table = { 40, 56, 64, 72, 80, 88, 96, 112, 120 };
constexpr int NUM_GAMEOPTIONS = 9;
int gameoptions_cursor;

void M_GameOptions_Draw()
{
    qpic_t* p;
    int x;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_DrawTextBox(152, 32, 10, 1);
    M_Print(160, 40, "begin game");

    M_Print(0, 56, "      Max players");
    M_Print(160, 56, va("%i", maxplayers));

    M_Print(0, 64, "        Game Type");
    if (coop.value) {
        M_Print(160, 64, "Cooperative");
    } else {
        M_Print(160, 64, "Deathmatch");
    }

    M_Print(0, 72, "        Teamplay");
    if (rogue) {
        const char* msg;

        switch ((int)teamplay.value) {
        case 1:
            msg = "No Friendly Fire";
            break;
        case 2:
            msg = "Friendly Fire";
            break;
        case 3:
            msg = "Tag";
            break;
        case 4:
            msg = "Capture the Flag";
            break;
        case 5:
            msg = "One Flag CTF";
            break;
        case 6:
            msg = "Three Team CTF";
            break;
        default:
            msg = "Off";
            break;
        }
        M_Print(160, 72, msg);
    } else {
        const char* msg;

        switch ((int)teamplay.value) {
        case 1:
            msg = "No Friendly Fire";
            break;
        case 2:
            msg = "Friendly Fire";
            break;
        default:
            msg = "Off";
            break;
        }
        M_Print(160, 72, msg);
    }

    M_Print(0, 80, "            Skill");
    if (skill.value == 0) {
        M_Print(160, 80, "Easy difficulty");
    } else if (skill.value == 1) {
        M_Print(160, 80, "Normal difficulty");
    } else if (skill.value == 2) {
        M_Print(160, 80, "Hard difficulty");
    } else {
        M_Print(160, 80, "Nightmare difficulty");
    }

    M_Print(0, 88, "       Frag Limit");
    if (fraglimit.value == 0) {
        M_Print(160, 88, "none");
    } else {
        M_Print(160, 88, va("%i frags", (int)fraglimit.value));
    }

    M_Print(0, 96, "       Time Limit");
    if (timelimit.value == 0) {
        M_Print(160, 96, "none");
    } else {
        M_Print(160, 96, va("%i minutes", (int)timelimit.value));
    }

    M_Print(0, 112, "         Episode");
    if (hipnotic) {
        M_Print(160, 112, hipnoticepisodes[startepisode].description);
    }
    else if (rogue) {
        M_Print(160, 112, rogueepisodes[startepisode].description);
    } else {
        M_Print(160, 112, episodes[startepisode].description);
    }

    M_Print(0, 120, "           Level");
    if (hipnotic) {
        M_Print(
            160, 120,
            hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
                .description);
        M_Print(
            160, 128,
            hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
                .name);
    }
    else if (rogue) {
        M_Print(160, 120,
            roguelevels[rogueepisodes[startepisode].firstLevel + startlevel]
                .description);
        M_Print(
            160, 128,
            roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
    } else {
        M_Print(160, 120,
            levels[episodes[startepisode].firstLevel + startlevel].description);
        M_Print(160, 128,
            levels[episodes[startepisode].firstLevel + startlevel].name);
    }

    // line cursor
    M_DrawCharacter(144, gameoptions_cursor_table[gameoptions_cursor],
        12 + ((int)(realtime * 4) & 1));

    if (m_serverInfoMessage) {
        if ((realtime - m_serverInfoMessageTime) < 5.0) {
            x = (320 - 26 * 8) / 2;
            M_DrawTextBox(x, 138, 24, 4);
            x += 8;
            M_Print(x, 146, "  More than 4 players   ");
            M_Print(x, 154, " requires using command ");
            M_Print(x, 162, "line parameters; please ");
            M_Print(x, 170, "   see techinfo.txt.    ");
        } else {
            m_serverInfoMessage = false;
        }
    }
}

void M_NetStart_Change(int dir)
{
    int count;

    switch (gameoptions_cursor) {
    case 1:
        maxplayers += dir;
        if (maxplayers > svs.maxclientslimit) {
            maxplayers = svs.maxclientslimit;
            m_serverInfoMessage = true;
            m_serverInfoMessageTime = realtime;
        }

        if (maxplayers < 2) {
            maxplayers = 2;
        }

        break;

    case 2:
        Cvar::SetValue("coop", static_cast<float>(coop.value ? 0 : 1));
        break;

    case 3:
        if (rogue) {
            count = 6;
        } else {
            count = 2;
        }

        Cvar::SetValue("teamplay", static_cast<float>(teamplay.value + dir));
        if (teamplay.value > count) {
            Cvar::SetValue("teamplay", static_cast<float>(0));
        } else if (teamplay.value < 0) {
            Cvar::SetValue("teamplay", static_cast<float>(count));
        }

        break;

    case 4:
        Cvar::SetValue("skill", skill.value + dir);
        if (skill.value > 3) {
            Cvar::SetValue("skill", static_cast<float>(0));
        }

        if (skill.value < 0) {
            Cvar::SetValue("skill", static_cast<float>(3));
        }

        break;

    case 5:
        Cvar::SetValue("fraglimit", static_cast<float>(fraglimit.value + dir * 10));
        if (fraglimit.value > 100) {
            Cvar::SetValue("fraglimit", static_cast<float>(0));
        }

        if (fraglimit.value < 0) {
            Cvar::SetValue("fraglimit", static_cast<float>(100));
        }

        break;

    case 6:
        Cvar::SetValue("timelimit", timelimit.value + dir * 5);
        if (timelimit.value > 60) {
            Cvar::SetValue("timelimit", static_cast<float>(0));
        }

        if (timelimit.value < 0) {
            Cvar::SetValue("timelimit", static_cast<float>(60));
        }

        break;

    case 7:
        startepisode += dir;
        if (hipnotic) {
            count = 6;
        }
        else if (rogue) {
            count = 4;
        } else if (registered.value) {
            count = 7;
        } else {
            count = 2;
        }

        if (startepisode < 0) {
            startepisode = count - 1;
        }

        if (startepisode >= count) {
            startepisode = 0;
        }

        startlevel = 0;
        break;

    case 8:
        startlevel += dir;
        if (hipnotic) {
            count = hipnoticepisodes[startepisode].levels;
        }
        else if (rogue) {
            count = rogueepisodes[startepisode].levels;
        } else {
            count = episodes[startepisode].levels;
        }

        if (startlevel < 0) {
            startlevel = count - 1;
        }

        if (startlevel >= count) {
            startlevel = 0;
        }

        break;
    }
}

void M_GameOptions_Key(int key)
{
    switch (key) {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        gameoptions_cursor--;
        if (gameoptions_cursor < 0) {
            gameoptions_cursor = NUM_GAMEOPTIONS - 1;
        }

        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        gameoptions_cursor++;
        if (gameoptions_cursor >= NUM_GAMEOPTIONS) {
            gameoptions_cursor = 0;
        }

        break;

    case K_LEFTARROW:
        if (gameoptions_cursor == 0) {
            break;
        }

        S_LocalSound("misc/menu3.wav");
        M_NetStart_Change(-1);
        break;

    case K_RIGHTARROW:
        if (gameoptions_cursor == 0) {
            break;
        }

        S_LocalSound("misc/menu3.wav");
        M_NetStart_Change(1);
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        if (gameoptions_cursor == 0) {
            if (sv.active) {
                Cmd::BufferAddText("disconnect\n");
            }

            Cmd::BufferAddText("listen 0\n"); // so host_netport will be re-examined
            Cmd::BufferAddText(va("maxplayers %u\n", maxplayers));
            Screen::GetScreenSystem().BeginLoadingPlaque();

            if (hipnotic) {
                Cmd::BufferAddText(
                    va("map %s\n",
                        hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
                            .name));
            } else if (rogue) {
                Cmd::BufferAddText(va(
                    "map %s\n",
                    roguelevels[rogueepisodes[startepisode].firstLevel + startlevel]
                        .name));
            } else {
                Cmd::BufferAddText(
                    va("map %s\n",
                        levels[episodes[startepisode].firstLevel + startlevel].name));
            }

            return;
        }

        M_NetStart_Change(1);
        break;
    }
}

//=============================================================================
/* SEARCH MENU */

bool searchComplete = false;
double searchCompleteTime;

void M_Menu_Search_f()
{
    key_dest = key_menu;
    m_state = MenuState::Search;
    m_entersound = false;
    slistSilent = true;
    slistLocal = false;
    searchComplete = false;
    NET_Slist_f();
}

void M_Search_Draw()
{
    qpic_t* p;
    int x;

    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    x = (320 / 2) - ((12 * 8) / 2) + 4;
    M_DrawTextBox(x - 8, 32, 12, 1);
    M_Print(x, 40, "Searching...");

    if (slistInProgress) {
        NET_Poll();

        return;
    }

    if (!searchComplete) {
        searchComplete = true;
        searchCompleteTime = realtime;
    }

    if (hostCacheCount) {
        M_Menu_ServerList_f();

        return;
    }

    M_PrintWhite((320 / 2) - ((22 * 8) / 2), 64, "No Quake servers found");
    if ((realtime - searchCompleteTime) < 3.0) {
        return;
    }

    M_Menu_LanConfig_f();
}

void M_Search_Key()
{
}

//=============================================================================
/* SLIST MENU */

int slist_cursor;
bool slist_sorted;

void M_Menu_ServerList_f()
{
    key_dest = key_menu;
    m_state = MenuState::SList;
    m_entersound = true;
    slist_cursor = 0;
    m_return_onerror = false;
    m_return_reason.clear();
    slist_sorted = false;
}

void M_ServerList_Draw()
{
    int n;
    char string[64];
    qpic_t* p;

    if (!slist_sorted) {
        if (hostCacheCount > 1) {
            int i, j;
            hostcache_t temp;
            for (i = 0; i < hostCacheCount; i++) {
                for (j = i + 1; j < hostCacheCount; j++) {
                    if (strcmp(hostcache[j].name, hostcache[i].name) < 0) {
                        Q_memcpy(&temp, &hostcache[j], sizeof(hostcache_t));
                        Q_memcpy(&hostcache[j], &hostcache[i], sizeof(hostcache_t));
                        Q_memcpy(&hostcache[i], &temp, sizeof(hostcache_t));
                    }
                }
            }
        }

        slist_sorted = true;
    }

    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    for (n = 0; n < hostCacheCount; n++) {
        if (hostcache[n].maxusers) {
            sprintf_s(string, sizeof(string), "%-15.15s %-15.15s %2u/%2u\n", hostcache[n].name,
                hostcache[n].map, hostcache[n].users, hostcache[n].maxusers);
        } else {
            sprintf_s(string, sizeof(string), "%-15.15s %-15.15s\n", hostcache[n].name,
                hostcache[n].map);
        }

        M_Print(16, 32 + 8 * n, string);
    }
    M_DrawCharacter(0, 32 + slist_cursor * 8, 12 + ((int)(realtime * 4) & 1));

    if (!m_return_reason.empty()) {
        M_PrintWhite(16, 148, m_return_reason.c_str());
    }
}

void M_ServerList_Key(int k)
{
    switch (k) {
    case K_ESCAPE:
        M_Menu_LanConfig_f();
        break;

    case K_SPACE:
        M_Menu_Search_f();
        break;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        slist_cursor--;
        if (slist_cursor < 0) {
            slist_cursor = hostCacheCount - 1;
        }

        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        slist_cursor++;
        if (slist_cursor >= hostCacheCount) {
            slist_cursor = 0;
        }

        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        m_return_state = m_state;
        m_return_onerror = true;
        slist_sorted = false;
        key_dest = key_game;
        m_state = MenuState::None;
        Cmd::BufferAddText(va("connect \"%s\"\n", hostcache[slist_cursor].cname));
        break;

    default:
        break;
    }
}

//=============================================================================
/* Menu Subsystem */

void M_Init()
{
    Cmd::AddCommand("togglemenu", M_ToggleMenu_f);

    Cmd::AddCommand("menu_main", M_Menu_Main_f);
    Cmd::AddCommand("menu_singleplayer", M_Menu_SinglePlayer_f);
    Cmd::AddCommand("menu_load", M_Menu_Load_f);
    Cmd::AddCommand("menu_save", M_Menu_Save_f);
    Cmd::AddCommand("menu_multiplayer", M_Menu_MultiPlayer_f);
    Cmd::AddCommand("menu_setup", M_Menu_Setup_f);
    Cmd::AddCommand("menu_options", M_Menu_Options_f);
    Cmd::AddCommand("menu_keys", M_Menu_Keys_f);
    Cmd::AddCommand("menu_video", M_Menu_Video_f);
    Cmd::AddCommand("help", M_Menu_Help_f);
    Cmd::AddCommand("menu_quit", M_Menu_Quit_f);
}

void M_Draw()
{
    if (m_state == MenuState::None || key_dest != key_menu) {
        return;
    }

    if (!m_recursiveDraw) {
        Screen::GetScreenSystem().SetCopyeverything(1);

        if (Screen::GetScreenSystem().GetConCurrent()) {
            Draw_ConsoleBackground(vid.height);
            VID_UnlockBuffer();
            S_ExtraUpdate();
            VID_LockBuffer();
        } else {
            Draw_FadeScreen();
        }

        Screen::GetScreenSystem().SetFullupdate(0);
    } else {
        m_recursiveDraw = false;
    }

    switch (m_state) {
    case MenuState::None:
        break;

    case MenuState::Main:
        M_Main_Draw();
        break;

    case MenuState::SinglePlayer:
        M_SinglePlayer_Draw();
        break;

    case MenuState::Load:
        M_Load_Draw();
        break;

    case MenuState::Save:
        M_Save_Draw();
        break;

    case MenuState::MultiPlayer:
        M_MultiPlayer_Draw();
        break;

    case MenuState::Setup:
        M_Setup_Draw();
        break;

    case MenuState::Net:
        M_Net_Draw();
        break;

    case MenuState::Options:
        M_Options_Draw();
        break;

    case MenuState::Keys:
        M_Keys_Draw();
        break;

    case MenuState::Video:
        M_Video_Draw();
        break;

    case MenuState::Help:
        M_Help_Draw();
        break;

    case MenuState::SerialConfig:
        M_SerialConfig_Draw();
        break;

    case MenuState::ModemConfig:
        M_ModemConfig_Draw();
        break;

    case MenuState::LanConfig:
        M_LanConfig_Draw();
        break;

    case MenuState::GameOptions:
        M_GameOptions_Draw();
        break;

    case MenuState::Search:
        M_Search_Draw();
        break;

    case MenuState::SList:
        M_ServerList_Draw();
        break;
    }

    if (m_entersound) {
        S_LocalSound("misc/menu2.wav");
        m_entersound = false;
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
}

void M_Keydown(int key)
{
    switch (m_state) {
    case MenuState::None:
        return;

    case MenuState::Main:
        M_Main_Key(key);
        return;

    case MenuState::SinglePlayer:
        M_SinglePlayer_Key(key);
        return;

    case MenuState::Load:
        M_Load_Key(key);
        return;

    case MenuState::Save:
        M_Save_Key(key);
        return;

    case MenuState::MultiPlayer:
        M_MultiPlayer_Key(key);
        return;

    case MenuState::Setup:
        M_Setup_Key(key);
        return;

    case MenuState::Net:
        M_Net_Key(key);
        return;

    case MenuState::Options:
        M_Options_Key(key);
        return;

    case MenuState::Keys:
        M_Keys_Key(key);
        return;

    case MenuState::Video:
        M_Video_Key(key);
        return;

    case MenuState::Help:
        M_Help_Key(key);
        return;

    case MenuState::SerialConfig:
        M_SerialConfig_Key(key);
        return;

    case MenuState::ModemConfig:
        M_ModemConfig_Key(key);
        return;

    case MenuState::LanConfig:
        M_LanConfig_Key(key);
        return;

    case MenuState::GameOptions:
        M_GameOptions_Key(key);
        return;

    case MenuState::Search:
        M_Search_Key();
        break;

    case MenuState::SList:
        M_ServerList_Key(key);
        return;
    }
}

void M_ConfigureNetSubsystem()
{
    // enable/disable net systems to match desired config

    Cmd::BufferAddText("stopdemo\n");
    if (SerialConfig() || DirectConfig()) {
        Cmd::BufferAddText("com1 enable\n");
    }

    if (IPXConfig() || TCPIPConfig()) {
        net_hostport = lanConfig_port;
    }
}

} // namespace Menu
