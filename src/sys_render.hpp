// sys_render.hpp -- Subsystem Render: Software Renderer, Rasterizer, Draw 2D, Screen, Video & Models
#pragma once

#include <cstdint>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <EASTL/string_view.h>
#include <EASTL/fixed_string.h>

#include "sys_core.hpp"

//=============================================================================
// Video Driver & Pixel Types
//=============================================================================

inline constexpr int VID_CBITS = 6;
inline constexpr int VID_GRADES = (1 << VID_CBITS);

using pixel_t = byte;

struct vrect_s {
    int x = 0, y = 0, width = 0, height = 0;
    vrect_s* pnext = nullptr;
};
using vrect_t = vrect_s;

struct viddef_t {
    pixel_t* buffer = nullptr;
    pixel_t* colormap = nullptr;
    unsigned short* colormap16 = nullptr;
    int fullbright = 0;
    unsigned rowbytes = 0;
    unsigned width = 0;
    unsigned height = 0;
    float aspect = 0.0f;
    int numpages = 0;
    int recalc_refdef = 0;
    pixel_t* conbuffer = nullptr;
    int conrowbytes = 0;
    unsigned conwidth = 0;
    unsigned conheight = 0;
    int maxwarpwidth = 0;
    int maxwarpheight = 0;
    pixel_t* direct = nullptr;
};

namespace Vid {
extern viddef_t vid;
extern unsigned short d_8to16table[256];
extern unsigned d_8to24table[256];
extern void (*vid_menudrawfn)(void);
extern void (*vid_menukeyfn)(int key);

void VID_SetPalette(unsigned char* palette);
inline void VID_ShiftPalette(unsigned char* palette) { VID_SetPalette(palette); }
void VID_Init(unsigned char* palette);
void VID_Shutdown();
void VID_Update(vrect_t* rects);
int VID_SetMode(int modenum, unsigned char* palette);
void VID_HandlePause();
void D_BeginDirectRect(int x, int y, byte* pbitmap, int width, int height);
void D_EndDirectRect(int x, int y, int width, int height);
} // namespace Vid

//=============================================================================
// BSP Map Structures
//=============================================================================

inline constexpr int MAX_MAP_HULLS = 4;
inline constexpr int MAX_MAP_MODELS = 256;
inline constexpr int MAX_MAP_BRUSHES = 4096;
inline constexpr int MAX_MAP_ENTITIES = 1024;
inline constexpr int MAX_MAP_ENTSTRING = 65536;
inline constexpr int MAX_MAP_PLANES = 32767;
inline constexpr int MAX_MAP_NODES = 32767;
inline constexpr int MAX_MAP_CLIPNODES = 32767;
inline constexpr int MAX_MAP_LEAFS = 8192;
inline constexpr int MAX_MAP_VERTS = 65535;
inline constexpr int MAX_MAP_FACES = 65535;
inline constexpr int MAX_MAP_MARKSURFACES = 65535;
inline constexpr int MAX_MAP_TEXINFO = 4096;
inline constexpr int MAX_MAP_EDGES = 256000;
inline constexpr int MAX_MAP_SURFEDGES = 512000;
inline constexpr int MAX_MAP_TEXTURES = 512;
inline constexpr int MAX_MAP_MIPTEX = 0x200000;
inline constexpr int MAX_MAP_LIGHTING = 0x100000;
inline constexpr int MAX_MAP_VISIBILITY = 0x100000;
inline constexpr int MAX_MAP_PORTALS = 65536;
inline constexpr int MAX_KEY = 32;
inline constexpr int MAX_VALUE = 1024;
inline constexpr int BSPVERSION = 29;
inline constexpr int TOOLVERSION = 2;

struct lump_t {
    int32_t fileofs = 0;
    int32_t filelen = 0;
};

