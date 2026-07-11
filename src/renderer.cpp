// renderer.cpp -- merged renderer subsystem

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
#include "d_local.hpp"

namespace Render {

namespace {
    int r_bmodelactive;
    mnode_t* r_pefragtopnode;
    Vector3 r_emins, r_emaxs;
    int r_dlightframecount;
    int c_faceclip;
    clipplane_t view_clipplanes[4];
    edge_t* auxedges;
    edge_t *r_edges, *edge_p, *edge_max;
    edge_t* newedges[MAXHEIGHT];
    edge_t* removeedges[MAXHEIGHT];
    int r_currentkey;
    edge_t edge_head;
    edge_t edge_tail;
    edge_t edge_aftertail;
    Vector3 r_entorigin;
    float entity_rotation[3][3];
    Vector3 r_worldmodelorg;
    int r_currentbkey;

    mdl_t* pmdl;
    aliashdr_t* paliashdr;
    finalvert_t* pfinalverts;
    auxvert_t* pauxverts;
    int r_amodels_drawn;
    int a_skinwidth;
    float r_time1;
    int r_numallocatededges;
    int r_outofsurfaces;
    int r_outofedges;
    qboolean r_dowarpold, r_viewchanged;
    int numbtofpolys;
    btofpoly_t* pbtofpolys;
    mvertex_t* r_pcurrentvertbase;
    int r_maxsurfsseen, r_maxedgesseen, r_cnumsurfs;
    qboolean r_surfsonstack;
    int r_clipflags;
    qboolean r_fov_greater_than_90;
    float aliasxscale, aliasyscale, aliasxcenter, aliasycenter;
    float screenAspect;
    float verticalFieldOfView;
    float xOrigin, yOrigin;
    mplane_t screenedge[4];
    int r_visframecount;
    int r_polycount;
    int r_wholepolycount;
    int* pfrustum_indexes[4];
    int r_frustum_indexes[4 * 6];
    mleaf_t *r_viewleaf, *r_oldviewleaf;
    float r_aliastransition, r_resfudge;
    float dp_time1, dp_time2, db_time1, db_time2, rw_time1, rw_time2;
    float se_time1, se_time2, de_time1, de_time2, dv_time1, dv_time2;
    cvar_t r_draworder = { "r_draworder", "0", {}, {}, {}, {} };
    cvar_t r_speeds = { "r_speeds", "0", {}, {}, {}, {} };
    cvar_t r_timegraph = { "r_timegraph", "0", {}, {}, {}, {} };
    cvar_t r_graphheight = { "r_graphheight", "10", {}, {}, {}, {} };
    cvar_t r_waterwarp = { "r_waterwarp", "1", {}, {}, {}, {} };
    cvar_t r_fullbright = { "r_fullbright", "0", {}, {}, {}, {} };
    cvar_t r_drawentities = { "r_drawentities", "1", {}, {}, {}, {} };
    cvar_t r_aliasstats = { "r_polymodelstats", "0", {}, {}, {}, {} };
    cvar_t r_dspeeds = { "r_dspeeds", "0", {}, {}, {}, {} };
    cvar_t r_ambient = { "r_ambient", "0", {}, {}, {}, {} };
    cvar_t r_reportsurfout = { "r_reportsurfout", "0", {}, {}, {}, {} };
    cvar_t r_maxsurfs = { "r_maxsurfs", "0", {}, {}, {}, {} };
    cvar_t r_numsurfs = { "r_numsurfs", "0", {}, {}, {}, {} };
    cvar_t r_reportedgeout = { "r_reportedgeout", "0", {}, {}, {}, {} };
    cvar_t r_maxedges = { "r_maxedges", "0", {}, {}, {}, {} };
    cvar_t r_numedges = { "r_numedges", "0", {}, {}, {}, {} };
}

// ============================================================
// Content from: src\r_vars.cpp
// ============================================================
// r_vars.cpp: global refresh variables


// all global and static refresh variables are collected in a contiguous block
// to avoid cache conflicts.

//-------------------------------------------------------
// global refresh variables
//-------------------------------------------------------


// ============================================================
// Content from: src\r_efrag.cpp
// ============================================================


//===========================================================================

/*
===============================================================================

					ENTITY FRAGMENT FUNCTIONS

===============================================================================
*/

efrag_t** lastlink;

entity_t* r_addent;

/*
================
R_RemoveEfrags

Call when removing an object from the world or moving it to another position
================
*/
void R_RemoveEfrags(entity_t* ent)
{
    efrag_t *ef, *old, *walk, **prev;

    ef = ent->efrag;

    while (ef) {
        prev = &ef->leaf->efrags;
        while (1) {
            walk = *prev;
            if (!walk) {
                break;
            }

            if (walk == ef) { // remove this fragment
                *prev = ef->leafnext;
                break;
            } else {
                prev = &walk->leafnext;
            }
        }

        old = ef;
        ef = ef->entnext;

        // put it on the free list
        old->entnext = cl.free_efrags;
        cl.free_efrags = old;
    }

    ent->efrag = NULL;
}

/*
===================
R_SplitEntityOnNode
===================
*/
void R_SplitEntityOnNode(mnode_t* node)
{
    efrag_t* ef;
    mplane_t* splitplane;
    mleaf_t* leaf;
    int sides;

    if (node->contents == CONTENTS_SOLID) {
        return;
    }

    // add an efrag if the node is a leaf

    if (node->contents < 0) {
        if (!r_pefragtopnode) {
            r_pefragtopnode = node;
        }

        leaf = (mleaf_t*)node;

        // grab an efrag off the free list
        ef = cl.free_efrags;
        if (!ef) {
            Con_Printf("Too many efrags!\n");

            return; // no free fragments...
        }

        cl.free_efrags = cl.free_efrags->entnext;

        ef->entity = r_addent;

        // add the entity link
        *lastlink = ef;
        lastlink = &ef->entnext;
        ef->entnext = NULL;

        // set the leaf links
        ef->leaf = leaf;
        ef->leafnext = leaf->efrags;
        leaf->efrags = ef;

        return;
    }

    // NODE_MIXED

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);

    if (sides == 3) {
        // split on this plane
        // if this is the first splitter of this bmodel, remember it
        if (!r_pefragtopnode) {
            r_pefragtopnode = node;
        }
    }

    // recurse down the contacted sides
    if (sides & 1) {
        R_SplitEntityOnNode(node->children[0]);
    }

    if (sides & 2) {
        R_SplitEntityOnNode(node->children[1]);
    }
}

/*
===================
R_SplitEntityOnNode2
===================
*/
void R_SplitEntityOnNode2(mnode_t* node)
{
    mplane_t* splitplane;
    int sides;

    if (node->visframe != r_visframecount) {
        return;
    }

    if (node->contents < 0) {
        if (node->contents != CONTENTS_SOLID) {
            r_pefragtopnode = node; // we've reached a non-solid leaf, so it's
        }

        //  visible and not BSP clipped
        return;
    }

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);

    if (sides == 3) {
        // remember first splitter
        r_pefragtopnode = node;

        return;
    }

    // not split yet; recurse down the contacted side
    if (sides & 1) {
        R_SplitEntityOnNode2(node->children[0]);
    } else {
        R_SplitEntityOnNode2(node->children[1]);
    }
}

/*
===========
R_AddEfrags
===========
*/
void R_AddEfrags(entity_t* ent)
{
    model_t* entmodel;
    int i;

    if (!ent->model) {
        return;
    }

    if (ent == cl_entities) {
        return; // never add the world
    }

    r_addent = ent;

    lastlink = &ent->efrag;
    r_pefragtopnode = NULL;

    entmodel = ent->model;

    for (i = 0; i < 3; i++) {
        r_emins[i] = ent->origin[i] + entmodel->mins[i];
        r_emaxs[i] = ent->origin[i] + entmodel->maxs[i];
    }

    R_SplitEntityOnNode(cl.worldmodel->nodes);

    ent->topnode = r_pefragtopnode;
}

/*
================
R_StoreEfrags

================
*/
void R_StoreEfrags(efrag_t** ppefrag)
{
    entity_t* pent;
    model_t* clmodel;
    efrag_t* pefrag;

    while ((pefrag = *ppefrag) != NULL) {
        pent = pefrag->entity;
        clmodel = pent->model;

        switch (clmodel->type) {
        case mod_alias:
        case mod_brush:
        case mod_sprite:
            pent = pefrag->entity;

            if ((pent->visframe != r_framecount) && (cl_numvisedicts < MAX_VISEDICTS)) {
                cl_visedicts[cl_numvisedicts++] = pent;

                // mark that we've recorded this entity for this frame
                pent->visframe = r_framecount;
            }

            ppefrag = &pefrag->leafnext;
            break;

        default:
            Sys_Error("R_StoreEfrags: Bad entity type %d\n", clmodel->type);
        }
    }
}


// ============================================================
// Content from: src\r_light.cpp
// ============================================================


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight(void)
{
    int i, j, k;

    //
    // light animations
    // 'm' is normal light, 'a' is no light, 'z' is double bright
    i = static_cast<int>(cl.time * 10);
    for (j = 0; j < MAX_LIGHTSTYLES; j++) {
        if (!cl_lightstyle[j].length) {
            d_lightstylevalue[j] = 256;
            continue;
        }

        k = i % cl_lightstyle[j].length;
        k = cl_lightstyle[j].map[k] - 'a';
        k = k * 22;
        d_lightstylevalue[j] = k;
    }
}

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights(dlight_t* light, int bit, mnode_t* node)
{
    mplane_t* splitplane;
    float dist;
    msurface_t* surf;
    int i;

    if (node->contents < 0) {
        return;
    }

    splitplane = node->plane;
    dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

    if (dist > light->radius) {
        R_MarkLights(light, bit, node->children[0]);

        return;
    }

    if (dist < -light->radius) {
        R_MarkLights(light, bit, node->children[1]);

        return;
    }

    // mark the polygons
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->dlightframe != r_dlightframecount) {
            surf->dlightbits = 0;
            surf->dlightframe = r_dlightframecount;
        }

        surf->dlightbits |= bit;
    }

    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights(void)
{
    int i;
    dlight_t* l;

    r_dlightframecount = r_framecount + 1; // because the count hasn't
    //  advanced yet for this frame
    l = cl_dlights;

    for (i = 0; i < MAX_DLIGHTS; i++, l++) {
        if (l->die < cl.time || !l->radius) {
            continue;
        }

        R_MarkLights(l, 1 << i, cl.worldmodel->nodes);
    }
}

/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

int RecursiveLightPoint(mnode_t* node, const Vector3& start, const Vector3& end)
{
    int r;
    float front, back, frac;
    int side;
    mplane_t* plane;
    Vector3 mid;
    msurface_t* surf;
    int s, t, ds, dt;
    int i;
    mtexinfo_t* tex;
    byte* lightmap;
    unsigned scale;
    int maps;

    if (node->contents < 0) {
        return -1; // didn't hit anything
    }

    // calculate mid point

    // FIXME: optimize for axial
    plane = node->plane;
    front = start.dot(plane->normal) - plane->dist;
    back = end.dot(plane->normal) - plane->dist;
    side = front < 0;

    if ((back < 0) == side) {
        return RecursiveLightPoint(node->children[side], start, end);
    }

    frac = front / (front - back);
    mid = start + (end - start) * frac;

    // go down front side
    r = RecursiveLightPoint(node->children[side], start, mid);
    if (r >= 0) {
        return r; // hit something
    }

    if ((back < 0) == side) {
        return -1; // didn't hit anuthing
    }

    // check for impact on this node

    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->flags & SURF_DRAWTILED) {
            continue; // no lightmaps
        }

        tex = surf->texinfo;

        s = static_cast<int>(mid.dot(tex->vecs[0]) + tex->vecs[0][3]);
        t = static_cast<int>(mid.dot(tex->vecs[1]) + tex->vecs[1][3]);
        ;

        if (s < surf->texturemins[0] || t < surf->texturemins[1]) {
            continue;
        }

        ds = s - surf->texturemins[0];
        dt = t - surf->texturemins[1];

        if (ds > surf->extents[0] || dt > surf->extents[1]) {
            continue;
        }

        if (!surf->samples) {
            return 0;
        }

        ds >>= 4;
        dt >>= 4;

        lightmap = surf->samples;
        r = 0;
        if (lightmap) {
            lightmap += dt * ((surf->extents[0] >> 4) + 1) + ds;

            for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
                scale = d_lightstylevalue[surf->styles[maps]];
                r += *lightmap * scale;
                lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1);
            }

            r >>= 8;
        }

        return r;
    }

    // go down back side
    return RecursiveLightPoint(node->children[!side], mid, end);
}

int R_LightPoint(const Vector3& p)
{
    Vector3 end;
    int r;

    if (!cl.worldmodel->lightdata) {
        return 255;
    }

    end = p - Vector3(0.0f, 0.0f, 2048.0f);

    r = RecursiveLightPoint(cl.worldmodel->nodes, p, end);

    if (r == -1) {
        r = 0;
    }

    if (r < r_refdef.ambientlight) {
        r = r_refdef.ambientlight;
    }

    return r;
}


// ============================================================
// Content from: src\r_part.cpp
// ============================================================


#define MAX_PARTICLES 2048 // default max # of particles at one
//  time
#define ABSOLUTE_MIN_PARTICLES 512 // no fewer than this no matter what's
//  on the command line

int ramp1[8] = { 0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61 };
int ramp2[8] = { 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66 };
int ramp3[8] = { 0x6d, 0x6b, 6, 5, 4, 3 };

particle_t *active_particles, *free_particles;

particle_t* particles;
int r_numparticles;

Vector3 r_pright, r_pup, r_ppn;

/*
===============
R_InitParticles
===============
*/
void R_InitParticles(void)
{
    int i;

    i = COM_CheckParm("-particles");

    if (i) {
        r_numparticles = (int)(Q_atoi(com_argv[i + 1]));
        if (r_numparticles < ABSOLUTE_MIN_PARTICLES) {
            r_numparticles = ABSOLUTE_MIN_PARTICLES;
        }
    } else {
        r_numparticles = MAX_PARTICLES;
    }

    particles = (particle_t*)Hunk_Alloc(r_numparticles * sizeof(particle_t),
        "particles");
}


/*
===============
R_EntityParticles
===============
*/

#define NUMVERTEXNORMALS 162
float r_avertexnormals[NUMVERTEXNORMALS][3];
Vector3 avelocities[NUMVERTEXNORMALS];
float beamlength = 16;

void R_EntityParticles(entity_t* ent)
{
    int i;
    particle_t* p;
    float angle;
    float sp, sy, cp, cy;
    Vector3 forward;
    float dist;

    dist = 64;

    if (!avelocities[0].x) {
        for (int j = 0; j < NUMVERTEXNORMALS; j++) {
            for (i = 0; i < 3; i++) {
                avelocities[j][i] = (rand() & 255) * 0.01f;
            }
        }
    }

    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        angle = static_cast<float>(cl.time * avelocities[i].x);
        sy = sin(angle);
        cy = cos(angle);
        angle = static_cast<float>(cl.time * avelocities[i].y);
        sp = sin(angle);
        cp = cos(angle);

        forward.x = cp * cy;
        forward.y = cp * sy;
        forward.z = -sp;

        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = static_cast<float>(cl.time + 0.01);
        p->color = static_cast<float>(0x6f);
        p->type = pt_explode;

        p->org = ent->origin + Vector3(r_avertexnormals[i][0], r_avertexnormals[i][1], r_avertexnormals[i][2]) * dist + forward * beamlength;
    }
}

/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles(void)
{
    int i;

    free_particles = &particles[0];
    active_particles = NULL;

    for (i = 0; i < r_numparticles; i++) {
        particles[i].next = &particles[i + 1];
    }
    particles[r_numparticles - 1].next = NULL;
}

