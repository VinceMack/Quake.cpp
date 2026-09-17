// sw_edge.cpp -- Edge List Generation, Insertion, and Active Edge Scanline Rasterization
#include "render/software/sw_edge.hpp"
#include "render/software/sw_vid.hpp"
#include "audio/audio_main.hpp"
#include "world/bsp_format.hpp"

#include <climits>

namespace Render {

void R_DrawCulledPolys();

surf_t *surfaces = nullptr, *surface_p = nullptr, *surf_max = nullptr;
edge_t *r_edges = nullptr, *edge_p = nullptr, *edge_max = nullptr;
std::array<edge_t*, MAXHEIGHT> newedges{};
std::array<edge_t*, MAXHEIGHT> removeedges{};

edge_t edge_head{}, edge_tail{}, edge_aftertail{};
static edge_t edge_sentinel{};

static espan_t *span_p = nullptr, *max_span_p = nullptr;
static int current_iv = 0;
static int64_t edge_head_u_shift20 = 0, edge_tail_u_shift20 = 0;
static float edge_fv = 0.0f;
static void (*pdrawfunc)(void) = nullptr;

int r_currentkey = 0;
int r_bmodelactive = 0;

void R_BeginEdgeFrame()
{
    edge_p = r_edges;
    edge_max = &r_edges[r_numallocatededges];
    surface_p = &surfaces[2]; // background is surface 1, surface 0 is a dummy
    surfaces[1].spans = nullptr; // no background spans yet
    surfaces[1].flags = SURF_DRAWBACKGROUND;

    if (r_draworder.value) {
        pdrawfunc = R_GenerateSpansBackward;
        surfaces[1].key = 0;
        r_currentkey = 1;
    } else {
        pdrawfunc = R_GenerateSpans;
        surfaces[1].key = 0x7FFFFFFF;
        r_currentkey = 0;
    }

    for (int v = r_refdef.vrect.y; v < r_refdef.vrectbottom; v++) {
        newedges[v] = removeedges[v] = nullptr;
    }
}

void R_InsertNewEdges(edge_t* edgestoadd, edge_t* edgelist)
{
    do {
        edge_t* next_edge = edgestoadd->next;
    edgesearch:
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }
        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }
        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }
        edgelist = edgelist->next;
        if (edgelist->u >= edgestoadd->u) {
            goto addedge;
        }
        edgelist = edgelist->next;
        goto edgesearch;

    addedge:
        edgestoadd->next = edgelist;
        edgestoadd->prev = edgelist->prev;
        edgelist->prev->next = edgestoadd;
        edgelist->prev = edgestoadd;
        edgestoadd = next_edge;
    } while (edgestoadd != nullptr);
}

void R_RemoveEdges(edge_t* pedge)
{
    do {
        pedge->next->prev = pedge->prev;
        pedge->prev->next = pedge->next;
    } while ((pedge = pedge->nextremove) != nullptr);
}

void R_StepActiveU(edge_t* pedge)
{
    while (true) {
    nextedge:
        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }
        pedge = pedge->next;
        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }
        pedge = pedge->next;
        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }
        pedge = pedge->next;
        pedge->u += pedge->u_step;
        if (pedge->u < pedge->prev->u) {
            goto pushback;
        }
        pedge = pedge->next;
        goto nextedge;

    pushback:
        if (pedge == &edge_aftertail) {
            return;
        }
        edge_t* pnext_edge = pedge->next;
        pedge->next->prev = pedge->prev;
        pedge->prev->next = pedge->next;

        edge_t* pwedge = pedge->prev->prev;
        while (pwedge->u > pedge->u) {
            pwedge = pwedge->prev;
        }

        pedge->next = pwedge->next;
        pedge->prev = pwedge;
        pedge->next->prev = pedge;
        pwedge->next = pedge;
        pedge = pnext_edge;
        if (pedge == &edge_tail) {
            return;
        }
    }
}