inline constexpr int LUMP_ENTITIES = 0;
inline constexpr int LUMP_PLANES = 1;
inline constexpr int LUMP_TEXTURES = 2;
inline constexpr int LUMP_VERTEXES = 3;
inline constexpr int LUMP_VISIBILITY = 4;
inline constexpr int LUMP_NODES = 5;
inline constexpr int LUMP_TEXINFO = 6;
inline constexpr int LUMP_FACES = 7;
inline constexpr int LUMP_LIGHTING = 8;
inline constexpr int LUMP_CLIPNODES = 9;
inline constexpr int LUMP_LEAFS = 10;
inline constexpr int LUMP_MARKSURFACES = 11;
inline constexpr int LUMP_EDGES = 12;
inline constexpr int LUMP_SURFEDGES = 13;
inline constexpr int LUMP_MODELS = 14;
inline constexpr int HEADER_LUMPS = 15;

struct dmodel_t {
    float mins[3]{};
    float maxs[3]{};
    float origin[3]{};
    int32_t headnode[MAX_MAP_HULLS]{};
    int32_t visleafs = 0;
    int32_t firstface = 0;
    int32_t numfaces = 0;
};

struct dheader_t {
    int32_t version = 0;
    lump_t lumps[HEADER_LUMPS]{};
};

struct dmiptexlump_t {
    int32_t nummiptex = 0;
    int32_t dataofs[4]{};
};

inline constexpr int MIPLEVELS = 4;

struct miptex_t {
    char name[16]{};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offsets[MIPLEVELS]{};
};

struct dvertex_t {
    float point[3]{};
};

inline constexpr int PLANE_X = 0;
inline constexpr int PLANE_Y = 1;
inline constexpr int PLANE_Z = 2;
inline constexpr int PLANE_ANYX = 3;
inline constexpr int PLANE_ANYY = 4;
inline constexpr int PLANE_ANYZ = 5;

struct dplane_t {
    float normal[3]{};
    float dist = 0.0f;
    int32_t type = 0;
};

inline constexpr int CONTENTS_EMPTY = -1;
inline constexpr int CONTENTS_SOLID = -2;
inline constexpr int CONTENTS_WATER = -3;
inline constexpr int CONTENTS_SLIME = -4;
inline constexpr int CONTENTS_LAVA = -5;
inline constexpr int CONTENTS_SKY = -6;
inline constexpr int CONTENTS_ORIGIN = -7;
inline constexpr int CONTENTS_CLIP = -8;
inline constexpr int CONTENTS_CURRENT_0 = -9;
inline constexpr int CONTENTS_CURRENT_90 = -10;
inline constexpr int CONTENTS_CURRENT_180 = -11;
inline constexpr int CONTENTS_CURRENT_270 = -12;
inline constexpr int CONTENTS_CURRENT_UP = -13;
inline constexpr int CONTENTS_CURRENT_DOWN = -14;

struct dnode_t {
    int32_t planenum = 0;
    int16_t children[2]{};
    int16_t mins[3]{};
    int16_t maxs[3]{};
    uint16_t firstface = 0;
    uint16_t numfaces = 0;
};

struct dclipnode_t {
    int32_t planenum = 0;
    int16_t children[2]{};
};

struct texinfo_t {
    float vecs[2][4]{};
    int32_t miptex = 0;
    int32_t flags = 0;
};

inline constexpr int TEX_SPECIAL = 1;

struct dedge_t {
    uint16_t v[2]{};
};

inline constexpr int MAXLIGHTMAPS = 4;

struct dface_t {
    int16_t planenum = 0;
    int16_t side = 0;
    int32_t firstedge = 0;
    int16_t numedges = 0;
    int16_t texinfo = 0;
    byte styles[MAXLIGHTMAPS]{};
    int32_t lightofs = 0;
};

inline constexpr int AMBIENT_WATER = 0;
inline constexpr int AMBIENT_SKY = 1;
inline constexpr int AMBIENT_SLIME = 2;
inline constexpr int AMBIENT_LAVA = 3;
inline constexpr int NUM_AMBIENTS = 4;