void R_ReadPointFile_f(void)
{
    FILE* f;
    Vector3 org;
    int r;
    int c;
    particle_t* p;
    char name[MAX_OSPATH];

    sprintf_s(name, sizeof(name), "maps/%s.pts", sv.name);

    COM_FOpenFile(name, &f);
    if (!f) {
        Con_Printf("couldn't open %s\n", name);

        return;
    }

    Con_Printf("Reading %s...\n", name);
    c = 0;
    for (;;) {
        r = fscanf_s(f, "%f %f %f\n", &org[0], &org[1], &org[2]);
        if (r != 3) {
            break;
        }

        c++;

        if (!free_particles) {
            Con_Printf("Not enough free particles\n");
            break;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = 99999.0f;
        p->color = static_cast<float>((-c) & 15);
        p->type = pt_static;
        p->vel = vec3_origin;
        p->org = org;
    }

    fclose(f);
    Con_Printf("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect(void)
{
    Vector3 org, dir;
    int count, msgcount, color;

    org.x = MSG_ReadCoord();
    org.y = MSG_ReadCoord();
    org.z = MSG_ReadCoord();

    dir.x = MSG_ReadChar() * (1.0f / 16.0f);
    dir.y = MSG_ReadChar() * (1.0f / 16.0f);
    dir.z = MSG_ReadChar() * (1.0f / 16.0f);

    msgcount = MSG_ReadByte();
    color = MSG_ReadByte();

    if (msgcount == 255) {
        count = 1024;
    } else {
        count = msgcount;
    }

    R_RunParticleEffect(org, dir, color, count);
}

/*
===============
R_ParticleExplosion

===============
*/
void R_ParticleExplosion(const Vector3& org)
{
    int i;
    particle_t* p;

    for (i = 0; i < 1024; i++) {
        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = static_cast<float>(cl.time + 5);
        p->color = static_cast<float>(ramp1[0]);
        p->ramp = static_cast<float>(rand() & 3);
        if (i & 1) {
            p->type = pt_explode;
        } else {
            p->type = pt_explode2;
        }
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

/*
===============
R_ParticleExplosion2

===============
*/
void R_ParticleExplosion2(const Vector3& org, int colorStart, int colorLength)
{
    int i;
    particle_t* p;
    int colorMod = 0;

    for (i = 0; i < 512; i++) {
        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = static_cast<float>(cl.time + 0.3);
        p->color = static_cast<float>(colorStart + (colorMod % colorLength));
        colorMod++;

        p->type = pt_blob;
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

/*
===============
R_BlobExplosion

===============
*/
void R_BlobExplosion(const Vector3& org)
{
    int i;
    particle_t* p;

    for (i = 0; i < 1024; i++) {
        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = static_cast<float>(cl.time + 1 + (rand() & 8) * 0.05);

        if (i & 1) {
            p->type = pt_blob;
            p->color = static_cast<float>(66 + rand() % 6);
        } else {
            p->type = pt_blob2;
            p->color = static_cast<float>(150 + rand() % 6);
        }
        p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
        p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
    }
}

/*
===============
R_RunParticleEffect

===============
*/
void R_RunParticleEffect(const Vector3& org, const Vector3& dir, int color, int count)
{
    int i;
    particle_t* p;

    for (i = 0; i < count; i++) {
        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        if (count == 1024) { // rocket explosion
            p->die = static_cast<float>(cl.time + 5);
            p->color = static_cast<float>(ramp1[0]);
            p->ramp = static_cast<float>(rand() & 3);
            if (i & 1) {
                p->type = pt_explode;
            } else {
                p->type = pt_explode2;
            }
            p->org = org + Vector3(static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16), static_cast<float>((rand() % 32) - 16));
            p->vel = Vector3(static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256), static_cast<float>((rand() % 512) - 256));
        } else {
            p->die = static_cast<float>(cl.time + 0.1 * (rand() % 5));
            p->color = static_cast<float>((color & ~7) + (rand() & 7));
            p->type = pt_slowgrav;
            p->org = org + Vector3(static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8));
            p->vel = dir * 15; // + (rand()%300)-150;
        }
    }
}

/*
===============
R_LavaSplash

===============
*/
void R_LavaSplash(const Vector3& org)
{
    int i, j, k;
    particle_t* p;
    float vel;
    Vector3 dir;

    for (i = -16; i < 16; i++) {
        for (j = -16; j < 16; j++) {
            for (k = 0; k < 1; k++) {
                if (!free_particles) {
                    return;
                }

                p = free_particles;
                free_particles = p->next;
                p->next = active_particles;
                active_particles = p;

                p->die = static_cast<float>(cl.time + 2 + (rand() & 31) * 0.02);
                p->color = static_cast<float>(224 + (rand() & 7));
                p->type = pt_slowgrav;

                dir.x = static_cast<float>(j * 8 + (rand() & 7));
                dir.y = static_cast<float>(i * 8 + (rand() & 7));
                dir.z = 256;

                p->org = org + Vector3(dir.x, dir.y, static_cast<float>(rand() & 63));

                dir.normalize();
                vel = static_cast<float>(50 + (rand() & 63));
                p->vel = dir * vel;
            }
        }
    }
}

/*
===============
R_TeleportSplash

===============
*/
void R_TeleportSplash(const Vector3& org)
{
    int i, j, k;
    particle_t* p;
    float vel;
    Vector3 dir;

    for (i = -16; i < 16; i += 4) {
        for (j = -16; j < 16; j += 4) {
            for (k = -24; k < 32; k += 4) {
                if (!free_particles) {
                    return;
                }

                p = free_particles;
                free_particles = p->next;
                p->next = active_particles;
                active_particles = p;

                p->die = static_cast<float>(cl.time + 0.2 + (rand() & 7) * 0.02);
                p->color = static_cast<float>(7 + (rand() & 7));
                p->type = pt_slowgrav;

                dir = Vector3(static_cast<float>(j * 8), static_cast<float>(i * 8), static_cast<float>(k * 8));

                p->org = org + Vector3(static_cast<float>(i + (rand() & 3)), static_cast<float>(j + (rand() & 3)), static_cast<float>(k + (rand() & 3)));

                dir.normalize();
                vel = static_cast<float>(50 + (rand() & 63));
                p->vel = dir * vel;
            }
        }
    }
}

void R_RocketTrail(Vector3 start, const Vector3& end, int type)
{
    Vector3 vec;
    float len;
    particle_t* p;
    int dec;
    static int tracercount;

    vec = end - start;
    len = vec.normalize();
    if (type < 128) {
        dec = 3;
    } else {
        dec = 1;
        type -= 128;
    }

    while (len > 0) {
        len -= dec;

        if (!free_particles) {
            return;
        }

        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->vel = vec3_origin;
        p->die = static_cast<float>(cl.time + 2);

        switch (type) {
        case 0: // rocket trail
            p->ramp = static_cast<float>(rand() & 3);
            p->color = static_cast<float>(ramp3[(int)p->ramp]);
            p->type = pt_fire;
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;

        case 1: // smoke smoke
            p->ramp = static_cast<float>((rand() & 3) + 2);
            p->color = static_cast<float>(ramp3[(int)p->ramp]);
            p->type = pt_fire;
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;

        case 2: // blood
            p->type = pt_grav;
            p->color = static_cast<float>(67 + (rand() & 3));
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            break;

        case 3:
        case 5: // tracer
            p->die = static_cast<float>(cl.time + 0.5);
            p->type = pt_static;
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
            p->type = pt_grav;
            p->color = static_cast<float>(67 + (rand() & 3));
            p->org = start + Vector3(static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3), static_cast<float>((rand() % 6) - 3));
            len -= 3;
            break;

        case 6: // voor trail
            p->color = static_cast<float>(9 * 16 + 8 + (rand() & 3));
            p->type = pt_static;
            p->die = static_cast<float>(cl.time + 0.3);
            p->org = start + Vector3(static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8), static_cast<float>((rand() & 15) - 8));
            break;
        }

        start += vec;
    }
}

/*
===============
R_DrawParticles
===============
*/
void R_DrawParticles(void)
{
    particle_t *p, *kill;
    float grav;
    int i;
    float time2, time3;
    float time1;
    float dvel;
    float frametime;

    D_StartParticles();

    VectorScale(vright, xscaleshrink, r_pright);
    VectorScale(vup, yscaleshrink, r_pup);
    VectorCopy(vpn, r_ppn);
    frametime = static_cast<float>(cl.time - cl.oldtime);
    time3 = frametime * 15;
    time2 = frametime * 10; // 15;
    time1 = frametime * 5;
    grav = static_cast<float>(frametime * sv_gravity.value * 0.05);
    dvel = 4 * frametime;

    for (;;) {
        kill = active_particles;
        if (kill && kill->die < cl.time) {
            active_particles = kill->next;
            kill->next = free_particles;
            free_particles = kill;
            continue;
        }

        break;
    }

    for (p = active_particles; p; p = p->next) {
        for (;;) {
            kill = p->next;
            if (kill && kill->die < cl.time) {
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
        case pt_static:
            break;
        case pt_fire:
            p->ramp += time1;
            if (p->ramp >= 6) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp3[(int)p->ramp]);
            }

            p->vel[2] += grav;
            break;

        case pt_explode:
            p->ramp += time2;
            if (p->ramp >= 8) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp1[(int)p->ramp]);
            }

            for (i = 0; i < 3; i++) {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case pt_explode2:
            p->ramp += time3;
            if (p->ramp >= 8) {
                p->die = -1;
            } else {
                p->color = static_cast<float>(ramp2[(int)p->ramp]);
            }

            for (i = 0; i < 3; i++) {
                p->vel[i] -= p->vel[i] * frametime;
            }
            p->vel[2] -= grav;
            break;

        case pt_blob:
            for (i = 0; i < 3; i++) {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case pt_blob2:
            for (i = 0; i < 2; i++) {
                p->vel[i] -= p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case pt_grav:
        case pt_slowgrav:
            p->vel[2] -= grav;
            break;
        }
    }

    D_EndParticles();
}


// ============================================================
// Content from: src\r_sky.cpp
// ============================================================


int iskyspeed = 8;
int iskyspeed2 = 2;
float skyspeed, skyspeed2;

float skytime;

byte* r_skysource;

int r_skymade;
int r_skydirect; // not used?

// TODO: clean up these routines

byte bottomsky[128 * 131];
byte bottommask[128 * 131];
byte newsky[128 * 256]; // newsky and topsky both pack in here, 128 bytes

//  of newsky on the left of each scan, 128 bytes
//  of topsky on the right, because the low-level
//  drawers need 256-byte scan widths

/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky(texture_t* mt)
{
    int i, j;
    byte* src;

    src = (byte*)mt + mt->offsets[0];

    for (i = 0; i < 128; i++) {
        for (j = 0; j < 128; j++) {
            newsky[(i * 256) + j + 128] = src[i * 256 + j + 128];
        }
    }

    for (i = 0; i < 128; i++) {
        for (j = 0; j < 131; j++) {
            if (src[i * 256 + (j & 0x7F)]) {
                bottomsky[(i * 131) + j] = src[i * 256 + (j & 0x7F)];
                bottommask[(i * 131) + j] = 0;
            } else {
                bottomsky[(i * 131) + j] = 0;
                bottommask[(i * 131) + j] = 0xff;
            }
        }
    }

    r_skysource = newsky;
}

/*
=================
R_MakeSky
=================
*/
void R_MakeSky(void)
{
    int x, y;
    int ofs, baseofs;
    int xshift, yshift;
    unsigned* pnewsky;
    static int xlast = -1, ylast = -1;

    xshift = static_cast<int>(skytime * skyspeed);
    yshift = static_cast<int>(skytime * skyspeed);

    if ((xshift == xlast) && (yshift == ylast)) {
        return;
    }

    xlast = xshift;
    ylast = yshift;

    pnewsky = (unsigned*)&newsky[0];

    for (y = 0; y < SKYSIZE; y++) {
        baseofs = ((y + yshift) & SKYMASK) * 131;

#if UNALIGNED_OK

        for (x = 0; x < SKYSIZE; x += 4) {
            ofs = baseofs + ((x + xshift) & SKYMASK);

            // PORT: unaligned dword access to bottommask and bottomsky

            *pnewsky = (*(pnewsky + (128 / sizeof(unsigned))) & *(unsigned*)&bottommask[ofs]) | *(unsigned*)&bottomsky[ofs];
            pnewsky++;
        }

#else

        for (x = 0; x < SKYSIZE; x++) {
            ofs = baseofs + ((x + xshift) & SKYMASK);

            *(byte*)pnewsky = (*((byte*)pnewsky + 128) & *(byte*)&bottommask[ofs]) | *(byte*)&bottomsky[ofs];
            pnewsky = (unsigned*)((byte*)pnewsky + 1);
        }

#endif

        pnewsky += 128 / sizeof(unsigned);
    }

    r_skymade = 1;
}

/*
=============
R_SetSkyFrame
==============
*/
void R_SetSkyFrame(void)
{
    int g, s1, s2;
    float temp;

    skyspeed = static_cast<float>(iskyspeed);
    skyspeed2 = static_cast<float>(iskyspeed2);

    g = GreatestCommonDivisor(iskyspeed, iskyspeed2);
    s1 = iskyspeed / g;
    s2 = iskyspeed2 / g;
    temp = static_cast<float>(SKYSIZE * s1 * s2);

    skytime = static_cast<float>(cl.time - ((int)(cl.time / temp) * temp));

    r_skymade = 0;
}


// ============================================================
// Content from: src\r_surf.cpp
// ============================================================
// r_surf.cpp: surface-related refresh code


drawsurf_t r_drawsurf;

int lightleft, sourcesstep, blocksize, sourcetstep;
int lightdelta, lightdeltastep;
int lightright, lightleftstep, lightrightstep, blockdivshift;
unsigned blockdivmask;
void* prowdestbase;
unsigned char* pbasesource;
int surfrowbytes; // used by ASM files
unsigned* r_lightptr;
int r_stepback;
int r_lightwidth;
int r_numhblocks, r_numvblocks;
unsigned char *r_source, *r_sourcemax;

void R_DrawSurfaceBlock8_mip0(void);
void R_DrawSurfaceBlock8_mip1(void);
void R_DrawSurfaceBlock8_mip2(void);
void R_DrawSurfaceBlock8_mip3(void);

static void (*surfmiptable[4])(void) = {
    R_DrawSurfaceBlock8_mip0, R_DrawSurfaceBlock8_mip1,
    R_DrawSurfaceBlock8_mip2, R_DrawSurfaceBlock8_mip3
};

unsigned blocklights[18 * 18];

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights(void)
{
    msurface_t* surf;
    int lnum;
    int sd, td;
    float dist, rad, minlight;
    Vector3 impact, local;
    int s, t;
    int smax, tmax;
    mtexinfo_t* tex;

    surf = r_drawsurf.surf;
    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    tex = surf->texinfo;

    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
        if (!(surf->dlightbits & (1 << lnum))) {
            continue; // not lit by this light
        }

        rad = cl_dlights[lnum].radius;
        dist = cl_dlights[lnum].origin.dot(surf->plane->normal) - surf->plane->dist;
        rad -= fabs(dist);
        minlight = cl_dlights[lnum].minlight;
        if (rad < minlight) {
            continue;
        }

        minlight = rad - minlight;

        impact = cl_dlights[lnum].origin - surf->plane->normal * dist;

        local.x = impact.dot(tex->vecs[0]) + tex->vecs[0][3];
        local.y = impact.dot(tex->vecs[1]) + tex->vecs[1][3];

        local.x -= surf->texturemins[0];
        local.y -= surf->texturemins[1];

        for (t = 0; t < tmax; t++) {
            td = static_cast<int>(local.y - t * 16);
            if (td < 0) {
                td = -td;
            }

            for (s = 0; s < smax; s++) {
                sd = static_cast<int>(local.x - s * 16);
                if (sd < 0) {
                    sd = -sd;
                }

                if (sd > td) {
                    dist = static_cast<float>(sd + (td >> 1));
                } else {
                    dist = static_cast<float>(td + (sd >> 1));
                }

                if (dist < minlight)
                {
                    blocklights[t * smax + s] += static_cast<unsigned int>((rad - dist) * 256);
                }
            }
        }
    }
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap(void)
{
    int smax, tmax;
    int t;
    int i, size;
    byte* lightmap;
    unsigned scale;
    int maps;
    msurface_t* surf;

    surf = r_drawsurf.surf;

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    size = smax * tmax;
    lightmap = surf->samples;

    if (r_fullbright.value || !cl.worldmodel->lightdata) {
        for (i = 0; i < size; i++) {
            blocklights[i] = 0;
        }

        return;
    }

    // clear to ambient
    for (i = 0; i < size; i++) {
        blocklights[i] = r_refdef.ambientlight << 8;
    }

    // add all the lightmaps
    if (lightmap) {
        for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
            scale = r_drawsurf.lightadj[maps]; // 8.8 fraction
            for (i = 0; i < size; i++) {
                blocklights[i] += lightmap[i] * scale;
            }
            lightmap += size; // skip to next lightmap
        }
    }

    // add all the dynamic lights
    if (surf->dlightframe == r_framecount) {
        R_AddDynamicLights();
    }

    // bound, invert, and shift
    for (i = 0; i < size; i++) {
        t = (255 * 256 - (int)blocklights[i]) >> (8 - VID_CBITS);

        if (t < (1 << 6)) {
            t = (1 << 6);
        }

        blocklights[i] = t;
    }
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t* R_TextureAnimation(texture_t* base)
{
    int reletive;
    int count;

    if (currententity->frame) {
        if (base->alternate_anims) {
            base = base->alternate_anims;
        }
    }

    if (!base->anim_total) {
        return base;
    }

    reletive = (int)(cl.time * 10) % base->anim_total;

    count = 0;
    while (base->anim_min > reletive || base->anim_max <= reletive) {
        base = base->anim_next;
        if (!base) {
            Sys_Error("R_TextureAnimation: broken cycle");
        }

        if (++count > 100) {
            Sys_Error("R_TextureAnimation: infinite cycle");
        }
    }

    return base;
}

/*
===============
R_DrawSurface
===============
*/
void R_DrawSurface(void)
{
    unsigned char* basetptr;
    int smax, tmax, twidth;
    int u;
    int soffset, basetoffset, texwidth;
    int horzblockstep;
    unsigned char* pcolumndest;
    void (*pblockdrawer)(void);
    texture_t* mt;

    // calculate the lightings
    R_BuildLightMap();

    surfrowbytes = r_drawsurf.rowbytes;

    mt = r_drawsurf.texture;

    r_source = (byte*)mt + mt->offsets[r_drawsurf.surfmip];

    // the fractional light values should range from 0 to (VID_GRADES - 1) << 16
    // from a source range of 0 - 255

    texwidth = mt->width >> r_drawsurf.surfmip;

    blocksize = 16 >> r_drawsurf.surfmip;
    blockdivshift = 4 - r_drawsurf.surfmip;
    blockdivmask = (1 << blockdivshift) - 1;

    r_lightwidth = (r_drawsurf.surf->extents[0] >> 4) + 1;

    r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
    r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

    //==============================

    if (r_pixbytes == 1) {
        pblockdrawer = surfmiptable[r_drawsurf.surfmip];
        // TODO: only needs to be set when there is a display settings change
        horzblockstep = blocksize;
    } else {
        pblockdrawer = R_DrawSurfaceBlock16;
        // TODO: only needs to be set when there is a display settings change
        horzblockstep = blocksize << 1;
    }

    smax = mt->width >> r_drawsurf.surfmip;
    twidth = texwidth;
    tmax = mt->height >> r_drawsurf.surfmip;
    sourcetstep = texwidth;
    r_stepback = tmax * twidth;

    r_sourcemax = r_source + (tmax * smax);

    soffset = r_drawsurf.surf->texturemins[0];
    basetoffset = r_drawsurf.surf->texturemins[1];

    // << 16 components are to guarantee positive values for %
    soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
    basetptr = &r_source[(
        (((basetoffset >> r_drawsurf.surfmip) + (tmax << 16)) % tmax) * twidth)];

    pcolumndest = r_drawsurf.surfdat;

    for (u = 0; u < r_numhblocks; u++) {
        r_lightptr = blocklights + u;

        prowdestbase = pcolumndest;

        pbasesource = basetptr + soffset;

        (*pblockdrawer)();

        soffset = soffset + blocksize;
        if (soffset >= smax) {
            soffset = 0;
        }

        pcolumndest += horzblockstep;
    }
}

//=============================================================================

/*
================
R_DrawSurfaceBlock8_mip0
================
*/
void R_DrawSurfaceBlock8_mip0(void)
{
    int v, i, b, lightstep, lighttemp, light;
    unsigned char pix, *psource, *prowdest;

    psource = (unsigned char*)pbasesource;
    prowdest = (unsigned char*)prowdestbase;

    for (v = 0; v < r_numvblocks; v++) {
        // FIXME: make these locals?
        // FIXME: use delta rather than both right and left, like ASM?
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> 4;
        lightrightstep = (r_lightptr[1] - lightright) >> 4;

        for (i = 0; i < 16; i++) {
            lighttemp = lightleft - lightright;
            lightstep = lighttemp >> 4;

            light = lightright;

            for (b = 15; b >= 0; b--) {
                pix = psource[b];
                prowdest[b] = ((unsigned char*)vid.colormap)[(light & 0xFF00) + pix];
                light += lightstep;
            }

            psource += sourcetstep;
            lightright += lightrightstep;
            lightleft += lightleftstep;
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax) {
            psource -= r_stepback;
        }
    }
}

/*
================
R_DrawSurfaceBlock8_mip1
================
*/
void R_DrawSurfaceBlock8_mip1(void)
{
    int v, i, b, lightstep, lighttemp, light;
    unsigned char pix, *psource, *prowdest;

    psource = (unsigned char*)pbasesource;
    prowdest = (unsigned char*)prowdestbase;

    for (v = 0; v < r_numvblocks; v++) {
        // FIXME: make these locals?
        // FIXME: use delta rather than both right and left, like ASM?
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> 3;
        lightrightstep = (r_lightptr[1] - lightright) >> 3;

        for (i = 0; i < 8; i++) {
            lighttemp = lightleft - lightright;
            lightstep = lighttemp >> 3;

            light = lightright;

            for (b = 7; b >= 0; b--) {
                pix = psource[b];
                prowdest[b] = ((unsigned char*)vid.colormap)[(light & 0xFF00) + pix];
                light += lightstep;
            }

            psource += sourcetstep;
            lightright += lightrightstep;
            lightleft += lightleftstep;
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax) {
            psource -= r_stepback;
        }
    }
}

/*
================
R_DrawSurfaceBlock8_mip2
================
*/
void R_DrawSurfaceBlock8_mip2(void)
{
    int v, i, b, lightstep, lighttemp, light;
    unsigned char pix, *psource, *prowdest;

    psource = (unsigned char*)pbasesource;
    prowdest = (unsigned char*)prowdestbase;

    for (v = 0; v < r_numvblocks; v++) {
        // FIXME: make these locals?
        // FIXME: use delta rather than both right and left, like ASM?
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> 2;
        lightrightstep = (r_lightptr[1] - lightright) >> 2;

        for (i = 0; i < 4; i++) {
            lighttemp = lightleft - lightright;
            lightstep = lighttemp >> 2;

            light = lightright;

            for (b = 3; b >= 0; b--) {
                pix = psource[b];
                prowdest[b] = ((unsigned char*)vid.colormap)[(light & 0xFF00) + pix];
                light += lightstep;
            }

            psource += sourcetstep;
            lightright += lightrightstep;
            lightleft += lightleftstep;
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax) {
            psource -= r_stepback;
        }
    }
}

/*
================
R_DrawSurfaceBlock8_mip3
================
*/
void R_DrawSurfaceBlock8_mip3(void)
{
    int v, i, b, lightstep, lighttemp, light;
    unsigned char pix, *psource, *prowdest;

    psource = (unsigned char*)pbasesource;
    prowdest = (unsigned char*)prowdestbase;

    for (v = 0; v < r_numvblocks; v++) {
        // FIXME: make these locals?
        // FIXME: use delta rather than both right and left, like ASM?
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> 1;
        lightrightstep = (r_lightptr[1] - lightright) >> 1;

        for (i = 0; i < 2; i++) {
            lighttemp = lightleft - lightright;
            lightstep = lighttemp >> 1;

            light = lightright;

            for (b = 1; b >= 0; b--) {
                pix = psource[b];
                prowdest[b] = ((unsigned char*)vid.colormap)[(light & 0xFF00) + pix];
                light += lightstep;
            }

            psource += sourcetstep;
            lightright += lightrightstep;
            lightleft += lightleftstep;
            prowdest += surfrowbytes;
        }

        if (psource >= r_sourcemax) {
            psource -= r_stepback;
        }
    }
}

/*
================
R_DrawSurfaceBlock16

FIXME: make this work
================
*/
void R_DrawSurfaceBlock16(void)
{
    int k;
    unsigned char* psource;
    int lighttemp, lightstep, light;
    unsigned short* prowdest;

    prowdest = (unsigned short*)prowdestbase;

    for (k = 0; k < blocksize; k++) {
        unsigned short* pdest;
        unsigned char pix;
        int b;

        psource = pbasesource;
        lighttemp = lightright - lightleft;
        lightstep = lighttemp >> blockdivshift;

        light = lightleft;
        pdest = prowdest;

        for (b = 0; b < blocksize; b++) {
            pix = *psource;
            *pdest = vid.colormap16[(light & 0xFF00) + pix];
            psource += sourcesstep;
            pdest++;
            light += lightstep;
        }

        pbasesource += sourcetstep;
        lightright += lightrightstep;
        lightleft += lightleftstep;
        prowdest = (unsigned short*)((size_t)prowdest + surfrowbytes);
    }

    prowdestbase = prowdest;
}


// ============================================================
// Content from: src\r_misc.cpp
// ============================================================


/*
===============
R_CheckVariables
===============
*/
void R_CheckVariables(void)
{
    static float oldbright;

    if (r_fullbright.value != oldbright) {
        oldbright = r_fullbright.value;
        D_FlushCaches(); // so all lighting changes
    }
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f(void)
{
    int i;
    float start, stop, time;
    int startangle;
    vrect_t vr;

    startangle = static_cast<int>(r_refdef.viewangles[1]);

    start = static_cast<float>(Sys_FloatTime());
    for (i = 0; i < 128; i++) {
        r_refdef.viewangles[1] = static_cast<float>(i / 128.0 * 360.0);

        VID_LockBuffer();

        R_RenderView();

        VID_UnlockBuffer();

        vr.x = r_refdef.vrect.x;
        vr.y = r_refdef.vrect.y;
        vr.width = r_refdef.vrect.width;
        vr.height = r_refdef.vrect.height;
        vr.pnext = NULL;
        VID_Update(&vr);
    }
    stop = static_cast<float>(Sys_FloatTime());
    time = stop - start;
    Con_Printf("%f seconds (%f fps)\n", time, 128 / time);

    r_refdef.viewangles[1] = static_cast<float>(startangle);
}

/*
================
R_LineGraph

Only called by R_DisplayTime
================
*/
void R_LineGraph(int x, int y, int h)
{
    int i;
    byte* dest;
    int s;

    // FIXME: should be disabled on no-buffer adapters, or should be in the driver

    x += r_refdef.vrect.x;
    y += r_refdef.vrect.y;

    dest = vid.buffer + vid.rowbytes * y + x;

    s = static_cast<int>(r_graphheight.value);

    if (h > s) {
        h = s;
    }

    for (i = 0; i < h; i++, dest -= vid.rowbytes * 2) {
        dest[0] = 0xff;
        *(dest - vid.rowbytes) = 0x30;
    }
    for (; i < s; i++, dest -= vid.rowbytes * 2) {
        dest[0] = 0x30;
        *(dest - vid.rowbytes) = 0x30;
    }
}

/*
==============
R_TimeGraph

Performance monitoring tool
==============
*/
#define MAX_TIMINGS 100
extern float mouse_x, mouse_y;

void R_TimeGraph(void)
{
    static int timex;
    int a;
    float r_time2;
    static byte r_timings[MAX_TIMINGS];
    int x;

    r_time2 = static_cast<float>(Sys_FloatTime());

    a = static_cast<int>((r_time2 - r_time1) / 0.01);
    //a = fabs(mouse_y * 0.05);
    //a = (int)((r_refdef.vieworg[2] + 1024)/1)%(int)r_graphheight.value;
    //a = fabs(velocity[0])/20;
    //a = ((int)fabs(origin[0])/8)%20;
    //a = (cl.idealpitch + 30)/5;
    r_timings[timex] = static_cast<byte>(a);
    a = timex;

    if (r_refdef.vrect.width <= MAX_TIMINGS) {
        x = r_refdef.vrect.width - 1;
    } else {
        x = r_refdef.vrect.width - (r_refdef.vrect.width - MAX_TIMINGS) / 2;
    }

    do {
        R_LineGraph(x, r_refdef.vrect.height - 2, r_timings[a]);
        if (x == 0) {
            break; // screen too small to hold entire thing
        }

        x--;
        a--;
        if (a == -1) {
            a = MAX_TIMINGS - 1;
        }
    } while (a != timex);

    timex = (timex + 1) % MAX_TIMINGS;
}

/*
=============
R_PrintAliasStats
================
*/
void R_PrintAliasStats(void)
{
    Con_Printf("%3i polygon model drawn\n", r_amodels_drawn);
}

/*
=============
R_PrintTimes
=============
*/
void R_PrintTimes(void)
{
    float r_time2;
    float ms;

    r_time2 = static_cast<float>(Sys_FloatTime());

    ms = static_cast<float>(1000 * (r_time2 - r_time1));

    Con_Printf("%5.1f ms %3i/%3i/%3i poly %3i surf\n", ms, c_faceclip,
        r_polycount, r_drawnpolycount, c_surf);
    c_surf = 0;
}

/*
=============
R_PrintDSpeeds
=============
*/
void R_PrintDSpeeds(void)
{
    float ms, dp_time, r_time2, rw_time, db_time, se_time, de_time, dv_time;

    r_time2 = static_cast<float>(Sys_FloatTime());

    dp_time = static_cast<float>((dp_time2 - dp_time1) * 1000);
    rw_time = (rw_time2 - rw_time1) * 1000;
    db_time = (db_time2 - db_time1) * 1000;
    se_time = (se_time2 - se_time1) * 1000;
    de_time = (de_time2 - de_time1) * 1000;
    dv_time = (dv_time2 - dv_time1) * 1000;
    ms = (r_time2 - r_time1) * 1000;

    Con_Printf("%3i %4.1fp %3iw %4.1fb %3is %4.1fe %4.1fv\n", (int)ms, dp_time,
        (int)rw_time, db_time, (int)se_time, de_time, dv_time);
}

/*
===================
R_TransformFrustum
===================
*/
void R_TransformFrustum(void)
{
    int i;
    Vector3 v, v2;

    for (i = 0; i < 4; i++) {
        v = Vector3(screenedge[i].normal.z, -screenedge[i].normal.x, screenedge[i].normal.y);

        v2 = vright * v.y + vup * v.z + vpn * v.x;

        view_clipplanes[i].normal = v2;

        view_clipplanes[i].dist = modelorg.dot(v2);
    }
}

/*
================
TransformVector
================
*/
void TransformVector(const Vector3& in, Vector3& out)
{
    out.x = in.dot(vright);
    out.y = in.dot(vup);
    out.z = in.dot(vpn);
}

/*
===============
R_SetUpFrustumIndexes
===============
*/
void R_SetUpFrustumIndexes(void)
{
    int i, j, *pindex;

    pindex = r_frustum_indexes;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            if (view_clipplanes[i].normal[j] < 0) {
                pindex[j] = j;
                pindex[j + 3] = j + 3;
            } else {
                pindex[j] = j + 3;
                pindex[j + 3] = j;
            }
        }

        // FIXME: do just once at start
        pfrustum_indexes[i] = pindex;
        pindex += 6;
    }
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame(void)
{
    int edgecount;
    vrect_t vrect;
    float w, h;

    // don't allow cheats in multiplayer
    if (cl.maxclients > 1) {
        Cvar::Set("r_draworder", "0");
        Cvar::Set("r_fullbright", "0");
        Cvar::Set("r_ambient", "0");
        Cvar::Set("r_drawflat", "0");
    }

    if (r_numsurfs.value) {
        if ((surface_p - surfaces) > r_maxsurfsseen) {
            r_maxsurfsseen = static_cast<int>(surface_p - surfaces);
        }

        Con_Printf("Used %d of %d surfs; %d max\n", surface_p - surfaces,
            surf_max - surfaces, r_maxsurfsseen);
    }

    if (r_numedges.value) {
        edgecount = static_cast<int>(edge_p - r_edges);

        if (edgecount > r_maxedgesseen) {
            r_maxedgesseen = edgecount;
        }

        Con_Printf("Used %d of %d edges; %d max\n", edgecount, r_numallocatededges,
            r_maxedgesseen);
    }

    r_refdef.ambientlight = static_cast<int>(r_ambient.value);

    if (r_refdef.ambientlight < 0) {
        r_refdef.ambientlight = 0;
    }

    if (!sv.active) {
        r_draworder.value = 0; // don't let cheaters look behind walls
    }

    R_CheckVariables();

    R_AnimateLight();

    r_framecount++;

    numbtofpolys = 0;

    // build the transformation matrix for the given view angles
    VectorCopy(r_refdef.vieworg, modelorg);
    VectorCopy(r_refdef.vieworg, r_origin);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

    // current viewleaf
    r_oldviewleaf = r_viewleaf;
    r_viewleaf = Mod_PointInLeaf(r_origin, cl.worldmodel);

    r_dowarpold = r_dowarp;
    r_dowarp = r_waterwarp.value && (r_viewleaf->contents <= CONTENTS_WATER);

    if ((r_dowarp != r_dowarpold) || r_viewchanged || lcd_x.value) {
        if (r_dowarp) {
            if (((int)vid.width <= vid.maxwarpwidth) && ((int)vid.height <= vid.maxwarpheight)) {
                vrect.x = 0;
                vrect.y = 0;
                vrect.width = vid.width;
                vrect.height = vid.height;

                R_ViewChanged(&vrect, sb_lines, vid.aspect);
            } else {
                w = static_cast<float>(vid.width);
                h = static_cast<float>(vid.height);

                if (w > vid.maxwarpwidth) {
                    h *= static_cast<float>(vid.maxwarpwidth) / w;
                    w = static_cast<float>(vid.maxwarpwidth);
                }

                if (h > vid.maxwarpheight) {
                    h = static_cast<float>(vid.maxwarpheight);
                    w *= static_cast<float>(vid.maxwarpheight) / h;
                }

                vrect.x = 0;
                vrect.y = 0;
                vrect.width = (int)w;
                vrect.height = (int)h;

                R_ViewChanged(
                    &vrect, static_cast<int>(static_cast<float>(sb_lines) * (h / static_cast<float>(vid.height))),
                    vid.aspect * (h / w) * (static_cast<float>(vid.width) / static_cast<float>(vid.height)));
            }
        } else {
            vrect.x = 0;
            vrect.y = 0;
            vrect.width = vid.width;
            vrect.height = vid.height;

            R_ViewChanged(&vrect, sb_lines, vid.aspect);
        }

        r_viewchanged = false;
    }

    // start off with just the four screen edge clip planes
    R_TransformFrustum();

    // save base values
    VectorCopy(vpn, base_vpn);
    VectorCopy(vright, base_vright);
    VectorCopy(vup, base_vup);
    VectorCopy(modelorg, base_modelorg);

    R_SetSkyFrame();

    R_SetUpFrustumIndexes();

    r_cache_thrash = false;

    // clear frame counts
    c_faceclip = 0;
    d_spanpixcount = 0;
    r_polycount = 0;
    r_drawnpolycount = 0;
    r_wholepolycount = 0;
    r_amodels_drawn = 0;
    r_outofsurfaces = 0;
    r_outofedges = 0;

    D_SetupFrame();
}


// ============================================================
// Content from: src\r_draw.cpp
// ============================================================


#define MAXLEFTCLIPEDGES 100

// !!! if these are changed, they must be changed in asm_draw.h too !!!
#define FULLY_CLIPPED_CACHED 0x80000000
#define FRAMECOUNT_MASK 0x7FFFFFFF

unsigned int cacheoffset;

zpointdesc_t r_zpointdesc;

polydesc_t r_polydesc;

clipplane_t* entity_clipplanes;

medge_t* r_pedge;

qboolean r_leftclipped, r_rightclipped;
static qboolean makeleftedge, makerightedge;
qboolean r_nearzionly;

int sintable[SIN_BUFFER_SIZE];
int intsintable[SIN_BUFFER_SIZE];

mvertex_t r_leftenter, r_leftexit;
mvertex_t r_rightenter, r_rightexit;

typedef struct {
    float u, v;
    int ceilv;
} evert_t;

int r_emitted;
float r_nearzi;
float r_u1, r_v1, r_lzi1;
int r_ceilv1;

qboolean r_lastvertvalid;

/*
================
R_EmitEdge
================
*/
void R_EmitEdge(mvertex_t* pv0, mvertex_t* pv1)
{
    edge_t *edge, *pcheck;
    int64_t u_check; // Changed from int to int64_t
    float u, u_step;
    Vector3 local, transformed;
    float* world;
    int v, v2, ceilv0;
    float scale, lzi0, u0, v0;
    int side;

    if (r_lastvertvalid) {
        u0 = r_u1;
        v0 = r_v1;
        lzi0 = r_lzi1;
        ceilv0 = r_ceilv1;
    } else {
        world = &pv0->position[0];

        // transform and project
        local = Vector3(world) - modelorg;
        TransformVector(local, transformed);

        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }

        lzi0 = static_cast<float>(1.0 / transformed.z);

        // FIXME: build x/yscale into transform?
        scale = xscale * lzi0;
        u0 = (xcenter + scale * transformed.x);
        if (u0 < r_refdef.fvrectx_adj) {
            u0 = r_refdef.fvrectx_adj;
        }

        if (u0 > r_refdef.fvrectright_adj) {
            u0 = r_refdef.fvrectright_adj;
        }

        scale = yscale * lzi0;
        v0 = (ycenter - scale * transformed.y);
        if (v0 < r_refdef.fvrecty_adj) {
            v0 = r_refdef.fvrecty_adj;
        }

        if (v0 > r_refdef.fvrectbottom_adj) {
            v0 = r_refdef.fvrectbottom_adj;
        }

        ceilv0 = (int)ceil(v0);
    }

    world = &pv1->position[0];

    // transform and project
    local = Vector3(world) - modelorg;
    TransformVector(local, transformed);

    if (transformed.z < NEAR_CLIP) {
        transformed.z = (vec_t)NEAR_CLIP;
    }

    r_lzi1 = static_cast<float>(1.0 / transformed.z);

    scale = xscale * r_lzi1;
    r_u1 = (xcenter + scale * transformed.x);
    if (r_u1 < r_refdef.fvrectx_adj) {
        r_u1 = r_refdef.fvrectx_adj;
    }

    if (r_u1 > r_refdef.fvrectright_adj) {
        r_u1 = r_refdef.fvrectright_adj;
    }

    scale = yscale * r_lzi1;
    r_v1 = (ycenter - scale * transformed.y);
    if (r_v1 < r_refdef.fvrecty_adj) {
        r_v1 = r_refdef.fvrecty_adj;
    }

    if (r_v1 > r_refdef.fvrectbottom_adj) {
        r_v1 = r_refdef.fvrectbottom_adj;
    }

    if (r_lzi1 > lzi0) {
        lzi0 = r_lzi1;
    }

    if (lzi0 > r_nearzi) { // for mipmap finding
        r_nearzi = lzi0;
    }

    // for right edges, all we want is the effect on 1/z
    if (r_nearzionly) {
        return;
    }

    r_emitted = 1;

    r_ceilv1 = (int)ceil(r_v1);

    // create the edge
    if (ceilv0 == r_ceilv1) {
        // we cache unclipped horizontal edges as fully clipped
        if (cacheoffset != 0x7FFFFFFF) {
            cacheoffset = FULLY_CLIPPED_CACHED | (r_framecount & FRAMECOUNT_MASK);
        }

        return; // horizontal edge
    }

    side = ceilv0 > r_ceilv1;

    edge = edge_p++;

    edge->owner = r_pedge;

    edge->nearzi = lzi0;

    if (side == 0) {
        // trailing edge (go from p1 to p2)
        v = ceilv0;
        v2 = r_ceilv1 - 1;

        edge->surfs[0] = static_cast<unsigned short>(surface_p - surfaces);
        edge->surfs[1] = 0;

        u_step = ((r_u1 - u0) / (r_v1 - v0));
        u = u0 + ((float)v - v0) * u_step;
    } else {
        // leading edge (go from p2 to p1)
        v2 = ceilv0 - 1;
        v = r_ceilv1;

        edge->surfs[0] = 0;
        edge->surfs[1] = static_cast<unsigned short>(surface_p - surfaces);

        u_step = ((u0 - r_u1) / (v0 - r_v1));
        u = r_u1 + ((float)v - r_v1) * u_step;
    }

    edge->u_step = static_cast<int64_t>(u_step * 0x100000);
    edge->u = static_cast<int64_t>(u * 0x100000 + 0xFFFFF);

    // we need to do this to avoid stepping off the edges if a very nearly
    // horizontal edge is less than epsilon above a scan, and numeric error causes
    // it to incorrectly extend to the scan, and the extension of the line goes off
    // the edge of the screen
    // FIXME: is this actually needed?
    if (edge->u < r_refdef.vrect_x_adj_shift20) {
        edge->u = r_refdef.vrect_x_adj_shift20;
    }

    if (edge->u > r_refdef.vrectright_adj_shift20) {
        edge->u = r_refdef.vrectright_adj_shift20;
    }

    //
    // sort the edge in normally
    //
    u_check = edge->u;
    if (edge->surfs[0]) {
        u_check++; // sort trailers after leaders
    }

    if (!newedges[v] || newedges[v]->u >= u_check) {
        edge->next = newedges[v];
        newedges[v] = edge;
    } else {
        pcheck = newedges[v];
        while (pcheck->next && pcheck->next->u < u_check) {
            pcheck = pcheck->next;
        }
        edge->next = pcheck->next;
        pcheck->next = edge;
    }

    edge->nextremove = removeedges[v2];
    removeedges[v2] = edge;
}

/*
================
R_ClipEdge
================
*/
void R_ClipEdge(mvertex_t* pv0, mvertex_t* pv1, clipplane_t* clip)
{
    float d0, d1, f;
    mvertex_t clipvert;

    if (clip) {
        do {
            d0 = DotProduct(pv0->position, clip->normal) - clip->dist;
            d1 = DotProduct(pv1->position, clip->normal) - clip->dist;

            if (d0 >= 0) {
                // point 0 is unclipped
                if (d1 >= 0) {
                    // both points are unclipped
                    continue;
                }

                // only point 1 is clipped

                // we don't cache clipped edges
                cacheoffset = 0x7FFFFFFF;

                f = d0 / (d0 - d1);
                clipvert.position[0] = pv0->position[0] + f * (pv1->position[0] - pv0->position[0]);
                clipvert.position[1] = pv0->position[1] + f * (pv1->position[1] - pv0->position[1]);
                clipvert.position[2] = pv0->position[2] + f * (pv1->position[2] - pv0->position[2]);

                if (clip->leftedge) {
                    r_leftclipped = true;
                    r_leftexit = clipvert;
                } else if (clip->rightedge) {
                    r_rightclipped = true;
                    r_rightexit = clipvert;
                }

                R_ClipEdge(pv0, &clipvert, clip->next);

                return;
            } else {
                // point 0 is clipped
                if (d1 < 0) {
                    // both points are clipped
                    // we do cache fully clipped edges
                    if (!r_leftclipped) {
                        cacheoffset = FULLY_CLIPPED_CACHED | (r_framecount & FRAMECOUNT_MASK);
                    }

                    return;
                }

                // only point 0 is clipped
                r_lastvertvalid = false;

                // we don't cache partially clipped edges
                cacheoffset = 0x7FFFFFFF;

                f = d0 / (d0 - d1);
                clipvert.position[0] = pv0->position[0] + f * (pv1->position[0] - pv0->position[0]);
                clipvert.position[1] = pv0->position[1] + f * (pv1->position[1] - pv0->position[1]);
                clipvert.position[2] = pv0->position[2] + f * (pv1->position[2] - pv0->position[2]);

                if (clip->leftedge) {
                    r_leftclipped = true;
                    r_leftenter = clipvert;
                } else if (clip->rightedge) {
                    r_rightclipped = true;
                    r_rightenter = clipvert;
                }

                R_ClipEdge(&clipvert, pv1, clip->next);

                return;
            }
        } while ((clip = clip->next) != NULL);
    }

    // add the edge
    R_EmitEdge(pv0, pv1);
}

/*
================
R_EmitCachedEdge
================
*/
void R_EmitCachedEdge(void)
{
    edge_t* pedge_t;

    pedge_t = (edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset);

    if (!pedge_t->surfs[0]) {
        pedge_t->surfs[0] = static_cast<unsigned short>(surface_p - surfaces);
    } else {
        pedge_t->surfs[1] = static_cast<unsigned short>(surface_p - surfaces);
    }

    if (pedge_t->nearzi > r_nearzi) { // for mipmap finding
        r_nearzi = pedge_t->nearzi;
    }

    r_emitted = 1;
}

/*
================
R_RenderFace
================
*/
void R_RenderFace(msurface_t* fa, int clipflags)
{
    int i, lindex;
    unsigned mask;
    mplane_t* pplane;
    float distinv;
    Vector3 p_normal;
    medge_t *pedges;
    static medge_t tedge;
    clipplane_t* pclip;

    // skip out if no more surfs
    if ((surface_p) >= surf_max) {
        r_outofsurfaces++;

        return;
    }

    // ditto if not enough edges left, or switch to auxedges if possible
    if ((edge_p + fa->numedges + 4) >= edge_max) {
        r_outofedges += fa->numedges;

        return;
    }

    c_faceclip++;

    // set up clip planes
    pclip = NULL;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    // push the edges through
    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = false;
    makeleftedge = makerightedge = false;
    pedges = currententity->model->edges;
    r_lastvertvalid = false;

    for (i = 0; i < fa->numedges; i++) {
        lindex = currententity->model->surfedges[fa->firstedge + i];

        if (lindex > 0) {
            r_pedge = &pedges[lindex];

            // if the edge is cached, we can just reuse the edge
            if (!insubmodel) {
                if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                    if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) == (unsigned int)r_framecount) {
                        r_lastvertvalid = false;
                        continue;
                    }
                } else {
                    if ((((size_t)edge_p - (size_t)r_edges) > r_pedge->cachededgeoffset) && (((edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset))->owner == r_pedge)) {
                        R_EmitCachedEdge();
                        r_lastvertvalid = false;
                        continue;
                    }
                }
            }

            // assume it's cacheable
            cacheoffset = static_cast<unsigned int>((byte*)edge_p - (byte*)r_edges);
            r_leftclipped = r_rightclipped = false;
            R_ClipEdge(&r_pcurrentvertbase[r_pedge->v[0]],
                &r_pcurrentvertbase[r_pedge->v[1]], pclip);
            r_pedge->cachededgeoffset = cacheoffset;

            if (r_leftclipped) {
                makeleftedge = true;
            }

            if (r_rightclipped) {
                makerightedge = true;
            }

            r_lastvertvalid = true;
        } else {
            lindex = -lindex;
            r_pedge = &pedges[lindex];
            // if the edge is cached, we can just reuse the edge
            if (!insubmodel) {
                if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                    if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) == (unsigned int)r_framecount) {
                        r_lastvertvalid = false;
                        continue;
                    }
                } else {
                    // it's cached if the cached edge is valid and is owned
                    // by this medge_t
                    if ((((size_t)edge_p - (size_t)r_edges) > r_pedge->cachededgeoffset) && (((edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset))->owner == r_pedge)) {
                        R_EmitCachedEdge();
                        r_lastvertvalid = false;
                        continue;
                    }
                }
            }

            // assume it's cacheable
            cacheoffset = static_cast<unsigned int>((byte*)edge_p - (byte*)r_edges);
            r_leftclipped = r_rightclipped = false;
            R_ClipEdge(&r_pcurrentvertbase[r_pedge->v[1]],
                &r_pcurrentvertbase[r_pedge->v[0]], pclip);
            r_pedge->cachededgeoffset = cacheoffset;

            if (r_leftclipped) {
                makeleftedge = true;
            }

            if (r_rightclipped) {
                makerightedge = true;
            }

            r_lastvertvalid = true;
        }
    }

    // if there was a clip off the left edge, add that edge too
    // FIXME: faster to do in screen space?
    // FIXME: share clipped edges?
    if (makeleftedge) {
        r_pedge = &tedge;
        r_lastvertvalid = false;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }

    // if there was a clip off the right edge, get the right r_nearzi
    if (makerightedge) {
        r_pedge = &tedge;
        r_lastvertvalid = false;
        r_nearzionly = true;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }

    // if no edges made it out, return without posting the surface
    if (!r_emitted) {
        return;
    }

    r_polycount++;

    surface_p->data = (void*)fa;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = fa->flags;
    surface_p->insubmodel = insubmodel;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentkey++;
    surface_p->spans = NULL;

    pplane = fa->plane;
    // FIXME: cache this?
    TransformVector(pplane->normal, p_normal);
    // FIXME: cache this?
    distinv = static_cast<float>(1.0 / (pplane->dist - modelorg.dot(pplane->normal)));

    surface_p->d_zistepu = p_normal.x * xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal.y * yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal.z * distinv - xcenter * surface_p->d_zistepu - ycenter * surface_p->d_zistepv;

    //JDC	VectorCopy (r_worldmodelorg, surface_p->modelorg);
    surface_p++;
}

