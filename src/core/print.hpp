// print.hpp -- Console output entry points usable from every layer
#pragma once

namespace Console {

// Prints to the in-game console (and the terminal). Implemented in ui/console.cpp.
void Con_Printf(const char* fmt, ...);
// Same, but only when the "developer" cvar is set.
void Con_DPrintf(const char* fmt, ...);

} // namespace Console
