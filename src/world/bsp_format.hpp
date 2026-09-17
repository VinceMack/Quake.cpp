// bsp_format.hpp -- On-disk BSP, MDL, and SPR file format structures
#pragma once
#include "core/types.hpp"
#include "core/math.hpp"

#include <cstdint>
//=============================================================================
// BSP Map On-Disk Structures
//=============================================================================

inline constexpr int MAX_MAP_HULLS = 4, MAX_MAP_MODELS = 256, MAX_MAP_BRUSHES = 4096, MAX_MAP_ENTITIES = 1024;
inline constexpr int MAX_MAP_ENTSTRING = 65536, MAX_MAP_PLANES = 32767, MAX_MAP_NODES = 32767,
                     MAX_MAP_CLIPNODES = 32767;
inline constexpr int MAX_MAP_LEAFS = 8192, MAX_MAP_VERTS = 65535, MAX_MAP_FACES = 65535, MAX_MAP_MARKSURFACES = 65535;
inline constexpr int MAX_MAP_TEXINFO = 4096, MAX_MAP_EDGES = 256000, MAX_MAP_SURFEDGES = 512000, MAX_MAP_TEXTURES = 512;
inline constexpr int MAX_MAP_MIPTEX = 0x200000, MAX_MAP_LIGHTING = 0x100000, MAX_MAP_VISIBILITY = 0x100000,
                     MAX_MAP_PORTALS = 65536;
inline constexpr int MAX_KEY = 32, MAX_VALUE = 1024, BSPVERSION = 29, TOOLVERSION = 2;

struct lump_t {
    int32_t fileofs = 0;
    int32_t filelen = 0;
};

inline constexpr int LUMP_ENTITIES = 0, LUMP_PLANES = 1, LUMP_TEXTURES = 2, LUMP_VERTEXES = 3, LUMP_VISIBILITY = 4;
inline constexpr int LUMP_NODES = 5, LUMP_TEXINFO = 6, LUMP_FACES = 7, LUMP_LIGHTING = 8, LUMP_CLIPNODES = 9;
inline constexpr int LUMP_LEAFS = 10, LUMP_MARKSURFACES = 11, LUMP_EDGES = 12, LUMP_SURFEDGES = 13, LUMP_MODELS = 14,
                     HEADER_LUMPS = 15;

struct dmodel_t {
    float mins[3] { };
    float maxs[3] { };
    float origin[3] { };
    int32_t headnode[MAX_MAP_HULLS] { };
    int32_t visleafs = 0;
    int32_t firstface = 0;
    int32_t numfaces = 0;
};

struct dheader_t {
    int32_t version = 0;
    lump_t lumps[HEADER_LUMPS] { };
};

struct dmiptexlump_t {
    int32_t nummiptex = 0;
    int32_t dataofs[4] { };
};

inline constexpr int MIPLEVELS = 4;

struct miptex_t {
    char name[16] { };
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offsets[MIPLEVELS] { };
};

struct dvertex_t {
    float point[3] { };
};

inline constexpr int PLANE_X = 0, PLANE_Y = 1, PLANE_Z = 2, PLANE_ANYX = 3, PLANE_ANYY = 4, PLANE_ANYZ = 5;

struct dplane_t {
    float normal[3] { };
    float dist = 0.0f;
    int32_t type = 0;
};

inline constexpr int CONTENTS_EMPTY = -1, CONTENTS_SOLID = -2, CONTENTS_WATER = -3, CONTENTS_SLIME = -4,
                     CONTENTS_LAVA = -5;
inline constexpr int CONTENTS_SKY = -6, CONTENTS_ORIGIN = -7, CONTENTS_CLIP = -8, CONTENTS_CURRENT_0 = -9,
                     CONTENTS_CURRENT_90 = -10;
inline constexpr int CONTENTS_CURRENT_180 = -11, CONTENTS_CURRENT_270 = -12, CONTENTS_CURRENT_UP = -13,
                     CONTENTS_CURRENT_DOWN = -14;

struct dnode_t {
    int32_t planenum = 0;
    int16_t children[2] { };
    int16_t mins[3] { };
    int16_t maxs[3] { };
    uint16_t firstface = 0;
    uint16_t numfaces = 0;
};

struct dclipnode_t {
    int32_t planenum = 0;
    int16_t children[2] { };
};

struct texinfo_t {
    float vecs[2][4] { };
    int32_t miptex = 0;
    int32_t flags = 0;
};

inline constexpr int TEX_SPECIAL = 1;

struct dedge_t {
    uint16_t v[2] { };
};

inline constexpr int MAXLIGHTMAPS = 4;

struct dface_t {
    int16_t planenum = 0;
    int16_t side = 0;
    int32_t firstedge = 0;
    int16_t numedges = 0;
    int16_t texinfo = 0;
    byte styles[MAXLIGHTMAPS] { };
    int32_t lightofs = 0;
};

inline constexpr int AMBIENT_WATER = 0, AMBIENT_SKY = 1, AMBIENT_SLIME = 2, AMBIENT_LAVA = 3, NUM_AMBIENTS = 4;

struct dleaf_t {
    int32_t contents = 0;
    int32_t visofs = 0;
    int16_t mins[3] { };
    int16_t maxs[3] { };
    uint16_t firstmarksurface = 0;
    uint16_t nummarksurfaces = 0;
    byte ambient_level[NUM_AMBIENTS] { };
};

//=============================================================================
// Alias (.mdl) & Sprite (.spr) On-Disk Structures
//=============================================================================

inline constexpr int ALIAS_VERSION = 6, ALIAS_ONSEAM = 0x0020, DT_FACES_FRONT = 0x0010;
inline constexpr std::uint32_t IDPOLYHEADER = (('O' << 24) + ('P' << 16) + ('D' << 8) + 'I');

#ifndef SYNCTYPE_T
#define SYNCTYPE_T
enum class synctype_t : int { ST_SYNC = 0, ST_RAND };
#endif

enum class aliasframetype_t : int { ALIAS_SINGLE = 0, ALIAS_GROUP };

enum class aliasskintype_t : int { ALIAS_SKIN_SINGLE = 0, ALIAS_SKIN_GROUP };

struct mdl_t {
    int ident = 0;
    int version = 0;
    Vector3 scale { };
    Vector3 scale_origin { };
    float boundingradius = 0.0f;
    Vector3 eyeposition { };
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
    int vertindex[3] { };
};

struct trivertx_t {
    std::uint8_t v[3] { };
    std::uint8_t lightnormalindex = 0;
};

struct daliasframe_t {
    trivertx_t bboxmin { };
    trivertx_t bboxmax { };
    char name[16] { };
};

struct daliasgroup_t {
    int numframes = 0;
    trivertx_t bboxmin { };
    trivertx_t bboxmax { };
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

inline constexpr int SPRITE_VERSION = 1, SPR_VP_PARALLEL_UPRIGHT = 0, SPR_FACING_UPRIGHT = 1;
inline constexpr int SPR_VP_PARALLEL = 2, SPR_ORIENTED = 3, SPR_VP_PARALLEL_ORIENTED = 4;

enum class spriteframetype_t : int { SPR_SINGLE = 0, SPR_GROUP };

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
    int origin[2] { };
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