/*
================
R_RenderBmodelFace
================
*/
void R_RenderBmodelFace(bedge_t* pedges, msurface_t* psurf)
{
    int i;
    unsigned mask;
    mplane_t* pplane;
    float distinv;
    Vector3 p_normal;
    static medge_t tedge;
    clipplane_t* pclip;

    // skip out if no more surfs
    if (surface_p >= surf_max) {
        r_outofsurfaces++;

        return;
    }

    // ditto if not enough edges left, or switch to auxedges if possible
    if ((edge_p + psurf->numedges + 4) >= edge_max) {
        r_outofedges += psurf->numedges;

        return;
    }

    c_faceclip++;

    // this is a dummy to give the caching mechanism someplace to write to
    r_pedge = &tedge;

    // set up clip planes
    pclip = NULL;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (r_clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    // push the edges through
    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = false;
    makeleftedge = makerightedge = false;
    // FIXME: keep clipped bmodel edges in clockwise order so last vertex caching
    // can be used?
    r_lastvertvalid = false;

    for (; pedges; pedges = pedges->pnext) {
        r_leftclipped = r_rightclipped = false;
        R_ClipEdge(pedges->v[0], pedges->v[1], pclip);

        if (r_leftclipped) {
            makeleftedge = true;
        }

        if (r_rightclipped) {
            makerightedge = true;
        }
    }

    // if there was a clip off the left edge, add that edge too
    // FIXME: faster to do in screen space?
    // FIXME: share clipped edges?
    if (makeleftedge) {
        r_pedge = &tedge;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }

    // if there was a clip off the right edge, get the right r_nearzi
    if (makerightedge) {
        r_pedge = &tedge;
        r_nearzionly = true;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }

    // if no edges made it out, return without posting the surface
    if (!r_emitted) {
        return;
    }

    r_polycount++;

    surface_p->data = (void*)psurf;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = psurf->flags;
    surface_p->insubmodel = true;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentbkey;
    surface_p->spans = NULL;

    pplane = psurf->plane;
    // FIXME: cache this?
    TransformVector(pplane->normal, p_normal);
    // FIXME: cache this?
    distinv = static_cast<float>(1.0 / (pplane->dist - modelorg.dot(pplane->normal)));

    surface_p->d_zistepu = p_normal.x * xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal.y * yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal.z * distinv - xcenter * surface_p->d_zistepu - ycenter * surface_p->d_zistepv;

    //JDC	VectorCopy (r_worldmodelorg, surface_p->modelorg);
    surface_p++;
}

/*
================
R_RenderPoly
================
*/
void R_RenderPoly(msurface_t* fa, int clipflags)
{
    int i, lindex, lnumverts, s_axis, t_axis;
    float dist, lastdist, lzi, scale, u, v, frac;
    unsigned mask;
    Vector3 local, transformed;
    clipplane_t* pclip;
    medge_t* pedges;
    mplane_t* pplane;
    mvertex_t verts[2][100]; //FIXME: do real number
    polyvert_t pverts[100];  //FIXME: do real number, safely
    int vertpage, newverts, newpage, lastvert;
    qboolean visible;

    // FIXME: clean this up and make it faster
    // FIXME: guard against running out of vertices

    s_axis = t_axis = 0; // keep compiler happy

    // set up clip planes
    pclip = NULL;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    // reconstruct the polygon
    // FIXME: these should be precalculated and loaded off disk
    pedges = currententity->model->edges;
    lnumverts = fa->numedges;
    vertpage = 0;

    for (i = 0; i < lnumverts; i++) {
        lindex = currententity->model->surfedges[fa->firstedge + i];

        if (lindex > 0) {
            r_pedge = &pedges[lindex];
            verts[0][i] = r_pcurrentvertbase[r_pedge->v[0]];
        } else {
            r_pedge = &pedges[-lindex];
            verts[0][i] = r_pcurrentvertbase[r_pedge->v[1]];
        }
    }

    // clip the polygon, done if not visible
    while (pclip) {
        lastvert = lnumverts - 1;
        lastdist = Vector3(verts[vertpage][lastvert].position).dot(pclip->normal) - pclip->dist;

        visible = false;
        newverts = 0;
        newpage = vertpage ^ 1;

        for (i = 0; i < lnumverts; i++) {
            dist = Vector3(verts[vertpage][i].position).dot(pclip->normal) - pclip->dist;

            if ((lastdist > 0) != (dist > 0)) {
                frac = dist / (dist - lastdist);
                Vector3 interp = Vector3(verts[vertpage][i].position) + (Vector3(verts[vertpage][lastvert].position) - Vector3(verts[vertpage][i].position)) * frac;
                verts[newpage][newverts].position[0] = interp.x;
                verts[newpage][newverts].position[1] = interp.y;
                verts[newpage][newverts].position[2] = interp.z;
                newverts++;
            }

            if (dist >= 0) {
                verts[newpage][newverts] = verts[vertpage][i];
                newverts++;
                visible = true;
            }

            lastvert = i;
            lastdist = dist;
        }

        if (!visible || (newverts < 3)) {
            return;
        }

        lnumverts = newverts;
        vertpage ^= 1;
        pclip = pclip->next;
    }

    // transform and project, remembering the z values at the vertices and
    // r_nearzi, and extract the s and t coordinates at the vertices
    pplane = fa->plane;
    switch (pplane->type) {
    case PLANE_X:
    case PLANE_ANYX:
        s_axis = 1;
        t_axis = 2;
        break;
    case PLANE_Y:
    case PLANE_ANYY:
        s_axis = 0;
        t_axis = 2;
        break;
    case PLANE_Z:
    case PLANE_ANYZ:
        s_axis = 0;
        t_axis = 1;
        break;
    }

    r_nearzi = 0;

    for (i = 0; i < lnumverts; i++) {
        // transform and project
        local = Vector3(verts[vertpage][i].position) - modelorg;
        TransformVector(local, transformed);

        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }

        lzi = static_cast<float>(1.0 / transformed.z);

        if (lzi > r_nearzi) { // for mipmap finding
            r_nearzi = lzi;
        }

        // FIXME: build x/yscale into transform?
        scale = xscale * lzi;
        u = (xcenter + scale * transformed.x);
        if (u < r_refdef.fvrectx_adj) {
            u = r_refdef.fvrectx_adj;
        }

        if (u > r_refdef.fvrectright_adj) {
            u = r_refdef.fvrectright_adj;
        }

        scale = yscale * lzi;
        v = (ycenter - scale * transformed.y);
        if (v < r_refdef.fvrecty_adj) {
            v = r_refdef.fvrecty_adj;
        }

        if (v > r_refdef.fvrectbottom_adj) {
            v = r_refdef.fvrectbottom_adj;
        }

        pverts[i].u = u;
        pverts[i].v = v;
        pverts[i].zi = lzi;
        pverts[i].s = verts[vertpage][i].position[s_axis];
        pverts[i].t = verts[vertpage][i].position[t_axis];
    }

    // build the polygon descriptor, including fa, r_nearzi, and u, v, s, t, and z
    // for each vertex
    r_polydesc.numverts = lnumverts;
    r_polydesc.nearzi = r_nearzi;
    r_polydesc.pcurrentface = fa;
    r_polydesc.pverts = pverts;

    // draw the polygon
    D_DrawPoly();
}