struct dleaf_t {
    int32_t contents = 0;
    int32_t visofs = 0;
    int16_t mins[3]{};
    int16_t maxs[3]{};
    uint16_t firstmarksurface = 0;
    uint16_t nummarksurfaces = 0;
    byte ambient_level[NUM_AMBIENTS]{};
};

//=============================================================================
// Alias & Sprite Definitions
//=============================================================================

inline constexpr int ALIAS_VERSION = 6;
inline constexpr int ALIAS_ONSEAM = 0x0020;
inline constexpr int DT_FACES_FRONT = 0x0010;
inline constexpr std::uint32_t IDPOLYHEADER = (('O' << 24) + ('P' << 16) + ('D' << 8) + 'I');

#ifndef SYNCTYPE_T
#define SYNCTYPE_T
enum class synctype_t : int {
    ST_SYNC = 0,
    ST_RAND
};
#endif

enum class aliasframetype_t : int {
    ALIAS_SINGLE = 0,
    ALIAS_GROUP
};

enum class aliasskintype_t : int {
    ALIAS_SKIN_SINGLE = 0,
    ALIAS_SKIN_GROUP
};

struct mdl_t {
    int ident = 0;
    int version = 0;
    Vector3 scale{};
    Vector3 scale_origin{};
    float boundingradius = 0.0f;
    Vector3 eyeposition{};
    int numskins = 0;
    int skinwidth = 0;
    int skinheight = 0;
    int numverts = 0;
    int numtris = 0;
    int numframes = 0;
    synctype_t synctype = synctype_t::ST_SYNC;
    int flags = 0;
    float size = 0.0f;
};

struct stvert_t {
    int onseam = 0;
    int s = 0;
    int t = 0;
};

struct dtriangle_t {
    int facesfront = 0;
    int vertindex[3]{};
};

struct trivertx_t {
    std::uint8_t v[3]{};
    std::uint8_t lightnormalindex = 0;
};

struct daliasframe_t {
    trivertx_t bboxmin{};
    trivertx_t bboxmax{};
    char name[16]{};
};

struct daliasgroup_t {
    int numframes = 0;
    trivertx_t bboxmin{};
    trivertx_t bboxmax{};
};

struct daliasskingroup_t {
    int numskins = 0;
};

struct daliasinterval_t {
    float interval = 0.0f;
};

struct daliasskininterval_t {
    float interval = 0.0f;
};

struct daliasframetype_t {
    aliasframetype_t type = aliasframetype_t::ALIAS_SINGLE;
};

struct daliasskintype_t {
    aliasskintype_t type = aliasskintype_t::ALIAS_SKIN_SINGLE;
};

inline constexpr int SPRITE_VERSION = 1;
inline constexpr int SPR_VP_PARALLEL_UPRIGHT = 0;
inline constexpr int SPR_FACING_UPRIGHT = 1;
inline constexpr int SPR_VP_PARALLEL = 2;
inline constexpr int SPR_ORIENTED = 3;
inline constexpr int SPR_VP_PARALLEL_ORIENTED = 4;

enum class spriteframetype_t : int {
    SPR_SINGLE = 0,
    SPR_GROUP
};

struct dsprite_t {
    int ident = 0;
    int version = 0;
    int type = 0;
    float boundingradius = 0.0f;
    int width = 0;
    int height = 0;
    int numframes = 0;
    float beamlength = 0.0f;
    synctype_t synctype = synctype_t::ST_SYNC;
};

struct dspriteframe_t {
    int origin[2]{};
    int width = 0;
    int height = 0;
};

struct dspritegroup_t {
    int numframes = 0;
};

struct dspriteinterval_t {
    float interval = 0.0f;
};

struct dspriteframetype_t {
    spriteframetype_t type = spriteframetype_t::SPR_SINGLE;
};

