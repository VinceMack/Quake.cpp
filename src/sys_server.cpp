// sys_server.cpp -- Subsystem Server Implementation
// Contains: server state management, client connections, physics world, collision tracing, entity area movement

#include "quakedef.hpp"

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

namespace VM {
extern cvar_t sv_aim;
void PF_changeyaw();
}

namespace Server {

cvar_t teamplay = { "teamplay", "0", false, true };
cvar_t skill = { "skill", "1" };
cvar_t deathmatch = { "deathmatch", "0" };
cvar_t coop = { "coop", "0" };
cvar_t fraglimit = { "fraglimit", "0", false, true };
cvar_t timelimit = { "timelimit", "0", false, true };

ServerSubsystem& GetServerSubsystem() noexcept
{
    static ServerSubsystem subsystem;
    return subsystem;
}

static eastl::array<eastl::array<char, 5>, MAX_MODELS> localmodels{};
static int fatbytes = 0;
static eastl::array<byte, MAX_MAP_LEAFS / 8> fatpvs{};

cvar_t sv_friction = { "sv_friction", "4", false, true };
cvar_t sv_stopspeed = { "sv_stopspeed", "100" };
cvar_t sv_gravity = { "sv_gravity", "800", false, true };
cvar_t sv_maxvelocity = { "sv_maxvelocity", "2000" };
cvar_t sv_nostep = { "sv_nostep", "0" };

static int c_yes = 0;
static int c_no = 0;

edict_t* sv_player = nullptr;
cvar_t sv_edgefriction = { "edgefriction", "2" };
Vector3 wishdir{};
float wishspeed{0.0f};
float* angles{nullptr};
float* origin{nullptr};
float* velocity{nullptr};
qboolean onground{false};
usercmd_t cmd{};
cvar_t sv_idealpitchscale = { "sv_idealpitchscale", "0.8" };
cvar_t sv_maxspeed = { "sv_maxspeed", "320", false, true };
cvar_t sv_accelerate = { "sv_accelerate", "10" };

typedef struct {
    Vector3 boxmins, boxmaxs;
    Vector3 mins, maxs;
    Vector3 mins2, maxs2;
    Vector3 start, end;
    trace_t trace;
    int type;
    edict_t* passedict;
} moveclip_t;

static hull_t box_hull;
static dclipnode_t box_clipnodes[6];
static mplane_t box_planes[6];

void SV_InitBoxHull(void)
{
    int i, side;
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

hull_t* SV_HullForEntity(edict_t* ent, const Vector3& mins, const Vector3& maxs, Vector3& offset)
{
    model_t* model;
    Vector3 size, hullmins, hullmaxs;
    hull_t* hull;

    if (ent->v.solid == SOLID_BSP) {
        if (ent->v.movetype != MOVETYPE_PUSH) Sys_Error("SOLID_BSP without MOVETYPE_PUSH");

        model = sv.models[(int)ent->v.modelindex];
        if (!model || model->type != mod_brush) Sys_Error("MOVETYPE_PUSH with a non bsp model");

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

areanode_t* SV_CreateAreaNode(int depth, const Vector3& mins, const Vector3& maxs)
{
    areanode_t* anode;
    Vector3 size, mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes++];
    ClearLink(&anode->trigger_edicts);
    ClearLink(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;
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
    RemoveLink(&ent->area);
    ent->area.prev = ent->area.next = NULL;
}

void SV_TouchLinks(edict_t* ent, areanode_t* node)
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

        old_self = pr_global_struct->self;
        old_other = pr_global_struct->other;

        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(touch));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(ent));
        pr_global_struct->time = static_cast<float>(sv.time);
        PR_ExecuteProgram(touch->v.touch);

        pr_global_struct->self = old_self;
        pr_global_struct->other = old_other;
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

    if (ent->v.solid == SOLID_TRIGGER) InsertLinkBefore(&ent->area, &node->trigger_edicts);
    else InsertLinkBefore(&ent->area, &node->solid_edicts);

    if (touch_triggers) SV_TouchLinks(ent, sv_areanodes);
}

int SV_HullPointContents(hull_t* hull, int num, const Vector3& p)
{
    float d;
    dclipnode_t* node;
    mplane_t* plane;

    while (num >= 0) {
        if (num < hull->firstclipnode || num > hull->lastclipnode) Sys_Error("SV_HullPointContents: bad node number");
        node = hull->clipnodes + num;
        plane = hull->planes + node->planenum;

        if (plane->type < 3) d = p[plane->type] - plane->dist;
        else d = plane->normal.dot(p) - plane->dist;

        if (d < 0) num = node->children[1];
        else num = node->children[0];
    }
    return num;
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
    return NULL;
}

#define DIST_EPSILON (0.03125)

qboolean SV_RecursiveHullCheck(hull_t* hull, int num, float p1f, float p2f, const Vector3& p1, const Vector3& p2, trace_t* trace)
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

    if (num < hull->firstclipnode || num > hull->lastclipnode) Sys_Error("SV_RecursiveHullCheck: bad node number");

    node = hull->clipnodes + num;
    plane = hull->planes + node->planenum;

    if (plane->type < 3) {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
    } else {
        t1 = plane->normal.dot(p1) - plane->dist;
        t2 = plane->normal.dot(p2) - plane->dist;
    }

    if (t1 >= 0 && t2 >= 0) return SV_RecursiveHullCheck(hull, node->children[0], p1f, p2f, p1, p2, trace);
    if (t1 < 0 && t2 < 0) return SV_RecursiveHullCheck(hull, node->children[1], p1f, p2f, p1, p2, trace);

    if (t1 < 0) frac = static_cast<float>((t1 + DIST_EPSILON) / (t1 - t2));
    else frac = static_cast<float>((t1 - DIST_EPSILON) / (t1 - t2));

    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    midf = p1f + (p2f - p1f) * frac;
    mid = p1 + (p2 - p1) * frac;
    side = (t1 < 0);

    if (!SV_RecursiveHullCheck(hull, node->children[side], p1f, midf, p1, mid, trace)) return false;

    if (SV_HullPointContents(hull, node->children[side ^ 1], mid) != CONTENTS_SOLID) {
        return SV_RecursiveHullCheck(hull, node->children[side ^ 1], midf, p2f, mid, p2, trace);
    }

    if (trace->allsolid) return false;

    if (!side) {
        trace->plane.normal = plane->normal;
        trace->plane.dist = plane->dist;
    } else {
        trace->plane.normal = -plane->normal;
        trace->plane.dist = -plane->dist;
    }