/*
================
R_ZDrawSubmodelPolys
================
*/
void R_ZDrawSubmodelPolys(model_t* pmodel)
{
    int i, numsurfaces;
    msurface_t* psurf;
    float dot;
    mplane_t* pplane;

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            // FIXME: use bounding-box-based frustum clipping info?
            R_RenderPoly(psurf, 15);
        }
    }
}


// ============================================================
// Content from: src\r_edge.cpp
// ============================================================

#include <limits.h>


surf_t *surfaces, *surface_p, *surf_max;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack

espan_t *span_p, *max_span_p;

extern int screenwidth;

int current_iv;

int64_t edge_head_u_shift20, edge_tail_u_shift20; // Changed from int to int64_t

static void (*pdrawfunc)(void);

edge_t edge_sentinel;

float edge_fv;

void R_GenerateSpans(void);
void R_GenerateSpansBackward(void);

void R_LeadingEdge(edge_t* edge);
void R_LeadingEdgeBackwards(edge_t* edge);
void R_TrailingEdge(surf_t* surf, edge_t* edge);

//=============================================================================

/*
==============
R_DrawCulledPolys
==============
*/
void R_DrawCulledPolys(void)
{
    surf_t* s;
    msurface_t* pface;

    currententity = &cl_entities[0];

    if (r_worldpolysbacktofront) {
        for (s = surface_p - 1; s > &surfaces[1]; s--) {
            if (!s->spans) {
                continue;
            }

            if (!(s->flags & SURF_DRAWBACKGROUND)) {
                pface = (msurface_t*)s->data;
                R_RenderPoly(pface, 15);
            }
        }
    } else {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }

            if (!(s->flags & SURF_DRAWBACKGROUND)) {
                pface = (msurface_t*)s->data;
                R_RenderPoly(pface, 15);
            }
        }
    }
}

/*
==============
R_BeginEdgeFrame
==============
*/
void R_BeginEdgeFrame(void)
{
    int v;

    edge_p = r_edges;
    edge_max = &r_edges[r_numallocatededges];

    surface_p = &surfaces[2]; // background is surface 1,
    //  surface 0 is a dummy
    surfaces[1].spans = NULL; // no background spans yet
    surfaces[1].flags = SURF_DRAWBACKGROUND;

    // put the background behind everything in the world
    if (r_draworder.value) {
        pdrawfunc = R_GenerateSpansBackward;
        surfaces[1].key = 0;
        r_currentkey = 1;
    } else {
        pdrawfunc = R_GenerateSpans;
        surfaces[1].key = 0x7FFFFFFF;
        r_currentkey = 0;
    }

    // FIXME: set with memset
    for (v = r_refdef.vrect.y; v < r_refdef.vrectbottom; v++) {
        newedges[v] = removeedges[v] = NULL;
    }
}

/*
==============
R_InsertNewEdges

Adds the edges in the linked list edgestoadd, adding them to the edges in the
linked list edgelist.  edgestoadd is assumed to be sorted on u, and non-empty (this is actually newedges[v]).  edgelist is assumed to be sorted on u, with a
sentinel at the end (actually, this is the active edge table starting at
edge_head.next).
==============
*/
void R_InsertNewEdges(edge_t* edgestoadd, edge_t* edgelist)
{
    edge_t* next_edge;

    do {
        next_edge = edgestoadd->next;
    edgesearch:
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }

        edgelist = edgelist->next;
        goto edgesearch;

        // insert edgestoadd before edgelist
    addedge:
        edgestoadd->next = edgelist;
        edgestoadd->prev = edgelist->prev;
        edgelist->prev->next = edgestoadd;
        edgelist->prev = edgestoadd;
    } while ((edgestoadd = next_edge) != NULL);
}

/*
==============
R_RemoveEdges
==============
*/
void R_RemoveEdges(edge_t* pedge)
{
    do {
        pedge->next->prev = pedge->prev;
        pedge->prev->next = pedge->next;
    } while ((pedge = pedge->nextremove) != NULL);
}

/*
==============
R_StepActiveU
==============
*/
void R_StepActiveU(edge_t* pedge)
{
    edge_t *pnext_edge, *pwedge;

    while (1) {
    nextedge:
        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }

        pedge = pedge->next;

        goto nextedge;

    pushback:
        if (pedge == &edge_aftertail) {
            return;
        }

        // push it back to keep it sorted
        pnext_edge = pedge->next;

        // pull the edge out of the edge list
        pedge->next->prev = pedge->prev;
        pedge->prev->next = pedge->next;

        // find out where the edge goes in the edge list
        pwedge = pedge->prev->prev;

        while (pwedge->u > pedge->u) {
            pwedge = pwedge->prev;
        }

        // put the edge back into the edge list
        pedge->next = pwedge->next;
        pedge->prev = pwedge;
        pedge->next->prev = pedge;
        pwedge->next = pedge;

        pedge = pnext_edge;
        if (pedge == &edge_tail) {
            return;
        }
    }
}

/*
==============
R_CleanupSpan
==============
*/
void R_CleanupSpan()
{
    surf_t* surf;
    int iu;
    espan_t* span;

    // now that we've reached the right edge of the screen, we're done with any
    // unfinished surfaces, so emit a span for whatever's on top
    surf = surfaces[1].next;
    iu = static_cast<int>(edge_tail_u_shift20);
    if (iu > surf->last_u) {
        span = span_p++;
        span->u = surf->last_u;
        span->count = iu - span->u;
        span->v = current_iv;
        span->pnext = surf->spans;
        surf->spans = span;
    }

    // reset spanstate for all surfaces in the surface stack
    do {
        surf->spanstate = 0;
        surf = surf->next;
    } while (surf != &surfaces[1]);
}

/*
==============
R_LeadingEdgeBackwards
==============
*/
void R_LeadingEdgeBackwards(edge_t* edge)
{
    espan_t* span;
    surf_t *surf, *surf2;
    int iu;

    // it's adding a new surface in, so find the correct place
    surf = &surfaces[edge->surfs[1]];

    // don't start a span if this is an inverted span, with the end
    // edge preceding the start edge (that is, we've already seen the
    // end edge)
    if (++surf->spanstate == 1) {
        surf2 = surfaces[1].next;

        if (surf->key > surf2->key) {
            goto newtop;
        }

        // if it's two surfaces on the same plane, the one that's already
        // active is in front, so keep going unless it's a bmodel
        if (surf->insubmodel && (surf->key == surf2->key)) {
            // must be two bmodels in the same leaf; don't care, because they'll
            // never be farthest anyway
            goto newtop;
        }

    continue_search:

        do {
            surf2 = surf2->next;
        } while (surf->key < surf2->key);

        if (surf->key == surf2->key) {
            // if it's two surfaces on the same plane, the one that's already
            // active is in front, so keep going unless it's a bmodel
            if (!surf->insubmodel) {
                goto continue_search;
            }

            // must be two bmodels in the same leaf; don't care which is really
            // in front, because they'll never be farthest anyway
        }

        goto gotposition;

    newtop:
        // emit a span (obscures current top)
        iu = static_cast<int>(edge->u >> 20);

        if (iu > surf2->last_u) {
            span = span_p++;
            span->u = surf2->last_u;
            span->count = iu - span->u;
            span->v = current_iv;
            span->pnext = surf2->spans;
            surf2->spans = span;
        }

        // set last_u on the new span
        surf->last_u = iu;

    gotposition:
        // insert before surf2
        surf->next = surf2;
        surf->prev = surf2->prev;
        surf2->prev->next = surf;
        surf2->prev = surf;
    }
}

/*
==============
R_TrailingEdge
==============
*/
void R_TrailingEdge(surf_t* surf, edge_t* edge)
{
    espan_t* span;
    int iu;

    // don't generate a span if this is an inverted span, with the end
    // edge preceding the start edge (that is, we haven't seen the
    // start edge yet)
    if (--surf->spanstate == 0) {
        if (surf->insubmodel) {
            r_bmodelactive--;
        }

        if (surf == surfaces[1].next) {
            // emit a span (current top going away)
            iu = static_cast<int>(edge->u >> 20);
            if (iu > surf->last_u) {
                span = span_p++;
                span->u = surf->last_u;
                span->count = iu - span->u;
                span->v = current_iv;
                span->pnext = surf->spans;
                surf->spans = span;
            }

            // set last_u on the surface below
            surf->next->last_u = iu;
        }

        surf->prev->next = surf->next;
        surf->next->prev = surf->prev;
    }
}

/*
==============
R_LeadingEdge
==============
*/
void R_LeadingEdge(edge_t* edge)
{
    espan_t* span;
    surf_t *surf, *surf2;
    int iu;
    double fu, newzi, testzi, newzitop, newzibottom;

    if (edge->surfs[1]) {
        // it's adding a new surface in, so find the correct place
        surf = &surfaces[edge->surfs[1]];

        // don't start a span if this is an inverted span, with the end
        // edge preceding the start edge (that is, we've already seen the
        // end edge)
        if (++surf->spanstate == 1) {
            if (surf->insubmodel) {
                r_bmodelactive++;
            }

            surf2 = surfaces[1].next;

            if (surf->key < surf2->key) {
                goto newtop;
            }

            // if it's two surfaces on the same plane, the one that's already
            // active is in front, so keep going unless it's a bmodel
            if (surf->insubmodel && (surf->key == surf2->key)) {
                // must be two bmodels in the same leaf; sort on 1/z
                fu = static_cast<float>(edge->u - 0xFFFFF) * (1.0f / 0x100000);
                newzi = surf->d_ziorigin + edge_fv * surf->d_zistepv + fu * surf->d_zistepu;
                newzibottom = newzi * 0.99f;

                testzi = surf2->d_ziorigin + edge_fv * surf2->d_zistepv + fu * surf2->d_zistepu;

                if (newzibottom >= testzi) {
                    goto newtop;
                }

                newzitop = newzi * 1.01;
                if (newzitop >= testzi) {
                    if (surf->d_zistepu >= surf2->d_zistepu) {
                        goto newtop;
                    }
                }
            }

        continue_search:

            do {
                surf2 = surf2->next;
            } while (surf->key > surf2->key);

            if (surf->key == surf2->key) {
                // if it's two surfaces on the same plane, the one that's already
                // active is in front, so keep going unless it's a bmodel
                if (!surf->insubmodel) {
                    goto continue_search;
                }

                // must be two bmodels in the same leaf; sort on 1/z
                fu = static_cast<float>(edge->u - 0xFFFFF) * (1.0f / 0x100000);
                newzi = surf->d_ziorigin + edge_fv * surf->d_zistepv + fu * surf->d_zistepu;
                newzibottom = newzi * 0.99f;

                testzi = surf2->d_ziorigin + edge_fv * surf2->d_zistepv + fu * surf2->d_zistepu;

                if (newzibottom >= testzi) {
                    goto gotposition;
                }

                newzitop = newzi * 1.01;
                if (newzitop >= testzi) {
                    if (surf->d_zistepu >= surf2->d_zistepu) {
                        goto gotposition;
                    }
                }

                goto continue_search;
            }

            goto gotposition;

        newtop:
            // emit a span (obscures current top)
            iu = static_cast<int>(edge->u >> 20);

            if (iu > surf2->last_u) {
                span = span_p++;
                span->u = surf2->last_u;
                span->count = iu - span->u;
                span->v = current_iv;
                span->pnext = surf2->spans;
                surf2->spans = span;
            }

            // set last_u on the new span
            surf->last_u = iu;

        gotposition:
            // insert before surf2
            surf->next = surf2;
            surf->prev = surf2->prev;
            surf2->prev->next = surf;
            surf2->prev = surf;
        }
    }
}

/*
==============
R_GenerateSpans
==============
*/
void R_GenerateSpans(void)
{
    edge_t* edge;
    surf_t* surf;

    r_bmodelactive = 0;

    // clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = static_cast<int>(edge_head_u_shift20);

    // generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
        if (edge->surfs[0]) {
            // it has a left surface, so a surface is going away for this span
            surf = &surfaces[edge->surfs[0]];

            R_TrailingEdge(surf, edge);

            if (!edge->surfs[1]) {
                continue;
            }
        }

        R_LeadingEdge(edge);
    }

    R_CleanupSpan();
}

/*
==============
R_GenerateSpansBackward
==============
*/
void R_GenerateSpansBackward(void)
{
    edge_t* edge;

    r_bmodelactive = 0;

    // clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = static_cast<int>(edge_head_u_shift20);

    // generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
        if (edge->surfs[0]) {
            R_TrailingEdge(&surfaces[edge->surfs[0]], edge);
        }

        if (edge->surfs[1]) {
            R_LeadingEdgeBackwards(edge);
        }
    }

    R_CleanupSpan();
}