inline constexpr std::uint32_t IDSPRITEHEADER = (('P' << 24) + ('S' << 16) + ('D' << 8) + 'I');

//=============================================================================
// In-Memory Model Definitions
//=============================================================================

struct mvertex_t {
    Vector3 position{};
};

inline constexpr int SIDE_FRONT = 0;
inline constexpr int SIDE_BACK = 1;
inline constexpr int SIDE_ON = 2;

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

inline constexpr int SURF_PLANEBACK = 2;
inline constexpr int SURF_DRAWSKY = 4;
inline constexpr int SURF_DRAWSPRITE = 8;
inline constexpr int SURF_DRAWTURB = 0x10;
inline constexpr int SURF_DRAWTILED = 0x20;
inline constexpr int SURF_DRAWBACKGROUND = 0x40;

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

inline constexpr int EF_ROCKET = 1;
inline constexpr int EF_GRENADE = 2;
inline constexpr int EF_GIB = 4;
inline constexpr int EF_ROTATE = 8;
inline constexpr int EF_TRACER = 16;
inline constexpr int EF_ZOMGIB = 32;
inline constexpr int EF_TRACER2 = 64;
inline constexpr int EF_TRACER3 = 128;

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

//=============================================================================
// Entity, Efrag & Render Defs
//=============================================================================

inline constexpr int MAXCLIPPLANES = 11;
inline constexpr int TOP_RANGE = 16;
inline constexpr int BOTTOM_RANGE = 96;

struct efrag_s {
    mleaf_s* leaf = nullptr;
    efrag_s* leafnext = nullptr;
    struct entity_t* entity = nullptr;
    efrag_s* entnext = nullptr;
};
using efrag_t = efrag_s;

struct entity_state_t {
    Vector3 origin{};
    Vector3 angles{};
    int modelindex = 0;
    int frame = 0;
    int colormap = 0;
    int skin = 0;
    int effects = 0;
};

struct dlight_t {
    Vector3 origin{};
    float radius = 0.0f;
    float die = 0.0f;
    float decay = 0.0f;
    float minlight = 0.0f;
    int key = 0;
};

struct entity_t {
    bool forcelink = false;
    int update_type = 0;
    entity_state_t baseline{};
    double msgtime = 0.0;
    Vector3 msg_origins[2]{};
    Vector3 origin{};
    Vector3 msg_angles[2]{};
    Vector3 angles{};
    model_s* model = nullptr;
    efrag_s* efrag = nullptr;
    int frame = 0;
    float syncbase = 0.0f;
    byte* colormap = nullptr;
    int effects = 0;
    int skinnum = 0;
    int visframe = 0;
    int dlightframe = 0;
    int dlightbits = 0;
    int trivial_accept = 0;
    mnode_s* topnode = nullptr;
};
using entity_s = entity_t;

struct refdef_t {
    vrect_t vrect{};
    vrect_t aliasvrect{};
    int vrectright = 0, vrectbottom = 0;
    int aliasvrectright = 0, aliasvrectbottom = 0;
    float vrectrightedge = 0.0f;
    float fvrectx = 0.0f, fvrecty = 0.0f;
    float fvrectx_adj = 0.0f, fvrecty_adj = 0.0f;
    int64_t vrect_x_adj_shift20 = 0;
    int64_t vrectright_adj_shift20 = 0;
    float fvrectright_adj = 0.0f, fvrectbottom_adj = 0.0f;
    float fvrectright = 0.0f;
    float fvrectbottom = 0.0f;
    float horizontalFieldOfView = 0.0f;
    float xOrigin = 0.0f;
    float yOrigin = 0.0f;
    Vector3 vieworg{};
    Vector3 viewangles{};
    float fov_x = 0.0f, fov_y = 0.0f;
    int ambientlight = 0;
};

//=============================================================================
// Refresh Shared & Local & Rasterizer
//=============================================================================

