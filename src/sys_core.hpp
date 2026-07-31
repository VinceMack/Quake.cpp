// sys_core.hpp -- Subsystem Core: Types, Memory/Zone, Cvars, Commands, Math, System & WAD
#pragma once

#include <utility>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <numbers>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ostream>

#include <EASTL/variant.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <EASTL/functional.h>
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/vector.h>
#include <EASTL/span.h>
#include <EASTL/array.h>

//============================================================================
// Expected Result Type (using EASTL variant)
//============================================================================

template <typename T, typename E>
class Expected {
public:
    constexpr Expected(const T& val) : data_(val) {}
    constexpr Expected(T&& val) : data_(std::move(val)) {}
    constexpr Expected(const E& err) : data_(err) {}
    constexpr Expected(E&& err) : data_(std::move(err)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return eastl::holds_alternative<T>(data_); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr const T& value() const& { assert(has_value()); return eastl::get<T>(data_); }
    [[nodiscard]] constexpr T& value() & { assert(has_value()); return eastl::get<T>(data_); }
    [[nodiscard]] constexpr const E& error() const& { assert(!has_value()); return eastl::get<E>(data_); }
    [[nodiscard]] constexpr E& error() & { assert(!has_value()); return eastl::get<E>(data_); }

    [[nodiscard]] constexpr const T& operator*() const& { return value(); }
    [[nodiscard]] constexpr T& operator*() & { return value(); }
    [[nodiscard]] constexpr const T* operator->() const { return &value(); }
    [[nodiscard]] constexpr T* operator->() { return &value(); }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& def) const& {
        return has_value() ? eastl::get<T>(data_) : static_cast<T>(std::forward<U>(def));
    }
    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& def) && {
        return has_value() ? std::move(eastl::get<T>(data_)) : static_cast<T>(std::forward<U>(def));
    }

private:
    eastl::variant<T, E> data_;
};

template <typename E>
class Expected<void, E> {
public:
    constexpr Expected() = default;
    constexpr Expected(const E& err) : error_(err), has_value_(false) {}
    constexpr Expected(E&& err) : error_(std::move(err)), has_value_(false) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value_; }
    [[nodiscard]] constexpr const E& error() const { assert(!has_value_); return error_; }

private:
    E error_{};
    bool has_value_{true};
};

//============================================================================
// Common & Data Structures
//============================================================================

#if !defined BYTE_DEFINED
using byte = unsigned char;
#define BYTE_DEFINED 1
#endif

using qboolean = bool;

struct sizebuf_t {
    bool allowoverflow = false;
    bool overflowed = false;
    byte* data = nullptr;
    int maxsize = 0;
    int cursize = 0;

    [[nodiscard]] constexpr eastl::span<byte> as_span() noexcept { return {data, static_cast<size_t>(cursize)}; }
    [[nodiscard]] constexpr eastl::span<const byte> as_span() const noexcept { return {data, static_cast<size_t>(cursize)}; }
    [[nodiscard]] constexpr int remaining() const noexcept { return maxsize - cursize; }
    [[nodiscard]] constexpr bool has_overflowed() const noexcept { return overflowed; }
};

struct link_t {
    link_t *prev = nullptr, *next = nullptr;
    constexpr void clear() noexcept { prev = next = this; }
    void remove() noexcept { if (next && prev) { next->prev = prev; prev->next = next; } }
    void insert_before(link_t* before) noexcept {
        if (!before) return;
        next = before; prev = before->prev;
        if (prev) prev->next = this;
        before->prev = this;
    }
};

#define STRUCT_FROM_LINK(l, t, m) ((t*)((byte*)l - (intptr_t)&(((t*)0)->m)))

inline constexpr char Q_MAXCHAR = 0x7f, Q_MINCHAR = static_cast<char>(0x80);
inline constexpr short Q_MAXSHORT = 0x7fff, Q_MINSHORT = static_cast<short>(0x8000);
inline constexpr int Q_MAXINT = 0x7fffffff, Q_MININT = static_cast<int>(0x80000000);
inline constexpr int Q_MAXLONG = 0x7fffffff, Q_MINLONG = static_cast<int>(0x80000000);
inline constexpr int Q_MAXFLOAT = 0x7fffffff, Q_MINFLOAT = static_cast<int>(0x7fffffff);

struct cache_user_s;
using cache_user_t = cache_user_s;

