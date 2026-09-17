// sw_main.cpp -- Software Renderer Pipeline Orchestration
#include "render/software/sw_main.hpp"
#include "render/software/sw_frame.hpp"
#include "render/software/sw_efrag.hpp"
#include "render/software/sw_sky.hpp"
#include "render/software/sw_light.hpp"
#include "render/software/sw_surf.hpp"
#include "render/software/sw_drawface.hpp"
#include "render/software/sw_edge.hpp"
#include "render/software/sw_bsp.hpp"
#include "render/software/sw_sprite.hpp"
#include "render/software/sw_alias.hpp"
#include "render/software/sw_part.hpp"
#include "render/software/sw_warp.hpp"
#include "render/software/sw_raster.hpp"
#include "render/software/sw_vid.hpp"
#include "client/client_types.hpp"
#include "client/view.hpp"
#include "world/model.hpp"
#include "core/cmd.hpp"
#include "core/cvar.hpp"
#include "core/memory.hpp"
#include "platform/system.hpp"
#include "audio/audio_main.hpp"
#include "ui/console.hpp"

using namespace Common;
using namespace Console;
using namespace Client;
using namespace Model;
using namespace Audio;
using namespace Vid;
using namespace Math;

namespace Render {

// Core global rendering state
refdef_t r_refdef{};
Vector3 r_origin{}, vpn{}, vright{}, vup{};
Vector3 base_vpn{}, base_vright{}, base_vup{};
Vector3 r_worldmodelorg{};
float xcenter = 0.0f, ycenter = 0.0f;
float xscale = 0.0f, yscale = 0.0f;
float xscaleinv = 0.0f, yscaleinv = 0.0f;
float xscaleshrink = 0.0f, yscaleshrink = 0.0f;
int screenwidth = 0;
float pixelAspect = 0.0f;
int r_framecount = 1;
int r_visframecount = 0;
int r_drawnpolycount = 0, r_polycount = 0, r_wholepolycount = 0;
int c_faceclip = 0;
texture_s* r_notexture_mip = nullptr;

qboolean r_drawpolys = false;
qboolean r_drawculledpolys = false;
qboolean r_worldpolysbacktofront = false;
qboolean r_recursiveaffinetriangles = true;
float r_aliasuvscale = 1.0f;
qboolean r_dowarp = false;
bool r_dowarpold = false;
bool r_cache_thrash = false;
int d_con_indirect = 0;

byte* r_warpbuffer = nullptr;
byte* r_stack_start = nullptr;

mleaf_t* r_viewleaf = nullptr;
mleaf_t* r_oldviewleaf = nullptr;

int r_cnumsurfs = 0;
bool r_surfsonstack = false;
int r_maxedgesseen = 0, r_maxsurfsseen = 0, r_numallocatededges = 0;
edge_t* auxedges = nullptr;
int r_outofsurfaces = 0, r_outofedges = 0;

cvar_t r_clearcolor = { "r_clearcolor", "2" };
cvar_t r_drawviewmodel = { "r_drawviewmodel", "1" };
cvar_t r_drawflat = { "r_drawflat", "0" };
cvar_t r_aliastransbase = { "r_aliastransbase", "200" };
cvar_t r_aliastransadj = { "r_aliastransadj", "100" };
cvar_t r_draworder = { "r_draworder", "0", false };
cvar_t r_speeds = { "r_speeds", "0", false };
cvar_t r_timegraph = { "r_timegraph", "0", false };
cvar_t r_graphheight = { "r_graphheight", "15", false };
cvar_t r_ambient = { "r_ambient", "0", false };
cvar_t r_waterwarp = { "r_waterwarp", "1", false };
cvar_t r_fullbright = { "r_fullbright", "0", false };
cvar_t r_drawentities = { "r_drawentities", "1", false };
cvar_t r_aliasstats = { "r_aliasstats", "0", false };
cvar_t r_dspeeds = { "r_dspeeds", "0", false };
cvar_t r_reportsurfout = { "r_reportsurfout", "0", false };
cvar_t r_maxsurfs = { "r_maxsurfs", "0", false };
cvar_t r_numsurfs = { "r_numsurfs", "0", false };
cvar_t r_reportedgeout = { "r_reportedgeout", "0", false };
cvar_t r_maxedges = { "r_maxedges", "0", false };
cvar_t r_numedges = { "r_numedges", "0", false };

void R_InitTextures()
{
    r_notexture_mip = static_cast<texture_t*>(Hunk_Alloc(
        sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2, "notexture"));
    r_notexture_mip->width = r_notexture_mip->height = 16;
    r_notexture_mip->offsets[0] = sizeof(texture_t);
    r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
    r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
    r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;
    for (int m = 0; m < 4; m++) {
        byte* dest = reinterpret_cast<byte*>(r_notexture_mip) + r_notexture_mip->offsets[m];
        for (int y = 0; y < (16 >> m); y++) {
            for (int x = 0; x < (16 >> m); x++) {
                if ((y < (8 >> m)) ^ (x < (8 >> m))) {
                    *dest++ = 0;
                } else {
                    *dest++ = 0xff;
                }
            }
        }
    }
}

void R_Init()
{
    int dummy;
    r_stack_start = reinterpret_cast<byte*>(&dummy);
    R_InitTurb();
    R_InitVertexNormals();
    Cmd::AddCommand("timerefresh", R_TimeRefresh_f);
    Cmd::AddCommand("pointfile", R_ReadPointFile_f);
    Cvar::Register(&r_draworder);
    Cvar::Register(&r_speeds);
    Cvar::Register(&r_timegraph);
    Cvar::Register(&r_graphheight);
    Cvar::Register(&r_drawflat);
    Cvar::Register(&r_ambient);
    Cvar::Register(&r_clearcolor);
    Cvar::Register(&r_waterwarp);
    Cvar::Register(&r_fullbright);
    Cvar::Register(&r_drawentities);
    Cvar::Register(&r_drawviewmodel);
    Cvar::Register(&r_aliasstats);
    Cvar::Register(&r_dspeeds);
    Cvar::Register(&r_reportsurfout);
    Cvar::Register(&r_maxsurfs);
    Cvar::Register(&r_numsurfs);
    Cvar::Register(&r_reportedgeout);
    Cvar::Register(&r_maxedges);
    Cvar::Register(&r_numedges);
    Cvar::Register(&r_aliastransbase);
    Cvar::Register(&r_aliastransadj);
    Cvar::SetValue("r_maxedges", static_cast<float>(NUMSTACKEDGES));
    Cvar::SetValue("r_maxsurfs", static_cast<float>(NUMSTACKSURFACES));
    view_clipplanes[0].leftedge = true;
    view_clipplanes[1].rightedge = true;
    view_clipplanes[1].leftedge = view_clipplanes[2].leftedge = view_clipplanes[3].leftedge = false;
    view_clipplanes[0].rightedge = view_clipplanes[2].rightedge = view_clipplanes[3].rightedge = false;
    r_refdef.xOrigin = XCENTERING;
    r_refdef.yOrigin = YCENTERING;
    R_InitParticles();
    D_Init();
}

void R_NewMap()
{
    for (int i = 0; i < cl.worldmodel->numleafs; i++) {
        cl.worldmodel->leafs[i].efrags = nullptr;
    }
    r_viewleaf = nullptr;
    R_ClearParticles();
    r_cnumsurfs = static_cast<int>(r_maxsurfs.value);
    if (r_cnumsurfs <= MINSURFACES) {
        r_cnumsurfs = MINSURFACES;
    }
    if (r_cnumsurfs > NUMSTACKSURFACES) {
        surfaces = static_cast<surf_t*>(Hunk_Alloc(r_cnumsurfs * sizeof(surf_t), "surfaces"));
        surface_p = surfaces;
        surf_max = &surfaces[r_cnumsurfs];
        r_surfsonstack = false;
        surfaces--;
    } else {
        r_surfsonstack = true;
    }
    r_maxedgesseen = 0;
    r_maxsurfsseen = 0;
    r_numallocatededges = static_cast<int>(r_maxedges.value);
    if (r_numallocatededges < MINEDGES) {
        r_numallocatededges = MINEDGES;
    }
    if (r_numallocatededges <= NUMSTACKEDGES) {
        auxedges = nullptr;
    } else {
        auxedges = static_cast<edge_t*>(Hunk_Alloc(r_numallocatededges * sizeof(edge_t), "edges"));
    }
    r_dowarpold = false;
    r_viewchanged = false;
}

void R_MarkLeaves()
{
    if (r_oldviewleaf == r_viewleaf) {
        return;
    }
    r_visframecount++;
    r_oldviewleaf = r_viewleaf;
    byte* vis = Mod_LeafPVS(r_viewleaf, cl.worldmodel);
    for (int i = 0; i < cl.worldmodel->numleafs; i++) {
        if (vis[i >> 3] & (1 << (i & 7))) {
            mnode_t* node = reinterpret_cast<mnode_t*>(&cl.worldmodel->leafs[i + 1]);
            do {
                if (node->visframe == r_visframecount) {
                    break;
                }
                node->visframe = r_visframecount;
                node = node->parent;
            } while (node);
        }
    }
}

void R_DrawEntitiesOnList()
{
    float lightvec[3] = { -1, 0, 0 };
    if (!r_drawentities.value) {
        return;
    }
    for (int i = 0; i < cl_numvisedicts; i++) {
        currententity = cl_visedicts[i];
        if (currententity == &cl_entities[cl.viewentity]) {
            continue;
        }
        switch (currententity->model->type) {
        case mod_sprite:
            r_entorigin = currententity->origin;
            modelorg = r_origin - r_entorigin;
            R_DrawSprite();
            break;
        case mod_alias:
            r_entorigin = currententity->origin;
            modelorg = r_origin - r_entorigin;
            if (R_AliasCheckBBox()) {
                int j = R_LightPoint(currententity->origin);
                alight_t lighting{};
                lighting.ambientlight = j;
                lighting.shadelight = j;
                lighting.plightvec = lightvec;
                for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
                    if (cl_dlights[lnum].die >= cl.time) {
                        Vector3 dist = currententity->origin - cl_dlights[lnum].origin;
                        float add = cl_dlights[lnum].radius - dist.length();
                        if (add > 0) {
                            lighting.ambientlight += static_cast<int>(add);
                        }
                    }
                }
                if (lighting.ambientlight > 128) {
                    lighting.ambientlight = 128;
                }
                if (lighting.ambientlight + lighting.shadelight > 192) {
                    lighting.shadelight = 192 - lighting.ambientlight;
                }
                R_AliasDrawModel(&lighting);
            }
            break;
        default:
            break;
        }
    }
}