inline constexpr int SCANBUFFERPAD = 0x1000;
inline constexpr int R_SKY_SMASK = 0x007F0000;
inline constexpr int R_SKY_TMASK = 0x007F0000;
inline constexpr int DS_SPAN_LIST_END = -128;
inline constexpr int SURFCACHE_SIZE_AT_320X200 = 600 * 1024;
inline constexpr double ALIAS_BASE_SIZE_RATIO = 1.0 / 11.0;
inline constexpr int BMODEL_FULLY_CLIPPED = 0x10;

extern short* d_pzbuffer;

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

inline constexpr int MAXVERTS = 16;
inline constexpr int MAXWORKINGVERTS = MAXVERTS + 4;
inline constexpr int MAXHEIGHT = 2160;
inline constexpr int MAXWIDTH = 3840;
inline constexpr int MAXDIMENSION = (MAXHEIGHT > MAXWIDTH) ? MAXHEIGHT : MAXWIDTH;
inline constexpr int CYCLE = 256;
inline constexpr int SIN_BUFFER_SIZE = MAXDIMENSION + CYCLE;
inline constexpr int INFINITE_DISTANCE = 0x10000;

inline constexpr int NUMSTACKEDGES = 2400;
inline constexpr int MINEDGES = NUMSTACKEDGES;
inline constexpr int NUMSTACKSURFACES = 800;
inline constexpr int MINSURFACES = NUMSTACKSURFACES;
inline constexpr int MAXSPANS = 8000;

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

inline constexpr int ALIAS_LEFT_CLIP = 0x0001;
inline constexpr int ALIAS_TOP_CLIP = 0x0002;
inline constexpr int ALIAS_RIGHT_CLIP = 0x0004;
inline constexpr int ALIAS_BOTTOM_CLIP = 0x0008;
inline constexpr int ALIAS_Z_CLIP = 0x0010;
inline constexpr int ALIAS_XY_CLIP_MASK = 0x000F;

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

inline constexpr int SKYSHIFT = 7;
inline constexpr int SKYSIZE = 1 << SKYSHIFT;
inline constexpr int SKYMASK = SKYSIZE - 1;

