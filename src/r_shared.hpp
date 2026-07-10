// r_shared.h: general refresh-related stuff shared between the refresh and the
#pragma once
// driver

// FIXME: clean up and move into d_iface.h

#ifndef _R_SHARED_H_
#define _R_SHARED_H_

#define MAXVERTS 16                    // max points in a surface polygon
#define MAXWORKINGVERTS (MAXVERTS + 4) // max points in an intermediate
//  polygon (while processing)
// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define MAXHEIGHT 2160
#define MAXWIDTH 3840
#define MAXDIMENSION ((MAXHEIGHT > MAXWIDTH) ? MAXHEIGHT : MAXWIDTH)

#define CYCLE 256
#define SIN_BUFFER_SIZE (MAXDIMENSION + CYCLE)

#define INFINITE_DISTANCE 0x10000 // distance that's always guaranteed to
//  be farther away than anything in
//  the scene

//===================================================================

#define NUMSTACKEDGES 2400
#define MINEDGES NUMSTACKEDGES
#define NUMSTACKSURFACES 800
#define MINSURFACES NUMSTACKSURFACES
#define MAXSPANS 8000

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct espan_s {
    int u, v, count;
    struct espan_s* pnext;
} espan_t;

typedef struct surf_s {
    struct surf_s* next;
    struct surf_s* prev;
    struct espan_s* spans;
    int key;
    int last_u;
    int spanstate;
    int flags;
    void* data;
    entity_t* entity;
    float nearzi;
    qboolean insubmodel;
    float d_ziorigin, d_zistepu, d_zistepv;
    int pad[2];
} surf_t;

// flags in finalvert_t.flags
#define ALIAS_LEFT_CLIP 0x0001
#define ALIAS_TOP_CLIP 0x0002
#define ALIAS_RIGHT_CLIP 0x0004
#define ALIAS_BOTTOM_CLIP 0x0008
#define ALIAS_Z_CLIP 0x0010
#define ALIAS_ONSEAM 0x0020
#define ALIAS_XY_CLIP_MASK 0x000F

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct edge_s {
    int64_t u;
    int64_t u_step;
    struct edge_s *prev, *next;
    unsigned short surfs[2];
    struct edge_s* nextremove;
    float nearzi;
    medge_t* owner;
} edge_t;

namespace Render {

extern void R_DrawLine(polyvert_t* polyvert0, polyvert_t* polyvert1);

extern int screenwidth;

extern float pixelAspect;

extern int r_drawnpolycount;

extern int sintable[SIN_BUFFER_SIZE];
extern int intsintable[SIN_BUFFER_SIZE];

extern Vector3 vup, base_vup;
extern Vector3 vpn, base_vpn;
extern Vector3 vright, base_vright;
extern entity_t* currententity;

extern surf_t *surfaces, *surface_p, *surf_max;

extern Vector3 sxformaxis[4];
extern Vector3 txformaxis[4];

extern Vector3 modelorg, base_modelorg;

extern float xcenter, ycenter;
extern float xscale, yscale;
extern float xscaleinv, yscaleinv;
extern float xscaleshrink, yscaleshrink;

extern int d_lightstylevalue[256];

extern void TransformVector(const Vector3& in, Vector3& out);
extern void SetUpForLineScan(fixed8_t startvertu,
    fixed8_t startvertv,
    fixed8_t endvertu,
    fixed8_t endvertv);

extern int r_skymade;
extern void R_MakeSky(void);

} // namespace Render


#endif // _R_SHARED_H_


