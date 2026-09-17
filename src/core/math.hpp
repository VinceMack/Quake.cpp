// math.hpp -- 3D vector algebra, trigonometry, and bounding box plane classification
#pragma once

#include <cmath>
#include <numbers>
#include <numeric>
#include <utility>
#include <cstdint>
#include <cstddef>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IS_NAN(x) std::isnan(x)

using vec_t = float;
using vec5_t = vec_t[5];
using fixed4_t = int;
using fixed8_t = int;
using fixed16_t = int;

// Forward declaration for error reporting
namespace Common {
[[noreturn]] void Sys_Error(const char* error, ...);
}

//============================================================================
// Vector3
//============================================================================

struct Vector3 {
    float x { 0.0f }, y { 0.0f }, z { 0.0f };

    constexpr Vector3() = default;
    constexpr Vector3(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z)
    { }
    constexpr Vector3(const float* ptr)
        : x(ptr[0])
        , y(ptr[1])
        , z(ptr[2])
    { }

    constexpr float operator[](size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float& operator[](size_t i) { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr operator float*() { return &x; }
    constexpr operator const float*() const { return &x; }

    constexpr Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    constexpr Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    constexpr Vector3 operator-() const { return { -x, -y, -z }; }
    constexpr Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }
    constexpr Vector3 operator/(float s) const { return { x / s, y / s, z / s }; }

    constexpr Vector3& operator+=(const Vector3& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vector3& operator-=(const Vector3& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vector3& operator*=(float s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr Vector3& operator/=(float s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    constexpr bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vector3& o) const { return !(*this == o); }

    constexpr float dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vector3 cross(const Vector3& o) const
    {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float normalize()
    {
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

// Legacy global helper templates
template <typename T, typename U> inline constexpr float DotProduct(const T& a, const U& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

template <typename T, typename U, typename V> inline constexpr void VectorSubtract(const T& a, const U& b, V&& c)
{
    c[0] = a[0] - b[0];
    c[1] = a[1] - b[1];
    c[2] = a[2] - b[2];
}

template <typename T, typename U, typename V> inline constexpr void VectorAdd(const T& a, const U& b, V&& c)
{
    c[0] = a[0] + b[0];
    c[1] = a[1] + b[1];
    c[2] = a[2] + b[2];
}

template <typename T, typename U> inline constexpr void VectorCopy(const T& a, U&& b)
{
    b[0] = a[0];
    b[1] = a[1];
    b[2] = a[2];
}

namespace Math {

inline constexpr Vector3 vec3_origin = { 0.0f, 0.0f, 0.0f };

template <typename T, typename U, typename V> inline void VectorMA(const T& veca, float scale, const U& vecb, V&& vecc)
{
    vecc[0] = veca[0] + scale * vecb[0];
    vecc[1] = veca[1] + scale * vecb[1];
    vecc[2] = veca[2] + scale * vecb[2];
}

template <typename T> inline vec_t Length(const T& v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

template <typename T, typename U, typename V> inline void CrossProduct(const T& v1, const U& v2, V&& cross)
{
    cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
    cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
    cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

template <typename T> inline float VectorNormalize(T&& v)
{
    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length != 0.0f) {
        float ilength = 1.0f / length;
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    }
    return length;
}

template <typename T> inline void VectorInverse(T&& v)
{
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

template <typename T, typename U> inline void VectorScale(const T& in, vec_t scale, U&& out)
{
    out[0] = in[0] * scale;
    out[1] = in[1] * scale;
    out[2] = in[2] * scale;
}

inline void R_ConcatRotations(float in1[3][3], float in2[3][3], float out[3][3])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = in1[i][0] * in2[0][j] + in1[i][1] * in2[1][j] + in1[i][2] * in2[2][j];
        }
    }
}

inline void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = in1[i][0] * in2[0][j] + in1[i][1] * in2[1][j] + in1[i][2] * in2[2][j];
        }
        out[i][3] = in1[i][0] * in2[0][3] + in1[i][1] * in2[1][3] + in1[i][2] * in2[2][3] + in1[i][3];
    }
}

inline std::pair<int, int> FloorDivMod(double numer, double denom)
{
    int q = 0, r = 0;
    if (numer >= 0.0) {
        double x = std::floor(numer / denom);
        q = static_cast<int>(x);
        r = static_cast<int>(std::floor(numer - (x * denom)));
    } else {
        double x = std::floor(-numer / denom);
        q = -static_cast<int>(x);
        r = static_cast<int>(std::floor(-numer - (x * denom)));
        if (r != 0) {
            q--;
            r = static_cast<int>(denom) - r;
        }
    }
    return { q, r };
}

inline int GreatestCommonDivisor(int i1, int i2)
{
    return std::gcd(i1, i2);
}

template <typename T, typename U, typename V, typename W>
inline void AngleVectors(const T& angles, U&& forward, V&& right, W&& up)
{
    float sy = std::sin(angles[1] * (std::numbers::pi_v<float> * 2 / 360));
    float cy = std::cos(angles[1] * (std::numbers::pi_v<float> * 2 / 360));
    float sp = std::sin(angles[0] * (std::numbers::pi_v<float> * 2 / 360));
    float cp = std::cos(angles[0] * (std::numbers::pi_v<float> * 2 / 360));
    float sr = std::sin(angles[2] * (std::numbers::pi_v<float> * 2 / 360));
    float cr = std::cos(angles[2] * (std::numbers::pi_v<float> * 2 / 360));

    forward[0] = cp * cy;
    forward[1] = cp * sy;
    forward[2] = -sp;
    right[0] = (-sr * sp * cy - cr * -sy);
    right[1] = (-sr * sp * sy - cr * cy);
    right[2] = -sr * cp;
    up[0] = (cr * sp * cy + sr * sy);
    up[1] = (cr * sp * sy - sr * cy);
    up[2] = cr * cp;
}

inline void BOPS_Error()
{
    Common::Sys_Error("BoxOnPlaneSide: Bad signbits");
}

template <typename T, typename U, typename P> inline int BoxOnPlaneSide(const T& emins, const U& emaxs, P* p)
{
    if (p->signbits >= 8) {
        BOPS_Error();
        return 0;
    }
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

inline float anglemod(float a)
{
    return static_cast<float>((360.0 / 65536.0) * (static_cast<int>(a * (65536.0 / 360.0)) & 65535));
}

} // namespace Math

template <typename T, typename U, typename P> inline int BoxOnPlaneSideFast(const T& emins, const U& emaxs, const P* p)
{
    if (p->type < 3) {
        if (p->dist <= emins[p->type]) return 1;
        if (p->dist >= emaxs[p->type]) return 2;
        return 3;
    }
    return Math::BoxOnPlaneSide(emins, emaxs, const_cast<P*>(p));
}

#define BOX_ON_PLANE_SIDE(emins, emaxs, p) BoxOnPlaneSideFast(emins, emaxs, p)
