// sys_vm.hpp -- Subsystem VM: QuakeC Virtual Machine, Bytecode Execution & Types
#pragma once

#include <cstdint>
#include <ostream>
#include <EASTL/array.h>
#include <EASTL/string_view.h>

#include "sys_core.hpp"

//=============================================================================
// QuakeC Compiler Shared Defs (from pr_comp.hpp)
//=============================================================================

using func_t = int;
using string_t = int;

enum etype_t : int {
    ev_void,
    ev_string,
    ev_float,
    ev_vector,
    ev_entity,
    ev_field,
    ev_function,
    ev_pointer
};

constexpr int OFS_NULL = 0;
constexpr int OFS_RETURN = 1;
constexpr int OFS_PARM0 = 4;
constexpr int OFS_PARM1 = 7;
constexpr int OFS_PARM2 = 10;
constexpr int OFS_PARM3 = 13;
constexpr int OFS_PARM4 = 16;
constexpr int OFS_PARM5 = 19;
constexpr int OFS_PARM6 = 22;
constexpr int OFS_PARM7 = 25;
constexpr int RESERVED_OFS = 28;

enum OpCode : int {
    OP_DONE,
    OP_MUL_F,
    OP_MUL_V,
    OP_MUL_FV,
    OP_MUL_VF,
    OP_DIV_F,
    OP_ADD_F,
    OP_ADD_V,
    OP_SUB_F,
    OP_SUB_V,

    OP_EQ_F,
    OP_EQ_V,
    OP_EQ_S,
    OP_EQ_E,
    OP_EQ_FNC,

    OP_NE_F,
    OP_NE_V,
    OP_NE_S,
    OP_NE_E,
    OP_NE_FNC,

    OP_LE,
    OP_GE,
    OP_LT,
    OP_GT,

    OP_LOAD_F,
    OP_LOAD_V,
    OP_LOAD_S,
    OP_LOAD_ENT,
    OP_LOAD_FLD,
    OP_LOAD_FNC,

    OP_ADDRESS,

    OP_STORE_F,
    OP_STORE_V,
    OP_STORE_S,
    OP_STORE_ENT,
    OP_STORE_FLD,
    OP_STORE_FNC,

    OP_STOREP_F,
    OP_STOREP_V,
    OP_STOREP_S,
    OP_STOREP_ENT,
    OP_STOREP_FLD,
    OP_STOREP_FNC,

    OP_RETURN,
    OP_NOT_F,
    OP_NOT_V,
    OP_NOT_S,
    OP_NOT_ENT,
    OP_NOT_FNC,
    OP_IF,
    OP_IFNOT,
    OP_CALL0,
    OP_CALL1,
    OP_CALL2,
    OP_CALL3,
    OP_CALL4,
    OP_CALL5,
    OP_CALL6,
    OP_CALL7,
    OP_CALL8,
    OP_STATE,
    OP_GOTO,
    OP_AND,
    OP_OR,

    OP_BITAND,
    OP_BITOR
};

struct dstatement_t {
    unsigned short op = 0;
    short a = 0, b = 0, c = 0;
};

struct ddef_t {
    unsigned short type = 0;
    unsigned short ofs = 0;
    int s_name = 0;
};

constexpr int DEF_SAVEGLOBAL = (1 << 15);
constexpr int MAX_PARMS = 8;

struct dfunction_t {
    int first_statement = 0;
    int parm_start = 0;
    int locals = 0;
    int profile = 0;
    int s_name = 0;
    int s_file = 0;
    int numparms = 0;
    eastl::array<uint8_t, MAX_PARMS> parm_size{};
};

constexpr int PROG_VERSION = 6;

struct dprograms_t {
    int version = 0;
    int crc = 0;

    int ofs_statements = 0;
    int numstatements = 0;

    int ofs_globaldefs = 0;
    int numglobaldefs = 0;

    int ofs_fielddefs = 0;
    int numfielddefs = 0;