namespace Common {

enum class HunkType { Zone = 0, Hunk = 1, HunkTemp = 2, Cache = 3, Stack = 4 };

inline void ClearLink(link_t* l) { if (l) l->clear(); }
inline void RemoveLink(link_t* l) { if (l) l->remove(); }
inline void InsertLinkBefore(link_t* l, link_t* before) { if (l) l->insert_before(before); }

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

void MSG_WriteChar(sizebuf_t* sb, int c);
void MSG_WriteByte(sizebuf_t* sb, int c);
void MSG_WriteShort(sizebuf_t* sb, int c);
void MSG_WriteLong(sizebuf_t* sb, int c);
void MSG_WriteFloat(sizebuf_t* sb, float f);
void MSG_WriteString(sizebuf_t* sb, const char* s);
inline void MSG_WriteCoord(sizebuf_t* sb, float f) { MSG_WriteShort(sb, static_cast<int>(f * 8)); }
inline void MSG_WriteAngle(sizebuf_t* sb, float f) { MSG_WriteByte(sb, (static_cast<int>(f) * 256 / 360) & 255); }

extern int msg_readcount;
extern bool msg_badread;

void MSG_BeginReading(void);
int MSG_ReadChar(void);
int MSG_ReadByte(void);
int MSG_ReadShort(void);
int MSG_ReadLong(void);
float MSG_ReadFloat(void);
char* MSG_ReadString(void);

inline float MSG_ReadCoord(void) { return MSG_ReadShort() * (1.0f / 8); }
inline float MSG_ReadAngle(void) { return MSG_ReadChar() * (360.0f / 256); }

inline void Q_memset(void* dest, int fill, int count) { std::memset(dest, fill, count); }
inline void Q_memcpy(void* dest, const void* src, int count) { std::memcpy(dest, src, count); }
inline void Q_strcpy(char* dest, const char* src) { std::strcpy(dest, src); }
inline void Q_strcpy(char* dest, eastl::string_view src) { std::memcpy(dest, src.data(), src.size()); dest[src.size()] = 0; }
inline void Q_strncpy(char* dest, const char* src, int count) { if (count <= 0) return; std::strncpy(dest, src, count - 1); dest[count - 1] = 0; }
inline void Q_strncpy(char* dest, eastl::string_view src, int count) {
    if (count <= 0) return;
    int len = static_cast<int>(eastl::min(src.size(), static_cast<size_t>(count - 1)));
    std::memcpy(dest, src.data(), len);
    dest[len] = 0;
}
inline int Q_strlen(const char* str) { return static_cast<int>(std::strlen(str)); }
inline const char* Q_strrchr(const char* s, char c) { return std::strrchr(s, c); }
inline char* Q_strrchr(char* s, char c) { return const_cast<char*>(Q_strrchr(static_cast<const char*>(s), c)); }
inline void Q_strcat(char* dest, const char* src) { std::strcat(dest, src); }
inline void Q_strcat(char* dest, eastl::string_view src) {
    dest += std::strlen(dest);
    std::memcpy(dest, src.data(), src.size());
    dest[src.size()] = 0;
}
inline int Q_strcmp(const char* s1, const char* s2) { return std::strcmp(s1, s2); }
inline int Q_strncmp(const char* s1, const char* s2, int count) { return std::strncmp(s1, s2, count); }
inline int Q_strcmp(eastl::string_view s1, eastl::string_view s2) { return s1.compare(s2); }

int Q_strncasecmp(const char* s1, const char* s2, int n);
int Q_strcasecmp(const char* s1, const char* s2);
int Q_strcasecmp(eastl::string_view s1, eastl::string_view s2);

inline void SZ_Write(sizebuf_t* buf, const void* data, int length) { Q_memcpy(SZ_GetSpace(buf, length), data, length); }

int Q_atoi(eastl::string_view str);
float Q_atof(eastl::string_view str);

extern char com_token[1024];
extern bool com_eof;

const char* COM_Parse(const char* data);
inline char* COM_Parse(char* data) { return const_cast<char*>(COM_Parse(static_cast<const char*>(data))); }

extern int com_argc;
extern char** com_argv;

int COM_CheckParm(const char* parm);
void COM_Init();
void COM_InitArgv(int argc, char** argv);

void COM_FileBase(const char* in, char* out);
void COM_DefaultExtension(char* path, const char* extension);

char* va(const char* format, ...);

extern int com_filesize;
extern char com_gamedir[128];

void COM_WriteFile(const char* filename, void* data, int len);
int COM_FindFile(const char* filename, int* handle, FILE** file);
byte* COM_LoadFile(const char* path, HunkType usehunk);

inline int COM_OpenFile(const char* filename, int* hndl) { return COM_FindFile(filename, hndl, nullptr); }
inline int COM_FOpenFile(const char* filename, FILE** file) { return COM_FindFile(filename, nullptr, file); }
void COM_CloseFile(int h);

byte* COM_LoadStackFile(const char* path, void* buffer, int bufsize);
inline byte* COM_LoadHunkFile(const char* path) { return COM_LoadFile(path, HunkType::Hunk); }
void COM_LoadCacheFile(const char* path, cache_user_s* cu);

extern bool standard_quake, rogue, hipnotic;
extern bool msg_suppress_1;

} // namespace Common

