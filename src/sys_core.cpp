// sys_core.cpp -- Subsystem Core Implementation
// Combines: common.cpp, zone.cpp, crc.cpp, cvar.cpp, cmd.cpp, mathlib.cpp, sys_sdl.cpp, wad.cpp, eastl_defaults.cpp

#include "quakedef.hpp"

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

#include <bit>
#include <cstring>
#include <cctype>
#include <cmath>
#include <numeric>
#include <utility>
#include <new>

#include <EASTL/array.h>
#include <EASTL/algorithm.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/map.h>
#include <EASTL/functional.h>
#include <EASTL/unordered_map.h>

#include <SDL.h>

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

//=============================================================================
// 1. EASTL Default Memory Allocators (from eastl_defaults.cpp)
//=============================================================================

static void* aligned_alloc_helper(size_t size, size_t alignment) {
    size_t adjustedAlignment = (alignment > sizeof(void*)) ? alignment : sizeof(void*);
    void* orig = std::malloc(size + adjustedAlignment + sizeof(void*));
    if (!orig) return nullptr;

    void* pPlusPointerSize = (void*)((uintptr_t)orig + sizeof(void*));
    void* pAligned = (void*)(((uintptr_t)pPlusPointerSize + adjustedAlignment - 1) & ~(adjustedAlignment - 1));
    ((void**)pAligned)[-1] = orig;
    return pAligned;
}

static void aligned_free_helper(void* ptr) {
    if (ptr) {
        void* orig = ((void**)ptr)[-1];
        std::free(orig);
    }
}

void* operator new[](size_t size) {
    void* ptr = aligned_alloc_helper(size, sizeof(void*));
    assert(ptr != nullptr && "Out of memory");
    return ptr;
}

void* operator new[](size_t size, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    void* ptr = aligned_alloc_helper(size, sizeof(void*));
    assert(ptr != nullptr && "Out of memory");
    return ptr;
}

void* operator new[](size_t size, size_t alignment, size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    void* ptr = aligned_alloc_helper(size, alignment);
    assert(ptr != nullptr && "Out of memory");
    return ptr;
}

void* operator new[](size_t size, std::align_val_t alignment) {
    void* ptr = aligned_alloc_helper(size, static_cast<size_t>(alignment));
    assert(ptr != nullptr && "Out of memory");
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    aligned_free_helper(ptr);
}

void operator delete[](void* ptr, size_t /*size*/) noexcept {
    aligned_free_helper(ptr);
}

void operator delete[](void* ptr, std::align_val_t /*alignment*/) noexcept {
    aligned_free_helper(ptr);
}

void operator delete[](void* ptr, size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    aligned_free_helper(ptr);
}

namespace EA {
namespace StdC {
int Vsnprintf(char* pDestination, size_t n, const char* pFormat, va_list arguments) {
    return std::vsnprintf(pDestination, n, pFormat, arguments);
}
}
}

//=============================================================================
// Global Cvar & Path Definitions
//=============================================================================

cvar_t registered = { "registered", "0", {}, {}, {}, {} };
cvar_t cmdline = { "cmdline", "0", false, true, {}, {} };

const char* basedir = ".";
const char* cachedir = "/tmp";

namespace Host {
qboolean isDedicated;
cvar_t sys_nostdout = { "sys_nostdout", "0" };
} // namespace Host

//=============================================================================
// 2. Common Utilities & Filesystem (from common.cpp)
//=============================================================================

