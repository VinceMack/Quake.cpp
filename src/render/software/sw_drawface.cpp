// sw_drawface.cpp -- Polygon and Bmodel Face Clipping and Edge Emission
#include "render/software/sw_drawface.hpp"
#include "client/client_types.hpp"
#include "world/bsp_format.hpp"

#include <cmath>

namespace Render {

polydesc_t r_polydesc { };
int r_currentbkey = 0;
int r_clipflags = 0;

bool r_lastvertvalid = false;
int r_emitted = 0;
float r_nearzi = 0.0f;
float r_u1 = 0.0f, r_v1 = 0.0f, r_lzi1 = 0.0f;
int r_ceilv1 = 0;
bool r_nearzionly = false;

static unsigned int cacheoffset = 0;
static medge_t* r_pedge = nullptr;
static bool r_leftclipped = false, r_rightclipped = false;
static bool makeleftedge = false, makerightedge = false;
static mvertex_t r_leftenter { }, r_leftexit { };
static mvertex_t r_rightenter { }, r_rightexit { };

void R_EmitEdge(mvertex_t* pv0, mvertex_t* pv1)
{
    edge_t *edge, *pcheck;
    int64_t u_check;
    float u, u_step;
    Vector3 local, transformed;
    float* world;
    int v, v2, ceilv0;
    float scale, lzi0, u0, v0;
    int side;

    if (r_lastvertvalid) {
        u0 = r_u1;
        v0 = r_v1;
        lzi0 = r_lzi1;
        ceilv0 = r_ceilv1;
    } else {
        world = &pv0->position[0];
        local = Vector3(world) - modelorg;
        TransformVector(local, transformed);
        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }
        lzi0 = static_cast<float>(1.0 / transformed.z);
        scale = xscale * lzi0;
        u0 = (xcenter + scale * transformed.x);
        if (u0 < r_refdef.fvrectx_adj) {
            u0 = r_refdef.fvrectx_adj;
        }
        if (u0 > r_refdef.fvrectright_adj) {
            u0 = r_refdef.fvrectright_adj;
        }
        scale = yscale * lzi0;
        v0 = (ycenter - scale * transformed.y);
        if (v0 < r_refdef.fvrecty_adj) {
            v0 = r_refdef.fvrecty_adj;
        }
        if (v0 > r_refdef.fvrectbottom_adj) {
            v0 = r_refdef.fvrectbottom_adj;
        }
        ceilv0 = static_cast<int>(std::ceil(v0));
    }

    world = &pv1->position[0];
    local = Vector3(world) - modelorg;
    TransformVector(local, transformed);
    if (transformed.z < NEAR_CLIP) {
        transformed.z = (vec_t)NEAR_CLIP;
    }
    r_lzi1 = static_cast<float>(1.0 / transformed.z);
    scale = xscale * r_lzi1;
    r_u1 = (xcenter + scale * transformed.x);
    if (r_u1 < r_refdef.fvrectx_adj) {
        r_u1 = r_refdef.fvrectx_adj;
    }
    if (r_u1 > r_refdef.fvrectright_adj) {
        r_u1 = r_refdef.fvrectright_adj;
    }
    scale = yscale * r_lzi1;
    r_v1 = (ycenter - scale * transformed.y);
    if (r_v1 < r_refdef.fvrecty_adj) {
        r_v1 = r_refdef.fvrecty_adj;
    }
    if (r_v1 > r_refdef.fvrectbottom_adj) {
        r_v1 = r_refdef.fvrectbottom_adj;
    }
    if (r_lzi1 > lzi0) {
        lzi0 = r_lzi1;
    }
    if (lzi0 > r_nearzi) {
        r_nearzi = lzi0;
    }
    if (r_nearzionly) {
        return;
    }
    r_emitted = 1;
    r_ceilv1 = static_cast<int>(std::ceil(r_v1));
    if (ceilv0 == r_ceilv1) {
        if (cacheoffset != 0x7FFFFFFF) {
            cacheoffset = FULLY_CLIPPED_CACHED | (r_framecount & FRAMECOUNT_MASK);
        }
        return;
    }
    side = ceilv0 > r_ceilv1;
    edge = edge_p++;
    edge->owner = r_pedge;
    edge->nearzi = lzi0;
    if (side == 0) {
        v = ceilv0;
        v2 = r_ceilv1 - 1;
        edge->surfs[0] = static_cast<unsigned short>(surface_p - surfaces);
        edge->surfs[1] = 0;
        u_step = ((r_u1 - u0) / (r_v1 - v0));
        u = u0 + ((float)v - v0) * u_step;
    } else {
        v2 = ceilv0 - 1;
        v = r_ceilv1;
        edge->surfs[0] = 0;
        edge->surfs[1] = static_cast<unsigned short>(surface_p - surfaces);
        u_step = ((u0 - r_u1) / (v0 - r_v1));
        u = r_u1 + ((float)v - r_v1) * u_step;
    }
    edge->u_step = static_cast<int64_t>(u_step * 0x100000);
    edge->u = static_cast<int64_t>(u * 0x100000 + 0xFFFFF);
    if (edge->u < r_refdef.vrect_x_adj_shift20) {
        edge->u = r_refdef.vrect_x_adj_shift20;
    }
    if (edge->u > r_refdef.vrectright_adj_shift20) {
        edge->u = r_refdef.vrectright_adj_shift20;
    }