struct cache_user_s { void* data = nullptr; };

//============================================================================
// Zone Memory Allocation
//============================================================================

namespace Common {

void Memory_Init(void* buf, int size);
void Z_Free(void* ptr);
void* Z_Malloc(int size);
void* Z_Realloc(void* ptr, int new_size);
void* Z_TagMalloc(int size, int tag);
void Z_DumpHeap(void);
void Z_CheckHeap(void);
int Z_FreeMemory(void);

void* Hunk_Alloc(int size, const char* name = "unknown");
void* Hunk_HighAllocName(int size, const char* name);
int Hunk_LowMark(void);
void Hunk_FreeToLowMark(int mark);
int Hunk_HighMark(void);
void Hunk_FreeToHighMark(int mark);
void* Hunk_TempAlloc(int size);
void Hunk_Check(void);

void Cache_Flush(void);
void* Cache_Check(cache_user_t* c);
void Cache_Free(cache_user_t* c);
void* Cache_Alloc(cache_user_t* c, int size, const char* name);
void Cache_Report(void);

void CRC_Init(std::uint16_t& crcvalue) noexcept;
void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept;

} // namespace Common

//============================================================================
// Console Variables
//============================================================================

struct cvar_s {
    eastl::string name;
    eastl::string string;
    bool archive = false;
    bool server = false;
    float value = 0.0f;
    cvar_s* next = nullptr;
};
using cvar_t = cvar_s;
extern struct cvar_s registered;

namespace Cvar {

struct State { cvar_t* vars = nullptr; };

class CvarRegistry {
public:
    void Register(cvar_t* variable);
    void Set(eastl::string_view var_name, eastl::string_view value);
    void SetValue(eastl::string_view var_name, float value);
    float VariableValue(eastl::string_view var_name);
    eastl::string_view VariableString(eastl::string_view var_name);
    eastl::string_view CompleteVariable(eastl::string_view partial);
    bool Command();
    void WriteVariables(std::ostream& f);
    cvar_t* FindVar(eastl::string_view var_name);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

private:
    State state_;
    eastl::unordered_map<eastl::string_view, cvar_t*> vars_map_;
};

CvarRegistry& GetCvarRegistry();
inline State& state = GetCvarRegistry().GetState();

void Register(cvar_t* variable);
void Set(eastl::string_view var_name, eastl::string_view value);
void SetValue(eastl::string_view var_name, float value);
float VariableValue(eastl::string_view var_name);
eastl::string_view VariableString(eastl::string_view var_name);
eastl::string_view CompleteVariable(eastl::string_view partial);
bool Command();
void WriteVariables(std::ostream& f);
cvar_t* FindVar(eastl::string_view var_name);

} // namespace Cvar

//============================================================================
// Commands & Command Buffer
//============================================================================

namespace Cmd {

using xcommand_t = eastl::function<void()>;

enum class Source { Client, Command };

struct State { Source source = Source::Command; };

struct CaseInsensitiveLess {
    using is_transparent = void;
    template <typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const {
        eastl::string_view a(lhs.data(), lhs.size()), b(rhs.data(), rhs.size());
        return eastl::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
    }
};

class CommandRegistry {
public:
    void BufferInit(void);
    void BufferAddText(eastl::string_view text);
    void BufferInsertText(eastl::string_view text);
    void BufferExecute(void);
    void Init(void);
    void AddCommand(eastl::string_view cmd_name, xcommand_t function);
    bool Exists(eastl::string_view cmd_name);
    eastl::string_view CompleteCommand(eastl::string_view partial);
    int Argc(void);
    eastl::string_view Argv(int arg);
    eastl::string_view Args(void);
    void TokenizeString(eastl::string_view text);
    void ExecuteString(eastl::string_view text, Source src);

    State& GetState() { return state_; }
    const State& GetState() const { return state_; }

    const eastl::map<eastl::string, eastl::string, CaseInsensitiveLess>& GetAliases() const { return aliases_; }
    eastl::map<eastl::string, eastl::string, CaseInsensitiveLess>& GetAliases() { return aliases_; }
    bool& GetCmdWait() { return cmd_wait_; }

