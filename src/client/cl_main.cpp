// cl_main.cpp -- Client Subsystem Lifecycle, Connection, and Frame Orchestration Implementation
#include "client/cl_main.hpp"
#include "client/cl_input.hpp"
#include "client/cl_parse.hpp"
#include "client/cl_tent.hpp"
#include "client/cl_demo.hpp"
#include "client/chase.hpp"
#include "host/host.hpp"
#include "client/input.hpp"
#include "network/net_main.hpp"
#include "render/software/sw_vid.hpp"
#include "render/software/sw_efrag.hpp"
#include "audio/audio_main.hpp"
#include "render/software/sw_part.hpp"
#include "ui/screen.hpp"
#include "network/protocol.hpp"
#include "core/print.hpp"
#include "server/server_types.hpp"
#include "core/cmd.hpp"

namespace Client {

cvar_t cl_name = { "_cl_name", "player", true, { }, { }, { } };
cvar_t cl_color = { "_cl_color", "0", true, { }, { }, { } };
cvar_t cl_shownet = { "cl_shownet", "0", { }, { }, { }, { } };
cvar_t cl_nolerp = { "cl_nolerp", "0", { }, { }, { }, { } };
cvar_t lookspring = { "lookspring", "0", true, { }, { }, { } };
cvar_t lookstrafe = { "lookstrafe", "0", true, { }, { }, { } };
cvar_t sensitivity = { "sensitivity", "3", true, { }, { }, { } };
cvar_t m_pitch = { "m_pitch", "0.022", true, { }, { }, { } };
cvar_t m_yaw = { "m_yaw", "0.022", true, { }, { }, { } };
cvar_t m_forward = { "m_forward", "1", true, { }, { }, { } };
cvar_t m_side = { "m_side", "0.8", true, { }, { }, { } };
cvar_t cl_upspeed = { "cl_upspeed", "200", { }, { }, { }, { } };
cvar_t cl_forwardspeed = { "cl_forwardspeed", "200", true, { }, { }, { } };
cvar_t cl_backspeed = { "cl_backspeed", "200", true, { }, { }, { } };
cvar_t cl_sidespeed = { "cl_sidespeed", "350", { }, { }, { }, { } };
cvar_t cl_movespeedkey = { "cl_movespeedkey", "2.0", { }, { }, { }, { } };
cvar_t cl_yawspeed = { "cl_yawspeed", "140", { }, { }, { }, { } };
cvar_t cl_pitchspeed = { "cl_pitchspeed", "150", { }, { }, { }, { } };
cvar_t cl_anglespeedkey = { "cl_anglespeedkey", "1.5", { }, { }, { }, { } };

ClientSubsystem& GetClientSubsystem() noexcept
{
    static ClientSubsystem subsystem;
    return subsystem;
}

int cl_numvisedicts = 0;
entity_t* cl_visedicts[MAX_VISEDICTS];

entity_t* CL_EntityNum(int num)
{
    if (num >= MAX_EDICTS) Host::Host_Error("CL_EntityNum: %i is an invalid number", num);
    while (cl.num_entities <= num) {
        cl_entities[cl.num_entities].colormap = Vid::vid.colormap;
        cl.num_entities++;
    }
    return &cl_entities[num];
}

void CL_ClearState()
{
    if (!Server::sv.active) Host::Host_ClearMemory();
    cl = { };
    Common::SZ_Clear(&cls.message);
    cl_efrags.fill({ });
    cl_entities.fill({ });
    cl_static_entities.fill({ });
    cl_lightstyle.fill({ });
    cl_temp_entities.fill({ });
    cl_beams.fill({ });
    cl_dlights.fill({ });

    cl.free_efrags = cl_efrags.data();
    for (size_t i = 0; i < MAX_EFRAGS - 1; ++i) cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];
    cl.free_efrags[MAX_EFRAGS - 1].entnext = nullptr;
}

void CL_Disconnect()
{
    Audio::S_StopAllSounds(true);
    if (cls.demoplayback)
        CL_StopPlayback();
    else if (cls.state == ca_connected) {
        if (cls.demorecording) CL_Stop_f();
        Console::Con_DPrintf("Sending clc_disconnect\n");
        Common::SZ_Clear(&cls.message);
        Common::MSG_WriteByte(&cls.message, clc_disconnect);
        Net::NET_SendUnreliableMessage(cls.netcon, &cls.message);
        Common::SZ_Clear(&cls.message);
        Net::NET_Close(cls.netcon);
        cls.state = ca_disconnected;
        if (Server::sv.active) Host::Host_ShutdownServer(false);
    }
    cls.demoplayback = cls.timedemo = false;
    cls.signon = 0;
}

