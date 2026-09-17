// model.cpp -- In-memory model structures (BSP, MDL, SPR) & Model management
#include "quakedef.hpp"
#include "world/bsp_format.hpp"
#include "world/model.hpp"

using namespace Common;
using namespace Console;
using namespace Cmd;
using namespace Math;
using namespace Model;
using namespace Render;
using namespace Vid;

namespace Model {

static model_t* loadmodel = nullptr;
static char loadname[32] = {}; // for hunk tags

void Mod_LoadSpriteModel(model_t* mod, void* buffer);
void Mod_LoadBrushModel(model_t* mod, void* buffer);
void Mod_LoadAliasModel(model_t* mod, void* buffer);
model_t* Mod_LoadModel(model_t* mod, qboolean crash);

static byte mod_novis[MAX_MAP_LEAFS / 8];

constexpr int MAX_MOD_KNOWN = 256;
static eastl::array<model_t, MAX_MOD_KNOWN> mod_known;
static int mod_numknown = 0;

// values for model_t's needload
constexpr int NL_PRESENT = 0;
constexpr int NL_NEEDS_LOADED = 1;
constexpr int NL_UNREFERENCED = 2;

static byte* mod_base = nullptr;

void Mod_Init(void)
{
    std::memset(mod_novis, 0xff, sizeof(mod_novis));
    Cmd::AddCommand("modellist", Mod_Print);
}

void* Mod_Extradata(model_t* mod)
{
    if (!mod) {
        Sys_Error("Mod_Extradata: NULL mod");
    }
    void* r = Cache_Check(&mod->cache);
    if (r) {
        return r;
    }
    Mod_LoadModel(mod, true);
    if (!mod->cache.data) {
        Sys_Error("Mod_Extradata: caching failed");
    }
    return mod->cache.data;
}

mleaf_t* Mod_PointInLeaf(const Vector3& p, model_t* model)
{
    if (!model || !model->nodes) {
        Sys_Error("Mod_PointInLeaf: bad model");
    }
    mnode_t* node = model->nodes;
    while (true) {
        if (node->contents < 0) {
            return reinterpret_cast<mleaf_t*>(node);
        }
        mplane_t* plane = node->plane;
        float d = p.dot(plane->normal) - plane->dist;
        if (d > 0) {
            node = node->children[0];
        } else {
            node = node->children[1];
        }
    }
    return nullptr; // never reached
}

byte* Mod_DecompressVis(byte* in, model_t* model)
{
    static byte decompressed[MAX_MAP_LEAFS / 8];
    int row = (model->numleafs + 7) >> 3;
    byte* out = decompressed;
    if (!in) { // no vis info, so make all visible
        while (row) {
            *out++ = 0xff;
            row--;
        }
        return decompressed;
    }
    do {
        if (*in) {
            *out++ = *in++;
            continue;
        }
        int c = in[1];
        in += 2;
        while (c) {
            *out++ = 0;
            c--;
        }
    } while (out - decompressed < row);
    return decompressed;
}

byte* Mod_LeafPVS(mleaf_t* leaf, model_t* model)
{
    if (leaf == model->leafs) {
        return mod_novis;
    }
    return Mod_DecompressVis(leaf->compressed_vis, model);
}

void Mod_ClearAll(void)
{
    for (int i = 0; i < mod_numknown; ++i) {
        model_t& mod = mod_known[i];
        if (mod.type != mod_alias) {
            mod.needload = NL_NEEDS_LOADED;
        }
    }
}

model_t* Mod_FindName(const char* name)
{
    if (!name || !name[0]) {
        Sys_Error("Mod_ForName: NULL name");
    }
    model_t* avail = nullptr;
    // search the currently loaded models
    for (int i = 0; i < mod_numknown; ++i) {
        model_t* mod = &mod_known[i];
        if (std::strcmp(mod->name, name) == 0) {
            return mod;
        }
        if (mod->needload == NL_UNREFERENCED) {
            if (!avail || mod->type != mod_alias) {
                avail = mod;
            }
        }
    }
    model_t* mod = nullptr;
    if (mod_numknown == MAX_MOD_KNOWN) {
        if (avail) {
            mod = avail;
            if (mod->type == mod_alias) {
                if (Cache_Check(&mod->cache)) {
                    Cache_Free(&mod->cache);
                }
            }
        } else {
            Sys_Error("mod_numknown == MAX_MOD_KNOWN");
        }
    } else {
        mod = &mod_known[mod_numknown];
        mod_numknown++;
    }
    strcpy_s(mod->name, sizeof(mod->name), name);
    mod->needload = NL_NEEDS_LOADED;
    return mod;
}

void Mod_TouchModel(char* name)
{
    model_t* mod = Mod_FindName(name);
    if (mod->needload == NL_PRESENT) {
        if (mod->type == mod_alias) {
            Cache_Check(&mod->cache);
        }
    }
}

model_t* Mod_LoadModel(model_t* mod, qboolean crash)
{
    if (mod->type == mod_alias) {
        if (Cache_Check(&mod->cache)) {
            mod->needload = NL_PRESENT;
            return mod;
        }
    } else {
        if (mod->needload == NL_PRESENT) {
            return mod;
        }
    }
    // load the file
    byte stackbuf[1024]; // avoid dirtying the cache heap
    unsigned* buf = reinterpret_cast<unsigned*>(COM_LoadStackFile(mod->name, stackbuf, sizeof(stackbuf)));
    if (!buf) {
        if (crash) {
            Sys_Error("Mod_NumForName: %s not found", mod->name);
        }
        return nullptr;
    }
    // allocate a new model
    COM_FileBase(mod->name, loadname);
    loadmodel = mod;
    // fill it in
    mod->needload = NL_PRESENT;
    switch (LittleLong(*buf)) {
    case IDPOLYHEADER:
        Mod_LoadAliasModel(mod, buf);
        break;
    case IDSPRITEHEADER:
        Mod_LoadSpriteModel(mod, buf);
        break;
    default:
        Mod_LoadBrushModel(mod, buf);
        break;
    }
    return mod;
}

model_t* Mod_ForName(const char* name, qboolean crash)
{
    model_t* mod = Mod_FindName(name);
    return Mod_LoadModel(mod, crash);
}

void Mod_LoadTextures(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->textures = nullptr;
        return;
    }
    dmiptexlump_t* m = reinterpret_cast<dmiptexlump_t*>(mod_base + l->fileofs);
    m->nummiptex = LittleLong(m->nummiptex);
    loadmodel->numtextures = m->nummiptex;
    loadmodel->textures_owner.resize(m->nummiptex);
    loadmodel->textures = loadmodel->textures_owner.data();
    loadmodel->texture_allocations.clear();
    for (int i = 0; i < m->nummiptex; i++) {
        m->dataofs[i] = LittleLong(m->dataofs[i]);
        if (m->dataofs[i] == -1) {
            loadmodel->textures[i] = nullptr;
            continue;
        }
        miptex_t* mt = reinterpret_cast<miptex_t*>(reinterpret_cast<byte*>(m) + m->dataofs[i]);
        mt->width = LittleLong(mt->width);
        mt->height = LittleLong(mt->height);
        for (int j = 0; j < MIPLEVELS; j++) {
            mt->offsets[j] = LittleLong(mt->offsets[j]);
        }
        if ((mt->width & 15) || (mt->height & 15)) {
            Sys_Error("Texture %s is not 16 aligned", mt->name);
        }
        int pixels = mt->width * mt->height / 64 * 85;
        int texture_size = sizeof(texture_t) + pixels;
        loadmodel->texture_allocations.emplace_back();
        auto& tex_buf = loadmodel->texture_allocations.back();
        tex_buf.resize(texture_size);
        texture_t* tx = reinterpret_cast<texture_t*>(tex_buf.data());
        loadmodel->textures[i] = tx;
        std::memcpy(tx->name, mt->name, sizeof(tx->name));
        tx->width = mt->width;
        tx->height = mt->height;
        for (int j = 0; j < MIPLEVELS; j++) {
            tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
        }
        // the pixels immediately follow the structures
        std::memcpy(static_cast<void*>(tx + 1), mt + 1, pixels);
        if (Q_strncmp(mt->name, "sky", 3) == 0) {
            R_InitSky(tx);
        }
    }
    // sequence the animations
    texture_t* anims[10];
    texture_t* altanims[10];
    for (int i = 0; i < m->nummiptex; i++) {
        texture_t* tx = loadmodel->textures[i];
        if (!tx || tx->name[0] != '+') {
            continue;
        }
        if (tx->anim_next) {
            continue; // allready sequenced
        }
        // find the number of frames in the animation
        std::memset(anims, 0, sizeof(anims));
        std::memset(altanims, 0, sizeof(altanims));
        int max = tx->name[1];
        if (max >= 'a' && max <= 'z') {
            max -= 'a' - 'A';
        }
        int altmax = 0;
        if (max >= '0' && max <= '9') {
            max -= '0';
            anims[max] = tx;
            max++;
        } else if (max >= 'A' && max <= 'J') {
            altmax = max - 'A';
            max = 0;
            altanims[altmax] = tx;
            altmax++;
        } else {
            Sys_Error("Bad animating texture %s", tx->name);
        }
        for (int j = i + 1; j < m->nummiptex; j++) {
            texture_t* tx2 = loadmodel->textures[j];
            if (!tx2 || tx2->name[0] != '+') {
                continue;
            }
            if (std::strcmp(tx2->name + 2, tx->name + 2) != 0) {
                continue;
            }
            int num = tx2->name[1];
            if (num >= 'a' && num <= 'z') {
                num -= 'a' - 'A';
            }
            if (num >= '0' && num <= '9') {
                num -= '0';
                anims[num] = tx2;
                if (num + 1 > max) {
                    max = num + 1;
                }
            } else if (num >= 'A' && num <= 'J') {
                num = num - 'A';
                altanims[num] = tx2;
                if (num + 1 > altmax) {
                    altmax = num + 1;
                }
            } else {
                Sys_Error("Bad animating texture %s", tx->name);
            }
        }
        constexpr int ANIM_CYCLE = 2;
        // link them all together
        for (int j = 0; j < max; j++) {
            texture_t* tx2 = anims[j];
            if (!tx2) {
                Sys_Error("Missing frame %i of %s", j, tx->name);
            }
            tx2->anim_total = max * ANIM_CYCLE;
            tx2->anim_min = j * ANIM_CYCLE;
            tx2->anim_max = (j + 1) * ANIM_CYCLE;
            tx2->anim_next = anims[(j + 1) % max];
            if (altmax) {
                tx2->alternate_anims = altanims[0];
            }
        }
        for (int j = 0; j < altmax; j++) {
            texture_t* tx2 = altanims[j];
            if (!tx2) {
                Sys_Error("Missing frame %i of %s", j, tx->name);
            }
            tx2->anim_total = altmax * ANIM_CYCLE;
            tx2->anim_min = j * ANIM_CYCLE;
            tx2->anim_max = (j + 1) * ANIM_CYCLE;
            tx2->anim_next = altanims[(j + 1) % altmax];
            if (max) {
                tx2->alternate_anims = anims[0];
            }
        }
    }
}

