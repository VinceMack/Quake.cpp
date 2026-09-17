// edict.hpp -- Entity dictionary data structure, field reflection, and serialization
#pragma once

#include <ostream>
#include <string_view>
#include "core/types.hpp"
#include "core/math.hpp"
#include "vm/program.hpp"

// Forward declare entity_state_t
struct entity_state_t;

//=============================================================================
// Program Variables (from progdefs.q1)
//=============================================================================

typedef struct {
    int pad[28];
    int self;
    int other;
    int world;
    float time;
    float frametime;
    float force_retouch;
    string_t mapname;
    float deathmatch;
    float coop;
    float teamplay;
    float serverflags;
    float total_secrets;
    float total_monsters;
    float found_secrets;
    float killed_monsters;
    float parm1;
    float parm2;
    float parm3;
    float parm4;
    float parm5;
    float parm6;
    float parm7;
    float parm8;
    float parm9;
    float parm10;
    float parm11;
    float parm12;
    float parm13;
    float parm14;
    float parm15;
    float parm16;
    Vector3 v_forward;
    Vector3 v_up;
    Vector3 v_right;
    float trace_allsolid;
    float trace_startsolid;
    float trace_fraction;
    Vector3 trace_endpos;
    Vector3 trace_plane_normal;
    float trace_plane_dist;
    int trace_ent;
    float trace_inopen;
    float trace_inwater;
    int msg_entity;
    func_t main;
    func_t StartFrame;
    func_t PlayerPreThink;
    func_t PlayerPostThink;
    func_t ClientKill;
    func_t ClientConnect;
    func_t PutClientInServer;
    func_t ClientDisconnect;
    func_t SetNewParms;
    func_t SetChangeParms;
} globalvars_t;

typedef struct {
    float modelindex;
    Vector3 absmin;
    Vector3 absmax;
    float ltime;
    float movetype;
    float solid;
    Vector3 origin;
    Vector3 oldorigin;
    Vector3 velocity;
    Vector3 angles;
    Vector3 avelocity;
    Vector3 punchangle;
    string_t classname;
    string_t model;
    float frame;
    float skin;
    float effects;
    Vector3 mins;
    Vector3 maxs;
    Vector3 size;
    func_t touch;
    func_t use;
    func_t think;
    func_t blocked;
    float nextthink;
    int groundentity;
    float health;
    float frags;
    float weapon;
    string_t weaponmodel;
    float weaponframe;
    float currentammo;
    float ammo_shells;
    float ammo_nails;
    float ammo_rockets;
    float ammo_cells;
    float items;
    float takedamage;
    int chain;
    float deadflag;
    Vector3 view_ofs;
    float button0;
    float button1;
    float button2;
    float impulse;
    float fixangle;
    Vector3 v_angle;
    float idealpitch;
    string_t netname;
    int enemy;
    float flags;
    float colormap;
    float team;
    float max_health;
    float teleport_time;
    float armortype;
    float armorvalue;
    float waterlevel;
    float watertype;
    float ideal_yaw;
    float yaw_speed;
    int aiment;
    int goalentity;
    float spawnflags;
    string_t target;
    string_t targetname;
    float dmg_take;
    float dmg_save;
    int dmg_inflictor;
    int owner;
    Vector3 movedir;
    string_t message;
    float sounds;
    string_t noise;
    string_t noise1;
    string_t noise2;
    string_t noise3;
} entvars_t;

constexpr int MAX_ENT_LEAFS = 16;

typedef struct edict_s {
    qboolean free;
    link_t area;

    int num_leafs;
    short leafnums[MAX_ENT_LEAFS];

    entity_state_t baseline;

    float freetime;
    entvars_t v;
} edict_t;

#define EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l, edict_t, area)

namespace VM {

extern globalvars_t* pr_global_struct;

[[nodiscard]] edict_t* ED_Alloc();
void ED_Free(edict_t* ed);
void ED_ClearEdict(edict_t* e);

string_t ED_NewString(const char* source);
inline string_t ED_NewString(std::string_view source) { return ED_NewString(source.data()); }

void ED_Print(edict_t* ed);
void ED_Write(std::ostream& f, edict_t* ed);
char* ED_ParseEdict(char* data, edict_t* ent);

void ED_WriteGlobals(std::ostream& f);
void ED_ParseGlobals(char* data);

void ED_LoadFromFile(char* data);

[[nodiscard]] edict_t* EDICT_NUM(int n);
[[nodiscard]] int NUM_FOR_EDICT(edict_t* e);

void ED_PrintEdicts();
void ED_PrintNum(int ent);
void ED_PrintEdict_f();
void ED_Count();

[[nodiscard]] eval_t* GetEdictFieldValue(edict_t* ed, const char* field);
inline eval_t* GetEdictFieldValue(edict_t* ed, std::string_view field) { return GetEdictFieldValue(ed, field.data()); }

ddef_t* ED_FieldAtOfs(int ofs);
ddef_t* ED_GlobalAtOfs(int ofs);
ddef_t* ED_FindField(const char* name);
ddef_t* ED_FindGlobal(char* name);
dfunction_t* ED_FindFunction(char* name);

char* PR_UglyValueString(etype_t type, eval_t* val);
char* PR_GlobalString(int ofs);
char* PR_GlobalStringNoContents(int ofs);

void ED_ClearFieldCache();

} // namespace VM

#define NEXT_EDICT(e) ((edict_t*)((byte*)e + VM::pr_edict_size))
#define EDICT_TO_PROG(e) ((byte*)e - (byte*)Server::sv.edicts)
#define PROG_TO_EDICT(e) ((edict_t*)((byte*)Server::sv.edicts + e))

#define G_FLOAT(o) (VM::pr_globals[o])
#define G_INT(o) (*(int*)&VM::pr_globals[o])
#define G_EDICT(o) ((edict_t*)((byte*)Server::sv.edicts + *(int*)&VM::pr_globals[o]))
#define G_EDICTNUM(o) VM::NUM_FOR_EDICT(G_EDICT(o))
#define G_VECTOR(o) (&VM::pr_globals[o])
#define G_STRING(o) (VM::PR_GetString(*(string_t*)&VM::pr_globals[o]))
#define G_FUNCTION(o) (*(func_t*)&VM::pr_globals[o])

#define E_FLOAT(e, o) (((float*)&e->v)[o])
#define E_INT(e, o) (*(int*)&((float*)&e->v)[o])
#define E_VECTOR(e, o) (&((float*)&e->v)[o])
#define E_STRING(e, o) (VM::PR_GetString(*(string_t*)&((float*)&e->v)[o]))
