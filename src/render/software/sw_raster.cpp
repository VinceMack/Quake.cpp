// sw_raster.cpp -- Software Span and Surface Rasterization
#include "render/software/sw_raster.hpp"
#include "render/software/sw_sky.hpp"
#include "render/software/sw_warp.hpp"
#include "render/software/sw_surf.hpp"
#include "render/software/sw_bsp.hpp"
#include "client/client_types.hpp"
#include "core/cvar.hpp"

namespace Render {

short* d_pzbuffer = nullptr;
unsigned int d_zrowbytes = 0;
unsigned int d_zwidth = 0;

constexpr int NUM_MIPS = 4;
cvar_t d_subdiv16 = { "d_subdiv16", "1", false, false, 0.0f, nullptr };
cvar_t d_mipcap = { "d_mipcap", "0", false, false, 0.0f, nullptr };
cvar_t d_mipscale = { "d_mipscale", "1", false, false, 0.0f, nullptr };
int d_minmip = 0;
std::array<float, 3> d_scalemip{};
constexpr std::array<float, 3> basemip = { 1.0f, 0.5f * 0.8f, 0.25f * 0.8f };
void (*d_drawspans)(espan_t* pspan) = nullptr;

int d_vrectx = 0, d_vrecty = 0, d_vrectright_particle = 0, d_vrectbottom_particle = 0;
int d_y_aspect_shift = 0, d_pix_min = 0, d_pix_max = 0, d_pix_shift = 0;
std::array<int, MAXHEIGHT> d_scantable{};
std::array<short*, MAXHEIGHT> zspantable{};

int miplevel = 0;
float scale_for_mip = 0.0f;
int ubasestep = 0, errorterm = 0, erroradjustup = 0, erroradjustdown = 0;
Vector3 transformed_modelorg{};

float d_sdivzstepu = 0.0f, d_tdivzstepu = 0.0f, d_zistepu = 0.0f;
float d_sdivzstepv = 0.0f, d_tdivzstepv = 0.0f, d_zistepv = 0.0f;
float d_sdivzorigin = 0.0f, d_tdivzorigin = 0.0f, d_ziorigin = 0.0f;
fixed16_t sadjust = 0, tadjust = 0, bbextents = 0, bbextentt = 0;
pixel_t* cacheblock = nullptr;
int cachewidth = 0;
pixel_t* d_viewbuffer = nullptr;
int d_aflatcolor = 0;
int d_spanpixcount = 0;

void D_Init()
{
    r_skydirect = 1;
    Cvar::Register(&d_subdiv16);
    Cvar::Register(&d_mipcap);
    Cvar::Register(&d_mipscale);
    r_drawpolys = false;
    r_worldpolysbacktofront = false;
    r_recursiveaffinetriangles = true;
    r_aliasuvscale = 1.0;
}

void D_TurnZOn()
{
}

void D_SetupFrame()
{
    if (r_dowarp) {
        d_viewbuffer = r_warpbuffer;
    } else {
        d_viewbuffer = Vid::vid.buffer;
    }
    if (r_dowarp) {
        screenwidth = WARP_WIDTH;
    } else {
        screenwidth = Vid::vid.rowbytes;
    }
    d_roverwrapped = false;
    d_initial_rover = sc_rover;
    d_minmip = static_cast<int>(d_mipcap.value);
    if (d_minmip > 3) {
        d_minmip = 3;
    } else if (d_minmip < 0) {
        d_minmip = 0;
    }
    for (size_t i = 0; i < (NUM_MIPS - 1); ++i) {
        d_scalemip[i] = basemip[i] * d_mipscale.value;
    }
    d_drawspans = D_DrawSpans8;
    d_aflatcolor = 0;
}

void D_UpdateRects(vrect_t* prect)
{
    static_cast<void>(prect);
}

void D_ViewChanged()
{
    int rowbytes;
    if (r_dowarp) {
        rowbytes = WARP_WIDTH;
    } else {
        rowbytes = Vid::vid.rowbytes;
    }
    scale_for_mip = xscale;
    if (yscale > xscale) {
        scale_for_mip = yscale;
    }
    d_zrowbytes = Vid::vid.width * 2;
    d_zwidth = Vid::vid.width;
    d_pix_min = r_refdef.vrect.width / 320;
    if (d_pix_min < 1) {
        d_pix_min = 1;
    }
    d_pix_max = static_cast<int>(static_cast<float>(r_refdef.vrect.width) / (320.0f / 4.0f) + 0.5f);
    d_pix_shift = 8 - static_cast<int>(static_cast<float>(r_refdef.vrect.width) / 320.0f + 0.5f);
    if (d_pix_max < 1) {
        d_pix_max = 1;
    }
    if (pixelAspect > 1.4) {
        d_y_aspect_shift = 1;
    } else {
        d_y_aspect_shift = 0;
    }
    d_vrectx = r_refdef.vrect.x;
    d_vrecty = r_refdef.vrect.y;
    d_vrectright_particle = r_refdef.vrectright - d_pix_max;
    d_vrectbottom_particle = r_refdef.vrectbottom - (d_pix_max << d_y_aspect_shift);
    for (unsigned i = 0; i < Vid::vid.height; ++i) {
        d_scantable[i] = i * rowbytes;
        zspantable[i] = d_pzbuffer + i * d_zwidth;
    }
}

void D_DrawPoly()
{
}

int D_MipLevelForScale(float scale)
{
    int lmiplevel;
    if (scale >= d_scalemip[0]) {
        lmiplevel = 0;
    } else if (scale >= d_scalemip[1]) {
        lmiplevel = 1;
    } else if (scale >= d_scalemip[2]) {
        lmiplevel = 2;
    } else {
        lmiplevel = 3;
    }
    if (lmiplevel < d_minmip) {
        lmiplevel = d_minmip;
    }
    return lmiplevel;
}

void D_DrawSolidSurface(surf_t* surf, int color)
{
    espan_t* span;
    byte* pdest;
    int u, u2, pix;
    pix = (color << 24) | (color << 16) | (color << 8) | color;
    for (span = surf->spans; span; span = span->pnext) {
        pdest = reinterpret_cast<byte*>(d_viewbuffer) + screenwidth * span->v;
        u = span->u;
        u2 = span->u + span->count - 1;
        pdest[u] = static_cast<byte>(pix);
        if (u2 - u < 8) {
            for (u++; u <= u2; u++) {
                pdest[u] = static_cast<byte>(pix);
            }
        } else {
            for (u++; u & 3; u++) {
                pdest[u] = static_cast<byte>(pix);
            }
            u2 -= 4;
            for (; u <= u2; u += 4) {
                *reinterpret_cast<int*>(pdest + u) = pix;
            }
            u2 += 4;
            for (; u <= u2; u++) {
                pdest[u] = static_cast<byte>(pix);
            }
        }
    }
}

void D_CalcGradients(msurface_t* pface)
{
    float mipscale;
    Vector3 p_temp1;
    Vector3 p_saxis, p_taxis;
    float t;
    mipscale = 1.0f / static_cast<float>(1 << miplevel);
    TransformVector(pface->texinfo->vecs[0], p_saxis);
    TransformVector(pface->texinfo->vecs[1], p_taxis);
    t = xscaleinv * mipscale;
    d_sdivzstepu = p_saxis.x * t;
    d_tdivzstepu = p_taxis.x * t;
    t = yscaleinv * mipscale;
    d_sdivzstepv = -p_saxis.y * t;
    d_tdivzstepv = -p_taxis.y * t;
    d_sdivzorigin = p_saxis.z * mipscale - xcenter * d_sdivzstepu - ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis.z * mipscale - xcenter * d_tdivzstepu - ycenter * d_tdivzstepv;
    p_temp1 = transformed_modelorg * mipscale;
    t = 0x10000 * mipscale;
    sadjust = static_cast<fixed16_t>(p_temp1.dot(p_saxis) * 0x10000 + 0.5f - ((pface->texturemins[0] << 16) >> miplevel) + pface->texinfo->vecs[0][3] * t);
    tadjust = static_cast<fixed16_t>(p_temp1.dot(p_taxis) * 0x10000 + 0.5f - ((pface->texturemins[1] << 16) >> miplevel) + pface->texinfo->vecs[1][3] * t);
    bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
    bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
}

void D_DrawSurfaces()
{
    surf_t* s;
    msurface_t* pface;
    surfcache_t* pcurrentcache;
    Vector3 world_transformed_modelorg;
    Vector3 local_modelorg;
    currententity = &Client::cl_entities[0];
    TransformVector(modelorg, transformed_modelorg);
    world_transformed_modelorg = transformed_modelorg;
    if (r_drawflat.value) {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }
            d_zistepu = s->d_zistepu;
            d_zistepv = s->d_zistepv;
            d_ziorigin = s->d_ziorigin;
            D_DrawSolidSurface(s, static_cast<int>(reinterpret_cast<uintptr_t>(s->data) & 0xFF));
            D_DrawZSpans(s->spans);
        }
    } else {
        for (s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }
            r_drawnpolycount++;
            d_zistepu = s->d_zistepu;
            d_zistepv = s->d_zistepv;
            d_ziorigin = s->d_ziorigin;
            if (s->flags & SURF_DRAWSKY) {
                if (!r_skymade) {
                    R_MakeSky();
                }
                D_DrawSkyScans8(s->spans);
                D_DrawZSpans(s->spans);
            } else if (s->flags & SURF_DRAWBACKGROUND) {
                d_zistepu = 0;
                d_zistepv = 0;
                d_ziorigin = -0.9f;
                D_DrawSolidSurface(s, static_cast<int>(r_clearcolor.value) & 0xFF);
                D_DrawZSpans(s->spans);
            } else if (s->flags & SURF_DRAWTURB) {
                pface = reinterpret_cast<msurface_t*>(s->data);
                miplevel = 0;
                cacheblock = reinterpret_cast<pixel_t*>(reinterpret_cast<byte*>(pface->texinfo->texture) + pface->texinfo->texture->offsets[0]);
                cachewidth = 64;
                if (s->insubmodel) {
                    currententity = s->entity;
                    local_modelorg = r_origin - currententity->origin;
                    TransformVector(local_modelorg, transformed_modelorg);
                    R_RotateBmodel();
                }
                D_CalcGradients(pface);
                Turbulent8(s->spans);
                D_DrawZSpans(s->spans);
                if (s->insubmodel) {
                    currententity = &Client::cl_entities[0];
                    transformed_modelorg = world_transformed_modelorg;
                    vpn = base_vpn;
                    vup = base_vup;
                    vright = base_vright;
                    modelorg = base_modelorg;
                    R_TransformFrustum();
                }
            } else {
                if (s->insubmodel) {
                    currententity = s->entity;
                    local_modelorg = r_origin - currententity->origin;
                    TransformVector(local_modelorg, transformed_modelorg);
                    R_RotateBmodel();
                }
                pface = reinterpret_cast<msurface_t*>(s->data);
                miplevel = D_MipLevelForScale(s->nearzi * scale_for_mip * pface->texinfo->mipadjust);
                pcurrentcache = D_CacheSurface(pface, miplevel);
                cacheblock = reinterpret_cast<pixel_t*>(pcurrentcache->data);
                cachewidth = pcurrentcache->width;
                D_CalcGradients(pface);
                (*d_drawspans)(s->spans);
                D_DrawZSpans(s->spans);
                if (s->insubmodel) {
                    currententity = &Client::cl_entities[0];
                    transformed_modelorg = world_transformed_modelorg;
                    vpn = base_vpn;
                    vup = base_vup;
                    vright = base_vright;
                    modelorg = base_modelorg;
                    R_TransformFrustum();
                }
            }
        }
    }
}