    u_check = edge->u;
    if (edge->surfs[0]) {
        u_check++;
    }
    if (!newedges[v] || newedges[v]->u >= u_check) {
        edge->next = newedges[v];
        newedges[v] = edge;
    } else {
        pcheck = newedges[v];
        while (pcheck->next && pcheck->next->u < u_check) {
            pcheck = pcheck->next;
        }
        edge->next = pcheck->next;
        pcheck->next = edge;
    }
    edge->nextremove = removeedges[v2];
    removeedges[v2] = edge;
}

void R_ClipEdge(mvertex_t* pv0, mvertex_t* pv1, clipplane_t* clip)
{
    float d0, d1, f;
    mvertex_t clipvert;
    if (clip) {
        do {
            d0 = DotProduct(pv0->position, clip->normal) - clip->dist;
            d1 = DotProduct(pv1->position, clip->normal) - clip->dist;
            if (d0 >= 0) {
                if (d1 >= 0) {
                    continue;
                }
                cacheoffset = 0x7FFFFFFF;
                f = d0 / (d0 - d1);
                clipvert.position[0] = pv0->position[0] + f * (pv1->position[0] - pv0->position[0]);
                clipvert.position[1] = pv0->position[1] + f * (pv1->position[1] - pv0->position[1]);
                clipvert.position[2] = pv0->position[2] + f * (pv1->position[2] - pv0->position[2]);
                if (clip->leftedge) {
                    r_leftclipped = true;
                    r_leftexit = clipvert;
                } else if (clip->rightedge) {
                    r_rightclipped = true;
                    r_rightexit = clipvert;
                }
                R_ClipEdge(pv0, &clipvert, clip->next);
                return;
            } else {
                if (d1 < 0) {
                    if (!r_leftclipped) {
                        cacheoffset = FULLY_CLIPPED_CACHED | (r_framecount & FRAMECOUNT_MASK);
                    }
                    return;
                }
                r_lastvertvalid = false;
                cacheoffset = 0x7FFFFFFF;
                f = d0 / (d0 - d1);
                clipvert.position[0] = pv0->position[0] + f * (pv1->position[0] - pv0->position[0]);
                clipvert.position[1] = pv0->position[1] + f * (pv1->position[1] - pv0->position[1]);
                clipvert.position[2] = pv0->position[2] + f * (pv1->position[2] - pv0->position[2]);
                if (clip->leftedge) {
                    r_leftclipped = true;
                    r_leftenter = clipvert;
                } else if (clip->rightedge) {
                    r_rightclipped = true;
                    r_rightenter = clipvert;
                }
                R_ClipEdge(&clipvert, pv1, clip->next);
                return;
            }
        } while ((clip = clip->next) != nullptr);
    }
    R_EmitEdge(pv0, pv1);
}

void R_EmitCachedEdge()
{
    edge_t* pedge_t = (edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset);
    if (!pedge_t->surfs[0]) {
        pedge_t->surfs[0] = static_cast<unsigned short>(surface_p - surfaces);
    } else {
        pedge_t->surfs[1] = static_cast<unsigned short>(surface_p - surfaces);
    }
    if (pedge_t->nearzi > r_nearzi) {
        r_nearzi = pedge_t->nearzi;
    }
    r_emitted = 1;
}