    int ofs_functions = 0;
    int numfunctions = 0;

    int ofs_strings = 0;
    int numstrings = 0;

    int ofs_globals = 0;
    int numglobals = 0;

    int entityfields = 0;
};

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

#define PROGHEADER_CRC 5927

//=============================================================================
// VM Execution Engine & Edicts (from vm.hpp)
//=============================================================================

typedef union eval_s {
    string_t string;
    float _float;
    float vector[3];
    func_t function;
    int _int;
    int edict;
} eval_t;

constexpr int MAX_ENT_LEAFS = 16;

struct entity_state_t;

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

extern dprograms_t* progs;
extern dfunction_t* pr_functions;
extern char* pr_strings;
extern ddef_t* pr_globaldefs;
extern ddef_t* pr_fielddefs;
extern dstatement_t* pr_statements;
extern globalvars_t* pr_global_struct;
extern float* pr_globals;
extern int pr_edict_size;

void PR_Init();
void PR_ExecuteProgram(func_t fnum);
void PR_LoadProgs();

string_t PR_SetString(const char* str);
inline string_t PR_SetString(eastl::string_view str) { return PR_SetString(str.data()); }
[[nodiscard]] char* PR_GetString(string_t handle);
string_t PR_CreateString(int size, char** out_ptr);

void PR_Profile_f();

[[nodiscard]] edict_t* ED_Alloc();
void ED_Free(edict_t* ed);

string_t ED_NewString(const char* source);
inline string_t ED_NewString(eastl::string_view source) { return ED_NewString(source.data()); }

void ED_Print(edict_t* ed);
void ED_Write(std::ostream& f, edict_t* ed);
char* ED_ParseEdict(char* data, edict_t* ent);

void ED_WriteGlobals(std::ostream& f);
void ED_ParseGlobals(char* data);

void ED_LoadFromFile(char* data);

[[nodiscard]] edict_t* EDICT_NUM(int n);
[[nodiscard]] int NUM_FOR_EDICT(edict_t* e);

#define NEXT_EDICT(e) ((edict_t*)((byte*)e + pr_edict_size))
#define EDICT_TO_PROG(e) ((byte*)e - (byte*)sv.edicts)
#define PROG_TO_EDICT(e) ((edict_t*)((byte*)sv.edicts + e))

#define G_FLOAT(o) (pr_globals[o])
#define G_INT(o) (*(int*)&pr_globals[o])
#define G_EDICT(o) ((edict_t*)((byte*)sv.edicts + *(int*)&pr_globals[o]))
#define G_EDICTNUM(o) NUM_FOR_EDICT(G_EDICT(o))
#define G_VECTOR(o) (&pr_globals[o])
#define G_STRING(o) (PR_GetString(*(string_t*)&pr_globals[o]))
#define G_FUNCTION(o) (*(func_t*)&pr_globals[o])

#define E_FLOAT(e, o) (((float*)&e->v)[o])
#define E_INT(e, o) (*(int*)&((float*)&e->v)[o])
#define E_VECTOR(e, o) (&((float*)&e->v)[o])
#define E_STRING(e, o) (PR_GetString(*(string_t*)&((float*)&e->v)[o]))

extern eastl::array<int, 8> type_size;

typedef void (*builtin_t)(void);
extern builtin_t* pr_builtins;
extern int pr_numbuiltins;

extern int pr_argc;
extern qboolean pr_trace;
extern dfunction_t* pr_xfunction;
extern int pr_xstatement;
extern unsigned short pr_crc;

[[noreturn]] void PR_RunError(const char* error, ...);

void ED_PrintEdicts();
void ED_PrintNum(int ent);

[[nodiscard]] eval_t* GetEdictFieldValue(edict_t* ed, const char* field);
inline eval_t* GetEdictFieldValue(edict_t* ed, eastl::string_view field) { return GetEdictFieldValue(ed, field.data()); }

} // namespace VM
