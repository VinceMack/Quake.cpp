// view.cpp -- 3D View Setup, Camera Orientation, Damage/Palette Blending Implementation
#include "client/view.hpp"
#include "client/view_blend.hpp"
#include "ui/screen.hpp"
#include "client/chase.hpp"
#include "ui/console.hpp"
#include "ui/hud.hpp"
#include "render/draw2d.hpp"
#include "host/host.hpp"
#include "render/software/sw_local.hpp"
#include "render/software/sw_vid.hpp"
#include "render/software/sw_main.hpp"
#include "quakedef.hpp"
#include "render/software/sw_light.hpp"
#include "core/cmd.hpp"

#include <cmath>

namespace View {

cvar_t lcd_x = { "lcd_x", "0" };
cvar_t lcd_yaw = { "lcd_yaw", "0" };

cvar_t scr_ofsx = { "scr_ofsx", "0", false };
cvar_t scr_ofsy = { "scr_ofsy", "0", false };
cvar_t scr_ofsz = { "scr_ofsz", "0", false };

cvar_t cl_rollspeed = { "cl_rollspeed", "200" };
cvar_t cl_rollangle = { "cl_rollangle", "2.0" };

cvar_t cl_bob = { "cl_bob", "0.02", false };
cvar_t cl_bobcycle = { "cl_bobcycle", "0.6", false };
cvar_t cl_bobup = { "cl_bobup", "0.5", false };

cvar_t v_kicktime = { "v_kicktime", "0.5", false };
cvar_t v_kickroll = { "v_kickroll", "0.6", false };
cvar_t v_kickpitch = { "v_kickpitch", "0.6", false };

cvar_t v_iyaw_cycle = { "v_iyaw_cycle", "2", false };
cvar_t v_iroll_cycle = { "v_iroll_cycle", "0.5", false };
cvar_t v_ipitch_cycle = { "v_ipitch_cycle", "1", false };
cvar_t v_iyaw_level = { "v_iyaw_level", "0.3", false };
cvar_t v_iroll_level = { "v_iroll_level", "0.1", false };
cvar_t v_ipitch_level = { "v_ipitch_level", "0.3", false };

cvar_t v_idlescale = { "v_idlescale", "0", false };

cvar_t crosshair = { "crosshair", "0", true };
cvar_t cl_crossx = { "cl_crossx", "0", false };
cvar_t cl_crossy = { "cl_crossy", "0", false };

float v_dmg_time, v_dmg_roll, v_dmg_pitch;

static Vector3 forward, right, up;

float V_CalcRoll(const Vector3& angles, const Vector3& velocity)
{
    float sign;
    float side;
    float value;
    Math::AngleVectors(angles, forward, right, up);
    side = velocity.dot(right);
    sign = static_cast<float>(side < 0 ? -1 : 1);
    side = std::abs(side);
    value = cl_rollangle.value;
    if (side < cl_rollspeed.value) {
        side = side * value / cl_rollspeed.value;
    } else {
        side = value;
    }
    return side * sign;
}

float V_CalcBob(void)
{
    float bob;
    float cycle;
    cycle = static_cast<float>(Client::cl.time - (int)(Client::cl.time / cl_bobcycle.value) * cl_bobcycle.value);
    cycle /= cl_bobcycle.value;
    if (cycle < cl_bobup.value) {
        cycle = static_cast<float>(M_PI * cycle / cl_bobup.value);
    } else {
        cycle = static_cast<float>(M_PI + M_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value));
    }
    bob = std::sqrt(Client::cl.velocity[0] * Client::cl.velocity[0] + Client::cl.velocity[1] * Client::cl.velocity[1])
        * cl_bob.value;
    bob = static_cast<float>(bob * 0.3 + bob * 0.7 * std::sin(cycle));
    if (bob > 4) {
        bob = 4;
    } else if (bob < -7) {
        bob = -7;
    }
    return bob;
}

cvar_t v_centermove = { "v_centermove", "0.15", false };
cvar_t v_centerspeed = { "v_centerspeed", "500" };

void V_StartPitchDrift(void)
{
    if (Client::cl.laststop == Client::cl.time) {
        return;
    }
    if (Client::cl.nodrift || !Client::cl.pitchvel) {
        Client::cl.pitchvel = v_centerspeed.value;
        Client::cl.nodrift = false;
        Client::cl.driftmove = 0;
    }
}

