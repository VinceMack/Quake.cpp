// interpreter.hpp -- QuakeC bytecode interpreter execution loop and stack management
#pragma once

#include "core/types.hpp"
#include "vm/program.hpp"

namespace VM {

extern int pr_argc;
extern qboolean pr_trace;
extern dfunction_t* pr_xfunction;
extern int pr_xstatement;

typedef void (*builtin_t)(void);
extern builtin_t* pr_builtins;
extern int pr_numbuiltins;

void PR_ExecuteProgram(func_t fnum);
// Discards any interpreter call stack left over after an aborted execution.
void PR_ResetExecutionState();
[[noreturn]] void PR_RunError(const char* error, ...);
void PR_StackTrace();

} // namespace VM
