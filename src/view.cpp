// view.cpp -- player eye positioning

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

#include "r_local.hpp"

namespace View {

/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

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

cvar_t gl_cshiftpercent = { "gl_cshiftpercent", "100", false };

float v_dmg_time, v_dmg_roll, v_dmg_pitch;

extern int in_forward, in_forward2, in_back;

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
Vector3 forward, right, up;

float V_CalcRoll(const Vector3& angles, const Vector3& velocity)
{
    float sign;
    float side;
    float value;

    AngleVectors(angles, forward, right, up);
    side = velocity.dot(right);
    sign = static_cast<float>(side < 0 ? -1 : 1);
    side = fabs(side);

    value = cl_rollangle.value;
    //	if (cl.inwater)
    //		value *= 6;

    if (side < cl_rollspeed.value) {
        side = side * value / cl_rollspeed.value;
    } else {
        side = value;
    }

    return side * sign;
}

/*
===============
V_CalcBob

===============
*/
float V_CalcBob(void)
{
    float bob;
    float cycle;

    cycle = static_cast<float>(cl.time - (int)(cl.time / cl_bobcycle.value) * cl_bobcycle.value);
    cycle /= cl_bobcycle.value;
    if (cycle < cl_bobup.value) {
        cycle = static_cast<float>(M_PI * cycle / cl_bobup.value);
    } else {
        cycle = static_cast<float>(M_PI + M_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value));
    }

    // bob is proportional to velocity in the xy plane
    // (don't count Z, or jumping messes it up)

    bob = sqrt(cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
    //Con_Printf ("speed: %5.1f\n", Length(cl.velocity));
    bob = static_cast<float>(bob * 0.3 + bob * 0.7 * sin(cycle));
    if (bob > 4) {
        bob = 4;
    } else if (bob < -7) {
        bob = -7;
    }

    return bob;
}

//=============================================================================

cvar_t v_centermove = { "v_centermove", "0.15", false };
cvar_t v_centerspeed = { "v_centerspeed", "500" };

void V_StartPitchDrift(void)
{
#if 1
    if (cl.laststop == cl.time) {
        return; // something else is keeping it from drifting
    }

#endif
    if (cl.nodrift || !cl.pitchvel) {
        cl.pitchvel = v_centerspeed.value;
        cl.nodrift = false;
        cl.driftmove = 0;
    }
}

void V_StopPitchDrift(void)
{
    cl.laststop = cl.time;
    cl.nodrift = true;
    cl.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when
===============
*/
void V_DriftPitch(void)
{
    float delta, move;

    if (noclip_anglehack || !cl.onground || cls.demoplayback) {
        cl.driftmove = 0;
        cl.pitchvel = 0;

        return;
    }

    // don't count small mouse motion
    if (cl.nodrift) {
        if (fabs(cl.cmd.forwardmove) < cl_forwardspeed.value) {
            cl.driftmove = 0;
        } else {
            cl.driftmove += static_cast<float>(host_frametime);
        }

        if (cl.driftmove > v_centermove.value) {
            V_StartPitchDrift();
        }

        return;
    }

    delta = cl.idealpitch - cl.viewangles[PITCH];

    if (!delta) {
        cl.pitchvel = 0;

        return;
    }

    move = static_cast<float>(host_frametime * cl.pitchvel);
    cl.pitchvel += static_cast<float>(host_frametime * v_centerspeed.value);

    //Con_Printf ("move: %f (%f)\n", move, host_frametime);

    if (delta > 0) {
        if (move > delta) {
            cl.pitchvel = 0;
            move = delta;
        }

        cl.viewangles[PITCH] += move;
    } else if (delta < 0) {
        if (move > -delta) {
            cl.pitchvel = 0;
            move = -delta;
        }

        cl.viewangles[PITCH] -= move;
    }
}

/*
==============================================================================

						PALETTE FLASHES

==============================================================================
*/

cshift_t cshift_empty = { { 130, 80, 50 }, 0 };
static cshift_t cshift_water = { { 130, 80, 50 }, 128 };
cshift_t cshift_slime = { { 0, 25, 5 }, 150 };
cshift_t cshift_lava = { { 255, 80, 0 }, 150 };

cvar_t v_gamma = { "gamma", "1", true };

byte gammatable[256]; // palette is sent through this


void BuildGammaTable(float g)
{
    int i, inf;

    if (g == 1.0) {
        for (i = 0; i < 256; i++) {
            gammatable[i] = static_cast<byte>(i);
        }

        return;
    }

    for (i = 0; i < 256; i++) {
        inf = static_cast<int>(255 * pow((i + 0.5) / 255.5, g) + 0.5);
        if (inf < 0) {
            inf = 0;
        }

        if (inf > 255) {
            inf = 255;
        }

        gammatable[i] = static_cast<byte>(inf);
    }
}

/*
=================
V_CheckGamma
=================
*/
qboolean V_CheckGamma(void)
{
    static float oldgammavalue;

    if (v_gamma.value == oldgammavalue) {
        return false;
    }

    oldgammavalue = v_gamma.value;

    BuildGammaTable(v_gamma.value);
    vid.recalc_refdef = 1; // force a surface cache flush

    return true;
}

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage(void)
{
    int armor, blood;
    Vector3 from;
    Vector3 v_forward, v_right, v_up;
    entity_t* ent;
    float side;
    float count;

    armor = MSG_ReadByte();
    blood = MSG_ReadByte();
    from.x = MSG_ReadCoord();
    from.y = MSG_ReadCoord();
    from.z = MSG_ReadCoord();

    count = static_cast<float>(blood * 0.5 + armor * 0.5);
    if (count < 10) {
        count = 10;
    }

    cl.faceanimtime = static_cast<float>(cl.time + 0.2); // but sbar face into pain frame

    cl.cshifts[CSHIFT_DAMAGE].percent += static_cast<int>(3 * count);
    if (cl.cshifts[CSHIFT_DAMAGE].percent < 0) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 0;
    }

    if (cl.cshifts[CSHIFT_DAMAGE].percent > 150) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 150;
    }

