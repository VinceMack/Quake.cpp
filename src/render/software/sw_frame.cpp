// sw_frame.cpp -- Software Renderer Frame Setup, Viewport & Timing
#include "render/software/sw_frame.hpp"
#include "render/software/sw_sky.hpp"
#include "render/software/sw_light.hpp"
#include "render/software/sw_surf.hpp"
#include "render/software/sw_raster.hpp"
#include "render/software/sw_vid.hpp"
#include "ui/screen.hpp"
#include "client/view.hpp"
#include "ui/hud.hpp"
#include "server/server.hpp"
#include "world/model.hpp"
#include "client/client_types.hpp"
#include "core/print.hpp"
#include "platform/system.hpp"
#include "core/cvar.hpp"
#include <cmath>

namespace Render {

// External render view entry point forward declaration
void R_RenderView();

float dp_time1 = 0.0f, dp_time2 = 0.0f, db_time1 = 0.0f, db_time2 = 0.0f, rw_time1 = 0.0f, rw_time2 = 0.0f;
float se_time1 = 0.0f, se_time2 = 0.0f, de_time1 = 0.0f, de_time2 = 0.0f, dv_time1 = 0.0f, dv_time2 = 0.0f;
float r_time1 = 0.0f;

std::array<int*, 4> pfrustum_indexes { };
std::array<int, 4 * 6> r_frustum_indexes { };
std::array<clipplane_t, 4> view_clipplanes { };
std::array<mplane_t, 4> screenedge { };

float screenAspect = 0.0f, verticalFieldOfView = 0.0f, xOrigin = 0.0f, yOrigin = 0.0f;
float aliasxscale = 0.0f, aliasyscale = 0.0f, aliasxcenter = 0.0f, aliasycenter = 0.0f;
float r_aliastransition = 0.0f, r_resfudge = 0.0f;
bool r_fov_greater_than_90 = false;
bool r_viewchanged = false;

void TransformVector(const Vector3& in, Vector3& out)
{
    out.x = in.dot(vright);
    out.y = in.dot(vup);
    out.z = in.dot(vpn);
}

void R_TransformFrustum()
{
    for (int i = 0; i < 4; i++) {
        Vector3 v(screenedge[i].normal.z, -screenedge[i].normal.x, screenedge[i].normal.y);
        Vector3 v2 = vright * v.y + vup * v.z + vpn * v.x;
        view_clipplanes[i].normal = v2;
        view_clipplanes[i].dist = modelorg.dot(v2);
    }
}

void R_SetUpFrustumIndexes()
{
    int* pindex = r_frustum_indexes.data();
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            if (view_clipplanes[i].normal[j] < 0) {
                pindex[j] = j;
                pindex[j + 3] = j + 3;
            } else {
                pindex[j] = j + 3;
                pindex[j + 3] = j;
            }
        }
        pfrustum_indexes[i] = pindex;
        pindex += 6;
    }
}

void R_CheckVariables()
{
    static float oldbright = 0.0f;
    if (r_fullbright.value != oldbright) {
        oldbright = r_fullbright.value;
        D_FlushCaches();
    }
}

void R_TimeRefresh_f()
{
    vrect_t vr { };
    int startangle = static_cast<int>(r_refdef.viewangles[1]);
    float start = static_cast<float>(Common::Sys_FloatTime());
    for (int i = 0; i < 128; i++) {
        r_refdef.viewangles[1] = static_cast<float>(i / 128.0 * 360.0);
        R_RenderView();
        vr.x = r_refdef.vrect.x;
        vr.y = r_refdef.vrect.y;
        vr.width = r_refdef.vrect.width;
        vr.height = r_refdef.vrect.height;
        vr.pnext = nullptr;
        Vid::VID_Update(&vr);
    }
    float stop = static_cast<float>(Common::Sys_FloatTime());
    float time = stop - start;
    Console::Con_Printf("%f seconds (%f fps)\n", time, 128.0f / time);
    r_refdef.viewangles[1] = static_cast<float>(startangle);
}

void R_LineGraph(int x, int y, int h)
{
    x += r_refdef.vrect.x;
    y += r_refdef.vrect.y;
    byte* dest = Vid::vid.buffer + Vid::vid.rowbytes * y + x;
    int s = static_cast<int>(r_graphheight.value);
    if (h > s) {
        h = s;
    }
    for (int i = 0; i < h; i++, dest -= Vid::vid.rowbytes * 2) {
        dest[0] = 0xff;
        *(dest - Vid::vid.rowbytes) = 0x30;
    }
    for (int i = h; i < s; i++, dest -= Vid::vid.rowbytes * 2) {
        dest[0] = 0x30;
        *(dest - Vid::vid.rowbytes) = 0x30;
    }
}

