#ifndef FORGECAD_FK_CURVE_H
#define FORGECAD_FK_CURVE_H

#include <memory>
#include <vector>

#include "fk_math.h"

// Curve parametriche esatte, nel piano (N = 2, le future SP-curve e lo
// schizzo) e nello spazio (N = 3). La geometria e' immutabile e condivisa
// con shared_ptr<const ...>, come gli Handle di OCCT.
namespace ForgeCad::Kernel {

// Other: curve definite altrove (per esempio le isoparametriche generiche delle superfici).
enum class CurveType { Line, Circle, Ellipse, BSpline, Trimmed, Transformed, Other };

template <int N>
class Curve {
public:
    virtual ~Curve() = default;

    virtual CurveType type() const = 0;
    // Dominio naturale del parametro (infinito per la retta).
    virtual Interval domain() const = 0;
    virtual bool isPeriodic() const { return false; }
    virtual double period() const { return 0.0; }
    // out[k] = derivata k-esima in t, per k = 0..order.
    virtual void evaluate(double t, int order, Vec<N> *out) const = 0;
    // Come evaluate, ma in un punto di discontinuita' delle derivate (nodo di
    // una B-spline) usa il tratto a sinistra di t invece di quello a destra.
    virtual void evaluateLeft(double t, int order, Vec<N> *out) const { evaluate(t, order, out); }
    // Parametri ordinati dentro range (estremi compresi) che dividono la
    // curva in tratti lisci: i nodi delle B-spline, i quadranti delle coniche.
    // Servono a campionare e integrare senza scavalcare discontinuita'.
    virtual std::vector<double> breakpoints(const Interval &range) const { return {range.lo, range.hi}; }

    Vec<N> point(double t) const {
        Vec<N> result;
        evaluate(t, 0, &result);
        return result;
    }
    Vec<N> derivative(double t, int order = 1) const {
        std::vector<Vec<N>> result(order + 1);
        evaluate(t, order, result.data());
        return result[order];
    }
};

template <int N>
using CurvePtr = std::shared_ptr<const Curve<N>>;

// Retta P(t) = origin + t * direction, con direction unitaria (t = ascissa).
template <int N>
class Line final : public Curve<N> {
public:
    Line(const Vec<N> &origin, const Vec<N> &direction);

    CurveType type() const override { return CurveType::Line; }
    Interval domain() const override;
    void evaluate(double t, int order, Vec<N> *out) const override;

    const Vec<N> &origin() const { return origin_; }
    const Vec<N> &direction() const { return direction_; }

private:
    Vec<N> origin_;
    Vec<N> direction_;
};

// Cerchio P(t) = center + r (cos t X + sin t Y), t in [0, 2 pi), come Geom_Circle.
template <int N>
class Circle final : public Curve<N> {
public:
    // xAxis e yAxis vengono ortonormalizzati (Gram-Schmidt).
    Circle(const Vec<N> &center, const Vec<N> &xAxis, const Vec<N> &yAxis, double radius);

    CurveType type() const override { return CurveType::Circle; }
    Interval domain() const override { return {0.0, kTwoPi}; }
    bool isPeriodic() const override { return true; }
    double period() const override { return kTwoPi; }
    void evaluate(double t, int order, Vec<N> *out) const override;
    std::vector<double> breakpoints(const Interval &range) const override;

    const Vec<N> &center() const { return center_; }
    const Vec<N> &xAxis() const { return x_; }
    const Vec<N> &yAxis() const { return y_; }
    double radius() const { return radius_; }

private:
    Vec<N> center_, x_, y_;
    double radius_;
};

// Ellisse P(t) = center + a cos t X + b sin t Y, come Geom_Ellipse
// (a = semiasse lungo X, b lungo Y; non si impone a >= b).
template <int N>
class Ellipse final : public Curve<N> {
public:
    Ellipse(const Vec<N> &center, const Vec<N> &xAxis, const Vec<N> &yAxis, double xRadius, double yRadius);

