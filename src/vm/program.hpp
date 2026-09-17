// program.hpp -- QuakeC program header, type definitions, symbol tables, and bytecode representation
#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include "core/types.hpp"

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
    std::array<uint8_t, MAX_PARMS> parm_size{};
};

constexpr int PROG_VERSION = 6;
constexpr int PROGHEADER_CRC = 5927;

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

typedef union eval_s {
    string_t string;
    float _float;
    float vector[3];
    func_t function;
    int _int;
    int edict;
} eval_t;

namespace VM {

extern dprograms_t* progs;
extern dfunction_t* pr_functions;
extern char* pr_strings;
extern ddef_t* pr_globaldefs;
extern ddef_t* pr_fielddefs;
extern dstatement_t* pr_statements;
extern float* pr_globals;
extern int pr_edict_size;
extern unsigned short pr_crc;
extern std::array<int, 8> type_size;

void PR_Init();
void PR_LoadProgs();

string_t PR_SetString(const char* str);
inline string_t PR_SetString(std::string_view str) { return PR_SetString(str.data()); }
[[nodiscard]] char* PR_GetString(string_t handle);
string_t PR_CreateString(int size, char** out_ptr);

char* PR_ValueString(etype_t type, eval_t* val);
void PR_PrintStatement(dstatement_t* s);
void PR_Profile_f();

} // namespace VM
