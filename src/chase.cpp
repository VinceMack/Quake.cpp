// chase.cpp -- chase camera code

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

cvar_t chase_back = { "chase_back", "100", {}, {}, {}, {} };
cvar_t chase_up = { "chase_up", "16", {}, {}, {}, {} };
cvar_t chase_right = { "chase_right", "0", {}, {}, {}, {} };
cvar_t chase_active = { "chase_active", "0", {}, {}, {}, {} };

Vector3 chase_dest;

void Chase_Init(void)
{
    Cvar::Register(&chase_back);
    Cvar::Register(&chase_up);
    Cvar::Register(&chase_right);
    Cvar::Register(&chase_active);
}

void TraceLine(const Vector3& start, const Vector3& end, Vector3& impact)
{
    trace_t trace;

    trace = {};
    SV_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);

    impact = trace.endpos;
}

void Chase_Update(void)
{
    float dist;
    Vector3 forward, up, right;
    Vector3 dest, stop;

    // if can't see player, reset
    AngleVectors(cl.viewangles, forward, right, up);

    // calc exact destination
    chase_dest = r_refdef.vieworg - forward * chase_back.value - right * chase_right.value;
    chase_dest.z = r_refdef.vieworg.z + chase_up.value;

    // find the spot the player is looking at
    dest = r_refdef.vieworg + forward * 4096.0f;
    TraceLine(r_refdef.vieworg, dest, stop);

    // calculate pitch to look at the same spot from camera
    stop = stop - r_refdef.vieworg;
    dist = stop.dot(forward);
    if (dist < 1) {
        dist = 1;
    }

    r_refdef.viewangles[PITCH] = static_cast<float>(-atan(stop.z / dist) / M_PI * 180);

    // move towards destination
    r_refdef.vieworg = chase_dest;
}

} // namespace Client