void R_CleanupSpan()
{
    surf_t* surf = surfaces[1].next;
    int iu = static_cast<int>(edge_tail_u_shift20);
    if (iu > surf->last_u) {
        espan_t* span = span_p++;
        span->u = surf->last_u;
        span->count = iu - span->u;
        span->v = current_iv;
        span->pnext = surf->spans;
        surf->spans = span;
    }
    do {
        surf->spanstate = 0;
        surf = surf->next;
    } while (surf != &surfaces[1]);
}

void R_LeadingEdgeBackwards(edge_t* edge)
{
    surf_t* surf = &surfaces[edge->surfs[1]];
    if (++surf->spanstate == 1) {
        surf_t* surf2 = surfaces[1].next;
        if (surf->key > surf2->key) {
            goto newtop;
        }
        if (surf->insubmodel && (surf->key == surf2->key)) {
            goto newtop;
        }
    continue_search:
        do {
            surf2 = surf2->next;
        } while (surf->key < surf2->key);
        if (surf->key == surf2->key) {
            if (!surf->insubmodel) {
                goto continue_search;
            }
        }
        goto gotposition;

    newtop: {
        int iu = static_cast<int>(edge->u >> 20);
        if (iu > surf2->last_u) {
            espan_t* span = span_p++;
            span->u = surf2->last_u;
            span->count = iu - span->u;
            span->v = current_iv;
            span->pnext = surf2->spans;
            surf2->spans = span;
        }
        surf->last_u = iu;
    }

    gotposition:
        surf->next = surf2;
        surf->prev = surf2->prev;
        surf2->prev->next = surf;
        surf2->prev = surf;
    }
}

void R_TrailingEdge(surf_t* surf, edge_t* edge)
{
    if (--surf->spanstate == 0) {
        if (surf->insubmodel) {
            r_bmodelactive--;
        }
        if (surf == surfaces[1].next) {
            int iu = static_cast<int>(edge->u >> 20);
            if (iu > surf->last_u) {
                espan_t* span = span_p++;
                span->u = surf->last_u;
                span->count = iu - span->u;
                span->v = current_iv;
                span->pnext = surf->spans;
                surf->spans = span;
            }
            surf->next->last_u = iu;
        }
        surf->prev->next = surf->next;
        surf->next->prev = surf->prev;
    }
}

void R_LeadingEdge(edge_t* edge)
{
    if (edge->surfs[1]) {
        surf_t* surf = &surfaces[edge->surfs[1]];
        if (++surf->spanstate == 1) {
            if (surf->insubmodel) {
                r_bmodelactive++;
            }
            surf_t* surf2 = surfaces[1].next;
            if (surf->key < surf2->key) {
                goto newtop;
            }
            if (surf->insubmodel && (surf->key == surf2->key)) {
                double fu = static_cast<float>(edge->u - 0xFFFFF) * (1.0f / 0x100000);
                double newzi = surf->d_ziorigin + edge_fv * surf->d_zistepv + fu * surf->d_zistepu;
                double newzibottom = newzi * 0.99f;
                double testzi = surf2->d_ziorigin + edge_fv * surf2->d_zistepv + fu * surf2->d_zistepu;
                if (newzibottom >= testzi) {
                    goto newtop;
                }
                double newzitop = newzi * 1.01;
                if (newzitop >= testzi) {
                    if (surf->d_zistepu >= surf2->d_zistepu) {
                        goto newtop;
                    }
                }
            }
        continue_search:
            do {
                surf2 = surf2->next;
            } while (surf->key > surf2->key);
            if (surf->key == surf2->key) {
                if (!surf->insubmodel) {
                    goto continue_search;
                }
                double fu = static_cast<float>(edge->u - 0xFFFFF) * (1.0f / 0x100000);
                double newzi = surf->d_ziorigin + edge_fv * surf->d_zistepv + fu * surf->d_zistepu;
                double newzibottom = newzi * 0.99f;
                double testzi = surf2->d_ziorigin + edge_fv * surf2->d_zistepv + fu * surf2->d_zistepu;
                if (newzibottom >= testzi) {
                    goto gotposition;
                }
                double newzitop = newzi * 1.01;
                if (newzitop >= testzi) {
                    if (surf->d_zistepu >= surf2->d_zistepu) {
                        goto gotposition;
                    }
                }
                goto continue_search;
            }
            goto gotposition;

        newtop: {
            int iu = static_cast<int>(edge->u >> 20);
            if (iu > surf2->last_u) {
                espan_t* span = span_p++;
                span->u = surf2->last_u;
                span->count = iu - span->u;
                span->v = current_iv;
                span->pnext = surf2->spans;
                surf2->spans = span;
            }
            surf->last_u = iu;
        }

        gotposition:
            surf->next = surf2;
            surf->prev = surf2->prev;
            surf2->prev->next = surf;
            surf2->prev = surf;
        }
    }
}

