// host.hpp -- Central engine host orchestration (loop, state, error handling, dispatch)
#pragma once

#include <cstdint>
#include "sys_core.hpp"

struct client_t;

//=============================================================================
// Host & Parms
//=============================================================================

struct quakeparms_t {
    const char* basedir{nullptr};
    const char* cachedir{nullptr};
    int argc{0};
    char** argv{nullptr};
    void* membase{nullptr};
    int memsize{0};
};

namespace Host {

extern quakeparms_t host_parms;
extern cvar_t sys_ticrate, sys_nostdout, developer;
extern qboolean host_initialized, isDedicated, noclip_anglehack;
extern double host_frametime, host_time, realtime;
extern byte *host_basepal, *host_colormap;
extern int host_framecount, current_skill, minimum_memory;
extern ::client_t* host_client;

void Host_ClearMemory();
void Host_ServerFrame();
void Host_InitCommands();
void Host_Init(quakeparms_t* parms);
void Host_Shutdown();

[[noreturn]] void Host_Error(const char* error, ...);
[[noreturn]] void Host_EndGame(const char* message, ...);
void Host_Frame(float time);
void Host_Quit_f();
void Host_ClientCommands(const char* fmt, ...);
void Host_ShutdownServer(qboolean crash);

// Deterministic digest of server, client, and framebuffer state for regression testing.
[[nodiscard]] uint64_t Host_StateHash();
void Host_PrintStateHash();

} // namespace Host