void Mod_LoadLighting(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->lightdata = nullptr;
        return;
    }
    loadmodel->lightdata_owner.resize(l->filelen);
    loadmodel->lightdata = loadmodel->lightdata_owner.data();
    std::memcpy(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}

void Mod_LoadVisibility(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->visdata = nullptr;
        return;
    }
    loadmodel->visdata_owner.resize(l->filelen);
    loadmodel->visdata = loadmodel->visdata_owner.data();
    std::memcpy(loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}

void Mod_LoadEntities(lump_t* l)
{
    if (!l->filelen) {
        loadmodel->entities = nullptr;
        return;
    }
    loadmodel->entities_owner.resize(l->filelen);
    loadmodel->entities = loadmodel->entities_owner.data();
    std::memcpy(loadmodel->entities, mod_base + l->fileofs, l->filelen);
}

void Mod_LoadVertexes(lump_t* l)
{
    if (l->filelen % sizeof(dvertex_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dvertex_t);
    loadmodel->vertexes_owner.resize(count);
    loadmodel->vertexes = loadmodel->vertexes_owner.data();
    loadmodel->numvertexes = count;
    dvertex_t* in = reinterpret_cast<dvertex_t*>(mod_base + l->fileofs);
    mvertex_t* out = loadmodel->vertexes;
    for (int i = 0; i < count; i++, in++, out++) {
        out->position[0] = LittleFloat(in->point[0]);
        out->position[1] = LittleFloat(in->point[1]);
        out->position[2] = LittleFloat(in->point[2]);
    }
}

void Mod_LoadSubmodels(lump_t* l)
{
    if (l->filelen % sizeof(dmodel_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dmodel_t);
    loadmodel->submodels_owner.resize(count);
    loadmodel->submodels = loadmodel->submodels_owner.data();
    loadmodel->numsubmodels = count;
    dmodel_t* in = reinterpret_cast<dmodel_t*>(mod_base + l->fileofs);
    dmodel_t* out = loadmodel->submodels;
    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 3; j++) { // spread the mins / maxs by a pixel
            out->mins[j] = LittleFloat(in->mins[j]) - 1;
            out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
            out->origin[j] = LittleFloat(in->origin[j]);
        }
        for (int j = 0; j < MAX_MAP_HULLS; j++) {
            out->headnode[j] = LittleLong(in->headnode[j]);
        }
        out->visleafs = LittleLong(in->visleafs);
        out->firstface = LittleLong(in->firstface);
        out->numfaces = LittleLong(in->numfaces);
    }
}

