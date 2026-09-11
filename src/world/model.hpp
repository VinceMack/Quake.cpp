// model.hpp -- In-memory model structures (BSP, MDL, SPR) & Model management
#pragma once

#include <cstdint>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/string_view.h>
#include "sys_core.hpp"
#include "world/bsp_format.hpp"

//=============================================================================
// In-Memory Model Definitions
//=============================================================================

struct mvertex_t {
    Vector3 position{};
};

inline constexpr int SIDE_FRONT = 0, SIDE_BACK = 1, SIDE_ON = 2;

struct mplane_s {
    Vector3 normal{};
    float dist = 0.0f;
    byte type = 0;
    byte signbits = 0;
    byte pad[2]{};
};
using mplane_t = mplane_s;

struct texture_s {
    char name[16]{};
    unsigned width = 0, height = 0;
    int anim_total = 0;
    int anim_min = 0, anim_max = 0;
    texture_s* anim_next = nullptr;
    texture_s* alternate_anims = nullptr;
    unsigned offsets[MIPLEVELS]{};
};
using texture_t = texture_s;

inline constexpr int SURF_PLANEBACK = 2, SURF_DRAWSKY = 4, SURF_DRAWSPRITE = 8;
inline constexpr int SURF_DRAWTURB = 0x10, SURF_DRAWTILED = 0x20, SURF_DRAWBACKGROUND = 0x40;

struct medge_t {
    unsigned short v[2]{};
    unsigned int cachededgeoffset = 0;
};

struct mtexinfo_t {
    float vecs[2][4]{};
    float mipadjust = 0.0f;
    texture_t* texture = nullptr;
    int flags = 0;
};

struct surfcache_s;
struct efrag_s;

struct msurface_s {
    int visframe = 0;
    int dlightframe = 0;
    int dlightbits = 0;
    mplane_t* plane = nullptr;
    int flags = 0;
    int firstedge = 0;
    int numedges = 0;
    surfcache_s* cachespots[MIPLEVELS]{};
    short texturemins[2]{};
    short extents[2]{};
    mtexinfo_t* texinfo = nullptr;
    byte styles[MAXLIGHTMAPS]{};
    byte* samples = nullptr;
};
using msurface_t = msurface_s;

struct mnode_s {
    int contents = 0;
    int visframe = 0;
    short minmaxs[6]{};
    mnode_s* parent = nullptr;
    mplane_t* plane = nullptr;
    mnode_s* children[2]{};
    unsigned short firstsurface = 0;
    unsigned short numsurfaces = 0;
};
using mnode_t = mnode_s;

struct mleaf_s {
    int contents = 0;
    int visframe = 0;
    short minmaxs[6]{};
    mnode_s* parent = nullptr;
    byte* compressed_vis = nullptr;
    efrag_s* efrags = nullptr;
    msurface_t** firstmarksurface = nullptr;
    int nummarksurfaces = 0;
    int key = 0;
    byte ambient_sound_level[NUM_AMBIENTS]{};
};
using mleaf_t = mleaf_s;

struct hull_t {
    dclipnode_t* clipnodes = nullptr;
    mplane_t* planes = nullptr;
    int firstclipnode = 0;
    int lastclipnode = 0;
    Vector3 clip_mins{};
    Vector3 clip_maxs{};
};

struct mspriteframe_s {
    int width = 0;
    int height = 0;
    void* pcachespot = nullptr;
    float up = 0.0f, down = 0.0f, left = 0.0f, right = 0.0f;
    byte pixels[4]{};
};
using mspriteframe_t = mspriteframe_s;

struct mspritegroup_t {
    int numframes = 0;
    float* intervals = nullptr;
    mspriteframe_t* frames[1]{};
};

struct mspriteframedesc_t {
    spriteframetype_t type = spriteframetype_t::SPR_SINGLE;
    mspriteframe_t* frameptr = nullptr;
};

struct msprite_t {
    int type = 0;
    int maxwidth = 0;
    int maxheight = 0;
    int numframes = 0;
    float beamlength = 0.0f;
    void* cachespot = nullptr;
    mspriteframedesc_t frames[1]{};
};

