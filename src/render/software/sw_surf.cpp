// sw_surf.cpp -- Surface Rasterization, Lightmaps, Dynamic Lights, and Surface Cache
#include "render/software/sw_surf.hpp"
#include "client/client_types.hpp"
#include "world/bsp_format.hpp"
#include "core/cmd.hpp"
#include "core/cvar.hpp"
#include "ui/console.hpp"

#include <cmath>
#include <cstddef>

using namespace Math;
using namespace Common;
using namespace Console;
using namespace Client;

namespace Render {

drawsurf_t r_drawsurf;

static int lightleft, blocksize, sourcetstep;
static int lightright, lightleftstep, lightrightstep, blockdivshift;
static void* prowdestbase;
static unsigned char* pbasesource;
static int surfrowbytes;
static unsigned* r_lightptr;
static int r_stepback;
static int r_lightwidth;
static int r_numhblocks, r_numvblocks;
static unsigned char *r_source, *r_sourcemax;

static void (*surfmiptable[4])(void) = {
    R_DrawSurfaceBlock8_mip0, R_DrawSurfaceBlock8_mip1,
    R_DrawSurfaceBlock8_mip2, R_DrawSurfaceBlock8_mip3
};

static unsigned blocklights[18 * 18];

constexpr int GUARDSIZE = 4;
int sc_size = 0;
surfcache_t* sc_rover = nullptr;
surfcache_t* sc_base = nullptr;
surfcache_t* d_initial_rover = nullptr;
qboolean d_roverwrapped = false;
float surfscale = 0.0f;
int c_surf = 0;

void R_AddDynamicLights()
{
    msurface_t* surf = r_drawsurf.surf;
    int smax = (surf->extents[0] >> 4) + 1;
    int tmax = (surf->extents[1] >> 4) + 1;
    mtexinfo_t* tex = surf->texinfo;
    for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
        if (!(surf->dlightbits & (1 << lnum))) {
            continue; // not lit by this light
        }
        float rad = cl_dlights[lnum].radius;
        float dist = cl_dlights[lnum].origin.dot(surf->plane->normal) - surf->plane->dist;
        rad -= std::fabs(dist);
        float minlight = cl_dlights[lnum].minlight;
        if (rad < minlight) {
            continue;
        }
        minlight = rad - minlight;
        Vector3 impact = cl_dlights[lnum].origin - surf->plane->normal * dist;
        Vector3 local;
        local.x = impact.dot(tex->vecs[0]) + tex->vecs[0][3];
        local.y = impact.dot(tex->vecs[1]) + tex->vecs[1][3];
        local.x -= surf->texturemins[0];
        local.y -= surf->texturemins[1];
        for (int t = 0; t < tmax; t++) {
            int td = static_cast<int>(local.y - t * 16);
            if (td < 0) {
                td = -td;
            }
            for (int s = 0; s < smax; s++) {
                int sd = static_cast<int>(local.x - s * 16);
                if (sd < 0) {
                    sd = -sd;
                }
                if (sd > td) {
                    dist = static_cast<float>(sd + (td >> 1));
                } else {
                    dist = static_cast<float>(td + (sd >> 1));
                }
                if (dist < minlight) {
                    blocklights[t * smax + s] += static_cast<unsigned int>((rad - dist) * 256);
                }
            }
        }
    }
}

