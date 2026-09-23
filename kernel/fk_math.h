#ifndef FORGECAD_FK_MATH_H
#define FORGECAD_FK_MATH_H

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

// Algebra di base del kernel, tutta in double.
namespace ForgeCad::Kernel {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kHalfPi = 0.5 * kPi;

// Punto o vettore a 2 o 3 componenti. Come in Eigen (e a differenza di OCCT,
// che separa gp_Pnt e gp_Vec) punti e vettori hanno lo stesso tipo: curve e
// algoritmi si scrivono una sola volta per il 2D e il 3D.
template <int N>
struct Vec {
    static_assert(N == 2 || N == 3, "Vec: solo 2 o 3 componenti");
    double c[N] = {};

    constexpr Vec() = default;
    template <int M = N, std::enable_if_t<M == 2, int> = 0>
    constexpr Vec(double x, double y) : c{x, y} {}
    template <int M = N, std::enable_if_t<M == 3, int> = 0>
    constexpr Vec(double x, double y, double z) : c{x, y, z} {}

    constexpr double operator[](int i) const { return c[i]; }
    constexpr double &operator[](int i) { return c[i]; }
    constexpr double x() const { return c[0]; }
    constexpr double y() const { return c[1]; }
    template <int M = N, std::enable_if_t<M == 3, int> = 0>
    constexpr double z() const { return c[2]; }

    constexpr Vec &operator+=(const Vec &o) { for (int i = 0; i < N; ++i) c[i] += o.c[i]; return *this; }
    constexpr Vec &operator-=(const Vec &o) { for (int i = 0; i < N; ++i) c[i] -= o.c[i]; return *this; }
    constexpr Vec &operator*=(double s) { for (int i = 0; i < N; ++i) c[i] *= s; return *this; }
    constexpr Vec &operator/=(double s) { for (int i = 0; i < N; ++i) c[i] /= s; return *this; }
};

using Vec2 = Vec<2>;
using Vec3 = Vec<3>;

template <int N> constexpr Vec<N> operator+(Vec<N> a, const Vec<N> &b) { return a += b; }
template <int N> constexpr Vec<N> operator-(Vec<N> a, const Vec<N> &b) { return a -= b; }
template <int N> constexpr Vec<N> operator-(Vec<N> a) { return a *= -1.0; }
template <int N> constexpr Vec<N> operator*(Vec<N> a, double s) { return a *= s; }
template <int N> constexpr Vec<N> operator*(double s, Vec<N> a) { return a *= s; }
template <int N> constexpr Vec<N> operator/(Vec<N> a, double s) { return a /= s; }

template <int N> constexpr double dot(const Vec<N> &a, const Vec<N> &b) {
    double sum = 0.0;
    for (int i = 0; i < N; ++i) sum += a.c[i] * b.c[i];
    return sum;
}
template <int N> constexpr double squaredNorm(const Vec<N> &a) { return dot(a, a); }
template <int N> double norm(const Vec<N> &a) { return std::sqrt(dot(a, a)); }
template <int N> double distance(const Vec<N> &a, const Vec<N> &b) { return norm(a - b); }

constexpr Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return Vec3(a.y() * b.z() - a.z() * b.y(), a.z() * b.x() - a.x() * b.z(), a.x() * b.y() - a.y() * b.x());
}
// Componente z del prodotto vettoriale nel piano.
constexpr double cross(const Vec2 &a, const Vec2 &b) { return a.x() * b.y() - a.y() * b.x(); }
// Rotazione di 90 gradi in senso antiorario.
constexpr Vec2 perpendicular(const Vec2 &a) { return Vec2(-a.y(), a.x()); }

template <int N> bool isFinite(const Vec<N> &a) {
    for (int i = 0; i < N; ++i) if (!std::isfinite(a.c[i])) return false;
    return true;
}

template <int N> Vec<N> normalized(const Vec<N> &a) {
    const double length = norm(a);
    if (!(length >= std::numeric_limits<double>::min()) || !std::isfinite(length))
        throw std::domain_error("ForgeCad::Kernel: vettore nullo o non finito");
    return a / length;
}

// Intervallo chiuso [lo, hi]; puo' essere infinito (dominio della retta).
struct Interval {
    double lo = 0.0;
    double hi = 0.0;

    double length() const { return hi - lo; }
    bool isFinite() const { return std::isfinite(lo) && std::isfinite(hi); }
    bool contains(double t, double tolerance = 0.0) const { return t >= lo - tolerance && t <= hi + tolerance; }
    double clamp(double t) const { return t < lo ? lo : (t > hi ? hi : t); }
};

// Sistema di riferimento cartesiano destrorso (equivalente di gp_Ax3 diretto).
class Frame3 {
public:
    Frame3();
    // xReference viene proiettato sul piano normale a zDirection, come in gp_Ax2.
    Frame3(const Vec3 &origin, const Vec3 &zDirection, const Vec3 &xReference);

    const Vec3 &origin() const { return origin_; }
    const Vec3 &xDir() const { return x_; }
    const Vec3 &yDir() const { return y_; }
    const Vec3 &zDir() const { return z_; }

    Vec3 toGlobal(const Vec3 &localPoint) const;
    Vec3 toLocal(const Vec3 &globalPoint) const;
    Vec3 directionToGlobal(const Vec3 &localVector) const;
    Vec3 directionToLocal(const Vec3 &globalVector) const;

private:
    Vec3 origin_;
    Vec3 x_, y_, z_;
};

// Trasformazione affine p -> M p + t.
class Transform3 {
public:
    Transform3();
    static Transform3 translation(const Vec3 &offset);
    static Transform3 rotation(const Vec3 &axisPoint, const Vec3 &axisDirection, double angle);
    static Transform3 scaling(const Vec3 &center, double factor);
    // Proiezione ortogonale sul piano per l'origine normale a `normal`
    // (singolare: non ha inversa).
    static Transform3 projectionAlong(const Vec3 &normal);
    // Da coordinate locali del sistema a coordinate globali.
    static Transform3 fromFrame(const Frame3 &frame);

    Vec3 applyToPoint(const Vec3 &point) const;
    Vec3 applyToVector(const Vec3 &vector) const;
    // (A * B)(p) = A(B(p)).
    Transform3 operator*(const Transform3 &other) const;
    Transform3 inverted() const;
    // Vero se M = s Q con Q ortogonale (rototraslazione, eventuale simmetria e
    // scala uniforme): le distanze vengono moltiplicate per `scale`.
    bool isSimilarity(double *scale = nullptr) const;

    double matrix(int row, int column) const { return m_[row][column]; }
    const Vec3 &translationPart() const { return t_; }

private:
    double m_[3][3];
    Vec3 t_;
};

}

#endif