namespace Render {
extern refdef_t r_refdef;
extern Vector3 r_origin, vpn, vright, vup;
extern texture_s* r_notexture_mip;

void R_Init();
void R_InitTextures();
void R_InitEfrags();
void R_RenderView();
void R_ViewChanged(vrect_t* pvrect, int lineadj, float aspect);
void R_InitSky(texture_s* mt);
void R_AddEfrags(entity_t* ent);
void R_RemoveEfrags(entity_t* ent);
void R_NewMap();
void R_ParseParticleEffect();
void R_RunParticleEffect(const Vector3& org, const Vector3& dir, int color, int count);
void R_RocketTrail(Vector3 start, const Vector3& end, int type);
void R_EntityParticles(entity_t* ent);
void R_BlobExplosion(const Vector3& org);
void R_ParticleExplosion(const Vector3& org);
void R_ParticleExplosion2(const Vector3& org, int colorStart, int colorLength);
void R_LavaSplash(const Vector3& org);
void R_TeleportSplash(const Vector3& org);
void R_PushDlights();

extern bool r_cache_thrash;
int D_SurfaceCacheForRes(int width, int height);
void D_FlushCaches();
void D_DeleteSurfaceCache();
void D_InitCaches(void* buffer, int size);
void R_SetVrect(vrect_t* pvrect, vrect_t* pvrectin, int lineadj);

void D_DrawSpans8(espan_t* pspans);
void D_DrawSpans16(espan_t* pspans);
void D_DrawZSpans(espan_t* pspans);
void Turbulent8(espan_t* pspan);
void D_SpriteDrawSpans(sspan_t* pspan);
void D_DrawSkyScans8(espan_t* pspan);
void D_DrawSkyScans16(espan_t* pspan);
void R_ShowSubDiv();
surfcache_t* D_CacheSurface(msurface_s* surface, int mip_level);

extern void R_DrawLine(polyvert_t* polyvert0, polyvert_t* polyvert1);
extern int screenwidth;
extern float pixelAspect;
extern int r_drawnpolycount;
extern eastl::array<int, SIN_BUFFER_SIZE> sintable;
extern eastl::array<int, SIN_BUFFER_SIZE> intsintable;
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
extern eastl::array<int, 256> d_lightstylevalue;

extern void TransformVector(const Vector3& in, Vector3& out);
extern void SetUpForLineScan(fixed8_t startvertu, fixed8_t startvertv, fixed8_t endvertu, fixed8_t endvertv);
extern int r_skymade;
extern void R_MakeSky();

extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;

inline constexpr double XCENTERING = 1.0 / 2.0;
inline constexpr double YCENTERING = 1.0 / 2.0;
inline constexpr double CLIP_EPSILON = 0.001;
inline constexpr double BACKFACE_EPSILON = 0.01;
inline constexpr int DIST_NOT_SET = 98765;

struct clipplane_t {
    Vector3 normal{};
    float dist = 0.0f;
    clipplane_t* next = nullptr;
    uint8_t leftedge = 0;
    uint8_t rightedge = 0;
    eastl::array<uint8_t, 2> reserved{};
};

void R_RenderWorld();
void R_ClearPolyList();
void R_DrawPolyList();

extern qboolean insubmodel;

void R_DrawSprite();
void R_RenderFace(msurface_t* fa, int clipflags);
void R_RenderPoly(msurface_t* fa, int clipflags);
void R_RenderBmodelFace(bedge_t* pedges, msurface_t* psurf);
void R_TransformFrustum();
void R_SetSkyFrame();
void R_DrawSurfaceBlock16();
void R_DrawSurfaceBlock8();
texture_t* R_TextureAnimation(texture_t* base);

void R_DrawSubmodelPolygons(model_t* pmodel, int clipflags);
void R_DrawSolidClippedSubmodelPolygons(model_t* pmodel);

void R_AddPolygonEdges(emitpoint_t* pverts, int numverts, int miplevel);
surf_t* R_GetSurf();
void R_AliasDrawModel(alight_t* plighting);
void R_BeginEdgeFrame();
void R_ScanEdges();
void D_DrawSurfaces();
void R_InsertNewEdges(edge_t* edgestoadd, edge_t* edgelist);
void R_StepActiveU(edge_t* pedge);
void R_RemoveEdges(edge_t* pedge);
extern void R_RotateBmodel();

inline constexpr double NEAR_CLIP = 0.01;
inline constexpr int MAXBVERTINDEXES = 1000;

struct btofpoly_t {
    int clipflags = 0;
    msurface_t* psurf = nullptr;
};

inline constexpr int MAX_BTOFPOLYS = 5000;

void R_InitTurb();
void R_ZDrawSubmodelPolys(model_t* clmodel);

inline constexpr int MAXALIASVERTS = 2000;
inline constexpr int ALIAS_Z_CLIP_PLANE = 5;

qboolean R_AliasCheckBBox();

inline constexpr int AMP = 8 * 0x10000;
inline constexpr int AMP2 = 3;
inline constexpr int SPEED = 20;

void R_DrawParticles();
void R_InitParticles();
void R_ClearParticles();
void R_ReadPointFile_f();

void R_AliasClipTriangle(mtriangle_t* ptri);
void R_StoreEfrags(efrag_t** ppefrag);
void R_TimeRefresh_f();
void R_TimeGraph();
void R_PrintAliasStats();
void R_PrintTimes();
void R_PrintDSpeeds();
void R_AnimateLight();
int R_LightPoint(const Vector3& p);
void R_SetupFrame();
void R_cshift_f();
void R_EmitEdge(mvertex_t* pv0, mvertex_t* pv1);
void R_ClipEdge(mvertex_t* pv0, mvertex_t* pv1, clipplane_t* clip);
void R_SplitEntityOnNode2(mnode_t* node);
void R_MarkLights(dlight_t* light, int bit, mnode_t* node);

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

extern Vector3 r_pright, r_pup, r_ppn;

void D_Aff8Patch(void* pcolormap);
inline void D_EnableBackBufferAccess() { VID_LockBuffer(); }
inline void D_DisableBackBufferAccess() { VID_UnlockBuffer(); }

void D_PolysetDraw();
void D_PolysetDrawFinalVerts(finalvert_t* fv, int numverts);
void D_DrawParticle(particle_t* pparticle);
void D_DrawPoly();
void D_DrawSprite();
void D_EndParticles();
void D_Init();
void D_ViewChanged();
void D_SetupFrame();
void D_StartParticles();
void D_TurnZOn();
void D_WarpScreen();
void D_DrawRect();
void D_UpdateRects(vrect_t* prect);
void D_PolysetUpdateTables();

extern int r_skydirect;
extern byte* r_skysource;

inline constexpr int DR_SOLID = 0;
inline constexpr int DR_TRANSPARENT = 1;
inline constexpr int TRANSPARENT_COLOR = 0xFF;

extern void* acolormap;

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

extern drawsurf_t r_drawsurf;

void R_DrawSurface();
extern float skyspeed, skyspeed2;
extern float skytime;
extern int c_surf;
extern byte* r_warpbuffer;
} // namespace Render