void R_BuildLightMap()
{
    msurface_t* surf = r_drawsurf.surf;
    int smax = (surf->extents[0] >> 4) + 1;
    int tmax = (surf->extents[1] >> 4) + 1;
    int size = smax * tmax;
    byte* lightmap = surf->samples;
    if (r_fullbright.value || !cl.worldmodel->lightdata) {
        for (int i = 0; i < size; i++) {
            blocklights[i] = 0;
        }
        return;
    }
    // clear to ambient
    for (int i = 0; i < size; i++) {
        blocklights[i] = r_refdef.ambientlight << 8;
    }
    // add all the lightmaps
    if (lightmap) {
        for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++) {
            unsigned scale = r_drawsurf.lightadj[maps]; // 8.8 fraction
            for (int i = 0; i < size; i++) {
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
    for (int i = 0; i < size; i++) {
        int t = (255 * 256 - (int)blocklights[i]) >> (8 - VID_CBITS);
        if (t < (1 << 6)) {
            t = (1 << 6);
        }
        blocklights[i] = t;
    }
}

texture_t* R_TextureAnimation(texture_t* base)
{
    if (currententity->frame) {
        if (base->alternate_anims) {
            base = base->alternate_anims;
        }
    }
    if (!base->anim_total) {
        return base;
    }
    int reletive = (int)(cl.time * 10) % base->anim_total;
    int count = 0;
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

template<int Shift>
static inline void R_DrawSurfaceBlock8_mip_T()
{
    constexpr int BlockCount = 1 << Shift;
    const auto* psource = reinterpret_cast<const unsigned char*>(pbasesource);
    auto* prowdest = reinterpret_cast<unsigned char*>(prowdestbase);
    const auto* colormap = reinterpret_cast<const unsigned char*>(Vid::vid.colormap);
    for (int v = 0; v < r_numvblocks; v++) {
        lightleft = r_lightptr[0];
        lightright = r_lightptr[1];
        r_lightptr += r_lightwidth;
        lightleftstep = (r_lightptr[0] - lightleft) >> Shift;
        lightrightstep = (r_lightptr[1] - lightright) >> Shift;
        for (int i = 0; i < BlockCount; i++) {
            const int lightstep = (lightleft - lightright) >> Shift;
            int light = lightright;
            for (int b = BlockCount - 1; b >= 0; b--) {
                prowdest[b] = colormap[(light & 0xFF00) + psource[b]];
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

void R_DrawSurfaceBlock8_mip0() { R_DrawSurfaceBlock8_mip_T<4>(); }
void R_DrawSurfaceBlock8_mip1() { R_DrawSurfaceBlock8_mip_T<3>(); }
void R_DrawSurfaceBlock8_mip2() { R_DrawSurfaceBlock8_mip_T<2>(); }
void R_DrawSurfaceBlock8_mip3() { R_DrawSurfaceBlock8_mip_T<1>(); }

void R_DrawSurface()
{
    R_BuildLightMap();
    surfrowbytes = r_drawsurf.rowbytes;
    texture_t* mt = r_drawsurf.texture;
    r_source = reinterpret_cast<byte*>(mt) + mt->offsets[r_drawsurf.surfmip];
    int texwidth = mt->width >> r_drawsurf.surfmip;
    blocksize = 16 >> r_drawsurf.surfmip;
    blockdivshift = 4 - r_drawsurf.surfmip;
    r_lightwidth = (r_drawsurf.surf->extents[0] >> 4) + 1;
    r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
    r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

    void (*pblockdrawer)(void) = surfmiptable[r_drawsurf.surfmip];
    int horzblockstep = blocksize;
    int smax = mt->width >> r_drawsurf.surfmip;
    int twidth = texwidth;
    int tmax = mt->height >> r_drawsurf.surfmip;
    sourcetstep = texwidth;
    r_stepback = tmax * twidth;
    r_sourcemax = r_source + (tmax * smax);
    int soffset = r_drawsurf.surf->texturemins[0];
    int basetoffset = r_drawsurf.surf->texturemins[1];
    soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
    unsigned char* basetptr = &r_source[
        (((basetoffset >> r_drawsurf.surfmip) + (tmax << 16)) % tmax) * twidth];
    unsigned char* pcolumndest = r_drawsurf.surfdat;
    for (int u = 0; u < r_numhblocks; u++) {
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

int D_SurfaceCacheForRes(int width, int height)
{
    if (int pnum = COM_CheckParm("-surfcachesize")) {
        return Q_atoi(com_argv[pnum + 1]) * 1024;
    }
    int size = SURFCACHE_SIZE_AT_320X200;
    int pix = width * height;
    if (pix > 64000) {
        size += (pix - 64000) * 3;
    }
    return size;
}

void D_CheckCacheGuard()
{
    byte* s = reinterpret_cast<byte*>(sc_base) + sc_size;
    for (int i = 0; i < GUARDSIZE; i++) {
        if (s[i] != static_cast<byte>(i)) {
            Sys_Error("D_CheckCacheGuard: failed");
        }
    }
}

void D_ClearCacheGuard()
{
    byte* s = reinterpret_cast<byte*>(sc_base) + sc_size;
    for (int i = 0; i < GUARDSIZE; i++) {
        s[i] = static_cast<byte>(i);
    }
}

void D_InitCaches(void* buffer, int size)
{
    if (!msg_suppress_1) {
        Con_Printf("%ik surface cache\n", size / 1024);
    }
    sc_size = size - GUARDSIZE;
    sc_base = reinterpret_cast<surfcache_t*>(buffer);
    sc_rover = sc_base;
    sc_base->next = nullptr;
    sc_base->owner = nullptr;
    sc_base->size = sc_size;
    D_ClearCacheGuard();
}

void D_FlushCaches()
{
    if (!sc_base) {
        return;
    }
    for (surfcache_t* c = sc_base; c; c = c->next) {
        if (c->owner) {
            *c->owner = nullptr;
        }
    }
    sc_rover = sc_base;
    sc_base->next = nullptr;
    sc_base->owner = nullptr;
    sc_base->size = sc_size;
}

surfcache_t* D_SCAlloc(int width, int size)
{
    if ((width < 0) || (width > 256)) {
        Sys_Error("D_SCAlloc: bad cache width %d\n", width);
    }
    if ((size <= 0) || (size > 0x10000)) {
        Sys_Error("D_SCAlloc: bad cache size %d\n", size);
    }
    size = static_cast<int>(offsetof(surfcache_t, data) + size);
    size = (size + 3) & ~3;
    if (size > sc_size) {
        Sys_Error("D_SCAlloc: %i > cache size", size);
    }
    qboolean wrapped_this_time = false;
    if (!sc_rover || reinterpret_cast<byte*>(sc_rover) - reinterpret_cast<byte*>(sc_base) > sc_size - size) {
        if (sc_rover) {
            wrapped_this_time = true;
        }
        sc_rover = sc_base;
    }
    surfcache_t* new_surf = sc_rover;
    if (sc_rover->owner) {
        *sc_rover->owner = nullptr;
    }
    while (new_surf->size < size) {
        sc_rover = sc_rover->next;
        if (!sc_rover) {
            Sys_Error("D_SCAlloc: hit the end of memory");
        }
        if (sc_rover->owner) {
            *sc_rover->owner = nullptr;
        }
        new_surf->size += sc_rover->size;
        new_surf->next = sc_rover->next;
    }
    if (new_surf->size - size > 256) {
        sc_rover = reinterpret_cast<surfcache_t*>(reinterpret_cast<byte*>(new_surf) + size);
        sc_rover->size = new_surf->size - size;
        sc_rover->next = new_surf->next;
        sc_rover->width = 0;
        sc_rover->owner = nullptr;
        new_surf->next = sc_rover;
        new_surf->size = size;
    } else {
        sc_rover = new_surf->next;
    }
    new_surf->width = width;
    if (width > 0) {
        new_surf->height = (size - sizeof(*new_surf) + sizeof(new_surf->data)) / width;
    }
    new_surf->owner = nullptr;
    if (d_roverwrapped) {
        if (wrapped_this_time || (sc_rover >= d_initial_rover)) {
            r_cache_thrash = true;
        }
    } else if (wrapped_this_time) {
        d_roverwrapped = true;
    }
    D_CheckCacheGuard();
    return new_surf;
}

surfcache_t* D_CacheSurface(msurface_t* surface, int mip_level)
{
    r_drawsurf.texture = R_TextureAnimation(surface->texinfo->texture);
    r_drawsurf.lightadj[0] = d_lightstylevalue[surface->styles[0]];
    r_drawsurf.lightadj[1] = d_lightstylevalue[surface->styles[1]];
    r_drawsurf.lightadj[2] = d_lightstylevalue[surface->styles[2]];
    r_drawsurf.lightadj[3] = d_lightstylevalue[surface->styles[3]];
    surfcache_t* cache = surface->cachespots[mip_level];
    if (cache && !cache->dlight && surface->dlightframe != r_framecount &&
        cache->texture == r_drawsurf.texture &&
        cache->lightadj[0] == r_drawsurf.lightadj[0] &&
        cache->lightadj[1] == r_drawsurf.lightadj[1] &&
        cache->lightadj[2] == r_drawsurf.lightadj[2] &&
        cache->lightadj[3] == r_drawsurf.lightadj[3]) {
        return cache;
    }
    surfscale = 1.0f / (1 << mip_level);
    r_drawsurf.surfmip = mip_level;
    r_drawsurf.surfwidth = surface->extents[0] >> mip_level;
    r_drawsurf.rowbytes = r_drawsurf.surfwidth;
    r_drawsurf.surfheight = surface->extents[1] >> mip_level;
    if (!cache) {
        cache = D_SCAlloc(r_drawsurf.surfwidth,
            r_drawsurf.surfwidth * r_drawsurf.surfheight);
        surface->cachespots[mip_level] = cache;
        cache->owner = &surface->cachespots[mip_level];
        cache->mipscale = surfscale;
    }
    if (surface->dlightframe == r_framecount) {
        cache->dlight = 1;
    } else {
        cache->dlight = 0;
    }
    r_drawsurf.surfdat = reinterpret_cast<pixel_t*>(cache->data);
    cache->texture = r_drawsurf.texture;
    cache->lightadj[0] = r_drawsurf.lightadj[0];
    cache->lightadj[1] = r_drawsurf.lightadj[1];
    cache->lightadj[2] = r_drawsurf.lightadj[2];
    cache->lightadj[3] = r_drawsurf.lightadj[3];
    r_drawsurf.surf = surface;
    c_surf++;
    R_DrawSurface();
    return surface->cachespots[mip_level];
}

} // namespace Render
