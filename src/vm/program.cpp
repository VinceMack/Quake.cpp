// program.cpp -- QuakeC program loader, string management, and program inspection
#include "vm/program.hpp"
#include "vm/edict.hpp"
#include "vm/interpreter.hpp"
#include "core/filesystem.hpp"
#include "core/endian.hpp"
#include "core/string_utils.hpp"
#include "host/host.hpp"
#include "core/print.hpp"
#include "platform/crt_compat.hpp"
#include "server/server_types.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace VM {

dprograms_t* progs = nullptr;
static std::vector<byte> progs_data; // the loaded progs.dat; every pr_* pointer below points into it
dfunction_t* pr_functions = nullptr;
char* pr_strings = nullptr;
static int pr_stringssize = 0;
// Negative string_t handles index this table of engine-owned strings (see PR_SetString).
static std::vector<const char*> pr_knownstrings;
// Strings created at runtime (entity keys parsed from the map) live for the level.
static std::vector<std::unique_ptr<char[]>> pr_created_strings;
ddef_t* pr_fielddefs = nullptr;
ddef_t* pr_globaldefs = nullptr;
dstatement_t* pr_statements = nullptr;
float* pr_globals = nullptr;
int pr_edict_size = 0;
unsigned short pr_crc = 0;

std::array<int, 8> type_size = { 1, static_cast<int>(sizeof(string_t) / 4), 1, 3, 1, 1,
    static_cast<int>(sizeof(func_t) / 4), static_cast<int>(sizeof(void*) / 4) };

static string_t PR_FindString(const char* str)
{
    for (size_t slot_index = 0; slot_index < pr_knownstrings.size(); slot_index++) {
        if (pr_knownstrings[slot_index] == str) return static_cast<string_t>(slot_index);
    }
    return static_cast<string_t>(pr_knownstrings.size());
}

static void PR_SetStringAt(int slot_index, const char* str)
{
    if (static_cast<size_t>(slot_index) >= pr_knownstrings.size())
        pr_knownstrings.resize(static_cast<size_t>(slot_index) + 1, nullptr);
    pr_knownstrings[static_cast<size_t>(slot_index)] = str;
}

string_t PR_SetString(const char* str)
{
    if (!str) return 0;
    if (str >= pr_strings && str <= &pr_strings[pr_stringssize - 2]) {
        return static_cast<string_t>(str - pr_strings);
    }

    string_t slot_index = PR_FindString(str);
    if (static_cast<size_t>(slot_index) >= pr_knownstrings.size()) PR_SetStringAt(slot_index, str);
    return -(slot_index + 1);
}

char* PR_GetString(string_t handle)
{
    if (handle >= 0 && handle < pr_stringssize) return &pr_strings[handle];
    const int numknown = static_cast<int>(pr_knownstrings.size());
    if (handle < -numknown || handle >= pr_stringssize) {
        Host::Host_Error("PR_GetString: invalid string handle %d\n", handle);
    }

    int index = -1 - handle;
    if (pr_knownstrings[static_cast<size_t>(index)])
        return const_cast<char*>(pr_knownstrings[static_cast<size_t>(index)]);

    Host::Host_Error("PR_GetString: attempt to access missing string %d\n", handle);
}

string_t PR_CreateString(int size, char** out_ptr)
{
    if (size <= 0) return 0;

    string_t slot_index = PR_FindString(nullptr);
    auto buffer = std::make_unique<char[]>(static_cast<size_t>(size)); // zero-filled
    char* str_buffer = buffer.get();
    pr_created_strings.push_back(std::move(buffer));
    PR_SetStringAt(slot_index, str_buffer);

    if (out_ptr) *out_ptr = str_buffer;
    return -(slot_index + 1);
}