    CurveType type() const override { return CurveType::Ellipse; }
    Interval domain() const override { return {0.0, kTwoPi}; }
    bool isPeriodic() const override { return true; }
    double period() const override { return kTwoPi; }
    void evaluate(double t, int order, Vec<N> *out) const override;
    std::vector<double> breakpoints(const Interval &range) const override;

    const Vec<N> &center() const { return center_; }
    const Vec<N> &xAxis() const { return x_; }
    const Vec<N> &yAxis() const { return y_; }
    double xRadius() const { return xRadius_; }
    double yRadius() const { return yRadius_; }

private:
    Vec<N> center_, x_, y_;
    double xRadius_, yRadius_;
};

// Restrizione di una curva a [first, last]. Su una base periodica last viene
// riportato in (first, first + periodo]: un arco da 270 a 90 gradi ha dominio
// [3/2 pi, 5/2 pi].
template <int N>
class TrimmedCurve final : public Curve<N> {
public:
    TrimmedCurve(CurvePtr<N> basis, double first, double last);

    CurveType type() const override { return CurveType::Trimmed; }
    Interval domain() const override { return {first_, last_}; }
    void evaluate(double t, int order, Vec<N> *out) const override { basis_->evaluate(t, order, out); }
    void evaluateLeft(double t, int order, Vec<N> *out) const override { basis_->evaluateLeft(t, order, out); }
    std::vector<double> breakpoints(const Interval &range) const override;

    const CurvePtr<N> &basis() const { return basis_; }

private:
    CurvePtr<N> basis_;
    double first_, last_;
};

// Curva 3D trasformata: P(t) = T(C(t)), stesso parametro della curva base.
// Serve per le isoparametriche delle superfici estruse e di rivoluzione.
class TransformedCurve final : public Curve<3> {
public:
    TransformedCurve(CurvePtr<3> basis, const Transform3 &transform);

    CurveType type() const override { return CurveType::Transformed; }
    Interval domain() const override { return basis_->domain(); }
    bool isPeriodic() const override { return basis_->isPeriodic(); }
    double period() const override { return basis_->period(); }
    void evaluate(double t, int order, Vec3 *out) const override;
    void evaluateLeft(double t, int order, Vec3 *out) const override;
    std::vector<double> breakpoints(const Interval &range) const override { return basis_->breakpoints(range); }

    const CurvePtr<3> &basis() const { return basis_; }
    const Transform3 &transform() const { return transform_; }

private:
    CurvePtr<3> basis_;
    Transform3 transform_;
};

namespace detail {
// Estremi di range piu' i multipli di pi/2 al suo interno (tratti delle coniche).
std::vector<double> quadrantBreakpoints(const Interval &range);
}

inline Circle<2> makeCircle(const Vec2 &center, double radius) {
    return Circle<2>(center, Vec2(1.0, 0.0), Vec2(0.0, 1.0), radius);
}
inline Circle<3> makeCircle(const Frame3 &frame, double radius) {
    return Circle<3>(frame.origin(), frame.xDir(), frame.yDir(), radius);
}
inline Ellipse<2> makeEllipse(const Vec2 &center, double xRadius, double yRadius) {
    return Ellipse<2>(center, Vec2(1.0, 0.0), Vec2(0.0, 1.0), xRadius, yRadius);
}
inline Ellipse<3> makeEllipse(const Frame3 &frame, double xRadius, double yRadius) {
    return Ellipse<3>(frame.origin(), frame.xDir(), frame.yDir(), xRadius, yRadius);
}

extern template class Line<2>;
extern template class Line<3>;
extern template class Circle<2>;
extern template class Circle<3>;
extern template class Ellipse<2>;
extern template class Ellipse<3>;
extern template class TrimmedCurve<2>;
extern template class TrimmedCurve<3>;

}

#endif