void R_DrawViewModel()
{
    float lightvec[3] = { -1, 0, 0 };
    if (!r_drawviewmodel.value || r_fov_greater_than_90) {
        return;
    }
    if (cl.items & IT_INVISIBILITY) {
        return;
    }
    if (cl.stats[STAT_HEALTH] <= 0) {
        return;
    }
    currententity = &cl.viewent;
    if (!currententity->model) {
        return;
    }
    r_entorigin = currententity->origin;
    Vector3 viewlightvec = -vup;
    (void)viewlightvec;
    int j = R_LightPoint(currententity->origin);
    if (j < 24) {
        j = 24;
    }
    alight_t r_viewlighting{};
    r_viewlighting.ambientlight = j;
    r_viewlighting.shadelight = j;
    for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
        dlight_t* dl = &cl_dlights[lnum];
        if (!dl->radius || dl->die < cl.time) {
            continue;
        }
        Vector3 dist = currententity->origin - dl->origin;
        float add = dl->radius - dist.length();
        if (add > 0) {
            r_viewlighting.ambientlight += static_cast<int>(add);
        }
    }
    if (r_viewlighting.ambientlight > 128) {
        r_viewlighting.ambientlight = 128;
    }
    if (r_viewlighting.ambientlight + r_viewlighting.shadelight > 192) {
        r_viewlighting.shadelight = 192 - r_viewlighting.ambientlight;
    }
    r_viewlighting.plightvec = lightvec;
    R_AliasDrawModel(&r_viewlighting);
}