void V_StopPitchDrift(void)
{
    Client::cl.laststop = Client::cl.time;
    Client::cl.nodrift = true;
    Client::cl.pitchvel = 0;
}

void V_DriftPitch(void)
{
    float delta, move;
    if (Host::noclip_anglehack || !Client::cl.onground || Client::cls.demoplayback) {
        Client::cl.driftmove = 0;
        Client::cl.pitchvel = 0;
        return;
    }
    if (Client::cl.nodrift) {
        if (std::abs(Client::cl.cmd.forwardmove) < Client::cl_forwardspeed.value) {
            Client::cl.driftmove = 0;
        } else {
            Client::cl.driftmove += static_cast<float>(Host::host_frametime);
        }
        if (Client::cl.driftmove > v_centermove.value) {
            V_StartPitchDrift();
        }
        return;
    }
    delta = Client::cl.idealpitch - Client::cl.viewangles[PITCH];
    if (!delta) {
        Client::cl.pitchvel = 0;
        return;
    }
    move = static_cast<float>(Host::host_frametime * Client::cl.pitchvel);
    Client::cl.pitchvel += static_cast<float>(Host::host_frametime * v_centerspeed.value);
    if (delta > 0) {
        if (move > delta) {
            Client::cl.pitchvel = 0;
            move = delta;
        }
        Client::cl.viewangles[PITCH] += move;
    } else if (delta < 0) {
        if (move > -delta) {
            Client::cl.pitchvel = 0;
            move = -delta;
        }
        Client::cl.viewangles[PITCH] -= move;
    }
}

static float angledelta(float a)
{
    a = Math::anglemod(a);
    if (a > 180) {
        a -= 360;
    }
    return a;
}

static void CalcGunAngle(void)
{
    float yaw, pitch, move;
    static float oldyaw = 0;
    static float oldpitch = 0;
    yaw = Render::r_refdef.viewangles[YAW];
    pitch = -Render::r_refdef.viewangles[PITCH];
    yaw = static_cast<float>(angledelta(yaw - Render::r_refdef.viewangles[YAW]) * 0.4);
    if (yaw > 10) {
        yaw = 10;
    }
    if (yaw < -10) {
        yaw = -10;
    }
    pitch = static_cast<float>(angledelta(-pitch - Render::r_refdef.viewangles[PITCH]) * 0.4);
    if (pitch > 10) {
        pitch = 10;
    }
    if (pitch < -10) {
        pitch = -10;
    }
    move = static_cast<float>(Host::host_frametime * 20);
    if (yaw > oldyaw) {
        if (oldyaw + move < yaw) {
            yaw = oldyaw + move;
        }
    } else {
        if (oldyaw - move > yaw) {
            yaw = oldyaw - move;
        }
    }
    if (pitch > oldpitch) {
        if (oldpitch + move < pitch) {
            pitch = oldpitch + move;
        }
    } else {
        if (oldpitch - move > pitch) {
            pitch = oldpitch - move;
        }
    }
    oldyaw = yaw;
    oldpitch = pitch;
    Client::cl.viewent.angles[YAW] = Render::r_refdef.viewangles[YAW] + yaw;
    Client::cl.viewent.angles[PITCH] = -(Render::r_refdef.viewangles[PITCH] + pitch);
    Client::cl.viewent.angles[ROLL] -= static_cast<float>(
        v_idlescale.value * std::sin(Client::cl.time * v_iroll_cycle.value) * v_iroll_level.value);
    Client::cl.viewent.angles[PITCH] -= static_cast<float>(
        v_idlescale.value * std::sin(Client::cl.time * v_ipitch_cycle.value) * v_ipitch_level.value);
    Client::cl.viewent.angles[YAW]
        -= static_cast<float>(v_idlescale.value * std::sin(Client::cl.time * v_iyaw_cycle.value) * v_iyaw_level.value);
}

