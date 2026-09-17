// world.cpp -- Server World, Area Nodes, Entity Linking, and Collision Implementation
#include "quakedef.hpp"
#include "server/world.hpp"

namespace Server {

hull_t* SV_HullForEntity(edict_t* ent, const Vector3& mins, const Vector3& maxs, Vector3& offset)
{
    model_t* model;
    Vector3 size, hullmins, hullmaxs;
    hull_t* hull;

    if (ent->v.solid == SOLID_BSP) {
        if (ent->v.movetype != MOVETYPE_PUSH) Common::Sys_Error("SOLID_BSP without MOVETYPE_PUSH");

        model = sv.models[(int)ent->v.modelindex];
        if (!model || model->type != mod_brush) Common::Sys_Error("MOVETYPE_PUSH with a non bsp model");

        size = maxs - mins;
        if (size.x < 3) hull = &model->hulls[0];
        else if (size.x <= 32) hull = &model->hulls[1];
        else hull = &model->hulls[2];

        offset = hull->clip_mins - mins;
        offset += ent->v.origin;
    } else {
        hullmins = ent->v.mins - maxs;
        hullmaxs = ent->v.maxs - mins;
        hull = SV_HullForBox(hullmins, hullmaxs);

        offset = ent->v.origin;
    }

    return hull;
}

typedef struct areanode_s {
    int axis;
    float dist;
    struct areanode_s* children[2];
    link_t trigger_edicts;
    link_t solid_edicts;
} areanode_t;

#define AREA_DEPTH 4
#define AREA_NODES 32

static areanode_t sv_areanodes[AREA_NODES];
static int sv_numareanodes;

static areanode_t* SV_CreateAreaNode(int depth, const Vector3& mins, const Vector3& maxs)
{
    areanode_t* anode;
    Vector3 size, mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes++];
    Common::ClearLink(&anode->trigger_edicts);
    Common::ClearLink(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = nullptr;
        return anode;
    }

    size = maxs - mins;
    if (size.x > size.y) anode->axis = 0;
    else anode->axis = 1;

    anode->dist = static_cast<float>(0.5 * (maxs[anode->axis] + mins[anode->axis]));
    mins1 = mins; mins2 = mins;
    maxs1 = maxs; maxs2 = maxs;

    maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

    return anode;
}

void SV_ClearWorld(void)
{
    SV_InitBoxHull();
    for (auto& node : sv_areanodes) node = areanode_t{};
    sv_numareanodes = 0;
    SV_CreateAreaNode(0, sv.worldmodel->mins, sv.worldmodel->maxs);
}

void SV_UnlinkEdict(edict_t* ent)
{
    if (!ent->area.prev) return;
    Common::RemoveLink(&ent->area);
    ent->area.prev = ent->area.next = nullptr;
}

static void SV_TouchLinks(edict_t* ent, areanode_t* node)
{
    link_t *l, *next;
    edict_t* touch;
    int old_self, old_other;

    for (l = node->trigger_edicts.next; l != &node->trigger_edicts; l = next) {
        next = l->next;
        touch = EDICT_FROM_AREA(l);
        if (touch == ent) continue;
        if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER) continue;

        if (ent->v.absmin.x > touch->v.absmax.x || ent->v.absmin.y > touch->v.absmax.y || ent->v.absmin.z > touch->v.absmax.z ||
            ent->v.absmax.x < touch->v.absmin.x || ent->v.absmax.y < touch->v.absmin.y || ent->v.absmax.z < touch->v.absmin.z) {
            continue;
        }

        old_self = VM::pr_global_struct->self;
        old_other = VM::pr_global_struct->other;

        VM::pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(touch));
        VM::pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(ent));
        VM::pr_global_struct->time = static_cast<float>(sv.time);
        VM::PR_ExecuteProgram(touch->v.touch);

        VM::pr_global_struct->self = old_self;
        VM::pr_global_struct->other = old_other;
    }

    if (node->axis == -1) return;

    if (ent->v.absmax[node->axis] > node->dist) SV_TouchLinks(ent, node->children[0]);
    if (ent->v.absmin[node->axis] < node->dist) SV_TouchLinks(ent, node->children[1]);
}

void SV_FindTouchedLeafs(edict_t* ent, mnode_t* node)
{
    mplane_t* splitplane;
    mleaf_t* leaf;
    int sides, leafnum;

    if (node->contents == CONTENTS_SOLID) return;

    if (node->contents < 0) {
        if (ent->num_leafs == MAX_ENT_LEAFS) return;
        leaf = (mleaf_t*)node;
        leafnum = static_cast<int>(leaf - sv.worldmodel->leafs - 1);
        ent->leafnums[ent->num_leafs++] = static_cast<short>(leafnum);
        return;
    }

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);

    if (sides & 1) SV_FindTouchedLeafs(ent, node->children[0]);
    if (sides & 2) SV_FindTouchedLeafs(ent, node->children[1]);
}