/*
==============
R_ScanEdges

Input:
newedges[] array
	this has links to edges, which have links to surfaces

Output:
Each surface has a linked list of its visible spans
==============
*/
void R_ScanEdges(void)
{
    int iv, bottom;
    byte basespans[MAXSPANS * sizeof(espan_t) + CACHE_SIZE];
    espan_t* basespan_p;
    surf_t* s;

    basespan_p = (espan_t*)((size_t)(basespans + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    max_span_p = &basespan_p[MAXSPANS - r_refdef.vrect.width];

    span_p = basespan_p;

    // clear active edges to just the background edges around the whole screen
    // FIXME: most of this only needs to be set up once
    edge_head.u = (int64_t)r_refdef.vrect.x << 20;
    edge_head_u_shift20 = edge_head.u >> 20;
    edge_head.u_step = 0;
    edge_head.prev = NULL;
    edge_head.next = &edge_tail;
    edge_head.surfs[0] = 0;
    edge_head.surfs[1] = 1;

    edge_tail.u = ((int64_t)r_refdef.vrectright << 20) + 0xFFFFF;
    edge_tail_u_shift20 = edge_tail.u >> 20;
    edge_tail.u_step = 0;
    edge_tail.prev = &edge_head;
    edge_tail.next = &edge_aftertail;
    edge_tail.surfs[0] = 1;
    edge_tail.surfs[1] = 0;

    edge_aftertail.u = -1; // force a move
    edge_aftertail.u_step = 0;
    edge_aftertail.next = &edge_sentinel;
    edge_aftertail.prev = &edge_tail;

    // FIXME: do we need this now that we clamp x in r_draw.cpp?
    edge_sentinel.u = UINT_MAX; // make sure nothing sorts past this
    edge_sentinel.prev = &edge_aftertail;

    //
    // process all scan lines
    //
    bottom = r_refdef.vrectbottom - 1;

    for (iv = r_refdef.vrect.y; iv < bottom; iv++) {
        current_iv = iv;
        edge_fv = (float)iv;

        // mark that the head (background start) span is pre-included
        surfaces[1].spanstate = 1;

        if (newedges[iv]) {
            R_InsertNewEdges(newedges[iv], edge_head.next);
        }

        (*pdrawfunc)();

        // flush the span list if we can't be sure we have enough spans left for
        // the next scan
        if (span_p >= max_span_p) {
            VID_UnlockBuffer();
            S_ExtraUpdate(); // don't let sound get messed up if going slow
            VID_LockBuffer();

            if (r_drawculledpolys) {
                R_DrawCulledPolys();
            } else {
                D_DrawSurfaces();
            }

            // clear the surface span pointers
            for (s = &surfaces[1]; s < surface_p; s++) {
                s->spans = NULL;
            }

            span_p = basespan_p;
        }

        if (removeedges[iv]) {
            R_RemoveEdges(removeedges[iv]);
        }

        if (edge_head.next != &edge_tail) {
            R_StepActiveU(edge_head.next);
        }
    }

    // do the last scan (no need to step or sort or remove on the last scan)

    current_iv = iv;
    edge_fv = (float)iv;

    // mark that the head (background start) span is pre-included
    surfaces[1].spanstate = 1;

    if (newedges[iv]) {
        R_InsertNewEdges(newedges[iv], edge_head.next);
    }

    (*pdrawfunc)();

    // draw whatever's left in the span list
    if (r_drawculledpolys) {
        R_DrawCulledPolys();
    } else {
        D_DrawSurfaces();
    }
}


// ============================================================
// Content from: src\r_bsp.cpp
// ============================================================


//
// current entity info
//
qboolean insubmodel;
entity_t* currententity;
Vector3 modelorg, base_modelorg;

typedef enum { touchessolid,
    drawnode,
    nodrawnode } solidstate_t;

#define MAX_BMODEL_VERTS 500  // 6K
#define MAX_BMODEL_EDGES 1000 // 12K

static mvertex_t* pbverts;
static bedge_t* pbedges;
static int numbverts, numbedges;

static mvertex_t *pfrontenter, *pfrontexit;

static qboolean makeclippededge;

//===========================================================================

/*
================
R_EntityRotate
================
*/
void R_EntityRotate(Vector3& vec)
{
    Vector3 tvec;

    tvec = vec;
    vec.x = Vector3(entity_rotation[0][0], entity_rotation[0][1], entity_rotation[0][2]).dot(tvec);
    vec.y = Vector3(entity_rotation[1][0], entity_rotation[1][1], entity_rotation[1][2]).dot(tvec);
    vec.z = Vector3(entity_rotation[2][0], entity_rotation[2][1], entity_rotation[2][2]).dot(tvec);
}

/*
================
R_RotateBmodel
================
*/
void R_RotateBmodel(void)
{
    float angle, s, c, temp1[3][3], temp2[3][3], temp3[3][3];

    // TODO: should use a look-up table
    // TODO: should really be stored with the entity instead of being reconstructed
    // TODO: could cache lazily, stored in the entity
    // TODO: share work with R_SetUpAliasTransform

    // yaw
    angle = currententity->angles[YAW];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = c;
    temp1[0][1] = s;
    temp1[0][2] = 0;
    temp1[1][0] = -s;
    temp1[1][1] = c;
    temp1[1][2] = 0;
    temp1[2][0] = 0;
    temp1[2][1] = 0;
    temp1[2][2] = 1;

    // pitch
    angle = currententity->angles[PITCH];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = sin(angle);
    c = cos(angle);

    temp2[0][0] = c;
    temp2[0][1] = 0;
    temp2[0][2] = -s;
    temp2[1][0] = 0;
    temp2[1][1] = 1;
    temp2[1][2] = 0;
    temp2[2][0] = s;
    temp2[2][1] = 0;
    temp2[2][2] = c;

    R_ConcatRotations(temp2, temp1, temp3);

    // roll
    angle = currententity->angles[ROLL];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = 1;
    temp1[0][1] = 0;
    temp1[0][2] = 0;
    temp1[1][0] = 0;
    temp1[1][1] = c;
    temp1[1][2] = s;
    temp1[2][0] = 0;
    temp1[2][1] = -s;
    temp1[2][2] = c;

    R_ConcatRotations(temp1, temp3, entity_rotation);

    //
    // rotate modelorg and the transformation matrix
    //
    R_EntityRotate(modelorg);
    R_EntityRotate(vpn);
    R_EntityRotate(vright);
    R_EntityRotate(vup);

    R_TransformFrustum();
}

/*
================
R_RecursiveClipBPoly
================
*/
void R_RecursiveClipBPoly(bedge_t* pedges, mnode_t* pnode, msurface_t* psurf)
{
    bedge_t *psideedges[2], *pnextedge, *ptedge;
    int i, side, lastside;
    float dist, frac, lastdist;
    mplane_t *splitplane, tplane;
    mvertex_t *pvert, *plastvert, *ptvert;
    mnode_t* pn;

    psideedges[0] = psideedges[1] = NULL;

    makeclippededge = false;

    // transform the BSP plane into model space
    // FIXME: cache these?
    splitplane = pnode->plane;
    tplane.dist = splitplane->dist - DotProduct(r_entorigin, splitplane->normal);
    tplane.normal[0] = DotProduct(entity_rotation[0], splitplane->normal);
    tplane.normal[1] = DotProduct(entity_rotation[1], splitplane->normal);
    tplane.normal[2] = DotProduct(entity_rotation[2], splitplane->normal);

    // clip edges to BSP plane
    for (; pedges; pedges = pnextedge) {
        pnextedge = pedges->pnext;

        // set the status for the last point as the previous point
        // FIXME: cache this stuff somehow?
        plastvert = pedges->v[0];
        lastdist = DotProduct(plastvert->position, tplane.normal) - tplane.dist;

        if (lastdist > 0) {
            lastside = 0;
        } else {
            lastside = 1;
        }

        pvert = pedges->v[1];

        dist = DotProduct(pvert->position, tplane.normal) - tplane.dist;

        if (dist > 0) {
            side = 0;
        } else {
            side = 1;
        }

        if (side != lastside) {
            // clipped
            if (numbverts >= MAX_BMODEL_VERTS) {
                return;
            }

            // generate the clipped vertex
            frac = lastdist / (lastdist - dist);
            ptvert = &pbverts[numbverts++];
            ptvert->position[0] = plastvert->position[0] + frac * (pvert->position[0] - plastvert->position[0]);
            ptvert->position[1] = plastvert->position[1] + frac * (pvert->position[1] - plastvert->position[1]);
            ptvert->position[2] = plastvert->position[2] + frac * (pvert->position[2] - plastvert->position[2]);

            // split into two edges, one on each side, and remember entering
            // and exiting points
            // FIXME: share the clip edge by having a winding direction flag?
            if (numbedges >= (MAX_BMODEL_EDGES - 1)) {
                Con_Printf("Out of edges for bmodel\n");

                return;
            }

            ptedge = &pbedges[numbedges];
            ptedge->pnext = psideedges[lastside];
            psideedges[lastside] = ptedge;
            ptedge->v[0] = plastvert;
            ptedge->v[1] = ptvert;

            ptedge = &pbedges[numbedges + 1];
            ptedge->pnext = psideedges[side];
            psideedges[side] = ptedge;
            ptedge->v[0] = ptvert;
            ptedge->v[1] = pvert;

            numbedges += 2;

            if (side == 0) {
                // entering for front, exiting for back
                pfrontenter = ptvert;
                makeclippededge = true;
            } else {
                pfrontexit = ptvert;
                makeclippededge = true;
            }
        } else {
            // add the edge to the appropriate side
            pedges->pnext = psideedges[side];
            psideedges[side] = pedges;
        }
    }

    // if anything was clipped, reconstitute and add the edges along the clip
    // plane to both sides (but in opposite directions)
    if (makeclippededge) {
        if (numbedges >= (MAX_BMODEL_EDGES - 2)) {
            Con_Printf("Out of edges for bmodel\n");

            return;
        }

        ptedge = &pbedges[numbedges];
        ptedge->pnext = psideedges[0];
        psideedges[0] = ptedge;
        ptedge->v[0] = pfrontexit;
        ptedge->v[1] = pfrontenter;

        ptedge = &pbedges[numbedges + 1];
        ptedge->pnext = psideedges[1];
        psideedges[1] = ptedge;
        ptedge->v[0] = pfrontenter;
        ptedge->v[1] = pfrontexit;

        numbedges += 2;
    }

    // draw or recurse further
    for (i = 0; i < 2; i++) {
        if (psideedges[i]) {
            // draw if we've reached a non-solid leaf, done if all that's left is a
            // solid leaf, and continue down the tree if it's not a leaf
            pn = pnode->children[i];

            // we're done with this branch if the node or leaf isn't in the PVS
            if (pn->visframe == r_visframecount) {
                if (pn->contents < 0) {
                    if (pn->contents != CONTENTS_SOLID) {
                        r_currentbkey = ((mleaf_t*)pn)->key;
                        R_RenderBmodelFace(psideedges[i], psurf);
                    }
                } else {
                    R_RecursiveClipBPoly(psideedges[i], pnode->children[i], psurf);
                }
            }
        }
    }
}

/*
================
R_DrawSolidClippedSubmodelPolygons
================
*/
void R_DrawSolidClippedSubmodelPolygons(model_t* pmodel)
{
    int i, j, lindex;
    vec_t dot;
    msurface_t* psurf;
    int numsurfaces;
    mplane_t* pplane;
    mvertex_t bverts[MAX_BMODEL_VERTS];
    bedge_t bedges[MAX_BMODEL_EDGES], *pbedge;
    medge_t *pedge, *pedges;

    // FIXME: use bounding-box-based frustum clipping info?

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;
    pedges = pmodel->edges;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            // FIXME: use bounding-box-based frustum clipping info?

            // copy the edges to bedges, flipping if necessary so always
            // clockwise winding
            // FIXME: if edges and vertices get caches, these assignments must move
            // outside the loop, and overflow checking must be done here
            pbverts = bverts;
            pbedges = bedges;
            numbverts = numbedges = 0;

            if (psurf->numedges > 0) {
                pbedge = &bedges[numbedges];
                numbedges += psurf->numedges;

                for (j = 0; j < psurf->numedges; j++) {
                    lindex = pmodel->surfedges[psurf->firstedge + j];

                    if (lindex > 0) {
                        pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
                    } else {
                        lindex = -lindex;
                        pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
                    }

                    pbedge[j].pnext = &pbedge[j + 1];
                }

                pbedge[j - 1].pnext = NULL; // mark end of edges

                R_RecursiveClipBPoly(pbedge, currententity->topnode, psurf);
            } else {
                Sys_Error("no edges in bmodel");
            }
        }
    }
}

/*
================
R_DrawSubmodelPolygons
================
*/
void R_DrawSubmodelPolygons(model_t* pmodel, int clipflags)
{
    int i;
    vec_t dot;
    msurface_t* psurf;
    int numsurfaces;
    mplane_t* pplane;

    // FIXME: use bounding-box-based frustum clipping info?

    psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    numsurfaces = pmodel->nummodelsurfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            r_currentkey = ((mleaf_t*)currententity->topnode)->key;

            // FIXME: use bounding-box-based frustum clipping info?
            R_RenderFace(psurf, clipflags);
        }
    }
}

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode(mnode_t* node, int clipflags)
{
    int i, c, side, *pindex;
    Vector3 acceptpt, rejectpt;
    mplane_t* plane;
    msurface_t *surf, **mark;
    mleaf_t* pleaf;
    double d, dot;

    if (node->contents == CONTENTS_SOLID) {
        return; // solid
    }

    if (node->visframe != r_visframecount) {
        return;
    }

    // cull the clipping planes if not trivial accept
    // FIXME: the compiler is doing a lousy job of optimizing here; it could be
    //  twice as fast in ASM
    if (clipflags) {
        for (i = 0; i < 4; i++) {
            if (!(clipflags & (1 << i))) {
                continue; // don't need to clip against it
            }

            // generate accept and reject points
            // FIXME: do with fast look-ups or integer tests based on the sign bit
            // of the floating point values

            pindex = pfrustum_indexes[i];

            rejectpt = Vector3((float)node->minmaxs[pindex[0]], (float)node->minmaxs[pindex[1]], (float)node->minmaxs[pindex[2]]);

            d = rejectpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0) {
                return;
            }

            acceptpt = Vector3((float)node->minmaxs[pindex[3 + 0]], (float)node->minmaxs[pindex[3 + 1]], (float)node->minmaxs[pindex[3 + 2]]);

            d = acceptpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d >= 0) {
                clipflags &= ~(1 << i); // node is entirely on screen
            }
        }
    }

    // if a leaf node, draw stuff
    if (node->contents < 0) {
        pleaf = (mleaf_t*)node;

        mark = pleaf->firstmarksurface;
        c = pleaf->nummarksurfaces;

        if (c) {
            do {
                (*mark)->visframe = r_framecount;
                mark++;
            } while (--c);
        }

        // deal with model fragments in this leaf
        if (pleaf->efrags) {
            R_StoreEfrags(&pleaf->efrags);
        }

        pleaf->key = r_currentkey;
        r_currentkey++; // all bmodels in a leaf share the same key
    } else {
        // node is just a decision point, so go down the apropriate sides

        // find which side of the node we are on
        plane = node->plane;

        switch (plane->type) {
        case PLANE_X:
            dot = modelorg[0] - plane->dist;
            break;
        case PLANE_Y:
            dot = modelorg[1] - plane->dist;
            break;
        case PLANE_Z:
            dot = modelorg[2] - plane->dist;
            break;
        default:
            dot = DotProduct(modelorg, plane->normal) - plane->dist;
            break;
        }

        if (dot >= 0) {
            side = 0;
        } else {
            side = 1;
        }

        // recurse down the children, front side first
        R_RecursiveWorldNode(node->children[side], clipflags);

        // draw stuff
        c = node->numsurfaces;

        if (c) {
            surf = cl.worldmodel->surfaces + node->firstsurface;

            if (dot < -BACKFACE_EPSILON) {
                do {
                    if ((surf->flags & SURF_PLANEBACK) && (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags = clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }

                    surf++;
                } while (--c);
            } else if (dot > BACKFACE_EPSILON) {
                do {
                    if (!(surf->flags & SURF_PLANEBACK) && (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags = clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }

                    surf++;
                } while (--c);
            }

            // all surfaces on the same node share the same sequence number
            r_currentkey++;
        }

        // recurse down the back side
        R_RecursiveWorldNode(node->children[!side], clipflags);
    }
}

/*
================
R_RenderWorld
================
*/
void R_RenderWorld(void)
{
    int i;
    model_t* clmodel;
    btofpoly_t btofpolys[MAX_BTOFPOLYS];

    pbtofpolys = btofpolys;

    currententity = &cl_entities[0];
    VectorCopy(r_origin, modelorg);
    clmodel = currententity->model;
    r_pcurrentvertbase = clmodel->vertexes;

    R_RecursiveWorldNode(clmodel->nodes, 15);

    // if the driver wants the polygons back to front, play the visible ones back
    // in that order
    if (r_worldpolysbacktofront) {
        for (i = numbtofpolys - 1; i >= 0; i--) {
            R_RenderPoly(btofpolys[i].psurf, btofpolys[i].clipflags);
        }
    }
}


// ============================================================
// Content from: src\r_sprite.cpp
// ============================================================


static int clip_current;
static vec5_t clip_verts[2][MAXWORKINGVERTS];
static int sprite_width, sprite_height;

spritedesc_t r_spritedesc;

/*
================
R_RotateSprite
================
*/
void R_RotateSprite(float beam_len)
{
    Vector3 vec;

    if (beam_len == 0.0) {
        return;
    }

    vec = r_spritedesc.vpn * -beam_len;
    r_entorigin += vec;
    modelorg -= vec;
}

/*
=============
R_ClipSpriteFace

Clips the winding at clip_verts[clip_current] and changes clip_current
Throws out the back side
==============
*/
int R_ClipSpriteFace(int nump, clipplane_t* pclipplane)
{
    int i, outcount;
    float dists[MAXWORKINGVERTS + 1];
    float frac, clipdist, *pclipnormal;
    float *in, *instep, *outstep, *vert2;

    clipdist = pclipplane->dist;
    pclipnormal = pclipplane->normal;

    // calc dists
    if (clip_current) {
        in = clip_verts[1][0];
        outstep = clip_verts[0][0];
        clip_current = 0;
    } else {
        in = clip_verts[0][0];
        outstep = clip_verts[1][0];
        clip_current = 1;
    }

    instep = in;
    for (i = 0; i < nump; i++, instep += sizeof(vec5_t) / sizeof(float)) {
        dists[i] = DotProduct(instep, pclipnormal) - clipdist;
    }

    // handle wraparound case
    dists[nump] = dists[0];
    Q_memcpy(instep, in, sizeof(vec5_t));

    // clip the winding
    instep = in;
    outcount = 0;

    for (i = 0; i < nump; i++, instep += sizeof(vec5_t) / sizeof(float)) {
        if (dists[i] >= 0) {
            Q_memcpy(outstep, instep, sizeof(vec5_t));
            outstep += sizeof(vec5_t) / sizeof(float);
            outcount++;
        }

        if (dists[i] == 0 || dists[i + 1] == 0) {
            continue;
        }

        if ((dists[i] > 0) == (dists[i + 1] > 0)) {
            continue;
        }

        // split it into a new vertex
        frac = dists[i] / (dists[i] - dists[i + 1]);

        vert2 = instep + sizeof(vec5_t) / sizeof(float);

        outstep[0] = instep[0] + frac * (vert2[0] - instep[0]);
        outstep[1] = instep[1] + frac * (vert2[1] - instep[1]);
        outstep[2] = instep[2] + frac * (vert2[2] - instep[2]);
        outstep[3] = instep[3] + frac * (vert2[3] - instep[3]);
        outstep[4] = instep[4] + frac * (vert2[4] - instep[4]);

        outstep += sizeof(vec5_t) / sizeof(float);
        outcount++;
    }

    return outcount;
}

/*
================
R_SetupAndDrawSprite
================
*/
void R_SetupAndDrawSprite()
{
    int i, nump;
    float dot, scale, *pv;
    vec5_t* pverts;
    Vector3 left, up, right, down, transformed, local;
    emitpoint_t outverts[MAXWORKINGVERTS + 1], *pout;

    dot = r_spritedesc.vpn.dot(modelorg);

    // backface cull
    if (dot >= 0) {
        return;
    }

    // build the sprite poster in worldspace
    right = r_spritedesc.vright * r_spritedesc.pspriteframe->right;
    up = r_spritedesc.vup * r_spritedesc.pspriteframe->up;
    left = r_spritedesc.vright * r_spritedesc.pspriteframe->left;
    down = r_spritedesc.vup * r_spritedesc.pspriteframe->down;

    pverts = clip_verts[0];

    pverts[0][0] = r_entorigin.x + up.x + left.x;
    pverts[0][1] = r_entorigin.y + up.y + left.y;
    pverts[0][2] = r_entorigin.z + up.z + left.z;
    pverts[0][3] = 0;
    pverts[0][4] = 0;

    pverts[1][0] = r_entorigin.x + up.x + right.x;
    pverts[1][1] = r_entorigin.y + up.y + right.y;
    pverts[1][2] = r_entorigin.z + up.z + right.z;
    pverts[1][3] = static_cast<float>(sprite_width);
    pverts[1][4] = 0;

    pverts[2][0] = r_entorigin.x + down.x + right.x;
    pverts[2][1] = r_entorigin.y + down.y + right.y;
    pverts[2][2] = r_entorigin.z + down.z + right.z;
    pverts[2][3] = static_cast<float>(sprite_width);
    pverts[2][4] = static_cast<float>(sprite_height);

    pverts[3][0] = r_entorigin.x + down.x + left.x;
    pverts[3][1] = r_entorigin.y + down.y + left.y;
    pverts[3][2] = r_entorigin.z + down.z + left.z;
    pverts[3][3] = 0;
    pverts[3][4] = static_cast<float>(sprite_height);

    // clip to the frustum in worldspace
    nump = 4;
    clip_current = 0;

    for (i = 0; i < 4; i++) {
        nump = R_ClipSpriteFace(nump, &view_clipplanes[i]);
        if (nump < 3) {
            return;
        }

        if (nump >= MAXWORKINGVERTS) {
            Sys_Error("R_SetupAndDrawSprite: too many points");
        }
    }

    // transform vertices into viewspace and project
    pv = &clip_verts[clip_current][0][0];
    r_spritedesc.nearzi = -999999;

    for (i = 0; i < nump; i++) {
        local = Vector3(pv[0], pv[1], pv[2]) - r_origin;
        TransformVector(local, transformed);

        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }

        pout = &outverts[i];
        pout->zi = static_cast<float>(1.0 / transformed.z);
        if (pout->zi > r_spritedesc.nearzi) {
            r_spritedesc.nearzi = pout->zi;
        }

        pout->s = pv[3];
        pout->t = pv[4];

        scale = xscale * pout->zi;
        pout->u = (xcenter + scale * transformed.x);

        scale = yscale * pout->zi;
        pout->v = (ycenter - scale * transformed.y);

        pv += sizeof(vec5_t) / sizeof(*pv);
    }

    // draw it
    r_spritedesc.nump = nump;
    r_spritedesc.pverts = outverts;
    D_DrawSprite();
}

/*
================
R_GetSpriteframe
================
*/
mspriteframe_t* R_GetSpriteframe(msprite_t* psprite)
{
    mspritegroup_t* pspritegroup;
    mspriteframe_t* pspriteframe;
    int i, numframes, frame;
    float *pintervals, fullinterval, targettime, time;

    frame = currententity->frame;

    if ((frame >= psprite->numframes) || (frame < 0)) {
        Con_Printf("R_DrawSprite: no such frame %d\n", frame);
        frame = 0;
    }

    if (psprite->frames[frame].type == SPR_SINGLE) {
        pspriteframe = psprite->frames[frame].frameptr;
    } else {
        pspritegroup = (mspritegroup_t*)psprite->frames[frame].frameptr;
        pintervals = pspritegroup->intervals;
        numframes = pspritegroup->numframes;
        fullinterval = pintervals[numframes - 1];

        time = static_cast<float>(cl.time + currententity->syncbase);

        // when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
        // are positive, so we don't have to worry about division by 0
        targettime = time - ((int)(time / fullinterval)) * fullinterval;

        for (i = 0; i < (numframes - 1); i++) {
            if (pintervals[i] > targettime) {
                break;
            }
        }

        pspriteframe = pspritegroup->frames[i];
    }

    return pspriteframe;
}

