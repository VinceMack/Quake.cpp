// chase.cpp -- chase camera code
#include <cmath>
#include <numbers>
#include "quakedef.hpp"

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

} // namespace

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
    // if can't see player, reset
    Vector3 forward;
    Vector3 up;
    Vector3 right;
    AngleVectors(cl.viewangles, forward, right, up);

    // calc exact destination
    chase_dest = r_refdef.vieworg - forward * chase_back.value - right * chase_right.value;
    chase_dest.z = r_refdef.vieworg.z + chase_up.value;

    // find the spot the player is looking at
    Vector3 dest = r_refdef.vieworg + forward * 4096.0f;
    Vector3 stop;
    TraceLine(r_refdef.vieworg, dest, stop);

    // calculate pitch to look at the same spot from camera
    stop = stop - r_refdef.vieworg;
    float dist = stop.dot(forward);
    if (dist < 1.0f) {
        dist = 1.0f;
    }

    r_refdef.viewangles[PITCH] = static_cast<float>(-std::atan(stop.z / dist) / std::numbers::pi * 180.0f);

    // move towards destination
    r_refdef.vieworg = chase_dest;
}

} // namespace Client
