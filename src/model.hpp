// model.h -- model structures (brush, alias, sprite) and loading declarations
#pragma once

#ifndef __MODEL__
#define __MODEL__

#include "modelgen.hpp"
#include "spritegn.hpp"

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/

//
// in memory representation
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct {
    Vector3 position;
} mvertex_t;

#define SIDE_FRONT 0
#define SIDE_BACK 1
#define SIDE_ON 2

// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct mplane_s {
    Vector3 normal;
    float dist = 0.0f;
    byte type = 0;     // for texture axis selection and fast side tests
    byte signbits = 0; // signx + signy<<1 + signz<<1
    byte pad[2] = {};
} mplane_t;

typedef struct texture_s {
    char name[16];
    unsigned width, height;
    int anim_total;                    // total tenths in sequence ( 0 = no)
    int anim_min, anim_max;            // time for this frame min <=time< max
    struct texture_s* anim_next;       // in the animation sequence
    struct texture_s* alternate_anims; // bmodels in frmae 1 use these
    unsigned offsets[MIPLEVELS];       // four mip maps stored
} texture_t;

#define SURF_PLANEBACK 2
#define SURF_DRAWSKY 4
#define SURF_DRAWSPRITE 8
#define SURF_DRAWTURB 0x10
#define SURF_DRAWTILED 0x20
#define SURF_DRAWBACKGROUND 0x40

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct {
    unsigned short v[2];
    unsigned int cachededgeoffset;
} medge_t;

typedef struct {
    float vecs[2][4];
    float mipadjust;
    texture_t* texture;
    int flags;
} mtexinfo_t;

typedef struct msurface_s {
    int visframe = 0; // should be drawn when node is crossed

    int dlightframe = 0;
    int dlightbits = 0;

    mplane_t* plane = nullptr;
    int flags = 0;

    int firstedge = 0; // look up in model->surfedges[], negative numbers
    int numedges = 0;  // are backwards edges

    // surface generation data
    struct surfcache_s* cachespots[MIPLEVELS] = {};

    short texturemins[2] = {};
    short extents[2] = {};

    mtexinfo_t* texinfo = nullptr;

    // lighting info
    byte styles[MAXLIGHTMAPS] = {};
    byte* samples = nullptr; // [numstyles*surfsize]
} msurface_t;

typedef struct mnode_s {
    // common with leaf
    int contents; // 0, to differentiate from leafs
    int visframe; // node needs to be traversed if current

    short minmaxs[6]; // for bounding box culling

    struct mnode_s* parent;

    // node specific
    mplane_t* plane;
    struct mnode_s* children[2];

    unsigned short firstsurface;
    unsigned short numsurfaces;
} mnode_t;

typedef struct mleaf_s {
    // common with node
    int contents = 0; // wil be a negative contents number
    int visframe = 0; // node needs to be traversed if current

    short minmaxs[6] = {}; // for bounding box culling

    struct mnode_s* parent = nullptr;

    // leaf specific
    byte* compressed_vis = nullptr;
    efrag_t* efrags = nullptr;

    msurface_t** firstmarksurface = nullptr;
    int nummarksurfaces = 0;
    int key = 0; // BSP sequence number for leaf's contents
    byte ambient_sound_level[NUM_AMBIENTS] = {};
} mleaf_t;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct hull_s {
    dclipnode_t* clipnodes = nullptr;
    mplane_t* planes = nullptr;
    int firstclipnode = 0;
    int lastclipnode = 0;
    Vector3 clip_mins;
    Vector3 clip_maxs;
} hull_t;

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

// FIXME: shorten these?
typedef struct mspriteframe_s {
    int width = 0;
    int height = 0;
    void* pcachespot = nullptr; // remove?
    float up = 0.0f, down = 0.0f, left = 0.0f, right = 0.0f;
    byte pixels[4] = {};
} mspriteframe_t;

typedef struct mspritegroup_s {
    int numframes = 0;
    float* intervals = nullptr;
    mspriteframe_t* frames[1] = {};
} mspritegroup_t;

typedef struct {
    spriteframetype_t type;
    mspriteframe_t* frameptr;
} mspriteframedesc_t;