/*
================
R_DrawSprite
================
*/
void R_DrawSprite(void)
{
    msprite_t* psprite;
    Vector3 tvec;
    float dot, angle, sr, cr;

    psprite = (msprite_t*)currententity->model->cache.data;

    r_spritedesc.pspriteframe = R_GetSpriteframe(psprite);

    sprite_width = r_spritedesc.pspriteframe->width;
    sprite_height = r_spritedesc.pspriteframe->height;

    // TODO: make this caller-selectable
    if (psprite->type == SPR_FACING_UPRIGHT) {
        // generate the sprite's axes, with vup straight up in worldspace, and
        // r_spritedesc.vright perpendicular to modelorg.
        // This will not work if the view direction is very close to straight up or
        // down, because the cross product will be between two nearly parallel
        // vectors and starts to approach an undefined state, so we don't draw if
        // the two vectors are less than 1 degree apart
        tvec = -modelorg;
        tvec.normalize();
        dot = tvec.z; // same as DotProduct (tvec, r_spritedesc.vup) because
        //  r_spritedesc.vup is 0, 0, 1
        if ((dot > 0.999848) || (dot < -0.999848)) { // cos(1 degree) = 0.999848
            return;
        }

        r_spritedesc.vup = Vector3(0, 0, 1);
        r_spritedesc.vright = Vector3(tvec.y, -tvec.x, 0);
        r_spritedesc.vright.normalize();
        r_spritedesc.vpn = Vector3(-r_spritedesc.vright.y, r_spritedesc.vright.x, 0);
        // CrossProduct (r_spritedesc.vright, r_spritedesc.vup,
        //  r_spritedesc.vpn)
    } else if (psprite->type == SPR_VP_PARALLEL) {
        // generate the sprite's axes, completely parallel to the viewplane. There
        // are no problem situations, because the sprite is always in the same
        // position relative to the viewer
        r_spritedesc.vup = vup;
        r_spritedesc.vright = vright;
        r_spritedesc.vpn = vpn;
    } else if (psprite->type == SPR_VP_PARALLEL_UPRIGHT) {
        // generate the sprite's axes, with vup straight up in worldspace, and
        // r_spritedesc.vright parallel to the viewplane.
        // This will not work if the view direction is very close to straight up or
        // down, because the cross product will be between two nearly parallel
        // vectors and starts to approach an undefined state, so we don't draw if
        // the two vectors are less than 1 degree apart
        dot = vpn.z; // same as DotProduct (vpn, r_spritedesc.vup) because
        //  r_spritedesc.vup is 0, 0, 1
        if ((dot > 0.999848) || (dot < -0.999848)) { // cos(1 degree) = 0.999848
            return;
        }

        r_spritedesc.vup = Vector3(0, 0, 1);
        r_spritedesc.vright = Vector3(vpn.y, -vpn.x, 0);
        r_spritedesc.vright.normalize();
        r_spritedesc.vpn = Vector3(-r_spritedesc.vright.y, r_spritedesc.vright.x, 0);
        // CrossProduct (r_spritedesc.vright, r_spritedesc.vup,
        //  r_spritedesc.vpn)
    } else if (psprite->type == SPR_ORIENTED) {
        // generate the sprite's axes, according to the sprite's world orientation
        AngleVectors(currententity->angles, r_spritedesc.vpn, r_spritedesc.vright,
            r_spritedesc.vup);
    } else if (psprite->type == SPR_VP_PARALLEL_ORIENTED) {
        // generate the sprite's axes, parallel to the viewplane, but rotated in
        // that plane around the center according to the sprite entity's roll
        // angle. So vpn stays the same, but vright and vup rotate
        angle = static_cast<float>(currententity->angles[ROLL] * (M_PI * 2 / 360));
        sr = sin(angle);
        cr = cos(angle);

        r_spritedesc.vpn = vpn;
        r_spritedesc.vright = vright * cr + vup * sr;
        r_spritedesc.vup = vright * -sr + vup * cr;
    } else {
        Sys_Error("R_DrawSprite: Bad sprite type %d", psprite->type);
    }

    R_RotateSprite(psprite->beamlength);

    R_SetupAndDrawSprite();
}


// ============================================================
// Content from: src\r_alias.cpp
// ============================================================
// r_alias.cpp: routines for setting up to draw alias models

// right now, but that should move)

#define LIGHT_MIN 5 // lowest light value we'll allow, to avoid the
//  need for inner-loop light clamping

affinetridesc_t r_affinetridesc;

void* acolormap; // FIXME: should go away

trivertx_t* r_apverts;

Vector3 r_plightvec;
int r_ambientlight;
float r_shadelight;
static float ziscale;
static model_t* pmodel;

static Vector3 alias_forward, alias_right, alias_up;

static maliasskindesc_t* pskindesc;
int r_anumverts;

float aliastransform[3][4];

typedef struct {
    int index0;
    int index1;
} aedge_t;

static aedge_t aedges[12] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
    { 6, 7 }, { 7, 4 }, { 0, 5 }, { 1, 4 }, { 2, 7 }, { 3, 6 } };

/*
================
R_InitVertexNormals

Generate the 162 vertex normals by subdividing an icosahedron.
This reproduces the exact set and ordering from the original
Quake Pascal tool used to create anorms.h.
================
*/
void R_InitVertexNormals(void)
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    const float X = 0.525731112119133606f;
    const float Z = 0.850650808352039932f;

    const float verts[12][3] = {
        {-X, 0.0f, Z}, {X, 0.0f, Z}, {-X, 0.0f, -Z}, {X, 0.0f, -Z},
        {0.0f, Z, X}, {0.0f, Z, -X}, {0.0f, -Z, X}, {0.0f, -Z, -X},
        {Z, X, 0.0f}, {-Z, X, 0.0f}, {Z, -X, 0.0f}, {-Z, -X, 0.0f},
    };

    const int faces[20][3] = {
        {0, 4, 1}, {0, 9, 4}, {9, 5, 4}, {4, 5, 8}, {4, 8, 1},
        {8, 10, 1}, {8, 3, 10}, {5, 3, 8}, {5, 2, 3}, {2, 7, 3},
        {7, 10, 3}, {7, 6, 10}, {7, 11, 6}, {11, 0, 6}, {0, 1, 6},
        {6, 1, 10}, {9, 0, 11}, {9, 11, 2}, {9, 2, 5}, {7, 2, 11}
    };

    const int subdiv = 4;
    const int max_low_i = subdiv / 2 - 1;

    float temp[400][3];
    int num_temp = 0;

    for (int f = 0; f < 20; f++) {
        const float* a = verts[faces[f][0]];
        const float* b = verts[faces[f][1]];
        const float* c = verts[faces[f][2]];

        // High i part (i >= n/2)
        for (int i = subdiv; i >= subdiv / 2; i--) {
            for (int j = subdiv - i; j >= 0; j--) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }

        // Low i part, high j (j >= n/2)
        for (int j = subdiv; j >= subdiv / 2; j--) {
            int max_i = (max_low_i < subdiv - j) ? max_low_i : (subdiv - j);
            for (int i = max_i; i >= 0; i--) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }

        // Low i part, low j (j < n/2)
        for (int j = 0; j < subdiv / 2; j++) {
            int max_i = (max_low_i < subdiv - j) ? max_low_i : (subdiv - j);
            for (int i = 0; i <= max_i; i++) {
                int k = subdiv - i - j;
                float px = (i * a[0] + j * b[0] + k * c[0]) / subdiv;
                float py = (i * a[1] + j * b[1] + k * c[1]) / subdiv;
                float pz = (i * a[2] + j * b[2] + k * c[2]) / subdiv;
                float len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 0) {
                    temp[num_temp][0] = px / len;
                    temp[num_temp][1] = py / len;
                    temp[num_temp][2] = pz / len;
                    num_temp++;
                }
            }
        }
    }

    // Dedup preserving first occurrence (order matches original)
    int out_idx = 0;
    for (int i = 0; i < num_temp && out_idx < NUMVERTEXNORMALS; i++) {
        int key_x = (int)std::round(temp[i][0] * 10000.0f);
        int key_y = (int)std::round(temp[i][1] * 10000.0f);
        int key_z = (int)std::round(temp[i][2] * 10000.0f);

        bool dup = false;
        for (int j = 0; j < out_idx; j++) {
            int jx = (int)std::round(r_avertexnormals[j][0] * 10000.0f);
            int jy = (int)std::round(r_avertexnormals[j][1] * 10000.0f);
            int jz = (int)std::round(r_avertexnormals[j][2] * 10000.0f);
            if (jx == key_x && jy == key_y && jz == key_z) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            r_avertexnormals[out_idx][0] = temp[i][0];
            r_avertexnormals[out_idx][1] = temp[i][1];
            r_avertexnormals[out_idx][2] = temp[i][2];
            out_idx++;
        }
    }
}

void R_AliasTransformAndProjectFinalVerts(finalvert_t* fv, stvert_t* pstverts);
void R_AliasSetUpTransform(int trivial_accept);
void R_AliasTransformVector(const float* in, float* out);
void R_AliasTransformFinalVert(finalvert_t* fv,
    auxvert_t* av,
    trivertx_t* pverts,
    stvert_t* pstverts);
void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av);

/*
================
R_AliasCheckBBox
================
*/
qboolean R_AliasCheckBBox(void)
{
    int i, flags, frame, numv;
    aliashdr_t* pahdr;
    float zi, basepts[8][3], v0, v1, frac;
    finalvert_t *pv0, *pv1, viewpts[16];
    auxvert_t *pa0, *pa1, viewaux[16];
    maliasframedesc_t* pframedesc;
    qboolean zclipped, zfullyclipped;
    unsigned anyclip, allclip;
    int minz;

    // expand, rotate, and translate points into worldspace

    currententity->trivial_accept = 0;
    pmodel = currententity->model;
    pahdr = (aliashdr_t*)Mod_Extradata(pmodel);
    pmdl = (mdl_t*)((byte*)pahdr + pahdr->model);

    R_AliasSetUpTransform(0);

    // construct the base bounding box for this frame
    frame = currententity->frame;
    // TODO: don't repeat this check when drawing?
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("No such frame %d %s\n", frame, pmodel->name);
        frame = 0;
    }

    pframedesc = &pahdr->frames[frame];

    // x worldspace coordinates
    basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] = (float)pframedesc->bboxmin.v[0];
    basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] = (float)pframedesc->bboxmax.v[0];

    // y worldspace coordinates
    basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] = (float)pframedesc->bboxmin.v[1];
    basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] = (float)pframedesc->bboxmax.v[1];

    // z worldspace coordinates
    basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] = (float)pframedesc->bboxmin.v[2];
    basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] = (float)pframedesc->bboxmax.v[2];

    zclipped = false;
    zfullyclipped = true;

    minz = 9999;
    for (i = 0; i < 8; i++) {
        R_AliasTransformVector(&basepts[i][0], &viewaux[i].fv[0]);

        if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE) {
            // we must clip points that are closer than the near clip plane
            viewpts[i].flags = ALIAS_Z_CLIP;
            zclipped = true;
        } else {
            if (viewaux[i].fv[2] < minz) {
                minz = static_cast<int>(viewaux[i].fv[2]);
            }

            viewpts[i].flags = 0;
            zfullyclipped = false;
        }
    }

    if (zfullyclipped) {
        return false; // everything was near-z-clipped
    }

    numv = 8;

    if (zclipped) {
        // organize points by edges, use edges to get new points (possible trivial
        // reject)
        for (i = 0; i < 12; i++) {
            // edge endpoints
            pv0 = &viewpts[aedges[i].index0];
            pv1 = &viewpts[aedges[i].index1];
            pa0 = &viewaux[aedges[i].index0];
            pa1 = &viewaux[aedges[i].index1];

            // if one end is clipped and the other isn't, make a new point
            if (pv0->flags ^ pv1->flags) {
                frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) / (pa1->fv[2] - pa0->fv[2]);
                viewaux[numv].fv[0] = pa0->fv[0] + (pa1->fv[0] - pa0->fv[0]) * frac;
                viewaux[numv].fv[1] = pa0->fv[1] + (pa1->fv[1] - pa0->fv[1]) * frac;
                viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
                viewpts[numv].flags = 0;
                numv++;
            }
        }
    }

    // project the vertices that remain after clipping
    anyclip = 0;
    allclip = ALIAS_XY_CLIP_MASK;

    // TODO: probably should do this loop in ASM, especially if we use floats
    for (i = 0; i < numv; i++) {
        // we don't need to bother with vertices that were z-clipped
        if (viewpts[i].flags & ALIAS_Z_CLIP) {
            continue;
        }

        zi = static_cast<float>(1.0 / viewaux[i].fv[2]);

        // FIXME: do with chop mode in ASM, or convert to float
        v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
        v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;

        flags = 0;

        if (v0 < r_refdef.fvrectx) {
            flags |= ALIAS_LEFT_CLIP;
        }

        if (v1 < r_refdef.fvrecty) {
            flags |= ALIAS_TOP_CLIP;
        }

        if (v0 > r_refdef.fvrectright) {
            flags |= ALIAS_RIGHT_CLIP;
        }

        if (v1 > r_refdef.fvrectbottom) {
            flags |= ALIAS_BOTTOM_CLIP;
        }

        anyclip |= flags;
        allclip &= flags;
    }

    if (allclip) {
        return false; // trivial reject off one side
    }

    currententity->trivial_accept = !anyclip & !zclipped;

    if (currententity->trivial_accept) {
        if (minz > (r_aliastransition + (pmdl->size * r_resfudge))) {
            currententity->trivial_accept |= 2;
        }
    }

    return true;
}

/*
================
R_AliasTransformVector
================
*/
void R_AliasTransformVector(const float* in, float* out)
{
    out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
    out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
    out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}

/*
================
R_AliasPreparePoints

General clipped case
================
*/
void R_AliasPreparePoints(void)
{
    int i;
    stvert_t* pstverts;
    finalvert_t* fv;
    auxvert_t* av;
    mtriangle_t* ptri;
    finalvert_t* paclip_fv[3];

    pstverts = (stvert_t*)((byte*)paliashdr + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    fv = pfinalverts;
    av = pauxverts;

    for (i = 0; i < r_anumverts; i++, fv++, av++, r_apverts++, pstverts++) {
        R_AliasTransformFinalVert(fv, av, r_apverts, pstverts);
        if (av->fv[2] < ALIAS_Z_CLIP_PLANE) {
            fv->flags |= ALIAS_Z_CLIP;
        } else {
            R_AliasProjectFinalVert(fv, av);

            if (fv->v[0] < r_refdef.aliasvrect.x) {
                fv->flags |= ALIAS_LEFT_CLIP;
            }

            if (fv->v[1] < r_refdef.aliasvrect.y) {
                fv->flags |= ALIAS_TOP_CLIP;
            }

            if (fv->v[0] > r_refdef.aliasvrectright) {
                fv->flags |= ALIAS_RIGHT_CLIP;
            }

            if (fv->v[1] > r_refdef.aliasvrectbottom) {
                fv->flags |= ALIAS_BOTTOM_CLIP;
            }
        }
    }

    //
    // clip and draw all triangles
    //
    r_affinetridesc.numtriangles = 1;

    ptri = (mtriangle_t*)((byte*)paliashdr + paliashdr->triangles);
    for (i = 0; i < pmdl->numtris; i++, ptri++) {
        paclip_fv[0] = &pfinalverts[ptri->vertindex[0]];
        paclip_fv[1] = &pfinalverts[ptri->vertindex[1]];
        paclip_fv[2] = &pfinalverts[ptri->vertindex[2]];

        if (paclip_fv[0]->flags & paclip_fv[1]->flags & paclip_fv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP)) {
            continue; // completely clipped
        }

        if (!((paclip_fv[0]->flags | paclip_fv[1]->flags | paclip_fv[2]->flags) & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP))) { // totally unclipped
            r_affinetridesc.pfinalverts = pfinalverts;
            r_affinetridesc.ptriangles = ptri;
            D_PolysetDraw();
        } else { // partially clipped
            R_AliasClipTriangle(ptri);
        }
    }
}

/*
================
R_AliasSetUpTransform
================
*/
void R_AliasSetUpTransform(int trivial_accept)
{
    int i;
    float rotationmatrix[3][4], t2matrix[3][4];
    static float tmatrix[3][4];
    static float viewmatrix[3][4];
    Vector3 angles;

    // TODO: should really be stored with the entity instead of being reconstructed
    // TODO: should use a look-up table
    // TODO: could cache lazily, stored in the entity

    angles[ROLL] = currententity->angles[ROLL];
    angles[PITCH] = -currententity->angles[PITCH];
    angles[YAW] = currententity->angles[YAW];
    AngleVectors(angles, alias_forward, alias_right, alias_up);

    tmatrix[0][0] = pmdl->scale[0];
    tmatrix[1][1] = pmdl->scale[1];
    tmatrix[2][2] = pmdl->scale[2];

    tmatrix[0][3] = pmdl->scale_origin[0];
    tmatrix[1][3] = pmdl->scale_origin[1];
    tmatrix[2][3] = pmdl->scale_origin[2];

    // TODO: can do this with simple matrix rearrangement

    for (i = 0; i < 3; i++) {
        t2matrix[i][0] = alias_forward[i];
        t2matrix[i][1] = -alias_right[i];
        t2matrix[i][2] = alias_up[i];
    }

    t2matrix[0][3] = -modelorg[0];
    t2matrix[1][3] = -modelorg[1];
    t2matrix[2][3] = -modelorg[2];

    // FIXME: can do more efficiently than full concatenation
    R_ConcatTransforms(t2matrix, tmatrix, rotationmatrix);

    // TODO: should be global, set when vright, etc., set
    VectorCopy(vright, viewmatrix[0]);
    VectorCopy(vup, viewmatrix[1]);
    VectorInverse(viewmatrix[1]);
    VectorCopy(vpn, viewmatrix[2]);

    //	viewmatrix[0][3] = 0;
    //	viewmatrix[1][3] = 0;
    //	viewmatrix[2][3] = 0;

    R_ConcatTransforms(viewmatrix, rotationmatrix, aliastransform);

    // do the scaling up of x and y to screen coordinates as part of the transform
    // for the unclipped case (it would mess up clipping in the clipped case).
    // Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
    // correspondingly so the projected x and y come out right
    // FIXME: make this work for clipped case too?
    if (trivial_accept) {
        for (i = 0; i < 4; i++) {
            aliastransform[0][i] *= aliasxscale * static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
            aliastransform[1][i] *= aliasyscale * static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
            aliastransform[2][i] *= static_cast<float>(1.0 / ((float)0x8000 * 0x10000));
        }
    }
}

/*
================
R_AliasTransformFinalVert
================
*/
void R_AliasTransformFinalVert(finalvert_t* fv,
    auxvert_t* av,
    trivertx_t* pverts,
    stvert_t* pstverts)
{
    int temp;
    float lightcos, *plightnormal;

    av->fv[0] = DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3];
    av->fv[1] = DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3];
    av->fv[2] = DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3];

    fv->v[2] = pstverts->s;
    fv->v[3] = pstverts->t;

    fv->flags = pstverts->onseam;

    // lighting
    plightnormal = r_avertexnormals[pverts->lightnormalindex];
    lightcos = DotProduct(plightnormal, r_plightvec);
    temp = r_ambientlight;

    if (lightcos < 0) {
        temp += (int)(r_shadelight * lightcos);

        // clamp; because we limited the minimum ambient and shading light, we
        // don't have to clamp low light, just bright
        if (temp < 0) {
            temp = 0;
        }
    }

    fv->v[4] = temp;
}

/*
================
R_AliasTransformAndProjectFinalVerts
================
*/
void R_AliasTransformAndProjectFinalVerts(finalvert_t* fv, stvert_t* pstverts)
{
    int i, temp;
    float lightcos, *plightnormal, zi;
    trivertx_t* pverts;

    pverts = r_apverts;

    for (i = 0; i < r_anumverts; i++, fv++, pverts++, pstverts++) {
        // transform and project
        zi = static_cast<float>(1.0 / (DotProduct(pverts->v, aliastransform[2]) + aliastransform[2][3]));

        // x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
        // scaled up by 1/2**31, and the scaling cancels out for x and y in the
        // projection
        fv->v[5] = static_cast<int>(zi);

        fv->v[0] = static_cast<int>(((DotProduct(pverts->v, aliastransform[0]) + aliastransform[0][3]) * zi) + aliasxcenter);
        fv->v[1] = static_cast<int>(((DotProduct(pverts->v, aliastransform[1]) + aliastransform[1][3]) * zi) + aliasycenter);

        fv->v[2] = pstverts->s;
        fv->v[3] = pstverts->t;
        fv->flags = pstverts->onseam;

        // lighting
        plightnormal = r_avertexnormals[pverts->lightnormalindex];
        lightcos = DotProduct(plightnormal, r_plightvec);
        temp = r_ambientlight;

        if (lightcos < 0) {
            temp += (int)(r_shadelight * lightcos);

            // clamp; because we limited the minimum ambient and shading light, we
            // don't have to clamp low light, just bright
            if (temp < 0) {
                temp = 0;
            }
        }

        fv->v[4] = temp;
    }
}

/*
================
R_AliasProjectFinalVert
================
*/
void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av)
{
    float zi;

    // project points
    zi = static_cast<float>(1.0 / av->fv[2]);

    fv->v[5] = static_cast<int>(zi * ziscale);

    fv->v[0] = static_cast<int>((av->fv[0] * aliasxscale * zi) + aliasxcenter);
    fv->v[1] = static_cast<int>((av->fv[1] * aliasyscale * zi) + aliasycenter);
}

