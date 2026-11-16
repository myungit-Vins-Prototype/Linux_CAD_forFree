#ifndef FORGECAD_FK_SURFACE_H
#define FORGECAD_FK_SURFACE_H

#include <memory>
#include <vector>

#include "fk_curve.h"

// Superfici parametriche esatte S(u, v). Le parametrizzazioni coincidono con
// quelle di OCCT (Geom_Plane, Geom_CylindricalSurface, ...), cosi' i test
// possono confrontare punto per punto. Geometria immutabile, condivisa con
// shared_ptr<const ...>.
namespace ForgeCad::Kernel {

enum class SurfaceType { Plane, Cylinder, Cone, Sphere, Torus, Extrusion, Revolution, BSpline };

class Surface {
public:
    virtual ~Surface() = default;

    virtual SurfaceType type() const = 0;
    virtual Interval uDomain() const = 0;
    virtual Interval vDomain() const = 0;
    virtual bool isUPeriodic() const { return false; }
    virtual bool isVPeriodic() const { return false; }
    virtual double uPeriod() const { return 0.0; }
    virtual double vPeriod() const { return 0.0; }

    // Derivate parziali fino all'ordine `order`: out ha (order + 1)^2 elementi
    // e out[derivativeIndex(k, l, order)] = d^(k+l) S / du^k dv^l. Sono
    // garantite solo quelle con k + l <= order.
    virtual void evaluate(double u, double v, int order, Vec3 *out) const = 0;

    // Parametri che dividono la superficie in pezze lisce (nodi, quadranti).
    virtual std::vector<double> uBreakpoints(const Interval &range) const { return {range.lo, range.hi}; }
    virtual std::vector<double> vBreakpoints(const Interval &range) const { return {range.lo, range.hi}; }

    // Isoparametriche esatte con lo stesso parametro della superficie:
    // uIso(u) e' la curva v -> S(u, v), vIso(v) la curva u -> S(u, v).
    // nullptr se degenerano in un punto o non hanno una forma esatta nota.
    virtual CurvePtr<3> uIso(double) const { return nullptr; }
    virtual CurvePtr<3> vIso(double) const { return nullptr; }

    static constexpr int derivativeIndex(int k, int l, int order) { return k * (order + 1) + l; }

    Vec3 point(double u, double v) const;
    // Normale unitaria Su x Sv. Lancia std::domain_error nei punti singolari
    // (poli della sfera, vertice del cono).
    Vec3 normal(double u, double v) const;
};

using SurfacePtr = std::shared_ptr<const Surface>;

// S(u, v) = O + u X + v Y.
class Plane final : public Surface {
public:
    explicit Plane(const Frame3 &frame) : frame_(frame) {}

    SurfaceType type() const override { return SurfaceType::Plane; }
    Interval uDomain() const override;
    Interval vDomain() const override;
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const Frame3 &frame() const { return frame_; }

private:
    Frame3 frame_;
};

// S(u, v) = O + R (cos u X + sin u Y) + v Z.
class CylindricalSurface final : public Surface {
public:
    CylindricalSurface(const Frame3 &frame, double radius);

    SurfaceType type() const override { return SurfaceType::Cylinder; }
    Interval uDomain() const override { return {0.0, kTwoPi}; }
    Interval vDomain() const override;
    bool isUPeriodic() const override { return true; }
    double uPeriod() const override { return kTwoPi; }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const Frame3 &frame() const { return frame_; }
    double radius() const { return radius_; }

private:
    Frame3 frame_;
    double radius_;
};

// S(u, v) = O + (R + v sin a) (cos u X + sin u Y) + v cos a Z, con a = semiangolo
// (0 < |a| < pi/2) e R raggio nel piano di riferimento. v e' la distanza lungo
// la generatrice; il cono prosegue oltre il vertice (v = -R / sin a).
class ConicalSurface final : public Surface {
public:
    ConicalSurface(const Frame3 &frame, double semiAngle, double referenceRadius);

    SurfaceType type() const override { return SurfaceType::Cone; }
    Interval uDomain() const override { return {0.0, kTwoPi}; }
    Interval vDomain() const override;
    bool isUPeriodic() const override { return true; }
    double uPeriod() const override { return kTwoPi; }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const Frame3 &frame() const { return frame_; }
    double semiAngle() const { return semiAngle_; }
    double referenceRadius() const { return radius_; }
    Vec3 apex() const;

private:
    Frame3 frame_;
    double semiAngle_, radius_;
};

// S(u, v) = O + R cos v (cos u X + sin u Y) + R sin v Z, v in [-pi/2, pi/2].
class SphericalSurface final : public Surface {
public:
    SphericalSurface(const Frame3 &frame, double radius);

