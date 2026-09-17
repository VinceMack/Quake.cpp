// view_blend.cpp -- Palette Blending, Color Shifts, and Damage Flashes Implementation
#include "quakedef.hpp"
#include "client/view_blend.hpp"
#include "client/view.hpp"
#include "client/screen.hpp"
#include "render/draw2d.hpp"
#include "render/software/sw_vid.hpp"

#include <cmath>

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
using namespace Wad;
using namespace Cvar;
using namespace Cmd;
using namespace Client;

namespace View {

static cshift_t cshift_empty = { { 130, 80, 50 }, 0 };
static cshift_t cshift_water = { { 130, 80, 50 }, 128 };
static cshift_t cshift_slime = { { 0, 25, 5 }, 150 };
static cshift_t cshift_lava  = { { 255, 80, 0 }, 150 };

cvar_t v_gamma = { "gamma", "1", true };
cvar_t gl_cshiftpercent = { "gl_cshiftpercent", "100", false };

std::array<byte, 256> gammatable{};

static void BuildGammaTable(float g)
{
    int i, inf;
    if (g == 1.0f) {
        for (i = 0; i < 256; i++) {
            gammatable[i] = static_cast<byte>(i);
        }
        return;
    }
    for (i = 0; i < 256; i++) {
        inf = static_cast<int>(255.0 * std::pow((i + 0.5) / 255.5, g) + 0.5);
        if (inf < 0) {
            inf = 0;
        }
        if (inf > 255) {
            inf = 255;
        }
        gammatable[i] = static_cast<byte>(inf);
    }
}

static qboolean V_CheckGamma(void)
{
    static float oldgammavalue;
    if (v_gamma.value == oldgammavalue) {
        return false;
    }
    oldgammavalue = v_gamma.value;
    BuildGammaTable(v_gamma.value);
    vid.recalc_refdef = 1;
    return true;
}

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
    cl.faceanimtime = static_cast<float>(cl.time + 0.2);
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

static void V_cshift_f(void)
{
    cshift_empty.destcolor[0] = Q_atoi(Cmd::Argv(1));
    cshift_empty.destcolor[1] = Q_atoi(Cmd::Argv(2));
    cshift_empty.destcolor[2] = Q_atoi(Cmd::Argv(3));
    cshift_empty.percent = Q_atoi(Cmd::Argv(4));
}

static void V_BonusFlash_f(void)
{
    cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
    cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
    cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
    cl.cshifts[CSHIFT_BONUS].percent = 50;
}

void V_BonusFlash()
{
    V_BonusFlash_f();
}

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

static void V_CalcPowerupCshift(void)
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
    cl.cshifts[CSHIFT_DAMAGE].percent -= static_cast<int>(host_frametime * 150);
    if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0) {
        cl.cshifts[CSHIFT_DAMAGE].percent = 0;
    }
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

void V_InitBlend()
{
    Cmd::AddCommand("v_cshift", V_cshift_f);
    Cmd::AddCommand("bf", V_BonusFlash_f);
    Cvar::Register(&gl_cshiftpercent);
    BuildGammaTable(1.0f);
    Cvar::Register(&v_gamma);
}

} // namespace View