typedef struct {
    int type;
    int maxwidth;
    int maxheight;
    int numframes;
    float beamlength; // remove?
    void* cachespot;  // remove?
    mspriteframedesc_t frames[1];
} msprite_t;

/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

typedef struct maliasframedesc_s {
    aliasframetype_t type = {};
    trivertx_t bboxmin = {};
    trivertx_t bboxmax = {};
    int frame = 0;
    char name[16] = {};
} maliasframedesc_t;

typedef struct {
    aliasskintype_t type;
    void* pcachespot;
    int skin;
} maliasskindesc_t;

typedef struct maliasgroupframedesc_s {
    trivertx_t bboxmin = {};
    trivertx_t bboxmax = {};
    int frame = 0;
} maliasgroupframedesc_t;

typedef struct maliasgroup_s {
    int numframes = 0;
    int intervals = 0;
    maliasgroupframedesc_t frames[1] = {};
} maliasgroup_t;

typedef struct {
    int numskins;
    int intervals;
    maliasskindesc_t skindescs[1];
} maliasskingroup_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct mtriangle_s {
    int facesfront;
    int vertindex[3];
} mtriangle_t;

typedef struct aliashdr_s {
    int model = 0;
    int stverts = 0;
    int skindesc = 0;
    int triangles = 0;
    maliasframedesc_t frames[1] = {};
} aliashdr_t;

//===================================================================

//
// Whole model
//

typedef enum { mod_brush,
    mod_sprite,
    mod_alias } modtype_t;

#define EF_ROCKET 1    // leave a trail
#define EF_GRENADE 2   // leave a trail
#define EF_GIB 4       // leave a trail
#define EF_ROTATE 8    // rotate (bonus items)
#define EF_TRACER 16   // green split trail
#define EF_ZOMGIB 32   // small blood trail
#define EF_TRACER2 64  // orange split trail + rotate
#define EF_TRACER3 128 // purple trail

typedef struct model_s {
    char name[MAX_QPATH] = {};
    int needload = 0; // bmodels and sprites don't cache normally

    modtype_t type = {};
    int numframes = 0;
    synctype_t synctype = {};

    int flags = 0;

    //
    // volume occupied by the model
    //
    Vector3 mins, maxs;
    float radius = 0.0f;

    //
    // brush model
    //
    int firstmodelsurface = 0, nummodelsurfaces = 0;

    int numsubmodels = 0;
    dmodel_t* submodels = nullptr;

    int numplanes = 0;
    mplane_t* planes = nullptr;

    int numleafs = 0; // number of visible leafs, not counting 0
    mleaf_t* leafs = nullptr;

    int numvertexes = 0;
    mvertex_t* vertexes = nullptr;

    int numedges = 0;
    medge_t* edges = nullptr;

    int numnodes = 0;
    mnode_t* nodes = nullptr;

    int numtexinfo = 0;
    mtexinfo_t* texinfo = nullptr;

    int numsurfaces = 0;
    msurface_t* surfaces = nullptr;

    int numsurfedges = 0;
    int* surfedges = nullptr;

    int numclipnodes = 0;
    dclipnode_t* clipnodes = nullptr;

    int nummarksurfaces = 0;
    msurface_t** marksurfaces = nullptr;

    hull_t hulls[MAX_MAP_HULLS] = {};

    int numtextures = 0;
    texture_t** textures = nullptr;

    byte* visdata = nullptr;
    byte* lightdata = nullptr;
    char* entities = nullptr;

    //
    // additional model data
    //
    cache_user_t cache = {}; // only access through Mod_Extradata

} model_t;

//============================================================================

namespace Model {

void Mod_Print(void);
void Mod_Init(void);
void Mod_ClearAll(void);
model_t* Mod_ForName(const char* name, qboolean crash);
void* Mod_Extradata(model_t* mod);
void Mod_TouchModel(char* name);

mleaf_t* Mod_PointInLeaf(const Vector3& p, model_t* model);
byte* Mod_LeafPVS(mleaf_t* leaf, model_t* model);

} // namespace Model


#endif // __MODEL__