int R_BmodelCheckBBox(model_t* clmodel, float* minmaxs)
{
    int clipflags = 0;
    if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2]) {
        for (int i = 0; i < 4; i++) {
            double d = currententity->origin.dot(view_clipplanes[i].normal) - view_clipplanes[i].dist;
            if (d <= -clmodel->radius) {
                return BMODEL_FULLY_CLIPPED;
            }
            if (d <= clmodel->radius) {
                clipflags |= (1 << i);
            }
        }
    } else {
        for (int i = 0; i < 4; i++) {
            int* pindex = pfrustum_indexes[i];
            Vector3 rejectpt(minmaxs[pindex[0]], minmaxs[pindex[1]], minmaxs[pindex[2]]);
            double d = rejectpt.dot(view_clipplanes[i].normal) - view_clipplanes[i].dist;
            if (d <= 0) {
                return BMODEL_FULLY_CLIPPED;
            }
            Vector3 acceptpt(minmaxs[pindex[3]], minmaxs[pindex[4]], minmaxs[pindex[5]]);
            d = acceptpt.dot(view_clipplanes[i].normal) - view_clipplanes[i].dist;
            if (d <= 0) {
                clipflags |= (1 << i);
            }
        }
    }
    return clipflags;
}

void R_DrawBEntitiesOnList()
{
    float minmaxs[6];
    if (!r_drawentities.value) {
        return;
    }
    Vector3 oldorigin = modelorg;
    insubmodel = true;
    r_dlightframecount = r_framecount;
    for (int i = 0; i < cl_numvisedicts; i++) {
        currententity = cl_visedicts[i];
        if (currententity->model->type == mod_brush) {
            model_t* clmodel = currententity->model;
            minmaxs[0] = currententity->origin.x + clmodel->mins[0];
            minmaxs[1] = currententity->origin.y + clmodel->mins[1];
            minmaxs[2] = currententity->origin.z + clmodel->mins[2];
            minmaxs[3] = currententity->origin.x + clmodel->maxs[0];
            minmaxs[4] = currententity->origin.y + clmodel->maxs[1];
            minmaxs[5] = currententity->origin.z + clmodel->maxs[2];
            int clipflags = R_BmodelCheckBBox(clmodel, minmaxs);
            if (clipflags != BMODEL_FULLY_CLIPPED) {
                r_entorigin = currententity->origin;
                modelorg = r_origin - r_entorigin;
                r_worldmodelorg = modelorg;
                r_pcurrentvertbase = clmodel->vertexes;
                R_RotateBmodel();
                if (clmodel->firstmodelsurface != 0) {
                    for (int k = 0; k < MAX_DLIGHTS; k++) {
                        if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius)) {
                            continue;
                        }
                        R_MarkLights(&cl_dlights[k], 1 << k,
                            clmodel->nodes + clmodel->hulls[0].firstclipnode);
                    }
                }
                if (r_drawpolys | r_drawculledpolys) {
                    R_ZDrawSubmodelPolys(clmodel);
                } else {
                    r_pefragtopnode = nullptr;
                    r_emins = Vector3(minmaxs[0], minmaxs[1], minmaxs[2]);
                    r_emaxs = Vector3(minmaxs[3], minmaxs[4], minmaxs[5]);
                    R_SplitEntityOnNode2(cl.worldmodel->nodes);
                    if (r_pefragtopnode) {
                        currententity->topnode = r_pefragtopnode;
                        if (r_pefragtopnode->contents >= 0) {
                            r_clipflags = clipflags;
                            R_DrawSolidClippedSubmodelPolygons(clmodel);
                        } else {
                            R_DrawSubmodelPolygons(clmodel, clipflags);
                        }
                        currententity->topnode = nullptr;
                    }
                }
                vpn = base_vpn;
                vup = base_vup;
                vright = base_vright;
                modelorg = base_modelorg;
                modelorg = oldorigin;
                R_TransformFrustum();
            }
        }
    }
    insubmodel = false;
}

