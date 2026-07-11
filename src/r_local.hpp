// r_local.h -- private refresh defs
#pragma once


#include "r_shared.hpp"

#define ALIAS_BASE_SIZE_RATIO (1.0 / 11.0)
// normalizing factor so player model works out to about
//  1 pixel per triangle

#define BMODEL_FULLY_CLIPPED 0x10 // value returned by R_BmodelCheckBBox ()

//  if bbox is trivially rejected

//===========================================================================
// viewmodel lighting

typedef struct {
    int ambientlight;
    int shadelight;
    float* plightvec;
} alight_t;

//===========================================================================
// clipped bmodel edges

typedef struct bedge_s {
    mvertex_t* v[2];
    struct bedge_s* pnext;
} bedge_t;

typedef struct {
    float fv[3]; // viewspace x, y
} auxvert_t;

//===========================================================================

namespace Render {

extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;

#define XCENTERING (1.0 / 2.0)
#define YCENTERING (1.0 / 2.0)

#define CLIP_EPSILON 0.001

#define BACKFACE_EPSILON 0.01

//===========================================================================

#define DIST_NOT_SET 98765

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct clipplane_s {
    Vector3 normal;
    float dist;
    struct clipplane_s* next;
    byte leftedge;
    byte rightedge;
    byte reserved[2];
} clipplane_t;

//=============================================================================

void R_RenderWorld(void);

//=============================================================================

extern Vector3 r_origin;

//=============================================================================

void R_ClearPolyList(void);
void R_DrawPolyList(void);

//
// current entity info
//
extern qboolean insubmodel;

void R_DrawSprite(void);
void R_RenderFace(msurface_t* fa, int clipflags);
void R_RenderPoly(msurface_t* fa, int clipflags);
void R_RenderBmodelFace(bedge_t* pedges, msurface_t* psurf);
void R_TransformFrustum(void);
void R_SetSkyFrame(void);
void R_DrawSurfaceBlock16(void);
void R_DrawSurfaceBlock8(void);
texture_t* R_TextureAnimation(texture_t* base);

void R_DrawSubmodelPolygons(model_t* pmodel, int clipflags);
void R_DrawSolidClippedSubmodelPolygons(model_t* pmodel);

void R_AddPolygonEdges(emitpoint_t* pverts, int numverts, int miplevel);
surf_t* R_GetSurf(void);
void R_AliasDrawModel(alight_t* plighting);
void R_BeginEdgeFrame(void);
void R_ScanEdges(void);
void D_DrawSurfaces(void);
void R_InsertNewEdges(edge_t* edgestoadd, edge_t* edgelist);
void R_StepActiveU(edge_t* pedge);
void R_RemoveEdges(edge_t* pedge);

extern void R_RotateBmodel(void);

// !!! if this is changed, it must be changed in asm_draw.h too !!!
#define NEAR_CLIP 0.01

#define MAXBVERTINDEXES 1000 // new clipped vertices when clipping bmodels
//  to the world BSP

typedef struct btofpoly_s {
    int clipflags;
    msurface_t* psurf;
} btofpoly_t;

#define MAX_BTOFPOLYS 5000 // FIXME: tune this



void R_InitTurb(void);
void R_ZDrawSubmodelPolys(model_t* clmodel);

//=========================================================
// Alias models
//=========================================================

#define MAXALIASVERTS 2000 // TODO: tune this
#define ALIAS_Z_CLIP_PLANE 5



qboolean R_AliasCheckBBox(void);

//=========================================================
// turbulence stuff

#define AMP 8 * 0x10000
#define AMP2 3
#define SPEED 20

//=========================================================
// particle stuff

void R_DrawParticles(void);
void R_InitParticles(void);
void R_ClearParticles(void);
void R_ReadPointFile_f(void);



extern int screenwidth;



void R_AliasClipTriangle(mtriangle_t* ptri);



void R_StoreEfrags(efrag_t** ppefrag);
void R_TimeRefresh_f(void);
void R_TimeGraph(void);
void R_PrintAliasStats(void);
void R_PrintTimes(void);
void R_PrintDSpeeds(void);
void R_AnimateLight(void);
int R_LightPoint(const Vector3& p);
void R_SetupFrame(void);
void R_cshift_f(void);
void R_EmitEdge(mvertex_t* pv0, mvertex_t* pv1);
void R_ClipEdge(mvertex_t* pv0, mvertex_t* pv1, clipplane_t* clip);
void R_SplitEntityOnNode2(mnode_t* node);
void R_MarkLights(dlight_t* light, int bit, mnode_t* node);

} // namespace Render