namespace Common {

#define NUM_SAFE_ARGVS 7

static char* largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static const char* argvdummy = " ";

static const char* safeargvs[NUM_SAFE_ARGVS] = { "-stdvid", "-nolan", "-nosound",
    "-nocdaudio", "-nojoy", "-nomouse",
    "-dibonly" };

bool com_modified;
bool proghack;
int static_registered = 1;
bool msg_suppress_1 = false;

void COM_InitFilesystem(void);

#define PAK0_COUNT 339
#define PAK0_CRC 32981

char com_token[1024];
int com_argc;
char** com_argv;

#define CMDLINE_LENGTH 256
char com_cmdline[CMDLINE_LENGTH];

bool standard_quake = true, rogue, hipnotic;

unsigned short pop[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x6600, 0x0000, 0x0000, 0x0000, 0x6600, 0x0000, 0x0000, 0x0066,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0067, 0x0000, 0x0000, 0x6665, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0065, 0x6600, 0x0063, 0x6561, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0061, 0x6563, 0x0064, 0x6561, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0061, 0x6564, 0x0064, 0x6564, 0x0000, 0x6469, 0x6969, 0x6400,
    0x0064, 0x6564, 0x0063, 0x6568, 0x6200, 0x0064, 0x6864, 0x0000, 0x6268,
    0x6563, 0x0000, 0x6567, 0x6963, 0x0064, 0x6764, 0x0063, 0x6967, 0x6500,
    0x0000, 0x6266, 0x6769, 0x6a68, 0x6768, 0x6a69, 0x6766, 0x6200, 0x0000,
    0x0062, 0x6566, 0x6666, 0x6666, 0x6666, 0x6562, 0x0000, 0x0000, 0x0000,
    0x0062, 0x6364, 0x6664, 0x6362, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0062, 0x6662, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0061,
    0x6661, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x6500,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x6400, 0x0000,
    0x0000, 0x0000
};

void ClearLink(link_t* l)
{
    l->prev = l->next = l;
}

void RemoveLink(link_t* l)
{
    l->next->prev = l->prev;
    l->prev->next = l->next;
}

void InsertLinkBefore(link_t* l, link_t* before)
{
    l->next = before;
    l->prev = before->prev;
    l->prev->next = l;
    l->next->prev = l;
}

void Q_memset(void* dest, int fill, int count)
{
    std::memset(dest, fill, count);
}

void Q_memcpy(void* dest, const void* src, int count)
{
    std::memcpy(dest, src, count);
}

void Q_strcpy(char* dest, const char* src)
{
    std::size_t len = std::strlen(src);
    eastl::copy_n(src, len + 1, dest);
}

void Q_strncpy(char* dest, const char* src, int count)
{
    if (count <= 0) {
        return;
    }
    std::size_t len = std::strlen(src);
    if (len < static_cast<std::size_t>(count)) {
        eastl::copy_n(src, len, dest);
        dest[len] = '\0';
    } else {
        eastl::copy_n(src, count, dest);
    }
}

int Q_strlen(const char* str)
{
    return static_cast<int>(std::strlen(str));
}

const char* Q_strrchr(const char* s, char c)
{
    return std::strrchr(s, c);
}

void Q_strcat(char* dest, const char* src)
{
    std::size_t dest_len = std::strlen(dest);
    std::size_t src_len = std::strlen(src);
    eastl::copy_n(src, src_len + 1, dest + dest_len);
}

int Q_strcmp(const char* s1, const char* s2)
{
    return std::strcmp(s1, s2);
}

int Q_strncmp(const char* s1, const char* s2, int count)
{
    return std::strncmp(s1, s2, count);
}

int Q_strncasecmp(const char* s1, const char* s2, int n)
{
    while (n-- > 0) {
        char c1 = *s1++;
        char c2 = *s2++;
        if (c1 != c2) {
            char lc1 = static_cast<char>(std::tolower(static_cast<unsigned char>(c1)));
            char lc2 = static_cast<char>(std::tolower(static_cast<unsigned char>(c2)));
            if (lc1 != lc2) {
                return (lc1 < lc2) ? -1 : 1;
            }
        }
        if (c1 == '\0') {
            break;
        }
    }
    return 0;
}

int Q_atoi(eastl::string_view str)
{
    if (str.empty()) {
        return 0;
    }

    size_t pos = 0;
    int sign = 1;
    if (str[pos] == '-') {
        sign = -1;
        pos++;
    }

    if (pos >= str.size()) {
        return 0;
    }

    if (pos + 1 < str.size() && str[pos] == '0' && (str[pos + 1] == 'x' || str[pos + 1] == 'X')) {
        pos += 2;
        int val = 0;
        while (pos < str.size()) {
            char c = str[pos];
            if (c >= '0' && c <= '9') {
                val = (val << 4) + (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val = (val << 4) + (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                val = (val << 4) + (c - 'A' + 10);
            } else {
                break;
            }
            pos++;
        }
        return val * sign;
    }

    if (str[pos] == '\'') {
        if (pos + 1 < str.size()) {
            return sign * static_cast<int>(static_cast<unsigned char>(str[pos + 1]));
        }
        return 0;
    }

    int val = 0;
    while (pos < str.size()) {
        char c = str[pos];
        if (c < '0' || c > '9') {
            break;
        }
        val = val * 10 + (c - '0');
        pos++;
    }

    return val * sign;
}

float Q_atof(eastl::string_view str)
{
    if (str.empty()) {
        return 0.0f;
    }

    size_t pos = 0;
    int sign = 1;
    if (str[pos] == '-') {
        sign = -1;
        pos++;
    }

    if (pos >= str.size()) {
        return 0.0f;
    }

    if (pos + 1 < str.size() && str[pos] == '0' && (str[pos + 1] == 'x' || str[pos + 1] == 'X')) {
        pos += 2;
        double val = 0.0;
        while (pos < str.size()) {
            char c = str[pos];
            if (c >= '0' && c <= '9') {
                val = (val * 16) + (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val = (val * 16) + (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                val = (val * 16) + (c - 'A' + 10);
            } else {
                break;
            }
            pos++;
        }
        return static_cast<float>(val * sign);
    }

    if (str[pos] == '\'') {
        if (pos + 1 < str.size()) {
            return static_cast<float>(sign * static_cast<int>(static_cast<unsigned char>(str[pos + 1])));
        }
        return 0.0f;
    }

    double val = 0.0;
    int decimal = -1;
    int total = 0;
    while (pos < str.size()) {
        char c = str[pos];
        if (c == '.') {
            decimal = total;
            pos++;
            continue;
        }

        if (c < '0' || c > '9') {
            break;
        }

        val = val * 10 + (c - '0');
        total++;
        pos++;
    }

    if (decimal == -1) {
        return static_cast<float>(val * sign);
    }

    while (total > decimal) {
        val /= 10.0;
        total--;
    }

    return static_cast<float>(val * sign);
}

bool bigendien;

short (*BigShort)(short l);
short (*LittleShort)(short l);
int (*BigLong)(int l);
int (*LittleLong)(int l);
float (*BigFloat)(float l);
float (*LittleFloat)(float l);

short ShortSwap(short l)
{
    uint16_t u = static_cast<uint16_t>(l);
    return static_cast<short>((u >> 8) | (u << 8));
}

short ShortNoSwap(short l)
{
    return l;
}

int LongSwap(int l)
{
    uint32_t u = static_cast<uint32_t>(l);
    return static_cast<int>(
        ((u & 0xff000000) >> 24) |
        ((u & 0x00ff0000) >> 8)  |
        ((u & 0x0000ff00) << 8)  |
        ((u & 0x000000ff) << 24)
    );
}

int LongNoSwap(int l)
{
    return l;
}

float FloatSwap(float f)
{
    uint32_t u = std::bit_cast<uint32_t>(f);
    uint32_t swapped = ((u & 0xff000000) >> 24) |
                       ((u & 0x00ff0000) >> 8)  |
                       ((u & 0x0000ff00) << 8)  |
                       ((u & 0x000000ff) << 24);
    return std::bit_cast<float>(swapped);
}

float FloatNoSwap(float f)
{
    return f;
}

void MSG_WriteChar(sizebuf_t* sb, int c)
{
    byte* buf = (byte*)SZ_GetSpace(sb, 1);
    buf[0] = static_cast<byte>(c);
}

void MSG_WriteByte(sizebuf_t* sb, int c)
{
    byte* buf = (byte*)SZ_GetSpace(sb, 1);
    buf[0] = static_cast<byte>(c);
}

void MSG_WriteShort(sizebuf_t* sb, int c)
{
    byte* buf = (byte*)SZ_GetSpace(sb, 2);
    buf[0] = c & 0xff;
    buf[1] = static_cast<byte>(c >> 8);
}

void MSG_WriteLong(sizebuf_t* sb, int c)
{
    byte* buf = (byte*)SZ_GetSpace(sb, 4);
    buf[0] = c & 0xff;
    buf[1] = (c >> 8) & 0xff;
    buf[2] = (c >> 16) & 0xff;
    buf[3] = c >> 24;
}

void MSG_WriteFloat(sizebuf_t* sb, float f)
{
    uint32_t val = std::bit_cast<uint32_t>(f);
    int swapped = LittleLong(static_cast<int>(val));
    SZ_Write(sb, &swapped, 4);
}

void MSG_WriteString(sizebuf_t* sb, const char* s)
{
    if (!s) {
        SZ_Write(sb, "", 1);
    } else {
        SZ_Write(sb, s, Q_strlen(s) + 1);
    }
}

int msg_readcount;
bool msg_badread;

void MSG_BeginReading(void)
{
    msg_readcount = 0;
    msg_badread = false;
}

int MSG_ReadChar(void)
{
    int c;
    if (msg_readcount + 1 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    c = (signed char)net_message.data[msg_readcount];
    msg_readcount++;
    return c;
}

int MSG_ReadByte(void)
{
    int c;
    if (msg_readcount + 1 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    c = (unsigned char)net_message.data[msg_readcount];
    msg_readcount++;
    return c;
}

int MSG_ReadShort(void)
{
    int c;
    if (msg_readcount + 2 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    c = (short)(net_message.data[msg_readcount] + (net_message.data[msg_readcount + 1] << 8));
    msg_readcount += 2;
    return c;
}

int MSG_ReadLong(void)
{
    int c;
    if (msg_readcount + 4 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    c = net_message.data[msg_readcount] + (net_message.data[msg_readcount + 1] << 8) + (net_message.data[msg_readcount + 2] << 16) + (net_message.data[msg_readcount + 3] << 24);
    msg_readcount += 4;
    return c;
}

float MSG_ReadFloat(void)
{
    if (msg_readcount + 4 > net_message.cursize) {
        msg_badread = true;
        return -1.0f;
    }
    uint32_t val;
    std::memcpy(&val, &net_message.data[msg_readcount], 4);
    msg_readcount += 4;
    val = static_cast<uint32_t>(LittleLong(static_cast<int>(val)));
    return std::bit_cast<float>(val);
}

char* MSG_ReadString(void)
{
    static char string[2048];
    int l, c;
    l = 0;
    do {
        c = MSG_ReadChar();
        if (c == -1 || c == 0) {
            break;
        }
        string[l] = static_cast<char>(c);
        l++;
    } while (l < static_cast<int>(sizeof(string) - 1));
    string[l] = 0;
    return string;
}

void SZ_Alloc(sizebuf_t* buf, int startsize)
{
    if (startsize < 256) {
        startsize = 256;
    }
    buf->data = (byte *) Hunk_Alloc(startsize, "sizebuf");
    buf->maxsize = startsize;
    buf->cursize = 0;
}

void SZ_Clear(sizebuf_t* buf)
{
    buf->cursize = 0;
}

void* SZ_GetSpace(sizebuf_t* buf, int length)
{
    void* data;
    if (buf->cursize + length > buf->maxsize) {
        if (!buf->allowoverflow) {
            Sys_Error("SZ_GetSpace: overflow without allowoverflow set");
        }
        if (length > buf->maxsize) {
            Sys_Error("SZ_GetSpace: %i is > full buffer size", length);
        }
        buf->overflowed = true;
        Con_Printf("SZ_GetSpace: overflow");
        SZ_Clear(buf);
    }
    data = buf->data + buf->cursize;
    buf->cursize += length;
    return data;
}

void SZ_Print(sizebuf_t* buf, const char* data)
{
    int len = Q_strlen(data) + 1;
    if (buf->data[buf->cursize - 1]) {
        Q_memcpy((byte*)SZ_GetSpace(buf, len), data, len);
    } else {
        Q_memcpy((byte*)SZ_GetSpace(buf, len - 1) - 1, data, len);
    }
}

eastl::string_view COM_FileExtension(eastl::string_view in)
{
    auto dot_pos = in.find('.');
    if (dot_pos == eastl::string_view::npos) {
        return "";
    }
    return in.substr(dot_pos + 1, 7);
}

void COM_FileBase(const char* in, char* out)
{
    eastl::string_view path(in);
    auto dot_pos = path.find_last_of('.');
    if (dot_pos == eastl::string_view::npos) {
        strcpy_s(out, 32, "?model?");
        return;
    }

    auto filename = path.substr(0, dot_pos);
    auto last_slash = filename.find_last_of("/\\");
    eastl::string_view base = (last_slash == eastl::string_view::npos) ? filename : filename.substr(last_slash + 1);

    if (base.empty()) {
        strcpy_s(out, 32, "?model?");
    } else {
        size_t copy_len = eastl::min(base.size(), size_t{31});
        std::memcpy(out, base.data(), copy_len);
        out[copy_len] = '\0';
    }
}

void COM_DefaultExtension(char* path, const char* extension)
{
    eastl::string_view path_view(path);
    auto last_slash = path_view.find_last_of("/\\");
    eastl::string_view filename = (last_slash == eastl::string_view::npos) ? path_view : path_view.substr(last_slash + 1);

    if (filename.find('.') != eastl::string_view::npos) {
        return;
    }

    strcat_s(path, 256, extension);
}

const char* COM_Parse(const char* data)
{
    if (!data) {
        return nullptr;
    }

    int len = 0;
    com_token[0] = '\0';

    while (true) {
        while (*data && static_cast<unsigned char>(*data) <= ' ') {
            data++;
        }
        if (*data == '\0') {
            return nullptr;
        }
        if (data[0] == '/' && data[1] == '/') {
            while (*data && *data != '\n') {
                data++;
            }
            continue;
        }
        break;
    }

    char c = *data;

    if (c == '\"') {
        data++;
        while (true) {
            c = *data++;
            if (c == '\"' || c == '\0') {
                com_token[len] = '\0';
                return data;
            }
            if (len < static_cast<int>(sizeof(com_token)) - 1) {
                com_token[len++] = c;
            }
        }
    }

    if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':') {
        com_token[0] = c;
        com_token[1] = '\0';
        return data + 1;
    }

    while (true) {
        c = *data;
        if (c == '\0' || static_cast<unsigned char>(c) <= ' ' ||
            c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':') {
            break;
        }
        if (len < static_cast<int>(sizeof(com_token)) - 1) {
            com_token[len++] = c;
        }
        data++;
    }

    com_token[len] = '\0';
    return data;
}

int COM_CheckParm(const char* parm)
{
    for (int i = 1; i < com_argc; i++) {
        if (!com_argv[i]) continue;
        if (!Q_strcmp(parm, com_argv[i])) return i;
    }
    return 0;
}

void COM_CheckRegistered(void)
{
    int h;
    unsigned short check[128];
    COM_OpenFile("gfx/pop.lmp", &h);
    static_registered = 0;

    if (h == -1) {
        Con_Printf("Playing shareware version.\n");
        if (com_modified) {
            Sys_Error("You must have the registered version to use modified games");
        }
        return;
    }

    Sys_FileRead(h, check, sizeof(check));
    COM_CloseFile(h);

    for (int i = 0; i < 128; i++) {
        if (pop[i] != (unsigned short)BigShort(check[i])) {
            Sys_Error("Corrupted data file.");
        }
    }

    Cvar::Set("cmdline", com_cmdline);
    Cvar::Set("registered", "1");
    static_registered = 1;
    Con_Printf("Playing registered version.\n");
}

void COM_Path_f(void);

void COM_InitArgv(int argc, char** argv)
{
    int n = 0;
    for (int j = 0; j < MAX_NUM_ARGVS && j < argc; ++j) {
        eastl::string_view arg(argv[j]);
        for (char c : arg) {
            if (n >= CMDLINE_LENGTH - 1) break;
            com_cmdline[n++] = c;
        }
        if (n >= CMDLINE_LENGTH - 1) break;
        com_cmdline[n++] = ' ';
    }
    com_cmdline[n] = '\0';

    bool safe = false;
    for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++) {
        largv[com_argc] = argv[com_argc];
        if (eastl::string_view(argv[com_argc]) == "-safe") {
            safe = true;
        }
    }

    if (safe) {
        for (int i = 0; i < NUM_SAFE_ARGVS; i++) {
            largv[com_argc] = const_cast<char*>(safeargvs[i]);
            com_argc++;
        }
    }

    largv[com_argc] = const_cast<char*>(argvdummy);
    com_argv = largv;

    if (COM_CheckParm("-rogue")) {
        rogue = true;
        standard_quake = false;
    }
    if (COM_CheckParm("-hipnotic")) {
        hipnotic = true;
        standard_quake = false;
    }
}

void COM_Init()
{
#ifdef SDL
    if (SDL_BYTEORDER == SDL_LIL_ENDIAN)
#else
    byte swaptest[2] = { 1, 0 };
    if (*(short*)swaptest == 1)
#endif
    {
        bigendien = false;
        BigShort = ShortSwap;
        LittleShort = ShortNoSwap;
        BigLong = LongSwap;
        LittleLong = LongNoSwap;
        BigFloat = FloatSwap;
        LittleFloat = FloatNoSwap;
    } else {
        bigendien = true;
        BigShort = ShortNoSwap;
        LittleShort = ShortSwap;
        BigLong = LongNoSwap;
        LittleLong = LongSwap;
        BigFloat = FloatNoSwap;
        LittleFloat = FloatSwap;
    }

    Cvar::Register(&registered);
    Cvar::Register(&cmdline);
    Cmd::AddCommand("path", COM_Path_f);

    COM_InitFilesystem();
    COM_CheckRegistered();
}

char* va(const char* format, ...)
{
    va_list argptr;
    static char string[1024];

    va_start(argptr, format);
    vsprintf_s(string, sizeof(string), format, argptr);
    va_end(argptr);

    return string;
}

int com_filesize;

typedef struct {
    char name[MAX_QPATH];
    int filepos, filelen;
} packfile_t;

typedef struct pack_s {
    char filename[MAX_OSPATH];
    int handle;
    int numfiles;
    packfile_t* files;
} pack_t;

typedef struct {
    char name[56];
    int filepos, filelen;
} dpackfile_t;

typedef struct {
    char id[4];
    int dirofs;
    int dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK 2048

char com_cachedir[MAX_OSPATH];
char com_gamedir[MAX_OSPATH];

struct SearchPath {
    eastl::string filename;
    pack_t* pack = nullptr;
};

static eastl::vector<SearchPath> com_searchpaths;

void COM_Path_f(void)
{
    Con_Printf("Current search path:\n");
    for (const auto& s : com_searchpaths) {
        if (s.pack) {
            Con_Printf("%s (%i files)\n", s.pack->filename, s.pack->numfiles);
        } else {
            Con_Printf("%s\n", s.filename.c_str());
        }
    }
}

void COM_WriteFile(const char* filename, void* data, int len)
{
    int handle;
    char name[MAX_OSPATH];

    sprintf_s(name, sizeof(name), "%s/%s", com_gamedir, filename);
    handle = Sys_FileOpenWrite(name);
    if (handle == -1) {
        Sys_Printf("COM_WriteFile: failed on %s\n", name);
        return;
    }

    Sys_Printf("COM_WriteFile: %s\n", name);
    Sys_FileWrite(handle, data, len);
    Sys_FileClose(handle);
}

void COM_CreatePath(const char* path)
{
    eastl::string temp(path);
    for (size_t i = 1; i < temp.size(); ++i) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            Sys_mkdir(temp.data());
            temp[i] = '/';
        }
    }
}

void COM_CopyFile(const char* netpath, const char* cachepath)
{
    int in, out;
    int remaining = Sys_FileOpenRead(netpath, &in);
    if (remaining == -1) {
        return;
    }
    COM_CreatePath(cachepath);
    out = Sys_FileOpenWrite(cachepath);
    if (out == -1) {
        Sys_FileClose(in);
        return;
    }

    char buf[4096];
    while (remaining > 0) {
        int count = eastl::min(remaining, static_cast<int>(sizeof(buf)));
        Sys_FileRead(in, buf, count);
        Sys_FileWrite(out, buf, count);
        remaining -= count;
    }

    Sys_FileClose(in);
    Sys_FileClose(out);
}

int COM_FindFile(const char* filename, int* handle, FILE** file)
{
    char netpath[MAX_OSPATH];
    char cachepath[MAX_OSPATH];
    int i;
    int findtime, cachetime;

    if (file && handle) Sys_Error("COM_FindFile: both handle and file set");
    if (!file && !handle) Sys_Error("COM_FindFile: neither handle or file set");

    auto it = com_searchpaths.begin();
    if (proghack) {
        if (std::strcmp(filename, "progs.dat") == 0) {
            if (it != com_searchpaths.end()) {
                ++it;
            }
        }
    }

    for (; it != com_searchpaths.end(); ++it) {
        const auto& search = *it;
        if (search.pack) {
            pack_t* pak = search.pack;
            for (i = 0; i < pak->numfiles; i++) {
                if (std::strcmp(pak->files[i].name, filename) == 0) {
                    Sys_Printf("PackFile: %s : %s\n", pak->filename, filename);
                    if (handle) {
                        *handle = pak->handle;
                        Sys_FileSeek(pak->handle, pak->files[i].filepos);
                    } else {
                        fopen_s(file, pak->filename, "rb");
                        if (*file) {
                            fseek(*file, pak->files[i].filepos, SEEK_SET);
                        }
                    }
                    com_filesize = pak->files[i].filelen;
                    return com_filesize;
                }
            }
        } else {
            if (!static_registered) {
                if (strchr(filename, '/') || strchr(filename, '\\')) continue;
            }

            sprintf_s(netpath, sizeof(netpath), "%s/%s", search.filename.c_str(), filename);
            findtime = Sys_FileTime(netpath);
            if (findtime == -1) continue;

            if (!com_cachedir[0]) {
                strcpy_s(cachepath, sizeof(cachepath), netpath);
            } else {
                sprintf_s(cachepath, sizeof(cachepath), "%s/%s", com_cachedir, netpath);
                cachetime = Sys_FileTime(cachepath);
                if (cachetime < findtime) {
                    COM_CopyFile(netpath, cachepath);
                }
                strcpy_s(netpath, sizeof(netpath), cachepath);
            }

            Sys_Printf("FindFile: %s\n", netpath);
            com_filesize = Sys_FileOpenRead(netpath, &i);
            if (handle) {
                *handle = i;
            } else {
                Sys_FileClose(i);
                fopen_s(file, netpath, "rb");
            }
            return com_filesize;
        }
    }

    Sys_Printf("FindFile: can't find %s\n", filename);
    if (handle) *handle = -1; else *file = NULL;
    com_filesize = -1;
    return -1;
}

void COM_CloseFile(int h)
{
    for (const auto& s : com_searchpaths) {
        if (s.pack && s.pack->handle == h) return;
    }
    Sys_FileClose(h);
}

cache_user_t* loadcache = nullptr;
byte* loadbuf = nullptr;
int loadsize = 0;

byte* COM_LoadFile(const char* path, HunkType usehunk)
{
    int h;
    byte* buf = nullptr;
    char base[32];
    int len = COM_OpenFile(path, &h);
    if (h == -1) return nullptr;

    COM_FileBase(path, base);

    switch (usehunk) {
    case HunkType::Hunk:
        buf = static_cast<byte*>(Hunk_Alloc(len + 1, base));
        break;
    case HunkType::HunkTemp:
        buf = static_cast<byte*>(Hunk_TempAlloc(len + 1));
        break;
    case HunkType::Zone:
        buf = static_cast<byte*>(Z_Malloc(len + 1));
        break;
    case HunkType::Cache:
        buf = static_cast<byte*>(Cache_Alloc(loadcache, len + 1, base));
        break;
    case HunkType::Stack:
        if (len + 1 > loadsize) {
            buf = static_cast<byte*>(Hunk_TempAlloc(len + 1));
        } else {
            buf = loadbuf;
        }
        break;
    default:
        Sys_Error("COM_LoadFile: bad usehunk");
    }

    if (!buf) Sys_Error("COM_LoadFile: not enough space for %s", path);

    buf[len] = 0;

    Draw_BeginDisc();
    Sys_FileRead(h, buf, len);
    COM_CloseFile(h);
    Draw_EndDisc();

    return buf;
}

void COM_LoadCacheFile(const char* path, struct cache_user_s* cu)
{
    loadcache = cu;
    COM_LoadFile(path, HunkType::Cache);
}

byte* COM_LoadStackFile(const char* path, void* buffer, int bufsize)
{
    loadbuf = static_cast<byte*>(buffer);
    loadsize = bufsize;
    return COM_LoadFile(path, HunkType::Stack);
}

pack_t* COM_LoadPackFile(char* packfile)
{
    dpackheader_t header;
    int i;
    packfile_t* newfiles;
    int numpackfiles;
    pack_t* pack;
    int packhandle;
    dpackfile_t info[MAX_FILES_IN_PACK];
    unsigned short crc;

    if (Sys_FileOpenRead(packfile, &packhandle) == -1) return NULL;

    Sys_FileRead(packhandle, (void*)&header, sizeof(header));
    if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K') {
        Sys_Error("%s is not a packfile", packfile);
    }

    header.dirofs = LittleLong(header.dirofs);
    header.dirlen = LittleLong(header.dirlen);
    numpackfiles = header.dirlen / sizeof(dpackfile_t);

    if (numpackfiles > MAX_FILES_IN_PACK) Sys_Error("%s has %i files", packfile, numpackfiles);
    if (numpackfiles != PAK0_COUNT) com_modified = true;

    newfiles = (packfile_t *) Hunk_Alloc(numpackfiles * sizeof(packfile_t), "packfile");
    Sys_FileSeek(packhandle, header.dirofs);
    Sys_FileRead(packhandle, (void*)info, header.dirlen);

    CRC_Init(crc);
    for (i = 0; i < header.dirlen; i++) {
        CRC_ProcessByte(crc, ((byte*)info)[i]);
    }
    if (crc != PAK0_CRC) com_modified = true;

    for (i = 0; i < numpackfiles; i++) {
        strcpy_s(newfiles[i].name, sizeof(newfiles[i].name), info[i].name);
        newfiles[i].filepos = LittleLong(info[i].filepos);
        newfiles[i].filelen = LittleLong(info[i].filelen);
    }

    pack = (pack_t *) Hunk_Alloc(sizeof(pack_t));
    strcpy_s(pack->filename, sizeof(pack->filename), packfile);
    pack->handle = packhandle;
    pack->numfiles = numpackfiles;
    pack->files = newfiles;

    Con_Printf("Added packfile %s (%i files)\n", packfile, numpackfiles);
    return pack;
}

void COM_AddGameDirectory(const char* dir)
{
    int i;
    pack_t* pak;
    char pakfile[MAX_OSPATH];

    strcpy_s(com_gamedir, sizeof(com_gamedir), dir);

    SearchPath search;
    search.filename = dir;
    com_searchpaths.insert(com_searchpaths.begin(), search);

    for (i = 0;; i++) {
        sprintf_s(pakfile, sizeof(pakfile), "%s/pak%i.pak", dir, i);
        pak = COM_LoadPackFile(pakfile);
        if (!pak) break;

        SearchPath sp;
        sp.pack = pak;
        com_searchpaths.insert(com_searchpaths.begin(), sp);
    }
}

void COM_InitFilesystem(void)
{
    int i, j;
    char basedir[MAX_OSPATH];

    i = COM_CheckParm("-basedir");
    if (i && i < com_argc - 1) {
        strcpy_s(basedir, sizeof(basedir), com_argv[i + 1]);
    } else {
        strcpy_s(basedir, sizeof(basedir), host_parms.basedir);
    }

    j = (int)strlen(basedir);
    if (j > 0 && ((basedir[j - 1] == '\\') || (basedir[j - 1] == '/'))) {
        basedir[j - 1] = 0;
    }

    i = COM_CheckParm("-cachedir");
    if (i && i < com_argc - 1) {
        if (com_argv[i + 1][0] == '-') {
            com_cachedir[0] = 0;
        } else {
            strcpy_s(com_cachedir, sizeof(com_cachedir), com_argv[i + 1]);
        }
    } else if (host_parms.cachedir) {
        strcpy_s(com_cachedir, sizeof(com_cachedir), host_parms.cachedir);
    } else {
        com_cachedir[0] = 0;
    }

    COM_AddGameDirectory(va("%s/" GAMENAME, basedir));

    if (COM_CheckParm("-rogue")) COM_AddGameDirectory(va("%s/rogue", basedir));
    if (COM_CheckParm("-hipnotic")) COM_AddGameDirectory(va("%s/hipnotic", basedir));

    i = COM_CheckParm("-game");
    if (i && i < com_argc - 1) {
        com_modified = true;
        COM_AddGameDirectory(va("%s/%s", basedir, com_argv[i + 1]));
    }

    i = COM_CheckParm("-path");
    if (i) {
        com_modified = true;
        com_searchpaths.clear();
        while (++i < com_argc) {
            if (!com_argv[i] || com_argv[i][0] == '+' || com_argv[i][0] == '-') break;
            SearchPath sp;
            if (COM_FileExtension(com_argv[i]) == "pak") {
                sp.pack = COM_LoadPackFile(com_argv[i]);
                if (!sp.pack) Sys_Error("Couldn't load packfile: %s", com_argv[i]);
            } else {
                sp.filename = com_argv[i];
            }
            com_searchpaths.insert(com_searchpaths.begin(), sp);
        }
    }

    if (COM_CheckParm("-proghack")) proghack = true;
}

} // namespace Common

//=============================================================================
// 3. Zone Memory Allocation (from zone.cpp)
//=============================================================================

namespace Common {

#define DYNAMIC_SIZE 0x100000
#define ZONEID 0x1d4a11
#define MINFRAGMENT 64

typedef struct memblock_s {
    int size;
    int tag;
    int id;
    struct memblock_s *next, *prev;
    int pad;
} memblock_t;

typedef struct {
    int size;
    memblock_t blocklist;
    memblock_t* rover;
} memzone_t;

void Cache_FreeLow(int new_low_hunk);
void Cache_FreeHigh(int new_high_hunk);

memzone_t* mainzone;

void Z_ClearZone(memzone_t* zone, int size)
{
    memblock_t* block;
    zone->blocklist.next = zone->blocklist.prev = block = (memblock_t*)((byte*)zone + sizeof(memzone_t));
    zone->blocklist.tag = 1;
    zone->blocklist.id = 0;
    zone->blocklist.size = 0;
    zone->rover = block;

    block->prev = block->next = &zone->blocklist;
    block->tag = 0;
    block->id = ZONEID;
    block->size = size - sizeof(memzone_t);
}

void Z_Free(void* ptr)
{
    memblock_t *block, *other;
    if (!ptr) Sys_Error("Z_Free: NULL pointer");

    block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));
    if (block->id != ZONEID) Sys_Error("Z_Free: freed a pointer without ZONEID");
    if (block->tag == 0) Sys_Error("Z_Free: freed a freed pointer");

    block->tag = 0;

    other = block->prev;
    if (!other->tag) {
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;
        if (block == mainzone->rover) mainzone->rover = other;
        block = other;
    }

    other = block->next;
    if (!other->tag) {
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;
        if (other == mainzone->rover) mainzone->rover = block;
    }
}

void* Z_Malloc(int size)
{
    Z_CheckHeap();
    void* buffer = Z_TagMalloc(size, 1);
    if (!buffer) Sys_Error("Z_Malloc: failed to allocate %d bytes", size);
    Q_memset(buffer, 0, size);
    return buffer;
}

void* Z_Realloc(void* ptr, int new_size)
{
    if (!ptr) return Z_Malloc(new_size);

    memblock_t* block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));
    if (block->id != ZONEID) Sys_Error("Z_Realloc: pointer missing ZONEID");
    if (block->tag == 0) Sys_Error("Z_Realloc: pointer already freed");

    int usable_old_size = block->size - sizeof(memblock_t) - 4;
    if (usable_old_size >= new_size) return ptr;

    void* new_ptr = Z_TagMalloc(new_size, 1);
    if (!new_ptr) {
        void* backup = malloc(usable_old_size);
        if (!backup) Sys_Error("Z_Realloc: System out of memory during backup");
        Q_memcpy(backup, ptr, usable_old_size);
        Z_Free(ptr);
        new_ptr = Z_TagMalloc(new_size, 1);
        if (!new_ptr) Sys_Error("Z_Realloc: failed to allocate %d bytes even after freeing old block", new_size);
        Q_memcpy(new_ptr, backup, usable_old_size);
        free(backup);
    } else {
        Q_memcpy(new_ptr, ptr, usable_old_size);
        Z_Free(ptr);
    }

    Q_memset((char*)new_ptr + usable_old_size, 0, new_size - usable_old_size);
    return new_ptr;
}

void* Z_TagMalloc(int size, int tag)
{
    int extra;
    memblock_t *start, *rover, *new_block, *base;
    if (!tag) Sys_Error("Z_TagMalloc: tried to use a 0 tag");

    size += sizeof(memblock_t) + 4;
    size = (size + 7) & ~7;

    base = rover = mainzone->rover;
    start = base->prev;

    do {
        if (rover == start) return NULL;
        if (rover->tag) {
            base = rover = rover->next;
        } else {
            rover = rover->next;
        }
    } while (base->tag || base->size < size);

    extra = base->size - size;
    if (extra > MINFRAGMENT) {
        new_block = (memblock_t*)((byte*)base + size);
        new_block->size = extra;
        new_block->tag = 0;
        new_block->prev = base;
        new_block->id = ZONEID;
        new_block->next = base->next;
        new_block->next->prev = new_block;
        base->next = new_block;
        base->size = size;
    }

    base->tag = tag;
    mainzone->rover = base->next;
    base->id = ZONEID;
    *(int*)((byte*)base + base->size - 4) = ZONEID;

    return (void*)((byte*)base + sizeof(memblock_t));
}

void Z_CheckHeap(void)
{
    memblock_t* block;
    for (block = mainzone->blocklist.next;; block = block->next) {
        if (block->next == &mainzone->blocklist) break;
        if ((byte*)block + block->size != (byte*)block->next) {
            Sys_Error("Z_CheckHeap: block size does not touch the next block\n");
        }
        if (block->next->prev != block) {
            Sys_Error("Z_CheckHeap: next block doesn't have proper back link\n");
        }
        if (!block->tag && !block->next->tag) {
            Sys_Error("Z_CheckHeap: two consecutive free blocks\n");
        }
    }
}

#define HUNK_SENTINAL 0x1df001ed

typedef struct {
    int sentinal;
    int size;
    char name[8];
} hunk_t;

byte* hunk_base;
int hunk_size;
int hunk_low_used;
int hunk_high_used;

qboolean hunk_tempactive;
int hunk_tempmark;

void Hunk_Check(void)
{
    hunk_t* h;
    for (h = (hunk_t*)hunk_base; (byte*)h != hunk_base + hunk_low_used;) {
        if (h->sentinal != HUNK_SENTINAL) Sys_Error("Hunk_Check: trahsed sentinal");
        if (h->size < 16 || h->size + (byte*)h - hunk_base > hunk_size) Sys_Error("Hunk_Check: bad size");
        h = (hunk_t*)((byte*)h + h->size);
    }
}

void* Hunk_Alloc(int size, const char* name)
{
    hunk_t* h;
    if (size < 0) Sys_Error("Hunk_Alloc: bad size: %i", size);

    size = sizeof(hunk_t) + ((size + 15) & ~15);
    if (hunk_size - hunk_low_used - hunk_high_used < size) {
        Sys_Error("Hunk_Alloc: failed on %i bytes", size);
    }

    h = (hunk_t*)(hunk_base + hunk_low_used);
    hunk_low_used += size;

    Cache_FreeLow(hunk_low_used);
    memset(h, 0, size);

    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy(h->name, name, 8);

    return (void*)(h + 1);
}

int Hunk_LowMark(void)
{
    return hunk_low_used;
}

void Hunk_FreeToLowMark(int mark)
{
    if (mark < 0 || mark > hunk_low_used) Sys_Error("Hunk_FreeToLowMark: bad mark %i", mark);
    memset(hunk_base + mark, 0, hunk_low_used - mark);
    hunk_low_used = mark;
}

int Hunk_HighMark(void)
{
    if (hunk_tempactive) {
        hunk_tempactive = false;
        Hunk_FreeToHighMark(hunk_tempmark);
    }
    return hunk_high_used;
}

void Hunk_FreeToHighMark(int mark)
{
    if (hunk_tempactive) {
        hunk_tempactive = false;
        Hunk_FreeToHighMark(hunk_tempmark);
    }
    if (mark < 0 || mark > hunk_high_used) Sys_Error("Hunk_FreeToHighMark: bad mark %i", mark);
    memset(hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
    hunk_high_used = mark;
}

void* Hunk_HighAllocName(int size, const char* name)
{
    hunk_t* h;
    if (size < 0) Sys_Error("Hunk_HighAllocName: bad size: %i", size);
    if (hunk_tempactive) {
        Hunk_FreeToHighMark(hunk_tempmark);
        hunk_tempactive = false;
    }

    size = sizeof(hunk_t) + ((size + 15) & ~15);
    if (hunk_size - hunk_low_used - hunk_high_used < size) {
        Con_Printf("Hunk_HighAlloc: failed on %i bytes\n", size);
        return NULL;
    }

    hunk_high_used += size;
    Cache_FreeHigh(hunk_high_used);
    h = (hunk_t*)(hunk_base + hunk_size - hunk_high_used);

    memset(h, 0, size);
    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy(h->name, name, 8);

    return (void*)(h + 1);
}

void* Hunk_TempAlloc(int size)
{
    void* buf;
    size = (size + 15) & ~15;

    if (hunk_tempactive) {
        Hunk_FreeToHighMark(hunk_tempmark);
        hunk_tempactive = false;
    }

    hunk_tempmark = Hunk_HighMark();
    buf = Hunk_HighAllocName(size, "temp");
    hunk_tempactive = true;

    return buf;
}

typedef struct cache_system_s {
    int size;
    cache_user_t* user;
    char name[16];
    struct cache_system_s *prev, *next;
    struct cache_system_s *lru_prev, *lru_next;
} cache_system_t;

cache_system_t* Cache_TryAlloc(int size, qboolean nobottom);
cache_system_t cache_head;

void Cache_Move(cache_system_t* c)
{
    cache_system_t* new_cs = Cache_TryAlloc(c->size, true);
    if (new_cs) {
        Q_memcpy(new_cs + 1, c + 1, c->size - sizeof(cache_system_t));
        new_cs->user = c->user;
        Q_memcpy(new_cs->name, c->name, sizeof(new_cs->name));
        Cache_Free(c->user);
        new_cs->user->data = (void*)(new_cs + 1);
    } else {
        Cache_Free(c->user);
    }
}

void Cache_FreeLow(int new_low_hunk)
{
    cache_system_t* c;
    while (1) {
        c = cache_head.next;
        if (c == &cache_head) return;
        if ((byte*)c >= hunk_base + new_low_hunk) return;
        Cache_Move(c);
    }
}

void Cache_FreeHigh(int new_high_hunk)
{
    cache_system_t *c, *prev = NULL;
    while (1) {
        c = cache_head.prev;
        if (c == &cache_head) return;
        if ((byte*)c + c->size <= hunk_base + hunk_size - new_high_hunk) return;
        if (c == prev) {
            Cache_Free(c->user);
        } else {
            Cache_Move(c);
            prev = c;
        }
    }
}

void Cache_UnlinkLRU(cache_system_t* cs)
{
    if (!cs->lru_next || !cs->lru_prev) Sys_Error("Cache_UnlinkLRU: NULL link");
    cs->lru_next->lru_prev = cs->lru_prev;
    cs->lru_prev->lru_next = cs->lru_next;
    cs->lru_prev = cs->lru_next = NULL;
}

void Cache_MakeLRU(cache_system_t* cs)
{
    if (cs->lru_next || cs->lru_prev) Sys_Error("Cache_MakeLRU: active link");
    cache_head.lru_next->lru_prev = cs;
    cs->lru_next = cache_head.lru_next;
    cs->lru_prev = &cache_head;
    cache_head.lru_next = cs;
}

cache_system_t* Cache_TryAlloc(int size, qboolean nobottom)
{
    cache_system_t *cs, *new_cs;

    if (!nobottom && cache_head.prev == &cache_head) {
        if (hunk_size - hunk_high_used - hunk_low_used < size) {
            Sys_Error("Cache_TryAlloc: %i is greater then free hunk", size);
        }

        new_cs = (cache_system_t*)(hunk_base + hunk_low_used);
        memset(new_cs, 0, sizeof(*new_cs));
        new_cs->size = size;

        cache_head.prev = cache_head.next = new_cs;
        new_cs->prev = new_cs->next = &cache_head;
        Cache_MakeLRU(new_cs);
        return new_cs;
    }

    new_cs = (cache_system_t*)(hunk_base + hunk_low_used);
    cs = cache_head.next;

    do {
        if (!nobottom || cs != cache_head.next) {
            if ((byte*)cs - (byte*)new_cs >= size) {
                memset(new_cs, 0, sizeof(*new_cs));
                new_cs->size = size;
                new_cs->next = cs;
                new_cs->prev = cs->prev;
                cs->prev->next = new_cs;
                cs->prev = new_cs;
                Cache_MakeLRU(new_cs);
                return new_cs;
            }
        }

        new_cs = (cache_system_t*)((byte*)cs + cs->size);
        if ((byte*)new_cs < hunk_base + hunk_low_used) {
            new_cs = (cache_system_t*)(hunk_base + hunk_low_used);
        }
        cs = cs->next;
    } while (cs != &cache_head);

    if (hunk_base + hunk_size - hunk_high_used - (byte*)new_cs >= size) {
        memset(new_cs, 0, sizeof(*new_cs));
        new_cs->size = size;
        new_cs->next = &cache_head;
        new_cs->prev = cache_head.prev;
        cache_head.prev->next = new_cs;
        cache_head.prev = new_cs;
        Cache_MakeLRU(new_cs);
        return new_cs;
    }

    return NULL;
}

void Cache_Flush(void)
{
    while (cache_head.next != &cache_head) {
        Cache_Free(cache_head.next->user);
    }
}

void Cache_Report(void)
{
    Con_DPrintf("%4.1f megabyte data cache\n", (hunk_size - hunk_high_used - hunk_low_used) / (float)(1024 * 1024));
}

void Cache_Init(void)
{
    cache_head.next = cache_head.prev = &cache_head;
    cache_head.lru_next = cache_head.lru_prev = &cache_head;
    Cmd::AddCommand("flush", Cache_Flush);
}

void Cache_Free(cache_user_t* c)
{
    cache_system_t* cs;
    if (!c->data) Sys_Error("Cache_Free: not allocated");
    cs = ((cache_system_t*)c->data) - 1;
    cs->prev->next = cs->next;
    cs->next->prev = cs->prev;
    cs->next = cs->prev = NULL;
    c->data = NULL;
    Cache_UnlinkLRU(cs);
}

void* Cache_Check(cache_user_t* c)
{
    cache_system_t* cs;
    if (!c->data) return NULL;
    cs = ((cache_system_t*)c->data) - 1;
    Cache_UnlinkLRU(cs);
    Cache_MakeLRU(cs);
    return c->data;
}

void* Cache_Alloc(cache_user_t* c, int size, const char* name)
{
    cache_system_t* cs;
    if (c->data) Sys_Error("Cache_Alloc: allready allocated");
    if (size <= 0) Sys_Error("Cache_Alloc: size %i", size);

    size = (size + sizeof(cache_system_t) + 15) & ~15;

    while (1) {
        cs = Cache_TryAlloc(size, false);
        if (cs) {
            strncpy_s(cs->name, sizeof(cs->name), name, sizeof(cs->name) - 1);
            c->data = (void*)(cs + 1);
            cs->user = c;
            break;
        }

        if (cache_head.lru_prev == &cache_head) Sys_Error("Cache_Alloc: out of memory");
        Cache_Free(cache_head.lru_prev->user);
    }

    return Cache_Check(c);
}

void Memory_Init(void* buf, int size)
{
    int p;
    int zonesize = DYNAMIC_SIZE;

    hunk_base = (byte*)buf;
    hunk_size = size;
    hunk_low_used = 0;
    hunk_high_used = 0;

    Cache_Init();
    p = COM_CheckParm("-zone");
    if (p) {
        if (p < com_argc - 1) {
            zonesize = Q_atoi(com_argv[p + 1]) * 1024;
        } else {
            Sys_Error("Memory_Init: you must specify a size in KB after -zone");
        }
    }

    mainzone = (memzone_t*)Hunk_Alloc(zonesize, "zone");
    Z_ClearZone(mainzone, zonesize);
}

} // namespace Common

//=============================================================================
// 4. CRC Checksum (from crc.cpp)
//=============================================================================

namespace Common {

namespace {
constexpr eastl::array<std::uint16_t, 256> crctable = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108,
    0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210,
    0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b,
    0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401,
    0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee,
    0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6,
    0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d,
    0xc7bc, 0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5,
    0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc,
    0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87, 0x4ce4,
    0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd,
    0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13,
    0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e,
    0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1,
    0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb,
    0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0,
    0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8,
    0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657,
    0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9,
    0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882,
    0x28a3, 0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92, 0xfd2e,
    0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07,
    0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d,
    0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};
constexpr std::uint16_t CRC_INIT_VALUE = 0xffff;
} // namespace

void CRC_Init(std::uint16_t& crcvalue) noexcept
{
    crcvalue = CRC_INIT_VALUE;
}

void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept
{
    crcvalue = (crcvalue << 8) ^ crctable[(crcvalue >> 8) ^ data];
}

} // namespace Common