void R_EdgeDrawing()
{
    edge_t ledges[NUMSTACKEDGES + ((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];
    surf_t lsurfs[NUMSTACKSURFACES + ((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];
    if (auxedges) {
        r_edges = auxedges;
    } else {
        r_edges = reinterpret_cast<edge_t*>((reinterpret_cast<size_t>(&ledges[0]) + CACHE_SIZE - 1) & ~(static_cast<size_t>(CACHE_SIZE - 1)));
    }
    if (r_surfsonstack) {
        surfaces = reinterpret_cast<surf_t*>((reinterpret_cast<size_t>(&lsurfs[0]) + CACHE_SIZE - 1) & ~(static_cast<size_t>(CACHE_SIZE - 1)));
        surf_max = &surfaces[r_cnumsurfs];
        surfaces--;
    }
    R_BeginEdgeFrame();
    if (r_dspeeds.value) {
        rw_time1 = static_cast<float>(Sys_FloatTime());
    }
    R_RenderWorld();
    if (r_drawculledpolys) {
        R_ScanEdges();
    }
    D_TurnZOn();
    if (r_dspeeds.value) {
        rw_time2 = static_cast<float>(Sys_FloatTime());
        db_time1 = rw_time2;
    }
    R_DrawBEntitiesOnList();
    if (r_dspeeds.value) {
        db_time2 = static_cast<float>(Sys_FloatTime());
        se_time1 = db_time2;
    }
    if (!(r_drawpolys | r_drawculledpolys)) {
        R_ScanEdges();
    }
}

void R_RenderView_()
{
    eastl::array<byte, WARP_WIDTH * WARP_HEIGHT> warpbuffer{};
    r_warpbuffer = warpbuffer.data();
    if (r_timegraph.value || r_speeds.value || r_dspeeds.value) {
        r_time1 = static_cast<float>(Sys_FloatTime());
    }
    R_SetupFrame();
    R_MarkLeaves();
    if (!cl_entities[0].model || !cl.worldmodel) {
        Sys_Error("R_RenderView: nullptr worldmodel");
    }
    R_EdgeDrawing();
    if (r_dspeeds.value) {
        se_time2 = static_cast<float>(Sys_FloatTime());
        de_time1 = se_time2;
    }
    R_DrawEntitiesOnList();
    if (r_dspeeds.value) {
        de_time2 = static_cast<float>(Sys_FloatTime());
        dv_time1 = de_time2;
    }
    R_DrawViewModel();
    if (r_dspeeds.value) {
        dv_time2 = static_cast<float>(Sys_FloatTime());
        dp_time1 = static_cast<float>(Sys_FloatTime());
    }
    R_DrawParticles();
    if (r_dspeeds.value) {
        dp_time2 = static_cast<float>(Sys_FloatTime());
    }
    if (r_dowarp) {
        D_WarpScreen();
    }
    View::V_SetContentsColor(r_viewleaf->contents);
    if (r_timegraph.value) {
        R_TimeGraph();
    }
    if (r_aliasstats.value) {
        R_PrintAliasStats();
    }
    if (r_speeds.value) {
        R_PrintTimes();
    }
    if (r_dspeeds.value) {
        R_PrintDSpeeds();
    }
    if (r_reportsurfout.value && r_outofsurfaces) {
        Con_Printf("Short %d surfaces\n", r_outofsurfaces);
    }
    if (r_reportedgeout.value && r_outofedges) {
        Con_Printf("Short roughly %d edges\n", r_outofedges * 2 / 3);
    }
}

void R_RenderView()
{
    int dummy;
    int delta = static_cast<int>(reinterpret_cast<byte*>(&dummy) - r_stack_start);
    if (delta < -10000 || delta > 10000) {
        Sys_Error("R_RenderView: called without enough stack");
    }
    if (reinterpret_cast<size_t>(&dummy) & 3) {
        Sys_Error("Stack is missaligned");
    }
    if (reinterpret_cast<size_t>(&r_warpbuffer) & 3) {
        Sys_Error("Globals are missaligned");
    }
    R_RenderView_();
}

} // namespace Render
