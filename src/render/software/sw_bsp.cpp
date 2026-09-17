// sw_bsp.cpp -- BSP Traversal, Bmodel Rotation and Polygon Rasterization Setup
#include "render/software/sw_bsp.hpp"
#include "render/software/sw_drawface.hpp"
#include "client/client_types.hpp"
#include "world/bsp_format.hpp"
#include "platform/system.hpp"
#include "quakedef.hpp"

#include <cmath>

namespace Render {

bool insubmodel = false;
entity_t* currententity = nullptr;
Vector3 modelorg { }, base_modelorg { };
Vector3 r_entorigin { };
float entity_rotation[3][3] { };
mvertex_t* r_pcurrentvertbase = nullptr;
int numbtofpolys = 0;
btofpoly_t* pbtofpolys = nullptr;

inline constexpr int MAX_BMODEL_VERTS = 500;
inline constexpr int MAX_BMODEL_EDGES = 1000;

static mvertex_t* pbverts = nullptr;
static bedge_t* pbedges = nullptr;
static int numbverts = 0, numbedges = 0;
static mvertex_t *pfrontenter = nullptr, *pfrontexit = nullptr;
static bool makeclippededge = false;

void R_DrawCulledPolys()
{
    currententity = &Client::cl_entities[0];
    if (r_worldpolysbacktofront) {
        for (surf_t* s = surface_p - 1; s > &surfaces[1]; s--) {
            if (!s->spans) {
                continue;
            }
            if (!(s->flags & SURF_DRAWBACKGROUND)) {
                msurface_t* pface = (msurface_t*)s->data;
                R_RenderPoly(pface, 15);
            }
        }
    } else {
        for (surf_t* s = &surfaces[1]; s < surface_p; s++) {
            if (!s->spans) {
                continue;
            }
            if (!(s->flags & SURF_DRAWBACKGROUND)) {
                msurface_t* pface = (msurface_t*)s->data;
                R_RenderPoly(pface, 15);
            }
        }
    }
}

void R_ZDrawSubmodelPolys(model_t* pmodel)
{
    msurface_t* psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    int numsurfaces = pmodel->nummodelsurfaces;
    for (int i = 0; i < numsurfaces; i++, psurf++) {
        mplane_t* pplane = psurf->plane;
        float dot = DotProduct(modelorg, pplane->normal) - pplane->dist;
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON))
            || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            R_RenderPoly(psurf, 15);
        }
    }
}

void R_EntityRotate(Vector3& vec)
{
    Vector3 tvec = vec;
    vec.x = Vector3(entity_rotation[0][0], entity_rotation[0][1], entity_rotation[0][2]).dot(tvec);
    vec.y = Vector3(entity_rotation[1][0], entity_rotation[1][1], entity_rotation[1][2]).dot(tvec);
    vec.z = Vector3(entity_rotation[2][0], entity_rotation[2][1], entity_rotation[2][2]).dot(tvec);
}

void R_RotateBmodel()
{
    float temp1[3][3], temp2[3][3], temp3[3][3];
    // yaw
    float angle = currententity->angles[YAW];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    float s = std::sin(angle);
    float c = std::cos(angle);
    temp1[0][0] = c;
    temp1[0][1] = s;
    temp1[0][2] = 0;
    temp1[1][0] = -s;
    temp1[1][1] = c;
    temp1[1][2] = 0;
    temp1[2][0] = 0;
    temp1[2][1] = 0;
    temp1[2][2] = 1;
    // pitch
    angle = currententity->angles[PITCH];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = std::sin(angle);
    c = std::cos(angle);
    temp2[0][0] = c;
    temp2[0][1] = 0;
    temp2[0][2] = -s;
    temp2[1][0] = 0;
    temp2[1][1] = 1;
    temp2[1][2] = 0;
    temp2[2][0] = s;
    temp2[2][1] = 0;
    temp2[2][2] = c;
    Math::R_ConcatRotations(temp2, temp1, temp3);
    // roll
    angle = currententity->angles[ROLL];
    angle = static_cast<float>(angle * M_PI * 2 / 360);
    s = std::sin(angle);
    c = std::cos(angle);
    temp1[0][0] = 1;
    temp1[0][1] = 0;
    temp1[0][2] = 0;
    temp1[1][0] = 0;
    temp1[1][1] = c;
    temp1[1][2] = s;
    temp1[2][0] = 0;
    temp1[2][1] = -s;
    temp1[2][2] = c;
    Math::R_ConcatRotations(temp1, temp3, entity_rotation);

    R_EntityRotate(modelorg);
    R_EntityRotate(vpn);
    R_EntityRotate(vright);
    R_EntityRotate(vup);
    R_TransformFrustum();
}