    while (SV_HullPointContents(hull, hull->firstclipnode, mid) == CONTENTS_SOLID) {
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

void SV_ClipToLinks(areanode_t* node, moveclip_t* clip)
{
    link_t *l, *next;
    edict_t* touch;
    trace_t trace;

    for (l = node->solid_edicts.next; l != &node->solid_edicts; l = next) {
        next = l->next;
        touch = EDICT_FROM_AREA(l);
        if (touch->v.solid == SOLID_NOT || touch == clip->passedict) continue;
        if (touch->v.solid == SOLID_TRIGGER) Sys_Error("Trigger in clipping list");
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

void SV_Init()
{
    Cvar::Register(&sv_maxvelocity);
    Cvar::Register(&sv_gravity);
    Cvar::Register(&sv_friction);
    Cvar::Register(&sv_edgefriction);
    Cvar::Register(&sv_stopspeed);
    Cvar::Register(&sv_maxspeed);
    Cvar::Register(&sv_accelerate);
    Cvar::Register(&sv_idealpitchscale);
    Cvar::Register(&VM::sv_aim);
    Cvar::Register(&sv_nostep);

    for (size_t i = 0; i < MAX_MODELS; ++i) {
        sprintf_s(localmodels[i].data(), localmodels[i].size(), "*%i", static_cast<int>(i));
    }
}

void SV_StartParticle(const Vector3& org, const Vector3& dir, int color, int count)
{
    if (sv.datagram.cursize > MAX_DATAGRAM - 16) return;

    MSG_WriteByte(&sv.datagram, svc_particle);
    MSG_WriteCoord(&sv.datagram, org[0]);
    MSG_WriteCoord(&sv.datagram, org[1]);
    MSG_WriteCoord(&sv.datagram, org[2]);
    for (int i = 0; i < 3; ++i) {
        int v = static_cast<int>(dir[i] * 16.0f);
        if (v > 127) v = 127;
        else if (v < -128) v = -128;
        MSG_WriteChar(&sv.datagram, v);
    }
    MSG_WriteByte(&sv.datagram, count);
    MSG_WriteByte(&sv.datagram, color);
}

void SV_StartSound(edict_t* entity, int channel, const char* sample, int vol, float attenuation)
{
    if (vol < 0 || vol > 255) Sys_Error("SV_StartSound: volume = %i", vol);
    if (attenuation < 0.0f || attenuation > 4.0f) Sys_Error("SV_StartSound: attenuation = %f", static_cast<double>(attenuation));
    if (channel < 0 || channel > 7) Sys_Error("SV_StartSound: channel = %i", channel);
    if (sv.datagram.cursize > MAX_DATAGRAM - 16) return;

    int sound_num = 1;
    for (; sound_num < MAX_SOUNDS && sv.sound_precache[sound_num]; ++sound_num) {
        if (!strcmp(sample, sv.sound_precache[sound_num])) break;
    }

    if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num]) {
        Con_Printf("SV_StartSound: %s not precacheed\n", sample);
        return;
    }

    const int ent = NUM_FOR_EDICT(entity);
    channel = (ent << 3) | channel;

    int field_mask = 0;
    if (vol != DEFAULT_SOUND_PACKET_VOLUME) field_mask |= SND_VOLUME;
    if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION) field_mask |= SND_ATTENUATION;

    MSG_WriteByte(&sv.datagram, svc_sound);
    MSG_WriteByte(&sv.datagram, field_mask);
    if (field_mask & SND_VOLUME) MSG_WriteByte(&sv.datagram, vol);
    if (field_mask & SND_ATTENUATION) MSG_WriteByte(&sv.datagram, static_cast<int>(attenuation * 64.0f));

    MSG_WriteShort(&sv.datagram, channel);
    MSG_WriteByte(&sv.datagram, sound_num);
    const Vector3 center = entity->v.origin + (entity->v.mins + entity->v.maxs) * 0.5f;
    for (int i = 0; i < 3; ++i) MSG_WriteCoord(&sv.datagram, center[i]);
}

void SV_SendServerinfo(client_t* client)
{
    eastl::array<char, 2048> message{};

    MSG_WriteByte(&client->message, svc_print);
    sprintf_s(message.data(), message.size(), "%c\nVERSION %4.2f SERVER (%i CRC)", 2, static_cast<double>(VERSION), pr_crc);
    MSG_WriteString(&client->message, message.data());

    MSG_WriteByte(&client->message, svc_serverinfo);
    MSG_WriteLong(&client->message, PROTOCOL_VERSION);
    MSG_WriteByte(&client->message, svs.maxclients);

    if (!coop.value && deathmatch.value) MSG_WriteByte(&client->message, GAME_DEATHMATCH);
    else MSG_WriteByte(&client->message, GAME_COOP);

    sprintf_s(message.data(), message.size(), "%s", PR_GetString(sv.edicts->v.message));
    MSG_WriteString(&client->message, message.data());

    for (size_t i = 1; i < sv.model_precache.size() && sv.model_precache[i]; ++i) {
        MSG_WriteString(&client->message, sv.model_precache[i]);
    }
    MSG_WriteByte(&client->message, 0);

    for (size_t i = 1; i < sv.sound_precache.size() && sv.sound_precache[i]; ++i) {
        MSG_WriteString(&client->message, sv.sound_precache[i]);
    }
    MSG_WriteByte(&client->message, 0);

    MSG_WriteByte(&client->message, svc_cdtrack);
    MSG_WriteByte(&client->message, static_cast<int>(sv.edicts->v.sounds));
    MSG_WriteByte(&client->message, static_cast<int>(sv.edicts->v.sounds));

    MSG_WriteByte(&client->message, svc_setview);
    MSG_WriteShort(&client->message, NUM_FOR_EDICT(client->edict));

    MSG_WriteByte(&client->message, svc_signonnum);
    MSG_WriteByte(&client->message, 1);

    client->sendsignon = true;
    client->spawned = false;
}

void SV_ConnectClient(int clientnum)
{
    client_t* client = &svs.GetClients()[static_cast<size_t>(clientnum)];
    Con_DPrintf("Client %s connected\n", client->netconnection->address);

    const int edictnum = clientnum + 1;
    edict_t* ent = EDICT_NUM(edictnum);
    qsocket_s* netconnection = client->netconnection;

    eastl::array<float, NUM_SPAWN_PARMS> spawn_parms{};
    if (sv.loadgame) {
        eastl::copy(client->spawn_parms.begin(), client->spawn_parms.end(), spawn_parms.begin());
    }

    client->Reset();
    client->netconnection = netconnection;
    client->SetName("unconnected");
    client->active = true;
    client->spawned = false;
    client->edict = ent;
    client->message.data = client->msgbuf.data();
    client->message.maxsize = static_cast<int>(client->msgbuf.size());
    client->message.allowoverflow = true;
    client->privileged = false;

    if (sv.loadgame) {
        eastl::copy(spawn_parms.begin(), spawn_parms.end(), client->spawn_parms.begin());
    } else {
        PR_ExecuteProgram(pr_global_struct->SetNewParms);
        for (int i = 0; i < NUM_SPAWN_PARMS; ++i) {
            client->spawn_parms[static_cast<size_t>(i)] = (&pr_global_struct->parm1)[i];
        }
    }

    SV_SendServerinfo(client);
}

void SV_CheckForNewClients()
{
    while (true) {
        qsocket_s* ret = NET_CheckNewConnections();
        if (!ret) break;

        auto clients = svs.GetClients();
        auto it = eastl::find_if(clients.begin(), clients.end(), [](const client_t& cl) {
            return !cl.active;
        });

        if (it == clients.end()) Sys_Error("Host_CheckForNewClients: no free clients");

        const int i = static_cast<int>(eastl::distance(clients.begin(), it));
        it->netconnection = ret;
        SV_ConnectClient(i);

        net_activeconnections++;
    }
}

void SV_AddToFatPVS(const Vector3& org, mnode_t* node)
{
    while (true) {
        if (node->contents < 0) {
            if (node->contents != CONTENTS_SOLID) {
                const byte* pvs = Mod_LeafPVS(reinterpret_cast<mleaf_t*>(node), sv.worldmodel);
                for (int i = 0; i < fatbytes; ++i) {
                    fatpvs[static_cast<size_t>(i)] |= pvs[i];
                }
            }
            return;
        }

        mplane_t* plane = node->plane;
        const float d = org.dot(plane->normal) - plane->dist;
        if (d > 8.0f) node = node->children[0];
        else if (d < -8.0f) node = node->children[1];
        else {
            SV_AddToFatPVS(org, node->children[0]);
            node = node->children[1];
        }
    }
}

byte* SV_FatPVS(const Vector3& org)
{
    fatbytes = (sv.worldmodel->numleafs + 31) >> 3;
    eastl::fill_n(fatpvs.begin(), static_cast<size_t>(fatbytes), static_cast<byte>(0));
    SV_AddToFatPVS(org, sv.worldmodel->nodes);
    return fatpvs.data();
}

void SV_WriteEntitiesToClient(edict_t* clent, sizebuf_t* msg)
{
    const Vector3 org = clent->v.origin + clent->v.view_ofs;
    const byte* pvs = SV_FatPVS(org);

    edict_t* ent = NEXT_EDICT(sv.edicts);
    for (int e = 1; e < sv.num_edicts; ++e, ent = NEXT_EDICT(ent)) {
        if (ent != clent) {
            if (!ent->v.modelindex || !*PR_GetString(ent->v.model)) continue;

            int i = 0;
            for (; i < ent->num_leafs; ++i) {
                if (pvs[ent->leafnums[i] >> 3] & (1 << (ent->leafnums[i] & 7))) break;
            }
            if (i == ent->num_leafs) continue;
        }

        if (msg->maxsize - msg->cursize < 16) {
            Con_Printf("packet overflow\n");
            return;
        }

        int bits = 0;
        for (int i = 0; i < 3; ++i) {
            const float miss = ent->v.origin[i] - ent->baseline.origin[i];
            if (miss < -0.1f || miss > 0.1f) bits |= U_ORIGIN1 << i;
        }

        if (ent->v.angles[0] != ent->baseline.angles[0]) bits |= U_ANGLE1;
        if (ent->v.angles[1] != ent->baseline.angles[1]) bits |= U_ANGLE2;
        if (ent->v.angles[2] != ent->baseline.angles[2]) bits |= U_ANGLE3;

        if (ent->v.movetype == MOVETYPE_STEP) bits |= U_NOLERP;
        if (ent->baseline.colormap != ent->v.colormap) bits |= U_COLORMAP;
        if (ent->baseline.skin != ent->v.skin) bits |= U_SKIN;
        if (ent->baseline.frame != ent->v.frame) bits |= U_FRAME;
        if (ent->baseline.effects != ent->v.effects) bits |= U_EFFECTS;
        if (ent->baseline.modelindex != ent->v.modelindex) bits |= U_MODEL;
        if (e >= 256) bits |= U_LONGENTITY;
        if (bits >= 256) bits |= U_MOREBITS;

        MSG_WriteByte(msg, bits | U_SIGNAL);
        if (bits & U_MOREBITS) MSG_WriteByte(msg, bits >> 8);
        if (bits & U_LONGENTITY) MSG_WriteShort(msg, e);
        else MSG_WriteByte(msg, e);

        if (bits & U_MODEL) MSG_WriteByte(msg, static_cast<int>(ent->v.modelindex));
        if (bits & U_FRAME) MSG_WriteByte(msg, static_cast<int>(ent->v.frame));
        if (bits & U_COLORMAP) MSG_WriteByte(msg, static_cast<int>(ent->v.colormap));
        if (bits & U_SKIN) MSG_WriteByte(msg, static_cast<int>(ent->v.skin));
        if (bits & U_EFFECTS) MSG_WriteByte(msg, static_cast<int>(ent->v.effects));
        if (bits & U_ORIGIN1) MSG_WriteCoord(msg, ent->v.origin[0]);
        if (bits & U_ANGLE1) MSG_WriteAngle(msg, ent->v.angles[0]);
        if (bits & U_ORIGIN2) MSG_WriteCoord(msg, ent->v.origin[1]);
        if (bits & U_ANGLE2) MSG_WriteAngle(msg, ent->v.angles[1]);
        if (bits & U_ORIGIN3) MSG_WriteCoord(msg, ent->v.origin[2]);
        if (bits & U_ANGLE3) MSG_WriteAngle(msg, ent->v.angles[2]);
    }
}

void SV_CleanupEnts()
{
    edict_t* ent = NEXT_EDICT(sv.edicts);
    for (int e = 1; e < sv.num_edicts; ++e, ent = NEXT_EDICT(ent)) {
        ent->v.effects = static_cast<float>(static_cast<int>(ent->v.effects) & ~EF_MUZZLEFLASH);
    }
}

void SV_WriteClientdataToMessage(edict_t* ent, sizebuf_t* msg)
{
    if (ent->v.dmg_take || ent->v.dmg_save) {
        const edict_t* other = PROG_TO_EDICT(ent->v.dmg_inflictor);
        MSG_WriteByte(msg, svc_damage);
        MSG_WriteByte(msg, static_cast<int>(ent->v.dmg_save));
        MSG_WriteByte(msg, static_cast<int>(ent->v.dmg_take));
        const Vector3 center = other->v.origin + (other->v.mins + other->v.maxs) * 0.5f;
        for (int i = 0; i < 3; ++i) MSG_WriteCoord(msg, center[i]);
        ent->v.dmg_take = 0; ent->v.dmg_save = 0;
    }

    SV_SetIdealPitch();

    if (ent->v.fixangle) {
        MSG_WriteByte(msg, svc_setangle);
        for (int i = 0; i < 3; ++i) MSG_WriteAngle(msg, ent->v.angles[i]);
        ent->v.fixangle = 0;
    }

    int bits = 0;
    if (ent->v.view_ofs[2] != DEFAULT_VIEWHEIGHT) bits |= SU_VIEWHEIGHT;
    if (ent->v.idealpitch) bits |= SU_IDEALPITCH;

    const eval_t* val = GetEdictFieldValue(ent, "items2");
    int items = val ? (static_cast<int>(ent->v.items) | (static_cast<int>(val->_float) << 23))
                    : (static_cast<int>(ent->v.items) | (static_cast<int>(pr_global_struct->serverflags) << 28));

    bits |= SU_ITEMS;
    if (static_cast<int>(ent->v.flags) & FL_ONGROUND) bits |= SU_ONGROUND;
    if (ent->v.waterlevel >= 2) bits |= SU_INWATER;

    for (int i = 0; i < 3; ++i) {
        if (ent->v.punchangle[i]) bits |= (SU_PUNCH1 << i);
        if (ent->v.velocity[i]) bits |= (SU_VELOCITY1 << i);
    }

    if (ent->v.weaponframe) bits |= SU_WEAPONFRAME;
    if (ent->v.armorvalue) bits |= SU_ARMOR;
    bits |= SU_WEAPON;

    MSG_WriteByte(msg, svc_clientdata);
    MSG_WriteShort(msg, bits);

    if (bits & SU_VIEWHEIGHT) MSG_WriteChar(msg, static_cast<int>(ent->v.view_ofs[2]));
    if (bits & SU_IDEALPITCH) MSG_WriteChar(msg, static_cast<int>(ent->v.idealpitch));

    for (int i = 0; i < 3; ++i) {
        if (bits & (SU_PUNCH1 << i)) MSG_WriteChar(msg, static_cast<int>(ent->v.punchangle[i]));
        if (bits & (SU_VELOCITY1 << i)) MSG_WriteChar(msg, static_cast<int>(ent->v.velocity[i] / 16.0f));
    }

    MSG_WriteLong(msg, items);

    if (bits & SU_WEAPONFRAME) MSG_WriteByte(msg, static_cast<int>(ent->v.weaponframe));
    if (bits & SU_ARMOR) MSG_WriteByte(msg, static_cast<int>(ent->v.armorvalue));
    if (bits & SU_WEAPON) MSG_WriteByte(msg, SV_ModelIndex(PR_GetString(ent->v.weaponmodel)));

    MSG_WriteShort(msg, static_cast<int>(ent->v.health));
    MSG_WriteByte(msg, static_cast<int>(ent->v.currentammo));
    MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_shells));
    MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_nails));
    MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_rockets));
    MSG_WriteByte(msg, static_cast<int>(ent->v.ammo_cells));

    if (standard_quake) {
        MSG_WriteByte(msg, static_cast<int>(ent->v.weapon));
    } else {
        for (int i = 0; i < 32; ++i) {
            if (static_cast<int>(ent->v.weapon) & (1 << i)) {
                MSG_WriteByte(msg, i);
                break;
            }
        }
    }
}

