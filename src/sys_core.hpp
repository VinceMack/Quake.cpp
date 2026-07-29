// sys_core.hpp -- Subsystem Core: Types, Memory/Zone, Cvars, Commands, Math, System & WAD
#pragma once

#include <variant>
#include <utility>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <numbers>
#include <cstdio>
#include <ostream>

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <EASTL/functional.h>
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/vector.h>
#include <EASTL/span.h>

//============================================================================
// Expected Result Type (from core_types.hpp)
//============================================================================

template <typename T, typename E>
class Expected {
public:
    constexpr Expected(const T& val) : data_(val) {}
    constexpr Expected(T&& val) : data_(std::move(val)) {}
    constexpr Expected(const E& err) : data_(err) {}
    constexpr Expected(E&& err) : data_(std::move(err)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] constexpr const T& value() const& {
        assert(has_value() && "bad expected access");
        return std::get<T>(data_);
    }

    [[nodiscard]] constexpr T& value() & {
        assert(has_value() && "bad expected access");
        return std::get<T>(data_);
    }

    [[nodiscard]] constexpr const E& error() const& {
        assert(!has_value() && "bad expected access");
        return std::get<E>(data_);
    }

    [[nodiscard]] constexpr E& error() & {
        assert(!has_value() && "bad expected access");
        return std::get<E>(data_);
    }

    [[nodiscard]] constexpr const T& operator*() const& { return value(); }
    [[nodiscard]] constexpr T& operator*() & { return value(); }
    [[nodiscard]] constexpr const T* operator->() const { return &value(); }
    [[nodiscard]] constexpr T* operator->() { return &value(); }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& default_value) const& {
        return has_value() ? std::get<T>(data_) : static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& default_value) && {
        return has_value() ? std::move(std::get<T>(data_)) : static_cast<T>(std::forward<U>(default_value));
    }

private:
    std::variant<T, E> data_;
};

template <typename E>
class Expected<void, E> {
public:
    constexpr Expected() : has_value_(true) {}
    constexpr Expected(const E& err) : error_(err), has_value_(false) {}
    constexpr Expected(E&& err) : error_(std::move(err)), has_value_(false) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value_; }

    [[nodiscard]] constexpr const E& error() const {
        assert(!has_value_ && "bad expected access");
        return error_;
    }

private:
    E error_{};
    bool has_value_;
};

//============================================================================
// Common & Data Structures (from common.hpp)
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
};

struct link_t {
    link_t* prev = nullptr;
    link_t* next = nullptr;
};

#define STRUCT_FROM_LINK(l, t, m) ((t*)((byte*)l - (intptr_t)&(((t*)0)->m)))

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

struct cache_user_s;
typedef struct cache_user_s cache_user_t;

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