    if (armor > blood) {
        cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
    } else if (armor) {
        cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
    } else {
        cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
        cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
    }

    //
    // calculate view angle kicks
    //
    ent = &cl_entities[cl.viewentity];

    from = from - ent->origin;
    from.normalize();

    AngleVectors(ent->angles, v_forward, v_right, v_up);

    side = from.dot(v_right);
    v_dmg_roll = count * side * v_kickroll.value;

    side = from.dot(v_forward);
    v_dmg_pitch = count * side * v_kickpitch.value;

    v_dmg_time = v_kicktime.value;
}

/*
==================
V_cshift_f
==================
*/
void V_cshift_f(void)
{
    cshift_empty.destcolor[0] = Q_atoi(Cmd::Argv(1));
    cshift_empty.destcolor[1] = Q_atoi(Cmd::Argv(2));
    cshift_empty.destcolor[2] = Q_atoi(Cmd::Argv(3));
    cshift_empty.percent = Q_atoi(Cmd::Argv(4));
}

/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f(void)
{
    cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
    cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
    cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
    cl.cshifts[CSHIFT_BONUS].percent = 50;
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor(int contents)
{
    switch (contents) {
    case CONTENTS_EMPTY:
    case CONTENTS_SOLID:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
        break;
    case CONTENTS_LAVA:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
        break;
    case CONTENTS_SLIME:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
        break;
    default:
        cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
    }
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift(void)
{
    if (cl.items & IT_QUAD) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
        cl.cshifts[CSHIFT_POWERUP].percent = 30;
    } else if (cl.items & IT_SUIT) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
        cl.cshifts[CSHIFT_POWERUP].percent = 20;
    } else if (cl.items & IT_INVISIBILITY) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
        cl.cshifts[CSHIFT_POWERUP].percent = 100;
    } else if (cl.items & IT_INVULNERABILITY) {
        cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
        cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
        cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
        cl.cshifts[CSHIFT_POWERUP].percent = 30;
    } else {
        cl.cshifts[CSHIFT_POWERUP].percent = 0;
    }
}