void D_DrawSpans8(espan_t* pspan)
{
    int count, spancount;
    unsigned char *pbase, *pdest;
    fixed16_t s, t, snext, tnext, sstep, tstep;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz8stepu, tdivz8stepu, zi8stepu;
    sstep = 0;
    tstep = 0;
    pbase = reinterpret_cast<unsigned char*>(cacheblock);
    sdivz8stepu = d_sdivzstepu * 8;
    tdivz8stepu = d_tdivzstepu * 8;
    zi8stepu = d_zistepu * 8;
    do {
        pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);
        count = pspan->count;
        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);
        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;
        s = static_cast<int>(sdivz * z) + sadjust;
        if (s > bbextents) {
            s = bbextents;
        } else if (s < 0) {
            s = 0;
        }
        t = static_cast<int>(tdivz * z) + tadjust;
        if (t > bbextentt) {
            t = bbextentt;
        } else if (t < 0) {
            t = 0;
        }
        do {
            if (count >= 8) {
                spancount = 8;
            } else {
                spancount = count;
            }
            count -= spancount;
            if (count) {
                sdivz += sdivz8stepu;
                tdivz += tdivz8stepu;
                zi += zi8stepu;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }
                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }
                sstep = (snext - s) >> 3;
                tstep = (tnext - t) >> 3;
            } else {
                spancountminus1 = static_cast<float>(spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 8) {
                    snext = 8;
                }
                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 8) {
                    tnext = 8;
                }
                if (spancount > 1) {
                    sstep = (snext - s) / (spancount - 1);
                    tstep = (tnext - t) / (spancount - 1);
                }
            }
            do {
                *pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
                s += sstep;
                t += tstep;
            } while (--spancount > 0);
            s = snext;
            t = tnext;
        } while (count > 0);
    } while ((pspan = pspan->pnext) != nullptr);
}

void D_DrawZSpans(espan_t* pspan)
{
    int count, doublecount, izistep;
    int izi;
    short* pdest;
    unsigned ltemp;
    double zi;
    float du, dv;
    izistep = static_cast<int>(d_zistepu * 0x8000 * 0x10000);
    do {
        pdest = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;
        count = pspan->count;
        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        izi = static_cast<int>(zi * 0x8000 * 0x10000);
        if (reinterpret_cast<uintptr_t>(pdest) & 0x02) {
            *pdest++ = static_cast<short>(izi >> 16);
            izi += izistep;
            count--;
        }
        if ((doublecount = count >> 1) > 0) {
            do {
                ltemp = izi >> 16;
                izi += izistep;
                ltemp |= izi & 0xFFFF0000;
                izi += izistep;
                *reinterpret_cast<int*>(pdest) = ltemp;
                pdest += 2;
            } while (--doublecount > 0);
        }
        if (count & 1) {
            *pdest = static_cast<short>(izi >> 16);
        }
    } while ((pspan = pspan->pnext) != nullptr);
}

} // namespace Render
