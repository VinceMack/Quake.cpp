// sw_local.hpp -- Internal Software Renderer Common Declarations and Structures
#pragma once

#include "render/render_types.hpp"
#include "core/types.hpp"
#include "core/cvar.hpp"
#include "core/math.hpp"
#include "world/bsp_format.hpp"
#include "world/model.hpp"

#include <EASTL/array.h>

inline constexpr int SCANBUFFERPAD = 0x1000, R_SKY_SMASK = 0x007F0000, R_SKY_TMASK = 0x007F0000;
inline constexpr int DS_SPAN_LIST_END = -128, SURFCACHE_SIZE_AT_320X200 = 600 * 1024, BMODEL_FULLY_CLIPPED = 0x10;
inline constexpr double ALIAS_BASE_SIZE_RATIO = 1.0 / 11.0;
inline constexpr double NEAR_CLIP = 0.01;
inline constexpr unsigned int FULLY_CLIPPED_CACHED = 0x80000000;
inline constexpr unsigned int FRAMECOUNT_MASK = 0x7FFFFFFF;
inline constexpr double XCENTERING = 0.5;
inline constexpr double YCENTERING = 0.5;

struct surfcache_s {
    surfcache_s* next = nullptr;
    surfcache_s** owner = nullptr;
    int lightadj[MAXLIGHTMAPS]{};
    int dlight = 0;
    int size = 0;
    unsigned width = 0;
    unsigned height = 0;
    float mipscale = 0.0f;
    texture_s* texture = nullptr;
    byte data[4]{};
};
using surfcache_t = surfcache_s;

struct sspan_s {
    int u = 0, v = 0, count = 0;
};
using sspan_t = sspan_s;

struct espan_t {
    int u = 0, v = 0, count = 0;
    espan_t* pnext = nullptr;
};

inline constexpr int MAXVERTS = 16, MAXWORKINGVERTS = MAXVERTS + 4, MAXHEIGHT = 2160, MAXWIDTH = 3840;
inline constexpr int MAXDIMENSION = (MAXHEIGHT > MAXWIDTH) ? MAXHEIGHT : MAXWIDTH, CYCLE = 256, SIN_BUFFER_SIZE = MAXDIMENSION + CYCLE, INFINITE_DISTANCE = 0x10000;
inline constexpr int NUMSTACKEDGES = 2400, MINEDGES = NUMSTACKEDGES, NUMSTACKSURFACES = 800, MINSURFACES = NUMSTACKSURFACES, MAXSPANS = 8000;

struct surf_t {
    surf_t* next = nullptr;
    surf_t* prev = nullptr;
    espan_t* spans = nullptr;
    int key = 0;
    int last_u = 0;
    int spanstate = 0;
    int flags = 0;
    void* data = nullptr;
    entity_t* entity = nullptr;
    float nearzi = 0.0f;
    qboolean insubmodel = {};
    float d_ziorigin = 0.0f, d_zistepu = 0.0f, d_zistepv = 0.0f;
    eastl::array<int, 2> pad{};
};

inline constexpr int ALIAS_LEFT_CLIP = 0x0001, ALIAS_TOP_CLIP = 0x0002, ALIAS_RIGHT_CLIP = 0x0004;
inline constexpr int ALIAS_BOTTOM_CLIP = 0x0008, ALIAS_Z_CLIP = 0x0010, ALIAS_XY_CLIP_MASK = 0x000F;
inline constexpr int ALIAS_Z_CLIP_PLANE = 5;

struct edge_t {
    int64_t u = 0;
    int64_t u_step = 0;
    edge_t* prev = nullptr;
    edge_t* next = nullptr;
    eastl::array<unsigned short, 2> surfs{};
    edge_t* nextremove = nullptr;
    float nearzi = 0.0f;
    medge_t* owner = nullptr;
};

struct alight_t {
    int ambientlight = 0;
    int shadelight = 0;
    float* plightvec = nullptr;
};

struct bedge_t {
    eastl::array<mvertex_t*, 2> v{};
    bedge_t* pnext = nullptr;
};

struct auxvert_t {
    eastl::array<float, 3> fv{};
};

inline constexpr int WARP_WIDTH = 320;
inline constexpr int WARP_HEIGHT = 200;
inline constexpr int MAX_LBM_HEIGHT = 480;

struct emitpoint_t {
    float u = 0.0f;
    float v = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
    float zi = 0.0f;
};

enum class ptype_t {
    Static, Grav, SlowGrav, Fire, Explode, Explode2, Blob, Blob2
};

struct particle_t {
    Vector3 org{};
    float color = 0.0f;
    particle_t* next = nullptr;
    Vector3 vel{};
    float ramp = 0.0f;
    float die = 0.0f;
    ptype_t type = ptype_t::Static;
};

inline constexpr float PARTICLE_Z_CLIP = 8.0f;