void R_RenderFace(msurface_t* fa, int clipflags)
{
    if (surface_p >= surf_max) {
        r_outofsurfaces++;
        return;
    }
    if ((edge_p + fa->numedges + 4) >= edge_max) {
        r_outofedges += fa->numedges;
        return;
    }
    c_faceclip++;

    clipplane_t* pclip = nullptr;
    unsigned mask = 0x08;
    for (int i = 3; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = false;
    makeleftedge = makerightedge = false;
    medge_t* pedges = currententity->model->edges;
    static medge_t tedge;
    r_lastvertvalid = false;

    for (int i = 0; i < fa->numedges; i++) {
        int lindex = currententity->model->surfedges[fa->firstedge + i];
        if (lindex > 0) {
            r_pedge = &pedges[lindex];
            if (!insubmodel) {
                if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                    if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) == (unsigned int)r_framecount) {
                        r_lastvertvalid = false;
                        continue;
                    }
                } else {
                    if ((((size_t)edge_p - (size_t)r_edges) > r_pedge->cachededgeoffset)
                        && (((edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset))->owner == r_pedge)) {
                        R_EmitCachedEdge();
                        r_lastvertvalid = false;
                        continue;
                    }
                }
            }
            cacheoffset = static_cast<unsigned int>(reinterpret_cast<byte*>(edge_p) - reinterpret_cast<byte*>(r_edges));
            r_leftclipped = r_rightclipped = false;
            R_ClipEdge(&r_pcurrentvertbase[r_pedge->v[0]], &r_pcurrentvertbase[r_pedge->v[1]], pclip);
            r_pedge->cachededgeoffset = cacheoffset;
            if (r_leftclipped) {
                makeleftedge = true;
            }
            if (r_rightclipped) {
                makerightedge = true;
            }
            r_lastvertvalid = true;
        } else {
            lindex = -lindex;
            r_pedge = &pedges[lindex];
            if (!insubmodel) {
                if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                    if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) == (unsigned int)r_framecount) {
                        r_lastvertvalid = false;
                        continue;
                    }
                } else {
                    if ((((size_t)edge_p - (size_t)r_edges) > r_pedge->cachededgeoffset)
                        && (((edge_t*)((size_t)r_edges + r_pedge->cachededgeoffset))->owner == r_pedge)) {
                        R_EmitCachedEdge();
                        r_lastvertvalid = false;
                        continue;
                    }
                }
            }
            cacheoffset = static_cast<unsigned int>(reinterpret_cast<byte*>(edge_p) - reinterpret_cast<byte*>(r_edges));
            r_leftclipped = r_rightclipped = false;
            R_ClipEdge(&r_pcurrentvertbase[r_pedge->v[1]], &r_pcurrentvertbase[r_pedge->v[0]], pclip);
            r_pedge->cachededgeoffset = cacheoffset;
            if (r_leftclipped) {
                makeleftedge = true;
            }
            if (r_rightclipped) {
                makerightedge = true;
            }
            r_lastvertvalid = true;
        }
    }

    if (makeleftedge) {
        r_pedge = &tedge;
        r_lastvertvalid = false;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }
    if (makerightedge) {
        r_pedge = &tedge;
        r_lastvertvalid = false;
        r_nearzionly = true;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }
    if (!r_emitted) {
        return;
    }

    r_polycount++;
    surface_p->data = (void*)fa;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = fa->flags;
    surface_p->insubmodel = insubmodel;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentkey++;
    surface_p->spans = nullptr;
    mplane_t* pplane = fa->plane;
    Vector3 p_normal;
    TransformVector(pplane->normal, p_normal);
    float distinv = static_cast<float>(1.0 / (pplane->dist - modelorg.dot(pplane->normal)));
    surface_p->d_zistepu = p_normal.x * xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal.y * yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal.z * distinv - xcenter * surface_p->d_zistepu - ycenter * surface_p->d_zistepv;
    surface_p++;
}