void Mod_LoadEdges(lump_t* l)
{
    if (l->filelen % sizeof(dedge_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dedge_t);
    loadmodel->edges_owner.resize(count + 1);
    loadmodel->edges = loadmodel->edges_owner.data();
    loadmodel->numedges = count;
    dedge_t* in = reinterpret_cast<dedge_t*>(mod_base + l->fileofs);
    medge_t* out = loadmodel->edges;
    for (int i = 0; i < count; i++, in++, out++) {
        out->v[0] = static_cast<unsigned short>(LittleShort(in->v[0]));
        out->v[1] = static_cast<unsigned short>(LittleShort(in->v[1]));
    }
}

void Mod_LoadTexinfo(lump_t* l)
{
    if (l->filelen % sizeof(texinfo_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(texinfo_t);
    loadmodel->texinfo_owner.resize(count);
    loadmodel->texinfo = loadmodel->texinfo_owner.data();
    loadmodel->numtexinfo = count;
    texinfo_t* in = reinterpret_cast<texinfo_t*>(mod_base + l->fileofs);
    mtexinfo_t* out = loadmodel->texinfo;
    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 4; j++) {
            out->vecs[0][j] = LittleFloat(in->vecs[0][j]);
            out->vecs[1][j] = LittleFloat(in->vecs[1][j]);
        }
        float len1 = Length(out->vecs[0]);
        float len2 = Length(out->vecs[1]);
        len1 = (len1 + len2) / 2;
        if (len1 < 0.32) {
            out->mipadjust = 4;
        } else if (len1 < 0.49) {
            out->mipadjust = 3;
        } else if (len1 < 0.99) {
            out->mipadjust = 2;
        } else {
            out->mipadjust = 1;
        }
        int miptex = LittleLong(in->miptex);
        out->flags = LittleLong(in->flags);
        if (!loadmodel->textures) {
            out->texture = r_notexture_mip; // checkerboard texture
            out->flags = 0;
        } else {
            if (miptex >= loadmodel->numtextures) {
                Sys_Error("miptex >= loadmodel->numtextures");
            }
            out->texture = loadmodel->textures[miptex];
            if (!out->texture) {
                out->texture = r_notexture_mip; // texture not found
                out->flags = 0;
            }
        }
    }
}

void CalcSurfaceExtents(msurface_t* s)
{
    float mins[2] = {999999.0f, 999999.0f};
    float maxs[2] = {-99999.0f, -99999.0f};
    mtexinfo_t* tex = s->texinfo;
    for (int i = 0; i < s->numedges; i++) {
        int e = loadmodel->surfedges[s->firstedge + i];
        mvertex_t* v;
        if (e >= 0) {
            v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
        } else {
            v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
        }
        for (int j = 0; j < 2; j++) {
            float val = v->position[0] * tex->vecs[j][0] + v->position[1] * tex->vecs[j][1] + v->position[2] * tex->vecs[j][2] + tex->vecs[j][3];
            if (val < mins[j]) {
                mins[j] = val;
            }
            if (val > maxs[j]) {
                maxs[j] = val;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        int bmins = static_cast<int>(std::floor(mins[i] / 16.0f));
        int bmaxs = static_cast<int>(std::ceil(maxs[i] / 16.0f));
        s->texturemins[i] = static_cast<short>(bmins * 16);
        s->extents[i] = static_cast<short>((bmaxs - bmins) * 16);
        if (!(tex->flags & TEX_SPECIAL) && s->extents[i] > 256) {
            Sys_Error("Bad surface extents");
        }
    }
}

void Mod_LoadFaces(lump_t* l)
{
    if (l->filelen % sizeof(dface_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dface_t);
    loadmodel->surfaces_owner.resize(count);
    loadmodel->surfaces = loadmodel->surfaces_owner.data();
    loadmodel->numsurfaces = count;
    dface_t* in = reinterpret_cast<dface_t*>(mod_base + l->fileofs);
    msurface_t* out = loadmodel->surfaces;
    for (int surfnum = 0; surfnum < count; surfnum++, in++, out++) {
        out->firstedge = LittleLong(in->firstedge);
        out->numedges = LittleShort(in->numedges);
        out->flags = 0;
        int planenum = LittleShort(in->planenum);
        int side = LittleShort(in->side);
        if (side) {
            out->flags |= SURF_PLANEBACK;
        }
        out->plane = loadmodel->planes + planenum;
        out->texinfo = loadmodel->texinfo + LittleShort(in->texinfo);
        CalcSurfaceExtents(out);
        // lighting info
        for (int i = 0; i < MAXLIGHTMAPS; i++) {
            out->styles[i] = in->styles[i];
        }
        int i = LittleLong(in->lightofs);
        if (i == -1) {
            out->samples = nullptr;
        } else {
            out->samples = loadmodel->lightdata + i;
        }
        // set the drawing flags flag
        if (Q_strncmp(out->texinfo->texture->name, "sky", 3) == 0) // sky
        {
            out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
            continue;
        }
        if (Q_strncmp(out->texinfo->texture->name, "*", 1) == 0) // turbulent
        {
            out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
            for (int j = 0; j < 2; j++) {
                out->extents[j] = 16384;
                out->texturemins[j] = -8192;
            }
            continue;
        }
    }
}

void Mod_SetParent(mnode_t* node, mnode_t* parent)
{
    node->parent = parent;
    if (node->contents < 0) {
        return;
    }
    Mod_SetParent(node->children[0], node);
    Mod_SetParent(node->children[1], node);
}

void Mod_LoadNodes(lump_t* l)
{
    if (l->filelen % sizeof(dnode_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dnode_t);
    loadmodel->nodes_owner.resize(count);
    loadmodel->nodes = loadmodel->nodes_owner.data();
    loadmodel->numnodes = count;
    dnode_t* in = reinterpret_cast<dnode_t*>(mod_base + l->fileofs);
    mnode_t* out = loadmodel->nodes;
    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 3; j++) {
            out->minmaxs[j] = LittleShort(in->mins[j]);
            out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
        }
        int p = LittleLong(in->planenum);
        out->plane = loadmodel->planes + p;
        out->firstsurface = LittleShort(in->firstface);
        out->numsurfaces = LittleShort(in->numfaces);
        for (int j = 0; j < 2; j++) {
            p = LittleShort(in->children[j]);
            if (p >= 0) {
                out->children[j] = loadmodel->nodes + p;
            } else {
                out->children[j] = reinterpret_cast<mnode_t*>(loadmodel->leafs + (-1 - p));
            }
        }
    }
    Mod_SetParent(loadmodel->nodes, nullptr); // sets nodes and leafs
}

void Mod_LoadLeafs(lump_t* l)
{
    if (l->filelen % sizeof(dleaf_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dleaf_t);
    loadmodel->leafs_owner.resize(count);
    loadmodel->leafs = loadmodel->leafs_owner.data();
    loadmodel->numleafs = count;
    dleaf_t* in = reinterpret_cast<dleaf_t*>(mod_base + l->fileofs);
    mleaf_t* out = loadmodel->leafs;
    for (int i = 0; i < count; i++, in++, out++) {
        for (int j = 0; j < 3; j++) {
            out->minmaxs[j] = LittleShort(in->mins[j]);
            out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
        }
        int p = LittleLong(in->contents);
        out->contents = p;
        out->firstmarksurface = loadmodel->marksurfaces + LittleShort(in->firstmarksurface);
        out->nummarksurfaces = LittleShort(in->nummarksurfaces);
        p = LittleLong(in->visofs);
        if (p == -1) {
            out->compressed_vis = nullptr;
        } else {
            out->compressed_vis = loadmodel->visdata + p;
        }
        out->efrags = nullptr;
        for (int j = 0; j < 4; j++) {
            out->ambient_sound_level[j] = in->ambient_level[j];
        }
    }
}

void Mod_LoadClipnodes(lump_t* l)
{
    if (l->filelen % sizeof(dclipnode_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dclipnode_t);
    loadmodel->clipnodes_owner.resize(count);
    loadmodel->clipnodes = loadmodel->clipnodes_owner.data();
    loadmodel->numclipnodes = count;
    dclipnode_t* in = reinterpret_cast<dclipnode_t*>(mod_base + l->fileofs);
    dclipnode_t* out = loadmodel->clipnodes;
    hull_t* hull = &loadmodel->hulls[1];
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    hull->clip_mins[0] = -16;
    hull->clip_mins[1] = -16;
    hull->clip_mins[2] = -24;
    hull->clip_maxs[0] = 16;
    hull->clip_maxs[1] = 16;
    hull->clip_maxs[2] = 32;
    hull = &loadmodel->hulls[2];
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    hull->clip_mins[0] = -32;
    hull->clip_mins[1] = -32;
    hull->clip_mins[2] = -24;
    hull->clip_maxs[0] = 32;
    hull->clip_maxs[1] = 32;
    hull->clip_maxs[2] = 64;
    for (int i = 0; i < count; i++, out++, in++) {
        out->planenum = LittleLong(in->planenum);
        out->children[0] = LittleShort(in->children[0]);
        out->children[1] = LittleShort(in->children[1]);
    }
}

void Mod_MakeHull0(void)
{
    hull_t* hull = &loadmodel->hulls[0];
    mnode_t* in = loadmodel->nodes;
    int count = loadmodel->numnodes;
    loadmodel->hull0_clipnodes_owner.resize(count);
    dclipnode_t* out = loadmodel->hull0_clipnodes_owner.data();
    hull->clipnodes = out;
    hull->firstclipnode = 0;
    hull->lastclipnode = count - 1;
    hull->planes = loadmodel->planes;
    for (int i = 0; i < count; i++, out++, in++) {
        out->planenum = static_cast<int>(in->plane - loadmodel->planes);
        for (int j = 0; j < 2; j++) {
            mnode_t* child = in->children[j];
            if (child->contents < 0) {
                out->children[j] = static_cast<short>(child->contents);
            } else {
                out->children[j] = static_cast<short>(child - loadmodel->nodes);
            }
        }
    }
}

void Mod_LoadMarksurfaces(lump_t* l)
{
    if (l->filelen % sizeof(short)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(short);
    loadmodel->marksurfaces_owner.resize(count);
    loadmodel->marksurfaces = loadmodel->marksurfaces_owner.data();
    loadmodel->nummarksurfaces = count;
    short* in = reinterpret_cast<short*>(mod_base + l->fileofs);
    msurface_t** out = loadmodel->marksurfaces;
    for (int i = 0; i < count; i++) {
        int j = LittleShort(in[i]);
        if (j >= loadmodel->numsurfaces) {
            Sys_Error("Mod_ParseMarksurfaces: bad surface number");
        }
        out[i] = loadmodel->surfaces + j;
    }
}

void Mod_LoadSurfedges(lump_t* l)
{
    if (l->filelen % sizeof(int)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(int);
    loadmodel->surfedges_owner.resize(count);
    loadmodel->surfedges = loadmodel->surfedges_owner.data();
    loadmodel->numsurfedges = count;
    int* in = reinterpret_cast<int*>(mod_base + l->fileofs);
    int* out = loadmodel->surfedges;
    for (int i = 0; i < count; i++) {
        out[i] = LittleLong(in[i]);
    }
}

void Mod_LoadPlanes(lump_t* l)
{
    if (l->filelen % sizeof(dplane_t)) {
        Sys_Error("MOD_LoadBmodel: funny lump size in %s", loadmodel->name);
    }
    int count = l->filelen / sizeof(dplane_t);
    loadmodel->planes_owner.resize(count);
    loadmodel->planes = loadmodel->planes_owner.data();
    loadmodel->numplanes = count;
    dplane_t* in = reinterpret_cast<dplane_t*>(mod_base + l->fileofs);
    mplane_t* out = loadmodel->planes;
    for (int i = 0; i < count; i++, in++, out++) {
        int bits = 0;
        for (int j = 0; j < 3; j++) {
            out->normal[j] = LittleFloat(in->normal[j]);
            if (out->normal[j] < 0) {
                bits |= 1 << j;
            }
        }
        out->dist = static_cast<float>(LittleFloat(in->dist));
        out->type = static_cast<byte>(LittleLong(in->type));
        out->signbits = static_cast<byte>(bits);
    }
}

float RadiusFromBounds(const Vector3& mins, const Vector3& maxs)
{
    Vector3 corner;
    corner.x = std::max(std::fabs(mins.x), std::fabs(maxs.x));
    corner.y = std::max(std::fabs(mins.y), std::fabs(maxs.y));
    corner.z = std::max(std::fabs(mins.z), std::fabs(maxs.z));
    return corner.length();
}

void Mod_LoadBrushModel(model_t* mod, void* buffer)
{
    loadmodel->type = mod_brush;
    dheader_t* header = reinterpret_cast<dheader_t*>(buffer);
    int version = LittleLong(header->version);
    if (version != BSPVERSION) {
        Sys_Error(
            "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)",
            mod->name, version, BSPVERSION);
    }
    // swap all the lumps
    mod_base = reinterpret_cast<byte*>(header);
    for (size_t i = 0; i < sizeof(dheader_t) / 4; i++) {
        reinterpret_cast<int*>(header)[i] = LittleLong(reinterpret_cast<int*>(header)[i]);
    }
    // load into heap
    Mod_LoadVertexes(&header->lumps[LUMP_VERTEXES]);
    Mod_LoadEdges(&header->lumps[LUMP_EDGES]);
    Mod_LoadSurfedges(&header->lumps[LUMP_SURFEDGES]);
    Mod_LoadTextures(&header->lumps[LUMP_TEXTURES]);
    Mod_LoadLighting(&header->lumps[LUMP_LIGHTING]);
    Mod_LoadPlanes(&header->lumps[LUMP_PLANES]);
    Mod_LoadTexinfo(&header->lumps[LUMP_TEXINFO]);
    Mod_LoadFaces(&header->lumps[LUMP_FACES]);
    Mod_LoadMarksurfaces(&header->lumps[LUMP_MARKSURFACES]);
    Mod_LoadVisibility(&header->lumps[LUMP_VISIBILITY]);
    Mod_LoadLeafs(&header->lumps[LUMP_LEAFS]);
    Mod_LoadNodes(&header->lumps[LUMP_NODES]);
    Mod_LoadClipnodes(&header->lumps[LUMP_CLIPNODES]);
    Mod_LoadEntities(&header->lumps[LUMP_ENTITIES]);
    Mod_LoadSubmodels(&header->lumps[LUMP_MODELS]);
    Mod_MakeHull0();
    mod->numframes = 2; // regular and alternate animation
    mod->flags = 0;
    //
    // set up the submodels (FIXME: this is confusing)
    //
    for (int i = 0; i < mod->numsubmodels; i++) {
        dmodel_t* bm = &mod->submodels[i];
        mod->hulls[0].firstclipnode = bm->headnode[0];
        for (int j = 1; j < MAX_MAP_HULLS; j++) {
            mod->hulls[j].firstclipnode = bm->headnode[j];
            mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
        }
        mod->firstmodelsurface = bm->firstface;
        mod->nummodelsurfaces = bm->numfaces;
        VectorCopy(bm->maxs, mod->maxs);
        VectorCopy(bm->mins, mod->mins);
        mod->radius = RadiusFromBounds(mod->mins, mod->maxs);
        mod->numleafs = bm->visleafs;
        if (i < mod->numsubmodels - 1) { // duplicate the basic information
            char name[10];
            sprintf_s(name, sizeof(name), "*%i", i + 1);
            loadmodel = Mod_FindName(name);
            *loadmodel = *mod;
            strcpy_s(loadmodel->name, sizeof(loadmodel->name), name);
            mod = loadmodel;
        }
    }
}

void* Mod_LoadAliasFrame(void* pin,
    int* pframeindex,
    int numv,
    trivertx_t* pbboxmin,
    trivertx_t* pbboxmax,
    aliashdr_t* pheader,
    char* name)
{
    daliasframe_t* pdaliasframe = reinterpret_cast<daliasframe_t*>(pin);
    strcpy_s(name, 16, pdaliasframe->name);
    for (int i = 0; i < 3; i++) {
        // these are byte values, so we don't have to worry about
        // endianness
        pbboxmin->v[i] = pdaliasframe->bboxmin.v[i];
        pbboxmax->v[i] = pdaliasframe->bboxmax.v[i];
    }
    trivertx_t* pinframe = reinterpret_cast<trivertx_t*>(pdaliasframe + 1);
    trivertx_t* pframe = reinterpret_cast<trivertx_t*>(Hunk_Alloc(numv * sizeof(*pframe), loadname));
    *pframeindex = static_cast<int>(reinterpret_cast<byte*>(pframe) - reinterpret_cast<byte*>(pheader));
    for (int j = 0; j < numv; j++) {
        // these are all byte values, so no need to deal with endianness
        pframe[j].lightnormalindex = pinframe[j].lightnormalindex;
        for (int k = 0; k < 3; k++) {
            pframe[j].v[k] = pinframe[j].v[k];
        }
    }
    pinframe += numv;
    return reinterpret_cast<void*>(pinframe);
}

void* Mod_LoadAliasGroup(void* pin,
    int* pframeindex,
    int numv,
    trivertx_t* pbboxmin,
    trivertx_t* pbboxmax,
    aliashdr_t* pheader,
    char* name)
{
    daliasgroup_t* pingroup = reinterpret_cast<daliasgroup_t*>(pin);
    int numframes = LittleLong(pingroup->numframes);
    maliasgroup_t* paliasgroup = reinterpret_cast<maliasgroup_t*>(Hunk_Alloc(
        sizeof(maliasgroup_t) + (numframes - 1) * sizeof(paliasgroup->frames[0]),
        loadname));
    paliasgroup->numframes = numframes;
    for (int i = 0; i < 3; i++) {
        // these are byte values, so we don't have to worry about endianness
        pbboxmin->v[i] = pingroup->bboxmin.v[i];
        pbboxmax->v[i] = pingroup->bboxmax.v[i];
    }
    *pframeindex = static_cast<int>(reinterpret_cast<byte*>(paliasgroup) - reinterpret_cast<byte*>(pheader));
    daliasinterval_t* pin_intervals = reinterpret_cast<daliasinterval_t*>(pingroup + 1);
    float* poutintervals = reinterpret_cast<float*>(Hunk_Alloc(numframes * sizeof(float), loadname));
    paliasgroup->intervals = static_cast<int>(reinterpret_cast<byte*>(poutintervals) - reinterpret_cast<byte*>(pheader));
    for (int i = 0; i < numframes; i++) {
        *poutintervals = LittleFloat(pin_intervals->interval);
        if (*poutintervals <= 0.0) {
            Sys_Error("Mod_LoadAliasGroup: interval<=0");
        }
        poutintervals++;
        pin_intervals++;
    }
    void* ptemp = reinterpret_cast<void*>(pin_intervals);
    for (int i = 0; i < numframes; i++) {
        ptemp = Mod_LoadAliasFrame(ptemp, &paliasgroup->frames[i].frame, numv,
            &paliasgroup->frames[i].bboxmin,
            &paliasgroup->frames[i].bboxmax, pheader, name);
    }
    return ptemp;
}

void* Mod_LoadAliasSkin(void* pin,
    int* pskinindex,
    int skinsize,
    aliashdr_t* pheader)
{
    byte* pskin = reinterpret_cast<byte*>(Hunk_Alloc(skinsize * r_pixbytes, loadname));
    byte* pinskin = reinterpret_cast<byte*>(pin);
    *pskinindex = static_cast<int>(reinterpret_cast<byte*>(pskin) - reinterpret_cast<byte*>(pheader));
    if (r_pixbytes == 1) {
        Q_memcpy(pskin, pinskin, skinsize);
    } else if (r_pixbytes == 2) {
        unsigned short* pusskin = reinterpret_cast<unsigned short*>(pskin);
        for (int i = 0; i < skinsize; i++) {
            pusskin[i] = d_8to16table[pinskin[i]];
        }
    } else {
        Sys_Error("Mod_LoadAliasSkin: driver set invalid r_pixbytes: %d\n",
            r_pixbytes);
    }
    pinskin += skinsize;
    return reinterpret_cast<void*>(pinskin);
}

void* Mod_LoadAliasSkinGroup(void* pin,
    int* pskinindex,
    int skinsize,
    aliashdr_t* pheader)
{
    daliasskingroup_t* pinskingroup = reinterpret_cast<daliasskingroup_t*>(pin);
    int numskins = LittleLong(pinskingroup->numskins);
    maliasskingroup_t* paliasskingroup = reinterpret_cast<maliasskingroup_t*>(Hunk_Alloc(
        sizeof(maliasskingroup_t) + (numskins - 1) * sizeof(paliasskingroup->skindescs[0]),
        loadname));
    paliasskingroup->numskins = numskins;
    *pskinindex = static_cast<int>(reinterpret_cast<byte*>(paliasskingroup) - reinterpret_cast<byte*>(pheader));
    daliasskininterval_t* pinskinintervals = reinterpret_cast<daliasskininterval_t*>(pinskingroup + 1);
    float* poutskinintervals = reinterpret_cast<float*>(Hunk_Alloc(numskins * sizeof(float), loadname));
    paliasskingroup->intervals = static_cast<int>(reinterpret_cast<byte*>(poutskinintervals) - reinterpret_cast<byte*>(pheader));
    for (int i = 0; i < numskins; i++) {
        *poutskinintervals = LittleFloat(pinskinintervals->interval);
        if (*poutskinintervals <= 0) {
            Sys_Error("Mod_LoadAliasSkinGroup: interval<=0");
        }
        poutskinintervals++;
        pinskinintervals++;
    }
    void* ptemp = reinterpret_cast<void*>(pinskinintervals);
    for (int i = 0; i < numskins; i++) {
        ptemp = Mod_LoadAliasSkin(ptemp, &paliasskingroup->skindescs[i].skin,
            skinsize, pheader);
    }
    return ptemp;
}

void Mod_LoadAliasModel(model_t* mod, void* buffer)
{
    int start = Hunk_LowMark();
    mdl_t* pinmodel = reinterpret_cast<mdl_t*>(buffer);
    int version = LittleLong(pinmodel->version);
    if (version != ALIAS_VERSION) {
        Sys_Error("%s has wrong version number (%i should be %i)", mod->name,
            version, ALIAS_VERSION);
    }
    //
    // allocate space for a working header, plus all the data except the frames,
    // skin and group info
    //
    int size = sizeof(aliashdr_t) + (LittleLong(pinmodel->numframes) - 1) * sizeof(aliashdr_t::frames[0]) + sizeof(mdl_t) + LittleLong(pinmodel->numverts) * sizeof(stvert_t) + LittleLong(pinmodel->numtris) * sizeof(mtriangle_t);
    aliashdr_t* pheader = reinterpret_cast<aliashdr_t*>(Hunk_Alloc(size, loadname));
    mdl_t* pmodel = reinterpret_cast<mdl_t*>(reinterpret_cast<byte*>(&pheader[1]) + (LittleLong(pinmodel->numframes) - 1) * sizeof(pheader->frames[0]));
    mod->flags = LittleLong(pinmodel->flags);
    //
    // endian-adjust and copy the data, starting with the alias model header
    //
    pmodel->boundingradius = LittleFloat(pinmodel->boundingradius);
    pmodel->numskins = LittleLong(pinmodel->numskins);
    pmodel->skinwidth = LittleLong(pinmodel->skinwidth);
    pmodel->skinheight = LittleLong(pinmodel->skinheight);
    if (pmodel->skinheight > MAX_LBM_HEIGHT) {
        Sys_Error("model %s has a skin taller than %d", mod->name, MAX_LBM_HEIGHT);
    }
    pmodel->numverts = LittleLong(pinmodel->numverts);
    if (pmodel->numverts <= 0) {
        Sys_Error("model %s has no vertices", mod->name);
    }
    if (pmodel->numverts > MAXALIASVERTS) {
        Sys_Error("model %s has too many vertices", mod->name);
    }
    pmodel->numtris = LittleLong(pinmodel->numtris);
    if (pmodel->numtris <= 0) {
        Sys_Error("model %s has no triangles", mod->name);
    }
    pmodel->numframes = LittleLong(pinmodel->numframes);
    pmodel->size = static_cast<float>(LittleFloat(pinmodel->size) * ALIAS_BASE_SIZE_RATIO);
    mod->synctype = static_cast<synctype_t>(LittleLong(static_cast<int>(pinmodel->synctype)));
    mod->numframes = pmodel->numframes;
    for (int i = 0; i < 3; i++) {
        pmodel->scale[i] = LittleFloat(pinmodel->scale[i]);
        pmodel->scale_origin[i] = LittleFloat(pinmodel->scale_origin[i]);
        pmodel->eyeposition[i] = LittleFloat(pinmodel->eyeposition[i]);
    }
    int numskins = pmodel->numskins;
    int numframes = pmodel->numframes;
    if (pmodel->skinwidth & 0x03) {
        Sys_Error("Mod_LoadAliasModel: skinwidth not multiple of 4");
    }
    pheader->model = static_cast<int>(reinterpret_cast<byte*>(pmodel) - reinterpret_cast<byte*>(pheader));
    //
    // load the skins
    //
    int skinsize = pmodel->skinheight * pmodel->skinwidth;
    if (numskins < 1) {
        Sys_Error("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);
    }
    daliasskintype_t* pskintype = reinterpret_cast<daliasskintype_t*>(&pinmodel[1]);
    maliasskindesc_t* pskindesc = reinterpret_cast<maliasskindesc_t*>(Hunk_Alloc(numskins * sizeof(maliasskindesc_t), loadname));
    pheader->skindesc = static_cast<int>(reinterpret_cast<byte*>(pskindesc) - reinterpret_cast<byte*>(pheader));
    for (int i = 0; i < numskins; i++) {
        aliasskintype_t skintype = static_cast<aliasskintype_t>(LittleLong(static_cast<int>(pskintype->type)));
        pskindesc[i].type = skintype;
        if (skintype == aliasskintype_t::ALIAS_SKIN_SINGLE) {
            pskintype = reinterpret_cast<daliasskintype_t*>(Mod_LoadAliasSkin(
                pskintype + 1, &pskindesc[i].skin, skinsize, pheader));
        } else {
            pskintype = reinterpret_cast<daliasskintype_t*>(Mod_LoadAliasSkinGroup(
                pskintype + 1, &pskindesc[i].skin, skinsize, pheader));
        }
    }
    //
    // set base s and t vertices
    //
    stvert_t* pstverts = reinterpret_cast<stvert_t*>(&pmodel[1]);
    stvert_t* pinstverts = reinterpret_cast<stvert_t*>(pskintype);
    pheader->stverts = static_cast<int>(reinterpret_cast<byte*>(pstverts) - reinterpret_cast<byte*>(pheader));
    for (int i = 0; i < pmodel->numverts; i++) {
        pstverts[i].onseam = LittleLong(pinstverts[i].onseam);
        // put s and t in 16.16 format
        pstverts[i].s = LittleLong(pinstverts[i].s) << 16;
        pstverts[i].t = LittleLong(pinstverts[i].t) << 16;
    }
    //
    // set up the triangles
    //
    mtriangle_t* ptri = reinterpret_cast<mtriangle_t*>(&pstverts[pmodel->numverts]);
    dtriangle_t* pintriangles = reinterpret_cast<dtriangle_t*>(&pinstverts[pmodel->numverts]);
    pheader->triangles = static_cast<int>(reinterpret_cast<byte*>(ptri) - reinterpret_cast<byte*>(pheader));
    for (int i = 0; i < pmodel->numtris; i++) {
        ptri[i].facesfront = LittleLong(pintriangles[i].facesfront);
        for (int j = 0; j < 3; j++) {
            ptri[i].vertindex[j] = LittleLong(pintriangles[i].vertindex[j]);
        }
    }
    //
    // load the frames
    //
    if (numframes < 1) {
        Sys_Error("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);
    }
    daliasframetype_t* pframetype = reinterpret_cast<daliasframetype_t*>(&pintriangles[pmodel->numtris]);
    for (int i = 0; i < numframes; i++) {
        aliasframetype_t frametype = static_cast<aliasframetype_t>(LittleLong(static_cast<int>(pframetype->type)));
        pheader->frames[i].type = frametype;
        if (frametype == aliasframetype_t::ALIAS_SINGLE) {
            pframetype = reinterpret_cast<daliasframetype_t*>(Mod_LoadAliasFrame(
                pframetype + 1, &pheader->frames[i].frame, pmodel->numverts,
                &pheader->frames[i].bboxmin, &pheader->frames[i].bboxmax, pheader,
                pheader->frames[i].name));
        } else {
            pframetype = reinterpret_cast<daliasframetype_t*>(Mod_LoadAliasGroup(
                pframetype + 1, &pheader->frames[i].frame, pmodel->numverts,
                &pheader->frames[i].bboxmin, &pheader->frames[i].bboxmax, pheader,
                pheader->frames[i].name));
        }
    }
    mod->type = mod_alias;
    // FIXME: do this right
    mod->mins[0] = mod->mins[1] = mod->mins[2] = -16.0f;
    mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16.0f;
    //
    // move the complete, relocatable alias model to the cache
    //
    int end = Hunk_LowMark();
    int total = end - start;
    Cache_Alloc(&mod->cache, total, loadname);
    if (!mod->cache.data) {
        return;
    }
    std::memcpy(mod->cache.data, pheader, total);
    Hunk_FreeToLowMark(start);
}
void* Mod_LoadSpriteFrame(void* pin, mspriteframe_t** ppframe)
{
    dspriteframe_t* pinframe = reinterpret_cast<dspriteframe_t*>(pin);
    int width = LittleLong(pinframe->width);
    int height = LittleLong(pinframe->height);
    int size = width * height;
    int alloc_size = sizeof(mspriteframe_t) + size * r_pixbytes;
    loadmodel->sprite_allocations.emplace_back();
    auto& sprite_buf = loadmodel->sprite_allocations.back();
    sprite_buf.resize(alloc_size);
    std::memset(sprite_buf.data(), 0, alloc_size);
    mspriteframe_t* pspriteframe = reinterpret_cast<mspriteframe_t*>(sprite_buf.data());
    *ppframe = pspriteframe;
    pspriteframe->width = width;
    pspriteframe->height = height;
    int origin[2];
    origin[0] = LittleLong(pinframe->origin[0]);
    origin[1] = LittleLong(pinframe->origin[1]);
    pspriteframe->up = static_cast<float>(origin[1]);
    pspriteframe->down = static_cast<float>(origin[1] - height);
    pspriteframe->left = static_cast<float>(origin[0]);
    pspriteframe->right = static_cast<float>(width + origin[0]);
    if (r_pixbytes == 1) {
        std::memcpy(&pspriteframe->pixels[0], reinterpret_cast<byte*>(pinframe + 1), size);
    } else if (r_pixbytes == 2) {
        byte* ppixin = reinterpret_cast<byte*>(pinframe + 1);
        unsigned short* ppixout = reinterpret_cast<unsigned short*>(&pspriteframe->pixels[0]);
        for (int i = 0; i < size; i++) {
            ppixout[i] = d_8to16table[ppixin[i]];
        }
    } else {
        Sys_Error("Mod_LoadSpriteFrame: driver set invalid r_pixbytes: %d\n",
            r_pixbytes);
    }
    return reinterpret_cast<void*>(reinterpret_cast<byte*>(pinframe) + sizeof(dspriteframe_t) + size);
}

void* Mod_LoadSpriteGroup(void* pin, mspriteframe_t** ppframe)
{
    dspritegroup_t* pingroup = reinterpret_cast<dspritegroup_t*>(pin);
    int numframes = LittleLong(pingroup->numframes);
    int group_size = sizeof(mspritegroup_t) + (numframes - 1) * sizeof(mspritegroup_t::frames[0]);
    loadmodel->sprite_allocations.emplace_back();
    auto& group_buf = loadmodel->sprite_allocations.back();
    group_buf.resize(group_size);
    std::memset(group_buf.data(), 0, group_size);
    mspritegroup_t* pspritegroup = reinterpret_cast<mspritegroup_t*>(group_buf.data());
    pspritegroup->numframes = numframes;
    *ppframe = reinterpret_cast<mspriteframe_t*>(pspritegroup);
    dspriteinterval_t* pin_intervals = reinterpret_cast<dspriteinterval_t*>(pingroup + 1);
    int intervals_size = numframes * sizeof(float);
    loadmodel->sprite_allocations.emplace_back();
    auto& intervals_buf = loadmodel->sprite_allocations.back();
    intervals_buf.resize(intervals_size);
    float* poutintervals = reinterpret_cast<float*>(intervals_buf.data());
    pspritegroup->intervals = poutintervals;
    for (int i = 0; i < numframes; i++) {
        *poutintervals = LittleFloat(pin_intervals->interval);
        if (*poutintervals <= 0.0) {
            Sys_Error("Mod_LoadSpriteGroup: interval<=0");
        }
        poutintervals++;
        pin_intervals++;
    }
    void* ptemp = reinterpret_cast<void*>(pin_intervals);
    for (int i = 0; i < numframes; i++) {
        ptemp = Mod_LoadSpriteFrame(ptemp, &pspritegroup->frames[i]);
    }
    return ptemp;
}

void Mod_LoadSpriteModel(model_t* mod, void* buffer)
{
    dsprite_t* pin = reinterpret_cast<dsprite_t*>(buffer);
    int version = LittleLong(pin->version);
    if (version != SPRITE_VERSION) {
        Sys_Error(
            "%s has wrong version number "
            "(%i should be %i)",
            mod->name, version, SPRITE_VERSION);
    }
    int numframes = LittleLong(pin->numframes);
    int size = sizeof(msprite_t) + (numframes - 1) * sizeof(mspriteframedesc_t);
    mod->sprite_allocations.clear();
    mod->sprite_allocations.emplace_back();
    auto& sprite_buf = mod->sprite_allocations.back();
    sprite_buf.resize(size);
    std::memset(sprite_buf.data(), 0, size);
    msprite_t* psprite = reinterpret_cast<msprite_t*>(sprite_buf.data());
    mod->cache.data = psprite;
    psprite->type = LittleLong(pin->type);
    psprite->maxwidth = LittleLong(pin->width);
    psprite->maxheight = LittleLong(pin->height);
    psprite->beamlength = LittleFloat(pin->beamlength);
    mod->synctype = static_cast<synctype_t>(LittleLong(static_cast<int>(pin->synctype)));
    psprite->numframes = numframes;
    mod->mins[0] = mod->mins[1] = static_cast<float>(-psprite->maxwidth) / 2.0f;
    mod->maxs[0] = mod->maxs[1] = static_cast<float>(psprite->maxwidth) / 2.0f;
    mod->mins[2] = static_cast<float>(-psprite->maxheight) / 2.0f;
    mod->maxs[2] = static_cast<float>(psprite->maxheight) / 2.0f;
    //
    // load the frames
    //
    if (numframes < 1) {
        Sys_Error("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);
    }
    mod->numframes = numframes;
    mod->flags = 0;
    dspriteframetype_t* pframetype = reinterpret_cast<dspriteframetype_t*>(pin + 1);
    for (int i = 0; i < numframes; i++) {
        spriteframetype_t frametype = static_cast<spriteframetype_t>(LittleLong(static_cast<int>(pframetype->type)));
        psprite->frames[i].type = frametype;
        if (frametype == spriteframetype_t::SPR_SINGLE) {
            pframetype = reinterpret_cast<dspriteframetype_t*>(Mod_LoadSpriteFrame(
                pframetype + 1, &psprite->frames[i].frameptr));
        } else {
            pframetype = reinterpret_cast<dspriteframetype_t*>(Mod_LoadSpriteGroup(
                pframetype + 1, &psprite->frames[i].frameptr));
        }
    }
    mod->type = mod_sprite;
}
void Mod_Print(void)
{
    Con_Printf("Cached models:\n");
    for (int i = 0; i < mod_numknown; ++i) {
        model_t* mod = &mod_known[i];
        Con_Printf("%8p : %s", mod->cache.data, mod->name);
        if (mod->needload & NL_UNREFERENCED) {
            Con_Printf(" (!R)");
        }
        if (mod->needload & NL_NEEDS_LOADED) {
            Con_Printf(" (!P)");
        }
        Con_Printf("\n");
    }
}

} // namespace Model
