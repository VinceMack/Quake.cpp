// rasterizer.cpp -- merged software rasterization driver
//
// Merged from: d_vars.cpp, d_init.cpp, d_modech.cpp, d_edge.cpp,
//              d_scan.cpp, d_sky.cpp, d_surf.cpp, d_sprite.cpp, d_polyse.cpp

#include "quakedef.hpp"
#include <tuple>
#include <utility>

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

#include "d_local.hpp"
#include "r_local.hpp"

namespace Render {

// ==============================================================
// Global variables (TU-local via anonymous namespace)
// ==============================================================

namespace {

// d_vars.cpp
float d_sdivzstepu = 0.0f, d_tdivzstepu = 0.0f, d_zistepu = 0.0f;
float d_sdivzstepv = 0.0f, d_tdivzstepv = 0.0f, d_zistepv = 0.0f;
float d_sdivzorigin = 0.0f, d_tdivzorigin = 0.0f, d_ziorigin = 0.0f;

fixed16_t sadjust = 0, tadjust = 0, bbextents = 0, bbextentt = 0;

pixel_t* cacheblock = nullptr;
int cachewidth = 0;
pixel_t* d_viewbuffer = nullptr;

// d_init.cpp
constexpr int NUM_MIPS = 4;

cvar_t d_subdiv16 = { "d_subdiv16", "1", false, false, 0.0f, nullptr };
cvar_t d_mipcap = { "d_mipcap", "0", false, false, 0.0f, nullptr };
cvar_t d_mipscale = { "d_mipscale", "1", false, false, 0.0f, nullptr };

surfcache_t* d_initial_rover = nullptr;
qboolean d_roverwrapped = false;
int d_minmip = 0;
eastl::array<float, NUM_MIPS - 1> d_scalemip{};

constexpr eastl::array<float, NUM_MIPS - 1> basemip = { 1.0f, 0.5f * 0.8f, 0.25f * 0.8f };

void (*d_drawspans)(espan_t* pspan) = nullptr;

// d_modech.cpp
int d_vrectx = 0, d_vrecty = 0, d_vrectright_particle = 0, d_vrectbottom_particle = 0;

int d_y_aspect_shift = 0, d_pix_min = 0, d_pix_max = 0, d_pix_shift = 0;

eastl::array<int, MAXHEIGHT> d_scantable{};
eastl::array<short*, MAXHEIGHT> zspantable{};

// d_edge.cpp
int miplevel = 0;

float scale_for_mip = 0.0f;
int ubasestep = 0, errorterm = 0, erroradjustup = 0, erroradjustdown = 0;

Vector3 transformed_modelorg{};

// d_surf.cpp
float surfscale = 0.0f;

int sc_size = 0;
surfcache_t* sc_rover = nullptr;
surfcache_t* sc_base = nullptr;

// d_scan.cpp
unsigned char* r_turb_pbase = nullptr;
unsigned char* r_turb_pdest = nullptr;
fixed16_t r_turb_s = 0, r_turb_t = 0, r_turb_sstep = 0, r_turb_tstep = 0;
int* r_turb_turb = nullptr;
int r_turb_spancount = 0;

struct edgetable {
    int isflattop = 0;
    int numleftedges = 0;
    const eastl::array<int, 6>* pleftedgevert0 = nullptr;
    const eastl::array<int, 6>* pleftedgevert1 = nullptr;
    const eastl::array<int, 6>* pleftedgevert2 = nullptr;
    int numrightedges = 0;
    const eastl::array<int, 6>* prightedgevert0 = nullptr;
    const eastl::array<int, 6>* prightedgevert1 = nullptr;
    const eastl::array<int, 6>* prightedgevert2 = nullptr;
};

struct spanpackage_t {
    void* pdest = nullptr;
    short* pz = nullptr;
    int count = 0;
    byte* ptex = nullptr;
    int sfrac = 0, tfrac = 0, light = 0, zi = 0;
};

eastl::array<int, 6> r_p0{}, r_p1{}, r_p2{};

byte* d_pcolormap = nullptr;
int d_aflatcolor = 0;
int d_xdenom = 0;

const edgetable* pedgetable = nullptr;

// edgetables references r_p0, r_p1, r_p2 so must come after them
const edgetable edgetables[12] = {
    { 0, 1, &r_p0, &r_p2, nullptr, 2, &r_p0, &r_p1, &r_p2 },
    { 0, 2, &r_p1, &r_p0, &r_p2, 1, &r_p1, &r_p2, nullptr },
    { 1, 1, &r_p0, &r_p2, nullptr, 1, &r_p1, &r_p2, nullptr },
    { 0, 1, &r_p1, &r_p0, nullptr, 2, &r_p1, &r_p2, &r_p0 },
    { 0, 2, &r_p0, &r_p2, &r_p1, 1, &r_p0, &r_p1, nullptr },
    { 0, 1, &r_p2, &r_p1, nullptr, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p2, &r_p1, nullptr, 2, &r_p2, &r_p0, &r_p1 },
    { 0, 2, &r_p2, &r_p1, &r_p0, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p1, &r_p0, nullptr, 1, &r_p1, &r_p2, nullptr },
    { 1, 1, &r_p2, &r_p1, nullptr, 1, &r_p0, &r_p1, nullptr },
    { 1, 1, &r_p1, &r_p0, nullptr, 1, &r_p2, &r_p0, nullptr },
    { 0, 1, &r_p0, &r_p2, nullptr, 1, &r_p0, &r_p1, nullptr },
};

int a_sstepxfrac = 0, a_tstepxfrac = 0, r_lstepx = 0, a_ststepxwhole = 0;
int r_sstepx = 0, r_tstepx = 0, r_lstepy = 0, r_sstepy = 0, r_tstepy = 0;
int r_zistepx = 0, r_zistepy = 0;
int d_aspancount = 0, d_countextrastep = 0;

spanpackage_t* a_spans = nullptr;
spanpackage_t* d_pedgespanpackage = nullptr;
int ystart = 0;
byte* d_pdest = nullptr;
byte* d_ptex = nullptr;
short* d_pz = nullptr;
int d_sfrac = 0, d_tfrac = 0, d_light = 0, d_zi = 0;
int d_ptexextrastep = 0, d_sfracextrastep = 0;
int d_tfracextrastep = 0, d_lightextrastep = 0, d_pdestextrastep = 0;
int d_lightbasestep = 0, d_pdestbasestep = 0, d_ptexbasestep = 0;
int d_sfracbasestep = 0, d_tfracbasestep = 0;
int d_ziextrastep = 0, d_zibasestep = 0;
int d_pzextrastep = 0, d_pzbasestep = 0;

eastl::array<byte*, MAX_LBM_HEIGHT> skintable{};
int skinwidth = 0;
byte* skinstart = nullptr;

// d_sprite.cpp
int sprite_height = 0;
int minindex = 0, maxindex = 0;
sspan_t* sprite_spans = nullptr;

} // namespace

// Shared global variables (visible to other TUs)
short* d_pzbuffer = nullptr;
unsigned int d_zrowbytes = 0;
unsigned int d_zwidth = 0;
qboolean r_cache_thrash = false;

// ==============================================================
// Forward declarations for internal functions
// ==============================================================

void D_DrawTurbulent8Span();
void D_PolysetDrawSpans8(spanpackage_t* pspanpackage);
void D_PolysetCalcGradients(int skinwidth);
void D_DrawSubdiv();
void D_DrawNonSubdiv();
void D_PolysetRecursiveTriangle(const eastl::array<int, 6>* p1, const eastl::array<int, 6>* p2, const eastl::array<int, 6>* p3);
void D_PolysetSetEdgeTable();
void D_RasterizeAliasPolySmooth();
void D_PolysetScanLeftEdge(int height);

// ==============================================================
// d_init.cpp -- rasterization driver initialization
// ==============================================================

void D_Init(void)
{
    r_skydirect = 1;

    Cvar::Register(&d_subdiv16);
    Cvar::Register(&d_mipcap);
    Cvar::Register(&d_mipscale);

    r_drawpolys = false;
    r_worldpolysbacktofront = false;
    r_recursiveaffinetriangles = true;
    r_pixbytes = 1;
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
        d_viewbuffer = vid.buffer;
    }