void R_RecursiveClipBPoly(bedge_t* pedges, mnode_t* pnode, msurface_t* psurf)
{
    bedge_t *psideedges[2] { }, *pnextedge, *ptedge;
    mplane_t *splitplane, tplane;
    mvertex_t *pvert, *plastvert, *ptvert;

    makeclippededge = false;
    splitplane = pnode->plane;
    tplane.dist = splitplane->dist - DotProduct(r_entorigin, splitplane->normal);
    tplane.normal[0] = DotProduct(entity_rotation[0], splitplane->normal);
    tplane.normal[1] = DotProduct(entity_rotation[1], splitplane->normal);
    tplane.normal[2] = DotProduct(entity_rotation[2], splitplane->normal);

    for (; pedges; pedges = pnextedge) {
        pnextedge = pedges->pnext;
        plastvert = pedges->v[0];
        float lastdist = DotProduct(plastvert->position, tplane.normal) - tplane.dist;
        int lastside = (lastdist > 0) ? 0 : 1;
        pvert = pedges->v[1];
        float dist = DotProduct(pvert->position, tplane.normal) - tplane.dist;
        int side = (dist > 0) ? 0 : 1;

        if (lastside != side) {
            if (numbverts >= MAX_BMODEL_VERTS) {
                return;
            }
            ptvert = &pbverts[numbverts++];
            float frac = lastdist / (lastdist - dist);
            ptvert->position[0] = plastvert->position[0] + frac * (pvert->position[0] - plastvert->position[0]);
            ptvert->position[1] = plastvert->position[1] + frac * (pvert->position[1] - plastvert->position[1]);
            ptvert->position[2] = plastvert->position[2] + frac * (pvert->position[2] - plastvert->position[2]);

            if (numbedges >= (MAX_BMODEL_EDGES - 2)) {
                return;
            }
            ptedge = &pbedges[numbedges++];
            ptedge->pnext = psideedges[lastside];
            psideedges[lastside] = ptedge;
            ptedge->v[0] = plastvert;
            ptedge->v[1] = ptvert;

            ptedge = &pbedges[numbedges++];
            ptedge->pnext = psideedges[side];
            psideedges[side] = ptedge;
            ptedge->v[0] = ptvert;
            ptedge->v[1] = pvert;

            if (side == 0) {
                pfrontenter = ptvert;
            } else {
                pfrontexit = ptvert;
            }
            makeclippededge = true;
        } else {
            pedges->pnext = psideedges[side];
            psideedges[side] = pedges;
        }
    }

    if (makeclippededge) {
        if (numbedges >= MAX_BMODEL_EDGES) {
            return;
        }
        ptedge = &pbedges[numbedges++];
        ptedge->pnext = psideedges[0];
        psideedges[0] = ptedge;
        ptedge->v[0] = pfrontexit;
        ptedge->v[1] = pfrontenter;
    }

    for (int i = 0; i < 2; i++) {
        if (psideedges[i]) {
            if (pnode->children[i]->contents >= 0) {
                R_RecursiveClipBPoly(psideedges[i], pnode->children[i], psurf);
            } else if (pnode->children[i]->contents != CONTENTS_SOLID) {
                R_RenderBmodelFace(psideedges[i], psurf);
            }
        }
    }
}

void R_DrawSolidClippedSubmodelPolygons(model_t* pmodel)
{
    mvertex_t bverts[MAX_BMODEL_VERTS];
    bedge_t bedges[MAX_BMODEL_EDGES];
    msurface_t* psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    int numsurfaces = pmodel->nummodelsurfaces;
    medge_t* pedges = pmodel->edges;

    for (int i = 0; i < numsurfaces; i++, psurf++) {
        mplane_t* pplane = psurf->plane;
        float dot = DotProduct(modelorg, pplane->normal) - pplane->dist;
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON))
            || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            pbverts = bverts;
            pbedges = bedges;
            numbverts = numbedges = 0;
            if (psurf->numedges > 0) {
                bedge_t* pbedge = &bedges[numbedges];
                numbedges += psurf->numedges;
                int j = 0;
                for (; j < psurf->numedges; j++) {
                    int lindex = pmodel->surfedges[psurf->firstedge + j];
                    if (lindex > 0) {
                        medge_t* pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
                    } else {
                        lindex = -lindex;
                        medge_t* pedge = &pedges[lindex];
                        pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
                        pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
                    }
                    pbedge[j].pnext = &pbedge[j + 1];
                }
                pbedge[j - 1].pnext = nullptr;
                R_RecursiveClipBPoly(pbedge, currententity->topnode, psurf);
            } else {
                Common::Sys_Error("no edges in bmodel");
            }
        }
    }
}