//=============================================================================
// 2D Draw & Font
//=============================================================================

namespace Draw {
extern qpic_t* draw_disc;

void Draw_Init();
void Draw_Character(int x, int y, int num);
void Draw_Pic(int x, int y, qpic_t* pic);
void Draw_TransPic(int x, int y, qpic_t* pic);
void Draw_TransPicTranslate(int x, int y, qpic_t* pic, const byte* translation);
void Draw_ConsoleBackground(int lines);
void Draw_BeginDisc();
void Draw_EndDisc();
void Draw_TileClear(int x, int y, int w, int h);
void Draw_Fill(int x, int y, int w, int h, int c);
void Draw_FadeScreen();
void Draw_String(int x, int y, eastl::string_view str);

inline qpic_t* Draw_PicFromWad(eastl::string_view name) {
    return static_cast<qpic_t*>(Wad::W_GetLumpName(name));
}

qpic_t* Draw_CachePic(eastl::string_view path);
} // namespace Draw

//=============================================================================
// Status Bar
//=============================================================================

inline constexpr int SBAR_HEIGHT = 24;
extern int sb_lines;

namespace Sbar {
void Sbar_Init();
void Sbar_Changed();
void Sbar_Draw();
void Sbar_IntermissionOverlay();
void Sbar_FinaleOverlay();
} // namespace Sbar

//=============================================================================
// Screen Management System
//=============================================================================

namespace Screen {
class ScreenSystem {
public:
    ScreenSystem() = default;
    ~ScreenSystem() = default;

    void Init();
    void UpdateScreen();
    void SizeUp();
    void SizeDown();
    void CenterPrint(eastl::string_view str);
    void BeginLoadingPlaque();
    void EndLoadingPlaque();
    bool ModalMessage(eastl::string_view text);

    vrect_t& GetVrect() { return vrect_; }
    const vrect_t& GetVrect() const { return vrect_; }

    cvar_t& GetFov() { return fov_; }
    const cvar_t& GetFov() const { return fov_; }

    cvar_t& GetViewsize() { return viewsize_; }
    const cvar_t& GetViewsize() const { return viewsize_; }

    float& GetCentertimeOff() { return centertime_off_; }
    void SetCentertimeOff(float val) { centertime_off_ = val; }

    float& GetConCurrent() { return con_current_; }
    void SetConCurrent(float val) { con_current_ = val; }

    float& GetConlines() { return conlines_; }
    void SetConlines(float val) { conlines_ = val; }

