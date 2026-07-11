// mathlib.hpp -- 3D math primitives (vectors, matrices, fixed-point)
#pragma once
#include <cmath>

struct Vector3 {
    float x;
    float y;
    float z;

    constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    constexpr Vector3(const float* ptr) : x(ptr[0]), y(ptr[1]), z(ptr[2]) {}

    // Array subscript operators
    constexpr float operator[](size_t index) const {
        return (&x)[index];
    }
    constexpr float& operator[](size_t index) {
        return (&x)[index];
    }

    // Implicit conversions to raw pointers
    constexpr operator float*() { return &x; }
    constexpr operator const float*() const { return &x; }

    // Operator overloads
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

    // Methods
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

typedef float vec_t;
typedef vec_t vec5_t[5];

typedef int fixed4_t;
typedef int fixed8_t;
typedef int fixed16_t;

#ifndef M_PI
#define M_PI 3.14159265358979323846 // matches value in gcc v2 math.h
#endif

struct mplane_s;

#define IS_NAN(x) (((*(int*)&x) & nanmask) == nanmask)

// Modern C++ template functions replacing legacy macros
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

extern Vector3 vec3_origin;
extern int nanmask;

// Implement all vector math operations as templates in the header
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

void FloorDivMod(double numer, double denom, int* quotient, int* rem);
int GreatestCommonDivisor(int i1, int i2);

template <typename T, typename U, typename V, typename W>
inline void AngleVectors(const T& angles, U&& forward, V&& right, W&& up) {
    float angle;
    float sr, sp, sy, cr, cp, cy;

    angle = angles[1] * (3.14159265358979323846f * 2 / 360); // YAW
    sy = std::sin(angle);
    cy = std::cos(angle);
    angle = angles[0] * (3.14159265358979323846f * 2 / 360); // PITCH
    sp = std::sin(angle);
    cp = std::cos(angle);
    angle = angles[2] * (3.14159265358979323846f * 2 / 360); // ROLL
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
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
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
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
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


#define BOX_ON_PLANE_SIDE(emins, emaxs, p)                                    \
    (((p)->type < 3) ? (((p)->dist <= (emins)[(p)->type])                     \
                               ? 1                                            \
                               : (((p)->dist >= (emaxs)[(p)->type]) ? 2 : 3)) \
                      : BoxOnPlaneSide((emins), (emaxs), (p)))