/*
=============
V_CalcBlend
=============
*/

/*
=============
V_UpdatePalette
=============
*/
void V_UpdatePalette(void)
{
    int i, j;
    qboolean new_shift;
    byte *basepal, *newpal;
    byte pal[768];
    int r, g, b;
    qboolean force;

    V_CalcPowerupCshift();

    new_shift = false;

    for (i = 0; i < NUM_CSHIFTS; i++) {
        if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent) {
            new_shift = true;
            cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
        }

        for (j = 0; j < 3; j++) {
            if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j]) {
                new_shift = true;
                cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
            }
        }
    }

    // drop the damage value
    cl.cshifts[CSHIFT_DAMAGE].percent -= static_cast<int>(host_frametime * 150);
    if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 0;
    }

    // drop the bonus value
    cl.cshifts[CSHIFT_BONUS].percent -= static_cast<int>(host_frametime * 100);
    if (cl.cshifts[CSHIFT_BONUS].percent <= 0) {
        cl.cshifts[CSHIFT_BONUS].percent = 0;
    }

    force = V_CheckGamma();
    if (!new_shift && !force) {
        return;
    }

    basepal = host_basepal;
    newpal = pal;

    for (i = 0; i < 256; i++) {
        r = basepal[0];
        g = basepal[1];
        b = basepal[2];
        basepal += 3;

        for (j = 0; j < NUM_CSHIFTS; j++) {
            r += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[0] - r)) >> 8;
            g += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[1] - g)) >> 8;
            b += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[2] - b)) >> 8;
        }

        newpal[0] = gammatable[r];
        newpal[1] = gammatable[g];
        newpal[2] = gammatable[b];
        newpal += 3;
    }

    VID_ShiftPalette(pal);
}

/*
==============================================================================

						VIEW RENDERING

==============================================================================
*/