struct polyvert_t {
    float u = 0.0f;
    float v = 0.0f;
    float zi = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
};

struct polydesc_t {
    int numverts = 0;
    float nearzi = 0.0f;
    msurface_t* pcurrentface = nullptr;
    polyvert_t* pverts = nullptr;
};

struct finalvert_t {
    eastl::array<int, 6> v{};
    int flags = 0;
    float reserved = 0.0f;
};

struct affinetridesc_t {
    void* pskin = nullptr;
    maliasskindesc_t* pskindesc = nullptr;
    int skinwidth = 0;
    int skinheight = 0;
    mtriangle_t* ptriangles = nullptr;
    finalvert_t* pfinalverts = nullptr;
    int numtriangles = 0;
    int drawtype = 0;
    int seamfixupX16 = 0;
};

struct screenpart_t {
    float u = 0.0f;
    float v = 0.0f;
    float zi = 0.0f;
    float color = 0.0f;
};

struct spritedesc_t {
    int nump = 0;
    emitpoint_t* pverts = nullptr;
    mspriteframe_t* pspriteframe = nullptr;
    Vector3 vup{}, vright{}, vpn{};
    float nearzi = 0.0f;
};

struct zpointdesc_t {
    int u = 0;
    int v = 0;
    float zi = 0.0f;
    int color = 0;
};

struct btofpoly_t {
    int clipflags = 0;
    msurface_t* psurf = nullptr;
};
inline constexpr int MAX_BTOFPOLYS = 5000;
inline constexpr double BACKFACE_EPSILON = 0.01;

struct clipplane_t {
    Vector3 normal{};
    float dist = 0.0f;
    clipplane_t* next = nullptr;
    uint8_t leftedge = 0;
    uint8_t rightedge = 0;
    eastl::array<uint8_t, 2> reserved{};
};

inline constexpr int SKYSHIFT = 7;
inline constexpr int SKYSIZE = 1 << SKYSHIFT;
inline constexpr int SKYMASK = SKYSIZE - 1;

inline constexpr int AMP = 8 * 0x10000;
inline constexpr int AMP2 = 3;
inline constexpr int SPEED = 20;

inline constexpr int DR_SOLID = 0;
inline constexpr int DR_TRANSPARENT = 1;
inline constexpr int TRANSPARENT_COLOR = 0xFF;

struct drawsurf_t {
    pixel_t* surfdat = nullptr;
    int rowbytes = 0;
    msurface_t* surf = nullptr;
    eastl::array<fixed8_t, MAXLIGHTMAPS> lightadj{};
    texture_t* texture = nullptr;
    int surfmip = 0;
    int surfwidth = 0;
    int surfheight = 0;
};

