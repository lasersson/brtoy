#include <brtoy/linmath.h>
#include <brtoy/vec.h>
#include <math.h>

namespace brtoy {

float toRadians(float angle_degrees) {
    float angle_radians = angle_degrees / 360.0f * TwoPi;
    return angle_radians;
}

V3f &operator-=(V3f &u, const V3f &v) {
    vec3_sub(u.e, u.e, v.e);
    return u;
}

V3f operator-(V3f u, const V3f &v) {
    u -= v;
    return u;
}

V3f operator-(V3f u) {
    u.x = -u.x;
    u.y = -u.y;
    u.x = -u.z;
    return u;
}

V3f &operator+=(V3f &u, const V3f &v) {
    vec3_add(u.e, u.e, v.e);
    return u;
}

V3f operator+(V3f u, const V3f &v) {
    u += v;
    return u;
}

V3f &operator*=(V3f &u, float a) {
    vec3_scale(u.e, u.e, a);
    return u;
}

V3f operator*(V3f u, float a) {
    u *= a;
    return u;
}

V3f &operator/=(V3f &v, float a) {
    v *= (1.0f / a);
    return v;
}

V3f operator/(V3f v, float a) {
    v /= a;
    return v;
}

float length(const V3f &v) { return vec3_len(v.e); }

V3f normalize(V3f v) {
    vec3_norm(v.e, v.e);
    return v;
}

V3f cross(const V3f &u, const V3f &v) {
    V3f w;
    vec3_mul_cross(w.e, u.e, v.e);
    return w;
}

float dot(const V3f &u, const V3f &v) { return vec3_mul_inner(u.e, v.e); }

V4f::V4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

V4f::V4f(V3f &xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

void setIdentity(M44f &m) { mat4x4_identity((vec4 *)&m); }
void setTranslate(M44f &m, const V3f &t) { mat4x4_translate((vec4 *)&m, t.x, t.y, t.z); }
void translate(M44f &m, const V3f &t) { mat4x4_translate_in_place((vec4 *)&m, t.x, t.y, t.z); }
void rotateX(M44f &m, float a) { mat4x4_rotate_X((vec4 *)&m, (vec4 *)&m, a); }
void rotateY(M44f &m, float a) { mat4x4_rotate_Y((vec4 *)&m, (vec4 *)&m, a); }
void rotateZ(M44f &m, float a) { mat4x4_rotate_Z((vec4 *)&m, (vec4 *)&m, a); }
M44f transpose(const M44f &m) {
    M44f n;
    mat4x4_transpose((vec4 *)&n, (vec4 *)&m);
    return n;
}
M44f invert(const M44f &m) {
    M44f n;
    mat4x4_invert((vec4 *)&n, (vec4 *)&m);
    return n;
}
M44f orthonormalize(const M44f &m) {
    M44f n;
    mat4x4_orthonormalize((vec4 *)&n, (vec4 *)&m);
    return n;
}
M44f operator*(const M44f &m, const M44f &n) {
    M44f o;
    mat4x4_mul((vec4 *)&o, (vec4 *)&m, (vec4 *)&n);
    return o;
}

M44f lookAt(const V3f &eye, const V3f &center, const V3f &up) {
    M44f m;
    mat4x4_look_at((vec4 *)&m, eye.e, center.e, up.e);
    return m;
}

M44f perspectiveProjection(float fov_y_in_radians, float aspect_ratio, float near, float far) {
    M44f m;
    mat4x4_perspective((vec4 *)&m, fov_y_in_radians, aspect_ratio, near, far);
    return m;
}

} // namespace brtoy
