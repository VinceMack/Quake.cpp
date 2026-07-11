// vm.cpp -- merged VM subsystem (pr_*.cpp)
// Contains: QuakeC execution engine, edict management, built-in functions

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


namespace VM {

// ============================================================================
// pr_edict.cpp -- entity dictionary and progs data management
// ============================================================================

dprograms_t* progs;
dfunction_t* pr_functions;
char* pr_strings;
static int pr_stringssize;
static char** pr_knownstrings;
static int pr_maxknownstrings;
static int pr_numknownstrings;
ddef_t* pr_fielddefs;
ddef_t* pr_globaldefs;
dstatement_t* pr_statements;
globalvars_t* pr_global_struct;
float* pr_globals; // same as pr_global_struct
int pr_edict_size; // in bytes

unsigned short pr_crc;

int type_size[8] = {
    1, static_cast<int>(sizeof(string_t) / 4), 1, 3, 1, 1, static_cast<int>(sizeof(func_t) / 4), static_cast<int>(sizeof(void*) / 4)
};

ddef_t* ED_FieldAtOfs(int ofs);
qboolean ED_ParseEpair(void* base, ddef_t* key, char* s);

cvar_t nomonsters = { "nomonsters", "0", {}, {}, {}, {} };
cvar_t gamecfg = { "gamecfg", "0", {}, {}, {}, {} };
cvar_t scratch1 = { "scratch1", "0", {}, {}, {}, {} };
cvar_t scratch2 = { "scratch2", "0", {}, {}, {}, {} };
cvar_t scratch3 = { "scratch3", "0", {}, {}, {}, {} };
cvar_t scratch4 = { "scratch4", "0", {}, {}, {}, {} };
cvar_t savedgamecfg = { "savedgamecfg", "0", true, {}, {}, {} };
cvar_t saved1 = { "saved1", "0", true, {}, {}, {} };
cvar_t saved2 = { "saved2", "0", true, {}, {}, {} };
cvar_t saved3 = { "saved3", "0", true, {}, {}, {} };
cvar_t saved4 = { "saved4", "0", true, {}, {}, {} };

#define MAX_FIELD_LEN 64
#define GEFV_CACHESIZE 2

typedef struct {
    ddef_t* pcache;
    char field[MAX_FIELD_LEN];
} gefv_cache;

static gefv_cache gefvCache[GEFV_CACHESIZE] = { { NULL, "" }, { NULL, "" } };

/*
=================
ED_ClearEdict

Sets everything to NULL
=================
*/
void ED_ClearEdict(edict_t* e)
{
    memset(&e->v, 0, progs->entityfields * 4);
    e->free = false;
}

/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t* ED_Alloc(void)
{
    int i;
    edict_t* e;

    for (i = svs.maxclients + 1; i < sv.num_edicts; i++) {
        e = EDICT_NUM(i);
        // the first couple seconds of server time can involve a lot of
        // freeing and allocating, so relax the replacement policy
        if (e->free && (e->freetime < 2 || sv.time - e->freetime > 0.5)) {
            ED_ClearEdict(e);

            return e;
        }
    }

    if (i == MAX_EDICTS) {
        Sys_Error("ED_Alloc: no free edicts");
    }

    sv.num_edicts++;
    e = EDICT_NUM(i);
    ED_ClearEdict(e);

    return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free(edict_t* ed)
{
    SV_UnlinkEdict(ed); // unlink from world bsp

    ed->free = true;
    ed->v.model = 0;
    ed->v.takedamage = 0;
    ed->v.modelindex = 0;
    ed->v.colormap = 0;
    ed->v.skin = 0;
    ed->v.frame = 0;
    VectorCopy(vec3_origin, ed->v.origin);
    VectorCopy(vec3_origin, ed->v.angles);
    ed->v.nextthink = -1;
    ed->v.solid = 0;

    ed->freetime = static_cast<float>(sv.time);
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
ddef_t* ED_GlobalAtOfs(int ofs)
{
    ddef_t* def;
    int i;

    for (i = 0; i < progs->numglobaldefs; i++) {
        def = &pr_globaldefs[i];
        if (def->ofs == ofs) {
            return def;
        }
    }

    return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
ddef_t* ED_FieldAtOfs(int ofs)
{
    ddef_t* def;
    int i;

    for (i = 0; i < progs->numfielddefs; i++) {
        def = &pr_fielddefs[i];
        if (def->ofs == ofs) {
            return def;
        }
    }

    return NULL;
}

/*
============
ED_FindField
============
*/
ddef_t* ED_FindField(const char* name)
{
    ddef_t* def;
    int i;

    for (i = 0; i < progs->numfielddefs; i++) {
        def = &pr_fielddefs[i];
        if (!strcmp(PR_GetString(def->s_name), name)) {
            return def;
        }
    }

    return NULL;
}

/*
============
ED_FindGlobal
============
*/
ddef_t* ED_FindGlobal(char* name)
{
    ddef_t* def;
    int i;

    for (i = 0; i < progs->numglobaldefs; i++) {
        def = &pr_globaldefs[i];
        if (!strcmp(PR_GetString(def->s_name), name)) {
            return def;
        }
    }

    return NULL;
}

/*
============
ED_FindFunction
============
*/
dfunction_t* ED_FindFunction(char* name)
{
    dfunction_t* func;
    int i;

    for (i = 0; i < progs->numfunctions; i++) {
        func = &pr_functions[i];
        if (!strcmp(PR_GetString(func->s_name), name)) {
            return func;
        }
    }

    return NULL;
}

eval_t* GetEdictFieldValue(edict_t* ed, const char* field)
{
    ddef_t* def = NULL;
    int i;
    static int rep = 0;

    for (i = 0; i < GEFV_CACHESIZE; i++) {
        if (!strcmp(field, gefvCache[i].field)) {
            def = gefvCache[i].pcache;
            goto Done;
        }
    }

    def = ED_FindField(field);

    if (strlen(field) < MAX_FIELD_LEN) {
        gefvCache[rep].pcache = def;
        strcpy_s(gefvCache[rep].field, sizeof(gefvCache[rep].field), field);
        rep ^= 1;
    }

Done:
    if (!def) {
        return NULL;
    }

    return (eval_t*)((char*)&ed->v + def->ofs * 4);
}

/*
============
PR_ValueString

Returns a string describing *data in a type specific manner
=============
*/
char* PR_ValueString(etype_t type, eval_t* val)
{
    static char line[256];
    ddef_t* def;
    dfunction_t* f;

    type = (etype_t)(type & ~DEF_SAVEGLOBAL);

    switch (type) {
    case ev_string:
        sprintf_s(line, sizeof(line), "%s", PR_GetString(val->string));
        break;
    case ev_entity:
        sprintf_s(line, sizeof(line), "entity %i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
        break;
    case ev_function:
        f = pr_functions + val->function;
        sprintf_s(line, sizeof(line), "%s()", PR_GetString(f->s_name));
        break;
    case ev_field:
        def = ED_FieldAtOfs(val->_int);
        sprintf_s(line, sizeof(line), ".%s", PR_GetString(def->s_name));
        break;
    case ev_void:
        sprintf_s(line, sizeof(line), "void");
        break;
    case ev_float:
        sprintf_s(line, sizeof(line), "%5.1f", val->_float);
        break;
    case ev_vector:
        sprintf_s(line, sizeof(line), "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1],
            val->vector[2]);
        break;
    case ev_pointer:
        sprintf_s(line, sizeof(line), "pointer");
        break;
    default:
        sprintf_s(line, sizeof(line), "bad type %i", type);
        break;
    }

    return line;
}

/*
============
PR_UglyValueString

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
char* PR_UglyValueString(etype_t type, eval_t* val)
{
    static char line[256];
    ddef_t* def;
    dfunction_t* f;

    type = (etype_t)(type & ~DEF_SAVEGLOBAL);

    switch (type) {
    case ev_string:
        sprintf_s(line, sizeof(line), "%s", PR_GetString(val->string));
        break;
    case ev_entity:
        sprintf_s(line, sizeof(line), "%i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
        break;
    case ev_function:
        f = pr_functions + val->function;
        sprintf_s(line, sizeof(line), "%s", PR_GetString(f->s_name));
        break;
    case ev_field:
        def = ED_FieldAtOfs(val->_int);
        sprintf_s(line, sizeof(line), "%s", PR_GetString(def->s_name));
        break;
    case ev_void:
        sprintf_s(line, sizeof(line), "void");
        break;
    case ev_float:
        sprintf_s(line, sizeof(line), "%f", val->_float);
        break;
    case ev_vector:
        sprintf_s(line, sizeof(line), "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);
        break;
    default:
        sprintf_s(line, sizeof(line), "bad type %i", type);
        break;
    }

    return line;
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
char* PR_GlobalString(int ofs)
{
    char* s;
    int i;
    ddef_t* def;
    void* val;
    static char line[128];

    val = (void*)&pr_globals[ofs];
    def = ED_GlobalAtOfs(ofs);
    if (!def) {
        sprintf_s(line, sizeof(line), "%i(??? unknown)", ofs);
    } else {
        s = PR_ValueString((etype_t)def->type, (eval_t*)val);
        sprintf_s(line, sizeof(line), "%i(%s)%s", ofs, PR_GetString(def->s_name), s);
    }

    i = (int)strlen(line);
    for (; i < 20; i++) {
        strcat_s(line, sizeof(line), " ");
    }
    strcat_s(line, sizeof(line), " ");

    return line;
}

char* PR_GlobalStringNoContents(int ofs)
{
    int i;
    ddef_t* def;
    static char line[128];

    def = ED_GlobalAtOfs(ofs);
    if (!def) {
        sprintf_s(line, sizeof(line), "%i(???)", ofs);
    } else {
        sprintf_s(line, sizeof(line), "%i(%s)", ofs, PR_GetString(def->s_name));
    }

    i = (int)strlen(line);
    for (; i < 20; i++) {
        strcat_s(line, sizeof(line), " ");
    }
    strcat_s(line, sizeof(line), " ");

    return line;
}

/*
=============
ED_Print

For debugging
=============
*/
void ED_Print(edict_t* ed)
{
    int l;
    ddef_t* d;
    int* v;
    int i, j;
    char* name;
    int type;

    if (ed->free) {
        Con_Printf("FREE\n");

        return;
    }

    Con_Printf("\nEDICT %i:\n", NUM_FOR_EDICT(ed));
    for (i = 1; i < progs->numfielddefs; i++) {
        d = &pr_fielddefs[i];
        name = PR_GetString(d->s_name);
        if (name[strlen(name) - 2] == '_') {
            continue; // skip _x, _y, _z vars
        }

        v = (int*)((char*)&ed->v + d->ofs * 4);

        // if the value is still all 0, skip the field
        type = d->type & ~DEF_SAVEGLOBAL;

        for (j = 0; j < type_size[type]; j++) {
            if (v[j]) {
                break;
            }
        }
        if (j == type_size[type]) {
            continue;
        }

        Con_Printf("%s", name);
        l = (int)strlen(name);
        while (l++ < 15) {
            Con_Printf(" ");
        }

        Con_Printf("%s\n", PR_ValueString((etype_t)d->type, (eval_t*)v));
    }
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write(FILE* f, edict_t* ed)
{
    ddef_t* d;
    int* v;
    int i, j;
    char* name;
    int type;

    fprintf(f, "{\n");

    if (ed->free) {
        fprintf(f, "}\n");

        return;
    }

    for (i = 1; i < progs->numfielddefs; i++) {
        d = &pr_fielddefs[i];
        name = PR_GetString(d->s_name);
        if (name[strlen(name) - 2] == '_') {
            continue; // skip _x, _y, _z vars
        }

        v = (int*)((char*)&ed->v + d->ofs * 4);

        // if the value is still all 0, skip the field
        type = d->type & ~DEF_SAVEGLOBAL;
        for (j = 0; j < type_size[type]; j++) {
            if (v[j]) {
                break;
            }
        }
        if (j == type_size[type]) {
            continue;
        }

        fprintf(f, "\"%s\" ", name);
        fprintf(f, "\"%s\"\n", PR_UglyValueString((etype_t)d->type, (eval_t*)v));
    }

    fprintf(f, "}\n");
}

inline void ED_PrintNum(int ent)
{
    ED_Print(EDICT_NUM(ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts(void)
{
    int i;

    Con_Printf("%i entities\n", sv.num_edicts);
    for (i = 0; i < sv.num_edicts; i++) {
        ED_PrintNum(i);
    }
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edicy
=============
*/
void ED_PrintEdict_f(void)
{
    int i;

    i = Q_atoi(Cmd::Argv(1));
    if (i >= sv.num_edicts) {
        Con_Printf("Bad edict number\n");

        return;
    }

    ED_PrintNum(i);
}

/*
=============
ED_Count

For debugging
=============
*/
void ED_Count(void)
{
    int i;
    edict_t* ent;
    int active, models, solid, step;

    active = models = solid = step = 0;
    for (i = 0; i < sv.num_edicts; i++) {
        ent = EDICT_NUM(i);
        if (ent->free) {
            continue;
        }

        active++;
        if (ent->v.solid) {
            solid++;
        }

        if (ent->v.model) {
            models++;
        }

        if (ent->v.movetype == MOVETYPE_STEP) {
            step++;
        }
    }

    Con_Printf("num_edicts:%3i\n", sv.num_edicts);
    Con_Printf("active    :%3i\n", active);
    Con_Printf("view      :%3i\n", models);
    Con_Printf("touch     :%3i\n", solid);
    Con_Printf("step      :%3i\n", step);
}

/*
==============================================================================

					ARCHIVING GLOBALS

FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals(FILE* f)
{
    ddef_t* def;
    int i;
    char* name;
    int type;

    fprintf(f, "{\n");
    for (i = 0; i < progs->numglobaldefs; i++) {
        def = &pr_globaldefs[i];
        type = def->type;
        if (!(def->type & DEF_SAVEGLOBAL)) {
            continue;
        }

        type &= ~DEF_SAVEGLOBAL;

        if (type != ev_string && type != ev_float && type != ev_entity) {
            continue;
        }

        name = PR_GetString(def->s_name);
        fprintf(f, "\"%s\" ", name);
        fprintf(f, "\"%s\"\n",
            PR_UglyValueString((etype_t)type, (eval_t*)&pr_globals[def->ofs]));
    }
    fprintf(f, "}\n");
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals(char* data)
{
    char keyname[64];
    ddef_t* key;

    while (1) {
        // parse key
        data = COM_Parse(data);
        if (com_token[0] == '}') {
            break;
        }

        if (!data) {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        strcpy_s(keyname, sizeof(keyname), com_token);

        // parse value
        data = COM_Parse(data);
        if (!data) {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        if (com_token[0] == '}') {
            Sys_Error("ED_ParseEntity: closing brace without data");
        }

        key = ED_FindGlobal(keyname);
        if (!key) {
            Con_Printf("'%s' is not a global\n", keyname);
            continue;
        }

        if (!ED_ParseEpair((void*)pr_globals, key, com_token)) {
            Host_Error("ED_ParseGlobals: parse error");
        }
    }
}

//============================================================================

/*
========================
ED_NewString
========================
*/
string_t ED_NewString(const char* source)
{
    if (!source) {
        return 0;
    }

    // Allocate new string buffer via PR_CreateString
    int length = (int)strlen(source) + 1;
    char* dest = NULL;
    string_t handle = PR_CreateString(length, &dest);

    // Copy with escape handling
    for (int i = 0; i < length; i++) {
        if (source[i] == '\\' && i < length - 1) {
            i++;
            if (source[i] == 'n') {
                *dest++ = '\n';
            } else {
                *dest++ = '\\';
            }
        } else {
            *dest++ = source[i];
        }
    }

    return handle;
}

/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
qboolean ED_ParseEpair(void* base, ddef_t* key, char* s)
{
    int i;
    char string[128];
    ddef_t* def;
    char *v, *w;
    void* d;
    dfunction_t* func;

    d = (void*)((int*)base + key->ofs);

    switch (key->type & ~DEF_SAVEGLOBAL) {
    case ev_string:
        *(string_t*)d = ED_NewString(s);
        break;

    case ev_float:
        *(float*)d = static_cast<float>(atof(s));
        break;

    case ev_vector:
        strcpy_s(string, sizeof(string), s);
        v = string;
        w = string;
        for (i = 0; i < 3; i++) {
            while (*v && *v != ' ') {
                v++;
            }
            *v = 0;
            ((float*)d)[i] = static_cast<float>(atof(w));
            w = v = v + 1;
        }
        break;

    case ev_entity:
        *(int*)d = static_cast<int>(EDICT_TO_PROG(EDICT_NUM(atoi(s))));
        break;

    case ev_field:
        def = ED_FindField(s);
        if (!def) {
            Con_Printf("Can't find field %s\n", s);

            return false;
        }

        *(int*)d = G_INT(def->ofs);
        break;

    case ev_function:
        func = ED_FindFunction(s);
        if (!func) {
            Con_Printf("Can't find function %s\n", s);

            return false;
        }

        *(func_t*)d = static_cast<func_t>(func - pr_functions);
        break;

    default:
        break;
    }

    return true;
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
char* ED_ParseEdict(char* data, edict_t* ent)
{
    ddef_t* key;
    qboolean anglehack;
    qboolean init;
    char keyname[256];
    int n;

    init = false;

    // clear it
    if (ent != sv.edicts) { // hack
        memset(&ent->v, 0, progs->entityfields * 4);
    }

    // go through all the dictionary pairs
    while (1) {
        // parse key
        data = COM_Parse(data);
        if (com_token[0] == '}') {
            break;
        }

        if (!data) {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        // anglehack is to allow QuakeEd to write single scalar angles
        // and allow them to be turned into vectors. (FIXME...)
        if (!strcmp(com_token, "angle")) {
            strcpy_s(com_token, sizeof(com_token), "angles");
            anglehack = true;
        } else {
            anglehack = false;
        }

        // FIXME: change light to _light to get rid of this hack
        if (!strcmp(com_token, "light")) {
            strcpy_s(com_token, sizeof(com_token), "light_lev"); // hack for single light def
        }

        strcpy_s(keyname, sizeof(keyname), com_token);

        // another hack to fix heynames with trailing spaces
        n = (int)strlen(keyname);
        while (n && keyname[n - 1] == ' ') {
            keyname[n - 1] = 0;
            n--;
        }

        // parse value
        data = COM_Parse(data);
        if (!data) {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        if (com_token[0] == '}') {
            Sys_Error("ED_ParseEntity: closing brace without data");
        }

        init = true;

        // keynames with a leading underscore are used for utility comments,
        // and are immediately discarded by quake
        if (keyname[0] == '_') {
            continue;
        }

        key = ED_FindField(keyname);
        if (!key) {
            Con_Printf("'%s' is not a field\n", keyname);
            continue;
        }

        if (anglehack) {
            char temp[32];
            strcpy_s(temp, sizeof(temp), com_token);
            sprintf_s(com_token, sizeof(com_token), "0 %s 0", temp);
        }

        if (!ED_ParseEpair((void*)&ent->v, key, com_token)) {
            Host_Error("ED_ParseEdict: parse error");
        }
    }

    if (!init) {
        ent->free = true;
    }

    return data;
}

/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile(char* data)
{
    edict_t* ent;
    int inhibit;
    dfunction_t* func;

    ent = NULL;
    inhibit = 0;
    pr_global_struct->time = static_cast<float>(sv.time);

    // parse ents
    while (1) {
        // parse the opening brace
        data = COM_Parse(data);
        if (!data) {
            break;
        }

        if (com_token[0] != '{') {
            Sys_Error("ED_LoadFromFile: found %s when expecting {", com_token);
        }

        if (!ent) {
            ent = EDICT_NUM(0);
        } else {
            ent = ED_Alloc();
        }

        data = ED_ParseEdict(data, ent);

        // remove things from different skill levels or deathmatch
        if (deathmatch.value) {
            if (((int)ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH)) {
                ED_Free(ent);
                inhibit++;
                continue;
            }
        } else if ((current_skill == 0 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_EASY)) || (current_skill == 1 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM)) || (current_skill >= 2 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_HARD))) {
            ED_Free(ent);
            inhibit++;
            continue;
        }

        //
        // immediately call spawn function
        //
        if (!ent->v.classname) {
            Con_Printf("No classname for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        // look for the spawn function
        func = ED_FindFunction(PR_GetString(ent->v.classname));

        if (!func) {
            Con_Printf("No spawn function for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
        PR_ExecuteProgram(static_cast<func_t>(func - pr_functions));
    }

    Con_DPrintf("%i entities inhibited\n", inhibit);
}

/*
===============
PR_LoadProgs
===============
*/
void PR_LoadProgs(void)
{
    int i;

    // flush the non-C variable lookup cache
    for (i = 0; i < GEFV_CACHESIZE; i++) {
        gefvCache[i].field[0] = 0;
    }

    CRC_Init(&pr_crc);

    progs = (dprograms_t*)COM_LoadHunkFile("progs.dat");
    if (!progs) {
        Sys_Error("PR_LoadProgs: couldn't load progs.dat");
    }

    Con_DPrintf("Programs occupy %iK.\n", com_filesize / 1024);

    for (i = 0; i < com_filesize; i++) {
        CRC_ProcessByte(&pr_crc, ((byte*)progs)[i]);
    }

    // byte swap the header
    for (i = 0; i < sizeof(*progs) / 4; i++) {
        ((int*)progs)[i] = LittleLong(((int*)progs)[i]);
    }

    if (progs->version != PROG_VERSION) {
        Sys_Error("progs.dat has wrong version number (%i should be %i)",
            progs->version, PROG_VERSION);
    }

    if (progs->crc != PROGHEADER_CRC) {
        Sys_Error(
            "progs.dat system vars have been modified, progdefs.h is out of date");
    }

    pr_functions = (dfunction_t*)((byte*)progs + progs->ofs_functions);

    pr_strings = (char*)progs + progs->ofs_strings;
    pr_stringssize = progs->numstrings;
    pr_numknownstrings = 0;
    pr_maxknownstrings = 0;
    if (pr_knownstrings) {
        Z_Free((void*)pr_knownstrings);
    }

    pr_knownstrings = NULL;
    PR_SetString("");

    pr_globaldefs = (ddef_t*)((byte*)progs + progs->ofs_globaldefs);
    pr_fielddefs = (ddef_t*)((byte*)progs + progs->ofs_fielddefs);
    pr_statements = (dstatement_t*)((byte*)progs + progs->ofs_statements);

    pr_global_struct = (globalvars_t*)((byte*)progs + progs->ofs_globals);
    pr_globals = (float*)pr_global_struct;

    pr_edict_size = progs->entityfields * 4 + sizeof(edict_t) - sizeof(entvars_t);

    // byte swap the lumps
    for (i = 0; i < progs->numstatements; i++) {
        pr_statements[i].op = LittleShort(pr_statements[i].op);
        pr_statements[i].a = LittleShort(pr_statements[i].a);
        pr_statements[i].b = LittleShort(pr_statements[i].b);
        pr_statements[i].c = LittleShort(pr_statements[i].c);
    }

    for (i = 0; i < progs->numfunctions; i++) {
        pr_functions[i].first_statement = LittleLong(pr_functions[i].first_statement);
        pr_functions[i].parm_start = LittleLong(pr_functions[i].parm_start);
        pr_functions[i].s_name = LittleLong(pr_functions[i].s_name);
        pr_functions[i].s_file = LittleLong(pr_functions[i].s_file);
        pr_functions[i].numparms = LittleLong(pr_functions[i].numparms);
        pr_functions[i].locals = LittleLong(pr_functions[i].locals);
    }

    for (i = 0; i < progs->numglobaldefs; i++) {
        pr_globaldefs[i].type = LittleShort(pr_globaldefs[i].type);
        pr_globaldefs[i].ofs = LittleShort(pr_globaldefs[i].ofs);
        pr_globaldefs[i].s_name = LittleLong(pr_globaldefs[i].s_name);
    }

    for (i = 0; i < progs->numfielddefs; i++) {
        pr_fielddefs[i].type = LittleShort(pr_fielddefs[i].type);
        if (pr_fielddefs[i].type & DEF_SAVEGLOBAL) {
            Sys_Error("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
        }

        pr_fielddefs[i].ofs = LittleShort(pr_fielddefs[i].ofs);
        pr_fielddefs[i].s_name = LittleLong(pr_fielddefs[i].s_name);
    }

    for (i = 0; i < progs->numglobals; i++) {
        ((int*)pr_globals)[i] = LittleLong(((int*)pr_globals)[i]);
    }
}

/*
===============
PR_Init
===============
*/
void PR_Init(void)
{
    Cmd::AddCommand("edict", ED_PrintEdict_f);
    Cmd::AddCommand("edicts", ED_PrintEdicts);
    Cmd::AddCommand("edictcount", ED_Count);
    Cmd::AddCommand("profile", PR_Profile_f);
    Cvar::Register(&nomonsters);
    Cvar::Register(&gamecfg);
    Cvar::Register(&scratch1);
    Cvar::Register(&scratch2);
    Cvar::Register(&scratch3);
    Cvar::Register(&scratch4);
    Cvar::Register(&savedgamecfg);
    Cvar::Register(&saved1);
    Cvar::Register(&saved2);
    Cvar::Register(&saved3);
    Cvar::Register(&saved4);
}

edict_t* EDICT_NUM(int n)
{
    if (n < 0 || n >= sv.max_edicts) {
        Sys_Error("EDICT_NUM: bad number %i", n);
    }

    return (edict_t*)((byte*)sv.edicts + (n)*pr_edict_size);
}

int NUM_FOR_EDICT(edict_t* e)
{
    int b;

    b = static_cast<int>((byte*)e - (byte*)sv.edicts);
    b = b / pr_edict_size;

    if (b < 0 || b >= sv.num_edicts) {
        Sys_Error("NUM_FOR_EDICT: bad pointer");
    }

    return b;
}

/*
========================
PR_ExpandStringSlots

Increases the capacity of the known string table
========================
*/
static void PR_ExpandStringSlots(void)
{
    pr_maxknownstrings += 256;
    size_t new_size = pr_maxknownstrings * sizeof(char*);

    // Reallocate table
    pr_knownstrings = (char**)Z_Realloc((void*)pr_knownstrings, (int)new_size);
}

/*
========================
PR_FindString

Searches the known string table for an exact pointer match.
========================
*/
static string_t PR_FindString(const char* str)
{
    for (string_t slot_index = 0; slot_index < pr_numknownstrings; slot_index++) {
        if (pr_knownstrings[slot_index] == str) {
            return slot_index;
        }
    }

    return pr_numknownstrings;
}

/*
========================
PR_SetStringAt

Assigns a string to a slot in the known string table.
========================
*/
static void PR_SetStringAt(int slot_index, const char* str)
{
    // Expand if needed
    if (slot_index >= pr_maxknownstrings) {
        PR_ExpandStringSlots();
    }

    pr_knownstrings[slot_index] = const_cast<char*>(str);

    // Only count if the slot is new
    if (slot_index >= pr_numknownstrings) {
        pr_numknownstrings = slot_index + 1;
    }
}

/*
========================
PR_SetString

Registers a string and returns its handle (string_t).
========================
*/
string_t PR_SetString(const char* str)
{
    if (!str) {
        return 0;
    }

    // If string lies inside pr_strings buffer
    if (str >= pr_strings && str <= &pr_strings[pr_stringssize - 2]) {
        return (string_t)(str - pr_strings);
    }

    // Register if it's beyond current known strings
    string_t slot_index = PR_FindString(str);
    if (slot_index >= pr_numknownstrings) {
        PR_SetStringAt(slot_index, str);
    }

    // Encode known string handle as -(index+1)
    return -(slot_index + 1);
}

/*
========================
PR_GetString

Resolves a Quake string handle (string_t) into a C string.

- Non-negative values (>= 0) refer to offsets inside `pr_strings`.
- Negative values (< 0) refer to entries in `pr_knownstrings`,
  stored as -(index+1).
========================
*/
char* PR_GetString(string_t handle)
{
    // Handle string offset into pr_strings
    if (handle >= 0 && handle < pr_stringssize) {
        return &pr_strings[handle];
    }

    // Handle out of range values
    if (handle < -pr_numknownstrings || handle >= pr_stringssize) {
        Host_Error("PR_GetString: invalid string handle %d\n", handle);
    }

    // Handle known string table (encoded as -(index+1))
    int index = -1 - handle;
    if (pr_knownstrings[index]) {
        return pr_knownstrings[index];
    }

    Host_Error("PR_GetString: attempt to access missing string %d\n", handle);
}

/*
========================
PR_CreateString

Allocates a new string slot in the program string table,
backs it with hunk memory, and optionally returns a pointer
to the allocated buffer.
========================
*/
string_t PR_CreateString(int size, char** out_ptr)
{
    if (size <= 0) {
        return 0; // invalid request
    }

    // Get an unused string slot index
    string_t slot_index = PR_FindString(NULL);

    // Allocate memory for the string contents
    char* str_buffer = (char*)Hunk_Alloc(size, "string");

    // Register the string at the chosen slot
    PR_SetStringAt(slot_index, str_buffer);

    // Return the allocated buffer pointer if requested
    if (out_ptr) {
        *out_ptr = str_buffer;
    }

    // Encode string reference: stored as -(index+1)
    return -(slot_index + 1);
}

// ============================================================================
// pr_exec.cpp -- QuakeC virtual machine execution engine
// ============================================================================

typedef struct {
    int s;
    dfunction_t* f;
} prstack_t;

#define MAX_STACK_DEPTH 32
prstack_t pr_stack[MAX_STACK_DEPTH];
int pr_depth;

#define LOCALSTACK_SIZE 2048
int localstack[LOCALSTACK_SIZE];
int localstack_used;

qboolean pr_trace;
dfunction_t* pr_xfunction;
int pr_xstatement;

int pr_argc;

const char* pr_opnames[] = {
    "DONE",

    "MUL_F", "MUL_V", "MUL_FV", "MUL_VF",

    "DIV",

    "ADD_F", "ADD_V",

    "SUB_F", "SUB_V",

    "EQ_F", "EQ_V", "EQ_S", "EQ_E", "EQ_FNC",

    "NE_F", "NE_V", "NE_S", "NE_E", "NE_FNC",

    "LE", "GE", "LT", "GT",

    "INDIRECT", "INDIRECT", "INDIRECT", "INDIRECT", "INDIRECT",
    "INDIRECT",

    "ADDRESS",

    "STORE_F", "STORE_V", "STORE_S", "STORE_ENT", "STORE_FLD",
    "STORE_FNC",

    "STOREP_F", "STOREP_V", "STOREP_S", "STOREP_ENT", "STOREP_FLD",
    "STOREP_FNC",

    "RETURN",

    "NOT_F", "NOT_V", "NOT_S", "NOT_ENT", "NOT_FNC",

    "IF", "IFNOT",

    "CALL0", "CALL1", "CALL2", "CALL3", "CALL4",
    "CALL5", "CALL6", "CALL7", "CALL8",

    "STATE",

    "GOTO",

    "AND", "OR",

    "BITAND", "BITOR"
};

char* PR_GlobalString(int ofs);
char* PR_GlobalStringNoContents(int ofs);

//=============================================================================

/*
=================
PR_PrintStatement
=================
*/
void PR_PrintStatement(dstatement_t* s)
{
    int i;

    if ((unsigned)s->op < sizeof(pr_opnames) / sizeof(pr_opnames[0])) {
        Con_Printf("%s ", pr_opnames[s->op]);
        i = (int)strlen(pr_opnames[s->op]);
        for (; i < 10; i++) {
            Con_Printf(" ");
        }
    }

    if (s->op == OP_IF || s->op == OP_IFNOT) {
        Con_Printf("%sbranch %i", PR_GlobalString(s->a), s->b);
    } else if (s->op == OP_GOTO) {
        Con_Printf("branch %i", s->a);
    } else if ((unsigned)(s->op - OP_STORE_F) < 6) {
        Con_Printf("%s", PR_GlobalString(s->a));
        Con_Printf("%s", PR_GlobalStringNoContents(s->b));
    } else {
        if (s->a) {
            Con_Printf("%s", PR_GlobalString(s->a));
        }

        if (s->b) {
            Con_Printf("%s", PR_GlobalString(s->b));
        }

        if (s->c) {
            Con_Printf("%s", PR_GlobalStringNoContents(s->c));
        }
    }

    Con_Printf("\n");
}

/*
============
PR_StackTrace
============
*/
void PR_StackTrace(void)
{
    dfunction_t* f;
    int i;

    if (pr_depth == 0) {
        Con_Printf("<NO STACK>\n");

        return;
    }

    pr_stack[pr_depth].f = pr_xfunction;
    for (i = pr_depth; i >= 0; i--) {
        f = pr_stack[i].f;

        if (!f) {
            Con_Printf("<NO FUNCTION>\n");
        } else {
            Con_Printf("%12s : %s\n", PR_GetString(f->s_file),
                PR_GetString(f->s_name));
        }
    }
}

/*
============
PR_Profile_f

============
*/
void PR_Profile_f(void)
{
    dfunction_t *f, *best;
    int max;
    int num;
    int i;

    num = 0;
    do {
        max = 0;
        best = NULL;
        for (i = 0; i < progs->numfunctions; i++) {
            f = &pr_functions[i];
            if (f->profile > max) {
                max = f->profile;
                best = f;
            }
        }
        if (best) {
            if (num < 10) {
                Con_Printf("%7i %s\n", best->profile, PR_GetString(best->s_name));
            }

            num++;
            best->profile = 0;
        }
    } while (best);
}

/*
============
PR_RunError

Aborts the currently executing function
============
*/
[[noreturn]] void PR_RunError(const char* error, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, error);
    vsprintf_s(string, sizeof(string), error, argptr);
    va_end(argptr);

    PR_PrintStatement(pr_statements + pr_xstatement);
    PR_StackTrace();
    Con_Printf("%s\n", string);

    pr_depth = 0; // dump the stack so host_error can shutdown functions

    Host_Error("Program error");
}

/*
============================================================================
PR_ExecuteProgram

The interpretation main loop
============================================================================
*/

/*
====================
PR_EnterFunction

Returns the new program statement counter
====================
*/
int PR_EnterFunction(dfunction_t* f)
{
    int i, j, c, o;

    pr_stack[pr_depth].s = pr_xstatement;
    pr_stack[pr_depth].f = pr_xfunction;
    pr_depth++;
    if (pr_depth >= MAX_STACK_DEPTH) {
        PR_RunError("stack overflow");
    }

    // save off any locals that the new function steps on
    c = f->locals;
    if (localstack_used + c > LOCALSTACK_SIZE) {
        PR_RunError("PR_ExecuteProgram: locals stack overflow\n");
    }

    for (i = 0; i < c; i++) {
        localstack[localstack_used + i] = ((int*)pr_globals)[f->parm_start + i];
    }
    localstack_used += c;

    // copy parameters
    o = f->parm_start;
    for (i = 0; i < f->numparms; i++) {
        for (j = 0; j < f->parm_size[i]; j++) {
            ((int*)pr_globals)[o] = ((int*)pr_globals)[OFS_PARM0 + i * 3 + j];
            o++;
        }
    }

    pr_xfunction = f;

    return f->first_statement - 1; // offset the s++
}

/*
====================
PR_LeaveFunction
====================
*/
int PR_LeaveFunction(void)
{
    int i, c;

    if (pr_depth <= 0) {
        Sys_Error("prog stack underflow");
    }

    // restore locals from the stack
    c = pr_xfunction->locals;
    localstack_used -= c;
    if (localstack_used < 0) {
        PR_RunError("PR_ExecuteProgram: locals stack underflow\n");
    }

    for (i = 0; i < c; i++) {
        ((int*)pr_globals)[pr_xfunction->parm_start + i] = localstack[localstack_used + i];
    }

    // up stack
    pr_depth--;
    pr_xfunction = pr_stack[pr_depth].f;

    return pr_stack[pr_depth].s;
}

/*
====================
PR_ExecuteProgram
====================
*/
void PR_ExecuteProgram(func_t fnum)
{
    eval_t *a, *b, *c;
    int s;
    dstatement_t* st;
    dfunction_t *f, *newf;
    int runaway;
    int i;
    edict_t* ed;
    int exitdepth;
    eval_t* ptr;

    if (!fnum || fnum >= progs->numfunctions) {
        if (pr_global_struct->self) {
            ED_Print(PROG_TO_EDICT(pr_global_struct->self));
        }

        Host_Error("PR_ExecuteProgram: NULL function");
    }

    f = &pr_functions[fnum];

    runaway = 100000;
    pr_trace = false;

    // make a stack frame
    exitdepth = pr_depth;

    s = PR_EnterFunction(f);

    while (1) {
        s++; // next statement

        st = &pr_statements[s];
        a = (eval_t*)&pr_globals[st->a];
        b = (eval_t*)&pr_globals[st->b];
        c = (eval_t*)&pr_globals[st->c];

        if (!--runaway) {
            PR_RunError("runaway loop error");
        }

        pr_xfunction->profile++;
        pr_xstatement = s;

        if (pr_trace) {
            PR_PrintStatement(st);
        }

        switch (st->op) {
        case OP_ADD_F:
            c->_float = a->_float + b->_float;
            break;
        case OP_ADD_V:
            c->vector[0] = a->vector[0] + b->vector[0];
            c->vector[1] = a->vector[1] + b->vector[1];
            c->vector[2] = a->vector[2] + b->vector[2];
            break;

        case OP_SUB_F:
            c->_float = a->_float - b->_float;
            break;
        case OP_SUB_V:
            c->vector[0] = a->vector[0] - b->vector[0];
            c->vector[1] = a->vector[1] - b->vector[1];
            c->vector[2] = a->vector[2] - b->vector[2];
            break;

        case OP_MUL_F:
            c->_float = a->_float * b->_float;
            break;
        case OP_MUL_V:
            c->_float = a->vector[0] * b->vector[0] + a->vector[1] * b->vector[1] + a->vector[2] * b->vector[2];
            break;
        case OP_MUL_FV:
            c->vector[0] = a->_float * b->vector[0];
            c->vector[1] = a->_float * b->vector[1];
            c->vector[2] = a->_float * b->vector[2];
            break;
        case OP_MUL_VF:
            c->vector[0] = b->_float * a->vector[0];
            c->vector[1] = b->_float * a->vector[1];
            c->vector[2] = b->_float * a->vector[2];
            break;

        case OP_DIV_F:
            c->_float = a->_float / b->_float;
            break;

        case OP_BITAND:
            c->_float = static_cast<float>(static_cast<int>(a->_float) & static_cast<int>(b->_float));
            break;

        case OP_BITOR:
            c->_float = static_cast<float>(static_cast<int>(a->_float) | static_cast<int>(b->_float));
            break;

        case OP_GE:
            c->_float = a->_float >= b->_float;
            break;
        case OP_LE:
            c->_float = a->_float <= b->_float;
            break;
        case OP_GT:
            c->_float = a->_float > b->_float;
            break;
        case OP_LT:
            c->_float = a->_float < b->_float;
            break;
        case OP_AND:
            c->_float = a->_float && b->_float;
            break;
        case OP_OR:
            c->_float = a->_float || b->_float;
            break;

        case OP_NOT_F:
            c->_float = !a->_float;
            break;
        case OP_NOT_V:
            c->_float = !a->vector[0] && !a->vector[1] && !a->vector[2];
            break;
        case OP_NOT_S:
            c->_float = !a->string || !*PR_GetString(a->string);
            break;
        case OP_NOT_FNC:
            c->_float = !a->function;
            break;
        case OP_NOT_ENT:
            c->_float = (PROG_TO_EDICT(a->edict) == sv.edicts);
            break;

        case OP_EQ_F:
            c->_float = a->_float == b->_float;
            break;
        case OP_EQ_V:
            c->_float = (a->vector[0] == b->vector[0]) && (a->vector[1] == b->vector[1]) && (a->vector[2] == b->vector[2]);
            break;
        case OP_EQ_S:
            c->_float = !strcmp(PR_GetString(a->string), PR_GetString(b->string));
            break;
        case OP_EQ_E:
            c->_float = a->_int == b->_int;
            break;
        case OP_EQ_FNC:
            c->_float = a->function == b->function;
            break;

        case OP_NE_F:
            c->_float = a->_float != b->_float;
            break;
        case OP_NE_V:
            c->_float = (a->vector[0] != b->vector[0]) || (a->vector[1] != b->vector[1]) || (a->vector[2] != b->vector[2]);
            break;
        case OP_NE_S:
            c->_float = static_cast<float>(strcmp(PR_GetString(a->string), PR_GetString(b->string)));
            break;
        case OP_NE_E:
            c->_float = a->_int != b->_int;
            break;
        case OP_NE_FNC:
            c->_float = a->function != b->function;
            break;

        //==================
        case OP_STORE_F:
        case OP_STORE_ENT:
        case OP_STORE_FLD: // integers
        case OP_STORE_S:
        case OP_STORE_FNC: // pointers
            b->_int = a->_int;
            break;
        case OP_STORE_V:
            b->vector[0] = a->vector[0];
            b->vector[1] = a->vector[1];
            b->vector[2] = a->vector[2];
            break;

        case OP_STOREP_F:
        case OP_STOREP_ENT:
        case OP_STOREP_FLD: // integers
        case OP_STOREP_S:
        case OP_STOREP_FNC: // pointers
            ptr = (eval_t*)((byte*)sv.edicts + b->_int);
            ptr->_int = a->_int;
            break;
        case OP_STOREP_V:
            ptr = (eval_t*)((byte*)sv.edicts + b->_int);
            ptr->vector[0] = a->vector[0];
            ptr->vector[1] = a->vector[1];
            ptr->vector[2] = a->vector[2];
            break;

        case OP_ADDRESS:
            ed = PROG_TO_EDICT(a->edict);
#ifdef PARANOID
            NUM_FOR_EDICT(ed); // make sure it's in range
#endif
            if (ed == (edict_t*)sv.edicts && sv.state == ss_active) {
                PR_RunError("assignment to world entity");
            }

            c->_int = static_cast<int>((byte*)((int*)&ed->v + b->_int) - (byte*)sv.edicts);
            break;

        case OP_LOAD_F:
        case OP_LOAD_FLD:
        case OP_LOAD_ENT:
        case OP_LOAD_S:
        case OP_LOAD_FNC:
            ed = PROG_TO_EDICT(a->edict);
#ifdef PARANOID
            NUM_FOR_EDICT(ed); // make sure it's in range
#endif
            a = (eval_t*)((int*)&ed->v + b->_int);
            c->_int = a->_int;
            break;

        case OP_LOAD_V:
            ed = PROG_TO_EDICT(a->edict);
#ifdef PARANOID
            NUM_FOR_EDICT(ed); // make sure it's in range
#endif
            a = (eval_t*)((int*)&ed->v + b->_int);
            c->vector[0] = a->vector[0];
            c->vector[1] = a->vector[1];
            c->vector[2] = a->vector[2];
            break;

            //==================

        case OP_IFNOT:
            if (!a->_int) {
                s += st->b - 1; // offset the s++
            }

            break;

        case OP_IF:
            if (a->_int) {
                s += st->b - 1; // offset the s++
            }

            break;

        case OP_GOTO:
            s += st->a - 1; // offset the s++
            break;

        case OP_CALL0:
        case OP_CALL1:
        case OP_CALL2:
        case OP_CALL3:
        case OP_CALL4:
        case OP_CALL5:
        case OP_CALL6:
        case OP_CALL7:
        case OP_CALL8:
            pr_argc = st->op - OP_CALL0;
            if (!a->function) {
                PR_RunError("NULL function");
            }

            newf = &pr_functions[a->function];

            if (newf->first_statement < 0) { // negative statements are built in functions
                i = -newf->first_statement;
                if (i >= pr_numbuiltins) {
                    PR_RunError("Bad builtin call number");
                }

                pr_builtins[i]();
                break;
            }

            s = PR_EnterFunction(newf);
            break;

        case OP_DONE:
        case OP_RETURN:
            pr_globals[OFS_RETURN] = pr_globals[st->a];
            pr_globals[OFS_RETURN + 1] = pr_globals[st->a + 1];
            pr_globals[OFS_RETURN + 2] = pr_globals[st->a + 2];

            s = PR_LeaveFunction();
            if (pr_depth == exitdepth) {
                return; // all done
            }

            break;

        case OP_STATE:
            ed = PROG_TO_EDICT(pr_global_struct->self);
#ifdef FPS_20
            ed->v.nextthink = pr_global_struct->time + 0.05f;
#else
            ed->v.nextthink = pr_global_struct->time + 0.1f;
#endif
            if (a->_float != ed->v.frame) {
                ed->v.frame = a->_float;
            }

            ed->v.think = b->function;
            break;

        default:
            PR_RunError("Bad opcode %i", st->op);
        }
    }
}

// ============================================================================
// pr_cmds.cpp -- built-in QuakeC function implementations
// ============================================================================

#define RETURN_EDICT(e) (((int*)pr_globals)[OFS_RETURN] = static_cast<int>(EDICT_TO_PROG(e)))

/*
===============================================================================

						BUILT-IN FUNCTIONS

===============================================================================
*/

char* PF_VarString(int first)
{
    int i;
    static char out[256];

    out[0] = 0;
    for (i = first; i < pr_argc; i++) {
        strcat_s(out, sizeof(out), G_STRING((OFS_PARM0 + i * 3)));
    }

    return out;
}

/*
=================
PF_errror

This is a TERMINAL error, which will kill off the entire server.
Dumps self.

error(value)
=================
*/
void PF_error(void)
{
    char* s;
    edict_t* ed;

    s = PF_VarString(0);
    Con_Printf("======SERVER ERROR in %s:\n%s\n",
        PR_GetString(pr_xfunction->s_name), s);
    ed = PROG_TO_EDICT(pr_global_struct->self);
    ED_Print(ed);

    Host_Error("Program error");
}

/*
=================
PF_objerror

Dumps out self, then an error message.  The program is aborted and self is
removed, but the level can continue.

objerror(value)
=================
*/
void PF_objerror(void)
{
    char* s;
    edict_t* ed;

    s = PF_VarString(0);
    Con_Printf("======OBJECT ERROR in %s:\n%s\n",
        PR_GetString(pr_xfunction->s_name), s);
    ed = PROG_TO_EDICT(pr_global_struct->self);
    ED_Print(ed);
    ED_Free(ed);

    Host_Error("Program error");
}

/*
==============
PF_makevectors

Writes new values for v_forward, v_up, and v_right based on angles
makevectors(vector)
==============
*/
void PF_makevectors(void)
{
    AngleVectors(G_VECTOR(OFS_PARM0), pr_global_struct->v_forward,
        pr_global_struct->v_right, pr_global_struct->v_up);
}

/*
=================
PF_setorigin

This is the only valid way to move an object without using the physics of the world (setting velocity and waiting).  Directly changing origin will not set internal links correctly, so clipping would be messed up.  This should be called when an object is spawned, and then only if it is teleported.

setorigin (entity, origin)
=================
*/
void PF_setorigin(void)
{
    edict_t* e;
    float* org;

    e = G_EDICT(OFS_PARM0);
    org = G_VECTOR(OFS_PARM1);
    VectorCopy(org, e->v.origin);
    SV_LinkEdict(e, false);
}

void SetMinMaxSize(edict_t* e, float* min, float* max, qboolean rotate)
{
    float* angles;
    Vector3 rmin, rmax;
    float bounds[2][3];
    float xvector[2], yvector[2];
    float a;
    Vector3 base, transformed;
    int i, j, k, l;

    for (i = 0; i < 3; i++) {
        if (min[i] > max[i]) {
            PR_RunError("backwards mins/maxs");
        }
    }

    rotate = false; // FIXME: implement rotation properly again

    if (!rotate) {
        rmin = Vector3(min);
        rmax = Vector3(max);
    } else {
        // find min / max for rotations
        angles = e->v.angles;

        a = angles[1] / 180.0f * static_cast<float>(M_PI);

        xvector[0] = cos(a);
        xvector[1] = sin(a);
        yvector[0] = -sin(a);
        yvector[1] = cos(a);

        VectorCopy(min, bounds[0]);
        VectorCopy(max, bounds[1]);

        rmin[0] = rmin[1] = rmin[2] = 9999;
        rmax[0] = rmax[1] = rmax[2] = -9999;

        for (i = 0; i <= 1; i++) {
            base[0] = bounds[i][0];
            for (j = 0; j <= 1; j++) {
                base[1] = bounds[j][1];
                for (k = 0; k <= 1; k++) {
                    base[2] = bounds[k][2];

                    // transform the point
                    transformed[0] = xvector[0] * base[0] + yvector[0] * base[1];
                    transformed[1] = xvector[1] * base[0] + yvector[1] * base[1];
                    transformed[2] = base[2];

                    for (l = 0; l < 3; l++) {
                        if (transformed[l] < rmin[l]) {
                            rmin[l] = transformed[l];
                        }

                        if (transformed[l] > rmax[l]) {
                            rmax[l] = transformed[l];
                        }
                    }
                }
            }
        }
    }

    // set derived values
    e->v.mins = rmin;
    e->v.maxs = rmax;
    e->v.size = Vector3(max) - Vector3(min);

    SV_LinkEdict(e, false);
}

/*
=================
PF_setsize

the size box is rotated by the current angle

setsize (entity, minvector, maxvector)
=================
*/
void PF_setsize(void)
{
    edict_t* e;
    float *min, *max;

    e = G_EDICT(OFS_PARM0);
    min = G_VECTOR(OFS_PARM1);
    max = G_VECTOR(OFS_PARM2);
    SetMinMaxSize(e, min, max, false);
}

/*
=================
PF_setmodel

setmodel(entity, model)
=================
*/
void PF_setmodel(void)
{
    edict_t* e;
    char *m, **check;
    model_t* mod;
    int i;

    e = G_EDICT(OFS_PARM0);
    m = G_STRING(OFS_PARM1);

    // check to see if model was properly precached
    for (i = 0, check = sv.model_precache; *check; i++, check++) {
        if (!strcmp(*check, m)) {
            break;
        }
    }

    if (!*check) {
        PR_RunError("no precache: %s\n", m);
    }

    e->v.model = PR_SetString(m);
    e->v.modelindex = static_cast<float>(i); //SV_ModelIndex (m);

    mod = sv.models[(int)e->v.modelindex]; // Mod_ForName (m, true);

    if (mod) {
        SetMinMaxSize(e, mod->mins, mod->maxs, true);
    } else {
        SetMinMaxSize(e, vec3_origin, vec3_origin, true);
    }
}

/*
=================
PF_bprint

broadcast print to everyone on server

bprint(value)
=================
*/
void PF_bprint(void)
{
    char* s;

    s = PF_VarString(0);
    SV_BroadcastPrintf("%s", s);
}

/*
=================
PF_sprint

single print to a specific client

sprint(clientent, value)
=================
*/
void PF_sprint(void)
{
    char* s;
    client_t* client;
    int entnum;

    entnum = G_EDICTNUM(OFS_PARM0);
    s = PF_VarString(1);

    if (entnum < 1 || entnum > svs.maxclients) {
        Con_Printf("tried to sprint to a non-client\n");

        return;
    }

    client = &svs.clients[entnum - 1];

    MSG_WriteChar(&client->message, svc_print);
    MSG_WriteString(&client->message, s);
}

/*
=================
PF_centerprint

single print to a specific client

centerprint(clientent, value)
=================
*/
void PF_centerprint(void)
{
    char* s;
    client_t* client;
    int entnum;

    entnum = G_EDICTNUM(OFS_PARM0);
    s = PF_VarString(1);

    if (entnum < 1 || entnum > svs.maxclients) {
        Con_Printf("tried to sprint to a non-client\n");

        return;
    }

    client = &svs.clients[entnum - 1];

    MSG_WriteChar(&client->message, svc_centerprint);
    MSG_WriteString(&client->message, s);
}

/*
=================
PF_normalize

vector normalize(vector)
=================
*/
void PF_normalize(void)
{
    float* value1;
    Vector3 newvalue;
    float new_val;

    value1 = G_VECTOR(OFS_PARM0);

    new_val = value1[0] * value1[0] + value1[1] * value1[1] + value1[2] * value1[2];
    new_val = sqrt(new_val);

    if (new_val == 0) {
        newvalue[0] = newvalue[1] = newvalue[2] = 0;
    } else {
        new_val = 1 / new_val;
        newvalue[0] = value1[0] * new_val;
        newvalue[1] = value1[1] * new_val;
        newvalue[2] = value1[2] * new_val;
    }

    VectorCopy(newvalue, G_VECTOR(OFS_RETURN));
}

/*
=================
PF_vlen

scalar vlen(vector)
=================
*/
void PF_vlen(void)
{
    float* value1;
    float new_val;

    value1 = G_VECTOR(OFS_PARM0);

    new_val = value1[0] * value1[0] + value1[1] * value1[1] + value1[2] * value1[2];
    new_val = sqrt(new_val);

    G_FLOAT(OFS_RETURN) = new_val;
}

/*
=================
PF_vectoyaw

float vectoyaw(vector)
=================
*/
void PF_vectoyaw(void)
{
    float* value1;
    float yaw;

    value1 = G_VECTOR(OFS_PARM0);

    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
    } else {
        yaw = static_cast<float>(static_cast<int>(atan2(value1[1], value1[0]) * 180.0 / M_PI));
        if (yaw < 0) {
            yaw += 360;
        }
    }

    G_FLOAT(OFS_RETURN) = yaw;
}

/*
=================
PF_vectoangles

vector vectoangles(vector)
=================
*/
void PF_vectoangles(void)
{
    float* value1;
    float forward;
    float yaw, pitch;

    value1 = G_VECTOR(OFS_PARM0);

    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
        if (value1[2] > 0) {
            pitch = 90;
        } else {
            pitch = 270;
        }
    } else {
        yaw = static_cast<float>(static_cast<int>(atan2(value1[1], value1[0]) * 180.0 / M_PI));
        if (yaw < 0) {
            yaw += 360;
        }

        forward = sqrt(value1[0] * value1[0] + value1[1] * value1[1]);
        pitch = static_cast<float>(static_cast<int>(atan2(value1[2], forward) * 180.0 / M_PI));
        if (pitch < 0) {
            pitch += 360;
        }
    }

    G_FLOAT(OFS_RETURN + 0) = pitch;
    G_FLOAT(OFS_RETURN + 1) = yaw;
    G_FLOAT(OFS_RETURN + 2) = 0;
}

/*
=================
PF_Random

Returns a number from 0<= num < 1

random()
=================
*/
void PF_random(void)
{
    float num;

    num = (rand() & 0x7fff) / ((float)0x7fff);

    G_FLOAT(OFS_RETURN) = num;
}

/*
=================
PF_particle

particle(origin, color, count)
=================
*/
void PF_particle(void)
{
    float *org, *dir;
    float color;
    float count;

    org = G_VECTOR(OFS_PARM0);
    dir = G_VECTOR(OFS_PARM1);
    color = G_FLOAT(OFS_PARM2);
    count = G_FLOAT(OFS_PARM3);
    SV_StartParticle(org, dir, static_cast<int>(color), static_cast<int>(count));
}

/*
=================
PF_ambientsound

=================
*/
void PF_ambientsound(void)
{
    char** check;
    char* samp;
    float* pos;
    float vol, attenuation;
    int i, soundnum;

    pos = G_VECTOR(OFS_PARM0);
    samp = G_STRING(OFS_PARM1);
    vol = G_FLOAT(OFS_PARM2);
    attenuation = G_FLOAT(OFS_PARM3);

    // check to see if samp was properly precached
    for (soundnum = 0, check = sv.sound_precache; *check; check++, soundnum++) {
        if (!strcmp(*check, samp)) {
            break;
        }
    }

    if (!*check) {
        Con_Printf("no precache: %s\n", samp);

        return;
    }

    // add an svc_spawnambient command to the level signon packet

    MSG_WriteByte(&sv.signon, svc_spawnstaticsound);
    for (i = 0; i < 3; i++) {
        MSG_WriteCoord(&sv.signon, pos[i]);
    }

    MSG_WriteByte(&sv.signon, soundnum);

    MSG_WriteByte(&sv.signon, static_cast<int>(vol * 255.0f));
    MSG_WriteByte(&sv.signon, static_cast<int>(attenuation * 64.0f));
}

/*
=================
PF_sound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.

=================
*/
void PF_sound(void)
{
    char* sample;
    int channel;
    edict_t* entity;
    int vol;
    float attenuation;

    entity = G_EDICT(OFS_PARM0);
    channel = static_cast<int>(G_FLOAT(OFS_PARM1));
    sample = G_STRING(OFS_PARM2);
    vol = (int)(G_FLOAT(OFS_PARM3) * 255);
    attenuation = G_FLOAT(OFS_PARM4);

    if (vol < 0 || vol > 255) {
        Sys_Error("SV_StartSound: volume = %i", vol);
    }

    if (attenuation < 0 || attenuation > 4) {
        Sys_Error("SV_StartSound: attenuation = %f", attenuation);
    }

    if (channel < 0 || channel > 7) {
        Sys_Error("SV_StartSound: channel = %i", channel);
    }

    SV_StartSound(entity, channel, sample, vol, attenuation);
}

/*
=================
PF_break

break()
=================
*/
void PF_break(void)
{
    Con_Printf("break statement\n");
    *(int*)-4 = 0; // dump to debugger
                   //	PR_RunError ("break statement");
}

/*
=================
PF_traceline

Used for use tracing and shot targeting
Traces are blocked by bbox and exact bsp entityes, and also slide box entities
if the tryents flag is set.

traceline (vector1, vector2, tryents)
=================
*/
void PF_traceline(void)
{
    float *v1, *v2;
    trace_t trace;
    int no_monsters;
    edict_t* ent;

    v1 = G_VECTOR(OFS_PARM0);
    v2 = G_VECTOR(OFS_PARM1);
    no_monsters = static_cast<int>(G_FLOAT(OFS_PARM2));
    ent = G_EDICT(OFS_PARM3);

    trace = SV_Move(v1, vec3_origin, vec3_origin, v2, no_monsters, ent);

    pr_global_struct->trace_allsolid = trace.allsolid;
    pr_global_struct->trace_startsolid = trace.startsolid;
    pr_global_struct->trace_fraction = trace.fraction;
    pr_global_struct->trace_inwater = trace.inwater;
    pr_global_struct->trace_inopen = trace.inopen;
    VectorCopy(trace.endpos, pr_global_struct->trace_endpos);
    VectorCopy(trace.plane.normal, pr_global_struct->trace_plane_normal);
    pr_global_struct->trace_plane_dist = trace.plane.dist;
    if (trace.ent) {
        pr_global_struct->trace_ent = static_cast<int>(EDICT_TO_PROG(trace.ent));
    } else {
        pr_global_struct->trace_ent = static_cast<int>(EDICT_TO_PROG(sv.edicts));
    }
}


//============================================================================

byte checkpvs[MAX_MAP_LEAFS / 8];

int PF_newcheckclient(int check)
{
    int i;
    byte* pvs;
    edict_t* ent;
    mleaf_t* leaf;
    Vector3 org;

    // cycle to the next one

    if (check < 1) {
        check = 1;
    }

    if (check > svs.maxclients) {
        check = svs.maxclients;
    }

    if (check == svs.maxclients) {
        i = 1;
    } else {
        i = check + 1;
    }

    for (;; i++) {
        if (i == svs.maxclients + 1) {
            i = 1;
        }

        ent = EDICT_NUM(i);

        if (i == check) {
            break; // didn't find anything else
        }

        if (ent->free) {
            continue;
        }

        if (ent->v.health <= 0) {
            continue;
        }

        if ((int)ent->v.flags & FL_NOTARGET) {
            continue;
        }

        // anything that is a client, or has a client as an enemy
        break;
    }

    // get the PVS for the entity
    org = ent->v.origin + ent->v.view_ofs;
    leaf = Mod_PointInLeaf(org, sv.worldmodel);
    pvs = Mod_LeafPVS(leaf, sv.worldmodel);
    memcpy(checkpvs, pvs, (sv.worldmodel->numleafs + 7) >> 3);

    return i;
}

/*
=================
PF_checkclient

Returns a client (or object that has a client enemy) that would be a
valid target.

If there are more than one valid options, they are cycled each frame

If (self.origin + self.viewofs) is not in the PVS of the current target,
it is not returned at all.

name checkclient ()
=================
*/
#define MAX_CHECK 16
int c_invis, c_notvis;

void PF_checkclient(void)
{
    edict_t *ent, *self;
    mleaf_t* leaf;
    int l;
    Vector3 view;

    // find a new check if on a new frame
    if (sv.time - sv.lastchecktime >= 0.1) {
        sv.lastcheck = PF_newcheckclient(sv.lastcheck);
        sv.lastchecktime = sv.time;
    }

    // return check if it might be visible
    ent = EDICT_NUM(sv.lastcheck);
    if (ent->free || ent->v.health <= 0) {
        RETURN_EDICT(sv.edicts);

        return;
    }

    // if current entity can't possibly see the check entity, return 0
    self = PROG_TO_EDICT(pr_global_struct->self);
    view = self->v.origin + self->v.view_ofs;
    leaf = Mod_PointInLeaf(view, sv.worldmodel);
    l = static_cast<int>((leaf - sv.worldmodel->leafs) - 1);
    if ((l < 0) || !(checkpvs[l >> 3] & (1 << (l & 7)))) {
        c_notvis++;
        RETURN_EDICT(sv.edicts);

        return;
    }

    // might be able to see it
    c_invis++;
    RETURN_EDICT(ent);
}

//============================================================================

/*
=================
PF_stuffcmd

Sends text over to the client's execution buffer

stuffcmd (clientent, value)
=================
*/
void PF_stuffcmd(void)
{
    int entnum;
    char* str;
    client_t* old;

    entnum = G_EDICTNUM(OFS_PARM0);
    if (entnum < 1 || entnum > svs.maxclients) {
        PR_RunError("Parm 0 not a client");
    }

    str = G_STRING(OFS_PARM1);

    old = host_client;
    host_client = &svs.clients[entnum - 1];
    Host_ClientCommands("%s", str);
    host_client = old;
}

/*
=================
PF_localcmd

Sends text over to the client's execution buffer

localcmd (string)
=================
*/
void PF_localcmd(void)
{
    char* str;

    str = G_STRING(OFS_PARM0);
    Cmd::BufferAddText(str);
}

/*
=================
PF_cvar

float cvar (string)
=================
*/
void PF_cvar(void)
{
    char* str;

    str = G_STRING(OFS_PARM0);

    G_FLOAT(OFS_RETURN) = Cvar::VariableValue(str);
}

/*
=================
PF_cvar_set

float cvar (string)
=================
*/
void PF_cvar_set(void)
{
    char *var, *val;

    var = G_STRING(OFS_PARM0);
    val = G_STRING(OFS_PARM1);

    Cvar::Set(var, val);
}

/*
=================
PF_findradius

Returns a chain of entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
void PF_findradius(void)
{
    edict_t *ent, *chain;
    float rad;
    float* org;
    Vector3 eorg;
    int i;

    chain = (edict_t*)sv.edicts;

    org = G_VECTOR(OFS_PARM0);
    rad = G_FLOAT(OFS_PARM1);

    ent = NEXT_EDICT(sv.edicts);
    for (i = 1; i < sv.num_edicts; i++, ent = NEXT_EDICT(ent)) {
        if (ent->free) {
            continue;
        }

        if (ent->v.solid == SOLID_NOT) {
            continue;
        }

        eorg = Vector3(org) - (ent->v.origin + (ent->v.mins + ent->v.maxs) * 0.5f);
        if (eorg.length() > rad) {
            continue;
        }

        ent->v.chain = static_cast<int>(EDICT_TO_PROG(chain));
        chain = ent;
    }

    RETURN_EDICT(chain);
}

/*
=========
PF_dprint
=========
*/
void PF_dprint(void)
{
    Con_DPrintf("%s", PF_VarString(0));
}

char pr_string_temp[128];

void PF_ftos(void)
{
    float v;
    v = G_FLOAT(OFS_PARM0);

    if (v == (int)v) {
        sprintf_s(pr_string_temp, sizeof(pr_string_temp), "%d", (int)v);
    } else {
        sprintf_s(pr_string_temp, sizeof(pr_string_temp), "%5.1f", v);
    }

    G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);
}

void PF_fabs(void)
{
    float v;
    v = G_FLOAT(OFS_PARM0);
    G_FLOAT(OFS_RETURN) = static_cast<float>(fabs(v));
}

void PF_vtos(void)
{
    sprintf_s(pr_string_temp, sizeof(pr_string_temp), "'%5.1f %5.1f %5.1f'", G_VECTOR(OFS_PARM0)[0],
        G_VECTOR(OFS_PARM0)[1], G_VECTOR(OFS_PARM0)[2]);
    G_INT(OFS_RETURN) = PR_SetString(pr_string_temp);
}


void PF_Spawn(void)
{
    edict_t* ed;
    ed = ED_Alloc();
    RETURN_EDICT(ed);
}

void PF_Remove(void)
{
    edict_t* ed;

    ed = G_EDICT(OFS_PARM0);
    ED_Free(ed);
}

// entity (entity start, .string field, string match) find = #5;
void PF_Find(void)
{
    int e;
    int f;
    char *s, *t;
    edict_t* ed;

    e = G_EDICTNUM(OFS_PARM0);
    f = G_INT(OFS_PARM1);
    s = G_STRING(OFS_PARM2);
    if (!s) {
        PR_RunError("PF_Find: bad search string");
    }

    for (e++; e < sv.num_edicts; e++) {
        ed = EDICT_NUM(e);
        if (ed->free) {
            continue;
        }

        t = E_STRING(ed, f);
        if (!t) {
            continue;
        }

        if (!strcmp(t, s)) {
            RETURN_EDICT(ed);

            return;
        }
    }

    RETURN_EDICT(sv.edicts);
}

void PR_CheckEmptyString(char* s)
{
    if (s[0] <= ' ') {
        PR_RunError("Bad string");
    }
}

void PF_precache_file(
    void)
{ // precache_file is only used to copy files with qcc, it does nothing
    G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
}

void PF_precache_sound(void)
{
    char* s;
    int i;

    if (sv.state != ss_loading) {
        PR_RunError("PF_Precache_*: Precache can only be done in spawn functions");
    }

    s = G_STRING(OFS_PARM0);
    G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
    PR_CheckEmptyString(s);

    for (i = 0; i < MAX_SOUNDS; i++) {
        if (!sv.sound_precache[i]) {
            sv.sound_precache[i] = s;

            return;
        }

        if (!strcmp(sv.sound_precache[i], s)) {
            return;
        }
    }
    PR_RunError("PF_precache_sound: overflow");
}

void PF_precache_model(void)
{
    char* s;
    int i;

    if (sv.state != ss_loading) {
        PR_RunError("PF_Precache_*: Precache can only be done in spawn functions");
    }

    s = G_STRING(OFS_PARM0);
    G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
    PR_CheckEmptyString(s);

    for (i = 0; i < MAX_MODELS; i++) {
        if (!sv.model_precache[i]) {
            sv.model_precache[i] = s;
            sv.models[i] = Mod_ForName(s, true);

            return;
        }

        if (!strcmp(sv.model_precache[i], s)) {
            return;
        }
    }
    PR_RunError("PF_precache_model: overflow");
}

void PF_coredump(void)
{
    ED_PrintEdicts();
}

void PF_traceon(void)
{
    pr_trace = true;
}

void PF_traceoff(void)
{
    pr_trace = false;
}

void PF_eprint(void)
{
    ED_PrintNum(G_EDICTNUM(OFS_PARM0));
}

/*
===============
PF_walkmove

float(float yaw, float dist) walkmove
===============
*/
void PF_walkmove(void)
{
    edict_t* ent;
    float yaw, dist;
    Vector3 move;
    dfunction_t* oldf;
    int oldself;

    ent = PROG_TO_EDICT(pr_global_struct->self);
    yaw = G_FLOAT(OFS_PARM0);
    dist = G_FLOAT(OFS_PARM1);

    if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        G_FLOAT(OFS_RETURN) = 0;

        return;
    }

    yaw = yaw * static_cast<float>(M_PI) * 2.0f / 360.0f;

    move.x = cos(yaw) * dist;
    move.y = sin(yaw) * dist;
    move.z = 0;

    // save program state, because SV_movestep may call other progs
    oldf = pr_xfunction;
    oldself = pr_global_struct->self;

    G_FLOAT(OFS_RETURN) = SV_movestep(ent, move, true);

    // restore program state
    pr_xfunction = oldf;
    pr_global_struct->self = oldself;
}

/*
===============
PF_droptofloor

void() droptofloor
===============
*/
void PF_droptofloor(void)
{
    edict_t* ent;
    Vector3 end;
    trace_t trace;

    ent = PROG_TO_EDICT(pr_global_struct->self);

    end = ent->v.origin;
    end.z -= 256;

    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

    if (trace.fraction == 1 || trace.allsolid) {
        G_FLOAT(OFS_RETURN) = 0;
    } else {
        ent->v.origin = trace.endpos;
        SV_LinkEdict(ent, false);
        ent->v.flags = static_cast<float>(static_cast<int>(ent->v.flags) | FL_ONGROUND);
        ent->v.groundentity = static_cast<int>(EDICT_TO_PROG(trace.ent));
        G_FLOAT(OFS_RETURN) = 1;
    }
}

/*
===============
PF_lightstyle

void(float style, string value) lightstyle
===============
*/
void PF_lightstyle(void)
{
    int style;
    char* val;
    client_t* client;
    int j;

    style = static_cast<int>(G_FLOAT(OFS_PARM0));
    val = G_STRING(OFS_PARM1);

    // change the string in sv
    sv.lightstyles[style] = val;

    // send message to all clients on this server
    if (sv.state != ss_active) {
        return;
    }

    for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
        if (client->active || client->spawned) {
            MSG_WriteChar(&client->message, svc_lightstyle);
            MSG_WriteChar(&client->message, style);
            MSG_WriteString(&client->message, val);
        }
    }
}

void PF_rint(void)
{
    float f;
    f = G_FLOAT(OFS_PARM0);
    if (f > 0) {
        G_FLOAT(OFS_RETURN) = static_cast<float>(static_cast<int>(f + 0.5f));
    } else {
        G_FLOAT(OFS_RETURN) = static_cast<float>(static_cast<int>(f - 0.5f));
    }
}

void PF_floor(void)
{
    G_FLOAT(OFS_RETURN) = floor(G_FLOAT(OFS_PARM0));
}

void PF_ceil(void)
{
    G_FLOAT(OFS_RETURN) = ceil(G_FLOAT(OFS_PARM0));
}

/*
=============
PF_checkbottom
=============
*/
void PF_checkbottom(void)
{
    edict_t* ent;

    ent = G_EDICT(OFS_PARM0);

    G_FLOAT(OFS_RETURN) = SV_CheckBottom(ent);
}

/*
=============
PF_pointcontents
=============
*/
void PF_pointcontents(void)
{
    float* v;

    v = G_VECTOR(OFS_PARM0);

    G_FLOAT(OFS_RETURN) = static_cast<float>(SV_PointContents(v));
}

/*
=============
PF_nextent

entity nextent(entity)
=============
*/
void PF_nextent(void)
{
    int i;
    edict_t* ent;

    i = G_EDICTNUM(OFS_PARM0);
    while (1) {
        i++;
        if (i == sv.num_edicts) {
            RETURN_EDICT(sv.edicts);

            return;
        }

        ent = EDICT_NUM(i);
        if (!ent->free) {
            RETURN_EDICT(ent);

            return;
        }
    }
}

/*
=============
PF_aim

Pick a vector for the player to shoot along
vector aim(entity, missilespeed)
=============
*/
cvar_t sv_aim = { "sv_aim", "0.93" };

void PF_aim(void)
{
    edict_t *ent, *check, *bestent;
    Vector3 start, dir, end, bestdir;
    int i;
    trace_t tr;
    float dist, bestdist;
    float speed;

    ent = G_EDICT(OFS_PARM0);
    speed = G_FLOAT(OFS_PARM1);

    start = ent->v.origin;
    start.z += 20;

    // try sending a trace straight
    dir = pr_global_struct->v_forward;
    end = start + dir * 2048.0f;
    tr = SV_Move(start, vec3_origin, vec3_origin, end, false, ent);
    if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM && (!teamplay.value || ent->v.team <= 0 || ent->v.team != tr.ent->v.team)) {
        VectorCopy(pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));

        return;
    }

    // try all possible entities
    bestdir = dir;
    bestdist = sv_aim.value;
    bestent = NULL;

    check = NEXT_EDICT(sv.edicts);
    for (i = 1; i < sv.num_edicts; i++, check = NEXT_EDICT(check)) {
        if (check->v.takedamage != DAMAGE_AIM) {
            continue;
        }

        if (check == ent) {
            continue;
        }

        if (teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team) {
            continue; // don't aim at teammate
        }

        end = check->v.origin + (check->v.mins + check->v.maxs) * 0.5f;
        dir = end - start;
        dir.normalize();
        dist = dir.dot(pr_global_struct->v_forward);
        if (dist < bestdist) {
            continue; // to far to turn
        }

        tr = SV_Move(start, vec3_origin, vec3_origin, end, false, ent);
        if (tr.ent == check) { // can shoot at this one
            bestdist = dist;
            bestent = check;
        }
    }

    if (bestent) {
        dir = bestent->v.origin - ent->v.origin;
        dist = dir.dot(pr_global_struct->v_forward);
        end = pr_global_struct->v_forward * dist;
        end.z = dir.z;
        end.normalize();
        VectorCopy(end, G_VECTOR(OFS_RETURN));
    } else {
        VectorCopy(bestdir, G_VECTOR(OFS_RETURN));
    }
}

/*
==============
PF_changeyaw

This was a major timewaster in progs, so it was converted to C
==============
*/
void PF_changeyaw(void)
{
    edict_t* ent;
    float ideal, current, move, speed;

    ent = PROG_TO_EDICT(pr_global_struct->self);
    current = anglemod(ent->v.angles[1]);
    ideal = ent->v.ideal_yaw;
    speed = ent->v.yaw_speed;

    if (current == ideal) {
        return;
    }

    move = ideal - current;
    if (ideal > current) {
        if (move >= 180) {
            move = move - 360;
        }
    } else {
        if (move <= -180) {
            move = move + 360;
        }
    }

    if (move > 0) {
        if (move > speed) {
            move = speed;
        }
    } else {
        if (move < -speed) {
            move = -speed;
        }
    }

    ent->v.angles[1] = anglemod(current + move);
}


/*
===============================================================================

MESSAGE WRITING

===============================================================================
*/

#define MSG_BROADCAST 0 // unreliable to all
#define MSG_ONE 1       // reliable to one (msg_entity)
#define MSG_ALL 2       // reliable to all
#define MSG_INIT 3      // write to the init string

sizebuf_t* WriteDest(void)
{
    int entnum;
    int dest;
    edict_t* ent;

    dest = static_cast<int>(G_FLOAT(OFS_PARM0));
    switch (dest) {
    case MSG_BROADCAST:
        return &sv.datagram;

    case MSG_ONE:
        ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
        entnum = NUM_FOR_EDICT(ent);
        if (entnum < 1 || entnum > svs.maxclients) {
            PR_RunError("WriteDest: not a client");
        }

        return &svs.clients[entnum - 1].message;

    case MSG_ALL:
        return &sv.reliable_datagram;

    case MSG_INIT:
        return &sv.signon;

    default:
        PR_RunError("WriteDest: bad destination");
    }
}

inline void PF_WriteByte(void)
{
    MSG_WriteByte(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}

inline void PF_WriteChar(void)
{
    MSG_WriteChar(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}

inline void PF_WriteShort(void)
{
    MSG_WriteShort(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}

inline void PF_WriteLong(void)
{
    MSG_WriteLong(WriteDest(), static_cast<int>(G_FLOAT(OFS_PARM1)));
}

inline void PF_WriteAngle(void)
{
    MSG_WriteAngle(WriteDest(), G_FLOAT(OFS_PARM1));
}

inline void PF_WriteCoord(void)
{
    MSG_WriteCoord(WriteDest(), G_FLOAT(OFS_PARM1));
}

inline void PF_WriteString(void)
{
    MSG_WriteString(WriteDest(), G_STRING(OFS_PARM1));
}

inline void PF_WriteEntity(void)
{
    MSG_WriteShort(WriteDest(), G_EDICTNUM(OFS_PARM1));
}

//=============================================================================

// SV_ModelIndex declared in server.hpp

void PF_makestatic(void)
{
    edict_t* ent;
    int i;

    ent = G_EDICT(OFS_PARM0);

    MSG_WriteByte(&sv.signon, svc_spawnstatic);

    MSG_WriteByte(&sv.signon, SV_ModelIndex(PR_GetString(ent->v.model)));

    MSG_WriteByte(&sv.signon, static_cast<int>(ent->v.frame));
    MSG_WriteByte(&sv.signon, static_cast<int>(ent->v.colormap));
    MSG_WriteByte(&sv.signon, static_cast<int>(ent->v.skin));
    for (i = 0; i < 3; i++) {
        MSG_WriteCoord(&sv.signon, ent->v.origin[i]);
        MSG_WriteAngle(&sv.signon, ent->v.angles[i]);
    }

    // throw the entity away now
    ED_Free(ent);
}

//=============================================================================

/*
==============
PF_setspawnparms
==============
*/
void PF_setspawnparms(void)
{
    edict_t* ent;
    int i;
    client_t* client;

    ent = G_EDICT(OFS_PARM0);
    i = NUM_FOR_EDICT(ent);
    if (i < 1 || i > svs.maxclients) {
        PR_RunError("Entity is not a client");
    }

    // copy spawn parms out of the client_t
    client = svs.clients + (i - 1);

    for (i = 0; i < NUM_SPAWN_PARMS; i++) {
        (&pr_global_struct->parm1)[i] = client->spawn_parms[i];
    }
}

/*
==============
PF_changelevel
==============
*/
void PF_changelevel(void)
{
    char* s;

    // make sure we don't issue two changelevels
    if (svs.changelevel_issued) {
        return;
    }

    svs.changelevel_issued = true;

    s = G_STRING(OFS_PARM0);
    Cmd::BufferAddText(va("changelevel %s\n", s));
}


inline void PF_Fixme(void)
{
    PR_RunError("unimplemented bulitin");
}

builtin_t pr_builtin[] = {
    PF_Fixme,
    PF_makevectors,    // void(entity e)	makevectors 		= #1;
    PF_setorigin,      // void(entity e, vector o) setorigin	= #2;
    PF_setmodel,       // void(entity e, string m) setmodel	= #3;
    PF_setsize,        // void(entity e, vector min, vector max) setsize = #4;
    PF_Fixme,          // void(entity e, vector min, vector max) setabssize = #5;
    PF_break,          // void() break						= #6;
    PF_random,         // float() random						= #7;
    PF_sound,          // void(entity e, float chan, string samp) sound = #8;
    PF_normalize,      // vector(vector v) normalize			= #9;
    PF_error,          // void(string e) error				= #10;
    PF_objerror,       // void(string e) objerror				= #11;
    PF_vlen,           // float(vector v) vlen				= #12;
    PF_vectoyaw,       // float(vector v) vectoyaw		= #13;
    PF_Spawn,          // entity() spawn						= #14;
    PF_Remove,         // void(entity e) remove				= #15;
    PF_traceline,      // float(vector v1, vector v2, float tryents) traceline = #16;
    PF_checkclient,    // entity() clientlist					= #17;
    PF_Find,           // entity(entity start, .string fld, string match) find = #18;
    PF_precache_sound, // void(string s) precache_sound		= #19;
    PF_precache_model, // void(string s) precache_model		= #20;
    PF_stuffcmd,       // void(entity client, string s)stuffcmd = #21;
    PF_findradius,     // entity(vector org, float rad) findradius = #22;
    PF_bprint,         // void(string s) bprint				= #23;
    PF_sprint,         // void(entity client, string s) sprint = #24;
    PF_dprint,         // void(string s) dprint				= #25;
    PF_ftos,           // void(string s) ftos				= #26;
    PF_vtos,           // void(string s) vtos				= #27;
    PF_coredump,
    PF_traceon,
    PF_traceoff,
    PF_eprint,   // void(entity e) debug print an entire entity
    PF_walkmove, // float(float yaw, float dist) walkmove
    PF_Fixme,    // float(float yaw, float dist) walkmove
    PF_droptofloor,
    PF_lightstyle,
    PF_rint,
    PF_floor,
    PF_ceil,
    PF_Fixme,
    PF_checkbottom,
    PF_pointcontents,
    PF_Fixme,
    PF_fabs,
    PF_aim,
    PF_cvar,
    PF_localcmd,
    PF_nextent,
    PF_particle,
    PF_changeyaw,
    PF_Fixme,
    PF_vectoangles,

    PF_WriteByte,
    PF_WriteChar,
    PF_WriteShort,
    PF_WriteLong,
    PF_WriteCoord,
    PF_WriteAngle,
    PF_WriteString,
    PF_WriteEntity,

    PF_Fixme, PF_Fixme, PF_Fixme, PF_Fixme,
    PF_Fixme, PF_Fixme, PF_Fixme,

    SV_MoveToGoal,
    PF_precache_file,
    PF_makestatic,

    PF_changelevel,
    PF_Fixme,

    PF_cvar_set,
    PF_centerprint,

    PF_ambientsound,

    PF_precache_model,
    PF_precache_sound, // precache_sound2 is different only for qcc
    PF_precache_file,

    PF_setspawnparms
};

builtin_t* pr_builtins = pr_builtin;
int pr_numbuiltins = sizeof(pr_builtin) / sizeof(pr_builtin[0]);

} // namespace VM