void CL_Disconnect_f()
{
    CL_Disconnect();
    if (Server::sv.active) Host::Host_ShutdownServer(false);
}

void CL_EstablishConnection(const char* host)
{
    if (cls.state == ca_dedicated || cls.demoplayback) return;
    CL_Disconnect();
    cls.netcon = Net::NET_Connect(host);
    if (!cls.netcon) Host::Host_Error("CL_Connect: connect failed\n");
    Console::Con_DPrintf("CL_EstablishConnection: connected to %s\n", host);
    cls.demonum = -1;
    cls.state = ca_connected;
    cls.signon = 0;
}

void CL_SignonReply()
{
    Console::Con_DPrintf("CL_SignonReply: %i\n", cls.signon);
    auto WriteCmd = [](const char* cmd) {
        Common::MSG_WriteByte(&cls.message, clc_stringcmd);
        Common::MSG_WriteString(&cls.message, cmd);
    };
    switch (cls.signon) {
    case 1:
        WriteCmd("prespawn");
        break;
    case 2:
        WriteCmd(Common::va("name \"%s\"\n", cl_name.string.c_str()));
        WriteCmd(
            Common::va("color %i %i\n", static_cast<int>(cl_color.value) >> 4, static_cast<int>(cl_color.value) & 15));
        WriteCmd(("spawn " + std::string(cls.spawnparms.data())).c_str());
        break;
    case 3:
        WriteCmd("begin");
        break;
    case 4:
        Screen::GetScreenSystem().EndLoadingPlaque();
        break;
    }
}

void CL_PrintEntities_f()
{
    int i = 0;
    for (const auto& ent : std::span(cl_entities.data(), cl.num_entities)) {
        Console::Con_Printf("%3i:", i++);
        if (!ent.model) {
            Console::Con_Printf("EMPTY\n");
            continue;
        }
        Console::Con_Printf("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n", ent.model->name, ent.frame,
            ent.origin[0], ent.origin[1], ent.origin[2], ent.angles[0], ent.angles[1], ent.angles[2]);
    }
}

static float CL_LerpPoint()
{
    float f = static_cast<float>(cl.mtime[0] - cl.mtime[1]);
    if (!f || cl_nolerp.value || cls.timedemo || Server::sv.active) {
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
            if (d > 180.0f)
                d -= 360.0f;
            else if (d < -180.0f)
                d += 360.0f;
            cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
        }
    }
    if (cl.num_entities <= 1) return;
    const float bobjrotate = Math::anglemod(static_cast<float>(100.0 * cl.time));
    int i = 1;

    for (auto& ent : std::span(cl_entities.data() + 1, cl.num_entities - 1)) {
        if (!ent.model) {
            if (ent.forcelink) Render::R_RemoveEfrags(&ent);
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
            if (std::abs(delta.x) > 100.0f || std::abs(delta.y) > 100.0f || std::abs(delta.z) > 100.0f) f = 1.0f;
            ent.origin = ent.msg_origins[1] + delta * f;
            for (int j = 0; j < 3; ++j) {
                float d = ent.msg_angles[0][j] - ent.msg_angles[1][j];
                if (d > 180.0f)
                    d -= 360.0f;
                else if (d < -180.0f)
                    d += 360.0f;
                ent.angles[j] = ent.msg_angles[1][j] + f * d;
            }
        }

        if (ent.model->flags & EF_ROTATE) ent.angles[1] = bobjrotate;
        if (ent.effects & EF_BRIGHTFIELD) Render::R_EntityParticles(&ent);

        auto AddLight = [&](float base_rad, float die_off, float z_off = 16.0f, bool fwd = false) {
            if (auto* dl = CL_AllocDlight(i)) {
                dl->origin = ent.origin;
                dl->origin.z += z_off;
                if (fwd) {
                    Vector3 fv, rv, uv;
                    Math::AngleVectors(ent.angles, fv, rv, uv);
                    dl->origin += fv * 18.0f;
                }
                dl->radius = base_rad + static_cast<float>(rand() & 31);
                dl->die = static_cast<float>(cl.time + die_off);
            }
        };

        if (ent.effects & EF_MUZZLEFLASH) AddLight(200.0f, 0.1f, 16.0f, true);
        if (ent.effects & EF_BRIGHTLIGHT) AddLight(400.0f, 0.001f);
        if (ent.effects & EF_DIMLIGHT) AddLight(200.0f, 0.001f, 0.0f);

        constexpr auto trail_map = std::array<std::pair<int, int>, 7> { { { EF_GIB, 2 }, { EF_ZOMGIB, 4 },
            { EF_TRACER, 3 }, { EF_TRACER2, 5 }, { EF_GRENADE, 1 }, { EF_TRACER3, 6 }, { EF_ROCKET, 0 } } };
        for (auto [flag, type] : trail_map) {
            if (ent.model->flags & flag) {
                Render::R_RocketTrail(oldorg, ent.origin, type);
                if (flag == EF_ROCKET) {
                    if (auto* dl = CL_AllocDlight(i)) {
                        dl->origin = ent.origin;
                        dl->radius = 200.0f;
                        dl->die = static_cast<float>(cl.time + 0.01);
                    }
                }
                break;
            }
        }

        ent.forcelink = false;
        if ((i != cl.viewentity || chase_active.value) && cl_numvisedicts < MAX_VISEDICTS) {
            cl_visedicts[cl_numvisedicts++] = &ent;
        }
        ++i;
    }
}