    void AddAlias(eastl::string_view name, eastl::string_view value) {
        aliases_[eastl::string(name.data(), name.length())] = eastl::string(value.data(), value.length());
    }

private:
    State state_;
    eastl::string cmd_text_;
    bool cmd_wait_ = false;
    eastl::map<eastl::string, eastl::string, CaseInsensitiveLess> aliases_;
    eastl::map<eastl::string, xcommand_t, CaseInsensitiveLess> commands_;
    eastl::vector<eastl::string> cmd_argv_;
    eastl::string_view cmd_args_;
};

CommandRegistry& GetCommandRegistry();
inline State& state = GetCommandRegistry().GetState();

void BufferInit(void);
void BufferAddText(eastl::string_view text);
void BufferInsertText(eastl::string_view text);
void BufferExecute(void);
void Init(void);
void AddCommand(eastl::string_view cmd_name, xcommand_t function);
bool Exists(eastl::string_view cmd_name);
eastl::string_view CompleteCommand(eastl::string_view partial);
int Argc(void);
eastl::string_view Argv(int arg);
eastl::string_view Args(void);
void ExecuteString(eastl::string_view text, Source src);
void ForwardToServer(void);

} // namespace Cmd

//============================================================================
// Math Library
//============================================================================

struct Vector3;
struct usercmd_t;

namespace Input {
void IN_Init();
void IN_Shutdown();
void IN_Commands();
void IN_Move(usercmd_t* cmd);
}

struct Vector3 {
    float x{0.0f}, y{0.0f}, z{0.0f};

    constexpr Vector3() = default;
    constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    constexpr Vector3(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]) {}

    constexpr float operator[](size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float& operator[](size_t i) { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr operator float*() { return &x; }
    constexpr operator const float*() const { return &x; }

    constexpr Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vector3 operator-() const { return {-x, -y, -z}; }
    constexpr Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vector3 operator/(float s) const { return {x / s, y / s, z / s}; }

    constexpr Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    constexpr Vector3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }

    constexpr bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vector3& o) const { return !(*this == o); }

    constexpr float dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vector3 cross(const Vector3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float normalize() {
        float len = length();
        if (len != 0.0f) { x /= len; y /= len; z /= len; }
        return len;
    }
};

struct usercmd_t {
    Vector3 viewangles;
    float forwardmove = 0.0f;
    float sidemove = 0.0f;
    float upmove = 0.0f;
};

using vec_t = float;
using vec5_t = vec_t[5];
using fixed4_t = int;
using fixed8_t = int;
using fixed16_t = int;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IS_NAN(x) std::isnan(x)

template <typename T, typename U>
inline constexpr float DotProduct(const T& a, const U& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

template <typename T, typename U, typename V>
inline constexpr void VectorSubtract(const T& a, const U& b, V&& c) { c[0] = a[0] - b[0]; c[1] = a[1] - b[1]; c[2] = a[2] - b[2]; }

template <typename T, typename U, typename V>
inline constexpr void VectorAdd(const T& a, const U& b, V&& c) { c[0] = a[0] + b[0]; c[1] = a[1] + b[1]; c[2] = a[2] + b[2]; }

template <typename T, typename U>
inline constexpr void VectorCopy(const T& a, U&& b) { b[0] = a[0]; b[1] = a[1]; b[2] = a[2]; }

namespace Math {

inline constexpr Vector3 vec3_origin = { 0.0f, 0.0f, 0.0f };

template <typename T, typename U, typename V>
inline void VectorMA(const T& veca, float scale, const U& vecb, V&& vecc) {
    vecc[0] = veca[0] + scale * vecb[0];
    vecc[1] = veca[1] + scale * vecb[1];
    vecc[2] = veca[2] + scale * vecb[2];
}

template <typename T>
inline vec_t Length(const T& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

template <typename T, typename U, typename V>
inline void CrossProduct(const T& v1, const U& v2, V&& cross) {
    cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
    cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
    cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

template <typename T>
inline float VectorNormalize(T&& v) {
    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length != 0.0f) {
        float ilength = 1.0f / length;
        v[0] *= ilength; v[1] *= ilength; v[2] *= ilength;
    }
    return length;
}

template <typename T>
inline void VectorInverse(T&& v) { v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2]; }

template <typename T, typename U>
inline void VectorScale(const T& in, vec_t scale, U&& out) { out[0] = in[0] * scale; out[1] = in[1] * scale; out[2] = in[2] * scale; }

void R_ConcatRotations(float in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4]);

std::pair<int, int> FloorDivMod(double numer, double denom);
int GreatestCommonDivisor(int i1, int i2);

template <typename T, typename U, typename V, typename W>
inline void AngleVectors(const T& angles, U&& forward, V&& right, W&& up) {
    float sy = std::sin(angles[1] * (std::numbers::pi_v<float> * 2 / 360));
    float cy = std::cos(angles[1] * (std::numbers::pi_v<float> * 2 / 360));
    float sp = std::sin(angles[0] * (std::numbers::pi_v<float> * 2 / 360));
    float cp = std::cos(angles[0] * (std::numbers::pi_v<float> * 2 / 360));
    float sr = std::sin(angles[2] * (std::numbers::pi_v<float> * 2 / 360));
    float cr = std::cos(angles[2] * (std::numbers::pi_v<float> * 2 / 360));

    forward[0] = cp * cy; forward[1] = cp * sy; forward[2] = -sp;
    right[0] = (-sr * sp * cy - cr * -sy); right[1] = (-sr * sp * sy - cr * cy); right[2] = -sr * cp;
    up[0] = (cr * sp * cy + sr * sy); up[1] = (cr * sp * sy - sr * cy); up[2] = cr * cp;
}

void BOPS_Error();

template <typename T, typename U, typename P>
inline int BoxOnPlaneSide(const T& emins, const U& emaxs, P* p) {
    if (p->signbits >= 8) { BOPS_Error(); return 0; }
    float dist1 = 0.0f, dist2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        bool neg = (p->signbits >> i) & 1;
        dist1 += p->normal[i] * (neg ? emins[i] : emaxs[i]);
        dist2 += p->normal[i] * (neg ? emaxs[i] : emins[i]);
    }
    int sides = 0;
    if (dist1 >= p->dist) sides = 1;
    if (dist2 < p->dist) sides |= 2;
    return sides;
}

