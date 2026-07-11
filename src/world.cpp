// world.cpp -- world query functions

#include "quakedef.hpp"

using namespace CDAudio;
using namespace Client;
using namespace Common;
using namespace Console;
using namespace Render;
using namespace Draw;
using namespace Host;
using namespace Input;
using namespace Keys;
using namespace Math;
using namespace Menu;
using namespace Model;
using namespace Net;
using namespace VM;
using namespace Sbar;
using namespace Screen;
using namespace Server;
using namespace Audio;
using namespace Vid;
using namespace View;
using namespace Wad;
using namespace Cvar;
using namespace Cmd;


namespace Server {

/*

entities never clip against themselves, or their owner

line of sight checks trace->crosscontent, but bullets don't

*/

typedef struct {
    Vector3 boxmins, boxmaxs; // enclose the test object along entire move
    Vector3 mins, maxs;       // size of the moving object
    Vector3 mins2, maxs2;     // size when clipping against mosnters
    Vector3 start, end;
    trace_t trace;
    int type;
    edict_t* passedict;
} moveclip_t;

int SV_HullPointContents(hull_t* hull, int num, const Vector3& p);

/*
===============================================================================

HULL BOXES

===============================================================================
*/

static hull_t box_hull;
static dclipnode_t box_clipnodes[6];
static mplane_t box_planes[6];

/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
void SV_InitBoxHull(void)
{
    int i;
    int side;

    box_hull.clipnodes = box_clipnodes;
    box_hull.planes = box_planes;
    box_hull.firstclipnode = 0;
    box_hull.lastclipnode = 5;

    for (i = 0; i < 6; i++) {
        box_clipnodes[i].planenum = i;

        side = i & 1;

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

/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
hull_t* SV_HullForBox(const Vector3& mins, const Vector3& maxs)
{
    box_planes[0].dist = maxs.x;
    box_planes[1].dist = mins.x;
    box_planes[2].dist = maxs.y;
    box_planes[3].dist = mins.y;
    box_planes[4].dist = maxs.z;
    box_planes[5].dist = mins.z;

    return &box_hull;
}

/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
hull_t* SV_HullForEntity(edict_t* ent,
    const Vector3& mins,
    const Vector3& maxs,
    Vector3& offset)
{
    model_t* model;
    Vector3 size;
    Vector3 hullmins, hullmaxs;
    hull_t* hull;

    // decide which clipping hull to use, based on the size
    if (ent->v.solid == SOLID_BSP) { // explicit hulls in the BSP model
        if (ent->v.movetype != MOVETYPE_PUSH) {
            Sys_Error("SOLID_BSP without MOVETYPE_PUSH");
        }

        model = sv.models[(int)ent->v.modelindex];

        if (!model || model->type != mod_brush) {
            Sys_Error("MOVETYPE_PUSH with a non bsp model");
        }

        size = maxs - mins;
        if (size.x < 3) {
            hull = &model->hulls[0];
        } else if (size.x <= 32) {
            hull = &model->hulls[1];
        } else {
            hull = &model->hulls[2];
        }

        // calculate an offset value to center the origin
        offset = hull->clip_mins - mins;
        offset += ent->v.origin;
    } else { // create a temp hull from bounding box sizes

        hullmins = ent->v.mins - maxs;
        hullmaxs = ent->v.maxs - mins;
        hull = SV_HullForBox(hullmins, hullmaxs);

        offset = ent->v.origin;
    }

    return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

typedef struct areanode_s {
    int axis; // -1 = leaf node
    float dist;
    struct areanode_s* children[2];
    link_t trigger_edicts;
    link_t solid_edicts;
} areanode_t;

#define AREA_DEPTH 4
#define AREA_NODES 32

static areanode_t sv_areanodes[AREA_NODES];
static int sv_numareanodes;

/*
===============
SV_CreateAreaNode

===============
*/
areanode_t* SV_CreateAreaNode(int depth, const Vector3& mins, const Vector3& maxs)
{
    areanode_t* anode;
    Vector3 size;
    Vector3 mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    ClearLink(&anode->trigger_edicts);
    ClearLink(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;

        return anode;
    }

    size = maxs - mins;
    if (size.x > size.y) {
        anode->axis = 0;
    } else {
        anode->axis = 1;
    }

    anode->dist = static_cast<float>(0.5 * (maxs[anode->axis] + mins[anode->axis]));
    mins1 = mins;
    mins2 = mins;
    maxs1 = maxs;
    maxs2 = maxs;

    maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

    return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld(void)
{
    SV_InitBoxHull();

    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;
    SV_CreateAreaNode(0, sv.worldmodel->mins, sv.worldmodel->maxs);
}

/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict(edict_t* ent)
{
    if (!ent->area.prev) {
        return; // not linked in anywhere
    }

    RemoveLink(&ent->area);
    ent->area.prev = ent->area.next = NULL;
}

/*
====================
SV_TouchLinks
====================
*/
void SV_TouchLinks(edict_t* ent, areanode_t* node)
{
    link_t *l, *next;
    edict_t* touch;
    int old_self, old_other;

    // touch linked edicts
    for (l = node->trigger_edicts.next; l != &node->trigger_edicts; l = next) {
        next = l->next;
        touch = EDICT_FROM_AREA(l);
        if (touch == ent) {
            continue;
        }

        if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER) {
            continue;
        }

        if (ent->v.absmin.x > touch->v.absmax.x || ent->v.absmin.y > touch->v.absmax.y || ent->v.absmin.z > touch->v.absmax.z || ent->v.absmax.x < touch->v.absmin.x || ent->v.absmax.y < touch->v.absmin.y || ent->v.absmax.z < touch->v.absmin.z) {
            continue;
        }

        old_self = pr_global_struct->self;
        old_other = pr_global_struct->other;

        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(touch));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(ent));
        pr_global_struct->time = static_cast<float>(sv.time);
        PR_ExecuteProgram(touch->v.touch);

        pr_global_struct->self = old_self;
        pr_global_struct->other = old_other;
    }

    // recurse down both sides
    if (node->axis == -1) {
        return;
    }

    if (ent->v.absmax[node->axis] > node->dist) {
        SV_TouchLinks(ent, node->children[0]);
    }

    if (ent->v.absmin[node->axis] < node->dist) {
        SV_TouchLinks(ent, node->children[1]);
    }
}

/*
===============
SV_FindTouchedLeafs

===============
*/
void SV_FindTouchedLeafs(edict_t* ent, mnode_t* node)
{
    mplane_t* splitplane;
    mleaf_t* leaf;
    int sides;
    int leafnum;

    if (node->contents == CONTENTS_SOLID) {
        return;
    }

    // add an efrag if the node is a leaf

    if (node->contents < 0) {
        if (ent->num_leafs == MAX_ENT_LEAFS) {
            return;
        }

        leaf = (mleaf_t*)node;
        leafnum = static_cast<int>(leaf - sv.worldmodel->leafs - 1);

        ent->leafnums[ent->num_leafs] = static_cast<short>(leafnum);
        ent->num_leafs++;

        return;
    }

    // NODE_MIXED

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);

    // recurse down the contacted sides
    if (sides & 1) {
        SV_FindTouchedLeafs(ent, node->children[0]);
    }

    if (sides & 2) {
        SV_FindTouchedLeafs(ent, node->children[1]);
    }
}

/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict(edict_t* ent, qboolean touch_triggers)
{
    areanode_t* node;

    if (ent->area.prev) {
        SV_UnlinkEdict(ent); // unlink from old position
    }

    if (ent == sv.edicts) {
        return; // don't add the world
    }

    if (ent->free) {
        return;
    }

    // set the abs box

    {
        ent->v.absmin = ent->v.origin + ent->v.mins;
        ent->v.absmax = ent->v.origin + ent->v.maxs;
    }

    //
    // to make items easier to pick up and allow them to be grabbed off
    // of shelves, the abs sizes are expanded
    //
    if ((int)ent->v.flags & FL_ITEM) {
        ent->v.absmin.x -= 15;
        ent->v.absmin.y -= 15;
        ent->v.absmax.x += 15;
        ent->v.absmax.y += 15;
    } else { // because movement is clipped an epsilon away from an actual edge,
        // we must fully check even when bounding boxes don't quite touch
        ent->v.absmin.x -= 1;
        ent->v.absmin.y -= 1;
        ent->v.absmin.z -= 1;
        ent->v.absmax.x += 1;
        ent->v.absmax.y += 1;
        ent->v.absmax.z += 1;
    }

    // link to PVS leafs
    ent->num_leafs = 0;
    if (ent->v.modelindex) {
        SV_FindTouchedLeafs(ent, sv.worldmodel->nodes);
    }

    if (ent->v.solid == SOLID_NOT) {
        return;
    }

    // find the first node that the ent's box crosses
    node = sv_areanodes;
    while (1) {
        if (node->axis == -1) {
            break;
        }

        if (ent->v.absmin[node->axis] > node->dist) {
            node = node->children[0];
        } else if (ent->v.absmax[node->axis] < node->dist) {
            node = node->children[1];
        } else {
            break; // crosses the node
        }
    }

    // link it in

    if (ent->v.solid == SOLID_TRIGGER) {
        InsertLinkBefore(&ent->area, &node->trigger_edicts);
    } else {
        InsertLinkBefore(&ent->area, &node->solid_edicts);
    }

    // if touch_triggers, touch all entities at this node and decend for more
    if (touch_triggers) {
        SV_TouchLinks(ent, sv_areanodes);
    }
}

/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

/*
==================
SV_HullPointContents

==================
*/
int SV_HullPointContents(hull_t* hull, int num, const Vector3& p)
{
    float d;
    dclipnode_t* node;
    mplane_t* plane;

    while (num >= 0) {
        if (num < hull->firstclipnode || num > hull->lastclipnode) {
            Sys_Error("SV_HullPointContents: bad node number");
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

/*
==================
SV_PointContents

==================
*/
int SV_PointContents(const Vector3& p)
{
    int cont;

    cont = SV_HullPointContents(&sv.worldmodel->hulls[0], 0, p);
    if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN) {
        cont = CONTENTS_WATER;
    }

    return cont;
}

//===========================================================================

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
============
*/
edict_t* SV_TestEntityPosition(edict_t* ent)
{
    trace_t trace;

    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent);

    if (trace.startsolid) {
        return sv.edicts;
    }

    return NULL;
}

/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define DIST_EPSILON (0.03125)

/*
==================
SV_RecursiveHullCheck

==================
*/
qboolean SV_RecursiveHullCheck(hull_t* hull,
    int num,
    float p1f,
    float p2f,
    const Vector3& p1,
    const Vector3& p2,
    trace_t* trace)
{
    dclipnode_t* node;
    mplane_t* plane;
    float t1, t2;
    float frac;
    Vector3 mid;
    int side;
    float midf;

    // check for empty
    if (num < 0) {
        if (num != CONTENTS_SOLID) {
            trace->allsolid = false;
            if (num == CONTENTS_EMPTY) {
                trace->inopen = true;
            } else {
                trace->inwater = true;
            }
        } else {
            trace->startsolid = true;
        }

        return true; // empty
    }

    if (num < hull->firstclipnode || num > hull->lastclipnode) {
        Sys_Error("SV_RecursiveHullCheck: bad node number");
    }

    //
    // find the point distances
    //
    node = hull->clipnodes + num;
    plane = hull->planes + node->planenum;

    if (plane->type < 3) {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
    } else {
        t1 = plane->normal.dot(p1) - plane->dist;
        t2 = plane->normal.dot(p2) - plane->dist;
    }

#if 1
    if (t1 >= 0 && t2 >= 0) {
        return SV_RecursiveHullCheck(hull, node->children[0], p1f, p2f, p1, p2,
            trace);
    }

    if (t1 < 0 && t2 < 0) {
        return SV_RecursiveHullCheck(hull, node->children[1], p1f, p2f, p1, p2,
            trace);
    }

#else
    if ((t1 >= DIST_EPSILON && t2 >= DIST_EPSILON) || (t2 > t1 && t1 >= 0)) {
        return SV_RecursiveHullCheck(hull, node->children[0], p1f, p2f, p1, p2,
            trace);
    }

    if ((t1 <= -DIST_EPSILON && t2 <= -DIST_EPSILON) || (t2 < t1 && t1 <= 0)) {
        return SV_RecursiveHullCheck(hull, node->children[1], p1f, p2f, p1, p2,
            trace);
    }

#endif

    // put the crosspoint DIST_EPSILON pixels on the near side
    if (t1 < 0) {
        frac = static_cast<float>((t1 + DIST_EPSILON) / (t1 - t2));
    } else {
        frac = static_cast<float>((t1 - DIST_EPSILON) / (t1 - t2));
    }

    if (frac < 0) {
        frac = 0;
    }

    if (frac > 1) {
        frac = 1;
    }

    midf = p1f + (p2f - p1f) * frac;
    mid = p1 + (p2 - p1) * frac;

    side = (t1 < 0);

    // move up to the node
    if (!SV_RecursiveHullCheck(hull, node->children[side], p1f, midf, p1, mid,
            trace)) {
        return false;
    }

#ifdef PARANOID
    if (SV_HullPointContents(sv_hullmodel, mid, node->children[side]) == CONTENTS_SOLID) {
        Con_Printf("mid PointInHullSolid\n");

        return false;
    }

#endif

    if (SV_HullPointContents(hull, node->children[side ^ 1], mid) != CONTENTS_SOLID) {
        // go past the node
        return SV_RecursiveHullCheck(hull, node->children[side ^ 1], midf, p2f, mid,
            p2, trace);
    }

    if (trace->allsolid) {
        return false; // never got out of the solid area
    }

    //==================
    // the other side of the node is solid, this is the impact point
    //==================
    if (!side) {
        trace->plane.normal = plane->normal;
        trace->plane.dist = plane->dist;
    } else {
        trace->plane.normal = -plane->normal;
        trace->plane.dist = -plane->dist;
    }

    while (SV_HullPointContents(hull, hull->firstclipnode, mid) == CONTENTS_SOLID) { // shouldn't really happen, but does occasionally
        frac -= 0.1f;
        if (frac < 0) {
            trace->fraction = midf;
            trace->endpos = mid;
            Con_DPrintf("backup past 0\n");

            return false;
        }

        midf = p1f + (p2f - p1f) * frac;
        mid = p1 + (p2 - p1) * frac;
    }

    trace->fraction = midf;
    trace->endpos = mid;

    return false;
}

/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
trace_t SV_ClipMoveToEntity(edict_t* ent,
    const Vector3& start,
    const Vector3& mins,
    const Vector3& maxs,
    const Vector3& end)
{
    trace_t trace;
    Vector3 offset;
    Vector3 start_l, end_l;
    hull_t* hull;

    // fill in a default trace
    memset(&trace, 0, sizeof(trace_t));
    trace.fraction = 1;
    trace.allsolid = true;
    trace.endpos = end;

    // get the clipping hull
    hull = SV_HullForEntity(ent, mins, maxs, offset);

    start_l = start - offset;
    end_l = end - offset;


    // trace a line through the apropriate clipping hull
    SV_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_l, end_l,
        &trace);


    // fix trace up by the offset
    if (trace.fraction != 1) {
        trace.endpos += offset;
    }

    // did we clip the move?
    if (trace.fraction < 1 || trace.startsolid) {
        trace.ent = ent;
    }

    return trace;
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
void SV_ClipToLinks(areanode_t* node, moveclip_t* clip)
{
    link_t *l, *next;
    edict_t* touch;
    trace_t trace;

    // touch linked edicts
    for (l = node->solid_edicts.next; l != &node->solid_edicts; l = next) {
        next = l->next;
        touch = EDICT_FROM_AREA(l);
        if (touch->v.solid == SOLID_NOT) {
            continue;
        }

        if (touch == clip->passedict) {
            continue;
        }

        if (touch->v.solid == SOLID_TRIGGER) {
            Sys_Error("Trigger in clipping list");
        }

        if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP) {
            continue;
        }

        if (clip->boxmins.x > touch->v.absmax.x || clip->boxmins.y > touch->v.absmax.y || clip->boxmins.z > touch->v.absmax.z || clip->boxmaxs.x < touch->v.absmin.x || clip->boxmaxs.y < touch->v.absmin.y || clip->boxmaxs.z < touch->v.absmin.z) {
            continue;
        }

        if (clip->passedict && clip->passedict->v.size.x && !touch->v.size.x) {
            continue; // points never interact
        }

        // might intersect, so do an exact clip
        if (clip->trace.allsolid) {
            return;
        }

        if (clip->passedict) {
            if (PROG_TO_EDICT(touch->v.owner) == clip->passedict) {
                continue; // don't clip against own missiles
            }

            if (PROG_TO_EDICT(clip->passedict->v.owner) == touch) {
                continue; // don't clip against owner
            }
        }

        if ((int)touch->v.flags & FL_MONSTER) {
            trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins2, clip->maxs2,
                clip->end);
        } else {
            trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins, clip->maxs,
                clip->end);
        }