char* PR_ValueString(etype_t type, eval_t* val)
{
    static char line[256];
    ddef_t* def;
    dfunction_t* f;

    type = static_cast<etype_t>(type & ~DEF_SAVEGLOBAL);

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
        sprintf_s(line, sizeof(line), "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1], val->vector[2]);
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

void PR_Profile_f(void)
{
    dfunction_t *f, *best;
    int max, num = 0, i;

    do {
        max = 0;
        best = nullptr;
        for (i = 0; i < progs->numfunctions; i++) {
            f = &pr_functions[i];
            if (f->profile > max) {
                max = f->profile;
                best = f;
            }
        }
        if (best) {
            if (num < 10) Console::Con_Printf("%7i %s\n", best->profile, PR_GetString(best->s_name));
            num++;
            best->profile = 0;
        }
    } while (best);
}

void PR_LoadProgs(void)
{
    int i;
    ED_ClearFieldCache();
    PR_ResetExecutionState();

    Common::CRC_Init(pr_crc);

    progs_data = Common::COM_LoadFile("progs.dat");
    if (progs_data.empty()) Common::Sys_Error("PR_LoadProgs: couldn't load progs.dat");
    progs = reinterpret_cast<dprograms_t*>(progs_data.data());

    Console::Con_DPrintf("Programs occupy %iK.\n", Common::com_filesize / 1024);

    for (i = 0; i < Common::com_filesize; i++) Common::CRC_ProcessByte(pr_crc, ((byte*)progs)[i]);

    for (i = 0; i < static_cast<int>(sizeof(*progs) / 4); i++) ((int*)progs)[i] = Common::LittleLong(((int*)progs)[i]);

    if (progs->version != PROG_VERSION) {
        Common::Sys_Error("progs.dat has wrong version number (%i should be %i)", progs->version, PROG_VERSION);
    }

    if (progs->crc != PROGHEADER_CRC) {
        Common::Sys_Error("progs.dat system vars have been modified, progdefs.h is out of date");
    }

    pr_functions = (dfunction_t*)((byte*)progs + progs->ofs_functions);
    pr_strings = (char*)progs + progs->ofs_strings;
    pr_stringssize = progs->numstrings;
    pr_knownstrings.clear();
    pr_created_strings.clear();
    PR_SetString("");

    pr_globaldefs = (ddef_t*)((byte*)progs + progs->ofs_globaldefs);
    pr_fielddefs = (ddef_t*)((byte*)progs + progs->ofs_fielddefs);
    pr_statements = (dstatement_t*)((byte*)progs + progs->ofs_statements);
    pr_global_struct = (globalvars_t*)((byte*)progs + progs->ofs_globals);
    pr_globals = (float*)pr_global_struct;

    pr_edict_size = progs->entityfields * 4 + sizeof(edict_t) - sizeof(entvars_t);

    for (i = 0; i < progs->numstatements; i++) {
        pr_statements[i].op = Common::LittleShort(pr_statements[i].op);
        pr_statements[i].a = Common::LittleShort(pr_statements[i].a);
        pr_statements[i].b = Common::LittleShort(pr_statements[i].b);
        pr_statements[i].c = Common::LittleShort(pr_statements[i].c);
    }

    for (i = 0; i < progs->numfunctions; i++) {
        pr_functions[i].first_statement = Common::LittleLong(pr_functions[i].first_statement);
        pr_functions[i].parm_start = Common::LittleLong(pr_functions[i].parm_start);
        pr_functions[i].s_name = Common::LittleLong(pr_functions[i].s_name);
        pr_functions[i].s_file = Common::LittleLong(pr_functions[i].s_file);
        pr_functions[i].numparms = Common::LittleLong(pr_functions[i].numparms);
        pr_functions[i].locals = Common::LittleLong(pr_functions[i].locals);
    }

    for (i = 0; i < progs->numglobaldefs; i++) {
        pr_globaldefs[i].type = Common::LittleShort(pr_globaldefs[i].type);
        pr_globaldefs[i].ofs = Common::LittleShort(pr_globaldefs[i].ofs);
        pr_globaldefs[i].s_name = Common::LittleLong(pr_globaldefs[i].s_name);
    }

    for (i = 0; i < progs->numfielddefs; i++) {
        pr_fielddefs[i].type = Common::LittleShort(pr_fielddefs[i].type);
        if (pr_fielddefs[i].type & DEF_SAVEGLOBAL) {
            Common::Sys_Error("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
        }
        pr_fielddefs[i].ofs = Common::LittleShort(pr_fielddefs[i].ofs);
        pr_fielddefs[i].s_name = Common::LittleLong(pr_fielddefs[i].s_name);
    }

    for (i = 0; i < progs->numglobals; i++) {
        ((int*)pr_globals)[i] = Common::LittleLong(((int*)pr_globals)[i]);
    }
}

} // namespace VM
