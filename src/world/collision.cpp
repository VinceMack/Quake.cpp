// collision.cpp -- World collision, BSP hull tracing & raycasting
#include "quakedef.hpp"
#include "world/collision.hpp"

using namespace Common;

namespace Collision {

static hull_t box_hull;
static dclipnode_t box_clipnodes[6];
static mplane_t box_planes[6];

void InitBoxHull()
{
    box_hull.clipnodes = box_clipnodes;
    box_hull.planes = box_planes;
    box_hull.firstclipnode = 0;
    box_hull.lastclipnode = 5;

    for (int i = 0; i < 6; i++) {
        box_clipnodes[i].planenum = i;
        int side = i & 1;
        box_clipnodes[i].children[side] = CONTENTS_EMPTY;
        if (i != 5) {
            box_clipnodes[i].children[side ^ 1] = static_cast<short>(i + 1);
        } else {
            box_clipnodes[i].children[side ^ 1] = CONTENTS_SOLID;
        }

        box_planes[i].type = static_cast<byte>(i >> 1);
        box_planes[i].normal[i >> 1] = 1;
    }
}

hull_t* HullForBox(const Vector3& mins, const Vector3& maxs)
{
    box_planes[0].dist = maxs.x;
    box_planes[1].dist = mins.x;
    box_planes[2].dist = maxs.y;
    box_planes[3].dist = mins.y;
    box_planes[4].dist = maxs.z;
    box_planes[5].dist = mins.z;

    return &box_hull;
}

int HullPointContents(hull_t* hull, int num, const Vector3& p)
{
    float d;
    dclipnode_t* node;
    mplane_t* plane;

    while (num >= 0) {
        if (num < hull->firstclipnode || num > hull->lastclipnode) {
            Sys_Error("HullPointContents: bad node number");
        }
        node = hull->clipnodes + num;
        plane = hull->planes + node->planenum;

        if (plane->type < 3) {
            d = p[plane->type] - plane->dist;
        } else {
            d = plane->normal.dot(p) - plane->dist;
        }

        if (d < 0) {
            num = node->children[1];
        } else {
            num = node->children[0];
        }
    }
    return num;
}

constexpr double DIST_EPSILON = 0.03125;

qboolean RecursiveHullCheck(hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace)
{
    dclipnode_t* node;
    mplane_t* plane;
    float t1, t2, frac, midf;
    Vector3 mid;
    int side;

    if (num < 0) {
        if (num != CONTENTS_SOLID) {
            trace->allsolid = false;
            if (num == CONTENTS_EMPTY) trace->inopen = true;
            else trace->inwater = true;
        } else {
            trace->startsolid = true;
        }
        return true;
    }

    if (num < hull->firstclipnode || num > hull->lastclipnode) {
        Sys_Error("RecursiveHullCheck: bad node number");
    }

    node = hull->clipnodes + num;
    plane = hull->planes + node->planenum;

    if (plane->type < 3) {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
    } else {
        t1 = plane->normal.dot(p1) - plane->dist;
        t2 = plane->normal.dot(p2) - plane->dist;
    }

    if (t1 >= 0 && t2 >= 0) return RecursiveHullCheck(hull, node->children[0], p1f, p2f, p1, p2, trace);
    if (t1 < 0 && t2 < 0) return RecursiveHullCheck(hull, node->children[1], p1f, p2f, p1, p2, trace);

    if (t1 < 0) frac = static_cast<float>((t1 + DIST_EPSILON) / (t1 - t2));
    else frac = static_cast<float>((t1 - DIST_EPSILON) / (t1 - t2));

    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    midf = p1f + (p2f - p1f) * frac;
    mid = p1 + (p2 - p1) * frac;
    side = (t1 < 0);

    if (!RecursiveHullCheck(hull, node->children[side], p1f, midf, p1, mid, trace)) return false;

    if (HullPointContents(hull, node->children[side ^ 1], mid) != CONTENTS_SOLID) {
        return RecursiveHullCheck(hull, node->children[side ^ 1], midf, p2f, mid, p2, trace);
    }

    if (trace->allsolid) return false;

    if (!side) {
        trace->plane.normal = plane->normal;
        trace->plane.dist = plane->dist;
    } else {
        trace->plane.normal = -plane->normal;
        trace->plane.dist = -plane->dist;
    }

    while (HullPointContents(hull, hull->firstclipnode, mid) == CONTENTS_SOLID) {
        frac -= 0.1f;
        if (frac < 0) {
            trace->fraction = midf;
            trace->endpos = mid;
            return false;
        }
        midf = p1f + (p2f - p1f) * frac;
        mid = p1 + (p2 - p1) * frac;
    }

    trace->fraction = midf;
    trace->endpos = mid;
    return false;
}

} // namespace Collision