/*
================
R_AliasPrepareUnclippedPoints
================
*/
void R_AliasPrepareUnclippedPoints(void)
{
    stvert_t* pstverts;
    finalvert_t* fv;

    pstverts = (stvert_t*)((byte*)paliashdr + paliashdr->stverts);
    r_anumverts = pmdl->numverts;
    // FIXME: just use pfinalverts directly?
    fv = pfinalverts;

    R_AliasTransformAndProjectFinalVerts(fv, pstverts);

    if (r_affinetridesc.drawtype) {
        D_PolysetDrawFinalVerts(fv, r_anumverts);
    }

    r_affinetridesc.pfinalverts = pfinalverts;
    r_affinetridesc.ptriangles = (mtriangle_t*)((byte*)paliashdr + paliashdr->triangles);
    r_affinetridesc.numtriangles = pmdl->numtris;

    D_PolysetDraw();
}

/*
===============
R_AliasSetupSkin
===============
*/
void R_AliasSetupSkin(void)
{
    int skinnum;
    int i, numskins;
    maliasskingroup_t* paliasskingroup;
    float *pskinintervals, fullskininterval;
    float skintargettime, skintime;

    skinnum = currententity->skinnum;
    if ((skinnum >= pmdl->numskins) || (skinnum < 0)) {
        Con_DPrintf("R_AliasSetupSkin: no such skin # %d\n", skinnum);
        skinnum = 0;
    }

    pskindesc = ((maliasskindesc_t*)((byte*)paliashdr + paliashdr->skindesc)) + skinnum;
    a_skinwidth = pmdl->skinwidth;

    if (pskindesc->type == ALIAS_SKIN_GROUP) {
        paliasskingroup = (maliasskingroup_t*)((byte*)paliashdr + pskindesc->skin);
        pskinintervals = (float*)((byte*)paliashdr + paliasskingroup->intervals);
        numskins = paliasskingroup->numskins;
        fullskininterval = pskinintervals[numskins - 1];

        skintime = static_cast<float>(cl.time + currententity->syncbase);

        // when loading in Mod_LoadAliasSkinGroup, we guaranteed all interval
        // values are positive, so we don't have to worry about division by 0
        skintargettime = skintime - ((int)(skintime / fullskininterval)) * fullskininterval;

        for (i = 0; i < (numskins - 1); i++) {
            if (pskinintervals[i] > skintargettime) {
                break;
            }
        }

        pskindesc = &paliasskingroup->skindescs[i];
    }

    r_affinetridesc.pskindesc = pskindesc;
    r_affinetridesc.pskin = (void*)((byte*)paliashdr + pskindesc->skin);
    r_affinetridesc.skinwidth = a_skinwidth;
    r_affinetridesc.seamfixupX16 = (a_skinwidth >> 1) << 16;
    r_affinetridesc.skinheight = pmdl->skinheight;
}

/*
================
R_AliasSetupLighting
================
*/
void R_AliasSetupLighting(alight_t* plighting)
{
    // guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't have
    // to clamp off the bottom
    r_ambientlight = plighting->ambientlight;

    if (r_ambientlight < LIGHT_MIN) {
        r_ambientlight = LIGHT_MIN;
    }

    r_ambientlight = (255 - r_ambientlight) << VID_CBITS;

    if (r_ambientlight < LIGHT_MIN) {
        r_ambientlight = LIGHT_MIN;
    }

    r_shadelight = static_cast<float>(plighting->shadelight);

    if (r_shadelight < 0) {
        r_shadelight = 0;
    }

    r_shadelight *= VID_GRADES;

    // rotate the lighting vector into the model's frame of reference
    r_plightvec[0] = DotProduct(plighting->plightvec, alias_forward);
    r_plightvec[1] = -DotProduct(plighting->plightvec, alias_right);
    r_plightvec[2] = DotProduct(plighting->plightvec, alias_up);
}

/*
=================
R_AliasSetupFrame

set r_apverts
=================
*/
void R_AliasSetupFrame(void)
{
    int frame;
    int i, numframes;
    maliasgroup_t* paliasgroup;
    float *pintervals, fullinterval, targettime, time;

    frame = currententity->frame;
    if ((frame >= pmdl->numframes) || (frame < 0)) {
        Con_DPrintf("R_AliasSetupFrame: no such frame %d\n", frame);
        frame = 0;
    }

    if (paliashdr->frames[frame].type == ALIAS_SINGLE) {
        r_apverts = (trivertx_t*)((byte*)paliashdr + paliashdr->frames[frame].frame);

        return;
    }

    paliasgroup = (maliasgroup_t*)((byte*)paliashdr + paliashdr->frames[frame].frame);
    pintervals = (float*)((byte*)paliashdr + paliasgroup->intervals);
    numframes = paliasgroup->numframes;
    fullinterval = pintervals[numframes - 1];

    time = static_cast<float>(cl.time + currententity->syncbase);

    //
    // when loading in Mod_LoadAliasGroup, we guaranteed all interval values
    // are positive, so we don't have to worry about division by 0
    //
    targettime = time - ((int)(time / fullinterval)) * fullinterval;

    for (i = 0; i < (numframes - 1); i++) {
        if (pintervals[i] > targettime) {
            break;
        }
    }

    r_apverts = (trivertx_t*)((byte*)paliashdr + paliasgroup->frames[i].frame);
}

/*
================
R_AliasDrawModel
================
*/
void R_AliasDrawModel(alight_t* plighting)
{
    finalvert_t
        finalverts[MAXALIASVERTS + ((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 1];
    auxvert_t auxverts[MAXALIASVERTS];

    r_amodels_drawn++;

    // cache align
    pfinalverts = (finalvert_t*)(((size_t)&finalverts[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    pauxverts = &auxverts[0];

    paliashdr = (aliashdr_t*)Mod_Extradata(currententity->model);
    pmdl = (mdl_t*)((byte*)paliashdr + paliashdr->model);

    R_AliasSetupSkin();
    R_AliasSetUpTransform(currententity->trivial_accept);
    R_AliasSetupLighting(plighting);
    R_AliasSetupFrame();

    if (!currententity->colormap) {
        Sys_Error("R_AliasDrawModel: !currententity->colormap");
    }

    r_affinetridesc.drawtype = (currententity->trivial_accept == 3) && r_recursiveaffinetriangles;

    if (r_affinetridesc.drawtype) {
        D_PolysetUpdateTables(); // FIXME: precalc...
    }

    acolormap = currententity->colormap;

    if (currententity != &cl.viewent) {
        ziscale = (float)0x8000 * (float)0x10000;
    } else {
        ziscale = (float)0x8000 * (float)0x10000 * 3.0;
    }

    if (currententity->trivial_accept) {
        R_AliasPrepareUnclippedPoints();
    } else {
        R_AliasPreparePoints();
    }
}


// ============================================================
// Content from: src\r_aclip.cpp
// ============================================================
// r_aclip.cpp: clip routines for drawing Alias models directly to the screen


static finalvert_t aclip_fv[2][8];
static auxvert_t aclip_av[8];

void R_AliasProjectFinalVert(finalvert_t* fv, auxvert_t* av);
void R_Alias_clip_top(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out);
void R_Alias_clip_bottom(finalvert_t* pfv0,
    finalvert_t* pfv1,
    finalvert_t* out);
void R_Alias_clip_left(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out);
void R_Alias_clip_right(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out);

/*
================
R_Alias_clip_z

pfv0 is the unclipped vertex, pfv1 is the z-clipped vertex
================
*/
void R_Alias_clip_z(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    auxvert_t *pav0, *pav1, avout;

    pav0 = &aclip_av[pfv0 - &aclip_fv[0][0]];
    pav1 = &aclip_av[pfv1 - &aclip_fv[0][0]];

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = (ALIAS_Z_CLIP_PLANE - pav0->fv[2]) / (pav1->fv[2] - pav0->fv[2]);

        avout.fv[0] = pav0->fv[0] + (pav1->fv[0] - pav0->fv[0]) * scale;
        avout.fv[1] = pav0->fv[1] + (pav1->fv[1] - pav0->fv[1]) * scale;
        avout.fv[2] = ALIAS_Z_CLIP_PLANE;

        out->v[2] = static_cast<int>(pfv0->v[2] + (pfv1->v[2] - pfv0->v[2]) * scale);
        out->v[3] = static_cast<int>(pfv0->v[3] + (pfv1->v[3] - pfv0->v[3]) * scale);
        out->v[4] = static_cast<int>(pfv0->v[4] + (pfv1->v[4] - pfv0->v[4]) * scale);
    } else {
        scale = (ALIAS_Z_CLIP_PLANE - pav1->fv[2]) / (pav0->fv[2] - pav1->fv[2]);

        avout.fv[0] = pav1->fv[0] + (pav0->fv[0] - pav1->fv[0]) * scale;
        avout.fv[1] = pav1->fv[1] + (pav0->fv[1] - pav1->fv[1]) * scale;
        avout.fv[2] = ALIAS_Z_CLIP_PLANE;

        out->v[2] = static_cast<int>(pfv1->v[2] + (pfv0->v[2] - pfv1->v[2]) * scale);
        out->v[3] = static_cast<int>(pfv1->v[3] + (pfv0->v[3] - pfv1->v[3]) * scale);
        out->v[4] = static_cast<int>(pfv1->v[4] + (pfv0->v[4] - pfv1->v[4]) * scale);
    }

    R_AliasProjectFinalVert(out, &avout);

    if (out->v[0] < r_refdef.aliasvrect.x) {
        out->flags |= ALIAS_LEFT_CLIP;
    }

    if (out->v[1] < r_refdef.aliasvrect.y) {
        out->flags |= ALIAS_TOP_CLIP;
    }

    if (out->v[0] > r_refdef.aliasvrectright) {
        out->flags |= ALIAS_RIGHT_CLIP;
    }

    if (out->v[1] > r_refdef.aliasvrectbottom) {
        out->flags |= ALIAS_BOTTOM_CLIP;
    }
}

void R_Alias_clip_left(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrect.x - pfv0->v[0]) / (pfv1->v[0] - pfv0->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrect.x - pfv1->v[0]) / (pfv0->v[0] - pfv1->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

void R_Alias_clip_right(finalvert_t* pfv0,
    finalvert_t* pfv1,
    finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrectright - pfv0->v[0]) / (pfv1->v[0] - pfv0->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrectright - pfv1->v[0]) / (pfv0->v[0] - pfv1->v[0]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

void R_Alias_clip_top(finalvert_t* pfv0, finalvert_t* pfv1, finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrect.y - pfv0->v[1]) / (pfv1->v[1] - pfv0->v[1]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrect.y - pfv1->v[1]) / (pfv0->v[1] - pfv1->v[1]);
        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

void R_Alias_clip_bottom(finalvert_t* pfv0,
    finalvert_t* pfv1,
    finalvert_t* out)
{
    float scale;
    int i;

    if (pfv0->v[1] >= pfv1->v[1]) {
        scale = static_cast<float>(r_refdef.aliasvrectbottom - pfv0->v[1]) / (pfv1->v[1] - pfv0->v[1]);

        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv0->v[i] + (pfv1->v[i] - pfv0->v[i]) * scale + 0.5);
        }
    } else {
        scale = static_cast<float>(r_refdef.aliasvrectbottom - pfv1->v[1]) / (pfv0->v[1] - pfv1->v[1]);

        for (i = 0; i < 6; i++) {
            out->v[i] = static_cast<int>(pfv1->v[i] + (pfv0->v[i] - pfv1->v[i]) * scale + 0.5);
        }
    }
}

int R_AliasClip(finalvert_t* in,
    finalvert_t* out,
    int flag,
    int count,
    void (*clip)(finalvert_t* pfv0,
        finalvert_t* pfv1,
        finalvert_t* out))
{
    int i, j, k;
    int flags, oldflags;

    j = count - 1;
    k = 0;
    for (i = 0; i < count; j = i, i++) {
        oldflags = in[j].flags & flag;
        flags = in[i].flags & flag;

        if (flags && oldflags) {
            continue;
        }

        if (oldflags ^ flags) {
            clip(&in[j], &in[i], &out[k]);
            out[k].flags = 0;
            if (out[k].v[0] < r_refdef.aliasvrect.x) {
                out[k].flags |= ALIAS_LEFT_CLIP;
            }

            if (out[k].v[1] < r_refdef.aliasvrect.y) {
                out[k].flags |= ALIAS_TOP_CLIP;
            }

            if (out[k].v[0] > r_refdef.aliasvrectright) {
                out[k].flags |= ALIAS_RIGHT_CLIP;
            }

            if (out[k].v[1] > r_refdef.aliasvrectbottom) {
                out[k].flags |= ALIAS_BOTTOM_CLIP;
            }

            k++;
        }

        if (!flags) {
            out[k] = in[i];
            k++;
        }
    }

    return k;
}

/*
================
R_AliasClipTriangle
================
*/
void R_AliasClipTriangle(mtriangle_t* ptri)
{
    int i, k, pingpong;
    mtriangle_t mtri;
    unsigned clipflags;

    // copy vertexes and fix seam texture coordinates
    if (ptri->facesfront) {
        aclip_fv[0][0] = pfinalverts[ptri->vertindex[0]];
        aclip_fv[0][1] = pfinalverts[ptri->vertindex[1]];
        aclip_fv[0][2] = pfinalverts[ptri->vertindex[2]];
    } else {
        for (i = 0; i < 3; i++) {
            aclip_fv[0][i] = pfinalverts[ptri->vertindex[i]];

            if (!ptri->facesfront && (aclip_fv[0][i].flags & ALIAS_ONSEAM)) {
                aclip_fv[0][i].v[2] += r_affinetridesc.seamfixupX16;
            }
        }
    }

    // clip
    clipflags = aclip_fv[0][0].flags | aclip_fv[0][1].flags | aclip_fv[0][2].flags;

    if (clipflags & ALIAS_Z_CLIP) {
        for (i = 0; i < 3; i++) {
            aclip_av[i] = pauxverts[ptri->vertindex[i]];
        }

        k = R_AliasClip(aclip_fv[0], aclip_fv[1], ALIAS_Z_CLIP, 3, R_Alias_clip_z);
        if (k == 0) {
            return;
        }

        pingpong = 1;
        clipflags = aclip_fv[1][0].flags | aclip_fv[1][1].flags | aclip_fv[1][2].flags;
    } else {
        pingpong = 0;
        k = 3;
    }

    if (clipflags & ALIAS_LEFT_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_LEFT_CLIP, k,
            R_Alias_clip_left);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    if (clipflags & ALIAS_RIGHT_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_RIGHT_CLIP, k,
            R_Alias_clip_right);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    if (clipflags & ALIAS_BOTTOM_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_BOTTOM_CLIP, k,
            R_Alias_clip_bottom);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    if (clipflags & ALIAS_TOP_CLIP) {
        k = R_AliasClip(aclip_fv[pingpong], aclip_fv[pingpong ^ 1], ALIAS_TOP_CLIP, k,
            R_Alias_clip_top);
        if (k == 0) {
            return;
        }

        pingpong ^= 1;
    }

    for (i = 0; i < k; i++) {
        if (aclip_fv[pingpong][i].v[0] < r_refdef.aliasvrect.x) {
            aclip_fv[pingpong][i].v[0] = r_refdef.aliasvrect.x;
        } else if (aclip_fv[pingpong][i].v[0] > r_refdef.aliasvrectright) {
            aclip_fv[pingpong][i].v[0] = r_refdef.aliasvrectright;
        }

        if (aclip_fv[pingpong][i].v[1] < r_refdef.aliasvrect.y) {
            aclip_fv[pingpong][i].v[1] = r_refdef.aliasvrect.y;
        } else if (aclip_fv[pingpong][i].v[1] > r_refdef.aliasvrectbottom) {
            aclip_fv[pingpong][i].v[1] = r_refdef.aliasvrectbottom;
        }

        aclip_fv[pingpong][i].flags = 0;
    }

    // draw triangles
    mtri.facesfront = ptri->facesfront;
    r_affinetridesc.ptriangles = &mtri;
    r_affinetridesc.pfinalverts = aclip_fv[pingpong];

    // FIXME: do all at once as trifan?
    mtri.vertindex[0] = 0;
    for (i = 1; i < k - 1; i++) {
        mtri.vertindex[1] = i;
        mtri.vertindex[2] = i + 1;
        D_PolysetDraw();
    }
}


// ============================================================
// Content from: src\r_main.cpp
// ============================================================


//define	PASSAGES

void* colormap;
Vector3 viewlightvec;
alight_t r_viewlighting = { 128, 192, viewlightvec };
qboolean r_drawpolys;
qboolean r_drawculledpolys;
qboolean r_worldpolysbacktofront;
qboolean r_recursiveaffinetriangles = true;
int r_pixbytes = 1;
float r_aliasuvscale = 1.0;

qboolean r_dowarp;

int c_surf;

byte* r_warpbuffer;

byte* r_stack_start;

//
// view origin
//
Vector3 vup, base_vup;
Vector3 vpn, base_vpn;
Vector3 vright, base_vright;
Vector3 r_origin;

//
// screen size info
//
refdef_t r_refdef;
float xcenter, ycenter;
float xscale, yscale;
float xscaleinv, yscaleinv;
float xscaleshrink, yscaleshrink;
int screenwidth;

float pixelAspect;

//
// refresh flags
//
int r_framecount = 1; // so frame counts initialized to 0 don't match
int d_spanpixcount;
int r_drawnpolycount;

#define VIEWMODNAME_LENGTH 256
char viewmodname[VIEWMODNAME_LENGTH + 1];
int modcount;



texture_t* r_notexture_mip;

int d_lightstylevalue[256]; // 8.8 fraction of base light value

void R_MarkLeaves(void);

cvar_t r_clearcolor = { "r_clearcolor", "2", {}, {}, {}, {} };
cvar_t r_drawviewmodel = { "r_drawviewmodel", "1", {}, {}, {}, {} };
cvar_t r_drawflat = { "r_drawflat", "0", {}, {}, {}, {} };
cvar_t r_aliastransbase = { "r_aliastransbase", "200", {}, {}, {}, {} };
cvar_t r_aliastransadj = { "r_aliastransadj", "100", {}, {}, {}, {} };

void CreatePassages(void);
void SetVisibilityByPassages(void);

/*
==================
R_InitTextures
==================
*/
void R_InitTextures(void)
{
    int x, y, m;
    byte* dest;

    // create a simple checkerboard texture for the default
    r_notexture_mip = (texture_t*) Hunk_Alloc(
        sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2, "notexture");

    r_notexture_mip->width = r_notexture_mip->height = 16;
    r_notexture_mip->offsets[0] = sizeof(texture_t);
    r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
    r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
    r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;

    for (m = 0; m < 4; m++) {
        dest = (byte*)r_notexture_mip + r_notexture_mip->offsets[m];
        for (y = 0; y < (16 >> m); y++) {
            for (x = 0; x < (16 >> m); x++) {
                if ((y < (8 >> m)) ^ (x < (8 >> m))) {
                    *dest++ = 0;
                } else {
                    *dest++ = 0xff;
                }
            }
        }
    }
}

/*
===============
R_Init
===============
*/
void R_Init(void)
{
    int dummy;

    // get stack position so we can guess if we are going to overflow
    r_stack_start = (byte*)&dummy;

    R_InitTurb();
    R_InitVertexNormals();

    Cmd::AddCommand("timerefresh", R_TimeRefresh_f);
    Cmd::AddCommand("pointfile", R_ReadPointFile_f);

    Cvar::Register(&r_draworder);
    Cvar::Register(&r_speeds);
    Cvar::Register(&r_timegraph);
    Cvar::Register(&r_graphheight);
    Cvar::Register(&r_drawflat);
    Cvar::Register(&r_ambient);
    Cvar::Register(&r_clearcolor);
    Cvar::Register(&r_waterwarp);
    Cvar::Register(&r_fullbright);
    Cvar::Register(&r_drawentities);
    Cvar::Register(&r_drawviewmodel);
    Cvar::Register(&r_aliasstats);
    Cvar::Register(&r_dspeeds);
    Cvar::Register(&r_reportsurfout);
    Cvar::Register(&r_maxsurfs);
    Cvar::Register(&r_numsurfs);
    Cvar::Register(&r_reportedgeout);
    Cvar::Register(&r_maxedges);
    Cvar::Register(&r_numedges);
    Cvar::Register(&r_aliastransbase);
    Cvar::Register(&r_aliastransadj);

    Cvar::SetValue("r_maxedges", (float)NUMSTACKEDGES);
    Cvar::SetValue("r_maxsurfs", (float)NUMSTACKSURFACES);

    view_clipplanes[0].leftedge = true;
    view_clipplanes[1].rightedge = true;
    view_clipplanes[1].leftedge = view_clipplanes[2].leftedge = view_clipplanes[3].leftedge = false;
    view_clipplanes[0].rightedge = view_clipplanes[2].rightedge = view_clipplanes[3].rightedge = false;

    r_refdef.xOrigin = XCENTERING;
    r_refdef.yOrigin = YCENTERING;

    R_InitParticles();
    D_Init();
}

/*
===============
R_NewMap
===============
*/
void R_NewMap(void)
{
    int i;

    // clear out efrags in case the level hasn't been reloaded
    // FIXME: is this one short?
    for (i = 0; i < cl.worldmodel->numleafs; i++) {
        cl.worldmodel->leafs[i].efrags = NULL;
    }

    r_viewleaf = NULL;
    R_ClearParticles();

    r_cnumsurfs = static_cast<int>(r_maxsurfs.value);

    if (r_cnumsurfs <= MINSURFACES) {
        r_cnumsurfs = MINSURFACES;
    }

    if (r_cnumsurfs > NUMSTACKSURFACES) {
        surfaces = (surf_t *) Hunk_Alloc(r_cnumsurfs * sizeof(surf_t), "surfaces");
        surface_p = surfaces;
        surf_max = &surfaces[r_cnumsurfs];
        r_surfsonstack = false;
        // surface 0 doesn't really exist; it's just a dummy because index 0
        // is used to indicate no edge attached to surface
        surfaces--;
    } else {
        r_surfsonstack = true;
    }

    r_maxedgesseen = 0;
    r_maxsurfsseen = 0;

    r_numallocatededges = static_cast<int>(r_maxedges.value);

    if (r_numallocatededges < MINEDGES) {
        r_numallocatededges = MINEDGES;
    }

    if (r_numallocatededges <= NUMSTACKEDGES) {
        auxedges = NULL;
    } else {
        auxedges = (edge_t *) Hunk_Alloc(r_numallocatededges * sizeof(edge_t), "edges");
    }

    r_dowarpold = false;
    r_viewchanged = false;
}

/*
===============
R_SetVrect
===============
*/
void R_SetVrect(vrect_t* pvrectin, vrect_t* pvrect, int lineadj)
{
    int h;
    float size;

    size = scr_viewsize.value > 100 ? 100 : scr_viewsize.value;
    if (cl.intermission) {
        size = 100;
        lineadj = 0;
    }

    size /= 100;

    h = pvrectin->height - lineadj;
    pvrect->width = static_cast<int>(pvrectin->width * size);
    if (pvrect->width < 96) {
        size = static_cast<float>(96.0 / pvrectin->width);
        pvrect->width = 96; // min for icons
    }

    pvrect->width &= ~7;
    pvrect->height = static_cast<int>(pvrectin->height * size);
    if (pvrect->height > pvrectin->height - lineadj) {
        pvrect->height = pvrectin->height - lineadj;
    }

    pvrect->height &= ~1;

    pvrect->x = (pvrectin->width - pvrect->width) / 2;
    pvrect->y = (h - pvrect->height) / 2;

    {
        if (lcd_x.value) {
            pvrect->y >>= 1;
            pvrect->height >>= 1;
        }
    }
}

/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
void R_ViewChanged(vrect_t* pvrect, int lineadj, float aspect)
{
    int i;
    float res_scale;

    r_viewchanged = true;

    R_SetVrect(pvrect, &r_refdef.vrect, lineadj);

    r_refdef.horizontalFieldOfView = static_cast<float>(2.0 * tan(r_refdef.fov_x / 360 * M_PI));
    r_refdef.fvrectx = static_cast<float>(r_refdef.vrect.x);
    r_refdef.fvrectx_adj = static_cast<float>(r_refdef.vrect.x) - 0.5f;
    r_refdef.vrect_x_adj_shift20 = ((int64_t)r_refdef.vrect.x << 20) + (1 << 19) - 1;
    r_refdef.fvrecty = static_cast<float>(r_refdef.vrect.y);
    r_refdef.fvrecty_adj = static_cast<float>(r_refdef.vrect.y) - 0.5f;
    r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
    r_refdef.vrectright_adj_shift20 = ((int64_t)r_refdef.vrectright << 20) + (1 << 19) - 1;
    r_refdef.fvrectright = static_cast<float>(r_refdef.vrectright);
    r_refdef.fvrectright_adj = static_cast<float>(r_refdef.vrectright) - 0.5f;
    r_refdef.vrectrightedge = static_cast<float>(r_refdef.vrectright) - 0.99f;
    r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
    r_refdef.fvrectbottom = static_cast<float>(r_refdef.vrectbottom);
    r_refdef.fvrectbottom_adj = static_cast<float>(r_refdef.vrectbottom) - 0.5f;

    r_refdef.aliasvrect.x = (int)(r_refdef.vrect.x * r_aliasuvscale);
    r_refdef.aliasvrect.y = (int)(r_refdef.vrect.y * r_aliasuvscale);
    r_refdef.aliasvrect.width = (int)(r_refdef.vrect.width * r_aliasuvscale);
    r_refdef.aliasvrect.height = (int)(r_refdef.vrect.height * r_aliasuvscale);
    r_refdef.aliasvrectright = r_refdef.aliasvrect.x + r_refdef.aliasvrect.width;
    r_refdef.aliasvrectbottom = r_refdef.aliasvrect.y + r_refdef.aliasvrect.height;

    pixelAspect = aspect;
    xOrigin = r_refdef.xOrigin;
    yOrigin = r_refdef.yOrigin;

    screenAspect = r_refdef.vrect.width * pixelAspect / r_refdef.vrect.height;
    // 320*200 1.0 pixelAspect = 1.6 screenAspect
    // 320*240 1.0 pixelAspect = 1.3333 screenAspect
    // proper 320*200 pixelAspect = 0.8333333

    verticalFieldOfView = r_refdef.horizontalFieldOfView / screenAspect;

    // values for perspective projection
    // if math were exact, the values would range from 0.5 to to range+0.5
    // hopefully they wll be in the 0.000001 to range+.999999 and truncate
    // the polygon rasterization will never render in the first row or column
    // but will definately render in the [range] row and column, so adjust the
    // buffer origin to get an exact edge to edge fill
    xcenter = (static_cast<float>(r_refdef.vrect.width) * static_cast<float>(XCENTERING)) + static_cast<float>(r_refdef.vrect.x) - 0.5f;
    aliasxcenter = xcenter * r_aliasuvscale;
    ycenter = (static_cast<float>(r_refdef.vrect.height) * static_cast<float>(YCENTERING)) + static_cast<float>(r_refdef.vrect.y) - 0.5f;
    aliasycenter = ycenter * r_aliasuvscale;

    xscale = static_cast<float>(r_refdef.vrect.width) / r_refdef.horizontalFieldOfView;
    aliasxscale = xscale * r_aliasuvscale;
    xscaleinv = 1.0f / xscale;
    yscale = xscale * pixelAspect;
    aliasyscale = yscale * r_aliasuvscale;
    yscaleinv = 1.0f / yscale;
    xscaleshrink = static_cast<float>(r_refdef.vrect.width - 6) / r_refdef.horizontalFieldOfView;
    yscaleshrink = xscaleshrink * pixelAspect;

    // left side clip
    screenedge[0].normal[0] = static_cast<float>(-1.0 / (xOrigin * r_refdef.horizontalFieldOfView));
    screenedge[0].normal[1] = 0;
    screenedge[0].normal[2] = 1;
    screenedge[0].type = PLANE_ANYZ;

    // right side clip
    screenedge[1].normal[0] = static_cast<float>(1.0 / ((1.0 - xOrigin) * r_refdef.horizontalFieldOfView));
    screenedge[1].normal[1] = 0;
    screenedge[1].normal[2] = 1;
    screenedge[1].type = PLANE_ANYZ;

    // top side clip
    screenedge[2].normal[0] = 0;
    screenedge[2].normal[1] = static_cast<float>(-1.0 / (yOrigin * verticalFieldOfView));
    screenedge[2].normal[2] = 1;
    screenedge[2].type = PLANE_ANYZ;

    // bottom side clip
    screenedge[3].normal[0] = 0;
    screenedge[3].normal[1] = static_cast<float>(1.0 / ((1.0 - yOrigin) * verticalFieldOfView));
    screenedge[3].normal[2] = 1;
    screenedge[3].type = PLANE_ANYZ;

    for (i = 0; i < 4; i++) {
        VectorNormalize(screenedge[i].normal);
    }

    res_scale = static_cast<float>(sqrt(static_cast<double>(r_refdef.vrect.width * r_refdef.vrect.height) / (320.0 * 152.0)) * (2.0 / r_refdef.horizontalFieldOfView));
    r_aliastransition = r_aliastransbase.value * res_scale;
    r_resfudge = r_aliastransadj.value * res_scale;

    if (scr_fov.value <= 90.0) {
        r_fov_greater_than_90 = false;
    } else {
        r_fov_greater_than_90 = true;
    }

    D_ViewChanged();
}

/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves(void)
{
    byte* vis;
    mnode_t* node;
    int i;

    if (r_oldviewleaf == r_viewleaf) {
        return;
    }

    r_visframecount++;
    r_oldviewleaf = r_viewleaf;

    vis = Mod_LeafPVS(r_viewleaf, cl.worldmodel);

    for (i = 0; i < cl.worldmodel->numleafs; i++) {
        if (vis[i >> 3] & (1 << (i & 7))) {
            node = (mnode_t*)&cl.worldmodel->leafs[i + 1];
            do {
                if (node->visframe == r_visframecount) {
                    break;
                }

                node->visframe = r_visframecount;
                node = node->parent;
            } while (node);
        }
    }
}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList(void)
{
    int i, j;
    int lnum;
    alight_t lighting;
    // FIXME: remove and do real lighting
    float lightvec[3] = { -1, 0, 0 };
    Vector3 dist;
    float add;

    if (!r_drawentities.value) {
        return;
    }

    for (i = 0; i < cl_numvisedicts; i++) {
        currententity = cl_visedicts[i];

        if (currententity == &cl_entities[cl.viewentity]) {
            continue; // don't draw the player
        }

        switch (currententity->model->type) {
        case mod_sprite:
            r_entorigin = currententity->origin;
            modelorg = r_origin - r_entorigin;
            R_DrawSprite();
            break;

        case mod_alias:
            r_entorigin = currententity->origin;
            modelorg = r_origin - r_entorigin;

            // see if the bounding box lets us trivially reject, also sets
            // trivial accept status
            if (R_AliasCheckBBox()) {
                j = R_LightPoint(currententity->origin);

                lighting.ambientlight = j;
                lighting.shadelight = j;

                lighting.plightvec = lightvec;

                for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
                    if (cl_dlights[lnum].die >= cl.time) {
                        dist = currententity->origin - cl_dlights[lnum].origin;
                        add = cl_dlights[lnum].radius - dist.length();

                        if (add > 0) {
                            lighting.ambientlight += static_cast<int>(add);
                        }
                    }
                }

                // clamp lighting so it doesn't overbright as much
                if (lighting.ambientlight > 128) {
                    lighting.ambientlight = 128;
                }

                if (lighting.ambientlight + lighting.shadelight > 192) {
                    lighting.shadelight = 192 - lighting.ambientlight;
                }

                R_AliasDrawModel(&lighting);
            }

            break;

        default:
            break;
        }
    }
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel(void)
{
    // FIXME: remove and do real lighting
    float lightvec[3] = { -1, 0, 0 };
    int j;
    int lnum;
    Vector3 dist;
    float add;
    dlight_t* dl;

    if (!r_drawviewmodel.value || r_fov_greater_than_90) {
        return;
    }

    if (cl.items & IT_INVISIBILITY) {
        return;
    }

    if (cl.stats[STAT_HEALTH] <= 0) {
        return;
    }

    currententity = &cl.viewent;
    if (!currententity->model) {
        return;
    }

    r_entorigin = currententity->origin;
    modelorg = r_origin - r_entorigin;

    viewlightvec = -vup;

    j = R_LightPoint(currententity->origin);

    if (j < 24) {
        j = 24; // allways give some light on gun
    }

    r_viewlighting.ambientlight = j;
    r_viewlighting.shadelight = j;

    // add dynamic lights
    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
        dl = &cl_dlights[lnum];
        if (!dl->radius) {
            continue;
        }

        if (!dl->radius) {
            continue;
        }

        if (dl->die < cl.time) {
            continue;
        }

        dist = currententity->origin - dl->origin;
        add = dl->radius - dist.length();
        if (add > 0) {
            r_viewlighting.ambientlight += static_cast<int>(add);
        }
    }

    // clamp lighting so it doesn't overbright as much
    if (r_viewlighting.ambientlight > 128) {
        r_viewlighting.ambientlight = 128;
    }

    if (r_viewlighting.ambientlight + r_viewlighting.shadelight > 192) {
        r_viewlighting.shadelight = 192 - r_viewlighting.ambientlight;
    }

    r_viewlighting.plightvec = lightvec;


    R_AliasDrawModel(&r_viewlighting);
}

/*
=============
R_BmodelCheckBBox
=============
*/
int R_BmodelCheckBBox(model_t* clmodel, float* minmaxs)
{
    int i, *pindex, clipflags;
    Vector3 acceptpt, rejectpt;
    double d;

    clipflags = 0;

    if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2]) {
        for (i = 0; i < 4; i++) {
            d = currententity->origin.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= -clmodel->radius) {
                return BMODEL_FULLY_CLIPPED;
            }

            if (d <= clmodel->radius) {
                clipflags |= (1 << i);
            }
        }
    } else {
        for (i = 0; i < 4; i++) {
            // generate accept and reject points
            // FIXME: do with fast look-ups or integer tests based on the sign bit
            // of the floating point values

            pindex = pfrustum_indexes[i];

            rejectpt = Vector3(minmaxs[pindex[0]], minmaxs[pindex[1]], minmaxs[pindex[2]]);

            d = rejectpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0) {
                return BMODEL_FULLY_CLIPPED;
            }

            acceptpt = Vector3(minmaxs[pindex[3 + 0]], minmaxs[pindex[3 + 1]], minmaxs[pindex[3 + 2]]);

            d = acceptpt.dot(view_clipplanes[i].normal);
            d -= view_clipplanes[i].dist;

            if (d <= 0) {
                clipflags |= (1 << i);
            }
        }
    }

    return clipflags;
}