static void V_BoundOffsets(void)
{
    entity_t* ent;
    ent = &Client::cl_entities[Client::cl.viewentity];
    if (Render::r_refdef.vieworg[0] < ent->origin[0] - 14) {
        Render::r_refdef.vieworg[0] = ent->origin[0] - 14;
    } else if (Render::r_refdef.vieworg[0] > ent->origin[0] + 14) {
        Render::r_refdef.vieworg[0] = ent->origin[0] + 14;
    }
    if (Render::r_refdef.vieworg[1] < ent->origin[1] - 14) {
        Render::r_refdef.vieworg[1] = ent->origin[1] - 14;
    } else if (Render::r_refdef.vieworg[1] > ent->origin[1] + 14) {
        Render::r_refdef.vieworg[1] = ent->origin[1] + 14;
    }
    if (Render::r_refdef.vieworg[2] < ent->origin[2] - 22) {
        Render::r_refdef.vieworg[2] = ent->origin[2] - 22;
    } else if (Render::r_refdef.vieworg[2] > ent->origin[2] + 30) {
        Render::r_refdef.vieworg[2] = ent->origin[2] + 30;
    }
}

static void V_AddIdle(void)
{
    Render::r_refdef.viewangles[ROLL] += static_cast<float>(
        v_idlescale.value * std::sin(Client::cl.time * v_iroll_cycle.value) * v_iroll_level.value);
    Render::r_refdef.viewangles[PITCH] += static_cast<float>(
        v_idlescale.value * std::sin(Client::cl.time * v_ipitch_cycle.value) * v_ipitch_level.value);
    Render::r_refdef.viewangles[YAW]
        += static_cast<float>(v_idlescale.value * std::sin(Client::cl.time * v_iyaw_cycle.value) * v_iyaw_level.value);
}

static void V_CalcViewRoll(void)
{
    float side;
    side = V_CalcRoll(Client::cl_entities[Client::cl.viewentity].angles, Client::cl.velocity);
    Render::r_refdef.viewangles[ROLL] += side;
    if (v_dmg_time > 0) {
        Render::r_refdef.viewangles[ROLL] += v_dmg_time / v_kicktime.value * v_dmg_roll;
        Render::r_refdef.viewangles[PITCH] += v_dmg_time / v_kicktime.value * v_dmg_pitch;
        v_dmg_time -= static_cast<float>(Host::host_frametime);
    }
    if (Client::cl.stats[STAT_HEALTH] <= 0) {
        Render::r_refdef.viewangles[ROLL] = 80;
        return;
    }
}

static void V_CalcIntermissionRefdef(void)
{
    entity_t *ent, *view;
    float old;
    ent = &Client::cl_entities[Client::cl.viewentity];
    view = &Client::cl.viewent;
    VectorCopy(ent->origin, Render::r_refdef.vieworg);
    VectorCopy(ent->angles, Render::r_refdef.viewangles);
    view->model = NULL;
    old = v_idlescale.value;
    v_idlescale.value = 1;
    V_AddIdle();
    v_idlescale.value = old;
}

static void V_CalcRefdef(void)
{
    entity_t *ent, *view;
    Vector3 v_forward, v_right, v_up;
    Vector3 angles;
    float bob;
    static float oldz = 0;
    V_DriftPitch();
    ent = &Client::cl_entities[Client::cl.viewentity];
    view = &Client::cl.viewent;
    ent->angles[YAW] = Client::cl.viewangles[YAW];
    ent->angles[PITCH] = -Client::cl.viewangles[PITCH];
    bob = V_CalcBob();
    Render::r_refdef.vieworg = ent->origin;
    Render::r_refdef.vieworg.z += Client::cl.viewheight + bob;
    Render::r_refdef.vieworg += Vector3(1.0f / 32.0f, 1.0f / 32.0f, 1.0f / 32.0f);
    Render::r_refdef.viewangles = Client::cl.viewangles;
    V_CalcViewRoll();
    V_AddIdle();
    angles[PITCH] = -ent->angles[PITCH];
    angles[YAW] = ent->angles[YAW];
    angles[ROLL] = ent->angles[ROLL];
    Math::AngleVectors(angles, v_forward, v_right, v_up);
    Render::r_refdef.vieworg += v_forward * scr_ofsx.value + v_right * scr_ofsy.value + v_up * scr_ofsz.value;
    V_BoundOffsets();
    view->angles = Client::cl.viewangles;
    CalcGunAngle();
    view->origin = ent->origin;
    view->origin.z += Client::cl.viewheight;
    view->origin += forward * (bob * 0.4f);
    view->origin.z += bob;
    float viewsize_val = Screen::GetScreenSystem().GetViewsize().value;
    if (viewsize_val == 110) {
        view->origin[2] += 1;
    } else if (viewsize_val == 100) {
        view->origin[2] += 2;
    } else if (viewsize_val == 90) {
        view->origin[2] += 1;
    } else if (viewsize_val == 80) {
        view->origin[2] += 0.5f;
    }
    view->model = Client::cl.model_precache[Client::cl.stats[STAT_WEAPON]];
    view->frame = Client::cl.stats[STAT_WEAPONFRAME];
    view->colormap = Vid::vid.colormap;
    VectorAdd(Render::r_refdef.viewangles, Client::cl.punchangle, Render::r_refdef.viewangles);
    if (Client::cl.onground && ent->origin[2] - oldz > 0) {
        float steptime;
        steptime = static_cast<float>(Client::cl.time - Client::cl.oldtime);
        if (steptime < 0) {
            steptime = 0;
        }
        oldz += steptime * 80;
        if (oldz > ent->origin[2]) {
            oldz = ent->origin[2];
        }
        if (ent->origin[2] - oldz > 12) {
            oldz = ent->origin[2] - 12;
        }
        Render::r_refdef.vieworg[2] += oldz - ent->origin[2];
        view->origin[2] += oldz - ent->origin[2];
    } else {
        oldz = ent->origin[2];
    }
    if (Client::chase_active.value) {
        Client::Chase_Update();
    }
}