//=============================================================================
// 5. Console Variables (from cvar.cpp)
//=============================================================================

namespace Cvar {

CvarRegistry& GetCvarRegistry()
{
    static CvarRegistry registry;
    return registry;
}

cvar_t* CvarRegistry::FindVar(eastl::string_view var_name)
{
    auto it = vars_map_.find(var_name);
    if (it != vars_map_.end()) {
        return it->second;
    }
    return nullptr;
}

float CvarRegistry::VariableValue(eastl::string_view var_name)
{
    cvar_t* var = FindVar(var_name);
    if (!var) return 0.0f;
    return Q_atof(var->string.c_str());
}

eastl::string_view CvarRegistry::VariableString(eastl::string_view var_name)
{
    cvar_t* var = FindVar(var_name);
    if (!var) return "";
    return eastl::string_view(var->string.data(), var->string.length());
}

eastl::string_view CvarRegistry::CompleteVariable(eastl::string_view partial)
{
    if (partial.empty()) return "";
    for (cvar_t* var = state_.vars; var; var = var->next) {
        eastl::string_view var_name(var->name.data(), var->name.length());
        if (var_name.starts_with(partial)) {
            return eastl::string_view(var->name.data(), var->name.length());
        }
    }
    return "";
}

void CvarRegistry::Set(eastl::string_view var_name, eastl::string_view value)
{
    cvar_t* var = FindVar(var_name);
    if (!var) {
        Con_Printf("Cvar::Set: variable %.*s not found\n", static_cast<int>(var_name.length()), var_name.data());
        return;
    }

    bool changed = (value != eastl::string_view(var->string.data(), var->string.length()));
    var->string = eastl::string(value.data(), value.length());
    var->value = Q_atof(var->string.c_str());

    if (var->server && changed) {
        if (sv.active) {
            SV_BroadcastPrintf("\"%s\" changed to \"%s\"\n", var->name.c_str(), var->string.c_str());
        }
    }
}

void CvarRegistry::SetValue(eastl::string_view var_name, float value)
{
    char val[32];
    std::snprintf(val, sizeof(val), "%f", value);
    Set(var_name, val);
}

void CvarRegistry::Register(cvar_t* variable)
{
    if (!variable) return;
    if (FindVar(eastl::string_view(variable->name.data(), variable->name.length()))) {
        Con_Printf("Can't register variable %s, allready defined\n", variable->name.c_str());
        return;
    }
    if (Cmd::Exists(eastl::string_view(variable->name.data(), variable->name.length()))) {
        Con_Printf("Cvar::Register: %s is a command\n", variable->name.c_str());
        return;
    }

    variable->value = Q_atof(variable->string.c_str());
    variable->next = state_.vars;
    state_.vars = variable;
    vars_map_.insert(eastl::make_pair(eastl::string_view(variable->name.data(), variable->name.length()), variable));
}

bool CvarRegistry::Command()
{
    cvar_t* v = FindVar(Cmd::Argv(0));
    if (!v) return false;

    if (Cmd::Argc() == 1) {
        Con_Printf("\"%s\" is \"%s\"\n", v->name.c_str(), v->string.c_str());
        return true;
    }

    Set(eastl::string_view(v->name.data(), v->name.length()), Cmd::Argv(1));
    return true;
}

void CvarRegistry::WriteVariables(std::ostream& f)
{
    for (cvar_t* var = state_.vars; var; var = var->next) {
        if (var->archive) {
            f << var->name.c_str() << " \"" << var->string.c_str() << "\"\n";
        }
    }
}

cvar_t* FindVar(eastl::string_view var_name) { return GetCvarRegistry().FindVar(var_name); }
float VariableValue(eastl::string_view var_name) { return GetCvarRegistry().VariableValue(var_name); }
eastl::string_view VariableString(eastl::string_view var_name) { return GetCvarRegistry().VariableString(var_name); }
eastl::string_view CompleteVariable(eastl::string_view partial) { return GetCvarRegistry().CompleteVariable(partial); }
void Set(eastl::string_view var_name, eastl::string_view value) { GetCvarRegistry().Set(var_name, value); }
void SetValue(eastl::string_view var_name, float value) { GetCvarRegistry().SetValue(var_name, value); }
void Register(cvar_t* variable) { GetCvarRegistry().Register(variable); }
bool Command() { return GetCvarRegistry().Command(); }
void WriteVariables(std::ostream& f) { GetCvarRegistry().WriteVariables(f); }

} // namespace Cvar