int CL_ReadFromServer()
{
    cl.oldtime = cl.time;
    cl.time += Host::host_frametime;
    int ret;
    do {
        ret = CL_GetMessage();
        if (ret == -1) Host::Host_Error("CL_ReadFromServer: lost server connection");
        if (!ret) break;
        cl.last_received_message = static_cast<float>(Host::realtime);
        CL_ParseServerMessage();
    } while (ret && cls.state == ca_connected);

    if (cl_shownet.value) Console::Con_Printf("\n");
    CL_RelinkEntities();
    CL_UpdateTEnts();
    return 0;
}

void CL_SendCmd()
{
    if (cls.state != ca_connected) return;
    if (cls.signon == SIGNONS) {
        usercmd_t cmd { };
        CL_BaseMove(&cmd);
        Input::IN_Move(&cmd);
        CL_SendMove(&cmd);
    }
    if (cls.demoplayback) {
        Common::SZ_Clear(&cls.message);
        return;
    }
    if (!cls.message.cursize) return;
    if (!Net::NET_CanSendMessage(cls.netcon)) {
        Console::Con_DPrintf("CL_WriteToServer: can't send\n");
        return;
    }
    if (Net::NET_SendMessage(cls.netcon, &cls.message) == -1)
        Host::Host_Error("CL_WriteToServer: lost server connection");
    Common::SZ_Clear(&cls.message);
}

struct CmdPair {
    const char* name;
    void (*fn)();
};

void CL_Init()
{
    Common::SZ_Init(&cls.message, cls.message_buf);
    CL_InitInput();
    CL_InitTEnts();
    for (auto* c : { &cl_name, &cl_color, &cl_upspeed, &cl_forwardspeed, &cl_backspeed, &cl_sidespeed, &cl_movespeedkey,
             &cl_yawspeed, &cl_pitchspeed, &cl_anglespeedkey, &cl_shownet, &cl_nolerp, &lookspring, &lookstrafe,
             &sensitivity, &m_pitch, &m_yaw, &m_forward, &m_side }) {
        Cvar::Register(c);
    }
    constexpr CmdPair cmds[]
        = { { "entities", CL_PrintEntities_f }, { "disconnect", CL_Disconnect_f }, { "record", CL_Record_f },
              { "stop", CL_Stop_f }, { "playdemo", CL_PlayDemo_f }, { "timedemo", CL_TimeDemo_f } };
    for (auto [name, fn] : cmds) Cmd::AddCommand(name, fn);
}

} // namespace Client

namespace Cmd {

// Sends the current console command to the server as a clc_stringcmd. Declared in
// core/cmd.hpp; defined here because it needs the client's connection state.
void ForwardToServer(void)
{
    if (Client::cls.state != ca_connected) {
        std::string_view cmd_name = Argv(0);
        Console::Con_Printf("Can't \"%.*s\", not connected\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }
    if (Client::cls.demoplayback) return;
    Common::MSG_WriteByte(&Client::cls.message, clc_stringcmd);
    std::string argv0(Argv(0).data(), Argv(0).length());
    if (Common::Q_strcasecmp(argv0.c_str(), "cmd") != 0) {
        Common::SZ_Print(&Client::cls.message, argv0.c_str());
        Common::SZ_Print(&Client::cls.message, " ");
    }
    if (Argc() > 1) {
        std::string args_str(Args().data(), Args().length());
        Common::SZ_Print(&Client::cls.message, args_str.c_str());
    } else {
        Common::SZ_Print(&Client::cls.message, "\n");
    }
}

} // namespace Cmd