float anglemod(float a);

} // namespace Math

template <typename T, typename U, typename P>
inline int BoxOnPlaneSideFast(const T& emins, const U& emaxs, const P* p) {
    if (p->type < 3) {
        if (p->dist <= emins[p->type]) return 1;
        if (p->dist >= emaxs[p->type]) return 2;
        return 3;
    }
    return Math::BoxOnPlaneSide(emins, emaxs, const_cast<P*>(p));
}

#define BOX_ON_PLANE_SIDE(emins, emaxs, p) BoxOnPlaneSideFast(emins, emaxs, p)

//============================================================================
// System / Non-portable declarations
//============================================================================

namespace Common {

int Sys_FileOpenRead(const char* path, int* hndl);
int Sys_FileOpenWrite(const char* path);
void Sys_FileClose(int handle);
void Sys_FileSeek(int handle, int position);
int Sys_FileRead(int handle, void* dest, int count);
int Sys_FileWrite(int handle, const void* data, int count);
int Sys_FileTime(const char* path);
void Sys_mkdir(const char* path);

[[noreturn]] void Sys_Error(const char* error, ...);
void Sys_Printf(const char* fmt, ...);
void Sys_Quit(void);
double Sys_FloatTime(void);
char* Sys_ConsoleInput(void);
void Sys_SendKeyEvents(void);

void Sys_LowFPPrecision(void);
void Sys_HighFPPrecision(void);
void Sys_SetFPCW(void);

} // namespace Common

//============================================================================
// WAD File Format
//============================================================================

enum class WadCompression : uint8_t { None = 0, LZSS = 1 };

enum class LumpType : uint8_t {
    None = 0, Label = 1, Lumpy = 64, Palette = 64, QTex = 65, QPic = 66, Sound = 67, MipTex = 68
};

constexpr int TYP_QPIC = 66;

#pragma pack(push, 1)
struct qpic_t {
    int width, height;
    byte data[4];
};

struct wadinfo_t {
    char identification[4];
    int numlumps;
    int infotableofs;
};

struct lumpinfo_t {
    int filepos;
    int disksize;
    int size;
    char type;
    char compression;
    char pad1, pad2;
    char name[16];
};
#pragma pack(pop)

namespace Wad {

extern int wad_numlumps;
extern lumpinfo_t* wad_lumps;
extern byte* wad_base;

void W_LoadWadFile(eastl::string_view filename);
void W_CleanupName(eastl::string_view in, eastl::span<char, 16> out);
[[nodiscard]] lumpinfo_t* W_GetLumpinfo(eastl::string_view name);
[[nodiscard]] void* W_GetLumpName(eastl::string_view name);

void SwapPic(qpic_t* pic);

} // namespace Wad