void MSG_WriteChar(sizebuf_t* sb, int c);
void MSG_WriteByte(sizebuf_t* sb, int c);
void MSG_WriteShort(sizebuf_t* sb, int c);
void MSG_WriteLong(sizebuf_t* sb, int c);
void MSG_WriteFloat(sizebuf_t* sb, float f);
void MSG_WriteString(sizebuf_t* sb, const char* s);
inline void MSG_WriteCoord(sizebuf_t* sb, float f) {
    MSG_WriteShort(sb, static_cast<int>(f * 8));
}
inline void MSG_WriteAngle(sizebuf_t* sb, float f) {
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

inline float MSG_ReadCoord(void) {
    return MSG_ReadShort() * (1.0f / 8);
}
inline float MSG_ReadAngle(void) {
    return MSG_ReadChar() * (360.0f / 256);
}

void Q_memset(void* dest, int fill, int count);
void Q_memcpy(void* dest, const void* src, int count);
void Q_strcpy(char* dest, const char* src);
inline void Q_strcpy(char* dest, eastl::string_view src) {
    Q_memcpy(dest, src.data(), static_cast<int>(src.size()));
    dest[src.size()] = 0;
}
void Q_strncpy(char* dest, const char* src, int count);
inline void Q_strncpy(char* dest, eastl::string_view src, int count) {
    int len = static_cast<int>(src.size());
    if (len > count) len = count;
    Q_memcpy(dest, src.data(), len);
    dest[len] = 0;
}
int Q_strlen(const char* str);
const char* Q_strrchr(const char* s, char c);
inline char* Q_strrchr(char* s, char c) {
    return const_cast<char*>(Q_strrchr(static_cast<const char*>(s), c));
}
void Q_strcat(char* dest, const char* src);
inline void Q_strcat(char* dest, eastl::string_view src) {
    dest += Q_strlen(dest);
    Q_memcpy(dest, src.data(), static_cast<int>(src.size()));
    dest[src.size()] = 0;
}
int Q_strcmp(const char* s1, const char* s2);
int Q_strncmp(const char* s1, const char* s2, int count);
int Q_strncasecmp(const char* s1, const char* s2, int n);

inline void SZ_Write(sizebuf_t* buf, const void* data, int length) {
    Q_memcpy(SZ_GetSpace(buf, length), data, length);
}

inline int Q_strcasecmp(const char* s1, const char* s2) {
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
inline int Q_strcmp(eastl::string_view s1, eastl::string_view s2) {
    return s1.compare(s2);
}
inline int Q_strcasecmp(eastl::string_view s1, eastl::string_view s2) {
    size_t min_len = eastl::min(s1.size(), s2.size());
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
int Q_atoi(eastl::string_view str);
float Q_atof(eastl::string_view str);

extern char com_token[1024];
extern bool com_eof;

const char* COM_Parse(const char* data);
inline char* COM_Parse(char* data) {
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

extern int com_filesize;
extern char com_gamedir[128];

void COM_WriteFile(const char* filename, void* data, int len);
int COM_FindFile(const char* filename, int* handle, FILE** file);
byte* COM_LoadFile(const char* path, HunkType usehunk);

inline int COM_OpenFile(const char* filename, int* hndl) {
    return COM_FindFile(filename, hndl, nullptr);
}
inline int COM_FOpenFile(const char* filename, FILE** file) {
    return COM_FindFile(filename, nullptr, file);
}
void COM_CloseFile(int h);

byte* COM_LoadStackFile(const char* path, void* buffer, int bufsize);
inline byte* COM_LoadHunkFile(const char* path) {
    return COM_LoadFile(path, HunkType::Hunk);
}
void COM_LoadCacheFile(const char* path, cache_user_s* cu);

extern bool standard_quake, rogue, hipnotic;
extern bool msg_suppress_1;

} // namespace Common

struct cache_user_s {
    void* data;
};

//============================================================================
// Zone Memory Allocation (from zone.hpp)
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

// CRC (from crc.hpp)
void CRC_Init(std::uint16_t& crcvalue) noexcept;
void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept;

} // namespace Common

//============================================================================
// Console Variables (from cvar.hpp)
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

struct State {
    cvar_t* vars = nullptr;
};

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
// Commands & Command Buffer (from cmd.hpp)
//============================================================================

namespace Cmd {

using xcommand_t = eastl::function<void()>;

enum class Source {
    Client,
    Command
};

struct State {
    Source source = Source::Command;
};

struct CaseInsensitiveLess {
    using is_transparent = void;
    template <typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const {
        eastl::string_view sv_lhs(lhs.data(), lhs.size());
        eastl::string_view sv_rhs(rhs.data(), rhs.size());
        return eastl::lexicographical_compare(
            sv_lhs.begin(), sv_lhs.end(),
            sv_rhs.begin(), sv_rhs.end(),
            [](char a, char b) {
                char ca = (a >= 'a' && a <= 'z') ? static_cast<char>(a - ('a' - 'A')) : a;
                char cb = (b >= 'a' && b <= 'z') ? static_cast<char>(b - ('a' - 'A')) : b;
                return ca < cb;
            }
        );
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
// Math Library (from mathlib.hpp)
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
    float x;
    float y;
    float z;

    constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    constexpr Vector3(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]) {}

    constexpr float operator[](size_t index) const {
        if (index == 0) return x;
        if (index == 1) return y;
        return z;
    }
    constexpr float& operator[](size_t index) {
        if (index == 0) return x;
        if (index == 1) return y;
        return z;
    }

    constexpr operator float*() { return &x; }
    constexpr operator const float*() const { return &x; }

    constexpr Vector3 operator+(const Vector3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
    constexpr Vector3 operator-(const Vector3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }
    constexpr Vector3 operator-() const {
        return {-x, -y, -z};
    }
    constexpr Vector3 operator*(float scale) const {
        return {x * scale, y * scale, z * scale};
    }
    constexpr Vector3 operator/(float scale) const {
        return {x / scale, y / scale, z / scale};
    }

    constexpr Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;
    }
    constexpr Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;
    }
    constexpr Vector3& operator*=(float scale) {
        x *= scale; y *= scale; z *= scale;
        return *this;
    }
    constexpr Vector3& operator/=(float scale) {
        x /= scale; y /= scale; z /= scale;
        return *this;
    }

    constexpr bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    constexpr bool operator!=(const Vector3& other) const {
        return !(*this == other);
    }

    constexpr float dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    constexpr Vector3 cross(const Vector3& other) const {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float normalize() {
        float len = length();
        if (len != 0.0f) {
            x /= len;
            y /= len;
            z /= len;
        }
        return len;
    }
};

struct usercmd_t {
    Vector3 viewangles;
    float forwardmove = 0.0f;
    float sidemove = 0.0f;
    float upmove = 0.0f;
};

typedef float vec_t;
typedef vec_t vec5_t[5];

typedef int fixed4_t;
typedef int fixed8_t;
typedef int fixed16_t;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct mplane_s;

#define IS_NAN(x) std::isnan(x)

template <typename T, typename U>
inline constexpr float DotProduct(const T& a, const U& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

template <typename T, typename U, typename V>
inline constexpr void VectorSubtract(const T& a, const U& b, V&& c) {
    c[0] = a[0] - b[0];
    c[1] = a[1] - b[1];
    c[2] = a[2] - b[2];
}

template <typename T, typename U, typename V>
inline constexpr void VectorAdd(const T& a, const U& b, V&& c) {
    c[0] = a[0] + b[0];
    c[1] = a[1] + b[1];
    c[2] = a[2] + b[2];
}

template <typename T, typename U>
inline constexpr void VectorCopy(const T& a, U&& b) {
    b[0] = a[0];
    b[1] = a[1];
    b[2] = a[2];
}

namespace Math {

inline constexpr Vector3 vec3_origin = { 0.0f, 0.0f, 0.0f };

template <typename T, typename U, typename V>
inline void VectorMA(const T& veca, float scale, const U& vecb, V&& vecc) {
    vecc[0] = veca[0] + scale * vecb[0];
    vecc[1] = veca[1] + scale * vecb[1];
    vecc[2] = veca[2] + scale * vecb[2];
}

template <typename T>
inline vec_t Length(const T& v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

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
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    }
    return length;
}

template <typename T>
inline void VectorInverse(T&& v) {
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

template <typename T, typename U>
inline void VectorScale(const T& in, vec_t scale, U&& out) {
    out[0] = in[0] * scale;
    out[1] = in[1] * scale;
    out[2] = in[2] * scale;
}

void R_ConcatRotations(float in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4]);

std::pair<int, int> FloorDivMod(double numer, double denom);
int GreatestCommonDivisor(int i1, int i2);

template <typename T, typename U, typename V, typename W>
inline void AngleVectors(const T& angles, U&& forward, V&& right, W&& up) {
    float angle;
    float sr, sp, sy, cr, cp, cy;

    angle = angles[1] * (std::numbers::pi_v<float> * 2 / 360);
    sy = std::sin(angle);
    cy = std::cos(angle);
    angle = angles[0] * (std::numbers::pi_v<float> * 2 / 360);
    sp = std::sin(angle);
    cp = std::cos(angle);
    angle = angles[2] * (std::numbers::pi_v<float> * 2 / 360);
    sr = std::sin(angle);
    cr = std::cos(angle);

    forward[0] = cp * cy;
    forward[1] = cp * sy;
    forward[2] = -sp;
    right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
    right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
    right[2] = -1 * sr * cp;
    up[0] = (cr * sp * cy + -sr * -sy);
    up[1] = (cr * sp * sy + -sr * cy);
    up[2] = cr * cp;
}

void BOPS_Error();

template <typename T, typename U, typename P>
inline int BoxOnPlaneSide(const T& emins, const U& emaxs, P* p) {
    float dist1, dist2;
    int sides;

    switch (p->signbits) {
    case 0:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        break;
    case 1:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        break;
    case 2:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        break;
    case 3:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        break;
    case 4:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        break;
    case 5:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        break;
    case 6:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        break;
    case 7:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        break;
    default:
        dist1 = dist2 = 0;
        BOPS_Error();
        break;
    }

    sides = 0;
    if (dist1 >= p->dist) {
        sides = 1;
    }

    if (dist2 < p->dist) {
        sides |= 2;
    }

    return sides;
}

float anglemod(float a);

} // namespace Math

template <typename T, typename U, typename P>
inline int BoxOnPlaneSideFast(const T& emins, const U& emaxs, const P* p) {
    if (p->type < 3) {
        if (p->dist <= emins[p->type]) {
            return 1;
        }
        if (p->dist >= emaxs[p->type]) {
            return 2;
        }
        return 3;
    }
    return Math::BoxOnPlaneSide(emins, emaxs, const_cast<P*>(p));
}

#define BOX_ON_PLANE_SIDE(emins, emaxs, p) BoxOnPlaneSideFast(emins, emaxs, p)

//============================================================================
// System / Non-portable declarations (from sys.hpp)
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
// WAD File Format (from wad.hpp)
//============================================================================

enum class WadCompression : uint8_t {
    None = 0,
    LZSS = 1
};

enum class LumpType : uint8_t {
    None = 0,
    Label = 1,
    Lumpy = 64,
    Palette = 64,
    QTex = 65,
    QPic = 66,
    Sound = 67,
    MipTex = 68
};

constexpr int TYP_QPIC = 66;

#pragma pack(push, 1)
typedef struct {
    int width, height;
    byte data[4];
} qpic_t;

typedef struct {
    char identification[4];
    int numlumps;
    int infotableofs;
} wadinfo_t;

typedef struct {
    int filepos;
    int disksize;
    int size;
    char type;
    char compression;
    char pad1, pad2;
    char name[16];
} lumpinfo_t;
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