    SurfaceType type() const override { return SurfaceType::Sphere; }
    Interval uDomain() const override { return {0.0, kTwoPi}; }
    Interval vDomain() const override { return {-kHalfPi, kHalfPi}; }
    bool isUPeriodic() const override { return true; }
    double uPeriod() const override { return kTwoPi; }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    std::vector<double> vBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const Frame3 &frame() const { return frame_; }
    double radius() const { return radius_; }

private:
    Frame3 frame_;
    double radius_;
};

// S(u, v) = O + (R + r cos v) (cos u X + sin u Y) + r sin v Z.
class ToroidalSurface final : public Surface {
public:
    ToroidalSurface(const Frame3 &frame, double majorRadius, double minorRadius);

    SurfaceType type() const override { return SurfaceType::Torus; }
    Interval uDomain() const override { return {0.0, kTwoPi}; }
    Interval vDomain() const override { return {0.0, kTwoPi}; }
    bool isUPeriodic() const override { return true; }
    bool isVPeriodic() const override { return true; }
    double uPeriod() const override { return kTwoPi; }
    double vPeriod() const override { return kTwoPi; }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    std::vector<double> vBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const Frame3 &frame() const { return frame_; }
    double majorRadius() const { return majorRadius_; }
    double minorRadius() const { return minorRadius_; }

private:
    Frame3 frame_;
    double majorRadius_, minorRadius_;
};

// Estrusione lineare (Geom_SurfaceOfLinearExtrusion): S(u, v) = C(u) + v D,
// con D unitaria. Se la curva e' chiusa (estremi coincidenti, per esempio una
// B-spline chiusa non periodica) la superficie e' trattata come periodica in u
// con periodo = lunghezza del dominio: u fuori dal dominio viene riportato
// dentro. Cosi' il fianco di un profilo chiuso non ha una "cucitura" nello
// spazio dei parametri, come quello di un cilindro.
class ExtrusionSurface final : public Surface {
public:
    ExtrusionSurface(CurvePtr<3> curve, const Vec3 &direction);

    SurfaceType type() const override { return SurfaceType::Extrusion; }
    Interval uDomain() const override { return curve_->domain(); }
    Interval vDomain() const override;
    bool isUPeriodic() const override { return periodic_; }
    double uPeriod() const override { return period_; }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override;
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const CurvePtr<3> &curve() const { return curve_; }
    const Vec3 &direction() const { return direction_; }

private:
    double wrap(double u) const;  // nel dominio della curva chiusa non periodica

    CurvePtr<3> curve_;
    Vec3 direction_;
    bool periodic_ = false;
    bool wraps_ = false;  // curva chiusa ma non periodica: evaluate riporta u nel dominio
    double period_ = 0.0;
};

// Superficie di rivoluzione (Geom_SurfaceOfRevolution): S(u, v) = C(v)
// ruotata di u attorno all'asse (punto A, direzione unitaria a).
class RevolutionSurface final : public Surface {
public:
    RevolutionSurface(CurvePtr<3> meridian, const Vec3 &axisPoint, const Vec3 &axisDirection);

    SurfaceType type() const override { return SurfaceType::Revolution; }
    Interval uDomain() const override { return {0.0, kTwoPi}; }
    Interval vDomain() const override { return meridian_->domain(); }
    bool isUPeriodic() const override { return true; }
    double uPeriod() const override { return kTwoPi; }
    bool isVPeriodic() const override { return meridian_->isPeriodic(); }
    double vPeriod() const override { return meridian_->period(); }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override { return detail::quadrantBreakpoints(range); }
    std::vector<double> vBreakpoints(const Interval &range) const override { return meridian_->breakpoints(range); }
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    const CurvePtr<3> &meridian() const { return meridian_; }
    const Vec3 &axisPoint() const { return axisPoint_; }
    const Vec3 &axisDirection() const { return axisDirection_; }

private:
    CurvePtr<3> meridian_;
    Vec3 axisPoint_, axisDirection_;
};

}

#endif
