// common.cpp -- misc functions used in client and server

#include "quakedef.hpp"
#include <bit>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>

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


cvar_t registered = { "registered", "0", {}, {}, {}, {} };
cvar_t cmdline = { "cmdline", "0", false, true, {}, {} };

namespace Common {

#define NUM_SAFE_ARGVS 7

static char* largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static const char* argvdummy = " ";

static const char* safeargvs[NUM_SAFE_ARGVS] = { "-stdvid", "-nolan", "-nosound",
    "-nocdaudio", "-nojoy", "-nomouse",
    "-dibonly" };

bool com_modified; // set true if using non-id files

bool proghack;

int static_registered = 1; // only for startup check, then set

bool msg_suppress_1 = false;

void COM_InitFilesystem(void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT 339
#define PAK0_CRC 32981

char com_token[1024];
int com_argc;
char** com_argv;

#define CMDLINE_LENGTH 256
char com_cmdline[CMDLINE_LENGTH];

bool standard_quake = true, rogue, hipnotic;

// this graphic needs to be in the pak file to use registered features
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

/*


All of Quake's data access is through a hierchal file system, but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.  This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory.  The base directory is
only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that all generated files (savegames, screenshots, demos, config files) will be saved to.  This can be overridden with the "-game" command line parameter.  The game directory can never be changed while quake is executing.  This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.

The "cache directory" is only used during development to save network bandwidth, especially over ISDN / T1 lines.  If there is a cache directory
specified, when a file is found by the normal search path, it will be mirrored
into the cache directory, then opened there.



FIXME:
The file "parms.txt" will be read out of the game directory and appended to the current command line arguments to allow different games to initialize startup parms differently.  This could be used to add a "-sspeed 22050" for the high quality sound edition.  Because they are added at the end, they will not override an explicit setting on the original command line.

*/

//============================================================================

// ClearLink is used for new headnodes
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

/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

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
    std::strcpy(dest, src);
}

void Q_strncpy(char* dest, const char* src, int count)
{
    std::strncpy(dest, src, count);
    if (count > 0) {
        // Ensure null termination if src was longer or equal
        // Wait, the original code only set a single null terminator if count was not decremented to 0:
        // while (*src && count--) { *dest++ = *src++; }
        // if (count) { *dest++ = 0; }
        // In other words, if the src was longer than count, the destination is NOT null-terminated!
        // std::strncpy does NOT null-terminate if the count is reached.
        // So std::strncpy behaves exactly like the original in that respect!
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
    std::strcat(dest, src);
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

int Q_atoi(std::string_view str)
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

    // Check for hex
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

    // Check for character
    if (str[pos] == '\'') {
        if (pos + 1 < str.size()) {
            return sign * static_cast<int>(static_cast<unsigned char>(str[pos + 1]));
        }
        return 0;
    }

    // Assume decimal
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

float Q_atof(std::string_view str)
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

    // Check for hex
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

    // Check for character
    if (str[pos] == '\'') {
        if (pos + 1 < str.size()) {
            return static_cast<float>(sign * static_cast<int>(static_cast<unsigned char>(str[pos + 1])));
        }
        return 0.0f;
    }

    // Assume decimal
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

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

#ifdef SDL
#include "SDL.h"
#endif

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

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar(sizebuf_t* sb, int c)
{
    byte* buf;

#ifdef PARANOID
    if (c < -128 || c > 127) {
        Sys_Error("MSG_WriteChar: range error");
    }

#endif

    buf = (byte*)SZ_GetSpace(sb, 1);
    buf[0] = static_cast<byte>(c);
}

void MSG_WriteByte(sizebuf_t* sb, int c)
{
    byte* buf;

#ifdef PARANOID
    if (c < 0 || c > 255) {
        Sys_Error("MSG_WriteByte: range error");
    }

#endif

    buf = (byte*)SZ_GetSpace(sb, 1);
    buf[0] = static_cast<byte>(c);
}

void MSG_WriteShort(sizebuf_t* sb, int c)
{
    byte* buf;

#ifdef PARANOID
    if (c < ((short)0x8000) || c > (short)0x7fff) {
        Sys_Error("MSG_WriteShort: range error");
    }

#endif

    buf = (byte*)SZ_GetSpace(sb, 2);
    buf[0] = c & 0xff;
    buf[1] = static_cast<byte>(c >> 8);
}

void MSG_WriteLong(sizebuf_t* sb, int c)
{
    byte* buf;

    buf = (byte*)SZ_GetSpace(sb, 4);
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

//
// reading functions
//
int msg_readcount;
bool msg_badread;

void MSG_BeginReading(void)
{
    msg_readcount = 0;
    msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
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

//===========================================================================

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
    int len;

    len = Q_strlen(data) + 1;

    // byte * cast to keep VC++ happy
    if (buf->data[buf->cursize - 1]) {
        Q_memcpy((byte*)SZ_GetSpace(buf, len), data, len); // no trailing 0
    } else {
        Q_memcpy((byte*)SZ_GetSpace(buf, len - 1) - 1, data,
            len); // write over trailing 0
    }
}

//============================================================================

/*
============
COM_FileExtension
============
*/
std::string_view COM_FileExtension(std::string_view in)
{
    auto dot_pos = in.find('.');
    if (dot_pos == std::string_view::npos) {
        return "";
    }
    return in.substr(dot_pos + 1, 7);
}

/*
============
COM_FileBase
============
*/
void COM_FileBase(const char* in, char* out)
{
    std::string_view path(in);
    auto dot_pos = path.find_last_of('.');
    if (dot_pos == std::string_view::npos) {
        strcpy_s(out, 32, "?model?");
        return;
    }

    auto filename = path.substr(0, dot_pos);
    auto last_slash = filename.find_last_of("/\\");
    std::string_view base = (last_slash == std::string_view::npos) ? filename : filename.substr(last_slash + 1);

    if (base.empty()) {
        strcpy_s(out, 32, "?model?");
    } else {
        size_t copy_len = std::min(base.size(), size_t{31});
        std::memcpy(out, base.data(), copy_len);
        out[copy_len] = '\0';
    }
}

/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension(char* path, const char* extension)
{
    std::string_view path_view(path);
    auto last_slash = path_view.find_last_of("/\\");
    std::string_view filename = (last_slash == std::string_view::npos) ? path_view : path_view.substr(last_slash + 1);

    if (filename.find('.') != std::string_view::npos) {
        return; // it has an extension
    }

    strcat_s(path, 256, extension);
}

/*
==============
COM_Parse

Parse a token out of a string
==============
*/
const char* COM_Parse(const char* data)
{
    if (!data) {
        return nullptr;
    }

    int len = 0;
    com_token[0] = '\0';

    while (true) {
        // Skip whitespace
        while (*data && static_cast<unsigned char>(*data) <= ' ') {
            data++;
        }

        if (*data == '\0') {
            return nullptr;
        }

        // Skip // comments
        if (data[0] == '/' && data[1] == '/') {
            while (*data && *data != '\n') {
                data++;
            }
            continue;
        }

        break;
    }

    char c = *data;

    // Handle quoted strings specially
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

    // Parse single characters
    if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':') {
        com_token[0] = c;
        com_token[1] = '\0';
        return data + 1;
    }

    // Parse a regular word
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

/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParm(const char* parm)
{
    int i;

    for (i = 1; i < com_argc; i++) {
        if (!com_argv[i]) {
            continue; // NEXTSTEP sometimes clears appkit vars.
        }

        if (!Q_strcmp(parm, com_argv[i])) {
            return i;
        }
    }

    return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
void COM_CheckRegistered(void)
{
    int h;
    unsigned short check[128];
    int i;

    COM_OpenFile("gfx/pop.lmp", &h);
    static_registered = 0;

    if (h == -1) {
#if WINDED
        Sys_Error("This dedicated server requires a full registered copy of Quake");
#endif
        Con_Printf("Playing shareware version.\n");
        if (com_modified) {
            Sys_Error("You must have the registered version to use modified games");
        }

        return;
    }

    Sys_FileRead(h, check, sizeof(check));
    COM_CloseFile(h);

    for (i = 0; i < 128; i++) {
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

/*
================
COM_InitArgv
================
*/
void COM_InitArgv(int argc, char** argv)
{
    // reconstitute the command line for the cmdline externally visible cvar
    int n = 0;
    for (int j = 0; j < MAX_NUM_ARGVS && j < argc; ++j) {
        std::string_view arg(argv[j]);
        for (char c : arg) {
            if (n >= CMDLINE_LENGTH - 1) {
                break;
            }
            com_cmdline[n++] = c;
        }
        if (n >= CMDLINE_LENGTH - 1) {
            break;
        }
        com_cmdline[n++] = ' ';
    }
    com_cmdline[n] = '\0';

    bool safe = false;

    for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc);
        com_argc++) {
        largv[com_argc] = argv[com_argc];
        if (std::string_view(argv[com_argc]) == "-safe") {
            safe = true;
        }
    }

    if (safe) {
        // force all the safe-mode switches. Note that we reserved extra space in
        // case we need to add these, so we don't need an overflow check
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

/*
================
COM_Init
================
*/
void COM_Init()
{
// set the byte swapping variables in a portable manner
#ifdef SDL
    // This is necessary because egcs 1.1.1 mis-compiles swaptest with -O2
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

/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
FIXME: make this buffer size safe someday
============
*/
char* va(const char* format, ...)
{
    va_list argptr;
    static char string[1024];

    va_start(argptr, format);
    vsprintf_s(string, sizeof(string), format, argptr);
    va_end(argptr);

    return string;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int com_filesize;

//
// in memory
//

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

//
// on disk
//
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
    std::string filename;
    pack_t* pack = nullptr;
};

static std::vector<SearchPath> com_searchpaths;

/*
============
COM_Path_f

============
*/
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

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
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

/*
============
COM_CreatePath

Only used for CopyFile
============
*/
void COM_CreatePath(const char* path)
{
    std::string temp(path);
    for (size_t i = 1; i < temp.size(); ++i) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            Sys_mkdir(temp.data());
            temp[i] = '/';
        }
    }
}

/*
===========
COM_CopyFile

Copies a file over from the net to the local cache, creating any directories
needed.  This is for the convenience of developers using ISDN from home.
===========
*/
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
        int count = std::min(remaining, static_cast<int>(sizeof(buf)));
        Sys_FileRead(in, buf, count);
        Sys_FileWrite(out, buf, count);
        remaining -= count;
    }

    Sys_FileClose(in);
    Sys_FileClose(out);
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
===========
*/
int COM_FindFile(const char* filename, int* handle, FILE** file)
{
    char netpath[MAX_OSPATH];
    char cachepath[MAX_OSPATH];
    int i;
    int findtime, cachetime;

    if (file && handle) {
        Sys_Error("COM_FindFile: both handle and file set");
    }

    if (!file && !handle) {
        Sys_Error("COM_FindFile: neither handle or file set");
    }

    //
    // search through the path, one element at a time
    //
    auto it = com_searchpaths.begin();
    if (proghack) { // gross hack to use quake 1 progs with quake 2 maps
        if (std::strcmp(filename, "progs.dat") == 0) {
            if (it != com_searchpaths.end()) {
                ++it;
            }
        }
    }

    for (; it != com_searchpaths.end(); ++it) {
        const auto& search = *it;
        // is the element a pak file?
        if (search.pack) {
            // look through all the pak file elements
            pack_t* pak = search.pack;
            for (i = 0; i < pak->numfiles; i++) {
                if (std::strcmp(pak->files[i].name, filename) == 0) { // found it!
                    Sys_Printf("PackFile: %s : %s\n", pak->filename, filename);
                    if (handle) {
                        *handle = pak->handle;
                        Sys_FileSeek(pak->handle, pak->files[i].filepos);
                    } else { // open a new file on the pakfile
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
            // check a file in the directory tree
            if (!static_registered) { // if not a registered version, don't ever go beyond base
                if (strchr(filename, '/') || strchr(filename, '\\')) {
                    continue;
                }
            }

            sprintf_s(netpath, sizeof(netpath), "%s/%s", search.filename.c_str(), filename);

            findtime = Sys_FileTime(netpath);
            if (findtime == -1) {
                continue;
            }

            // see if the file needs to be updated in the cache
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

    if (handle) {
        *handle = -1;
    } else {
        *file = NULL;
    }

    com_filesize = -1;

    return -1;
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile(int h)
{
    for (const auto& s : com_searchpaths) {
        if (s.pack && s.pack->handle == h) {
            return;
        }
    }

    Sys_FileClose(h);
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
cache_user_t* loadcache = nullptr;
byte* loadbuf = nullptr;
int loadsize = 0;

byte* COM_LoadFile(const char* path, HunkType usehunk)
{
    int h;
    byte* buf = nullptr;
    char base[32];
    int len;

    // look for it in the filesystem or pack files
    len = COM_OpenFile(path, &h);
    if (h == -1) {
        return nullptr;
    }

    // extract the filename base name for hunk tag
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

    if (!buf) {
        Sys_Error("COM_LoadFile: not enough space for %s", path);
    }

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

// uses temp hunk if larger than bufsize
byte* COM_LoadStackFile(const char* path, void* buffer, int bufsize)
{
    loadbuf = static_cast<byte*>(buffer);
    loadsize = bufsize;
    return COM_LoadFile(path, HunkType::Stack);
}

/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
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

    if (Sys_FileOpenRead(packfile, &packhandle) == -1) {
        //              Con_Printf ("Couldn't open %s\n", packfile);
        return NULL;
    }

    Sys_FileRead(packhandle, (void*)&header, sizeof(header));
    if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K') {
        Sys_Error("%s is not a packfile", packfile);
    }

    header.dirofs = LittleLong(header.dirofs);
    header.dirlen = LittleLong(header.dirlen);

    numpackfiles = header.dirlen / sizeof(dpackfile_t);

    if (numpackfiles > MAX_FILES_IN_PACK) {
        Sys_Error("%s has %i files", packfile, numpackfiles);
    }

    if (numpackfiles != PAK0_COUNT) {
        com_modified = true; // not the original file
    }

    newfiles = (packfile_t *) Hunk_Alloc(numpackfiles * sizeof(packfile_t), "packfile");

    Sys_FileSeek(packhandle, header.dirofs);
    Sys_FileRead(packhandle, (void*)info, header.dirlen);

    // crc the directory to check for modifications
    CRC_Init(&crc);
    for (i = 0; i < header.dirlen; i++) {
        CRC_ProcessByte(&crc, ((byte*)info)[i]);
    }
    if (crc != PAK0_CRC) {
        com_modified = true;
    }

    // parse the directory
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

/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
void COM_AddGameDirectory(const char* dir)
{
    int i;
    pack_t* pak;
    char pakfile[MAX_OSPATH];

    strcpy_s(com_gamedir, sizeof(com_gamedir), dir);

    //
    // add the directory to the search path
    //
    SearchPath search;
    search.filename = dir;
    com_searchpaths.insert(com_searchpaths.begin(), search);

    //
    // add any pak files in the format pak0.pak pak1.pak, ...
    //
    for (i = 0;; i++) {
        sprintf_s(pakfile, sizeof(pakfile), "%s/pak%i.pak", dir, i);
        pak = COM_LoadPackFile(pakfile);
        if (!pak) {
            break;
        }

        SearchPath sp;
        sp.pack = pak;
        com_searchpaths.insert(com_searchpaths.begin(), sp);
    }

    //
    // add the contents of the parms.txt file to the end of the command line
    //
}

/*
================
COM_InitFilesystem
================
*/
void COM_InitFilesystem(void)
{
    int i, j;
    char basedir[MAX_OSPATH];

    //
    // -basedir <path>
    // Overrides the system supplied base directory (under GAMENAME)
    //
    i = COM_CheckParm("-basedir");
    if (i && i < com_argc - 1) {
        strcpy_s(basedir, sizeof(basedir), com_argv[i + 1]);
    } else {
        strcpy_s(basedir, sizeof(basedir), host_parms.basedir);
    }

    j = (int)strlen(basedir);

    if (j > 0) {
        if ((basedir[j - 1] == '\\') || (basedir[j - 1] == '/')) {
            basedir[j - 1] = 0;
        }
    }

    //
    // -cachedir <path>
    // Overrides the system supplied cache directory (NULL or /qcache)
    // -cachedir - will disable caching.
    //
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

    //
    // start up with GAMENAME by default (id1)
    //
    COM_AddGameDirectory(va("%s/" GAMENAME, basedir));

    if (COM_CheckParm("-rogue")) {
        COM_AddGameDirectory(va("%s/rogue", basedir));
    }

    if (COM_CheckParm("-hipnotic")) {
        COM_AddGameDirectory(va("%s/hipnotic", basedir));
    }

    //
    // -game <gamedir>
    // Adds basedir/gamedir as an override game
    //
    i = COM_CheckParm("-game");
    if (i && i < com_argc - 1) {
        com_modified = true;
        COM_AddGameDirectory(va("%s/%s", basedir, com_argv[i + 1]));
    }

    //
    // -path <dir or packfile> [<dir or packfile>] ...
    // Fully specifies the exact serach path, overriding the generated one
    //
    i = COM_CheckParm("-path");
    if (i) {
        com_modified = true;
        com_searchpaths.clear();
        while (++i < com_argc) {
            if (!com_argv[i] || com_argv[i][0] == '+' || com_argv[i][0] == '-') {
                break;
            }

            SearchPath sp;
            if (COM_FileExtension(com_argv[i]) == "pak") {
                sp.pack = COM_LoadPackFile(com_argv[i]);
                if (!sp.pack) {
                    Sys_Error("Couldn't load packfile: %s", com_argv[i]);
                }
            } else {
                sp.filename = com_argv[i];
            }

            com_searchpaths.insert(com_searchpaths.begin(), sp);
        }
    }

    if (COM_CheckParm("-proghack")) {
        proghack = true;
    }
}

} // namespace Common
