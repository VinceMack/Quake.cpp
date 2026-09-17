// edict.cpp -- Entity dictionary allocation, field reflection, and serialization
#include "quakedef.hpp"
#include "vm/edict.hpp"
#include "vm/program.hpp"
#include "vm/interpreter.hpp"
#include "core/filesystem.hpp"
#include "core/string_utils.hpp"
#include "core/cmd.hpp"
#include "host/host.hpp"
#include "ui/console.hpp"
#include "sys_server.hpp"

#include <cstring>
#include <cstdlib>

namespace VM {

globalvars_t* pr_global_struct = nullptr;

#define MAX_FIELD_LEN 64
#define GEFV_CACHESIZE 2

typedef struct {
    ddef_t* pcache;
    char field[MAX_FIELD_LEN];
} gefv_cache;

static gefv_cache gefvCache[GEFV_CACHESIZE] = { { nullptr, "" }, { nullptr, "" } };

void ED_ClearFieldCache() {
    for (int i = 0; i < GEFV_CACHESIZE; i++) gefvCache[i].field[0] = 0;
}

void ED_ClearEdict(edict_t* e) {
    std::memset(static_cast<void*>(&e->v), 0, static_cast<size_t>(progs->entityfields) * 4);
    e->free = false;
}

edict_t* ED_Alloc(void) {
    int i;
    edict_t* e;

    for (i = Server::svs.maxclients + 1; i < Server::sv.num_edicts; i++) {
        e = EDICT_NUM(i);
        if (e->free && (e->freetime < 2 || Server::sv.time - e->freetime > 0.5)) {
            ED_ClearEdict(e);
            return e;
        }
    }

    if (i == MAX_EDICTS) {
        Common::Sys_Error("ED_Alloc: no free edicts");
    }

    Server::sv.num_edicts++;
    e = EDICT_NUM(i);
    ED_ClearEdict(e);

    return e;
}

void ED_Free(edict_t* ed) {
    Server::SV_UnlinkEdict(ed);

    ed->free = true;
    ed->v.model = 0;
    ed->v.takedamage = 0;
    ed->v.modelindex = 0;
    ed->v.colormap = 0;
    ed->v.skin = 0;
    ed->v.frame = 0;
    VectorCopy(Math::vec3_origin, ed->v.origin);
    VectorCopy(Math::vec3_origin, ed->v.angles);
    ed->v.nextthink = -1;
    ed->v.solid = 0;

    ed->freetime = static_cast<float>(Server::sv.time);
}

ddef_t* ED_GlobalAtOfs(int ofs) {
    for (int i = 0; i < progs->numglobaldefs; i++) {
        ddef_t* def = &pr_globaldefs[i];
        if (def->ofs == ofs) return def;
    }
    return nullptr;
}

ddef_t* ED_FieldAtOfs(int ofs) {
    for (int i = 0; i < progs->numfielddefs; i++) {
        ddef_t* def = &pr_fielddefs[i];
        if (def->ofs == ofs) return def;
    }
    return nullptr;
}

ddef_t* ED_FindField(const char* name) {
    for (int i = 0; i < progs->numfielddefs; i++) {
        ddef_t* def = &pr_fielddefs[i];
        if (!std::strcmp(PR_GetString(def->s_name), name)) return def;
    }
    return nullptr;
}

ddef_t* ED_FindGlobal(char* name) {
    for (int i = 0; i < progs->numglobaldefs; i++) {
        ddef_t* def = &pr_globaldefs[i];
        if (!std::strcmp(PR_GetString(def->s_name), name)) return def;
    }
    return nullptr;
}

dfunction_t* ED_FindFunction(char* name) {
    for (int i = 0; i < progs->numfunctions; i++) {
        dfunction_t* func = &pr_functions[i];
        if (!std::strcmp(PR_GetString(func->s_name), name)) return func;
    }
    return nullptr;
}

eval_t* GetEdictFieldValue(edict_t* ed, const char* field) {
    ddef_t* def = nullptr;
    static int rep = 0;

    for (int i = 0; i < GEFV_CACHESIZE; i++) {
        if (!std::strcmp(field, gefvCache[i].field)) {
            def = gefvCache[i].pcache;
            goto Done;
        }
    }

    def = ED_FindField(field);

    if (std::strlen(field) < MAX_FIELD_LEN) {
        gefvCache[rep].pcache = def;
        strcpy_s(gefvCache[rep].field, sizeof(gefvCache[rep].field), field);
        rep ^= 1;
    }

Done:
    if (!def) return nullptr;
    return (eval_t*)((char*)&ed->v + def->ofs * 4);
}

char* PR_UglyValueString(etype_t type, eval_t* val) {
    static char line[256];
    ddef_t* def;
    dfunction_t* f;

    type = static_cast<etype_t>(type & ~DEF_SAVEGLOBAL);

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

char* PR_GlobalString(int ofs) {
    static char line[128];
    void* val = (void*)&pr_globals[ofs];
    ddef_t* def = ED_GlobalAtOfs(ofs);
    if (!def) {
        sprintf_s(line, sizeof(line), "%i(??? unknown)", ofs);
    } else {
        char* s = PR_ValueString((etype_t)def->type, (eval_t*)val);
        sprintf_s(line, sizeof(line), "%i(%s)%s", ofs, PR_GetString(def->s_name), s);
    }

    int i = static_cast<int>(std::strlen(line));
    for (; i < 20; i++) strcat_s(line, sizeof(line), " ");
    strcat_s(line, sizeof(line), " ");

    return line;
}

char* PR_GlobalStringNoContents(int ofs) {
    static char line[128];
    ddef_t* def = ED_GlobalAtOfs(ofs);
    if (!def) {
        sprintf_s(line, sizeof(line), "%i(?\?\?)", ofs);
    } else {
        sprintf_s(line, sizeof(line), "%i(%s)", ofs, PR_GetString(def->s_name));
    }

    int i = static_cast<int>(std::strlen(line));
    for (; i < 20; i++) strcat_s(line, sizeof(line), " ");
    strcat_s(line, sizeof(line), " ");

    return line;
}

void ED_Print(edict_t* ed) {
    if (ed->free) {
        Console::Con_Printf("FREE\n");
        return;
    }

    Console::Con_Printf("\nEDICT %i:\n", NUM_FOR_EDICT(ed));
    for (int i = 1; i < progs->numfielddefs; i++) {
        ddef_t* d = &pr_fielddefs[i];
        char* name = PR_GetString(d->s_name);
        if (name[std::strlen(name) - 2] == '_') continue;

        int* v = (int*)((char*)&ed->v + d->ofs * 4);
        int type = d->type & ~DEF_SAVEGLOBAL;

        int j;
        for (j = 0; j < type_size[type]; j++) {
            if (v[j]) break;
        }
        if (j == type_size[type]) continue;

        Console::Con_Printf("%s", name);
        int l = static_cast<int>(std::strlen(name));
        while (l++ < 15) Console::Con_Printf(" ");

        Console::Con_Printf("%s\n", PR_ValueString(static_cast<etype_t>(d->type), (eval_t*)v));
    }
}

void ED_Write(std::ostream& f, edict_t* ed) {
    f << "{\n";
    if (ed->free) {
        f << "}\n";
        return;
    }

    for (int i = 1; i < progs->numfielddefs; i++) {
        ddef_t* d = &pr_fielddefs[i];
        const char* name = PR_GetString(d->s_name);
        if (name[std::strlen(name) - 2] == '_') continue;

        int* v = (int*)((char*)&ed->v + d->ofs * 4);
        int type = d->type & ~DEF_SAVEGLOBAL;
        int j;
        for (j = 0; j < type_size[type]; j++) {
            if (v[j]) break;
        }
        if (j == type_size[type]) continue;

        f << "\"" << name << "\" ";
        f << "\"" << PR_UglyValueString(static_cast<etype_t>(d->type), reinterpret_cast<eval_t*>(v)) << "\"\n";
    }

    f << "}\n";
}

void ED_PrintNum(int ent) {
    ED_Print(EDICT_NUM(ent));
}

void ED_PrintEdicts(void) {
    Console::Con_Printf("%i entities\n", Server::sv.num_edicts);
    for (int i = 0; i < Server::sv.num_edicts; i++) ED_PrintNum(i);
}

void ED_PrintEdict_f(void) {
    int i = Common::Q_atoi(Cmd::Argv(1));
    if (i >= Server::sv.num_edicts) {
        Console::Con_Printf("Bad edict number\n");
        return;
    }
    ED_PrintNum(i);
}

void ED_Count(void) {
    int active = 0, models = 0, solid = 0, step = 0;

    for (int i = 0; i < Server::sv.num_edicts; i++) {
        edict_t* ent = EDICT_NUM(i);
        if (ent->free) continue;
        active++;
        if (ent->v.solid) solid++;
        if (ent->v.model) models++;
        if (ent->v.movetype == MOVETYPE_STEP) step++;
    }

    Console::Con_Printf("num_edicts:%3i\n", Server::sv.num_edicts);
    Console::Con_Printf("active    :%3i\n", active);
    Console::Con_Printf("view      :%3i\n", models);
    Console::Con_Printf("touch     :%3i\n", solid);
    Console::Con_Printf("step      :%3i\n", step);
}

void ED_WriteGlobals(std::ostream& f) {
    f << "{\n";
    for (int i = 0; i < progs->numglobaldefs; i++) {
        ddef_t* def = &pr_globaldefs[i];
        int type = def->type;
        if (!(def->type & DEF_SAVEGLOBAL)) continue;

        type &= ~DEF_SAVEGLOBAL;
        if (type != ev_string && type != ev_float && type != ev_entity) continue;

        const char* name = PR_GetString(def->s_name);
        f << "\"" << name << "\" ";
        f << "\"" << PR_UglyValueString(static_cast<etype_t>(type), reinterpret_cast<eval_t*>(&pr_globals[def->ofs])) << "\"\n";
    }
    f << "}\n";
}

string_t ED_NewString(const char* source) {
    if (!source) return 0;

    int length = static_cast<int>(std::strlen(source)) + 1;
    char* dest = nullptr;
    string_t handle = PR_CreateString(length, &dest);

    for (int i = 0; i < length; i++) {
        if (source[i] == '\\' && i < length - 1) {
            i++;
            if (source[i] == 'n') *dest++ = '\n';
            else *dest++ = '\\';
        } else {
            *dest++ = source[i];
        }
    }

    return handle;
}

static qboolean ED_ParseEpair(void* base, ddef_t* key, char* s) {
    char string[128];
    void* d = (void*)((int*)base + key->ofs);

    switch (key->type & ~DEF_SAVEGLOBAL) {
    case ev_string:
        *(string_t*)d = ED_NewString(s);
        break;
    case ev_float:
        *(float*)d = static_cast<float>(std::atof(s));
        break;
    case ev_vector: {
        strcpy_s(string, sizeof(string), s);
        char* v = string;
        char* w = string;
        for (int i = 0; i < 3; i++) {
            while (*v && *v != ' ') v++;
            *v = 0;
            ((float*)d)[i] = static_cast<float>(std::atof(w));
            w = v = v + 1;
        }
        break;
    }
    case ev_entity:
        *(int*)d = static_cast<int>(EDICT_TO_PROG(EDICT_NUM(std::atoi(s))));
        break;
    case ev_field: {
        ddef_t* def = ED_FindField(s);
        if (!def) {
            Console::Con_Printf("Can't find field %s\n", s);
            return false;
        }
        *(int*)d = G_INT(def->ofs);
        break;
    }
    case ev_function: {
        dfunction_t* func = ED_FindFunction(s);
        if (!func) {
            Console::Con_Printf("Can't find function %s\n", s);
            return false;
        }
        *(func_t*)d = static_cast<func_t>(func - pr_functions);
        break;
    }
    default:
        break;
    }

    return true;
}

void ED_ParseGlobals(char* data) {
    char keyname[64];

    while (1) {
        data = const_cast<char*>(Common::COM_Parse(data));
        if (Common::com_token[0] == '}') break;
        if (!data) Common::Sys_Error("ED_ParseEntity: EOF without closing brace");

        strcpy_s(keyname, sizeof(keyname), Common::com_token);
        data = const_cast<char*>(Common::COM_Parse(data));
        if (!data) Common::Sys_Error("ED_ParseEntity: EOF without closing brace");
        if (Common::com_token[0] == '}') Common::Sys_Error("ED_ParseEntity: closing brace without data");

        ddef_t* key = ED_FindGlobal(keyname);
        if (!key) {
            Console::Con_Printf("'%s' is not a global\n", keyname);
            continue;
        }

        if (!ED_ParseEpair((void*)pr_globals, key, Common::com_token)) {
            Host::Host_Error("ED_ParseGlobals: parse error");
        }
    }
}

char* ED_ParseEdict(char* data, edict_t* ent) {
    qboolean anglehack, init = false;
    char keyname[256];

    if (ent != Server::sv.edicts) {
        std::memset(static_cast<void*>(&ent->v), 0, static_cast<size_t>(progs->entityfields) * 4);
    }

    while (1) {
        data = const_cast<char*>(Common::COM_Parse(data));
        if (Common::com_token[0] == '}') break;
        if (!data) Common::Sys_Error("ED_ParseEntity: EOF without closing brace");

        if (!std::strcmp(Common::com_token, "angle")) {
            strcpy_s(Common::com_token, sizeof(Common::com_token), "angles");
            anglehack = true;
        } else {
            anglehack = false;
        }

        if (!std::strcmp(Common::com_token, "light")) {
            strcpy_s(Common::com_token, sizeof(Common::com_token), "light_lev");
        }

        strcpy_s(keyname, sizeof(keyname), Common::com_token);
        int n = static_cast<int>(std::strlen(keyname));
        while (n && keyname[n - 1] == ' ') {
            keyname[n - 1] = 0;
            n--;
        }

        data = const_cast<char*>(Common::COM_Parse(data));
        if (!data) Common::Sys_Error("ED_ParseEntity: EOF without closing brace");
        if (Common::com_token[0] == '}') Common::Sys_Error("ED_ParseEntity: closing brace without data");

        init = true;

        if (keyname[0] == '_') continue;

        ddef_t* key = ED_FindField(keyname);
        if (!key) {
            Console::Con_Printf("'%s' is not a field\n", keyname);
            continue;
        }

        if (anglehack) {
            char temp[32];
            strcpy_s(temp, sizeof(temp), Common::com_token);
            sprintf_s(Common::com_token, sizeof(Common::com_token), "0 %s 0", temp);
        }

        if (!ED_ParseEpair((void*)&ent->v, key, Common::com_token)) {
            Host::Host_Error("ED_ParseEdict: parse error");
        }
    }

    if (!init) ent->free = true;
    return data;
}

void ED_LoadFromFile(char* data) {
    edict_t* ent = nullptr;
    int inhibit = 0;

    pr_global_struct->time = static_cast<float>(Server::sv.time);

    while (1) {
        data = const_cast<char*>(Common::COM_Parse(data));
        if (!data) break;

        if (Common::com_token[0] != '{') {
            Common::Sys_Error("ED_LoadFromFile: found %s when expecting {", Common::com_token);
        }

        if (!ent) ent = EDICT_NUM(0);
        else ent = ED_Alloc();

        data = ED_ParseEdict(data, ent);

        if (Server::deathmatch.value) {
            if (static_cast<int>(ent->v.spawnflags) & SPAWNFLAG_NOT_DEATHMATCH) {
                ED_Free(ent);
                inhibit++;
                continue;
            }
        } else if ((Host::current_skill == 0 && (static_cast<int>(ent->v.spawnflags) & SPAWNFLAG_NOT_EASY)) ||
                   (Host::current_skill == 1 && (static_cast<int>(ent->v.spawnflags) & SPAWNFLAG_NOT_MEDIUM)) ||
                   (Host::current_skill >= 2 && (static_cast<int>(ent->v.spawnflags) & SPAWNFLAG_NOT_HARD))) {
            ED_Free(ent);
            inhibit++;
            continue;
        }

        if (!ent->v.classname) {
            Console::Con_Printf("No classname for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        dfunction_t* func = ED_FindFunction(PR_GetString(ent->v.classname));
        if (!func) {
            Console::Con_Printf("No spawn function for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        pr_global_struct->self = static_cast<int>(EDICT_TO_PROG(ent));
        PR_ExecuteProgram(static_cast<func_t>(func - pr_functions));
    }

    Console::Con_DPrintf("%i entities inhibited\n", inhibit);
}

edict_t* EDICT_NUM(int n) {
    if (n < 0 || n >= Server::sv.max_edicts) {
        Common::Sys_Error("EDICT_NUM: bad number %i", n);
    }
    return (edict_t*)((byte*)Server::sv.edicts + (n)*pr_edict_size);
}

int NUM_FOR_EDICT(edict_t* e) {
    int b = static_cast<int>((byte*)e - (byte*)Server::sv.edicts);
    b = b / pr_edict_size;
    if (b < 0 || b >= Server::sv.num_edicts) Common::Sys_Error("NUM_FOR_EDICT: bad pointer");
    return b;
}

} // namespace VM
