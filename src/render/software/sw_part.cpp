// sw_part.cpp -- Particle System Implementation
#include "render/software/sw_part.hpp"
#include "client/client_types.hpp"
#include "server/server_types.hpp"
#include "core/msg.hpp"
#include "core/cvar.hpp"
#include "core/cmd.hpp"
#include "core/filesystem.hpp"
#include "ui/console.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace Render {

inline constexpr int MAX_PARTICLES = 2048;
inline constexpr int ABSOLUTE_MIN_PARTICLES = 512;

constexpr std::array<int, 8> ramp1 = { 0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61 };
constexpr std::array<int, 8> ramp2 = { 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66 };
constexpr std::array<int, 8> ramp3 = { 0x6d, 0x6b, 6, 5, 4, 3, 0, 0 };

static particle_t* active_particles = nullptr;
static particle_t* free_particles = nullptr;
static std::vector<particle_t> particles;
static int r_numparticles = 0;

std::array<std::array<float, 3>, NUMVERTEXNORMALS> r_avertexnormals{};
static std::array<Vector3, NUMVERTEXNORMALS> avelocities{};
static float beamlength = 16.0f;

Vector3 r_pright, r_pup, r_ppn;

static inline particle_t* AllocParticle()
{
    if (!free_particles) return nullptr;
    particle_t* p = free_particles;
    free_particles = p->next;
    p->next = active_particles;
    active_particles = p;
    return p;
}

void R_InitParticles()
{
    int i = Common::COM_CheckParm("-particles");
    if (i) {
        r_numparticles = Common::Q_atoi(Common::com_argv[i + 1]);
        if (r_numparticles < ABSOLUTE_MIN_PARTICLES) {
            r_numparticles = ABSOLUTE_MIN_PARTICLES;
        }
    } else {
        r_numparticles = MAX_PARTICLES;
    }
    particles.resize(r_numparticles);
}

void R_EntityParticles(entity_t* ent)
{
    constexpr float dist = 64.0f;
    if (!avelocities[0].x) {
        for (int j = 0; j < NUMVERTEXNORMALS; j++) {
            for (int i = 0; i < 3; i++) {
                avelocities[j][i] = (rand() & 255) * 0.01f;
            }
        }
    }
    for (int i = 0; i < NUMVERTEXNORMALS; i++) {
        const float yaw = static_cast<float>(Client::cl.time * avelocities[i].x);
        const float pitch = static_cast<float>(Client::cl.time * avelocities[i].y);
        const Vector3 forward(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch));
        particle_t* p = AllocParticle();
        if (!p) return;
        p->die = static_cast<float>(Client::cl.time + 0.01);
        p->color = static_cast<float>(0x6f);
        p->type = ptype_t::Explode;
        p->org = ent->origin + Vector3(r_avertexnormals[i][0], r_avertexnormals[i][1], r_avertexnormals[i][2]) * dist + forward * beamlength;
    }
}

void R_ClearParticles()
{
    free_particles = particles.data();
    active_particles = nullptr;
    for (int i = 0; i < r_numparticles - 1; i++) {
        particles[i].next = &particles[i + 1];
    }
    particles[r_numparticles - 1].next = nullptr;
}

void R_ReadPointFile_f()
{
    FILE* f;
    Vector3 org;
    char name[MAX_OSPATH];
    sprintf_s(name, sizeof(name), "maps/%s.pts", Server::sv.name);
    Common::COM_FOpenFile(name, &f);
    if (!f) {
        Console::Con_Printf("couldn't open %s\n", name);
        return;
    }
    Console::Con_Printf("Reading %s...\n", name);
    int c = 0;
    for (;;) {
        int r = fscanf_s(f, "%f %f %f\n", &org[0], &org[1], &org[2]);
        if (r != 3) {
            break;
        }
        c++;
        if (!free_particles) {
            Console::Con_Printf("Not enough free particles\n");
            break;
        }
        particle_t* p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;
        p->die = 99999.0f;
        p->color = static_cast<float>((-c) & 15);
        p->type = ptype_t::Static;
        p->vel = Math::vec3_origin;
        p->org = org;
    }
    fclose(f);
    Console::Con_Printf("%i points read\n", c);
}

