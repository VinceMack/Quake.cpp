// host.h -- host system declarations
#pragma once

#include "common.hpp"
#include "cvar.hpp"

struct quakeparms_t {
    const char* basedir;
    const char* cachedir;
    int argc;
    char** argv;
    void* membase;
    int memsize;
};

namespace Host {

extern quakeparms_t host_parms;

extern cvar_t sys_ticrate;
extern cvar_t sys_nostdout;
extern cvar_t developer;

extern qboolean host_initialized;
extern double host_frametime;
extern byte* host_basepal;
extern byte* host_colormap;
extern int host_framecount;
extern double realtime;

void Host_ClearMemory();
void Host_ServerFrame();
void Host_InitCommands();
void Host_Init(quakeparms_t* parms);
void Host_Shutdown();
class HostException : public std::runtime_error {
public:
    explicit HostException(const std::string& msg) : std::runtime_error(msg) {}
};

class HostEndGameException : public HostException {
public:
    using HostException::HostException;
};

class HostErrorException : public HostException {
public:
    using HostException::HostException;
};

[[noreturn]] void Host_Error(const char* error, ...);
[[noreturn]] void Host_EndGame(const char* message, ...);
void Host_Frame(float time);
void Host_Quit_f();
void Host_ClientCommands(const char* fmt, ...);
void Host_ShutdownServer(qboolean crash);

extern int current_skill;
extern qboolean isDedicated;
extern int minimum_memory;
extern qboolean noclip_anglehack;

extern client_t* host_client;
extern double host_time;

} // namespace Host