qboolean SV_SendClientDatagram(client_t* client)
{
    eastl::array<byte, MAX_DATAGRAM> buf{};
    sizebuf_t msg{};

    msg.data = buf.data();
    msg.maxsize = static_cast<int>(buf.size());
    msg.cursize = 0;

    MSG_WriteByte(&msg, svc_time);
    MSG_WriteFloat(&msg, static_cast<float>(sv.time));

    SV_WriteClientdataToMessage(client->edict, &msg);
    SV_WriteEntitiesToClient(client->edict, &msg);

    if (msg.cursize + sv.datagram.cursize < msg.maxsize) {
        SZ_Write(&msg, sv.datagram.data, sv.datagram.cursize);
    }

    if (NET_SendUnreliableMessage(client->netconnection, &msg) == -1) {
        SV_DropClient(true);
        return false;
    }

    return true;
}

void SV_UpdateToReliableMessages()
{
    auto clients = svs.GetClients();
    for (size_t i = 0; i < clients.size(); ++i) {
        host_client = &clients[i];
        if (host_client->old_frags != static_cast<int>(host_client->edict->v.frags)) {
            for (auto& client : clients) {
                if (!client.active) continue;
                MSG_WriteByte(&client.message, svc_updatefrags);
                MSG_WriteByte(&client.message, static_cast<int>(i));
                MSG_WriteShort(&client.message, static_cast<int>(host_client->edict->v.frags));
            }
            host_client->old_frags = static_cast<int>(host_client->edict->v.frags);
        }
    }

    for (auto& client : clients) {
        if (!client.active) continue;
        SZ_Write(&client.message, sv.reliable_datagram.data, sv.reliable_datagram.cursize);
    }

    SZ_Clear(&sv.reliable_datagram);
}

void SV_SendNop(client_t* client)
{
    eastl::array<byte, 4> buf{};
    sizebuf_t msg{};

    msg.data = buf.data();
    msg.maxsize = static_cast<int>(buf.size());
    msg.cursize = 0;

    MSG_WriteChar(&msg, svc_nop);

    if (NET_SendUnreliableMessage(client->netconnection, &msg) == -1) {
        SV_DropClient(true);
    }

    client->last_message = realtime;
}

void SV_SendClientMessages()
{
    SV_UpdateToReliableMessages();

    auto clients = svs.GetClients();
    for (auto& client : clients) {
        host_client = &client;
        if (!host_client->active) continue;

        if (host_client->spawned) {
            if (!SV_SendClientDatagram(host_client)) continue;
        } else {
            if (!host_client->sendsignon) {
                if (realtime - host_client->last_message > 5.0) SV_SendNop(host_client);
                continue;
            }
        }

        if (host_client->message.overflowed) {
            SV_DropClient(true);
            host_client->message.overflowed = false;
            continue;
        }

        if (host_client->message.cursize || host_client->dropasap) {
            if (!NET_CanSendMessage(host_client->netconnection)) continue;

            if (host_client->dropasap) {
                SV_DropClient(false);
            } else {
                if (NET_SendMessage(host_client->netconnection, &host_client->message) == -1) {
                    SV_DropClient(true);
                }

                SZ_Clear(&host_client->message);
                host_client->last_message = realtime;
                host_client->sendsignon = false;
            }
        }
    }

    SV_CleanupEnts();
}

int SV_ModelIndex(const char* name)
{
    if (!name || !name[0]) return 0;

    for (size_t i = 0; i < sv.model_precache.size() && sv.model_precache[i]; ++i) {
        if (!strcmp(sv.model_precache[i], name)) return static_cast<int>(i);
    }

    Sys_Error("SV_ModelIndex: model %s not precached", name);
    return 0;
}

void SV_CreateBaseline()
{
    for (int entnum = 0; entnum < sv.num_edicts; ++entnum) {
        edict_t* svent = EDICT_NUM(entnum);
        if (svent->free) continue;
        if (entnum > svs.maxclients && !svent->v.modelindex) continue;

        VectorCopy(svent->v.origin, svent->baseline.origin);
        VectorCopy(svent->v.angles, svent->baseline.angles);
        svent->baseline.frame = static_cast<int>(svent->v.frame);
        svent->baseline.skin = static_cast<int>(svent->v.skin);
        if (entnum > 0 && entnum <= svs.maxclients) {
            svent->baseline.colormap = entnum;
            svent->baseline.modelindex = SV_ModelIndex("progs/player.mdl");
        } else {
            svent->baseline.colormap = 0;
            svent->baseline.modelindex = SV_ModelIndex(PR_GetString(svent->v.model));
        }

        MSG_WriteByte(&sv.signon, svc_spawnbaseline);
        MSG_WriteShort(&sv.signon, entnum);
        MSG_WriteByte(&sv.signon, svent->baseline.modelindex);
        MSG_WriteByte(&sv.signon, svent->baseline.frame);
        MSG_WriteByte(&sv.signon, svent->baseline.colormap);
        MSG_WriteByte(&sv.signon, svent->baseline.skin);
        for (int i = 0; i < 3; ++i) {
            MSG_WriteCoord(&sv.signon, svent->baseline.origin[i]);
            MSG_WriteAngle(&sv.signon, svent->baseline.angles[i]);
        }
    }
}