    int& GetFullupdate() { return fullupdate_; }
    void SetFullupdate(int val) { fullupdate_ = val; }

    int& GetClearnotify() { return clearnotify_; }
    void SetClearnotify(int val) { clearnotify_ = val; }

    qboolean& GetDisabledForLoading() { return disabled_for_loading_; }
    void SetDisabledForLoading(qboolean val) { disabled_for_loading_ = val; }

    qboolean& GetSkipupdate() { return skipupdate_; }
    void SetSkipupdate(qboolean val) { skipupdate_ = val; }

    qboolean& GetBlockDrawing() { return block_drawing_; }
    void SetBlockDrawing(qboolean val) { block_drawing_ = val; }

    int& GetCopytop() { return copytop_; }
    void SetCopytop(int val) { copytop_ = val; }

    int& GetCopyeverything() { return copyeverything_; }
    void SetCopyeverything(int val) { copyeverything_ = val; }

    static void ScreenShot_f();
    static void SizeUp_f();
    static void SizeDown_f();

private:
    cvar_t viewsize_ = { "viewsize", "100", true, false, 0.0f, nullptr };
    cvar_t fov_ = { "fov", "90", false, false, 0.0f, nullptr };
    cvar_t conspeed_ = { "scr_conspeed", "300", false, false, 0.0f, nullptr };
    cvar_t centertime_ = { "scr_centertime", "2", false, false, 0.0f, nullptr };
    cvar_t showram_ = { "showram", "1", false, false, 0.0f, nullptr };
    cvar_t showturtle_ = { "showturtle", "0", false, false, 0.0f, nullptr };
    cvar_t showpause_ = { "showpause", "1", false, false, 0.0f, nullptr };
    cvar_t printspeed_ = { "scr_printspeed", "8", false, false, 0.0f, nullptr };

    vrect_t vrect_{};

    float con_current_ = 0.0f;
    float conlines_ = 0.0f;
    float centertime_off_ = 0.0f;
    float centertime_start_ = 0.0f;
    float oldscreensize_ = 0.0f;
    float oldfov_ = 0.0f;
    float disabled_time_ = 0.0f;

    int copytop_ = 0;
    int copyeverything_ = 0;
    int fullupdate_ = 0;
    int clearconsole_ = 0;
    int clearnotify_ = 0;

    int center_lines_ = 0;
    int erase_lines_ = 0;
    int erase_center_ = 0;

    qboolean initialized_ = false;
    qboolean disabled_for_loading_ = false;
    qboolean drawloading_ = false;
    qboolean skipupdate_ = false;
    qboolean block_drawing_ = false;
    qboolean drawdialog_ = false;

    eastl::fixed_string<char, 1024> centerstring_{};
    eastl::string_view notifystring_{};

    qpic_t* ram_pic_ = nullptr;
    qpic_t* net_pic_ = nullptr;
    qpic_t* turtle_pic_ = nullptr;

    vrect_t* pconupdate_ = nullptr;

    void EraseCenterString();
    void DrawCenterString();
    void CheckDrawCenterString();
    void CalcRefdef();
    void DrawRam();
    void DrawTurtle();
    void DrawNet();
    void DrawPause();
    void DrawLoading();
    void SetUpToDrawConsole();
    void DrawConsole();
    void DrawNotifyString();
    float CalcFov(float fov_x, float width, float height);
};

ScreenSystem& GetScreenSystem();
} // namespace Screen

//=============================================================================
// View Setup & FX
//=============================================================================

namespace View {
extern cvar_t v_gamma;
extern eastl::array<byte, 256> gammatable;
extern cvar_t lcd_x;

void V_Init();
void V_RenderView();
[[nodiscard]] float V_CalcRoll(const Vector3& angles, const Vector3& velocity);
void V_UpdatePalette();
void V_StartPitchDrift();
void V_StopPitchDrift();
void V_Register();
void V_ParseDamage();
void V_SetContentsColor(int contents);
} // namespace View