constexpr int MAX_TIMINGS = 100;

void R_TimeGraph()
{
    static int timex = 0;
    static std::array<byte, MAX_TIMINGS> r_timings { };
    float r_time2 = static_cast<float>(Common::Sys_FloatTime());
    int a = static_cast<int>((r_time2 - r_time1) / 0.01f);
    r_timings[timex] = static_cast<byte>(a);
    a = timex;
    int x = (r_refdef.vrect.width <= MAX_TIMINGS) ? (r_refdef.vrect.width - 1)
                                                  : (r_refdef.vrect.width - (r_refdef.vrect.width - MAX_TIMINGS) / 2);
    do {
        R_LineGraph(x, r_refdef.vrect.height - 2, r_timings[a]);
        if (x == 0) {
            break;
        }
        x--;
        a--;
        if (a == -1) {
            a = MAX_TIMINGS - 1;
        }
    } while (a != timex);
    timex = (timex + 1) % MAX_TIMINGS;
}

void R_PrintAliasStats()
{
    Console::Con_Printf("%3i polygon model drawn\n", r_amodels_drawn);
}

void R_PrintTimes()
{
    float r_time2 = static_cast<float>(Common::Sys_FloatTime());
    float ms = static_cast<float>(1000.0f * (r_time2 - r_time1));
    Console::Con_Printf("%5.1f ms %3i/%3i/%3i poly %3i surf\n", ms, c_faceclip, r_polycount, r_drawnpolycount, c_surf);
    c_surf = 0;
}

void R_PrintDSpeeds()
{
    float r_time2 = static_cast<float>(Common::Sys_FloatTime());
    float dp_time = static_cast<float>((dp_time2 - dp_time1) * 1000.0f);
    float rw_time = (rw_time2 - rw_time1) * 1000.0f;
    float db_time = (db_time2 - db_time1) * 1000.0f;
    float se_time = (se_time2 - se_time1) * 1000.0f;
    float de_time = (de_time2 - de_time1) * 1000.0f;
    float dv_time = (dv_time2 - dv_time1) * 1000.0f;
    float ms = (r_time2 - r_time1) * 1000.0f;
    Console::Con_Printf("%3i %4.1fp %3iw %4.1fb %3is %4.1fe %4.1fv\n", static_cast<int>(ms), dp_time,
        static_cast<int>(rw_time), db_time, static_cast<int>(se_time), de_time, dv_time);
}

void R_SetVrect(vrect_t* pvrectin, vrect_t* pvrect, int lineadj)
{
    float size = Screen::GetScreenSystem().GetViewsize().value > 100.0f ? 100.0f
                                                                        : Screen::GetScreenSystem().GetViewsize().value;
    if (Client::cl.intermission) {
        size = 100.0f;
        lineadj = 0;
    }
    size /= 100.0f;
    int h = pvrectin->height - lineadj;
    pvrect->width = static_cast<int>(pvrectin->width * size);
    if (pvrect->width < 96) {
        size = 96.0f / static_cast<float>(pvrectin->width);
        pvrect->width = 96;
    }
    pvrect->width &= ~7;
    pvrect->height = static_cast<int>(pvrectin->height * size);
    if (pvrect->height > pvrectin->height - lineadj) {
        pvrect->height = pvrectin->height - lineadj;
    }
    pvrect->height &= ~1;
    pvrect->x = (pvrectin->width - pvrect->width) / 2;
    pvrect->y = (h - pvrect->height) / 2;
    if (View::lcd_x.value) {
        pvrect->y >>= 1;
        pvrect->height >>= 1;
    }
}