void SV_SendReconnect()
{
    eastl::array<char, 128> data{};
    sizebuf_t msg{};

    msg.data = reinterpret_cast<byte*>(data.data());
    msg.cursize = 0;
    msg.maxsize = static_cast<int>(data.size());

    MSG_WriteChar(&msg, svc_stufftext);
    MSG_WriteString(&msg, "reconnect\n");
    NET_SendToAll(&msg, 5);

    if (cls.state != ca_dedicated) Cmd::ExecuteString("reconnect\n", Cmd::Source::Command);
}

void SV_SaveSpawnparms()
{
    svs.serverflags = static_cast<int>(pr_global_struct->serverflags);

    for (auto& client : svs.GetClients()) {
        host_client = &client;
        if (!host_client->active) continue;

        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(host_client->edict));
        PR_ExecuteProgram(pr_global_struct->SetChangeParms);
        for (int j = 0; j < NUM_SPAWN_PARMS; ++j) {
            host_client->spawn_parms[static_cast<size_t>(j)] = (&pr_global_struct->parm1)[j];
        }
    }
}

void SV_SpawnServer(const char* server)
{
    if (hostname.string.empty()) Cvar::Set("hostname", "UNNAMED");

    Screen::GetScreenSystem().SetCentertimeOff(0.0f);
    Con_DPrintf("SpawnServer: %s\n", server);
    svs.changelevel_issued = false;

    if (sv.active) SV_SendReconnect();

    if (coop.value) Cvar::SetValue("deathmatch", 0);

    current_skill = static_cast<int>(skill.value + 0.5f);
    if (current_skill < 0) current_skill = 0;
    if (current_skill > 3) current_skill = 3;
    Cvar::SetValue("skill", static_cast<float>(current_skill));

    Host_ClearMemory();

    sv = server_t{};
    sv.SetName(server);

    PR_LoadProgs();

    sv.max_edicts = MAX_EDICTS;
    sv.edicts = reinterpret_cast<edict_t*>(Hunk_Alloc(sv.max_edicts * pr_edict_size, "edicts"));

    sv.datagram.maxsize = static_cast<int>(sv.datagram_buf.size());
    sv.datagram.cursize = 0;
    sv.datagram.data = sv.datagram_buf.data();

    sv.reliable_datagram.maxsize = static_cast<int>(sv.reliable_datagram_buf.size());
    sv.reliable_datagram.cursize = 0;
    sv.reliable_datagram.data = sv.reliable_datagram_buf.data();

    sv.signon.maxsize = static_cast<int>(sv.signon_buf.size());
    sv.signon.cursize = 0;
    sv.signon.data = sv.signon_buf.data();

    sv.num_edicts = svs.maxclients + 1;
    auto clients = svs.GetClients();
    for (size_t i = 0; i < clients.size(); ++i) {
        edict_t* ent = EDICT_NUM(static_cast<int>(i) + 1);
        clients[i].edict = ent;
    }

    sv.state = ss_loading;
    sv.paused = false;

    sprintf_s(sv.modelname.data(), sv.modelname.size(), "maps/%s.bsp", server);
    sv.worldmodel = Mod_ForName(sv.modelname.data(), false);
    if (!sv.worldmodel) {
        Con_Printf("Couldn't spawn server %s\n", sv.modelname.data());
        sv.active = false;
        return;
    }

    sv.models[1] = sv.worldmodel;

    SV_ClearWorld();

    sv.sound_precache[0] = pr_strings;
    sv.model_precache[0] = pr_strings;
    sv.model_precache[1] = sv.modelname.data();
    for (int i = 1; i < sv.worldmodel->numsubmodels; ++i) {
        sv.model_precache[1 + i] = localmodels[static_cast<size_t>(i)].data();
        sv.models[1 + i] = Mod_ForName(localmodels[static_cast<size_t>(i)].data(), false);
    }

    edict_t* ent = EDICT_NUM(0);
    memset(&ent->v, 0, progs->entityfields * 4);
    ent->free = false;
    ent->v.model = PR_SetString(sv.worldmodel->name);
    ent->v.modelindex = 1;
    ent->v.solid = SOLID_BSP;
    ent->v.movetype = MOVETYPE_PUSH;

    if (coop.value) pr_global_struct->coop = coop.value;
    else pr_global_struct->deathmatch = deathmatch.value;

    pr_global_struct->mapname = PR_SetString(sv.name.data());
    pr_global_struct->serverflags = static_cast<float>(svs.serverflags);

    ED_LoadFromFile(sv.worldmodel->entities);

    sv.active = true;
    sv.state = ss_active;

    host_frametime = 0.1;
    SV_Physics();
    SV_Physics();

    SV_CreateBaseline();

    for (auto& client : clients) {
        host_client = &client;
        if (host_client->active) SV_SendServerinfo(host_client);
    }

    Con_DPrintf("Server spawned.\n");
}

#define MOVE_EPSILON 0.01

void SV_Physics_Toss(edict_t* ent);

void SV_CheckVelocity(edict_t* ent)
{
    for (int i = 0; i < 3; ++i) {
        if (IS_NAN(ent->v.velocity[i])) {
            Con_Printf("Got a NaN velocity on %s\n", PR_GetString(ent->v.classname));
            ent->v.velocity[i] = 0.0f;
        }

        if (IS_NAN(ent->v.origin[i])) {
            Con_Printf("Got a NaN origin on %s\n", PR_GetString(ent->v.classname));
            ent->v.origin[i] = 0.0f;
        }

        if (ent->v.velocity[i] > sv_maxvelocity.value) ent->v.velocity[i] = sv_maxvelocity.value;
        else if (ent->v.velocity[i] < -sv_maxvelocity.value) ent->v.velocity[i] = -sv_maxvelocity.value;
    }
}

qboolean SV_RunThink(edict_t* ent)
{
    float thinktime = ent->v.nextthink;
    if (thinktime <= 0.0f || thinktime > sv.time + host_frametime) return true;

    if (thinktime < sv.time) thinktime = static_cast<float>(sv.time);

    ent->v.nextthink = 0;
    pr_global_struct->time = thinktime;
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
    pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    PR_ExecuteProgram(ent->v.think);

    return !ent->free;
}

void SV_Impact(edict_t* e1, edict_t* e2)
{
    const int old_self = pr_global_struct->self;
    const int old_other = pr_global_struct->other;

    pr_global_struct->time = static_cast<float>(sv.time);
    if (e1->v.touch && e1->v.solid != SOLID_NOT) {
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(e1));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(e2));
        PR_ExecuteProgram(e1->v.touch);
    }

    if (e2->v.touch && e2->v.solid != SOLID_NOT) {
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(e2));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(e1));
        PR_ExecuteProgram(e2->v.touch);
    }

    pr_global_struct->self = old_self;
    pr_global_struct->other = old_other;
}

constexpr float STOP_EPSILON = 0.1f;

int ClipVelocity(const Vector3& in, const Vector3& normal, Vector3& out, float overbounce)
{
    int blocked = 0;
    if (normal.z > 0.0f) blocked |= 1;
    if (normal.z == 0.0f) blocked |= 2;

    const float backoff = in.dot(normal) * overbounce;
    out = in - normal * backoff;
    for (int i = 0; i < 3; ++i) {
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) out[i] = 0.0f;
    }

    return blocked;
}

constexpr int MAX_CLIP_PLANES = 5;

int SV_FlyMove(edict_t* ent, float time, trace_t* steptrace)
{
    constexpr int numbumps = 4;
    int blocked = 0;
    Vector3 original_velocity = ent->v.velocity;
    Vector3 primal_velocity = ent->v.velocity;
    int numplanes = 0;
    eastl::array<Vector3, MAX_CLIP_PLANES> planes{};

    float time_left = time;

    for (int bumpcount = 0; bumpcount < numbumps; ++bumpcount) {
        if (ent->v.velocity == vec3_origin) break;

        const Vector3 end = ent->v.origin + ent->v.velocity * time_left;
        trace_t trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

        if (trace.allsolid) {
            ent->v.velocity = vec3_origin;
            return 3;
        }

        if (trace.fraction > 0.0f) {
            ent->v.origin = trace.endpos;
            original_velocity = ent->v.velocity;
            numplanes = 0;
        }

        if (trace.fraction == 1.0f) break;
        if (!trace.ent) Sys_Error("SV_FlyMove: !trace.ent");

        if (trace.plane.normal.z > 0.7f) {
            blocked |= 1;
            if (trace.ent->v.solid == SOLID_BSP) {
                ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
                ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));
            }
        }

        if (trace.plane.normal.z == 0.0f) {
            blocked |= 2;
            if (steptrace) *steptrace = trace;
        }

        SV_Impact(ent, trace.ent);
        if (ent->free) break;

        time_left -= time_left * trace.fraction;

        if (numplanes >= MAX_CLIP_PLANES) {
            ent->v.velocity = vec3_origin;
            return 3;
        }

        planes[static_cast<size_t>(numplanes++)] = trace.plane.normal;

        int i = 0;
        Vector3 new_velocity{};
        for (; i < numplanes; ++i) {
            ClipVelocity(original_velocity, planes[static_cast<size_t>(i)], new_velocity, 1.0f);
            int j = 0;
            for (; j < numplanes; ++j) {
                if (j != i) {
                    if (new_velocity.dot(planes[static_cast<size_t>(j)]) < 0.0f) break;
                }
            }
            if (j == numplanes) break;
        }

        if (i != numplanes) {
            ent->v.velocity = new_velocity;
        } else {
            if (numplanes != 2) {
                ent->v.velocity = vec3_origin;
                return 7;
            }

            const Vector3 dir = planes[0].cross(planes[1]);
            const float d = dir.dot(ent->v.velocity);
            ent->v.velocity = dir * d;
        }

        if (ent->v.velocity.dot(primal_velocity) <= 0.0f) {
            ent->v.velocity = vec3_origin;
            return blocked;
        }
    }

    return blocked;
}

