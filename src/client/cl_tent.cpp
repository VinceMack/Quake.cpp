// cl_tent.cpp -- Dynamic Lights, Temporary Entities, and Beams Implementation
#include "quakedef.hpp"
#include "client/cl_tent.hpp"
#include "client/cl_main.hpp"
#include "client/cl_parse.hpp"

#include <numbers>
#include <cmath>

using namespace Common;
using namespace Console;
using namespace Host;
using namespace Net;
using namespace Audio;
using namespace Model;
using namespace Render;
using namespace Math;
using namespace Vid;

namespace Client {

static int num_temp_entities = 0;
static sfx_t *cl_sfx_wizhit = nullptr;
static sfx_t *cl_sfx_knighthit = nullptr;
static sfx_t *cl_sfx_tink1 = nullptr;
static sfx_t *cl_sfx_ric1 = nullptr;
static sfx_t *cl_sfx_ric2 = nullptr;
static sfx_t *cl_sfx_ric3 = nullptr;
static sfx_t *cl_sfx_r_exp3 = nullptr;

dlight_t* CL_AllocDlight(int key) {
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

void CL_DecayLights() {
    const float dt = static_cast<float>(cl.time - cl.oldtime);
    for (auto& dl : cl_dlights) {
        if (dl.die >= cl.time && dl.radius > 0.0f) {
            dl.radius = std::max(0.0f, dl.radius - dt * dl.decay);
        }
    }
}

void CL_InitTEnts() {
    cl_sfx_wizhit    = S_PrecacheSound("wizard/hit.wav");
    cl_sfx_knighthit = S_PrecacheSound("hknight/hit.wav");
    cl_sfx_tink1     = S_PrecacheSound("weapons/tink1.wav");
    cl_sfx_ric1      = S_PrecacheSound("weapons/ric1.wav");
    cl_sfx_ric2      = S_PrecacheSound("weapons/ric2.wav");
    cl_sfx_ric3      = S_PrecacheSound("weapons/ric3.wav");
    cl_sfx_r_exp3    = S_PrecacheSound("weapons/r_exp3.wav");
}

void CL_ParseBeam(model_t* m) {
    const int ent = MSG_ReadShort();
    const Vector3 start{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    const Vector3 end{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
    for (int pass = 0; pass < 2; ++pass) {
        for (auto& b : cl_beams) {
            if (pass == 0 ? (b.entity == ent) : (!b.model || b.endtime < cl.time)) {
                b.entity = ent;
                b.model = m;
                b.endtime = static_cast<float>(cl.time + 0.2);
                b.start = start;
                b.end = end;
                return;
            }
        }
    }
    Con_Printf("beam list overflow!\n");
}

void CL_ParseTEnt() {
    const int type = MSG_ReadByte();
    auto PosSnd = [](sfx_t* sfx, int color = 0, int count = 0) {
        const Vector3 pos{ MSG_ReadCoord(), MSG_ReadCoord(), MSG_ReadCoord() };
        if (count) R_RunParticleEffect(pos, vec3_origin, color, count);
        if (sfx) S_StartSound(-1, 0, sfx, pos, 1, 1);
        return pos;
    };
    switch (type) {
    case TE_WIZSPIKE:   PosSnd(cl_sfx_wizhit, 20, 30); break;
    case TE_KNIGHTSPIKE:PosSnd(cl_sfx_knighthit, 226, 20); break;
    case TE_SPIKE: case TE_SUPERSPIKE: {
        Vector3 pos = PosSnd(nullptr, 0, (type == TE_SPIKE) ? 10 : 20);
        sfx_t* s = (rand() % 5) ? cl_sfx_tink1 : (std::array{ cl_sfx_ric3, cl_sfx_ric1, cl_sfx_ric2, cl_sfx_ric3 }[rand() & 3]);
        S_StartSound(-1, 0, s, pos, 1, 1);
        break;
    }
    case TE_GUNSHOT: PosSnd(nullptr, 0, 20); break;
    case TE_EXPLOSION: case TE_EXPLOSION2: {
        Vector3 pos = PosSnd(cl_sfx_r_exp3);
        if (type == TE_EXPLOSION) R_ParticleExplosion(pos);
        else {
            int cstart = MSG_ReadByte(), clen = MSG_ReadByte();
            R_ParticleExplosion2(pos, cstart, clen);
        }
        if (auto* dl = CL_AllocDlight(0)) {
            dl->origin = pos;
            dl->radius = 350.0f;
            dl->die = static_cast<float>(cl.time + 0.5);
            dl->decay = 300.0f;
        }
        break;
    }
    case TE_TAREXPLOSION: R_BlobExplosion(PosSnd(cl_sfx_r_exp3)); break;
    case TE_LIGHTNING1: CL_ParseBeam(Mod_ForName("progs/bolt.mdl", true)); break;
    case TE_LIGHTNING2: CL_ParseBeam(Mod_ForName("progs/bolt2.mdl", true)); break;
    case TE_LIGHTNING3: CL_ParseBeam(Mod_ForName("progs/bolt3.mdl", true)); break;
    case TE_BEAM:       CL_ParseBeam(Mod_ForName("progs/beam.mdl", true)); break;
    case TE_LAVASPLASH: R_LavaSplash(PosSnd(nullptr)); break;
    case TE_TELEPORT:   R_TeleportSplash(PosSnd(nullptr)); break;
    default: Sys_Error("CL_ParseTEnt: bad type");
    }
}

entity_t* CL_NewTempEntity() {
    if (cl_numvisedicts == MAX_VISEDICTS || num_temp_entities == MAX_TEMP_ENTITIES) return nullptr;
    entity_t* ent = &cl_temp_entities[num_temp_entities++];
    *ent = {};
    cl_visedicts[cl_numvisedicts++] = ent;
    ent->colormap = vid.colormap;
    return ent;
}

void CL_UpdateTEnts() {
    num_temp_entities = 0;
    for (auto& b : cl_beams) {
        if (!b.model || b.endtime < cl.time) continue;
        if (b.entity == cl.viewentity) b.start = cl_entities[cl.viewentity].origin;
        Vector3 dist = b.end - b.start;
        float yaw = 0.0f, pitch = 0.0f;
        if (dist.y == 0.0f && dist.x == 0.0f) {
            pitch = (dist.z > 0.0f) ? 90.0f : 270.0f;
        } else {
            yaw = static_cast<float>(std::atan2(dist.y, dist.x) * 180.0 / std::numbers::pi);
            if (yaw < 0.0f) yaw += 360.0f;
            pitch = static_cast<float>(std::atan2(dist.z, std::sqrt(dist.x * dist.x + dist.y * dist.y)) * 180.0 / std::numbers::pi);
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
    }
}

} // namespace Client