float angledelta(float a)
{
    a = anglemod(a);
    if (a > 180) {
        a -= 360;
    }

    return a;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle(void)
{
    float yaw, pitch, move;
    static float oldyaw = 0;
    static float oldpitch = 0;

    yaw = r_refdef.viewangles[YAW];
    pitch = -r_refdef.viewangles[PITCH];

    yaw = static_cast<float>(angledelta(yaw - r_refdef.viewangles[YAW]) * 0.4);
    if (yaw > 10) {
        yaw = 10;
    }

    if (yaw < -10) {
        yaw = -10;
    }

    pitch = static_cast<float>(angledelta(-pitch - r_refdef.viewangles[PITCH]) * 0.4);
    if (pitch > 10) {
        pitch = 10;
    }

    if (pitch < -10) {
        pitch = -10;
    }

    move = static_cast<float>(host_frametime * 20);
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

    cl.viewent.angles[YAW] = r_refdef.viewangles[YAW] + yaw;
    cl.viewent.angles[PITCH] = -(r_refdef.viewangles[PITCH] + pitch);

    cl.viewent.angles[ROLL] -= static_cast<float>(v_idlescale.value * sin(cl.time * v_iroll_cycle.value) * v_iroll_level.value);
    cl.viewent.angles[PITCH] -= static_cast<float>(v_idlescale.value * sin(cl.time * v_ipitch_cycle.value) * v_ipitch_level.value);
    cl.viewent.angles[YAW] -= static_cast<float>(v_idlescale.value * sin(cl.time * v_iyaw_cycle.value) * v_iyaw_level.value);
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets(void)
{
    entity_t* ent;

    ent = &cl_entities[cl.viewentity];

    // absolutely bound refresh reletive to entity clipping hull
    // so the view can never be inside a solid wall

    if (r_refdef.vieworg[0] < ent->origin[0] - 14) {
        r_refdef.vieworg[0] = ent->origin[0] - 14;
    } else if (r_refdef.vieworg[0] > ent->origin[0] + 14) {
        r_refdef.vieworg[0] = ent->origin[0] + 14;
    }

    if (r_refdef.vieworg[1] < ent->origin[1] - 14) {
        r_refdef.vieworg[1] = ent->origin[1] - 14;
    } else if (r_refdef.vieworg[1] > ent->origin[1] + 14) {
        r_refdef.vieworg[1] = ent->origin[1] + 14;
    }

    if (r_refdef.vieworg[2] < ent->origin[2] - 22) {
        r_refdef.vieworg[2] = ent->origin[2] - 22;
    } else if (r_refdef.vieworg[2] > ent->origin[2] + 30) {
        r_refdef.vieworg[2] = ent->origin[2] + 30;
    }
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle(void)
{
    r_refdef.viewangles[ROLL] += static_cast<float>(v_idlescale.value * sin(cl.time * v_iroll_cycle.value) * v_iroll_level.value);
    r_refdef.viewangles[PITCH] += static_cast<float>(v_idlescale.value * sin(cl.time * v_ipitch_cycle.value) * v_ipitch_level.value);
    r_refdef.viewangles[YAW] += static_cast<float>(v_idlescale.value * sin(cl.time * v_iyaw_cycle.value) * v_iyaw_level.value);
}

/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll(void)
{
    float side;

    side = V_CalcRoll(cl_entities[cl.viewentity].angles, cl.velocity);
    r_refdef.viewangles[ROLL] += side;

    if (v_dmg_time > 0) {
        r_refdef.viewangles[ROLL] += v_dmg_time / v_kicktime.value * v_dmg_roll;
        r_refdef.viewangles[PITCH] += v_dmg_time / v_kicktime.value * v_dmg_pitch;
        v_dmg_time -= static_cast<float>(host_frametime);
    }

    if (cl.stats[STAT_HEALTH] <= 0) {
        r_refdef.viewangles[ROLL] = 80; // dead view angle

        return;
    }
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef(void)
{
    entity_t *ent, *view;
    float old;

    // ent is the player model (visible when out of body)
    ent = &cl_entities[cl.viewentity];
    // view is the weapon model (only visible from inside body)
    view = &cl.viewent;

    VectorCopy(ent->origin, r_refdef.vieworg);
    VectorCopy(ent->angles, r_refdef.viewangles);
    view->model = NULL;

    // allways idle in intermission
    old = v_idlescale.value;
    v_idlescale.value = 1;
    V_AddIdle();
    v_idlescale.value = old;
}

/*
==================
V_CalcRefdef

==================
*/
void V_CalcRefdef(void)
{
    entity_t *ent, *view;
    Vector3 v_forward, v_right, v_up;
    Vector3 angles;
    float bob;
    static float oldz = 0;

    V_DriftPitch();

    // ent is the player model (visible when out of body)
    ent = &cl_entities[cl.viewentity];
    // view is the weapon model (only visible from inside body)
    view = &cl.viewent;

    // transform the view offset by the model's matrix to get the offset from
    // model origin for the view
    ent->angles[YAW] = cl.viewangles[YAW]; // the model should face
    // the view dir
    ent->angles[PITCH] = -cl.viewangles[PITCH]; // the model should face
    // the view dir

    bob = V_CalcBob();

    // refresh position
    r_refdef.vieworg = ent->origin;
    r_refdef.vieworg.z += cl.viewheight + bob;

    // never let it sit exactly on a node line, because a water plane can
    // dissapear when viewed with the eye exactly on it.
    // the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
    r_refdef.vieworg += Vector3(1.0f / 32.0f, 1.0f / 32.0f, 1.0f / 32.0f);

    r_refdef.viewangles = cl.viewangles;
    V_CalcViewRoll();
    V_AddIdle();

    // offsets
    angles[PITCH] = -ent->angles[PITCH]; // because entity pitches are
    //  actually backward
    angles[YAW] = ent->angles[YAW];
    angles[ROLL] = ent->angles[ROLL];

    AngleVectors(angles, v_forward, v_right, v_up);

    r_refdef.vieworg += v_forward * scr_ofsx.value + v_right * scr_ofsy.value + v_up * scr_ofsz.value;

    V_BoundOffsets();

    // set up gun position
    view->angles = cl.viewangles;

    CalcGunAngle();

    view->origin = ent->origin;
    view->origin.z += cl.viewheight;

    view->origin += forward * (bob * 0.4f);
    view->origin.z += bob;

    // fudge position around to keep amount of weapon visible
    // roughly equal with different FOV

    if (scr_viewsize.value == 110) {
        view->origin[2] += 1;
    } else if (scr_viewsize.value == 100) {
        view->origin[2] += 2;
    } else if (scr_viewsize.value == 90) {
        view->origin[2] += 1;
    } else if (scr_viewsize.value == 80) {
        view->origin[2] += 0.5;
    }

    view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
    view->frame = cl.stats[STAT_WEAPONFRAME];
    view->colormap = vid.colormap;

    // set up the refresh position
    VectorAdd(r_refdef.viewangles, cl.punchangle, r_refdef.viewangles);

    // smooth out stair step ups
    if (cl.onground && ent->origin[2] - oldz > 0) {
        float steptime;

        steptime = static_cast<float>(cl.time - cl.oldtime);
        if (steptime < 0) {
            //FIXME		I_Error ("steptime < 0");
            steptime = 0;
        }

        oldz += steptime * 80;
        if (oldz > ent->origin[2]) {
            oldz = ent->origin[2];
        }

        if (ent->origin[2] - oldz > 12) {
            oldz = ent->origin[2] - 12;
        }

        r_refdef.vieworg[2] += oldz - ent->origin[2];
        view->origin[2] += oldz - ent->origin[2];
    } else {
        oldz = ent->origin[2];
    }

    if (chase_active.value) {
        Chase_Update();
    }
}

/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
void V_RenderView(void)
{
    if (con_forcedup) {
        return;
    }

    // don't allow cheats in multiplayer
    if (cl.maxclients > 1) {
        Cvar::Set("scr_ofsx", "0");
        Cvar::Set("scr_ofsy", "0");
        Cvar::Set("scr_ofsz", "0");
    }

    if (cl.intermission) { // intermission / finale rendering
        V_CalcIntermissionRefdef();
    } else {
        if (!cl.paused /* && (sv.maxclients > 1 || key_dest == key_game) */) {
            V_CalcRefdef();
        }
    }

    R_PushDlights();

    if (lcd_x.value) {
        //
        // render two interleaved views
        //
        int i;

        vid.rowbytes <<= 1;
        vid.aspect *= 0.5;

        r_refdef.viewangles[YAW] -= lcd_yaw.value;
        for (i = 0; i < 3; i++) {
            r_refdef.vieworg[i] -= right[i] * lcd_x.value;
        }
        R_RenderView();

        vid.buffer += vid.rowbytes >> 1;

        R_PushDlights();

        r_refdef.viewangles[YAW] += lcd_yaw.value * 2;
        for (i = 0; i < 3; i++) {
            r_refdef.vieworg[i] += 2 * right[i] * lcd_x.value;
        }
        R_RenderView();

        vid.buffer -= vid.rowbytes >> 1;

        r_refdef.vrect.height <<= 1;

        vid.rowbytes >>= 1;
        vid.aspect *= 2;
    } else {
        R_RenderView();
    }

    if (crosshair.value) {
        Draw_Character(static_cast<int>(scr_vrect.x + scr_vrect.width / 2 + cl_crossx.value),
            static_cast<int>(scr_vrect.y + scr_vrect.height / 2 + cl_crossy.value), '+');
    }

}

//============================================================================

/*
=============
V_Init
=============
*/
void V_Init(void)
{
    Cmd::AddCommand("v_cshift", V_cshift_f);
    Cmd::AddCommand("bf", V_BonusFlash_f);
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
    Cvar::Register(&gl_cshiftpercent);

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

    BuildGammaTable(1.0); // no gamma yet
    Cvar::Register(&v_gamma);
}

} // namespace View
