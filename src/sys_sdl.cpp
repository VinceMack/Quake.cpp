// sys_sdl.cpp -- SDL system interface (file I/O, timing, main loop)

#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include <SDL.h>

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


const char* basedir = ".";
const char* cachedir = "/tmp";

namespace Host {
qboolean isDedicated;
cvar_t sys_nostdout = { "sys_nostdout", "0" };
} // namespace Host

namespace Common {

void Sys_Printf(const char* fmt, ...)
{
    va_list argptr;
    char text[1024];

    va_start(argptr, fmt);
    vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    fprintf(stderr, "%s", text);

    //Con_Print (text);
}

void Sys_Quit(void)
{
    Host_Shutdown();
    exit(0);
}

void Sys_Init(void)
{
}

/*
================
Sys_LowFPPrecision
================
*/
void Sys_LowFPPrecision(void)
{
    // causes weird problems on Nextstep
}

/*
================
Sys_HighFPPrecision
================
*/
void Sys_HighFPPrecision(void)
{
    // causes weird problems on Nextstep
}

[[noreturn]] void Sys_Error(const char* error, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, error);
    vsnprintf(string, sizeof(string), error, argptr);
    va_end(argptr);
    fprintf(stderr, "Error: %s\n", string);

    Host_Shutdown();
    exit(1);
}

/*
===============================================================================

FILE IO

===============================================================================
*/

#define MAX_HANDLES 10
FILE* sys_handles[MAX_HANDLES];

int findhandle(void)
{
    int i;

    for (i = 1; i < MAX_HANDLES; i++) {
        if (!sys_handles[i]) {
            return i;
        }
    }
    Sys_Error("out of handles");
}

/*
================
Qfilelength
================
*/
static int Qfilelength(FILE* f)
{
    int pos;
    int end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

int Sys_FileOpenRead(const char* path, int* hndl)
{
    FILE* f;
    int i;

    i = findhandle();

    if ((f = fopen(path, "rb")) == NULL) {
        *hndl = -1;

        return -1;
    }

    sys_handles[i] = f;
    *hndl = i;

    return Qfilelength(f);
}

int Sys_FileOpenWrite(const char* path)
{
    FILE* f;
    int i;

    i = findhandle();

    if ((f = fopen(path, "wb")) == NULL) {
        char errbuf[256];
        strerror_r(errno, errbuf, sizeof(errbuf));
        Sys_Error("Error opening %s: %s", path, errbuf);
    }

    sys_handles[i] = f;

    return i;
}

void Sys_FileClose(int handle)
{
    if (handle >= 0) {
        fclose(sys_handles[handle]);
        sys_handles[handle] = NULL;
    }
}

void Sys_FileSeek(int handle, int position)
{
    if (handle >= 0) {
        fseek(sys_handles[handle], position, SEEK_SET);
    }
}

int Sys_FileRead(int handle, void* dst, int count)
{
    int size = 0;
    if (handle >= 0) {
        char* data = static_cast<char*>(dst);
        while (count > 0) {
            int done = static_cast<int>(fread(data, 1, count, sys_handles[handle]));
            if (done == 0) {
                break;
            }

            data += done;
            count -= done;
            size += done;
        }
    }

    return size;
}

int Sys_FileWrite(int handle, const void* src, int count)
{
    int size = 0;
    if (handle >= 0) {
        const char* data = static_cast<const char*>(src);
        while (count > 0) {
            int done = static_cast<int>(fwrite(data, 1, count, sys_handles[handle]));
            if (done == 0) {
                break;
            }

            data += done;
            count -= done;
            size += done;
        }
    }

    return size;
}

int Sys_FileTime(const char* path)
{
    FILE* f;

    if ((f = fopen(path, "rb")) != NULL) {
        fclose(f);

        return 1;
    }

    return -1;
}

void Sys_mkdir(const char* /*path*/)
{
}

double Sys_FloatTime(void)
{
    static int starttime = 0;
    if (!starttime) {
        starttime = clock();
    }
    return (clock() - starttime) * 1.0 / CLOCKS_PER_SEC;
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

static volatile int oktogo;

void moncontrol()
{
}

} // namespace Common

int main(int c, char** v)
{
    quakeparms_t parms;
    extern int vcrFile;
    extern qboolean recording;
    static int frame;

    moncontrol();

    //	signal(SIGFPE, floating_point_exception_handler);
    signal(SIGFPE, SIG_IGN);

    parms.memsize = 64 * 1024 * 1024; // 64MB
    parms.membase = malloc(parms.memsize);
    parms.basedir = basedir;
    parms.cachedir = cachedir;

    COM_InitArgv(c, v);
    parms.argc = com_argc;
    parms.argv = com_argv;

    Sys_Init();

    Host_Init(&parms);

    Cvar::Register(&sys_nostdout);

    double oldtime = Sys_FloatTime() - 0.1;
    while (1) {
        // find time spent rendering last frame
        double newtime = Sys_FloatTime();
        double time = newtime - oldtime;

        if (cls.state == ca_dedicated) { // play vcrfiles at max speed
            if (time < sys_ticrate.value && (vcrFile == -1 || recording)) {
                SDL_Delay(1);
                continue; // not time to run a server only tic yet
            }

            time = sys_ticrate.value;
        }

        if (time > sys_ticrate.value * 2) {
            oldtime = newtime;
        } else {
            oldtime += time;
        }

        if (++frame > 10) {
            moncontrol(); // profile only while we do each Quake frame
        }

        Host_Frame(static_cast<float>(time));
        moncontrol();
    }
}