/*
=============
R_DrawBEntitiesOnList
=============
*/
void R_DrawBEntitiesOnList(void)
{
    int i, k, clipflags;
    Vector3 oldorigin;
    model_t* clmodel;
    float minmaxs[6];

    if (!r_drawentities.value) {
        return;
    }

    oldorigin = modelorg;
    insubmodel = true;
    r_dlightframecount = r_framecount;

    for (i = 0; i < cl_numvisedicts; i++) {
        currententity = cl_visedicts[i];

        switch (currententity->model->type) {
        case mod_brush:

            clmodel = currententity->model;

            // see if the bounding box lets us trivially reject, also sets
            // trivial accept status
            minmaxs[0] = currententity->origin.x + clmodel->mins[0];
            minmaxs[1] = currententity->origin.y + clmodel->mins[1];
            minmaxs[2] = currententity->origin.z + clmodel->mins[2];
            minmaxs[3] = currententity->origin.x + clmodel->maxs[0];
            minmaxs[4] = currententity->origin.y + clmodel->maxs[1];
            minmaxs[5] = currententity->origin.z + clmodel->maxs[2];

            clipflags = R_BmodelCheckBBox(clmodel, minmaxs);

            if (clipflags != BMODEL_FULLY_CLIPPED) {
                r_entorigin = currententity->origin;
                modelorg = r_origin - r_entorigin;
                // FIXME: is this needed?
                r_worldmodelorg = modelorg;

                r_pcurrentvertbase = clmodel->vertexes;

                // FIXME: stop transforming twice
                R_RotateBmodel();

                // calculate dynamic lighting for bmodel if it's not an
                // instanced model
                if (clmodel->firstmodelsurface != 0) {
                    for (k = 0; k < MAX_DLIGHTS; k++) {
                        if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius)) {
                            continue;
                        }

                        R_MarkLights(&cl_dlights[k], 1 << k,
                            clmodel->nodes + clmodel->hulls[0].firstclipnode);
                    }
                }

                // if the driver wants polygons, deliver those. Z-buffering is on
                // at this point, so no clipping to the world tree is needed, just
                // frustum clipping
                if (r_drawpolys | r_drawculledpolys) {
                    R_ZDrawSubmodelPolys(clmodel);
                } else {
                    r_pefragtopnode = NULL;

                    r_emins = Vector3(minmaxs[0], minmaxs[1], minmaxs[2]);
                    r_emaxs = Vector3(minmaxs[3], minmaxs[4], minmaxs[5]);

                    R_SplitEntityOnNode2(cl.worldmodel->nodes);

                    if (r_pefragtopnode) {
                        currententity->topnode = r_pefragtopnode;

                        if (r_pefragtopnode->contents >= 0) {
                            // not a leaf; has to be clipped to the world BSP
                            r_clipflags = clipflags;
                            R_DrawSolidClippedSubmodelPolygons(clmodel);
                        } else {
                            // falls entirely in one leaf, so we just put all the
                            // edges in the edge list and let 1/z sorting handle
                            // drawing order
                            R_DrawSubmodelPolygons(clmodel, clipflags);
                        }

                        currententity->topnode = NULL;
                    }
                }

                // put back world rotation and frustum clipping
                // FIXME: R_RotateBmodel should just work off base_vxx
                vpn = base_vpn;
                vup = base_vup;
                vright = base_vright;
                modelorg = base_modelorg;
                modelorg = oldorigin;
                R_TransformFrustum();
            }

            break;

        default:
            break;
        }
    }

    insubmodel = false;
}

/*
================
R_EdgeDrawing
================
*/
void R_EdgeDrawing(void)
{
    edge_t ledges[NUMSTACKEDGES + ((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];
    surf_t lsurfs[NUMSTACKSURFACES + ((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];

    if (auxedges) {
        r_edges = auxedges;
    } else {
        r_edges = (edge_t*)(((size_t)&ledges[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    }

    if (r_surfsonstack) {
        surfaces = (surf_t*)(((size_t)&lsurfs[0] + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
        surf_max = &surfaces[r_cnumsurfs];
        // surface 0 doesn't really exist; it's just a dummy because index 0
        // is used to indicate no edge attached to surface
        surfaces--;
    }

    R_BeginEdgeFrame();

    if (r_dspeeds.value) {
        rw_time1 = static_cast<float>(Sys_FloatTime());
    }

    R_RenderWorld();

    if (r_drawculledpolys) {
        R_ScanEdges();
    }

    // only the world can be drawn back to front with no z reads or compares, just
    // z writes, so have the driver turn z compares on now
    D_TurnZOn();

    if (r_dspeeds.value) {
        rw_time2 = static_cast<float>(Sys_FloatTime());
        db_time1 = rw_time2;
    }

    R_DrawBEntitiesOnList();

    if (r_dspeeds.value) {
        db_time2 = static_cast<float>(Sys_FloatTime());
        se_time1 = db_time2;
    }

    if (!r_dspeeds.value) {
        VID_UnlockBuffer();
        S_ExtraUpdate(); // don't let sound get messed up if going slow
        VID_LockBuffer();
    }

    if (!(r_drawpolys | r_drawculledpolys)) {
        R_ScanEdges();
    }
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView_(void)
{
    byte warpbuffer[WARP_WIDTH * WARP_HEIGHT];

    r_warpbuffer = warpbuffer;

    if (r_timegraph.value || r_speeds.value || r_dspeeds.value) {
        r_time1 = static_cast<float>(Sys_FloatTime());
    }

    R_SetupFrame();

    R_MarkLeaves(); // done here so we know if we're in water

    // make FDIV fast. This reduces timing precision after we've been running for a
    // while, so we don't do it globally.  This also sets chop mode, and we do it
    // here so that setup stuff like the refresh area calculations match what's
    // done in screen.cpp
    Sys_LowFPPrecision();

    if (!cl_entities[0].model || !cl.worldmodel) {
        Sys_Error("R_RenderView: NULL worldmodel");
    }

    if (!r_dspeeds.value) {
        VID_UnlockBuffer();
        S_ExtraUpdate(); // don't let sound get messed up if going slow
        VID_LockBuffer();
    }

    R_EdgeDrawing();

    if (!r_dspeeds.value) {
        VID_UnlockBuffer();
        S_ExtraUpdate(); // don't let sound get messed up if going slow
        VID_LockBuffer();
    }

    if (r_dspeeds.value) {
        se_time2 = static_cast<float>(Sys_FloatTime());
        de_time1 = se_time2;
    }

    R_DrawEntitiesOnList();

    if (r_dspeeds.value) {
        de_time2 = static_cast<float>(Sys_FloatTime());
        dv_time1 = de_time2;
    }

    R_DrawViewModel();

    if (r_dspeeds.value) {
        dv_time2 = static_cast<float>(Sys_FloatTime());
        dp_time1 = static_cast<float>(Sys_FloatTime());
    }

    R_DrawParticles();

    if (r_dspeeds.value) {
        dp_time2 = static_cast<float>(Sys_FloatTime());
    }

    if (r_dowarp) {
        D_WarpScreen();
    }

    V_SetContentsColor(r_viewleaf->contents);

    if (r_timegraph.value) {
        R_TimeGraph();
    }

    if (r_aliasstats.value) {
        R_PrintAliasStats();
    }

    if (r_speeds.value) {
        R_PrintTimes();
    }

    if (r_dspeeds.value) {
        R_PrintDSpeeds();
    }

    if (r_reportsurfout.value && r_outofsurfaces) {
        Con_Printf("Short %d surfaces\n", r_outofsurfaces);
    }

    if (r_reportedgeout.value && r_outofedges) {
        Con_Printf("Short roughly %d edges\n", r_outofedges * 2 / 3);
    }

    // back to high floating-point precision
    Sys_HighFPPrecision();
}

void R_RenderView(void)
{
    int dummy;
    int delta;

    delta = static_cast<int>((byte*)&dummy - r_stack_start);
    if (delta < -10000 || delta > 10000) {
        Sys_Error("R_RenderView: called without enough stack");
    }

    if (Hunk_LowMark() & 3) {
        Sys_Error("Hunk is missaligned");
    }

    if ((size_t)(&dummy) & 3) {
        Sys_Error("Stack is missaligned");
    }

    if ((size_t)(&r_warpbuffer) & 3) {
        Sys_Error("Globals are missaligned");
    }

    R_RenderView_();
}

/*
================
R_InitTurb
================
*/
void R_InitTurb(void)
{
    int i;

    for (i = 0; i < (SIN_BUFFER_SIZE); i++) {
        sintable[i] = static_cast<int>(AMP + sin(i * 3.14159 * 2 / CYCLE) * AMP);
        intsintable[i] = static_cast<int>(AMP2 + sin(i * 3.14159 * 2 / CYCLE) * AMP2); // AMP2, not 20
    }
}

} // namespace Render