void SV_AddGravity(edict_t* ent)
{
    float ent_gravity = 1.0f;
    const eval_t* val = GetEdictFieldValue(ent, "gravity");
    if (val && val->_float) ent_gravity = val->_float;

    ent->v.velocity[2] -= static_cast<float>(ent_gravity * sv_gravity.value * host_frametime);
}

trace_t SV_PushEntity(edict_t* ent, const Vector3& push)
{
    const Vector3 end = ent->v.origin + push;
    trace_t trace{};

    if (ent->v.movetype == MOVETYPE_FLYMISSILE) {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
    } else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT) {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
    } else {
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
    }

    ent->v.origin = trace.endpos;
    SV_LinkEdict(ent, true);

    if (trace.ent) SV_Impact(ent, trace.ent);

    return trace;
}

void SV_PushMove(edict_t* pusher, float movetime)
{
    if (pusher->v.velocity == vec3_origin) {
        pusher->v.ltime += movetime;
        return;
    }

    const Vector3 move = pusher->v.velocity * movetime;
    const Vector3 mins = pusher->v.absmin + move;
    const Vector3 maxs = pusher->v.absmax + move;
    const Vector3 pushorig = pusher->v.origin;

    pusher->v.origin += move;
    pusher->v.ltime += movetime;
    SV_LinkEdict(pusher, false);

    int num_moved = 0;
    eastl::array<edict_t*, MAX_EDICTS> moved_edict{};
    eastl::array<Vector3, MAX_EDICTS> moved_from{};

    edict_t* check = NEXT_EDICT(sv.edicts);
    for (int e = 1; e < sv.num_edicts; ++e, check = NEXT_EDICT(check)) {
        if (check->free) continue;
        if (check->v.movetype == MOVETYPE_PUSH || check->v.movetype == MOVETYPE_NONE || check->v.movetype == MOVETYPE_NOCLIP) continue;

        if (!((static_cast<int>(check->v.flags) & FL_ONGROUND) && PROG_TO_EDICT(check->v.groundentity) == pusher)) {
            if (check->v.absmin.x >= maxs.x || check->v.absmin.y >= maxs.y || check->v.absmin.z >= maxs.z ||
                check->v.absmax.x <= mins.x || check->v.absmax.y <= mins.y || check->v.absmax.z <= mins.z) {
                continue;
            }
            if (!SV_TestEntityPosition(check)) continue;
        }

        if (check->v.movetype != MOVETYPE_WALK) {
            check->v.flags = static_cast<float>(static_cast<int>(check->v.flags) & ~FL_ONGROUND);
        }

        const Vector3 entorig = check->v.origin;
        moved_from[static_cast<size_t>(num_moved)] = check->v.origin;
        moved_edict[static_cast<size_t>(num_moved)] = check;
        num_moved++;

        pusher->v.solid = SOLID_NOT;
        SV_PushEntity(check, move);
        pusher->v.solid = SOLID_BSP;

        edict_t* block = SV_TestEntityPosition(check);
        if (block) {
            if (check->v.mins.x == check->v.maxs.x) continue;
            if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER) {
                check->v.mins.x = check->v.mins.y = 0;
                check->v.maxs = check->v.mins;
                continue;
            }

            check->v.origin = entorig;
            SV_LinkEdict(check, true);

            pusher->v.origin = pushorig;
            SV_LinkEdict(pusher, false);
            pusher->v.ltime -= movetime;

            if (pusher->v.blocked) {
                pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(pusher));
                pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(check));
                PR_ExecuteProgram(pusher->v.blocked);
            }

            for (int i = 0; i < num_moved; ++i) {
                moved_edict[static_cast<size_t>(i)]->v.origin = moved_from[static_cast<size_t>(i)];
                SV_LinkEdict(moved_edict[static_cast<size_t>(i)], false);
            }
            return;
        }
    }
}

void SV_Physics_Pusher(edict_t* ent)
{
    float thinktime = ent->v.nextthink;
    float oldltime = ent->v.ltime;
    float movetime;

    if (thinktime < ent->v.ltime + host_frametime) {
        movetime = thinktime - ent->v.ltime;
        if (movetime < 0) movetime = 0;
    } else {
        movetime = static_cast<float>(host_frametime);
    }

    if (movetime) SV_PushMove(ent, movetime);

    if (thinktime > oldltime && thinktime <= ent->v.ltime) {
        ent->v.nextthink = 0;
        pr_global_struct->time = static_cast<float>(sv.time);
        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
        pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(sv.edicts));
        PR_ExecuteProgram(ent->v.think);
        if (ent->free) return;
    }
}

void SV_CheckStuck(edict_t* ent)
{
    int i, j, z;
    Vector3 org;

    if (!SV_TestEntityPosition(ent)) {
        ent->v.oldorigin = ent->v.origin;
        return;
    }

    org = ent->v.origin;
    ent->v.origin = ent->v.oldorigin;
    if (!SV_TestEntityPosition(ent)) {
        Con_DPrintf("Unstuck.\n");
        SV_LinkEdict(ent, true);
        return;
    }

    for (z = 0; z < 18; z++) {
        for (i = -1; i <= 1; i++) {
            for (j = -1; j <= 1; j++) {
                ent->v.origin.x = org.x + i;
                ent->v.origin.y = org.y + j;
                ent->v.origin.z = org.z + z;
                if (!SV_TestEntityPosition(ent)) {
                    Con_DPrintf("Unstuck.\n");
                    SV_LinkEdict(ent, true);
                    return;
                }
            }
        }
    }

    ent->v.origin = org;
    Con_DPrintf("player is stuck.\n");
}

qboolean SV_CheckWater(edict_t* ent)
{
    Vector3 point(ent->v.origin.x, ent->v.origin.y, ent->v.origin.z + ent->v.mins.z + 1);

    ent->v.waterlevel = 0;
    ent->v.watertype = CONTENTS_EMPTY;
    int cont = SV_PointContents(point);
    if (cont <= CONTENTS_WATER) {
        ent->v.watertype = static_cast<float>(cont);
        ent->v.waterlevel = 1;
        point.z = ent->v.origin.z + (ent->v.mins.z + ent->v.maxs.z) * 0.5f;
        cont = SV_PointContents(point);
        if (cont <= CONTENTS_WATER) {
            ent->v.waterlevel = 2;
            point.z = ent->v.origin.z + ent->v.view_ofs.z;
            cont = SV_PointContents(point);
            if (cont <= CONTENTS_WATER) {
                ent->v.waterlevel = 3;
            }
        }
    }

    return ent->v.waterlevel > 1;
}

void SV_WallFriction(edict_t* ent, trace_t* trace)
{
    Vector3 forward, right, up, into, side;
    float d, i;

    AngleVectors(ent->v.v_angle, forward, right, up);
    d = trace->plane.normal.dot(forward);

    d += 0.5;
    if (d >= 0) return;

    i = trace->plane.normal.dot(ent->v.velocity);
    into = trace->plane.normal * i;
    side = ent->v.velocity - into;

    ent->v.velocity.x = side.x * (1 + d);
    ent->v.velocity.y = side.y * (1 + d);
}

int SV_TryUnstick(edict_t* ent, const Vector3& oldvel)
{
    int i, clip;
    Vector3 oldorg, dir;
    trace_t steptrace;

    oldorg = ent->v.origin;
    dir = vec3_origin;

    for (i = 0; i < 8; i++) {
        switch (i) {
        case 0: dir.x = 2; dir.y = 0; break;
        case 1: dir.x = 0; dir.y = 2; break;
        case 2: dir.x = -2; dir.y = 0; break;
        case 3: dir.x = 0; dir.y = -2; break;
        case 4: dir.x = 2; dir.y = 2; break;
        case 5: dir.x = -2; dir.y = 2; break;
        case 6: dir.x = 2; dir.y = -2; break;
        case 7: dir.x = -2; dir.y = -2; break;
        }

        SV_PushEntity(ent, dir);
        ent->v.velocity = Vector3(oldvel.x, oldvel.y, 0.0f);
        clip = SV_FlyMove(ent, 0.1f, &steptrace);

        if (fabs(oldorg.y - ent->v.origin.y) > 4 || fabs(oldorg.x - ent->v.origin.x) > 4) {
            return clip;
        }

        ent->v.origin = oldorg;
    }

    ent->v.velocity = vec3_origin;
    return 7;
}

#define STEPSIZE 18