void R_DrawSubmodelPolygons(model_t* pmodel, int clipflags)
{
    msurface_t* psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
    int numsurfaces = pmodel->nummodelsurfaces;
    for (int i = 0; i < numsurfaces; i++, psurf++) {
        mplane_t* pplane = psurf->plane;
        float dot = DotProduct(modelorg, pplane->normal) - pplane->dist;
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON))
            || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            r_currentkey = ((mleaf_t*)currententity->topnode)->key;
            R_RenderFace(psurf, clipflags);
        }
    }
}

void R_RecursiveWorldNode(mnode_t* node, int clipflags)
{
    if (node->contents == CONTENTS_SOLID) {
        return;
    }
    if (node->visframe != r_visframecount) {
        return;
    }

    if (clipflags) {
        for (int i = 0; i < 4; i++) {
            if (!(clipflags & (1 << i))) {
                continue;
            }
            int* pindex = pfrustum_indexes[i];
            Vector3 rejectpt(
                (float)node->minmaxs[pindex[0]], (float)node->minmaxs[pindex[1]], (float)node->minmaxs[pindex[2]]);
            double d = rejectpt.dot(view_clipplanes[i].normal) - view_clipplanes[i].dist;
            if (d <= 0) {
                return;
            }
            Vector3 acceptpt((float)node->minmaxs[pindex[3 + 0]], (float)node->minmaxs[pindex[3 + 1]],
                (float)node->minmaxs[pindex[3 + 2]]);
            d = acceptpt.dot(view_clipplanes[i].normal) - view_clipplanes[i].dist;
            if (d >= 0) {
                clipflags &= ~(1 << i);
            }
        }
    }

    if (node->contents < 0) {
        mleaf_t* pleaf = (mleaf_t*)node;
        msurface_t** mark = pleaf->firstmarksurface;
        int c = pleaf->nummarksurfaces;
        if (c) {
            do {
                (*mark)->visframe = r_framecount;
                mark++;
            } while (--c);
        }
        if (pleaf->efrags) {
            R_StoreEfrags(&pleaf->efrags);
        }
        pleaf->key = r_currentkey++;
    } else {
        mplane_t* plane = node->plane;
        double dot;
        switch (plane->type) {
        case PLANE_X:
            dot = modelorg[0] - plane->dist;
            break;
        case PLANE_Y:
            dot = modelorg[1] - plane->dist;
            break;
        case PLANE_Z:
            dot = modelorg[2] - plane->dist;
            break;
        default:
            dot = DotProduct(modelorg, plane->normal) - plane->dist;
            break;
        }
        int side = (dot >= 0) ? 0 : 1;
        R_RecursiveWorldNode(node->children[side], clipflags);

        int c = node->numsurfaces;
        if (c) {
            msurface_t* surf = Client::cl.worldmodel->surfaces + node->firstsurface;
            if (dot < -BACKFACE_EPSILON) {
                do {
                    if ((surf->flags & SURF_PLANEBACK) && (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags = clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }
                    surf++;
                } while (--c);
            } else if (dot > BACKFACE_EPSILON) {
                do {
                    if (!(surf->flags & SURF_PLANEBACK) && (surf->visframe == r_framecount)) {
                        if (r_drawpolys) {
                            if (r_worldpolysbacktofront) {
                                if (numbtofpolys < MAX_BTOFPOLYS) {
                                    pbtofpolys[numbtofpolys].clipflags = clipflags;
                                    pbtofpolys[numbtofpolys].psurf = surf;
                                    numbtofpolys++;
                                }
                            } else {
                                R_RenderPoly(surf, clipflags);
                            }
                        } else {
                            R_RenderFace(surf, clipflags);
                        }
                    }
                    surf++;
                } while (--c);
            }
            r_currentkey++;
        }
        R_RecursiveWorldNode(node->children[!side], clipflags);
    }
}

void R_RenderWorld()
{
    std::array<btofpoly_t, MAX_BTOFPOLYS> btofpolys { };
    pbtofpolys = btofpolys.data();
    currententity = &Client::cl_entities[0];
    VectorCopy(r_origin, modelorg);
    model_t* clmodel = currententity->model;
    r_pcurrentvertbase = clmodel->vertexes;
    R_RecursiveWorldNode(clmodel->nodes, 15);
    if (r_worldpolysbacktofront) {
        for (int i = numbtofpolys - 1; i >= 0; i--) {
            R_RenderPoly(btofpolys[i].psurf, btofpolys[i].clipflags);
        }
    }
}

} // namespace Render
