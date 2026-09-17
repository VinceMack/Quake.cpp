// sys_sdl.cpp -- SDL-based operating system layer: file I/O, timers, console, process lifecycle
#include "platform/system.hpp"
#include "core/cvar.hpp"
#include "core/cmd.hpp"
#include "core/filesystem.hpp"
#include "host/host.hpp"
#include "platform/crt_compat.hpp"
#include <SDL.h>
#include <csignal>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <array>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace Host {
qboolean isDedicated = false;
cvar_t sys_nostdout = { "sys_nostdout", "0" };
}

namespace Common {

void Sys_Printf(const char* fmt, ...) {
    va_list argptr;
    char text[1024];
    va_start(argptr, fmt);
    vsprintf_s(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    std::fprintf(stderr, "%s", text);
}

void Sys_Quit(void) {
    Host::Host_Shutdown();
    std::exit(0);
}


[[noreturn]] void Sys_Error(const char* error, ...) {
    va_list argptr;
    char string[1024];
    va_start(argptr, error);
    vsprintf_s(string, sizeof(string), error, argptr);
    va_end(argptr);
    std::fprintf(stderr, "Error: %s\n", string);
    Host::Host_Shutdown();
    std::exit(1);
}

constexpr size_t MAX_HANDLES = 10;
static std::array<FILE*, MAX_HANDLES> sys_handles{};

static FILE* get_file_handle(int handle) {
    if (handle >= 0 && static_cast<size_t>(handle) < sys_handles.size()) return sys_handles[static_cast<size_t>(handle)];
    return nullptr;
}

static int findhandle(void) {
    for (size_t i = 1; i < sys_handles.size(); i++) {
        if (!sys_handles[i]) return static_cast<int>(i);
    }
    Sys_Error("out of handles");
}

static int Qfilelength(FILE* f) {
    long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    long end = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);
    return static_cast<int>(end);
}

int Sys_FileOpenRead(const char* path, int* hndl) {
    FILE* f = nullptr;
    int i = findhandle();
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        *hndl = -1;
        return -1;
    }
    sys_handles[static_cast<size_t>(i)] = f;
    *hndl = i;
    return Qfilelength(f);
}

int Sys_FileOpenWrite(const char* path) {
    FILE* f = nullptr;
    int i = findhandle();
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        char errbuf[256];
        strerror_s(errbuf, sizeof(errbuf), errno);
        Sys_Error("Error opening %s: %s", path, errbuf);
    }
    sys_handles[static_cast<size_t>(i)] = f;
    return i;
}

void Sys_FileClose(int handle) {
    if (FILE* f = get_file_handle(handle)) {
        std::fclose(f);
        sys_handles[static_cast<size_t>(handle)] = nullptr;
    }
}

void Sys_FileSeek(int handle, int position) {
    if (FILE* f = get_file_handle(handle)) std::fseek(f, position, SEEK_SET);
}

int Sys_FileRead(int handle, void* dst, int count) {
    FILE* f = get_file_handle(handle);
    if (!f) return 0;
    auto* data = static_cast<char*>(dst);
    int size = 0;
    while (count > 0) {
        int done = static_cast<int>(std::fread(data, 1, count, f));
        if (done == 0) break;
        data += done;
        count -= done;
        size += done;
    }
    return size;
}

int Sys_FileWrite(int handle, const void* src, int count) {
    FILE* f = get_file_handle(handle);
    if (!f) return 0;
    auto* data = static_cast<const char*>(src);
    int size = 0;
    while (count > 0) {
        int done = static_cast<int>(std::fwrite(data, 1, count, f));
        if (done == 0) break;
        data += done;
        count -= done;
        size += done;
    }
    return size;
}

bool Sys_FileExists(const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") == 0 && f) {
        std::fclose(f);
        return true;
    }
    return false;
}

void Sys_mkdir(const char* path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

double Sys_FloatTime(void) {
    static const Uint64 start = SDL_GetPerformanceCounter();
    static const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    return static_cast<double>(SDL_GetPerformanceCounter() - start) / frequency;
}

char* Sys_ConsoleInput(void) { return nullptr; }

} // namespace Common
