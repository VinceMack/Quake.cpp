// renderer.hpp -- public interface to refresh functions
#pragma once

#define MAXCLIPPLANES 11

#define TOP_RANGE 16 // soldier uniform colors
#define BOTTOM_RANGE 96

//=============================================================================

typedef struct efrag_s {
    struct mleaf_s* leaf;
    struct efrag_s* leafnext;
    struct entity_s* entity;
    struct efrag_s* entnext;
} efrag_t;

typedef struct entity_s {
    qboolean forcelink = false; // model changed

    int update_type = 0;

    entity_state_t baseline = {}; // to fill in defaults in updates

    double msgtime = 0.0;        // time of last update
    Vector3 msg_origins[2]; // last two updates (0 is newest)
    Vector3 origin;
    Vector3 msg_angles[2]; // last two updates (0 is newest)
    Vector3 angles;
    struct model_s* model = nullptr; // NULL = no model
    struct efrag_s* efrag = nullptr; // linked list of efrags
    int frame = 0;
    float syncbase = 0.0f; // for client-side animations
    byte* colormap = nullptr;
    int effects = 0;  // light, particals, etc
    int skinnum = 0;  // for Alias models
    int visframe = 0; // last frame this entity was
    //  found in an active leaf

    int dlightframe = 0; // dynamic lighting
    int dlightbits = 0;

    // FIXME: could turn these into a union
    int trivial_accept = 0;
    struct mnode_s* topnode = nullptr; // for bmodels, first world node
                             //  that splits bmodel, or NULL if
                             //  not split
} entity_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct refdef_s {
    vrect_t vrect = {}; // subwindow in video for refresh
    // FIXME: not need vrect next field here?
    vrect_t aliasvrect = {};                    // scaled Alias version
    int vrectright = 0, vrectbottom = 0;           // right & bottom screen coords
    int aliasvrectright = 0, aliasvrectbottom = 0; // scaled Alias versions
    float vrectrightedge = 0.0f;                  // rightmost right edge we care about,
    //  for use in edge list
    float fvrectx = 0.0f, fvrecty = 0.0f;         // for floating-point compares
    float fvrectx_adj = 0.0f, fvrecty_adj = 0.0f; // left and top edges, for clamping
    int64_t vrect_x_adj_shift20 = 0;        // (vrect.x + 0.5 - epsilon) << 20
    int64_t vrectright_adj_shift20 = 0;     // (vrectright + 0.5 - epsilon) << 20
    float fvrectright_adj = 0.0f, fvrectbottom_adj = 0.0f;
    // right and bottom edges, for clamping
    float fvrectright = 0.0f;           // rightmost edge, for Alias clamping
    float fvrectbottom = 0.0f;          // bottommost edge, for Alias clamping
    float horizontalFieldOfView = 0.0f; // at Z = 1.0, this many X is visible
    // 2.0 = 90 degrees
    float xOrigin = 0.0f; // should probably allways be 0.5
    float yOrigin = 0.0f; // between be around 0.3 to 0.5

    Vector3 vieworg;
    Vector3 viewangles;

    float fov_x = 0.0f, fov_y = 0.0f;

    int ambientlight = 0;
} refdef_t;

struct texture_s;

namespace Render {

//
// refresh
//

extern refdef_t r_refdef;
extern Vector3 r_origin, vpn, vright, vup;

extern struct texture_s* r_notexture_mip;

void R_Init(void);
void R_InitTextures(void);
void R_InitEfrags(void);
void R_RenderView(void);
void R_ViewChanged(vrect_t* pvrect, int lineadj, float aspect);
void R_InitSky(struct texture_s* mt);

void R_AddEfrags(entity_t* ent);
void R_RemoveEfrags(entity_t* ent);

void R_NewMap(void);

void R_ParseParticleEffect(void);
void R_RunParticleEffect(const Vector3& org, const Vector3& dir, int color, int count);
void R_RocketTrail(Vector3 start, const Vector3& end, int type);

void R_EntityParticles(entity_t* ent);
void R_BlobExplosion(const Vector3& org);
void R_ParticleExplosion(const Vector3& org);
void R_ParticleExplosion2(const Vector3& org, int colorStart, int colorLength);
void R_LavaSplash(const Vector3& org);
void R_TeleportSplash(const Vector3& org);

void R_PushDlights(void);

//
// surface cache related
//
extern qboolean r_cache_thrash;

int D_SurfaceCacheForRes(int width, int height);
void D_FlushCaches(void);
void D_DeleteSurfaceCache(void);
void D_InitCaches(void* buffer, int size);
void R_SetVrect(vrect_t* pvrectin, vrect_t* pvrect, int lineadj);

} // namespace Render