namespace Render {

extern refdef_t r_refdef;
extern Vector3 r_origin, vpn, vright, vup;
extern texture_s* r_notexture_mip;

extern bool r_cache_thrash;
extern int d_spanpixcount;
extern int r_framecount;
extern qboolean r_drawpolys;
extern qboolean r_drawculledpolys;
extern qboolean r_worldpolysbacktofront;
extern qboolean r_recursiveaffinetriangles;
extern float r_aliasuvscale;
extern qboolean r_dowarp;

extern affinetridesc_t r_affinetridesc;
extern spritedesc_t r_spritedesc;
extern zpointdesc_t r_zpointdesc;
extern polydesc_t r_polydesc;
extern int d_con_indirect;
extern Vector3 r_pright, r_pup, r_ppn;
extern void* acolormap;
extern drawsurf_t r_drawsurf;

extern int r_skydirect;
extern byte* r_skysource;
extern float skyspeed, skyspeed2;
extern float skytime;
extern int c_surf;
extern byte* r_warpbuffer;

inline constexpr int NUMVERTEXNORMALS = 162;
extern eastl::array<eastl::array<float, 3>, NUMVERTEXNORMALS> r_avertexnormals;
extern float xscaleshrink, yscaleshrink;

void D_StartParticles();
void D_EndParticles();
void D_DrawParticle(particle_t* pparticle);
void D_DrawPoly();
void D_DrawSurfaces();
void D_DrawSprite();
void D_PolysetDraw();
void D_PolysetDrawFinalVerts(finalvert_t* fv, int numverts);
void D_PolysetUpdateTables();

// Common variables shared across software rendering units
extern int r_bmodelactive;
extern mnode_t* r_pefragtopnode;
extern Vector3 r_emins, r_emaxs;
extern int r_dlightframecount;
extern int c_faceclip;
extern eastl::array<clipplane_t, 4> view_clipplanes;
extern edge_t* auxedges;
extern edge_t *r_edges, *edge_p, *edge_max;
extern surf_t *surfaces, *surface_p, *surf_max;
extern Vector3 modelorg, base_modelorg;
extern Vector3 base_vpn, base_vright, base_vup;
extern float xcenter, ycenter;
extern float xscale, yscale;
extern float xscaleinv, yscaleinv;
extern int screenwidth;
extern unsigned int d_zrowbytes, d_zwidth;
extern short* d_pzbuffer;
extern float pixelAspect;
extern int r_drawnpolycount;
extern eastl::array<int, SIN_BUFFER_SIZE> sintable;
extern eastl::array<int, SIN_BUFFER_SIZE> intsintable;
extern int r_emitted;
extern float r_nearzi;
extern float r_u1, r_v1, r_lzi1;
extern int r_ceilv1;
extern bool r_lastvertvalid;
extern bool r_nearzionly;
extern bool insubmodel;

void TransformVector(const Vector3& in, Vector3& out);
void R_TransformFrustum();
void R_StoreEfrags(efrag_t** ppefrag);
extern eastl::array<edge_t*, MAXHEIGHT> newedges;
extern eastl::array<edge_t*, MAXHEIGHT> removeedges;
extern int r_currentkey;
extern edge_t edge_head, edge_tail, edge_aftertail;
extern Vector3 r_entorigin;
extern float entity_rotation[3][3];
extern Vector3 r_worldmodelorg;
extern int r_currentbkey;
extern mdl_t* pmdl;
extern aliashdr_t* paliashdr;
extern finalvert_t* pfinalverts;
extern auxvert_t* pauxverts;
extern int r_amodels_drawn;
extern int a_skinwidth;
extern float r_time1;
extern int r_numallocatededges;
extern int r_outofsurfaces, r_outofedges;
extern bool r_dowarpold, r_viewchanged;
extern int numbtofpolys;
extern btofpoly_t* pbtofpolys;
extern mvertex_t* r_pcurrentvertbase;
extern int r_maxsurfsseen, r_maxedgesseen, r_cnumsurfs;
extern bool r_surfsonstack;
extern int r_clipflags;
extern bool r_fov_greater_than_90;
extern float aliasxscale, aliasyscale, aliasxcenter, aliasycenter;
extern float screenAspect, verticalFieldOfView, xOrigin, yOrigin;
extern eastl::array<mplane_t, 4> screenedge;
extern int r_visframecount, r_polycount, r_wholepolycount;
extern eastl::array<int*, 4> pfrustum_indexes;
extern eastl::array<int, 4 * 6> r_frustum_indexes;
extern mleaf_t *r_viewleaf, *r_oldviewleaf;
extern float r_aliastransition, r_resfudge;
extern float dp_time1, dp_time2, db_time1, db_time2, rw_time1, rw_time2;
extern float se_time1, se_time2, de_time1, de_time2, dv_time1, dv_time2;

extern cvar_t r_draworder, r_speeds, r_timegraph, r_graphheight, r_waterwarp;
extern cvar_t r_fullbright, r_drawentities, r_aliasstats, r_dspeeds, r_ambient;
extern cvar_t r_reportsurfout, r_maxsurfs, r_numsurfs, r_reportedgeout, r_maxedges, r_numedges;
extern cvar_t r_clearcolor, r_drawflat, r_drawviewmodel;
extern cvar_t r_aliastransbase, r_aliastransadj;

extern efrag_t** lastlink;
extern entity_t* r_addent;

// Rasterizer shared variables
extern float d_sdivzstepu, d_tdivzstepu, d_zistepu;
extern float d_sdivzstepv, d_tdivzstepv, d_zistepv;
extern float d_sdivzorigin, d_tdivzorigin, d_ziorigin;
extern fixed16_t sadjust, tadjust, bbextents, bbextentt;
extern pixel_t* cacheblock;
extern int cachewidth;
extern pixel_t* d_viewbuffer;
extern int d_minmip;
extern eastl::array<float, 3> d_scalemip;
extern void (*d_drawspans)(espan_t* pspan);
extern int d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;
extern int d_y_aspect_shift, d_pix_min, d_pix_max, d_pix_shift;
extern eastl::array<int, MAXHEIGHT> d_scantable;
extern eastl::array<short*, MAXHEIGHT> zspantable;
extern int miplevel;
extern float scale_for_mip;
extern int ubasestep, errorterm, erroradjustup, erroradjustdown;
extern Vector3 transformed_modelorg;
extern float surfscale;
extern int sc_size;
extern surfcache_t* sc_rover;
extern surfcache_t* sc_base;
extern surfcache_t* d_initial_rover;
extern qboolean d_roverwrapped;
extern entity_t* currententity;
extern eastl::array<int, 256> d_lightstylevalue;
extern unsigned char* r_turb_pbase;
extern unsigned char* r_turb_pdest;
extern fixed16_t r_turb_s, r_turb_t, r_turb_sstep, r_turb_tstep;
extern int* r_turb_turb;
extern int r_turb_spancount;
extern int r_sstepx, r_tstepx, r_lstepy, r_sstepy, r_tstepy;
extern int r_zistepx, r_zistepy;

} // namespace Render

using Render::d_pzbuffer;