void SV_WalkMove(edict_t* ent)
{
    Vector3 upmove, downmove, oldorg, oldvel, nosteporg, nostepvel;
    int clip, oldonground;
    trace_t steptrace, downtrace;

    oldonground = static_cast<int>(ent->v.flags) & FL_ONGROUND;
    ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) & ~FL_ONGROUND);

    oldorg = ent->v.origin;
    oldvel = ent->v.velocity;

    clip = SV_FlyMove(ent, static_cast<float>(host_frametime), &steptrace);

    if (!(clip & 2)) return;
    if (!oldonground && ent->v.waterlevel == 0) return;
    if (ent->v.movetype != MOVETYPE_WALK) return;
    if (sv_nostep.value) return;
    if (static_cast<int>(sv_player->v.flags) & FL_WATERJUMP) return;

    nosteporg = ent->v.origin;
    nostepvel = ent->v.velocity;

    ent->v.origin = oldorg;

    upmove = vec3_origin;
    downmove = vec3_origin;
    upmove.z = STEPSIZE;
    downmove.z = static_cast<float>(-STEPSIZE + oldvel.z * host_frametime);

    SV_PushEntity(ent, upmove);

    ent->v.velocity = Vector3(oldvel.x, oldvel.y, 0.0f);
    clip = SV_FlyMove(ent, static_cast<float>(host_frametime), &steptrace);

    if (clip) {
        if (fabs(oldorg.y - ent->v.origin.y) < 0.03125 && fabs(oldorg.x - ent->v.origin.x) < 0.03125) {
            clip = SV_TryUnstick(ent, oldvel);
        }
    }

    if (clip & 2) SV_WallFriction(ent, &steptrace);

    downtrace = SV_PushEntity(ent, downmove);

    if (downtrace.plane.normal.z > 0.7) {
        if (ent->v.solid == SOLID_BSP) {
            ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
            ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(downtrace.ent));
        }
    } else {
        ent->v.origin = nosteporg;
        ent->v.velocity = nostepvel;
    }
}

void SV_Physics_Client(edict_t* ent, int num)
{
    if (!svs.GetClients()[static_cast<size_t>(num - 1)].active) return;

    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
    PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

    SV_CheckVelocity(ent);

    switch (static_cast<int>(ent->v.movetype)) {
    case MOVETYPE_NONE:
        if (!SV_RunThink(ent)) return;
        break;

    case MOVETYPE_WALK:
        if (!SV_RunThink(ent)) return;
        if (!SV_CheckWater(ent) && !(static_cast<int>(ent->v.flags) & FL_WATERJUMP)) {
            SV_AddGravity(ent);
        }
        SV_CheckStuck(ent);
        SV_WalkMove(ent);
        break;

    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
        SV_Physics_Toss(ent);
        break;

    case MOVETYPE_FLY:
        if (!SV_RunThink(ent)) return;
        SV_FlyMove(ent, static_cast<float>(host_frametime), nullptr);
        break;

    case MOVETYPE_NOCLIP:
        if (!SV_RunThink(ent)) return;
        VectorMA(ent->v.origin, static_cast<float>(host_frametime), ent->v.velocity, ent->v.origin);
        break;

    default:
        Sys_Error("SV_Physics_client: bad movetype %i", static_cast<int>(ent->v.movetype));
    }

    SV_LinkEdict(ent, true);
    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
}

void SV_Physics_None(edict_t* ent)
{
    SV_RunThink(ent);
}

void SV_Physics_Noclip(edict_t* ent)
{
    if (!SV_RunThink(ent)) return;

    VectorMA(ent->v.angles, static_cast<float>(host_frametime), ent->v.avelocity, ent->v.angles);
    VectorMA(ent->v.origin, static_cast<float>(host_frametime), ent->v.velocity, ent->v.origin);
    SV_LinkEdict(ent, false);
}

void SV_CheckWaterTransition(edict_t* ent)
{
    const int cont = SV_PointContents(ent->v.origin);
    if (!ent->v.watertype) {
        ent->v.watertype = static_cast<float>(cont);
        ent->v.waterlevel = 1;
        return;
    }

    if (cont <= CONTENTS_WATER) {
        if (ent->v.watertype == CONTENTS_EMPTY) SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        ent->v.watertype = static_cast<float>(cont);
        ent->v.waterlevel = 1;
    } else {
        if (ent->v.watertype != CONTENTS_EMPTY) SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        ent->v.watertype = static_cast<float>(CONTENTS_EMPTY);
        ent->v.waterlevel = static_cast<float>(cont);
    }
}

void SV_Physics_Toss(edict_t* ent)
{
    if (!SV_RunThink(ent)) return;
    if (static_cast<int>(ent->v.flags) & FL_ONGROUND) return;

    SV_CheckVelocity(ent);

    if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE) {
        SV_AddGravity(ent);
    }

    ent->v.angles += ent->v.avelocity * static_cast<float>(host_frametime);

    const Vector3 move = ent->v.velocity * static_cast<float>(host_frametime);
    const trace_t trace = SV_PushEntity(ent, move);
    if (trace.fraction == 1.0f || ent->free) return;

    const float backoff = (ent->v.movetype == MOVETYPE_BOUNCE) ? 1.5f : 1.0f;
    ClipVelocity(ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

    if (trace.plane.normal.z > 0.7f) {
        if (ent->v.velocity.z < 60.0f || ent->v.movetype != MOVETYPE_BOUNCE) {
            ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
            ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));
            ent->v.velocity = vec3_origin;
            ent->v.avelocity = vec3_origin;
        }
    }

    SV_CheckWaterTransition(ent);
}

void SV_Physics_Step(edict_t* ent)
{
    if (!(static_cast<int>(ent->v.flags) & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        const bool hitsound = (ent->v.velocity.z < sv_gravity.value * -0.1f);

        SV_AddGravity(ent);
        SV_CheckVelocity(ent);
        SV_FlyMove(ent, static_cast<float>(host_frametime), nullptr);
        SV_LinkEdict(ent, true);

        if (static_cast<int>(ent->v.flags) & FL_ONGROUND) {
            if (hitsound) SV_StartSound(ent, 0, "demon/dland2.wav", 255, 1);
        }
    }

    SV_RunThink(ent);
    SV_CheckWaterTransition(ent);
}

void SV_Physics()
{
    pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    pr_global_struct->other = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    pr_global_struct->time = static_cast<float>(sv.time);
    PR_ExecuteProgram(pr_global_struct->StartFrame);

    edict_t* ent = sv.edicts;
    for (int i = 0; i < sv.num_edicts; ++i, ent = NEXT_EDICT(ent)) {
        if (ent->free) continue;
        if (pr_global_struct->force_retouch) SV_LinkEdict(ent, true);

        if (i > 0 && i <= svs.maxclients) {
            SV_Physics_Client(ent, i);
        } else if (ent->v.movetype == MOVETYPE_PUSH) {
            SV_Physics_Pusher(ent);
        } else if (ent->v.movetype == MOVETYPE_NONE) {
            SV_Physics_None(ent);
        } else if (ent->v.movetype == MOVETYPE_NOCLIP) {
            SV_Physics_Noclip(ent);
        } else if (ent->v.movetype == MOVETYPE_STEP) {
            SV_Physics_Step(ent);
        } else if (ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_BOUNCE
            || ent->v.movetype == MOVETYPE_FLY || ent->v.movetype == MOVETYPE_FLYMISSILE) {
            SV_Physics_Toss(ent);
        } else {
            Sys_Error("SV_Physics: bad movetype %i", static_cast<int>(ent->v.movetype));
        }
    }

    if (pr_global_struct->force_retouch) pr_global_struct->force_retouch--;

    sv.time += host_frametime;
}

bool SV_CheckBottom(edict_t* ent)
{
    Vector3 mins = ent->v.origin + ent->v.mins;
    Vector3 maxs = ent->v.origin + ent->v.maxs;
    Vector3 start{}, stop{};

    start.z = mins.z - 1.0f;
    for (int x = 0; x <= 1; ++x) {
        for (int y = 0; y <= 1; ++y) {
            start.x = x ? maxs.x : mins.x;
            start.y = y ? maxs.y : mins.y;
            if (SV_PointContents(start) != CONTENTS_SOLID) goto realcheck;
        }
    }

    c_yes++;
    return true;

realcheck:
    c_no++;
    start.z = mins.z;
    start.x = stop.x = (mins.x + maxs.x) * 0.5f;
    start.y = stop.y = (mins.y + maxs.y) * 0.5f;
    stop.z = start.z - 2.0f * STEPSIZE;
    trace_t trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, ent);

    if (trace.fraction == 1.0f) return false;

    const float mid = trace.endpos.z;
    float bottom = trace.endpos.z;

    for (int x = 0; x <= 1; ++x) {
        for (int y = 0; y <= 1; ++y) {
            start.x = stop.x = x ? maxs.x : mins.x;
            start.y = stop.y = y ? maxs.y : mins.y;

            trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, ent);

            if (trace.fraction != 1.0f && trace.endpos.z > bottom) bottom = trace.endpos.z;
            if (trace.fraction == 1.0f || mid - trace.endpos.z > STEPSIZE) return false;
        }
    }

    c_yes++;
    return true;
}

