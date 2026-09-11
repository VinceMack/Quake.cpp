// builtins.hpp -- Engine built-in functions callable from QuakeC bytecode
#pragma once

#include "core/cvar.hpp"
#include "vm/interpreter.hpp"

namespace VM {

extern cvar_t nomonsters;
extern cvar_t gamecfg;
extern cvar_t scratch1;
extern cvar_t scratch2;
extern cvar_t scratch3;
extern cvar_t scratch4;
extern cvar_t savedgamecfg;
extern cvar_t saved1;
extern cvar_t saved2;
extern cvar_t saved3;
extern cvar_t saved4;

void PR_Init();

} // namespace VM