void SV_LinkEdict(edict_t* ent, qboolean touch_triggers)
{
    areanode_t* node;

    if (ent->area.prev) SV_UnlinkEdict(ent);
    if (ent == sv.edicts || ent->free) return;

    ent->v.absmin = ent->v.origin + ent->v.mins;
    ent->v.absmax = ent->v.origin + ent->v.maxs;

    if ((int)ent->v.flags & FL_ITEM) {
        ent->v.absmin.x -= 15; ent->v.absmin.y -= 15;
        ent->v.absmax.x += 15; ent->v.absmax.y += 15;
    } else {
        ent->v.absmin.x -= 1; ent->v.absmin.y -= 1; ent->v.absmin.z -= 1;
        ent->v.absmax.x += 1; ent->v.absmax.y += 1; ent->v.absmax.z += 1;
    }

    ent->num_leafs = 0;
    if (ent->v.modelindex) SV_FindTouchedLeafs(ent, sv.worldmodel->nodes);

    if (ent->v.solid == SOLID_NOT) return;

    node = sv_areanodes;
    while (1) {
        if (node->axis == -1) break;
        if (ent->v.absmin[node->axis] > node->dist) node = node->children[0];
        else if (ent->v.absmax[node->axis] < node->dist) node = node->children[1];
        else break;
    }

    if (ent->v.solid == SOLID_TRIGGER) Common::InsertLinkBefore(&ent->area, &node->trigger_edicts);
    else Common::InsertLinkBefore(&ent->area, &node->solid_edicts);

    if (touch_triggers) SV_TouchLinks(ent, sv_areanodes);
}

int SV_PointContents(const Vector3& p)
{
    int cont = SV_HullPointContents(&sv.worldmodel->hulls[0], 0, p);
    if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN) cont = CONTENTS_WATER;
    return cont;
}

edict_t* SV_TestEntityPosition(edict_t* ent)
{
    trace_t trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent);
    if (trace.startsolid) return sv.edicts;
    return nullptr;
}

trace_t SV_ClipMoveToEntity(edict_t* ent, const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end)
{
    Vector3 offset, start_l, end_l;
    hull_t* hull;
    trace_t trace{};
    trace.fraction = 1.0f;
    trace.allsolid = true;
    trace.endpos = end;

    hull = SV_HullForEntity(ent, mins, maxs, offset);
    start_l = start - offset;
    end_l = end - offset;

    SV_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

    if (trace.fraction != 1) trace.endpos += offset;
    if (trace.fraction < 1 || trace.startsolid) trace.ent = ent;

    return trace;
}

typedef struct {
    Vector3 boxmins, boxmaxs;
    Vector3 mins, maxs;
    Vector3 mins2, maxs2;
    Vector3 start, end;
    trace_t trace;
    int type;
    edict_t* passedict;
} moveclip_t;

static void SV_ClipToLinks(areanode_t* node, moveclip_t* clip)
{
    link_t *l, *next;
    edict_t* touch;
    trace_t trace;

    for (l = node->solid_edicts.next; l != &node->solid_edicts; l = next) {
        next = l->next;
        touch = EDICT_FROM_AREA(l);
        if (touch->v.solid == SOLID_NOT || touch == clip->passedict) continue;
        if (touch->v.solid == SOLID_TRIGGER) Common::Sys_Error("Trigger in clipping list");
        if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP) continue;

        if (clip->boxmins.x > touch->v.absmax.x || clip->boxmins.y > touch->v.absmax.y || clip->boxmins.z > touch->v.absmax.z ||
            clip->boxmaxs.x < touch->v.absmin.x || clip->boxmaxs.y < touch->v.absmin.y || clip->boxmaxs.z < touch->v.absmin.z) {
            continue;
        }

        if (clip->passedict && clip->passedict->v.size.x && !touch->v.size.x) continue;
        if (clip->trace.allsolid) return;

        if (clip->passedict) {
            if (PROG_TO_EDICT(touch->v.owner) == clip->passedict) continue;
            if (PROG_TO_EDICT(clip->passedict->v.owner) == touch) continue;
        }

        if ((int)touch->v.flags & FL_MONSTER) {
            trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins2, clip->maxs2, clip->end);
        } else {
            trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins, clip->maxs, clip->end);
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

    if (node->axis == -1) return;

    if (clip->boxmaxs[node->axis] > node->dist) SV_ClipToLinks(node->children[0], clip);
    if (clip->boxmins[node->axis] < node->dist) SV_ClipToLinks(node->children[1], clip);
}

void SV_MoveBounds(const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end, Vector3& boxmins, Vector3& boxmaxs)
{
    for (int i = 0; i < 3; i++) {
        if (end[i] > start[i]) {
            boxmins[i] = start[i] + mins[i] - 1;
            boxmaxs[i] = end[i] + maxs[i] + 1;
        } else {
            boxmins[i] = end[i] + mins[i] - 1;
            boxmaxs[i] = start[i] + maxs[i] + 1;
        }
    }
}

trace_t SV_Move(const Vector3& start, const Vector3& mins, const Vector3& maxs, const Vector3& end, int type, edict_t* passedict)
{
    moveclip_t clip{};
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

    SV_MoveBounds(start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs);
    SV_ClipToLinks(sv_areanodes, &clip);

    return clip.trace;
}

} // namespace Server