        if (trace.allsolid || trace.startsolid || trace.fraction < clip->trace.fraction) {
            trace.ent = touch;
            if (clip->trace.startsolid) {
                clip->trace = trace;
                clip->trace.startsolid = true;
            } else {
                clip->trace = trace;
            }
        } else if (trace.startsolid) {
            clip->trace.startsolid = true;
        }
    }

    // recurse down both sides
    if (node->axis == -1) {
        return;
    }

    if (clip->boxmaxs[node->axis] > node->dist) {
        SV_ClipToLinks(node->children[0], clip);
    }

    if (clip->boxmins[node->axis] < node->dist) {
        SV_ClipToLinks(node->children[1], clip);
    }
}

/*
==================
SV_MoveBounds
==================
*/
void SV_MoveBounds(const Vector3& start,
    const Vector3& mins,
    const Vector3& maxs,
    const Vector3& end,
    Vector3& boxmins,
    Vector3& boxmaxs)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (end[i] > start[i]) {
            boxmins[i] = start[i] + mins[i] - 1;
            boxmaxs[i] = end[i] + maxs[i] + 1;
        } else {
            boxmins[i] = end[i] + mins[i] - 1;
            boxmaxs[i] = start[i] + maxs[i] + 1;
        }
    }
}

/*
==================
SV_Move
==================
*/
trace_t SV_Move(const Vector3& start,
    const Vector3& mins,
    const Vector3& maxs,
    const Vector3& end,
    int type,
    edict_t* passedict)
{
    moveclip_t clip{};

    // clip to world
    clip.trace = SV_ClipMoveToEntity(sv.edicts, start, mins, maxs, end);

    clip.start = start;
    clip.end = end;
    clip.mins = mins;
    clip.maxs = maxs;
    clip.type = type;
    clip.passedict = passedict;

    if (type == MOVE_MISSILE) {
        clip.mins2 = Vector3(-15, -15, -15);
        clip.maxs2 = Vector3(15, 15, 15);
    } else {
        clip.mins2 = mins;
        clip.maxs2 = maxs;
    }

    // create the bounding box of the entire move
    SV_MoveBounds(start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs);

    // clip to entities
    SV_ClipToLinks(sv_areanodes, &clip);

    return clip.trace;
}

} // namespace Server