void R_ViewChanged(vrect_t* pvrect, int lineadj, float aspect)
{
    r_viewchanged = true;
    R_SetVrect(pvrect, &r_refdef.vrect, lineadj);
    r_refdef.horizontalFieldOfView = static_cast<float>(2.0 * std::tan(r_refdef.fov_x / 360.0 * M_PI));
    r_refdef.fvrectx = static_cast<float>(r_refdef.vrect.x);
    r_refdef.fvrectx_adj = static_cast<float>(r_refdef.vrect.x) - 0.5f;
    r_refdef.vrect_x_adj_shift20 = ((int64_t)r_refdef.vrect.x << 20) + (1 << 19) - 1;
    r_refdef.fvrecty = static_cast<float>(r_refdef.vrect.y);
    r_refdef.fvrecty_adj = static_cast<float>(r_refdef.vrect.y) - 0.5f;
    r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
    r_refdef.vrectright_adj_shift20 = ((int64_t)r_refdef.vrectright << 20) + (1 << 19) - 1;
    r_refdef.fvrectright = static_cast<float>(r_refdef.vrectright);
    r_refdef.fvrectright_adj = static_cast<float>(r_refdef.vrectright) - 0.5f;
    r_refdef.vrectrightedge = static_cast<float>(r_refdef.vrectright) - 0.99f;
    r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
    r_refdef.fvrectbottom = static_cast<float>(r_refdef.vrectbottom);
    r_refdef.fvrectbottom_adj = static_cast<float>(r_refdef.vrectbottom) - 0.5f;
    r_refdef.aliasvrect.x = static_cast<int>(r_refdef.vrect.x * r_aliasuvscale);
    r_refdef.aliasvrect.y = static_cast<int>(r_refdef.vrect.y * r_aliasuvscale);
    r_refdef.aliasvrect.width = static_cast<int>(r_refdef.vrect.width * r_aliasuvscale);
    r_refdef.aliasvrect.height = static_cast<int>(r_refdef.vrect.height * r_aliasuvscale);
    r_refdef.aliasvrectright = r_refdef.aliasvrect.x + r_refdef.aliasvrect.width;
    r_refdef.aliasvrectbottom = r_refdef.aliasvrect.y + r_refdef.aliasvrect.height;
    pixelAspect = aspect;
    xOrigin = r_refdef.xOrigin;
    yOrigin = r_refdef.yOrigin;
    screenAspect = r_refdef.vrect.width * pixelAspect / r_refdef.vrect.height;
    verticalFieldOfView = r_refdef.horizontalFieldOfView / screenAspect;
    xcenter = (static_cast<float>(r_refdef.vrect.width) * static_cast<float>(XCENTERING))
        + static_cast<float>(r_refdef.vrect.x) - 0.5f;
    aliasxcenter = xcenter * r_aliasuvscale;
    ycenter = (static_cast<float>(r_refdef.vrect.height) * static_cast<float>(YCENTERING))
        + static_cast<float>(r_refdef.vrect.y) - 0.5f;
    aliasycenter = ycenter * r_aliasuvscale;
    xscale = static_cast<float>(r_refdef.vrect.width) / r_refdef.horizontalFieldOfView;
    aliasxscale = xscale * r_aliasuvscale;
    xscaleinv = 1.0f / xscale;
    yscale = xscale * pixelAspect;
    aliasyscale = yscale * r_aliasuvscale;
    yscaleinv = 1.0f / yscale;
    xscaleshrink = static_cast<float>(r_refdef.vrect.width - 6) / r_refdef.horizontalFieldOfView;
    yscaleshrink = xscaleshrink * pixelAspect;
    screenedge[0].normal[0] = static_cast<float>(-1.0 / (xOrigin * r_refdef.horizontalFieldOfView));
    screenedge[0].normal[1] = 0.0f;
    screenedge[0].normal[2] = 1.0f;
    screenedge[0].type = PLANE_ANYZ;
    screenedge[1].normal[0] = static_cast<float>(1.0 / ((1.0 - xOrigin) * r_refdef.horizontalFieldOfView));
    screenedge[1].normal[1] = 0.0f;
    screenedge[1].normal[2] = 1.0f;
    screenedge[1].type = PLANE_ANYZ;
    screenedge[2].normal[0] = 0.0f;
    screenedge[2].normal[1] = static_cast<float>(-1.0 / (yOrigin * verticalFieldOfView));
    screenedge[2].normal[2] = 1.0f;
    screenedge[2].type = PLANE_ANYZ;
    screenedge[3].normal[0] = 0.0f;
    screenedge[3].normal[1] = static_cast<float>(1.0 / ((1.0 - yOrigin) * verticalFieldOfView));
    screenedge[3].normal[2] = 1.0f;
    screenedge[3].type = PLANE_ANYZ;
    for (int i = 0; i < 4; i++) {
        Math::VectorNormalize(screenedge[i].normal);
    }
    float res_scale = static_cast<float>(
        std::sqrt(static_cast<double>(r_refdef.vrect.width * r_refdef.vrect.height) / (320.0 * 152.0))
        * (2.0 / r_refdef.horizontalFieldOfView));
    r_aliastransition = r_aliastransbase.value * res_scale;
    r_resfudge = r_aliastransadj.value * res_scale;
    r_fov_greater_than_90 = (Screen::GetScreenSystem().GetFov().value > 90.0f);
    D_ViewChanged();
}

