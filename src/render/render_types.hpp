// render_types.hpp -- Core Rendering and Visual Types
#pragma once

#include "core/types.hpp"
#include "core/math.hpp"

inline constexpr int VID_CBITS = 6;
inline constexpr int VID_GRADES = (1 << VID_CBITS);
inline constexpr int TOP_RANGE = 16;
inline constexpr int BOTTOM_RANGE = 96;

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

// Forward declarations
struct mnode_s;
struct mleaf_s;
struct model_s;

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