    if (r_dowarp) {
        screenwidth = WARP_WIDTH;
    } else {
        screenwidth = vid.rowbytes;
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
    UNUSED(prect);
}

// ==============================================================
// d_modech.cpp -- called when mode has just changed
// ==============================================================

void D_ViewChanged()
{
    int rowbytes;

    if (r_dowarp) {
        rowbytes = WARP_WIDTH;
    } else {
        rowbytes = vid.rowbytes;
    }

    scale_for_mip = xscale;
    if (yscale > xscale) {
        scale_for_mip = yscale;
    }

    d_zrowbytes = vid.width * 2;
    d_zwidth = vid.width;

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

    {
        for (unsigned i = 0; i < vid.height; ++i) {
            d_scantable[i] = i * rowbytes;
            zspantable[i] = d_pzbuffer + i * d_zwidth;
        }
    }
}

// ==============================================================
// d_edge.cpp -- software edge rendering (mipmapping and texture stepping)
// ==============================================================

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

    currententity = &cl_entities[0];
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
                    currententity = &cl_entities[0];
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
                    currententity = &cl_entities[0];
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

// ==============================================================
// d_scan.cpp -- portable C scan-level rasterization code
// ==============================================================

void D_DrawTurbulent8Span()
{
    int sturb, tturb;

    do {
        sturb = ((r_turb_s + r_turb_turb[(r_turb_t >> 16) & (CYCLE - 1)]) >> 16) & 63;
        tturb = ((r_turb_t + r_turb_turb[(r_turb_s >> 16) & (CYCLE - 1)]) >> 16) & 63;
        *r_turb_pdest++ = *(r_turb_pbase + (tturb << 6) + sturb);
        r_turb_s += r_turb_sstep;
        r_turb_t += r_turb_tstep;
    } while (--r_turb_spancount > 0);
}

void D_WarpScreen()
{
    int w, h;
    int u, v;
    byte* dest;
    int* turb;
    int* col;
    byte** row;
    eastl::array<byte*, MAXHEIGHT + (AMP2 * 2)> rowptr{};
    eastl::array<int, MAXWIDTH + (AMP2 * 2)> column{};
    float wratio, hratio;
    const auto& scr_vrect = GetScreenSystem().GetVrect();

    w = r_refdef.vrect.width;
    h = r_refdef.vrect.height;

    wratio = static_cast<float>(w) / static_cast<float>(scr_vrect.width);
    hratio = static_cast<float>(h) / static_cast<float>(scr_vrect.height);

    for (v = 0; v < scr_vrect.height + AMP2 * 2; v++) {
        rowptr[v] = reinterpret_cast<byte*>(d_viewbuffer) + (r_refdef.vrect.y * screenwidth) + (screenwidth * static_cast<int>(static_cast<float>(v) * hratio * static_cast<float>(h) / static_cast<float>(h + AMP2 * 2)));
    }

    for (u = 0; u < scr_vrect.width + AMP2 * 2; u++) {
        column[u] = r_refdef.vrect.x + static_cast<int>(static_cast<float>(u) * wratio * static_cast<float>(w) / static_cast<float>(w + AMP2 * 2));
    }

    turb = intsintable.data() + (static_cast<int>(cl.time * SPEED) & (CYCLE - 1));
    dest = reinterpret_cast<byte*>(vid.buffer) + scr_vrect.y * vid.rowbytes + scr_vrect.x;

    for (v = 0; v < scr_vrect.height; v++, dest += vid.rowbytes) {
        col = &column[turb[v]];
        row = &rowptr[v];

        for (u = 0; u < scr_vrect.width; u += 4) {
            dest[u + 0] = row[turb[u + 0]][col[u + 0]];
            dest[u + 1] = row[turb[u + 1]][col[u + 1]];
            dest[u + 2] = row[turb[u + 2]][col[u + 2]];
            dest[u + 3] = row[turb[u + 3]][col[u + 3]];
        }
    }
}

void Turbulent8(espan_t* pspan)
{
    int count;
    fixed16_t snext, tnext;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz16stepu, tdivz16stepu, zi16stepu;

    r_turb_turb = sintable.data() + (static_cast<int>(cl.time * SPEED) & (CYCLE - 1));

    r_turb_sstep = 0;
    r_turb_tstep = 0;

    r_turb_pbase = reinterpret_cast<unsigned char*>(cacheblock);

    sdivz16stepu = d_sdivzstepu * 16;
    tdivz16stepu = d_tdivzstepu * 16;
    zi16stepu = d_zistepu * 16;

    do {
        r_turb_pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);

        count = pspan->count;

        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;

        r_turb_s = static_cast<int>(sdivz * z) + sadjust;
        if (r_turb_s > bbextents) {
            r_turb_s = bbextents;
        } else if (r_turb_s < 0) {
            r_turb_s = 0;
        }

        r_turb_t = static_cast<int>(tdivz * z) + tadjust;
        if (r_turb_t > bbextentt) {
            r_turb_t = bbextentt;
        } else if (r_turb_t < 0) {
            r_turb_t = 0;
        }

        do {
            if (count >= 16) {
                r_turb_spancount = 16;
            } else {
                r_turb_spancount = count;
            }

            count -= r_turb_spancount;

            if (count) {
                sdivz += sdivz16stepu;
                tdivz += tdivz16stepu;
                zi += zi16stepu;
                z = 0x10000 / zi;

                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 16) {
                    snext = 16;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 16) {
                    tnext = 16;
                }

                r_turb_sstep = (snext - r_turb_s) >> 4;
                r_turb_tstep = (tnext - r_turb_t) >> 4;
            } else {
                spancountminus1 = static_cast<float>(r_turb_spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = 0x10000 / zi;
                snext = static_cast<int>(sdivz * z) + sadjust;
                if (snext > bbextents) {
                    snext = bbextents;
                } else if (snext < 16) {
                    snext = 16;
                }

                tnext = static_cast<int>(tdivz * z) + tadjust;
                if (tnext > bbextentt) {
                    tnext = bbextentt;
                } else if (tnext < 16) {
                    tnext = 16;
                }

                if (r_turb_spancount > 1) {
                    r_turb_sstep = (snext - r_turb_s) / (r_turb_spancount - 1);
                    r_turb_tstep = (tnext - r_turb_t) / (r_turb_spancount - 1);
                }
            }

            r_turb_s = r_turb_s & ((CYCLE << 16) - 1);
            r_turb_t = r_turb_t & ((CYCLE << 16) - 1);

            D_DrawTurbulent8Span();

            r_turb_s = snext;
            r_turb_t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != nullptr);
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

// ==============================================================
// d_sky.cpp -- software sky texture coordinate calculation
// ==============================================================

constexpr int SKY_SPAN_SHIFT = 5;
constexpr int SKY_SPAN_MAX = 1 << SKY_SPAN_SHIFT;

void D_Sky_uv_To_st(int u, int v, fixed16_t* s, fixed16_t* t)
{
    float wu, wv, temp;
    Vector3 end;

    if (r_refdef.vrect.width >= r_refdef.vrect.height) {
        temp = static_cast<float>(r_refdef.vrect.width);
    } else {
        temp = static_cast<float>(r_refdef.vrect.height);
    }

    wu = 8192.0f * static_cast<float>(u - (static_cast<int>(vid.width) >> 1)) / temp;
    wv = 8192.0f * static_cast<float>((static_cast<int>(vid.height) >> 1) - v) / temp;

    end = vpn * 4096.0f + vright * wu + vup * wv;
    end.z *= 3.0f;
    end.normalize();

    temp = skytime * skyspeed;
    *s = static_cast<int>((temp + 6.0f * (static_cast<float>(SKYSIZE) / 2.0f - 1.0f) * end.x) * 65536.0f);
    *t = static_cast<int>((temp + 6.0f * (static_cast<float>(SKYSIZE) / 2.0f - 1.0f) * end.y) * 65536.0f);
}

void D_DrawSkyScans8(espan_t* pspan)
{
    int count, spancount, u, v;
    unsigned char* pdest;
    fixed16_t s, t, snext = 0, tnext = 0, sstep, tstep;
    float spancountminus1;

    sstep = 0;
    tstep = 0;

    do {
        pdest = reinterpret_cast<unsigned char*>(reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u);

        count = pspan->count;

        u = pspan->u;
        v = pspan->v;
        D_Sky_uv_To_st(u, v, &s, &t);

        do {
            if (count >= SKY_SPAN_MAX) {
                spancount = SKY_SPAN_MAX;
            } else {
                spancount = count;
            }

            count -= spancount;

            if (count) {
                u += spancount;

                D_Sky_uv_To_st(u, v, &snext, &tnext);

                sstep = (snext - s) >> SKY_SPAN_SHIFT;
                tstep = (tnext - t) >> SKY_SPAN_SHIFT;
            } else {
                spancountminus1 = static_cast<float>(spancount - 1);

                if (spancountminus1 > 0) {
                    u += static_cast<int>(spancountminus1);
                    D_Sky_uv_To_st(u, v, &snext, &tnext);

                    sstep = static_cast<fixed16_t>((snext - s) / spancountminus1);
                    tstep = static_cast<fixed16_t>((tnext - t) / spancountminus1);
                }
            }

            do {
                *pdest++ = r_skysource[((t & R_SKY_TMASK) >> 8) + ((s & R_SKY_SMASK) >> 16)];
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != nullptr);
}

// ==============================================================
// d_surf.cpp -- rasterization driver surface heap manager
// ==============================================================

constexpr int GUARDSIZE = 4;

int D_SurfaceCacheForRes(int width, int height)
{
    int size, pix;

    if (COM_CheckParm("-surfcachesize")) {
        size = Q_atoi(com_argv[COM_CheckParm("-surfcachesize") + 1]) * 1024;

        return size;
    }

    size = SURFCACHE_SIZE_AT_320X200;

    pix = width * height;
    if (pix > 64000) {
        size += (pix - 64000) * 3;
    }

    return size;
}

void D_CheckCacheGuard()
{
    byte* s;
    int i;

    s = reinterpret_cast<byte*>(sc_base) + sc_size;
    for (i = 0; i < GUARDSIZE; i++) {
        if (s[i] != static_cast<byte>(i)) {
            Sys_Error("D_CheckCacheGuard: failed");
        }
    }
}

void D_ClearCacheGuard()
{
    byte* s;
    int i;

    s = reinterpret_cast<byte*>(sc_base) + sc_size;
    for (i = 0; i < GUARDSIZE; i++) {
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
    surfcache_t* c;

    if (!sc_base) {
        return;
    }

    for (c = sc_base; c; c = c->next) {
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
    surfcache_t* new_surf;
    qboolean wrapped_this_time;

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

    wrapped_this_time = false;

    if (!sc_rover || reinterpret_cast<byte*>(sc_rover) - reinterpret_cast<byte*>(sc_base) > sc_size - size) {
        if (sc_rover) {
            wrapped_this_time = true;
        }

        sc_rover = sc_base;
    }

    new_surf = sc_rover;
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
    surfcache_t* cache;

    r_drawsurf.texture = R_TextureAnimation(surface->texinfo->texture);
    r_drawsurf.lightadj[0] = d_lightstylevalue[surface->styles[0]];
    r_drawsurf.lightadj[1] = d_lightstylevalue[surface->styles[1]];
    r_drawsurf.lightadj[2] = d_lightstylevalue[surface->styles[2]];
    r_drawsurf.lightadj[3] = d_lightstylevalue[surface->styles[3]];

    cache = surface->cachespots[mip_level];

    if (cache && !cache->dlight && surface->dlightframe != r_framecount && cache->texture == r_drawsurf.texture && cache->lightadj[0] == r_drawsurf.lightadj[0] && cache->lightadj[1] == r_drawsurf.lightadj[1] && cache->lightadj[2] == r_drawsurf.lightadj[2] && cache->lightadj[3] == r_drawsurf.lightadj[3]) {
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

// ==============================================================
// d_sprite.cpp -- software sprite rasterization
// ==============================================================

void D_SpriteDrawSpans(sspan_t* pspan)
{
    int count, spancount, izistep;
    int izi;
    byte *pbase, *pdest;
    fixed16_t s, t, snext, tnext, sstep, tstep;
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float sdivz8stepu, tdivz8stepu, zi8stepu;
    byte btemp;
    short* pz;

    sstep = 0;
    tstep = 0;

    pbase = cacheblock;

    sdivz8stepu = d_sdivzstepu * 8;
    tdivz8stepu = d_tdivzstepu * 8;
    zi8stepu = d_zistepu * 8;

    izistep = static_cast<int>(d_zistepu * 0x8000 * 0x10000);

    do {
        pdest = reinterpret_cast<byte*>(d_viewbuffer) + (screenwidth * pspan->v) + pspan->u;
        pz = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;

        count = pspan->count;

        if (count <= 0) {
            goto NextSpan;
        }

        du = static_cast<float>(pspan->u);
        dv = static_cast<float>(pspan->v);

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = 0x10000 / zi;
        izi = static_cast<int>(zi * 0x8000 * 0x10000);

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
                btemp = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
                if (btemp != 255) {
                    if (*pz <= static_cast<short>(izi >> 16)) {
                        *pz = static_cast<short>(izi >> 16);
                        *pdest = btemp;
                    }
                }

                izi += izistep;
                pdest++;
                pz++;
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    NextSpan:
        pspan++;

    } while (pspan->count != DS_SPAN_LIST_END);
}

void D_SpriteScanLeftEdge()
{
    int i, v, itop, ibottom, lmaxindex;
    emitpoint_t *pvert, *pnext;
    sspan_t* pspan;
    float du, dv, vtop, vbottom, slope;
    fixed16_t u, u_step;

    pspan = sprite_spans;
    i = minindex;
    if (i == 0) {
        i = r_spritedesc.nump;
    }

    lmaxindex = maxindex;
    if (lmaxindex == 0) {
        lmaxindex = r_spritedesc.nump;
    }

    vtop = ceil(r_spritedesc.pverts[i].v);

    do {
        pvert = &r_spritedesc.pverts[i];
        pnext = pvert - 1;

        vbottom = ceil(pnext->v);

        if (vtop < vbottom) {
            du = pnext->u - pvert->u;
            dv = pnext->v - pvert->v;
            slope = du / dv;
            u_step = static_cast<fixed16_t>(slope * 65536.0f);
            u = static_cast<fixed16_t>((pvert->u + (slope * (vtop - pvert->v))) * 65536.0f) + (65536 - 1);
            itop = static_cast<int>(vtop);
            ibottom = static_cast<int>(vbottom);

            for (v = itop; v < ibottom; v++) {
                pspan->u = u >> 16;
                pspan->v = v;
                u += u_step;
                pspan++;
            }
        }

        vtop = vbottom;

        i--;
        if (i == 0) {
            i = r_spritedesc.nump;
        }

    } while (i != lmaxindex);
}

void D_SpriteScanRightEdge()
{
    int i, v, itop, ibottom;
    emitpoint_t *pvert, *pnext;
    sspan_t* pspan;
    float du, dv, vtop, vbottom, slope, uvert, unext, vvert, vnext;
    fixed16_t u, u_step;

    pspan = sprite_spans;
    i = minindex;

    vvert = r_spritedesc.pverts[i].v;
    if (vvert < r_refdef.fvrecty_adj) {
        vvert = r_refdef.fvrecty_adj;
    }

    if (vvert > r_refdef.fvrectbottom_adj) {
        vvert = r_refdef.fvrectbottom_adj;
    }

    vtop = ceil(vvert);

    do {
        pvert = &r_spritedesc.pverts[i];
        pnext = pvert + 1;

        vnext = pnext->v;
        if (vnext < r_refdef.fvrecty_adj) {
            vnext = r_refdef.fvrecty_adj;
        }

        if (vnext > r_refdef.fvrectbottom_adj) {
            vnext = r_refdef.fvrectbottom_adj;
        }

        vbottom = ceil(vnext);

        if (vtop < vbottom) {
            uvert = pvert->u;
            if (uvert < r_refdef.fvrectx_adj) {
                uvert = r_refdef.fvrectx_adj;
            }

            if (uvert > r_refdef.fvrectright_adj) {
                uvert = r_refdef.fvrectright_adj;
            }

            unext = pnext->u;
            if (unext < r_refdef.fvrectx_adj) {
                unext = r_refdef.fvrectx_adj;
            }

            if (unext > r_refdef.fvrectright_adj) {
                unext = r_refdef.fvrectright_adj;
            }

            du = unext - uvert;
            dv = vnext - vvert;
            slope = du / dv;
            u_step = static_cast<fixed16_t>(slope * 65536.0f);
            u = static_cast<fixed16_t>((uvert + (slope * (vtop - vvert))) * 65536.0f) + (65536 - 1);
            itop = static_cast<int>(vtop);
            ibottom = static_cast<int>(vbottom);

            for (v = itop; v < ibottom; v++) {
                pspan->count = (u >> 16) - pspan->u;
                u += u_step;
                pspan++;
            }
        }

        vtop = vbottom;
        vvert = vnext;

        i++;
        if (i == r_spritedesc.nump) {
            i = 0;
        }

    } while (i != maxindex);

    pspan->count = DS_SPAN_LIST_END;
}

void D_SpriteCalculateGradients()
{
    Vector3 p_normal, p_saxis, p_taxis, p_temp1;
    float distinv;

    TransformVector(r_spritedesc.vpn, p_normal);
    TransformVector(r_spritedesc.vright, p_saxis);
    TransformVector(r_spritedesc.vup, p_taxis);
    p_taxis = -p_taxis;

    distinv = 1.0f / (-modelorg.dot(r_spritedesc.vpn));

    d_sdivzstepu = p_saxis.x * xscaleinv;
    d_tdivzstepu = p_taxis.x * xscaleinv;

    d_sdivzstepv = -p_saxis.y * yscaleinv;
    d_tdivzstepv = -p_taxis.y * yscaleinv;

    d_zistepu = p_normal.x * xscaleinv * distinv;
    d_zistepv = -p_normal.y * yscaleinv * distinv;

    d_sdivzorigin = p_saxis.z - xcenter * d_sdivzstepu - ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis.z - xcenter * d_tdivzstepu - ycenter * d_tdivzstepv;
    d_ziorigin = p_normal.z * distinv - xcenter * d_zistepu - ycenter * d_zistepv;

    TransformVector(modelorg, p_temp1);

    sadjust = static_cast<fixed16_t>(p_temp1.dot(p_saxis) * 65536.0f + 0.5f) - (-(cachewidth >> 1) << 16);
    tadjust = static_cast<fixed16_t>(p_temp1.dot(p_taxis) * 65536.0f + 0.5f) - (-(sprite_height >> 1) << 16);

    bbextents = (cachewidth << 16) - 1;
    bbextentt = (sprite_height << 16) - 1;
}

void D_DrawSprite()
{
    int i, nump;
    float ymin, ymax;
    emitpoint_t* pverts;
    eastl::array<sspan_t, MAXHEIGHT + 1> spans{};

    sprite_spans = spans.data();

    ymin = 999999.9f;
    ymax = -999999.9f;
    pverts = r_spritedesc.pverts;

    for (i = 0; i < r_spritedesc.nump; i++) {
        if (pverts->v < ymin) {
            ymin = pverts->v;
            minindex = i;
        }

        if (pverts->v > ymax) {
            ymax = pverts->v;
            maxindex = i;
        }

        pverts++;
    }

    ymin = ceil(ymin);
    ymax = ceil(ymax);

    if (ymin >= ymax) {
        return;
    }

    cachewidth = r_spritedesc.pspriteframe->width;
    sprite_height = r_spritedesc.pspriteframe->height;
    cacheblock = reinterpret_cast<byte*>(&r_spritedesc.pspriteframe->pixels[0]);

    nump = r_spritedesc.nump;
    pverts = r_spritedesc.pverts;
    pverts[nump] = pverts[0];

    D_SpriteCalculateGradients();
    D_SpriteScanLeftEdge();
    D_SpriteScanRightEdge();
    D_SpriteDrawSpans(sprite_spans);
}

// ==============================================================
// d_polyse.cpp -- routines for drawing sets of polygons sharing the same
//                 texture (used for Alias models)
// ==============================================================

constexpr int DPS_MAXSPANS = MAXHEIGHT + 1;

void D_PolysetDraw()
{
    alignas(CACHE_SIZE) spanpackage_t spans[DPS_MAXSPANS + 1];

    a_spans = spans;

    if (r_affinetridesc.drawtype) {
        D_DrawSubdiv();
    } else {
        D_DrawNonSubdiv();
    }
}

void D_PolysetDrawFinalVerts(finalvert_t* fv, int num_verts)
{
    int i, z;
    short* zbuf;

    for (i = 0; i < num_verts; i++, fv++) {
        if ((fv->v[0] < r_refdef.vrectright) && (fv->v[1] < r_refdef.vrectbottom)) {
            z = fv->v[5] >> 16;
            zbuf = zspantable[fv->v[1]] + fv->v[0];
            if (z >= *zbuf) {
                int pix;

                *zbuf = static_cast<short>(z);
                pix = skintable[fv->v[3] >> 16][fv->v[2] >> 16];
                pix = reinterpret_cast<byte*>(acolormap)[pix + (fv->v[4] & 0xFF00)];
                d_viewbuffer[d_scantable[fv->v[1]] + fv->v[0]] = static_cast<pixel_t>(pix);
            }
        }
    }
}

void D_DrawSubdiv()
{
    mtriangle_t* ptri;
    finalvert_t *pfv, *index0, *index1, *index2;
    int i;
    int lnumtriangles;

    pfv = r_affinetridesc.pfinalverts;
    ptri = r_affinetridesc.ptriangles;
    lnumtriangles = r_affinetridesc.numtriangles;

    for (i = 0; i < lnumtriangles; i++) {
        index0 = pfv + ptri[i].vertindex[0];
        index1 = pfv + ptri[i].vertindex[1];
        index2 = pfv + ptri[i].vertindex[2];

        if (((index0->v[1] - index1->v[1]) * (index0->v[0] - index2->v[0]) - (index0->v[0] - index1->v[0]) * (index0->v[1] - index2->v[1])) >= 0) {
            continue;
        }

        d_pcolormap = &reinterpret_cast<byte*>(acolormap)[index0->v[4] & 0xFF00];

        if (ptri[i].facesfront) {
            D_PolysetRecursiveTriangle(&index0->v, &index1->v, &index2->v);
        } else {
            int s0, s1, s2;

            s0 = index0->v[2];
            s1 = index1->v[2];
            s2 = index2->v[2];

            if (index0->flags & ALIAS_ONSEAM) {
                index0->v[2] += r_affinetridesc.seamfixupX16;
            }

            if (index1->flags & ALIAS_ONSEAM) {
                index1->v[2] += r_affinetridesc.seamfixupX16;
            }

            if (index2->flags & ALIAS_ONSEAM) {
                index2->v[2] += r_affinetridesc.seamfixupX16;
            }

            D_PolysetRecursiveTriangle(&index0->v, &index1->v, &index2->v);

            index0->v[2] = s0;
            index1->v[2] = s1;
            index2->v[2] = s2;
        }
    }
}

void D_DrawNonSubdiv()
{
    mtriangle_t* ptri;
    finalvert_t *pfv, *index0, *index1, *index2;
    int i;
    int lnumtriangles;

    pfv = r_affinetridesc.pfinalverts;
    ptri = r_affinetridesc.ptriangles;
    lnumtriangles = r_affinetridesc.numtriangles;

    for (i = 0; i < lnumtriangles; i++, ptri++) {
        index0 = pfv + ptri->vertindex[0];
        index1 = pfv + ptri->vertindex[1];
        index2 = pfv + ptri->vertindex[2];

        d_xdenom = (index0->v[1] - index1->v[1]) * (index0->v[0] - index2->v[0]) - (index0->v[0] - index1->v[0]) * (index0->v[1] - index2->v[1]);

        if (d_xdenom >= 0) {
            continue;
        }

        r_p0 = index0->v;
        r_p1 = index1->v;
        r_p2 = index2->v;

        if (!ptri->facesfront) {
            if (index0->flags & ALIAS_ONSEAM) {
                r_p0[2] += r_affinetridesc.seamfixupX16;
            }

            if (index1->flags & ALIAS_ONSEAM) {
                r_p1[2] += r_affinetridesc.seamfixupX16;
            }

            if (index2->flags & ALIAS_ONSEAM) {
                r_p2[2] += r_affinetridesc.seamfixupX16;
            }
        }

        D_PolysetSetEdgeTable();
        D_RasterizeAliasPolySmooth();
    }
}

void D_PolysetRecursiveTriangle(const eastl::array<int, 6>* lp1, const eastl::array<int, 6>* lp2, const eastl::array<int, 6>* lp3)
{
    const eastl::array<int, 6>* temp;
    int d;
    eastl::array<int, 6> new_poly{};
    int z;
    short* zbuf;

    d = (*lp2)[0] - (*lp1)[0];
    if (d < -1 || d > 1) {
        goto split;
    }

    d = (*lp2)[1] - (*lp1)[1];
    if (d < -1 || d > 1) {
        goto split;
    }

    d = (*lp3)[0] - (*lp2)[0];
    if (d < -1 || d > 1) {
        goto split2;
    }

    d = (*lp3)[1] - (*lp2)[1];
    if (d < -1 || d > 1) {
        goto split2;
    }

    d = (*lp1)[0] - (*lp3)[0];
    if (d < -1 || d > 1) {
        goto split3;
    }

    d = (*lp1)[1] - (*lp3)[1];
    if (d < -1 || d > 1) {
    split3:
        temp = lp1;
        lp1 = lp3;
        lp3 = lp2;
        lp2 = temp;

        goto split;
    }

    return;

split2:
    temp = lp1;
    lp1 = lp2;
    lp2 = lp3;
    lp3 = temp;

split:
    new_poly[0] = ((*lp1)[0] + (*lp2)[0]) >> 1;
    new_poly[1] = ((*lp1)[1] + (*lp2)[1]) >> 1;
    new_poly[2] = ((*lp1)[2] + (*lp2)[2]) >> 1;
    new_poly[3] = ((*lp1)[3] + (*lp2)[3]) >> 1;
    new_poly[5] = ((*lp1)[5] + (*lp2)[5]) >> 1;

    if ((*lp2)[1] > (*lp1)[1]) {
        goto nodraw;
    }

    if (((*lp2)[1] == (*lp1)[1]) && ((*lp2)[0] < (*lp1)[0])) {
        goto nodraw;
    }

    z = new_poly[5] >> 16;
    zbuf = zspantable[new_poly[1]] + new_poly[0];
    if (z >= *zbuf) {
        int pix;

        *zbuf = static_cast<short>(z);
        pix = d_pcolormap[skintable[new_poly[3] >> 16][new_poly[2] >> 16]];
        d_viewbuffer[d_scantable[new_poly[1]] + new_poly[0]] = static_cast<pixel_t>(pix);
    }

nodraw:
    D_PolysetRecursiveTriangle(lp3, lp1, &new_poly);
    D_PolysetRecursiveTriangle(lp3, &new_poly, lp2);
}

void D_PolysetUpdateTables()
{
    int i;
    byte* s;

    if (r_affinetridesc.skinwidth != skinwidth || r_affinetridesc.pskin != skinstart) {
        skinwidth = r_affinetridesc.skinwidth;
        skinstart = reinterpret_cast<byte*>(r_affinetridesc.pskin);
        s = skinstart;
        for (i = 0; i < MAX_LBM_HEIGHT; i++, s += skinwidth) {
            skintable[i] = s;
        }
    }
}

void D_PolysetScanLeftEdge(int height)
{
    do {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;

        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;

        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;

        d_pedgespanpackage++;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_pdest += d_pdestextrastep;
            d_pz += d_pzextrastep;
            d_aspancount += d_countextrastep;
            d_ptex += d_ptexextrastep;
            d_sfrac += d_sfracextrastep;
            d_ptex += d_sfrac >> 16;

            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracextrastep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }

            d_light += d_lightextrastep;
            d_zi += d_ziextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_pdest += d_pdestbasestep;
            d_pz += d_pzbasestep;
            d_aspancount += ubasestep;
            d_ptex += d_ptexbasestep;
            d_sfrac += d_sfracbasestep;
            d_ptex += d_sfrac >> 16;
            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracbasestep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }

            d_light += d_lightbasestep;
            d_zi += d_zibasestep;
        }
    } while (--height);
}

void D_PolysetSetUpForLineScan(fixed8_t startvertu,
    fixed8_t startvertv,
    fixed8_t endvertu,
    fixed8_t endvertv)
{
    double dm, dn;
    int tm, tn;

    errorterm = -1;

    tm = endvertu - startvertu;
    tn = endvertv - startvertv;

    dm = static_cast<double>(tm);
    dn = static_cast<double>(tn);

    std::tie(ubasestep, erroradjustup) = FloorDivMod(dm, dn);

    erroradjustdown = tn;
}

void D_PolysetCalcGradients(int s_width)
{
    float xstepdenominv, ystepdenominv, t0, t1;
    float p01_minus_p21, p11_minus_p21, p00_minus_p20, p10_minus_p20;

    p00_minus_p20 = static_cast<float>(r_p0[0] - r_p2[0]);
    p01_minus_p21 = static_cast<float>(r_p0[1] - r_p2[1]);
    p10_minus_p20 = static_cast<float>(r_p1[0] - r_p2[0]);
    p11_minus_p21 = static_cast<float>(r_p1[1] - r_p2[1]);

    xstepdenominv = 1.0f / static_cast<float>(d_xdenom);

    ystepdenominv = -xstepdenominv;

    t0 = static_cast<float>(r_p0[4] - r_p2[4]);
    t1 = static_cast<float>(r_p1[4] - r_p2[4]);
    r_lstepx = static_cast<int>(ceil((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv));
    r_lstepy = static_cast<int>(ceil((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv));

    t0 = static_cast<float>(r_p0[2] - r_p2[2]);
    t1 = static_cast<float>(r_p1[2] - r_p2[2]);
    r_sstepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_sstepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    t0 = static_cast<float>(r_p0[3] - r_p2[3]);
    t1 = static_cast<float>(r_p1[3] - r_p2[3]);
    r_tstepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_tstepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    t0 = static_cast<float>(r_p0[5] - r_p2[5]);
    t1 = static_cast<float>(r_p1[5] - r_p2[5]);
    r_zistepx = static_cast<int>((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_zistepy = static_cast<int>((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    a_sstepxfrac = r_sstepx & 0xFFFF;
    a_tstepxfrac = r_tstepx & 0xFFFF;
    a_ststepxwhole = s_width * (r_tstepx >> 16) + (r_sstepx >> 16);
}

void D_PolysetDrawSpans8(spanpackage_t* pspanpackage)
{
    int lcount;
    byte* lpdest;
    byte* lptex;
    int lsfrac, ltfrac;
    int llight;
    int lzi;
    short* lpz;

    do {
        lcount = d_aspancount - pspanpackage->count;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_aspancount += d_countextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_aspancount += ubasestep;
        }

        if (lcount) {
            lpdest = reinterpret_cast<byte*>(pspanpackage->pdest);
            lptex = pspanpackage->ptex;
            lpz = pspanpackage->pz;
            lsfrac = pspanpackage->sfrac;
            ltfrac = pspanpackage->tfrac;
            llight = pspanpackage->light;
            lzi = pspanpackage->zi;

            do {
                if ((lzi >> 16) >= *lpz) {
                    *lpdest = reinterpret_cast<byte*>(acolormap)[*lptex + (llight & 0xFF00)];
                    *lpz = static_cast<short>(lzi >> 16);
                }

                lpdest++;
                lzi += r_zistepx;
                lpz++;
                llight += r_lstepx;
                lptex += a_ststepxwhole;
                lsfrac += a_sstepxfrac;
                lptex += lsfrac >> 16;
                lsfrac &= 0xFFFF;
                ltfrac += a_tstepxfrac;
                if (ltfrac & 0x10000) {
                    lptex += r_affinetridesc.skinwidth;
                    ltfrac &= 0xFFFF;
                }
            } while (--lcount);
        }

        pspanpackage++;
    } while (pspanpackage->count != -999999);
}

void D_RasterizeAliasPolySmooth()
{
    int initialleftheight, initialrightheight;
    const eastl::array<int, 6> *plefttop, *prighttop, *pleftbottom, *prightbottom;
    int working_lstepx, originalcount;

    plefttop = pedgetable->pleftedgevert0;
    prighttop = pedgetable->prightedgevert0;

    pleftbottom = pedgetable->pleftedgevert1;
    prightbottom = pedgetable->prightedgevert1;

    initialleftheight = (*pleftbottom)[1] - (*plefttop)[1];
    initialrightheight = (*prightbottom)[1] - (*prighttop)[1];

    D_PolysetCalcGradients(r_affinetridesc.skinwidth);

    d_pedgespanpackage = a_spans;

    ystart = (*plefttop)[1];
    d_aspancount = (*plefttop)[0] - (*prighttop)[0];

    d_ptex = reinterpret_cast<byte*>(r_affinetridesc.pskin) + ((*plefttop)[2] >> 16) + ((*plefttop)[3] >> 16) * r_affinetridesc.skinwidth;
    d_sfrac = (*plefttop)[2] & 0xFFFF;
    d_tfrac = (*plefttop)[3] & 0xFFFF;
    d_light = (*plefttop)[4];
    d_zi = (*plefttop)[5];

    d_pdest = reinterpret_cast<byte*>(d_viewbuffer) + ystart * screenwidth + (*plefttop)[0];
    d_pz = d_pzbuffer + ystart * d_zwidth + (*plefttop)[0];

    if (initialleftheight == 1) {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;

        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;

        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;

        d_pedgespanpackage++;
    } else {
        D_PolysetSetUpForLineScan((*plefttop)[0], (*plefttop)[1], (*pleftbottom)[0],
            (*pleftbottom)[1]);

        d_pzbasestep = d_zwidth + ubasestep;
        d_pzextrastep = d_pzbasestep + 1;

        d_pdestbasestep = screenwidth + ubasestep;
        d_pdestextrastep = d_pdestbasestep + 1;

        if (ubasestep < 0) {
            working_lstepx = r_lstepx - 1;
        } else {
            working_lstepx = r_lstepx;
        }

        d_countextrastep = ubasestep + 1;
        d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) + ((r_tstepy + r_tstepx * ubasestep) >> 16) * r_affinetridesc.skinwidth;
        d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
        d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
        d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
        d_zibasestep = r_zistepy + r_zistepx * ubasestep;

        d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) + ((r_tstepy + r_tstepx * d_countextrastep) >> 16) * r_affinetridesc.skinwidth;
        d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
        d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
        d_lightextrastep = d_lightbasestep + working_lstepx;
        d_ziextrastep = d_zibasestep + r_zistepx;

        D_PolysetScanLeftEdge(initialleftheight);
    }

    if (pedgetable->numleftedges == 2) {
        int height;

        plefttop = pleftbottom;
        pleftbottom = pedgetable->pleftedgevert2;

        height = (*pleftbottom)[1] - (*plefttop)[1];

        ystart = (*plefttop)[1];
        d_aspancount = (*plefttop)[0] - (*prighttop)[0];
        d_ptex = reinterpret_cast<byte*>(r_affinetridesc.pskin) + ((*plefttop)[2] >> 16) + ((*plefttop)[3] >> 16) * r_affinetridesc.skinwidth;
        d_sfrac = 0;
        d_tfrac = 0;
        d_light = (*plefttop)[4];
        d_zi = (*plefttop)[5];

        d_pdest = reinterpret_cast<byte*>(d_viewbuffer) + ystart * screenwidth + (*plefttop)[0];
        d_pz = d_pzbuffer + ystart * d_zwidth + (*plefttop)[0];

        if (height == 1) {
            d_pedgespanpackage->pdest = d_pdest;
            d_pedgespanpackage->pz = d_pz;
            d_pedgespanpackage->count = d_aspancount;
            d_pedgespanpackage->ptex = d_ptex;

            d_pedgespanpackage->sfrac = d_sfrac;
            d_pedgespanpackage->tfrac = d_tfrac;

            d_pedgespanpackage->light = d_light;
            d_pedgespanpackage->zi = d_zi;

            d_pedgespanpackage++;
        } else {
            D_PolysetSetUpForLineScan((*plefttop)[0], (*plefttop)[1], (*pleftbottom)[0],
                (*pleftbottom)[1]);

            d_pdestbasestep = screenwidth + ubasestep;
            d_pdestextrastep = d_pdestbasestep + 1;
            d_pzbasestep = d_zwidth + ubasestep;
            d_pzextrastep = d_pzbasestep + 1;

            if (ubasestep < 0) {
                working_lstepx = r_lstepx - 1;
            } else {
                working_lstepx = r_lstepx;
            }

            d_countextrastep = ubasestep + 1;
            d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) + ((r_tstepy + r_tstepx * ubasestep) >> 16) * r_affinetridesc.skinwidth;
            d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
            d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
            d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
            d_zibasestep = r_zistepy + r_zistepx * ubasestep;

            d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) + ((r_tstepy + r_tstepx * d_countextrastep) >> 16) * r_affinetridesc.skinwidth;
            d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
            d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
            d_lightextrastep = d_lightbasestep + working_lstepx;
            d_ziextrastep = d_zibasestep + r_zistepx;

            D_PolysetScanLeftEdge(height);
        }
    }

    d_pedgespanpackage = a_spans;

    D_PolysetSetUpForLineScan((*prighttop)[0], (*prighttop)[1], (*prightbottom)[0],
        (*prightbottom)[1]);
    d_aspancount = 0;
    d_countextrastep = ubasestep + 1;
    originalcount = a_spans[initialrightheight].count;
    a_spans[initialrightheight].count = -999999;
    D_PolysetDrawSpans8(a_spans);

    if (pedgetable->numrightedges == 2) {
        int height;
        spanpackage_t* pstart;

        pstart = a_spans + initialrightheight;
        pstart->count = originalcount;

        d_aspancount = (*prightbottom)[0] - (*prighttop)[0];

        prighttop = prightbottom;
        prightbottom = pedgetable->prightedgevert2;

        height = (*prightbottom)[1] - (*prighttop)[1];

        D_PolysetSetUpForLineScan((*prighttop)[0], (*prighttop)[1], (*prightbottom)[0],
            (*prightbottom)[1]);

        d_countextrastep = ubasestep + 1;
        a_spans[initialrightheight + height].count = -999999;
        D_PolysetDrawSpans8(pstart);
    }
}

void D_PolysetSetEdgeTable()
{
    int edgetableindex;

    edgetableindex = 0;

    if (r_p0[1] >= r_p1[1]) {
        if (r_p0[1] == r_p1[1]) {
            if (r_p0[1] < r_p2[1]) {
                pedgetable = &edgetables[2];
            } else {
                pedgetable = &edgetables[5];
            }

            return;
        } else {
            edgetableindex = 1;
        }
    }

    if (r_p0[1] == r_p2[1]) {
        if (edgetableindex) {
            pedgetable = &edgetables[8];
        } else {
            pedgetable = &edgetables[9];
        }

        return;
    } else if (r_p1[1] == r_p2[1]) {
        if (edgetableindex) {
            pedgetable = &edgetables[10];
        } else {
            pedgetable = &edgetables[11];
        }

        return;
    }

    if (r_p0[1] > r_p2[1]) {
        edgetableindex += 2;
    }

    if (r_p1[1] > r_p2[1]) {
        edgetableindex += 4;
    }

    pedgetable = &edgetables[edgetableindex];
}

// ==============================================================
// d_part.cpp -- software particle drawing
// ==============================================================

void D_EndParticles()
{
}

void D_StartParticles()
{
}

void D_DrawParticle(particle_t* pparticle)
{
    Vector3 local, transformed;
    float zi;
    byte* pdest;
    short* pz;
    int i, izi, pix, count, u, v;

    local = pparticle->org - r_origin;

    transformed.x = local.dot(r_pright);
    transformed.y = local.dot(r_pup);
    transformed.z = local.dot(r_ppn);

    if (transformed.z < PARTICLE_Z_CLIP) {
        return;
    }

    zi = 1.0f / transformed.z;
    u = static_cast<int>(xcenter + zi * transformed.x + 0.5f);
    v = static_cast<int>(ycenter - zi * transformed.y + 0.5f);

    if ((v > d_vrectbottom_particle) || (u > d_vrectright_particle) || (v < d_vrecty) || (u < d_vrectx)) {
        return;
    }

    pz = d_pzbuffer + (d_zwidth * v) + u;
    pdest = reinterpret_cast<byte*>(d_viewbuffer) + d_scantable[v] + u;
    izi = static_cast<int>(zi * 32768.0f);

    pix = izi >> d_pix_shift;

    if (pix < d_pix_min) {
        pix = d_pix_min;
    } else if (pix > d_pix_max) {
        pix = d_pix_max;
    }

    switch (pix) {
    case 1:
        count = 1 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    case 2:
        count = 2 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }

            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    case 3:
        count = 3 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }

            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }

            if (pz[2] <= izi) {
                pz[2] = static_cast<short>(izi);
                pdest[2] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    case 4:
        count = 4 << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            if (pz[0] <= izi) {
                pz[0] = static_cast<short>(izi);
                pdest[0] = static_cast<byte>(pparticle->color);
            }

            if (pz[1] <= izi) {
                pz[1] = static_cast<short>(izi);
                pdest[1] = static_cast<byte>(pparticle->color);
            }

            if (pz[2] <= izi) {
                pz[2] = static_cast<short>(izi);
                pdest[2] = static_cast<byte>(pparticle->color);
            }

            if (pz[3] <= izi) {
                pz[3] = static_cast<short>(izi);
                pdest[3] = static_cast<byte>(pparticle->color);
            }
        }
        break;

    default:
        count = pix << d_y_aspect_shift;

        for (; count; count--, pz += d_zwidth, pdest += screenwidth) {
            for (i = 0; i < pix; i++) {
                if (pz[i] <= izi) {
                    pz[i] = static_cast<short>(izi);
                    pdest[i] = static_cast<byte>(pparticle->color);
                }
            }
        }
        break;
    }
}

} // namespace Render