void R_SetupFrame()
{
    vrect_t vrect { };
    float w, h;
    if (Client::cl.maxclients > 1) {
        Cvar::Set("r_draworder", "0");
        Cvar::Set("r_fullbright", "0");
        Cvar::Set("r_ambient", "0");
        Cvar::Set("r_drawflat", "0");
    }
    if (r_numsurfs.value) {
        if ((surface_p - surfaces) > r_maxsurfsseen) {
            r_maxsurfsseen = static_cast<int>(surface_p - surfaces);
        }
        Console::Con_Printf("Used %d of %d surfs; %d max\n", surface_p - surfaces, surf_max - surfaces, r_maxsurfsseen);
    }
    if (r_numedges.value) {
        int edgecount = static_cast<int>(edge_p - r_edges);
        if (edgecount > r_maxedgesseen) {
            r_maxedgesseen = edgecount;
        }
        Console::Con_Printf("Used %d of %d edges; %d max\n", edgecount, r_numallocatededges, r_maxedgesseen);
    }
    r_refdef.ambientlight = static_cast<int>(r_ambient.value);
    if (r_refdef.ambientlight < 0) {
        r_refdef.ambientlight = 0;
    }
    if (!Server::sv.active) {
        r_draworder.value = 0;
    }
    R_CheckVariables();
    R_AnimateLight();
    r_framecount++;
    numbtofpolys = 0;
    modelorg = r_refdef.vieworg;
    r_origin = r_refdef.vieworg;
    Math::AngleVectors(r_refdef.viewangles, vpn, vright, vup);
    r_oldviewleaf = r_viewleaf;
    r_viewleaf = Model::Mod_PointInLeaf(r_origin, Client::cl.worldmodel);
    r_dowarpold = r_dowarp;
    r_dowarp = r_waterwarp.value && (r_viewleaf->contents <= CONTENTS_WATER);
    if ((r_dowarp != r_dowarpold) || r_viewchanged || View::lcd_x.value) {
        if (r_dowarp) {
            if ((static_cast<int>(Vid::vid.width) <= Vid::vid.maxwarpwidth)
                && (static_cast<int>(Vid::vid.height) <= Vid::vid.maxwarpheight)) {
                vrect.x = 0;
                vrect.y = 0;
                vrect.width = Vid::vid.width;
                vrect.height = Vid::vid.height;
                R_ViewChanged(&vrect, sb_lines, Vid::vid.aspect);
            } else {
                w = static_cast<float>(Vid::vid.width);
                h = static_cast<float>(Vid::vid.height);
                if (w > Vid::vid.maxwarpwidth) {
                    h *= static_cast<float>(Vid::vid.maxwarpwidth) / w;
                    w = static_cast<float>(Vid::vid.maxwarpwidth);
                }
                if (h > Vid::vid.maxwarpheight) {
                    h = static_cast<float>(Vid::vid.maxwarpheight);
                    w *= static_cast<float>(Vid::vid.maxwarpheight) / h;
                }
                vrect.x = 0;
                vrect.y = 0;
                vrect.width = static_cast<int>(w);
                vrect.height = static_cast<int>(h);
                R_ViewChanged(&vrect,
                    static_cast<int>(static_cast<float>(sb_lines) * (h / static_cast<float>(Vid::vid.height))),
                    Vid::vid.aspect * (h / w)
                        * (static_cast<float>(Vid::vid.width) / static_cast<float>(Vid::vid.height)));
            }
        } else {
            vrect.x = 0;
            vrect.y = 0;
            vrect.width = Vid::vid.width;
            vrect.height = Vid::vid.height;
            R_ViewChanged(&vrect, sb_lines, Vid::vid.aspect);
        }
        r_viewchanged = false;
    }
    R_TransformFrustum();
    base_vpn = vpn;
    base_vright = vright;
    base_vup = vup;
    base_modelorg = modelorg;
    R_SetSkyFrame();
    R_SetUpFrustumIndexes();
    r_cache_thrash = false;
    c_faceclip = 0;
    d_spanpixcount = 0;
    r_polycount = 0;
    r_drawnpolycount = 0;
    r_wholepolycount = 0;
    r_amodels_drawn = 0;
    r_outofsurfaces = 0;
    r_outofedges = 0;
    D_SetupFrame();
}

} // namespace Render
