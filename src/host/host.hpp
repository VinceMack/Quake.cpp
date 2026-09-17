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
    int argc{0};
    char** argv{nullptr};
};

namespace Host {

extern quakeparms_t host_parms;
extern cvar_t sys_ticrate, sys_nostdout, developer;
extern qboolean host_initialized, isDedicated, noclip_anglehack;
extern double host_frametime, host_time, realtime;
extern byte *host_basepal, *host_colormap;
extern int host_framecount, current_skill;
extern ::client_t* host_client;

void Host_ClearMemory();
void Host_ServerFrame();
void Host_InitCommands();
void Host_Init(quakeparms_t* parms);
void Host_Shutdown();

// Thrown by Host_Error and Host_EndGame after the server and client have been shut
// down. Host_Frame catches it, so a recoverable error (a QuakeC runtime fault, a lost
// connection, a malformed server message) returns the engine to the console instead of
// terminating the process. Fatal errors go through Sys_Error, which never returns.
struct HostAbort {};

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