//=============================================================================
// 6. Commands & Command Buffer (from cmd.cpp)
//=============================================================================

namespace Cmd {

CommandRegistry& GetCommandRegistry()
{
    static CommandRegistry registry;
    return registry;
}

static void Wait_f(void)
{
    GetCommandRegistry().GetCmdWait() = true;
}

void CommandRegistry::BufferInit(void)
{
    cmd_text_.clear();
    cmd_text_.reserve(8192);
}

void CommandRegistry::BufferAddText(eastl::string_view text)
{
    if (cmd_text_.length() + text.length() >= 8192) {
        Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }
    cmd_text_.append(text.data(), text.length());
}

void CommandRegistry::BufferInsertText(eastl::string_view text)
{
    if (cmd_text_.length() + text.length() >= 8192) {
        Con_Printf("Cmd::BufferAddText: overflow\n");
        return;
    }
    cmd_text_.insert(0, text.data(), text.length());
}

void CommandRegistry::BufferExecute(void)
{
    while (!cmd_text_.empty()) {
        int quotes = 0;
        size_t i = 0;
        for (i = 0; i < cmd_text_.length(); ++i) {
            if (cmd_text_[i] == '"') quotes++;
            if (!(quotes & 1) && cmd_text_[i] == ';') break;
            if (cmd_text_[i] == '\n') break;
        }

        eastl::string line = cmd_text_.substr(0, i);

        if (i == cmd_text_.length()) {
            cmd_text_.clear();
        } else {
            cmd_text_.erase(0, i + 1);
        }

        ExecuteString(eastl::string_view(line.data(), line.length()), Source::Command);

        if (cmd_wait_) {
            cmd_wait_ = false;
            break;
        }
    }
}

static void StuffCmds_f(void)
{
    if (Cmd::Argc() != 1) {
        Con_Printf("stuffcmds : execute command line parameters\n");
        return;
    }

    eastl::string text;
    for (int i = 1; i < com_argc; i++) {
        if (!com_argv[i]) continue;
        if (!text.empty()) text += " ";
        text += com_argv[i];
    }

    if (text.empty()) return;

    eastl::string build;
    size_t i = 0;
    while (i < text.length()) {
        if (text[i] == '+') {
            i++;
            size_t j = i;
            while (j < text.length() && text[j] != '+' && text[j] != '-') j++;
            build += text.substr(i, j - i);
            build += "\n";
            i = j;
        } else {
            i++;
        }
    }

    if (!build.empty()) {
        Cmd::BufferInsertText(eastl::string_view(build.data(), build.length()));
    }
}

static void Exec_f(void)
{
    if (Cmd::Argc() != 2) {
        Con_Printf("exec <filename> : execute a script file\n");
        return;
    }

    int mark = Hunk_LowMark();
    eastl::string_view filename = Cmd::Argv(1);
    eastl::string filename_str(filename.data(), filename.length());
    char* f = (char*)COM_LoadHunkFile(filename_str.c_str());
    if (!f) {
        Con_Printf("couldn't exec %s\n", filename_str.c_str());
        return;
    }

    Con_Printf("execing %s\n", filename_str.c_str());
    Cmd::BufferInsertText(f);
    Hunk_FreeToLowMark(mark);
}

static void Echo_f(void)
{
    for (int i = 1; i < Cmd::Argc(); i++) {
        eastl::string_view arg = Cmd::Argv(i);
        Con_Printf("%.*s ", static_cast<int>(arg.length()), arg.data());
    }
    Con_Printf("\n");
}

static void Alias_f(void)
{
    auto& registry = GetCommandRegistry();
    if (Cmd::Argc() == 1) {
        Con_Printf("Current alias commands:\n");
        for (const auto& [name, value] : registry.GetAliases()) {
            Con_Printf("%s : %s\n", name.c_str(), value.c_str());
        }
        return;
    }

    eastl::string_view alias_name = Cmd::Argv(1);
    if (alias_name.length() >= 32) {
        Con_Printf("Alias name is too long\n");
        return;
    }

    eastl::string cmd;
    int c = Cmd::Argc();
    for (int i = 2; i < c; ++i) {
        eastl::string_view arg = Cmd::Argv(i);
        cmd.append(arg.data(), arg.length());
        cmd += " ";
    }
    cmd += "\n";

    registry.AddAlias(alias_name, eastl::string_view(cmd.data(), cmd.length()));
}

void CommandRegistry::Init(void)
{
    AddCommand("stuffcmds", StuffCmds_f);
    AddCommand("exec", Exec_f);
    AddCommand("echo", Echo_f);
    AddCommand("alias", Alias_f);
    AddCommand("cmd", ForwardToServer);
    AddCommand("wait", Wait_f);
}

void CommandRegistry::AddCommand(eastl::string_view cmd_name, xcommand_t function)
{
    if (host_initialized) Sys_Error("Cmd::AddCommand after host_initialized");
    if (Cvar::FindVar(cmd_name) != nullptr) {
        Con_Printf("Cmd::AddCommand: %.*s already defined as a var\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }
    if (Exists(cmd_name)) {
        Con_Printf("Cmd::AddCommand: %.*s already defined\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }

    commands_.emplace(eastl::string(cmd_name.data(), cmd_name.length()), std::move(function));
}

bool CommandRegistry::Exists(eastl::string_view cmd_name)
{
    return commands_.count(cmd_name) > 0;
}

eastl::string_view CommandRegistry::CompleteCommand(eastl::string_view partial)
{
    if (partial.empty()) return "";
    auto it = commands_.lower_bound(partial);
    if (it != commands_.end()) {
        eastl::string_view cmd_name(it->first.data(), it->first.length());
        if (cmd_name.length() >= partial.length()) {
            eastl::string_view prefix = cmd_name.substr(0, partial.length());
            if (Q_strcasecmp(eastl::string(prefix.data(), prefix.length()).c_str(), eastl::string(partial.data(), partial.length()).c_str()) == 0) {
                return eastl::string_view(it->first.data(), it->first.length());
            }
        }
    }
    return "";
}

int CommandRegistry::Argc(void)
{
    return static_cast<int>(cmd_argv_.size());
}

eastl::string_view CommandRegistry::Argv(int arg)
{
    if (arg < 0 || static_cast<size_t>(arg) >= cmd_argv_.size()) return "";
    return eastl::string_view(cmd_argv_[arg].data(), cmd_argv_[arg].length());
}

eastl::string_view CommandRegistry::Args(void)
{
    return cmd_args_;
}

void CommandRegistry::TokenizeString(eastl::string_view text)
{
    cmd_argv_.clear();
    cmd_args_ = "";
    if (text.empty()) return;

    const char* ptr = text.data();
    bool command_parsed = false;

    while (true) {
        while (*ptr && *ptr <= ' ' && *ptr != '\n') ptr++;
        if (*ptr == '\n') { ptr++; break; }
        if (!*ptr) return;

        if (command_parsed && cmd_args_.empty()) {
            cmd_args_ = eastl::string_view(ptr);
        }

        const char* next_ptr = COM_Parse(ptr);
        if (!next_ptr) return;
        ptr = next_ptr;

        if (!command_parsed) command_parsed = true;

        if (cmd_argv_.size() < 80) {
            cmd_argv_.push_back(com_token);
        }
    }
}

void CommandRegistry::ExecuteString(eastl::string_view text, Source src)
{
    state_.source = src;
    TokenizeString(text);

    if (cmd_argv_.empty()) return;

    const auto& cmd_name = cmd_argv_[0];

    auto cmd_it = commands_.find(cmd_name);
    if (cmd_it != commands_.end()) {
        cmd_it->second();
        return;
    }

    auto alias_it = aliases_.find(cmd_name);
    if (alias_it != aliases_.end()) {
        BufferInsertText(eastl::string_view(alias_it->second.data(), alias_it->second.length()));
        return;
    }

    if (!Cvar::Command()) {
        Con_Printf("Unknown command \"%s\"\n", cmd_name.c_str());
    }
}

void ForwardToServer(void)
{
    if (cls.state != ca_connected) {
        eastl::string_view cmd_name = Argv(0);
        Con_Printf("Can't \"%.*s\", not connected\n", static_cast<int>(cmd_name.length()), cmd_name.data());
        return;
    }
    if (cls.demoplayback) return;

    MSG_WriteByte(&cls.message, clc_stringcmd);
    
    eastl::string argv0(Argv(0).data(), Argv(0).length());
    if (Q_strcasecmp(argv0.c_str(), "cmd") != 0) {
        SZ_Print(&cls.message, argv0.c_str());
        SZ_Print(&cls.message, " ");
    }

    if (Argc() > 1) {
        eastl::string args_str(Args().data(), Args().length());
        SZ_Print(&cls.message, args_str.c_str());
    } else {
        SZ_Print(&cls.message, "\n");
    }
}

void BufferInit(void) { GetCommandRegistry().BufferInit(); }
void BufferAddText(eastl::string_view text) { GetCommandRegistry().BufferAddText(text); }
void BufferInsertText(eastl::string_view text) { GetCommandRegistry().BufferInsertText(text); }
void BufferExecute(void) { GetCommandRegistry().BufferExecute(); }
void Init(void) { GetCommandRegistry().Init(); }
void AddCommand(eastl::string_view cmd_name, xcommand_t function) { GetCommandRegistry().AddCommand(cmd_name, function); }
bool Exists(eastl::string_view cmd_name) { return GetCommandRegistry().Exists(cmd_name); }
eastl::string_view CompleteCommand(eastl::string_view partial) { return GetCommandRegistry().CompleteCommand(partial); }
int Argc(void) { return GetCommandRegistry().Argc(); }
eastl::string_view Argv(int arg) { return GetCommandRegistry().Argv(arg); }
eastl::string_view Args(void) { return GetCommandRegistry().Args(); }
void ExecuteString(eastl::string_view text, Source src) { GetCommandRegistry().ExecuteString(text, src); }

} // namespace Cmd

//=============================================================================
// 7. Math Library (from mathlib.cpp)
//=============================================================================

namespace Math {

float anglemod(float a)
{
    a = static_cast<float>((360.0 / 65536) * ((int)(a * (65536 / 360.0)) & 65535));
    return a;
}

void BOPS_Error()
{
    Sys_Error("BoxOnPlaneSide: Bad signbits");
}

void R_ConcatRotations(float in1[3][3], float in2[3][3], float out[3][3])
{
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
}

void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4])
{
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] + in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] + in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] + in1[2][2] * in2[2][3] + in1[2][3];
}

