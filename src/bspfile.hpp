// bspfile.hpp -- BSP map file format structures and constants
#pragma once

#include <cstdint>
#include "common.hpp"
#include "mathlib.hpp"

inline constexpr int MAX_MAP_HULLS = 4;

inline constexpr int MAX_MAP_MODELS = 256;
inline constexpr int MAX_MAP_BRUSHES = 4096;
inline constexpr int MAX_MAP_ENTITIES = 1024;
inline constexpr int MAX_MAP_ENTSTRING = 65536;

inline constexpr int MAX_MAP_PLANES = 32767;
inline constexpr int MAX_MAP_NODES = 32767;     // because negative shorts are contents
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

// key / value pair sizes

inline constexpr int MAX_KEY = 32;
inline constexpr int MAX_VALUE = 1024;

//=============================================================================

inline constexpr int BSPVERSION = 29;
inline constexpr int TOOLVERSION = 2;

struct lump_t {
    int32_t fileofs;
    int32_t filelen;
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
    float mins[3];
    float maxs[3];
    float origin[3];
    int32_t headnode[MAX_MAP_HULLS];
    int32_t visleafs; // not including the solid leaf 0
    int32_t firstface;
    int32_t numfaces;
};

struct dheader_t {
    int32_t version;
    lump_t lumps[HEADER_LUMPS];
};

struct dmiptexlump_t {
    int32_t nummiptex;
    int32_t dataofs[4]; // [nummiptex]
};

inline constexpr int MIPLEVELS = 4;

struct miptex_t {
    char name[16];
    uint32_t width;
    uint32_t height;
    uint32_t offsets[MIPLEVELS]; // four mip maps stored
};

struct dvertex_t {
    float point[3];
};

// 0-2 are axial planes
inline constexpr int PLANE_X = 0;
inline constexpr int PLANE_Y = 1;
inline constexpr int PLANE_Z = 2;

// 3-5 are non-axial planes snapped to the nearest
inline constexpr int PLANE_ANYX = 3;
inline constexpr int PLANE_ANYY = 4;
inline constexpr int PLANE_ANYZ = 5;

struct dplane_t {
    float normal[3];
    float dist;
    int32_t type; // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
};

inline constexpr int CONTENTS_EMPTY = -1;
inline constexpr int CONTENTS_SOLID = -2;
inline constexpr int CONTENTS_WATER = -3;
inline constexpr int CONTENTS_SLIME = -4;
inline constexpr int CONTENTS_LAVA = -5;
inline constexpr int CONTENTS_SKY = -6;
inline constexpr int CONTENTS_ORIGIN = -7; // removed at csg time
inline constexpr int CONTENTS_CLIP = -8;   // changed to contents_solid

inline constexpr int CONTENTS_CURRENT_0 = -9;
inline constexpr int CONTENTS_CURRENT_90 = -10;
inline constexpr int CONTENTS_CURRENT_180 = -11;
inline constexpr int CONTENTS_CURRENT_270 = -12;
inline constexpr int CONTENTS_CURRENT_UP = -13;
inline constexpr int CONTENTS_CURRENT_DOWN = -14;

// !!! if this is changed, it must be changed in asm_i386.h too !!!
struct dnode_t {
    int32_t planenum;
    int16_t children[2]; // negative numbers are -(leafs+1), not nodes
    int16_t mins[3];     // for sphere culling
    int16_t maxs[3];
    uint16_t firstface;
    uint16_t numfaces; // counting both sides
};

struct dclipnode_t {
    int32_t planenum;
    int16_t children[2]; // negative numbers are contents
};

struct texinfo_t {
    float vecs[2][4]; // [s/t][xyz offset]
    int32_t miptex;
    int32_t flags;
};

inline constexpr int TEX_SPECIAL = 1; // sky or slime, no lightmap or 256 subdivision

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct dedge_t {
    uint16_t v[2]; // vertex numbers
};

inline constexpr int MAXLIGHTMAPS = 4;

struct dface_t {
    int16_t planenum;
    int16_t side;

    int32_t firstedge; // we must support > 64k edges
    int16_t numedges;
    int16_t texinfo;

    // lighting info
    byte styles[MAXLIGHTMAPS];
    int32_t lightofs; // start of [numstyles*surfsize] samples
};

inline constexpr int AMBIENT_WATER = 0;
inline constexpr int AMBIENT_SKY = 1;
inline constexpr int AMBIENT_SLIME = 2;
inline constexpr int AMBIENT_LAVA = 3;

inline constexpr int NUM_AMBIENTS = 4; // automatic ambient sounds

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
struct dleaf_t {
    int32_t contents;
    int32_t visofs; // -1 = no visibility info

    int16_t mins[3]; // for frustum culling
    int16_t maxs[3];

    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;

    byte ambient_level[NUM_AMBIENTS];
};

//============================================================================

#ifndef QUAKE_GAME

inline constexpr int ANGLE_UP = -1;
inline constexpr int ANGLE_DOWN = -2;

// the utilities get to be lazy and just use large static arrays

extern int nummodels;
extern dmodel_t dmodels[MAX_MAP_MODELS];

extern int visdatasize;
extern byte dvisdata[MAX_MAP_VISIBILITY];

extern int lightdatasize;
extern byte dlightdata[MAX_MAP_LIGHTING];

extern int texdatasize;
extern byte dtexdata[MAX_MAP_MIPTEX]; // (dmiptexlump_t)

extern int entdatasize;
extern char dentdata[MAX_MAP_ENTSTRING];

extern int numleafs;
extern dleaf_t dleafs[MAX_MAP_LEAFS];

extern int numplanes;
extern dplane_t dplanes[MAX_MAP_PLANES];

extern int numvertexes;
extern dvertex_t dvertexes[MAX_MAP_VERTS];

extern int numnodes;
extern dnode_t dnodes[MAX_MAP_NODES];

extern int numtexinfo;
extern texinfo_t texinfo[MAX_MAP_TEXINFO];

extern int numfaces;
extern dface_t dfaces[MAX_MAP_FACES];

extern int numclipnodes;
extern dclipnode_t dclipnodes[MAX_MAP_CLIPNODES];

extern int numedges;
extern dedge_t dedges[MAX_MAP_EDGES];

extern int nummarksurfaces;
extern unsigned short dmarksurfaces[MAX_MAP_MARKSURFACES];

extern int numsurfedges;
extern int dsurfedges[MAX_MAP_SURFEDGES];

void DecompressVis(const byte* in, byte* decompressed);
int CompressVis(const byte* vis, byte* dest);

void LoadBSPFile(const char* filename);
void WriteBSPFile(const char* filename);
void PrintBSPFileSizes();

//===============

struct epair_t {
    epair_t* next = nullptr;
    char* key = nullptr;
    char* value = nullptr;
};

struct entity_t {
    Vector3 origin;
    int firstbrush = 0;
    int numbrushes = 0;
    epair_t* epairs = nullptr;
};

extern int num_entities;
extern entity_t entities[MAX_MAP_ENTITIES];

void ParseEntities();
void UnparseEntities();

void SetKeyValue(entity_t* ent, const char* key, const char* value);
const char* ValueForKey(entity_t* ent, const char* key);
// will return "" if not present

vec_t FloatForKey(entity_t* ent, const char* key);
void GetVectorForKey(entity_t* ent, const char* key, Vector3& vec);

epair_t* ParseEpair();

#endif