void R_RenderBmodelFace(bedge_t* pedges, msurface_t* psurf)
{
    if (surface_p >= surf_max) {
        r_outofsurfaces++;
        return;
    }
    if ((edge_p + psurf->numedges + 4) >= edge_max) {
        r_outofedges += psurf->numedges;
        return;
    }
    c_faceclip++;

    static medge_t tedge;
    r_pedge = &tedge;
    clipplane_t* pclip = nullptr;
    unsigned mask = 0x08;
    for (int i = 3; i >= 0; i--, mask >>= 1) {
        if (r_clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = false;
    makeleftedge = makerightedge = false;
    r_lastvertvalid = false;
    for (; pedges; pedges = pedges->pnext) {
        r_leftclipped = r_rightclipped = false;
        R_ClipEdge(pedges->v[0], pedges->v[1], pclip);
        if (r_leftclipped) {
            makeleftedge = true;
        }
        if (r_rightclipped) {
            makerightedge = true;
        }
    }

    if (makeleftedge) {
        r_pedge = &tedge;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }
    if (makerightedge) {
        r_pedge = &tedge;
        r_nearzionly = true;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }
    if (!r_emitted) {
        return;
    }

    r_polycount++;
    surface_p->data = (void*)psurf;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = psurf->flags;
    surface_p->insubmodel = true;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentbkey;
    surface_p->spans = nullptr;
    mplane_t* pplane = psurf->plane;
    Vector3 p_normal;
    TransformVector(pplane->normal, p_normal);
    float distinv = static_cast<float>(1.0 / (pplane->dist - modelorg.dot(pplane->normal)));
    surface_p->d_zistepu = p_normal.x * xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal.y * yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal.z * distinv - xcenter * surface_p->d_zistepu - ycenter * surface_p->d_zistepv;
    surface_p++;
}

void R_RenderPoly(msurface_t* fa, int clipflags)
{
    int s_axis = 0, t_axis = 0;
    clipplane_t* pclip = nullptr;
    unsigned mask = 0x08;
    for (int i = 3; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

    medge_t* pedges = currententity->model->edges;
    int lnumverts = fa->numedges;
    int vertpage = 0;
    mvertex_t verts[2][100];
    polyvert_t pverts[100];

    for (int i = 0; i < lnumverts; i++) {
        int lindex = currententity->model->surfedges[fa->firstedge + i];
        if (lindex > 0) {
            r_pedge = &pedges[lindex];
            verts[0][i] = r_pcurrentvertbase[r_pedge->v[0]];
        } else {
            r_pedge = &pedges[-lindex];
            verts[0][i] = r_pcurrentvertbase[r_pedge->v[1]];
        }
    }

    while (pclip) {
        int lastvert = lnumverts - 1;
        float lastdist = Vector3(verts[vertpage][lastvert].position).dot(pclip->normal) - pclip->dist;
        bool visible = false;
        int newverts = 0;
        int newpage = vertpage ^ 1;
        for (int i = 0; i < lnumverts; i++) {
            float dist = Vector3(verts[vertpage][i].position).dot(pclip->normal) - pclip->dist;
            if ((lastdist > 0) != (dist > 0)) {
                float frac = dist / (dist - lastdist);
                Vector3 interp = Vector3(verts[vertpage][i].position)
                    + (Vector3(verts[vertpage][lastvert].position) - Vector3(verts[vertpage][i].position)) * frac;
                verts[newpage][newverts].position[0] = interp.x;
                verts[newpage][newverts].position[1] = interp.y;
                verts[newpage][newverts].position[2] = interp.z;
                newverts++;
            }
            if (dist >= 0) {
                verts[newpage][newverts] = verts[vertpage][i];
                newverts++;
                visible = true;
            }
            lastvert = i;
            lastdist = dist;
        }
        if (!visible || (newverts < 3)) {
            return;
        }
        lnumverts = newverts;
        vertpage ^= 1;
        pclip = pclip->next;
    }

    mplane_t* pplane = fa->plane;
    switch (pplane->type) {
    case PLANE_X:
    case PLANE_ANYX:
        s_axis = 1;
        t_axis = 2;
        break;
    case PLANE_Y:
    case PLANE_ANYY:
        s_axis = 0;
        t_axis = 2;
        break;
    case PLANE_Z:
    case PLANE_ANYZ:
        s_axis = 0;
        t_axis = 1;
        break;
    }

    r_nearzi = 0;
    for (int i = 0; i < lnumverts; i++) {
        Vector3 local = Vector3(verts[vertpage][i].position) - modelorg;
        Vector3 transformed;
        TransformVector(local, transformed);
        if (transformed.z < NEAR_CLIP) {
            transformed.z = (vec_t)NEAR_CLIP;
        }
        float lzi = static_cast<float>(1.0 / transformed.z);
        if (lzi > r_nearzi) {
            r_nearzi = lzi;
        }
        float scale = xscale * lzi;
        float u = (xcenter + scale * transformed.x);
        if (u < r_refdef.fvrectx_adj) {
            u = r_refdef.fvrectx_adj;
        }
        if (u > r_refdef.fvrectright_adj) {
            u = r_refdef.fvrectright_adj;
        }
        scale = yscale * lzi;
        float v = (ycenter - scale * transformed.y);
        if (v < r_refdef.fvrecty_adj) {
            v = r_refdef.fvrecty_adj;
        }
        if (v > r_refdef.fvrectbottom_adj) {
            v = r_refdef.fvrectbottom_adj;
        }
        pverts[i].u = u;
        pverts[i].v = v;
        pverts[i].zi = lzi;
        pverts[i].s = verts[vertpage][i].position[s_axis];
        pverts[i].t = verts[vertpage][i].position[t_axis];
    }

    r_polydesc.numverts = lnumverts;
    r_polydesc.nearzi = r_nearzi;
    r_polydesc.pcurrentface = fa;
    r_polydesc.pverts = pverts;
    D_DrawPoly();
}

} // namespace Render
