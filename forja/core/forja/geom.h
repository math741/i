// forja-core: geometria. Convenção idêntica à Unreal: centímetros, X à frente, Y à direita, Z para cima,
// yaw em graus (0 = +X, 90 = +Y). Posição de entidade = ponto dos pés (base da cápsula).
#pragma once
#include <cmath>

namespace forja {

constexpr double kPi = 3.14159265358979323846;

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    double len() const { return std::sqrt(x * x + y * y + z * z); }
    double len2d() const { return std::sqrt(x * x + y * y); }
};

inline double dist2d(const Vec3& a, const Vec3& b) { return (a - b).len2d(); }
inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }
inline Vec3 forward_of(double yaw_deg) { double r = deg2rad(yaw_deg); return {std::cos(r), std::sin(r), 0}; }
inline Vec3 right_of(double yaw_deg) { double r = deg2rad(yaw_deg); return {-std::sin(r), std::cos(r), 0}; }

// Vetor local (x frente, y direita, z cima) no referencial de um yaw -> mundo
inline Vec3 local_to_world(const Vec3& local, double yaw_deg) {
    return forward_of(yaw_deg) * local.x + right_of(yaw_deg) * local.y + Vec3(0, 0, local.z);
}

inline double wrap_deg(double a) {
    while (a > 180.0) a -= 360.0;
    while (a <= -180.0) a += 360.0;
    return a;
}

inline double yaw_of(const Vec3& v) { return rad2deg(std::atan2(v.y, v.x)); }

// Ângulo absoluto (0..180) entre o eixo de yaw e a direção (horizontal) de v
inline double angle_from_axis(double axis_yaw_deg, const Vec3& v) {
    if (v.len2d() < 1e-6) return 0.0;
    return std::fabs(wrap_deg(yaw_of(v) - axis_yaw_deg));
}

// Bezier quadrática: p0 -> p2 com ponto de controle p1
inline Vec3 bezier2(const Vec3& p0, const Vec3& p1, const Vec3& p2, double t) {
    double u = 1.0 - t;
    return p0 * (u * u) + p1 * (2 * u * t) + p2 * (t * t);
}

struct Box {  // caixa alinhada aos eixos
    Vec3 mn, mx;
    bool contains2d(const Vec3& p) const { return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y; }
};

// Primeiro t em [0,1] onde o segmento a->b entra na caixa (2D). Retorna -1 se não entra
// ou se já começa dentro.
inline double segment_enter_t(const Vec3& a, const Vec3& b, const Box& bx) {
    if (bx.contains2d(a)) return -1;
    double t0 = 0, t1 = 1;
    double d[2] = {b.x - a.x, b.y - a.y};
    double s[2] = {a.x, a.y};
    double lo[2] = {bx.mn.x, bx.mn.y};
    double hi[2] = {bx.mx.x, bx.mx.y};
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(d[i]) < 1e-9) {
            if (s[i] < lo[i] || s[i] > hi[i]) return -1;
        } else {
            double ta = (lo[i] - s[i]) / d[i];
            double tb = (hi[i] - s[i]) / d[i];
            if (ta > tb) { double tmp = ta; ta = tb; tb = tmp; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) return -1;
        }
    }
    return t0;
}

}  // namespace forja
