// sw_light.cpp -- BSP Lighting, Style Animation, and Dynamic Lights
#include "render/software/sw_light.hpp"
#include "client/client_types.hpp"
#include "world/bsp_format.hpp"

namespace Render {

std::array<int, 256> d_lightstylevalue{};
int r_dlightframecount = 0;

void R_AnimateLight()
{
    // light animations
    // 'm' is normal light, 'a' is no light, 'z' is double bright
    int i = static_cast<int>(Client::cl.time * 10);
    for (int j = 0; j < MAX_LIGHTSTYLES; j++) {
        if (!Client::cl_lightstyle[j].length) {
            d_lightstylevalue[j] = 256;
            continue;
        }
        int k = i % Client::cl_lightstyle[j].length;
        k = Client::cl_lightstyle[j].map[k] - 'a';
        k = k * 22;
        d_lightstylevalue[j] = k;
    }
}

void R_MarkLights(dlight_t* light, int bit, mnode_t* node)
{
    if (node->contents < 0) {
        return;
    }
    mplane_t* splitplane = node->plane;
    float dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;
    if (dist > light->radius) {
        R_MarkLights(light, bit, node->children[0]);
        return;
    }
    if (dist < -light->radius) {
        R_MarkLights(light, bit, node->children[1]);
        return;
    }
    // mark the polygons
    msurface_t* surf = Client::cl.worldmodel->surfaces + node->firstsurface;
    for (int i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->dlightframe != r_dlightframecount) {
            surf->dlightbits = 0;
            surf->dlightframe = r_dlightframecount;
        }
        surf->dlightbits |= bit;
    }
    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}

void R_PushDlights()
{
    r_dlightframecount = r_framecount + 1; // because the count hasn't advanced yet for this frame
    dlight_t* l = Client::cl_dlights;
    for (int i = 0; i < MAX_DLIGHTS; i++, l++) {
        if (l->die < Client::cl.time || !l->radius) {
            continue;
        }
        R_MarkLights(l, 1 << i, Client::cl.worldmodel->nodes);
    }
}

int RecursiveLightPoint(mnode_t* node, const Vector3& start, const Vector3& end)
{
    if (node->contents < 0) {
        return -1; // didn't hit anything
    }
    mplane_t* plane = node->plane;
    float front = start.dot(plane->normal) - plane->dist;
    float back = end.dot(plane->normal) - plane->dist;
    int side = front < 0;
    if ((back < 0) == side) {
        return RecursiveLightPoint(node->children[side], start, end);
    }
    float frac = front / (front - back);
    Vector3 mid = start + (end - start) * frac;
    // go down front side
    int r = RecursiveLightPoint(node->children[side], start, mid);
    if (r >= 0) {
        return r; // hit something
    }
    if ((back < 0) == side) {
        return -1; // didn't hit anything
    }
    // check for impact on this node
    msurface_t* surf = Client::cl.worldmodel->surfaces + node->firstsurface;
    for (int i = 0; i < node->numsurfaces; i++, surf++) {
        if (surf->flags & SURF_DRAWTILED) {
            continue; // no lightmaps
        }
        mtexinfo_t* tex = surf->texinfo;
        int s = static_cast<int>(mid.dot(tex->vecs[0]) + tex->vecs[0][3]);
        int t = static_cast<int>(mid.dot(tex->vecs[1]) + tex->vecs[1][3]);
        if (s < surf->texturemins[0] || t < surf->texturemins[1]) {
            continue;
        }
        int ds = s - surf->texturemins[0];
        int dt = t - surf->texturemins[1];
        if (ds > surf->extents[0] || dt > surf->extents[1]) {
            continue;
        }
        if (!surf->samples) {
            return 0;
        }
        ds >>= 4;
        dt >>= 4;
        byte* lightmap = surf->samples;
        r = 0;
        if (lightmap) {
            lightmap += dt * ((surf->extents[0] >> 4) + 1) + ds;
            for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
                unsigned scale = d_lightstylevalue[surf->styles[maps]];
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
    if (!Client::cl.worldmodel->lightdata) {
        return 255;
    }
    Vector3 end = p - Vector3(0.0f, 0.0f, 2048.0f);
    int r = RecursiveLightPoint(Client::cl.worldmodel->nodes, p, end);
    if (r == -1) {
        r = 0;
    }
    if (r < r_refdef.ambientlight) {
        r = r_refdef.ambientlight;
    }
    return r;
}

} // namespace Render
