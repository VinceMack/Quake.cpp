// rasterizer.hpp: interface header file for rasterization driver modules
#pragma once

#define WARP_WIDTH 320
#define WARP_HEIGHT 200

#define MAX_LBM_HEIGHT 480

typedef struct {
    float u, v;
    float s, t;
    float zi;
} emitpoint_t;

typedef enum {
    pt_static,
    pt_grav,
    pt_slowgrav,
    pt_fire,
    pt_explode,
    pt_explode2,
    pt_blob,
    pt_blob2
} ptype_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s {
    // driver-usable fields
    vec3_t org;
    float color;
    // drivers never touch the following fields
    struct particle_s* next;
    vec3_t vel;
    float ramp;
    float die;
    ptype_t type;
} particle_t;

#define PARTICLE_Z_CLIP 8.0

typedef struct polyvert_s {
    float u, v, zi, s, t;
} polyvert_t;

typedef struct polydesc_s {
    int numverts;
    float nearzi;
    msurface_t* pcurrentface;
    polyvert_t* pverts;
} polydesc_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct finalvert_s {
    int v[6]; // u, v, s, t, l, 1/z
    int flags;
    float reserved;
} finalvert_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct {
    void* pskin;
    maliasskindesc_t* pskindesc;
    int skinwidth;
    int skinheight;
    mtriangle_t* ptriangles;
    finalvert_t* pfinalverts;
    int numtriangles;
    int drawtype;
    int seamfixupX16;
} affinetridesc_t;

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct {
    float u, v, zi, color;
} screenpart_t;

typedef struct {
    int nump;
    emitpoint_t* pverts; // there's room for an extra element at [nump],
    //  if the driver wants to duplicate element [0] at
    //  element [nump] to avoid dealing with wrapping
    mspriteframe_t* pspriteframe;
    vec3_t vup, vright, vpn; // in worldspace
    float nearzi;
} spritedesc_t;

typedef struct {
    int u, v;
    float zi;
    int color;
} zpointdesc_t;

#define SKYSHIFT 7
#define SKYSIZE (1 << SKYSHIFT)
#define SKYMASK (SKYSIZE - 1)

namespace Render {

extern cvar_t r_drawflat;
extern int d_spanpixcount;
extern int r_framecount;
extern qboolean r_drawpolys;
extern qboolean r_drawculledpolys;
extern qboolean r_worldpolysbacktofront;
extern qboolean r_recursiveaffinetriangles;
extern float r_aliasuvscale;
extern int r_pixbytes;
extern qboolean r_dowarp;

extern affinetridesc_t r_affinetridesc;
extern spritedesc_t r_spritedesc;
extern zpointdesc_t r_zpointdesc;
extern polydesc_t r_polydesc;

extern int d_con_indirect;

extern vec3_t r_pright, r_pup, r_ppn;

void D_Aff8Patch(void* pcolormap);
inline void D_EnableBackBufferAccess(void)
{
    VID_LockBuffer();
}

inline void D_DisableBackBufferAccess(void)
{
    VID_UnlockBuffer();
}

void D_PolysetDraw(void);
void D_PolysetDrawFinalVerts(finalvert_t* fv, int numverts);
void D_DrawParticle(particle_t* pparticle);
void D_DrawPoly(void);
void D_DrawSprite(void);
void D_DrawSurfaces(void);
void D_EndParticles(void);
void D_Init(void);
void D_ViewChanged(void);
void D_SetupFrame(void);
void D_StartParticles(void);
void D_TurnZOn(void);
void D_WarpScreen(void);

void D_DrawRect(void);
void D_UpdateRects(vrect_t* prect);

void D_PolysetUpdateTables(void);

extern int r_skydirect;
extern byte* r_skysource;

// transparency types for D_DrawRect ()
#define DR_SOLID 0
#define DR_TRANSPARENT 1
#define TRANSPARENT_COLOR 0xFF

extern void* acolormap; // FIXME: should go away

//=======================================================================//

// callbacks to Quake

typedef struct {
    pixel_t* surfdat; // destination for generated surface
    int rowbytes;     // destination logical width in bytes
    msurface_t* surf; // description for surface to generate
    fixed8_t lightadj[MAXLIGHTMAPS];
    // adjust for lightmap levels for dynamic lighting
    texture_t* texture; // corrected for animating textures
    int surfmip;        // mipmapped ratio of surface texels / world pixels
    int surfwidth;      // in mipmapped texels
    int surfheight;     // in mipmapped texels
} drawsurf_t;

extern drawsurf_t r_drawsurf;

void R_DrawSurface(void);

extern float skyspeed, skyspeed2;
extern float skytime;

extern int c_surf;
extern byte* r_warpbuffer;

} // namespace Render