bool SV_movestep(edict_t* ent, const Vector3& move, bool relink)
{
    const Vector3 oldorg = ent->v.origin;
    Vector3 neworg = ent->v.origin + move;

    if (static_cast<int>(ent->v.flags) & (FL_SWIM | FL_FLY)) {
        for (int i = 0; i < 2; ++i) {
            neworg = ent->v.origin + move;
            const edict_t* enemy = PROG_TO_EDICT(ent->v.enemy);
            if (i == 0 && enemy != sv.edicts) {
                const float dz = ent->v.origin.z - PROG_TO_EDICT(ent->v.enemy)->v.origin.z;
                if (dz > 40.0f) neworg.z -= 8.0f;
                if (dz < 30.0f) neworg.z += 8.0f;
            }

            trace_t trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, neworg, false, ent);

            if (trace.fraction == 1.0f) {
                if ((static_cast<int>(ent->v.flags) & FL_SWIM) && SV_PointContents(trace.endpos) == CONTENTS_EMPTY) {
                    return false;
                }

                ent->v.origin = trace.endpos;
                if (relink) SV_LinkEdict(ent, true);
                return true;
            }

            if (enemy == sv.edicts) break;
        }

        return false;
    }

    neworg.z += STEPSIZE;
    Vector3 end = neworg;
    end.z -= STEPSIZE * 2;

    trace_t trace = SV_Move(neworg, ent->v.mins, ent->v.maxs, end, false, ent);

    if (trace.allsolid) return false;

    if (trace.startsolid) {
        neworg.z -= STEPSIZE;
        trace = SV_Move(neworg, ent->v.mins, ent->v.maxs, end, false, ent);
        if (trace.allsolid || trace.startsolid) return false;
    }

    if (trace.fraction == 1.0f) {
        if (static_cast<int>(ent->v.flags) & FL_PARTIALGROUND) {
            ent->v.origin += move;
            if (relink) SV_LinkEdict(ent, true);
            ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) & ~FL_ONGROUND);
            return true;
        }

        return false;
    }

    ent->v.origin = trace.endpos;

    if (!SV_CheckBottom(ent)) {
        if (static_cast<int>(ent->v.flags) & FL_PARTIALGROUND) {
            if (relink) SV_LinkEdict(ent, true);
            return true;
        }

        ent->v.origin = oldorg;
        return false;
    }

    if (static_cast<int>(ent->v.flags) & FL_PARTIALGROUND) {
        ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) & ~FL_PARTIALGROUND);
    }

    ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));

    if (relink) SV_LinkEdict(ent, true);

    return true;
}

qboolean SV_StepDirection(edict_t* ent, float yaw, float dist)
{
    ent->v.ideal_yaw = yaw;
    VM::PF_changeyaw();

    const float rad_yaw = static_cast<float>(yaw * M_PI * 2.0 / 360.0);
    const Vector3 move(cosf(rad_yaw) * dist, sinf(rad_yaw) * dist, 0.0f);

    const Vector3 oldorigin = ent->v.origin;
    if (SV_movestep(ent, move, false)) {
        const float delta = ent->v.angles[YAW] - ent->v.ideal_yaw;
        if (delta > 45.0f && delta < 315.0f) {
            ent->v.origin = oldorigin;
        }

        SV_LinkEdict(ent, true);
        return true;
    }

    SV_LinkEdict(ent, true);
    return false;
}

void SV_FixCheckBottom(edict_t* ent)
{
    ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_PARTIALGROUND);
}

constexpr float DI_NODIR = -1.0f;

void SV_NewChaseDir(edict_t* actor, edict_t* enemy, float dist)
{
    eastl::array<float, 3> d{0.0f, DI_NODIR, DI_NODIR};

    const float olddir = anglemod(static_cast<float>(static_cast<int>(actor->v.ideal_yaw / 45.0f) * 45));
    const float turnaround = anglemod(olddir - 180.0f);

    const float deltax = enemy->v.origin[0] - actor->v.origin[0];
    const float deltay = enemy->v.origin[1] - actor->v.origin[1];
    if (deltax > 10.0f) d[1] = 0.0f;
    else if (deltax < -10.0f) d[1] = 180.0f;
    else d[1] = DI_NODIR;

    if (deltay < -10.0f) d[2] = 270.0f;
    else if (deltay > 10.0f) d[2] = 90.0f;
    else d[2] = DI_NODIR;

    if (d[1] != DI_NODIR && d[2] != DI_NODIR) {
        const float tdir = (d[1] == 0.0f) ? ((d[2] == 90.0f) ? 45.0f : 315.0f) : ((d[2] == 90.0f) ? 135.0f : 215.0f);
        if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) return;
    }

    if (((rand() & 3) & 1) || fabs(deltay) > fabs(deltax)) {
        const float tdir = d[1];
        d[1] = d[2];
        d[2] = tdir;
    }

    if (d[1] != DI_NODIR && d[1] != turnaround && SV_StepDirection(actor, d[1], dist)) return;
    if (d[2] != DI_NODIR && d[2] != turnaround && SV_StepDirection(actor, d[2], dist)) return;
    if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist)) return;

    if (rand() & 1) {
        for (float tdir = 0.0f; tdir <= 315.0f; tdir += 45.0f) {
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) return;
        }
    } else {
        for (float tdir = 315.0f; tdir >= 0.0f; tdir -= 45.0f) {
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist)) return;
        }
    }

    if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist)) return;

    actor->v.ideal_yaw = olddir;
    if (!SV_CheckBottom(actor)) SV_FixCheckBottom(actor);
}

qboolean SV_CloseEnough(edict_t* ent, edict_t* goal, float dist)
{
    for (int i = 0; i < 3; ++i) {
        if (goal->v.absmin[i] > ent->v.absmax[i] + dist) return false;
        if (goal->v.absmax[i] < ent->v.absmin[i] - dist) return false;
    }
    return true;
}

void SV_MoveToGoal()
{
    edict_t* ent = PROG_TO_EDICT(pr_global_struct->self);
    edict_t* goal = PROG_TO_EDICT(ent->v.goalentity);
    const float dist = G_FLOAT(OFS_PARM0);

    if (!(static_cast<int>(ent->v.flags) & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        G_FLOAT(OFS_RETURN) = 0.0f;
        return;
    }

    if (PROG_TO_EDICT(ent->v.enemy) != sv.edicts && SV_CloseEnough(ent, goal, dist)) return;

    if ((rand() & 3) == 1 || !SV_StepDirection(ent, ent->v.ideal_yaw, dist)) {
        SV_NewChaseDir(ent, goal, dist);
    }
}

constexpr int MAX_FORWARD = 6;

void SV_SetIdealPitch()
{
    if (!(static_cast<int>(sv_player->v.flags) & FL_ONGROUND)) return;

    const float angleval = static_cast<float>(sv_player->v.angles[YAW] * M_PI * 2.0 / 360.0);
    const float sinval = sinf(angleval);
    const float cosval = cosf(angleval);

    eastl::array<float, MAX_FORWARD> z{};

    int i = 0;
    for (; i < MAX_FORWARD; ++i) {
        Vector3 top(
            sv_player->v.origin.x + cosval * (i + 3) * 12.0f,
            sv_player->v.origin.y + sinval * (i + 3) * 12.0f,
            sv_player->v.origin.z + sv_player->v.view_ofs.z);

        Vector3 bottom = top;
        bottom.z -= 160.0f;

        const trace_t tr = SV_Move(top, vec3_origin, vec3_origin, bottom, 1, sv_player);
        if (tr.allsolid || tr.fraction == 1.0f) return;

        z[static_cast<size_t>(i)] = top.z + tr.fraction * (bottom.z - top.z);
    }

    int dir = 0, steps = 0;
    for (int j = 1; j < i; ++j) {
        const int step = static_cast<int>(z[static_cast<size_t>(j)] - z[static_cast<size_t>(j - 1)]);
        if (step > -ON_EPSILON && step < ON_EPSILON) continue;
        if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON)) return;

        steps++;
        dir = step;
    }

    if (!dir) {
        sv_player->v.idealpitch = 0.0f;
        return;
    }

    if (steps < 2) return;

    sv_player->v.idealpitch = -dir * sv_idealpitchscale.value;
}

void SV_UserFriction()
{
    const float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    if (!speed) return;

    Vector3 start(
        origin[0] + velocity[0] / speed * 16.0f,
        origin[1] + velocity[1] / speed * 16.0f,
        origin[2] + sv_player->v.mins.z);
    Vector3 stop = start;
    stop.z -= 34.0f;

    const trace_t trace = SV_Move(start, vec3_origin, vec3_origin, stop, true, sv_player);
    const float friction = (trace.fraction == 1.0f) ? (sv_friction.value * sv_edgefriction.value) : sv_friction.value;

    const float control = (speed < sv_stopspeed.value) ? sv_stopspeed.value : speed;
    float newspeed = static_cast<float>(speed - host_frametime * control * friction);
    if (newspeed < 0.0f) newspeed = 0.0f;

    newspeed /= speed;
    velocity[0] *= newspeed;
    velocity[1] *= newspeed;
    velocity[2] *= newspeed;
}

void SV_Accelerate()
{
    const float currentspeed = DotProduct(velocity, wishdir);
    const float addspeed = wishspeed - currentspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = static_cast<float>(sv_accelerate.value * host_frametime * wishspeed);
    if (accelspeed > addspeed) accelspeed = addspeed;

    for (int i = 0; i < 3; ++i) velocity[i] += accelspeed * wishdir[i];
}

void SV_AirAccelerate(Vector3 wishveloc)
{
    float wishspd = wishveloc.normalize();
    if (wishspd > 30.0f) wishspd = 30.0f;

    const float currentspeed = DotProduct(velocity, wishveloc);
    const float addspeed = wishspd - currentspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = static_cast<float>(sv_accelerate.value * wishspeed * host_frametime);
    if (accelspeed > addspeed) accelspeed = addspeed;

    for (int i = 0; i < 3; ++i) velocity[i] += accelspeed * wishveloc[i];
}

void DropPunchAngle()
{
    float len = sv_player->v.punchangle.normalize();
    len -= static_cast<float>(10.0 * host_frametime);
    if (len < 0.0f) len = 0.0f;
    sv_player->v.punchangle *= len;
}