std::pair<int, int> FloorDivMod(double numer, double denom)
{
    int q, r;
    double x;

    if (numer >= 0.0) {
        x = std::floor(numer / denom);
        q = static_cast<int>(x);
        r = static_cast<int>(std::floor(numer - (x * denom)));
    } else {
        x = std::floor(-numer / denom);
        q = -static_cast<int>(x);
        r = static_cast<int>(std::floor(-numer - (x * denom)));
        if (r != 0) {
            q--;
            r = static_cast<int>(denom) - r;
        }
    }

    return {q, r};
}

int GreatestCommonDivisor(int i1, int i2)
{
    return std::gcd(i1, i2);
}

} // namespace Math

//=============================================================================
// 8. System/SDL Abstraction & main (from sys_sdl.cpp)
//=============================================================================

namespace Common {

void Sys_Printf(const char* fmt, ...)
{
    va_list argptr;
    char text[1024];

    va_start(argptr, fmt);
    vsprintf_s(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    fprintf(stderr, "%s", text);
}

void Sys_Quit(void)
{
    Host_Shutdown();
    exit(0);
}

void Sys_Init(void)
{
}

void Sys_LowFPPrecision(void)
{
}

void Sys_HighFPPrecision(void)
{
}

[[noreturn]] void Sys_Error(const char* error, ...)
{
    va_list argptr;
    char string[1024];

    va_start(argptr, error);
    vsprintf_s(string, sizeof(string), error, argptr);
    va_end(argptr);
    fprintf(stderr, "Error: %s\n", string);

    Host_Shutdown();
    exit(1);
}

#define MAX_HANDLES 10
FILE* sys_handles[MAX_HANDLES];

static int findhandle(void)
{
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (!sys_handles[i]) return i;
    }
    Sys_Error("out of handles");
}

static int Qfilelength(FILE* f)
{
    int pos = ftell(f);
    fseek(f, 0, SEEK_END);
    int end = ftell(f);
    fseek(f, pos, SEEK_SET);
    return end;
}

int Sys_FileOpenRead(const char* path, int* hndl)
{
    FILE* f;
    int i = findhandle();

    if (fopen_s(&f, path, "rb") != 0) {
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
    int i = findhandle();

    if (fopen_s(&f, path, "wb") != 0) {
        char errbuf[256];
        strerror_s(errbuf, sizeof(errbuf), errno);
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
    char* data;
    int size = 0, done;

    if (handle >= 0) {
        data = (char*)dst;
        while (count > 0) {
            done = (int)fread(data, 1, count, sys_handles[handle]);
            if (done == 0) break;
            data += done;
            count -= done;
            size += done;
        }
    }

    return size;
}

int Sys_FileWrite(int handle, const void* src, int count)
{
    const char* data;
    int size = 0, done;

    if (handle >= 0) {
        data = (const char*)src;
        while (count > 0) {
            done = (int)fwrite(data, 1, count, sys_handles[handle]);
            if (done == 0) break;
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
    if (fopen_s(&f, path, "rb") == 0) {
        fclose(f);
        return 1;
    }
    return -1;
}

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

void Sys_mkdir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

double Sys_FloatTime(void)
{
    static int starttime = 0;
    if (!starttime) {
        starttime = clock();
    }
    return (clock() - starttime) * 1.0 / CLOCKS_PER_SEC;
}

void moncontrol()
{
}

} // namespace Common

int main(int c, char** v)
{
    double time, oldtime, newtime;
    quakeparms_t parms;
    extern int vcrFile;
    extern qboolean recording;
    static int frame;

    moncontrol();
    signal(SIGFPE, SIG_IGN);

    parms.memsize = 64 * 1024 * 1024;
    parms.membase = malloc(parms.memsize);
    parms.basedir = basedir;
    parms.cachedir = cachedir;

    COM_InitArgv(c, v);
    parms.argc = com_argc;
    parms.argv = com_argv;

    Sys_Init();
    Host_Init(&parms);
    Cvar::Register(&sys_nostdout);

    oldtime = Sys_FloatTime() - 0.1;
    while (1) {
        newtime = Sys_FloatTime();
        time = newtime - oldtime;

        if (cls.state == ca_dedicated) {
            if (time < sys_ticrate.value && (vcrFile == -1 || recording)) {
                SDL_Delay(1);
                continue;
            }
            time = sys_ticrate.value;
        }

        if (time > sys_ticrate.value * 2) {
            oldtime = newtime;
        } else {
            oldtime += time;
        }

        if (++frame > 10) {
            moncontrol();
        }

        Host_Frame(static_cast<float>(time));
        moncontrol();
    }
}

//=============================================================================
// 9. WAD File Loading (from wad.cpp)
//=============================================================================

namespace Wad {

int wad_numlumps = 0;
lumpinfo_t* wad_lumps = nullptr;
byte* wad_base = nullptr;

void W_CleanupName(eastl::string_view in, eastl::span<char, 16> out)
{
    size_t i = 0;
    const size_t len = eastl::min(in.length(), static_cast<size_t>(16));

    for (; i < len; ++i) {
        if (in[i] == '\0') break;
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i])));
    }

    for (; i < 16; ++i) {
        out[i] = '\0';
    }
}

void W_LoadWadFile(eastl::string_view filename)
{
    eastl::string fname(filename.data(), filename.length());
    wad_base = static_cast<byte*>(COM_LoadHunkFile(fname.c_str()));
    if (!wad_base) {
        Sys_Error("W_LoadWadFile: couldn't load %s", fname.c_str());
    }

    auto* header = reinterpret_cast<wadinfo_t*>(wad_base);
    if (header->identification[0] != 'W' || header->identification[1] != 'A' ||
        header->identification[2] != 'D' || header->identification[3] != '2') {
        Sys_Error("Wad file %s doesn't have WAD2 id\n", fname.c_str());
    }

    wad_numlumps = LittleLong(header->numlumps);
    const int infotableofs = LittleLong(header->infotableofs);
    wad_lumps = reinterpret_cast<lumpinfo_t*>(wad_base + infotableofs);

    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        lump_p->filepos = LittleLong(lump_p->filepos);
        lump_p->size = LittleLong(lump_p->size);
        W_CleanupName(lump_p->name, eastl::span<char, 16>(lump_p->name, 16));
        if (lump_p->type == TYP_QPIC) {
            SwapPic(reinterpret_cast<qpic_t*>(wad_base + lump_p->filepos));
        }
    }
}

lumpinfo_t* W_GetLumpinfo(eastl::string_view name)
{
    eastl::array<char, 16> clean{};
    W_CleanupName(name, clean);

    lumpinfo_t* lump_p = wad_lumps;
    for (int i = 0; i < wad_numlumps; ++i, ++lump_p) {
        if (eastl::string_view(clean.data()) == lump_p->name) {
            return lump_p;
        }
    }

    eastl::string name_str(name.data(), name.length());
    Sys_Error("W_GetLumpinfo: %s not found", name_str.c_str());
}

void* W_GetLumpName(eastl::string_view name)
{
    lumpinfo_t* lump = W_GetLumpinfo(name);
    return reinterpret_cast<void*>(wad_base + lump->filepos);
}

void SwapPic(qpic_t* pic)
{
    if (pic != nullptr) {
        pic->width = LittleLong(pic->width);
        pic->height = LittleLong(pic->height);
    }
}

} // namespace Wad

namespace Common {
char* Sys_ConsoleInput(void)
{
    return nullptr;
}
}