void V_RenderView(void)
{
    if (Console::GetConsoleSystem().IsForcedUp()) {
        return;
    }
    if (Client::cl.maxclients > 1) {
        Cvar::Set("scr_ofsx", "0");
        Cvar::Set("scr_ofsy", "0");
        Cvar::Set("scr_ofsz", "0");
    }
    if (Client::cl.intermission) {
        V_CalcIntermissionRefdef();
    } else {
        if (!Client::cl.paused) {
            V_CalcRefdef();
        }
    }
    Render::R_PushDlights();
    if (lcd_x.value) {
        int i;
        Vid::vid.rowbytes <<= 1;
        Vid::vid.aspect *= 0.5;
        Render::r_refdef.viewangles[YAW] -= lcd_yaw.value;
        for (i = 0; i < 3; i++) {
            Render::r_refdef.vieworg[i] -= right[i] * lcd_x.value;
        }
        Render::R_RenderView();
        Vid::vid.buffer += Vid::vid.rowbytes >> 1;
        Render::R_PushDlights();
        Render::r_refdef.viewangles[YAW] += lcd_yaw.value * 2;
        for (i = 0; i < 3; i++) {
            Render::r_refdef.vieworg[i] += 2 * right[i] * lcd_x.value;
        }
        Render::R_RenderView();
        Vid::vid.buffer -= Vid::vid.rowbytes >> 1;
        Render::r_refdef.vrect.height <<= 1;
        Vid::vid.rowbytes >>= 1;
        Vid::vid.aspect *= 2;
    } else {
        Render::R_RenderView();
    }
    if (crosshair.value) {
        const auto& vrect = Screen::GetScreenSystem().GetVrect();
        Draw::Draw_Character(static_cast<int>(vrect.x + vrect.width / 2 + cl_crossx.value),
            static_cast<int>(vrect.y + vrect.height / 2 + cl_crossy.value), '+');
    }
}

void V_Init(void)
{
    V_InitBlend();
    Cmd::AddCommand("centerview", V_StartPitchDrift);
    Cvar::Register(&lcd_x);
    Cvar::Register(&lcd_yaw);
    Cvar::Register(&v_centermove);
    Cvar::Register(&v_centerspeed);
    Cvar::Register(&v_iyaw_cycle);
    Cvar::Register(&v_iroll_cycle);
    Cvar::Register(&v_ipitch_cycle);
    Cvar::Register(&v_iyaw_level);
    Cvar::Register(&v_iroll_level);
    Cvar::Register(&v_ipitch_level);
    Cvar::Register(&v_idlescale);
    Cvar::Register(&crosshair);
    Cvar::Register(&cl_crossx);
    Cvar::Register(&cl_crossy);
    Cvar::Register(&scr_ofsx);
    Cvar::Register(&scr_ofsy);
    Cvar::Register(&scr_ofsz);
    Cvar::Register(&cl_rollspeed);
    Cvar::Register(&cl_rollangle);
    Cvar::Register(&cl_bob);
    Cvar::Register(&cl_bobcycle);
    Cvar::Register(&cl_bobup);
    Cvar::Register(&v_kicktime);
    Cvar::Register(&v_kickroll);
    Cvar::Register(&v_kickpitch);
}

} // namespace View