void R_ParseParticleEffect()
{
    Vector3 org, dir;
    org.x = Common::MSG_ReadCoord();
    org.y = Common::MSG_ReadCoord();
    org.z = Common::MSG_ReadCoord();
    dir.x = Common::MSG_ReadChar() * (1.0f / 16.0f);
    dir.y = Common::MSG_ReadChar() * (1.0f / 16.0f);
    dir.z = Common::MSG_ReadChar() * (1.0f / 16.0f);
    int msgcount = Common::MSG_ReadByte();
    int color = Common::MSG_ReadByte();
    int count = (msgcount == 255) ? 1024 : msgcount;
    R_RunParticleEffect(org, dir, color, count);
}

void R_ParticleExplosion(const Vector3& org)
{
    for (int i = 0; i < 1024; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;
        p->die = static_cast<float>(Client::cl.time + 5);
        p->color = static_cast<float>(ramp1[0]);
        p->ramp = static_cast<float>(rand() & 3);
        p->type = (i & 1) ? ptype_t::Explode : ptype_t::Explode2;
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

void R_ParticleExplosion2(const Vector3& org, int colorStart, int colorLength)
{
    int colorMod = 0;
    for (int i = 0; i < 512; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;
        p->die = static_cast<float>(Client::cl.time + 0.3);
        p->color = static_cast<float>(colorStart + (colorMod++ % colorLength));
        p->type = ptype_t::Blob;
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

void R_BlobExplosion(const Vector3& org)
{
    for (int i = 0; i < 1024; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;
        p->die = static_cast<float>(Client::cl.time + 1 + (rand() & 8) * 0.05);
        if (i & 1) {
            p->type = ptype_t::Blob;
            p->color = static_cast<float>(66 + rand() % 6);
        } else {
            p->type = ptype_t::Blob2;
            p->color = static_cast<float>(150 + rand() % 6);
        }
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

void R_RunParticleEffect(const Vector3& org, const Vector3& dir, int color, int count)
{
    for (int i = 0; i < count; i++) {
        particle_t* p = AllocParticle();
        if (!p) return;
        if (count == 1024) {
            p->die = static_cast<float>(Client::cl.time + 5);
            p->color = static_cast<float>(ramp1[0]);
            p->ramp = static_cast<float>(rand() & 3);
            p->type = (i & 1) ? ptype_t::Explode : ptype_t::Explode2;
            p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
            p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
        } else {
            p->die = static_cast<float>(Client::cl.time + 0.1 * (rand() % 5));
            p->color = static_cast<float>((color & ~7) + (rand() & 7));
            p->type = ptype_t::SlowGrav;
            p->org = org + Vector3(static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8));
            p->vel = dir * 15;
        }
    }
}

void R_LavaSplash(const Vector3& org)
{
    for (int i = -16; i < 16; i++) {
        for (int j = -16; j < 16; j++) {
            particle_t* p = AllocParticle();
            if (!p) return;
            p->die = static_cast<float>(Client::cl.time + 2 + (rand() & 31) * 0.02);
            p->color = static_cast<float>(224 + (rand() & 7));
            p->type = ptype_t::SlowGrav;
            Vector3 dir(static_cast<float>(j * 8 + (rand() & 7)), static_cast<float>(i * 8 + (rand() & 7)), 256.0f);
            p->org = org + Vector3(dir.x, dir.y, static_cast<float>(rand() & 63));
            dir.normalize();
            p->vel = dir * static_cast<float>(50 + (rand() & 63));
        }
    }
}

void R_TeleportSplash(const Vector3& org)
{
    for (int i = -16; i < 16; i += 4) {
        for (int j = -16; j < 16; j += 4) {
            for (int k = -24; k < 32; k += 4) {
                particle_t* p = AllocParticle();
                if (!p) return;
                p->die = static_cast<float>(Client::cl.time + 0.2 + (rand() & 7) * 0.02);
                p->color = static_cast<float>(7 + (rand() & 7));
                p->type = ptype_t::SlowGrav;
                Vector3 dir(static_cast<float>(j * 8), static_cast<float>(i * 8), static_cast<float>(k * 8));
                p->org = org + Vector3(static_cast<float>(i + (rand() & 3)), static_cast<float>(j + (rand() & 3)), static_cast<float>(k + (rand() & 3)));
                dir.normalize();
                p->vel = dir * static_cast<float>(50 + (rand() & 63));
            }
        }
    }
}

void R_RocketTrail(Vector3 start, const Vector3& end, int type)
{
    Vector3 vec = end - start;
    float len = vec.normalize();
    int dec;
    static int tracercount = 0;
    if (type < 128) {
        dec = 3;
    } else {
        dec = 1;
        type -= 128;
    }
    while (len > 0) {
        len -= dec;
        particle_t* p = AllocParticle();
        if (!p) return;
        p->vel = Math::vec3_origin;
        p->die = static_cast<float>(Client::cl.time + 2);
        switch (type) {
        case 0: // rocket trail
            p->ramp = static_cast<float>(rand() & 3);
            p->color = static_cast<float>(ramp3[(int)p->ramp]);
            p->type = ptype_t::Fire;
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;
        case 1: // smoke smoke
            p->ramp = static_cast<float>((rand() & 3) + 2);
            p->color = static_cast<float>(ramp3[(int)p->ramp]);
            p->type = ptype_t::Fire;
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;
        case 2: // blood
            p->type = ptype_t::Grav;
            p->color = static_cast<float>(67 + (rand() & 3));
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;
        case 3:
        case 5: // tracer
            p->die = static_cast<float>(Client::cl.time + 0.5);
            p->type = ptype_t::Static;
            if (type == 3) {
                p->color = static_cast<float>(52 + ((tracercount & 4) << 1));
            } else {
                p->color = static_cast<float>(230 + ((tracercount & 4) << 1));
            }
            tracercount++;
            p->org = start;
            if (tracercount & 1) {
                p->vel.x = 30 * vec.y;
                p->vel.y = 30 * -vec.x;
                p->vel.z = 0;
            } else {
                p->vel.x = 30 * -vec.y;
                p->vel.y = 30 * vec.x;
                p->vel.z = 0;
            }
            break;
        case 4: // slight blood
            p->type = ptype_t::Grav;
            p->color = static_cast<float>(67 + (rand() & 3));
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            len -= 3;
            break;
        case 6: // voor trail
            p->color = static_cast<float>(9 * 16 + 8 + (rand() & 3));
            p->type = ptype_t::Static;
            p->die = static_cast<float>(Client::cl.time + 0.3);
            p->org = start + Vector3(static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8));
            break;
        }
        start += vec;
    }
}

void R_DrawParticles()
{
    D_StartParticles();
    Math::VectorScale(vright, xscaleshrink, r_pright);
    Math::VectorScale(vup, yscaleshrink, r_pup);
    VectorCopy(vpn, r_ppn);
    const float frametime = static_cast<float>(Client::cl.time - Client::cl.oldtime);
    const float time3 = frametime * 15;
    const float time2 = frametime * 10;
    const float time1 = frametime * 5;
    const float grav = static_cast<float>(frametime * Server::sv_gravity.value * 0.05);
    const float dvel = 4 * frametime;

    for (;;) {
        particle_t* kill = active_particles;
        if (kill && kill->die < Client::cl.time) {
            active_particles = kill->next;
            kill->next = free_particles;
            free_particles = kill;
            continue;
        }
        break;
    }

    for (particle_t* p = active_particles; p; p = p->next) {
        for (;;) {
            particle_t* kill = p->next;
            if (kill && kill->die < Client::cl.time) {
                p->next = kill->next;
                kill->next = free_particles;
                free_particles = kill;
                continue;
            }
            break;
        }
        D_DrawParticle(p);
        p->org[0] += p->vel[0] * frametime;
        p->org[1] += p->vel[1] * frametime;
        p->org[2] += p->vel[2] * frametime;
        switch (p->type) {
        case ptype_t::Static:
            break;
        case ptype_t::Fire:
            p->ramp += time1;
            if (p->ramp >= 6) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp3[(int)p->ramp]);
            }
            p->vel[2] += grav;
            break;
        case ptype_t::Explode:
            p->ramp += time2;
            if (p->ramp >= 8) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp1[(int)p->ramp]);
            }
            for (int i = 0; i < 3; i++) {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;
        case ptype_t::Explode2:
            p->ramp += time3;
            if (p->ramp >= 8) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp2[(int)p->ramp]);
            }
            for (int i = 0; i < 3; i++) {
                p->vel[i] -= p->vel[i] * frametime;
            }
            p->vel[2] -= grav;
            break;
        case ptype_t::Blob:
            for (int i = 0; i < 3; i++) {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;
        case ptype_t::Blob2:
            for (int i = 0; i < 2; i++) {
                p->vel[i] -= p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;
        case ptype_t::Grav:
        case ptype_t::SlowGrav:
            p->vel[2] -= grav;
            break;
        }
    }
    D_EndParticles();
}

} // namespace Render
