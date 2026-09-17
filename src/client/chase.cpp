// chase.cpp -- Chase Camera Subsystem Implementation
#include "client/chase.hpp"
#include "client/client_types.hpp"
#include "server/world.hpp"
#include "render/software/sw_local.hpp"
#include "quakedef.hpp"

#include <cmath>
#include <numbers>

namespace Client {

namespace {
cvar_t chase_back = { "chase_back", "100", {}, {}, {}, {} };
cvar_t chase_up   = { "chase_up", "16", {}, {}, {}, {} };
cvar_t chase_right= { "chase_right", "0", {}, {}, {}, {} };
Vector3 chase_dest;

void TraceLine(const Vector3& start, const Vector3& end, Vector3& impact) {
    trace_t trace{};
    SV_RecursiveHullCheck(cl.worldmodel->hulls, 0, 0, 1, start, end, &trace);
    impact = trace.endpos;
}
} // anonymous namespace

cvar_t chase_active = { "chase_active", "0", {}, {}, {}, {} };

void Chase_Init() {
    for (auto* c : { &chase_back, &chase_up, &chase_right, &chase_active }) {
        Cvar::Register(c);
    }
}

void Chase_Update() {
    Vector3 forward, up, right;
    Math::AngleVectors(cl.viewangles, forward, right, up);
    chase_dest = Render::r_refdef.vieworg - forward * chase_back.value - right * chase_right.value;
    chase_dest.z = Render::r_refdef.vieworg.z + chase_up.value;
    Vector3 stop;
    TraceLine(Render::r_refdef.vieworg, Render::r_refdef.vieworg + forward * 4096.0f, stop);
    stop = stop - Render::r_refdef.vieworg;
    float dist = std::max(1.0f, stop.dot(forward));
    Render::r_refdef.viewangles[PITCH] = static_cast<float>(-std::atan(stop.z / dist) / std::numbers::pi * 180.0f);
    Render::r_refdef.vieworg = chase_dest;
}

} // namespace Client