void R_GenerateSpans()
{
    r_bmodelactive = 0;
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = static_cast<int>(edge_head_u_shift20);
    for (edge_t* edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
        if (edge->surfs[0]) {
            surf_t* surf = &surfaces[edge->surfs[0]];
            R_TrailingEdge(surf, edge);
            if (!edge->surfs[1]) {
                continue;
            }
        }
        R_LeadingEdge(edge);
    }
    R_CleanupSpan();
}

void R_GenerateSpansBackward()
{
    r_bmodelactive = 0;
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = static_cast<int>(edge_head_u_shift20);
    for (edge_t* edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
        if (edge->surfs[0]) {
            R_TrailingEdge(&surfaces[edge->surfs[0]], edge);
        }
        if (edge->surfs[1]) {
            R_LeadingEdgeBackwards(edge);
        }
    }
    R_CleanupSpan();
}

void R_ScanEdges()
{
    byte basespans[MAXSPANS * sizeof(espan_t) + CACHE_SIZE];
    espan_t* basespan_p = (espan_t*)((size_t)(basespans + CACHE_SIZE - 1) & ~(size_t)(CACHE_SIZE - 1));
    max_span_p = &basespan_p[MAXSPANS - r_refdef.vrect.width];
    span_p = basespan_p;

    edge_head.u = (int64_t)r_refdef.vrect.x << 20;
    edge_head_u_shift20 = edge_head.u >> 20;
    edge_head.u_step = 0;
    edge_head.prev = nullptr;
    edge_head.next = &edge_tail;
    edge_head.surfs[0] = 0;
    edge_head.surfs[1] = 1;

    edge_tail.u = ((int64_t)r_refdef.vrectright << 20) + 0xFFFFF;
    edge_tail_u_shift20 = edge_tail.u >> 20;
    edge_tail.u_step = 0;
    edge_tail.prev = &edge_head;
    edge_tail.next = &edge_aftertail;
    edge_tail.surfs[0] = 1;
    edge_tail.surfs[1] = 0;

    edge_aftertail.u = -1;
    edge_aftertail.u_step = 0;
    edge_aftertail.next = &edge_sentinel;
    edge_aftertail.prev = &edge_tail;

    edge_sentinel.u = UINT_MAX;
    edge_sentinel.prev = &edge_aftertail;

    int bottom = r_refdef.vrectbottom - 1;
    int iv = r_refdef.vrect.y;
    for (; iv < bottom; iv++) {
        current_iv = iv;
        edge_fv = (float)iv;
        surfaces[1].spanstate = 1;
        if (newedges[iv]) {
            R_InsertNewEdges(newedges[iv], edge_head.next);
        }
        (*pdrawfunc)();
        if (span_p >= max_span_p) {
            if (r_drawculledpolys) {
                R_DrawCulledPolys();
            } else {
                D_DrawSurfaces();
            }
            for (surf_t* s = &surfaces[1]; s < surface_p; s++) {
                s->spans = nullptr;
            }
            span_p = basespan_p;
        }
        if (removeedges[iv]) {
            R_RemoveEdges(removeedges[iv]);
        }
        if (edge_head.next != &edge_tail) {
            R_StepActiveU(edge_head.next);
        }
    }

    current_iv = iv;
    edge_fv = (float)iv;
    surfaces[1].spanstate = 1;
    if (newedges[iv]) {
        R_InsertNewEdges(newedges[iv], edge_head.next);
    }
    (*pdrawfunc)();
    if (r_drawculledpolys) {
        R_DrawCulledPolys();
    } else {
        D_DrawSurfaces();
    }
}

} // namespace Render
