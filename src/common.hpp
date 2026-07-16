// common.h -- general type definitions and common macros
#pragma once

#include <stdint.h>

#if !defined BYTE_DEFINED
using byte = unsigned char;
#define BYTE_DEFINED 1
#endif

using qboolean = bool;

//============================================================================

struct sizebuf_t {
    bool allowoverflow = false; // if false, do a Sys_Error
    bool overflowed = false;    // set to true if the buffer size failed
    byte* data = nullptr;
    int maxsize = 0;
    int cursize = 0;
};

//============================================================================

struct link_t {
    link_t* prev = nullptr;
    link_t* next = nullptr;
};

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define STRUCT_FROM_LINK(l, t, m) ((t*)((byte*)l - (intptr_t)&(((t*)0)->m)))

//============================================================================

inline constexpr char Q_MAXCHAR = 0x7f;
inline constexpr short Q_MAXSHORT = 0x7fff;
inline constexpr int Q_MAXINT = 0x7fffffff;
inline constexpr int Q_MAXLONG = 0x7fffffff;
inline constexpr int Q_MAXFLOAT = 0x7fffffff;

inline constexpr char Q_MINCHAR = static_cast<char>(0x80);
inline constexpr short Q_MINSHORT = static_cast<short>(0x8000);
inline constexpr int Q_MININT = static_cast<int>(0x80000000);
inline constexpr int Q_MINLONG = static_cast<int>(0x80000000);
inline constexpr int Q_MINFLOAT = 0x7fffffff;

//============================================================================

struct cache_user_s;

namespace Common {

enum class HunkType {
    Zone = 0,
    Hunk = 1,
    HunkTemp = 2,
    Cache = 3,
    Stack = 4
};

void ClearLink(link_t* l);
void RemoveLink(link_t* l);
void InsertLinkBefore(link_t* l, link_t* before);

void SZ_Alloc(sizebuf_t* buf, int startsize);
void SZ_Clear(sizebuf_t* buf);
void* SZ_GetSpace(sizebuf_t* buf, int length);
void SZ_Print(sizebuf_t* buf, const char* data);

extern bool bigendien;

extern short (*BigShort)(short l);
extern short (*LittleShort)(short l);
extern int (*BigLong)(int l);
extern int (*LittleLong)(int l);
extern float (*BigFloat)(float l);
extern float (*LittleFloat)(float l);

//============================================================================

void MSG_WriteChar(sizebuf_t* sb, int c);
void MSG_WriteByte(sizebuf_t* sb, int c);
void MSG_WriteShort(sizebuf_t* sb, int c);
void MSG_WriteLong(sizebuf_t* sb, int c);
void MSG_WriteFloat(sizebuf_t* sb, float f);
void MSG_WriteString(sizebuf_t* sb, const char* s);
inline void MSG_WriteCoord(sizebuf_t* sb, float f)
{
    MSG_WriteShort(sb, static_cast<int>(f * 8));
}

inline void MSG_WriteAngle(sizebuf_t* sb, float f)
{
    MSG_WriteByte(sb, (static_cast<int>(f) * 256 / 360) & 255);
}

extern int msg_readcount;
extern bool msg_badread;

void MSG_BeginReading(void);
int MSG_ReadChar(void);
int MSG_ReadByte(void);
int MSG_ReadShort(void);
int MSG_ReadLong(void);
float MSG_ReadFloat(void);
char* MSG_ReadString(void);

inline float MSG_ReadCoord(void)
{
    return MSG_ReadShort() * (1.0f / 8);
}

inline float MSG_ReadAngle(void)
{
    return MSG_ReadChar() * (360.0f / 256);
}

//============================================================================

void Q_memset(void* dest, int fill, int count);
void Q_memcpy(void* dest, const void* src, int count);
void Q_strcpy(char* dest, const char* src);
inline void Q_strcpy(char* dest, std::string_view src)
{
    Q_memcpy(dest, src.data(), static_cast<int>(src.size()));
    dest[src.size()] = 0;
}
void Q_strncpy(char* dest, const char* src, int count);
inline void Q_strncpy(char* dest, std::string_view src, int count)
{
    int len = static_cast<int>(src.size());
    if (len > count) {
        len = count;
    }
    Q_memcpy(dest, src.data(), len);
    dest[len] = 0;
}
int Q_strlen(const char* str);
const char* Q_strrchr(const char* s, char c);
inline char* Q_strrchr(char* s, char c)
{
    return const_cast<char*>(Q_strrchr(static_cast<const char*>(s), c));
}
void Q_strcat(char* dest, const char* src);
inline void Q_strcat(char* dest, std::string_view src)
{
    dest += Q_strlen(dest);
    Q_memcpy(dest, src.data(), static_cast<int>(src.size()));
    dest[src.size()] = 0;
}
int Q_strcmp(const char* s1, const char* s2);
int Q_strncmp(const char* s1, const char* s2, int count);
int Q_strncasecmp(const char* s1, const char* s2, int n);

inline void SZ_Write(sizebuf_t* buf, const void* data, int length)
{
    Q_memcpy(SZ_GetSpace(buf, length), data, length);
}

inline int Q_strcasecmp(const char* s1, const char* s2)
{
    int c1, c2;
    do {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 != c2) {
            if (c1 >= 'a' && c1 <= 'z') c1 -= ('a' - 'A');
            if (c2 >= 'a' && c2 <= 'z') c2 -= ('a' - 'A');
            if (c1 != c2) return -1;
        }
    } while (c1);
    return 0;
}
inline int Q_strcmp(std::string_view s1, std::string_view s2)
{
    return s1.compare(s2);
}
inline int Q_strcasecmp(std::string_view s1, std::string_view s2)
{
    size_t min_len = std::min(s1.size(), s2.size());
    for (size_t i = 0; i < min_len; ++i) {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'a' && c1 <= 'z') c1 -= ('a' - 'A');
        if (c2 >= 'a' && c2 <= 'z') c2 -= ('a' - 'A');
        if (c1 < c2) return -1;
        if (c1 > c2) return 1;
    }
    if (s1.size() < s2.size()) return -1;
    if (s1.size() > s2.size()) return 1;
    return 0;
}
int Q_atoi(std::string_view str);
float Q_atof(std::string_view str);