inline constexpr int MAXALIASVERTS = 2000;

struct maliasframedesc_t {
    aliasframetype_t type = aliasframetype_t::ALIAS_SINGLE;
    trivertx_t bboxmin{};
    trivertx_t bboxmax{};
    int frame = 0;
    char name[16]{};
};

struct maliasskindesc_t {
    aliasskintype_t type = aliasskintype_t::ALIAS_SKIN_SINGLE;
    void* pcachespot = nullptr;
    int skin = 0;
};

struct maliasgroupframedesc_t {
    trivertx_t bboxmin{};
    trivertx_t bboxmax{};
    int frame = 0;
};

struct maliasgroup_t {
    int numframes = 0;
    int intervals = 0;
    maliasgroupframedesc_t frames[1]{};
};

struct maliasskingroup_t {
    int numskins = 0;
    int intervals = 0;
    maliasskindesc_t skindescs[1]{};
};

struct mtriangle_s {
    int facesfront = 0;
    int vertindex[3]{};
};
using mtriangle_t = mtriangle_s;

struct aliashdr_t {
    int model = 0;
    int stverts = 0;
    int skindesc = 0;
    int triangles = 0;
    maliasframedesc_t frames[1]{};
};

enum modtype_t {
    mod_brush,
    mod_sprite,
    mod_alias
};

inline constexpr int EF_ROCKET = 1, EF_GRENADE = 2, EF_GIB = 4, EF_ROTATE = 8;
inline constexpr int EF_TRACER = 16, EF_ZOMGIB = 32, EF_TRACER2 = 64, EF_TRACER3 = 128;

struct model_s {
    char name[MAX_QPATH]{};
    int needload = 0;
    modtype_t type = mod_brush;
    int numframes = 0;
    synctype_t synctype = synctype_t::ST_SYNC;
    int flags = 0;

    Vector3 mins{}, maxs{};
    float radius = 0.0f;

    int firstmodelsurface = 0, nummodelsurfaces = 0;
    int numsubmodels = 0;
    dmodel_t* submodels = nullptr;
    int numplanes = 0;
    mplane_t* planes = nullptr;
    int numleafs = 0;
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
    hull_t hulls[MAX_MAP_HULLS]{};
    int numtextures = 0;
    texture_t** textures = nullptr;
    byte* visdata = nullptr;
    byte* lightdata = nullptr;
    char* entities = nullptr;
    cache_user_t cache{};

    eastl::vector<dmodel_t> submodels_owner;
    eastl::vector<mplane_t> planes_owner;
    eastl::vector<mleaf_t> leafs_owner;
    eastl::vector<mvertex_t> vertexes_owner;
    eastl::vector<medge_t> edges_owner;
    eastl::vector<mnode_t> nodes_owner;
    eastl::vector<mtexinfo_t> texinfo_owner;
    eastl::vector<msurface_t> surfaces_owner;
    eastl::vector<int> surfedges_owner;
    eastl::vector<dclipnode_t> clipnodes_owner;
    eastl::vector<dclipnode_t> hull0_clipnodes_owner;
    eastl::vector<msurface_t*> marksurfaces_owner;
    eastl::vector<texture_t*> textures_owner;

    eastl::vector<byte> visdata_owner;
    eastl::vector<byte> lightdata_owner;
    eastl::vector<char> entities_owner;

    eastl::vector<eastl::vector<byte>> texture_allocations;
    eastl::vector<eastl::vector<byte>> sprite_allocations;
};
using model_t = model_s;

namespace Model {
void Mod_Print();
void Mod_Init();
void Mod_ClearAll();
model_t* Mod_ForName(const char* name, qboolean crash);
void* Mod_Extradata(model_t* mod);
void Mod_TouchModel(char* name);
mleaf_t* Mod_PointInLeaf(const Vector3& p, model_t* model);
byte* Mod_LeafPVS(mleaf_t* leaf, model_t* model);
} // namespace Model
