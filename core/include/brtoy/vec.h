#pragma once
#include <brtoy/brtoy.h>
#include <compare>

namespace brtoy {

inline constexpr float TwoPi = 6.28318530718f;
inline constexpr float HalfPi = 1.57079632679f;

float toRadians(float angle_in_degrees);

struct V2i {
    i32 x, y;
};

struct V2u {
    u32 x, y;

    std::strong_ordering operator<=>(const V2u &other) const = default;
};

struct V3u {
    u32 x, y, z;
};

struct V3f {
    union {
        struct {
            float x, y, z;
        };
        float e[3];
    };
};
V3f &operator-=(V3f &u, const V3f &v);
V3f operator-(V3f u, const V3f &v);
V3f operator-(V3f u);
V3f &operator+=(V3f &u, const V3f &v);
V3f operator+(V3f u, const V3f &v);
V3f &operator*=(V3f &u, float a);
V3f operator*(V3f u, float a);
V3f &operator/=(V3f &u, float a);
V3f operator/(V3f u, float a);
float length(const V3f &v);
V3f normalize(V3f v);
V3f cross(const V3f &u, const V3f &v);
float dot(const V3f &u, const V3f &v);

struct V4f {
    V4f() = default;
    V4f(float x, float y, float z, float w);
    V4f(V3f &xyz, float w);

    union {
        struct {
            float x, y, z, w;
        };
        float e[4];
    };
};

struct M44f {
    union {
        struct {
            V4f i, j, k, l;
        };
        V4f v[4];
    };
};

void setIdentity(M44f &m);
void setTranslate(M44f &m, const V3f &t);
void translate(M44f &m, const V3f &t);
void rotateX(M44f &m, float a);
void rotateY(M44f &m, float a);
void rotateZ(M44f &m, float a);

M44f transpose(const M44f &m);
M44f invert(const M44f &m);
M44f orthonormalize(const M44f &m);
M44f operator*(const M44f &m, const M44f &n);

M44f lookAt(const V3f &eye, const V3f &center, const V3f &up);
M44f perspectiveProjection(float fov_y_in_radians, float aspect_ratio, float near, float far);

} // namespace brtoy