//============================================================================

extern char com_token[1024];
extern bool com_eof;

const char* COM_Parse(const char* data);
inline char* COM_Parse(char* data)
{
    return const_cast<char*>(COM_Parse(static_cast<const char*>(data)));
}

extern int com_argc;
extern char** com_argv;

int COM_CheckParm(const char* parm);
void COM_Init();
void COM_InitArgv(int argc, char** argv);

void COM_FileBase(const char* in, char* out);
void COM_DefaultExtension(char* path, const char* extension);

char* va(const char* format, ...);

//============================================================================

extern int com_filesize;

extern char com_gamedir[MAX_OSPATH];

void COM_WriteFile(const char* filename, void* data, int len);

int COM_FindFile(const char* filename, int* handle, FILE** file);
byte* COM_LoadFile(const char* path, HunkType usehunk);

inline int COM_OpenFile(const char* filename, int* hndl)
{
    return COM_FindFile(filename, hndl, nullptr);
}

inline int COM_FOpenFile(const char* filename, FILE** file)
{
    return COM_FindFile(filename, nullptr, file);
}
void COM_CloseFile(int h);

byte* COM_LoadStackFile(const char* path, void* buffer, int bufsize);
inline byte* COM_LoadHunkFile(const char* path)
{
    return COM_LoadFile(path, HunkType::Hunk);
}
void COM_LoadCacheFile(const char* path, cache_user_s* cu);

extern bool standard_quake, rogue, hipnotic;
extern bool msg_suppress_1;

} // namespace Common

struct cvar_s;
extern struct cvar_s registered;