void SV_WaterMove()
{
    Vector3 forward{}, right{}, up{};
    AngleVectors(sv_player->v.v_angle, forward, right, up);

    Vector3 wishvel = forward * cmd.forwardmove + right * cmd.sidemove;

    if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove) wishvel.z -= 60.0f;
    else wishvel.z += cmd.upmove;

    float w_speed = wishvel.length();
    if (w_speed > sv_maxspeed.value) {
        wishvel *= sv_maxspeed.value / w_speed;
        w_speed = sv_maxspeed.value;
    }

    w_speed *= 0.7f;

    const float speed = Length(velocity);
    float newspeed = 0.0f;
    if (speed) {
        newspeed = speed - static_cast<float>(host_frametime * speed * sv_friction.value);
        if (newspeed < 0.0f) newspeed = 0.0f;
        VectorScale(velocity, newspeed / speed, velocity);
    }

    if (!w_speed) return;

    const float addspeed = w_speed - newspeed;
    if (addspeed <= 0.0f) return;

    wishvel.normalize();
    float accelspeed = static_cast<float>(sv_accelerate.value * w_speed * host_frametime);
    if (accelspeed > addspeed) accelspeed = addspeed;

    for (int i = 0; i < 3; ++i) velocity[i] += accelspeed * wishvel[i];
}

void SV_WaterJump()
{
    if (sv.time > sv_player->v.teleport_time || !sv_player->v.waterlevel) {
        sv_player->v.flags = static_cast<float>(static_cast<int>(sv_player->v.flags) & ~FL_WATERJUMP);
        sv_player->v.teleport_time = 0.0;
    }

    sv_player->v.velocity.x = sv_player->v.movedir.x;
    sv_player->v.velocity.y = sv_player->v.movedir.y;
}

void SV_AirMove()
{
    Vector3 forward{}, right{}, up{};
    AngleVectors(sv_player->v.angles, forward, right, up);

    float fmove = cmd.forwardmove;
    float smove = cmd.sidemove;

    if (sv.time < sv_player->v.teleport_time && fmove < 0.0f) fmove = 0.0f;

    Vector3 wishvel = forward * fmove + right * smove;
    if (static_cast<int>(sv_player->v.movetype) != MOVETYPE_WALK) wishvel.z = cmd.upmove;
    else wishvel.z = 0.0f;

    wishdir = wishvel;
    wishspeed = wishdir.normalize();
    if (wishspeed > sv_maxspeed.value) {
        wishvel *= sv_maxspeed.value / wishspeed;
        wishspeed = sv_maxspeed.value;
    }

    if (sv_player->v.movetype == MOVETYPE_NOCLIP) {
        VectorCopy(wishvel, velocity);
    } else if (onground) {
        SV_UserFriction();
        SV_Accelerate();
    } else {
        SV_AirAccelerate(wishvel);
    }
}

void SV_ClientThink()
{
    if (sv_player->v.movetype == MOVETYPE_NONE) return;

    onground = static_cast<int>(sv_player->v.flags) & FL_ONGROUND;
    origin = sv_player->v.origin;
    velocity = sv_player->v.velocity;

    DropPunchAngle();

    if (sv_player->v.health <= 0.0f) return;

    cmd = host_client->cmd;
    angles = sv_player->v.angles;

    const Vector3 v_angle = sv_player->v.v_angle + sv_player->v.punchangle;
    angles[ROLL] = V_CalcRoll(sv_player->v.angles, sv_player->v.velocity) * 4.0f;
    if (!sv_player->v.fixangle) {
        angles[PITCH] = -v_angle[PITCH] / 3.0f;
        angles[YAW] = v_angle[YAW];
    }

    if (static_cast<int>(sv_player->v.flags) & FL_WATERJUMP) {
        SV_WaterJump();
        return;
    }

    if ((sv_player->v.waterlevel >= 2) && (sv_player->v.movetype != MOVETYPE_NOCLIP)) {
        SV_WaterMove();
        return;
    }

    SV_AirMove();
}

void SV_ReadClientMove(usercmd_t* move)
{
    host_client->ping_times[static_cast<size_t>(host_client->num_pings % NUM_PING_TIMES)] = static_cast<float>(sv.time) - MSG_ReadFloat();
    host_client->num_pings++;

    Vector3 angle{};
    angle.x = MSG_ReadAngle();
    angle.y = MSG_ReadAngle();
    angle.z = MSG_ReadAngle();
    host_client->edict->v.v_angle = angle;

    move->forwardmove = static_cast<float>(MSG_ReadShort());
    move->sidemove = static_cast<float>(MSG_ReadShort());
    move->upmove = static_cast<float>(MSG_ReadShort());

    const int bits = MSG_ReadByte();
    host_client->edict->v.button0 = static_cast<float>(bits & 1);
    host_client->edict->v.button2 = static_cast<float>((bits & 2) >> 1);

    const int impulse = MSG_ReadByte();
    if (impulse) host_client->edict->v.impulse = static_cast<float>(impulse);
}

qboolean SV_ReadClientMessage()
{
    static constexpr eastl::array<eastl::string_view, 19> allowed_commands = {
        "status", "god", "notarget", "fly", "name", "noclip",
        "say", "say_team", "tell", "color", "kill", "pause",
        "spawn", "begin", "prespawn", "kick", "ping", "give", "ban"
    };

    int ret = 0;
    do {
    nextmsg:
        ret = NET_GetMessage(host_client->netconnection);
        if (ret == -1) {
            Sys_Printf("SV_ReadClientMessage: NET_GetMessage failed\n");
            return false;
        }

        if (!ret) return true;

        MSG_BeginReading();

        while (true) {
            if (!host_client->active) return false;

            if (msg_badread) {
                Sys_Printf("SV_ReadClientMessage: badread\n");
                return false;
            }

            const int msg_cmd = MSG_ReadChar();

            switch (msg_cmd) {
            case -1:
                goto nextmsg;

            default:
                Sys_Printf("SV_ReadClientMessage: unknown command char\n");
                return false;

            case clc_nop:
                break;

            case clc_stringcmd: {
                const char* s = MSG_ReadString();
                ret = host_client->privileged ? 2 : 0;

                const eastl::string_view cmd_sv(s);
                for (const auto& allowed : allowed_commands) {
                    if (cmd_sv.length() >= allowed.length() && Q_strncasecmp(s, allowed.data(), static_cast<int>(allowed.length())) == 0) {
                        ret = 1;
                        break;
                    }
                }

                if (ret == 2) {
                    Cmd::BufferInsertText(s);
                } else if (ret == 1) {
                    Cmd::ExecuteString(s, Cmd::Source::Client);
                } else {
                    Con_DPrintf("%s tried to %s\n", host_client->name.data(), s);
                }
                break;
            }

            case clc_disconnect:
                return false;

            case clc_move:
                SV_ReadClientMove(&host_client->cmd);
                break;
            }
        }
    } while (ret == 1);

    return true;
}

void SV_RunClients()
{
    auto clients = svs.GetClients();
    for (auto& client : clients) {
        host_client = &client;
        if (!host_client->active) continue;

        sv_player = host_client->edict;

        if (!SV_ReadClientMessage()) {
            SV_DropClient(false);
            continue;
        }

        if (!host_client->spawned) {
            host_client->cmd = usercmd_t{};
            continue;
        }

        if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
            SV_ClientThink();
        }
    }
}

void SV_ClientPrintf(const char* fmt, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, fmt);
    vsprintf_s(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    MSG_WriteByte(&host_client->message, svc_print);
    MSG_WriteString(&host_client->message, string);
}

void SV_BroadcastPrintf(const char* fmt, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, fmt);
    vsprintf_s(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    for (int i = 0; i < svs.maxclients; i++) {
        if (svs.clients[i].active && svs.clients[i].spawned) {
            MSG_WriteByte(&svs.clients[i].message, svc_print);
            MSG_WriteString(&svs.clients[i].message, string);
        }
    }
}

void SV_DropClient(bool crash)
{
    if (!crash) {
        if (NET_CanSendMessage(host_client->netconnection)) {
            MSG_WriteByte(&host_client->message, svc_disconnect);
            NET_SendMessage(host_client->netconnection, &host_client->message);
        }

        if (host_client->edict && host_client->spawned) {
            int saveSelf = pr_global_struct->self;
            pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(host_client->edict));
            PR_ExecuteProgram(pr_global_struct->ClientDisconnect);
            pr_global_struct->self = saveSelf;
        }

        Sys_Printf("Client %s removed\n", host_client->name.data());
    }

    NET_Close(host_client->netconnection);
    host_client->netconnection = nullptr;

    host_client->active = false;
    host_client->name[0] = 0;
    host_client->old_frags = -999999;
    net_activeconnections--;

    for (int i = 0; i < svs.maxclients; i++) {
        client_t* client = &svs.clients[i];
        if (!client->active) {
            continue;
        }

        MSG_WriteByte(&client->message, svc_updatename);
        MSG_WriteByte(&client->message, static_cast<int>(host_client - svs.clients));
        MSG_WriteString(&client->message, "");
        MSG_WriteByte(&client->message, svc_updatefrags);
        MSG_WriteByte(&client->message, static_cast<int>(host_client - svs.clients));
        MSG_WriteShort(&client->message, 0);
        MSG_WriteByte(&client->message, svc_updatecolors);
        MSG_WriteByte(&client->message, static_cast<int>(host_client - svs.clients));
        MSG_WriteByte(&client->message, 0);
    }

    host_client->netconnection = nullptr;
}

} // namespace Server

