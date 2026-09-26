#include <GeomAPI_IntCS.hxx>
#include <GeomAPI_IntSS.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_TrimmedCurve.hxx>

#include <cstdio>
#include <cstdlib>
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_marching.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

Box cube(double half) { return Box{Vec3(-half, -half, -half), Vec3(half, half, half)}; }

// Ogni curva sta sulle due superfici e le SP-curve ne seguono il parametro.
void checkOnSurfaces(const SurfaceIntersection &result, const Surface &a, const Surface &b) {
    for (const IntersectionCurve &c : result.curves) {
        double worst = 0.0;
        for (int i = 0; i <= 400; ++i) {
            const Vec3 p = c.curve->point(c.range.lo + c.range.length() * i / 400.0);
            worst = std::max({worst, projectPoint(a, p).distance, projectPoint(b, p).distance});
        }
        FK_CHECK(worst <= 3e-9);
        if (c.pcurves[0]) {
            FK_CHECK(pcurveDeviation(a, *c.curve, *c.pcurves[0], c.range) <= 3e-9);
            FK_CHECK(pcurveDeviation(b, *c.curve, *c.pcurves[1], c.range) <= 3e-9);
            FK_CHECK(c.deviation <= 1e-9);
        }
    }
}

double distanceToCurves(const SurfaceIntersection &result, const Vec3 &p) {
    double best = 1e300;
    for (const IntersectionCurve &c : result.curves) best = std::min(best, projectPoint(*c.curve, p, c.range).distance);
    return best;
}

// Completezza indipendente dal tracciamento: le generatrici di A in molti
// punti incontrano B in punti che devono stare su qualche curva.
void checkComplete(const SurfaceIntersection &result, const GeneralizedCylinder &a, const Surface &b, const Box &bounds) {
    for (int i = 0; i <= 1500; ++i) {
        const double u = a.domain.lo + a.domain.length() * (i + 0.37) / 1501.0;
        const Vec3 origin = a.profile->point(u);
        bool grazing;
        for (double t : intersectLineSurface(origin, a.direction, b, grazing, 1e-9)) {
            const Vec3 p = origin + t * a.direction;
            bool in = true;
            for (int k = 0; k < 3; ++k) in = in && p[k] > bounds.lo[k] + 1e-3 && p[k] < bounds.hi[k] - 1e-3;
            if (in) FK_CHECK(distanceToCurves(result, p) <= 1e-6);
        }
    }
}

// Lunghezza esatta della curva x^2 + y^2 = r^2, y^2 + z^2 = R^2 (un ramo),
// z = sqrt(R^2 - r^2 sin^2), per Gauss-Legendre composta.
double bicylinderLoopLength(double r, double R) {
    const double nodes[5] = {-0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640};
    const double weights[5] = {0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891};
    const int panels = 2000;
    double sum = 0.0;
    for (int p = 0; p < panels; ++p) {
        const double a = kTwoPi * p / panels, h = kTwoPi / panels;
        for (int j = 0; j < 5; ++j) {
            const double t = a + 0.5 * h * (nodes[j] + 1.0), s = std::sin(t), c = std::cos(t);
            const double dz = -r * r * s * c / std::sqrt(R * R - r * r * s * s);
            sum += 0.5 * h * weights[j] * std::sqrt(r * r + dz * dz);
        }
    }
    return sum;
}

Handle(Geom_CylindricalSurface) occtCylinder(const CylindricalSurface &c) {
    return new Geom_CylindricalSurface(toAx3(c.frame()), c.radius());
}

}

// Cilindro piccolo che attraversa uno grande (assi perpendicolari
// incidenti): due curve chiuse trovate senza semi esterni, con la lunghezza
// esatta.
FK_TEST(MarchingPerpendicularCylinders) {
    std::mt19937 rng(900);
    for (int trial = 0; trial < 4; ++trial) {
        const Frame3 frame = randomFrame(rng, 20.0);
        const double r = uniform(rng, 1.0, 3.0), R = uniform(rng, 3.5, 6.0);
        const CylindricalSurface small(Frame3(frame.origin(), frame.zDir(), frame.xDir()), r);
        const CylindricalSurface big(Frame3(frame.origin() - 10.0 * frame.xDir(), frame.xDir(), frame.yDir()), R);
        Box bounds = Box{frame.origin(), frame.origin()}.padded(15.0);
        const SurfaceIntersection result = intersectSurfaces(small, big, bounds, {});
        FK_CHECK(result.curves.size() == 2);
        FK_CHECK(result.tangentPoints.empty());
        checkOnSurfaces(result, small, big);
        const double exact = bicylinderLoopLength(r, R);
        for (const IntersectionCurve &c : result.curves) {
            FK_CHECK(c.closed);
            FK_CHECK_NEAR(arcLength(*c.curve, c.range), exact, 1e-9 * exact);
        }
    }
}

// Cilindri in posizione generica contro GeomAPI_IntSS: ogni punto delle
// curve di OCCT sta su una delle nostre e viceversa.
FK_TEST(MarchingCylindersAgainstOcct) {
    std::mt19937 rng(901);
    int compared = 0;
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 fa = randomFrame(rng, 3.0);
        const Frame3 fb(fa.origin() + randomVec<3>(rng, 2.0), randomDirection(rng), randomDirection(rng));
        const CylindricalSurface a(fa, uniform(rng, 1.0, 4.0)), b(fb, uniform(rng, 1.0, 4.0));
        if (norm(cross(a.frame().zDir(), b.frame().zDir())) < 0.2) continue;
        const Box bounds = cube(60.0);
        const SurfaceIntersection ours = intersectSurfaces(a, b, bounds, {});
        checkOnSurfaces(ours, a, b);
        GeomAPI_IntSS reference(occtCylinder(a), occtCylinder(b), 1e-9);
        if (!reference.IsDone()) continue;
        ++compared;
        for (int i = 1; i <= reference.NbLines(); ++i) {
            const Handle(Geom_Curve) line = reference.Line(i);
            const double t0 = line->FirstParameter(), t1 = line->LastParameter();
            if (!std::isfinite(t0) || !std::isfinite(t1) || std::fabs(t1 - t0) > 1e6) continue;
            for (int j = 0; j <= 50; ++j) FK_CHECK(distanceToCurves(ours, fromOcct(line->Value(t0 + (t1 - t0) * j / 50.0))) <= 1e-5);
        }
        for (const IntersectionCurve &c : ours.curves)
            for (int j = 0; j <= 50; ++j) {
                const gp_Pnt p = toPnt(c.curve->point(c.range.lo + c.range.length() * j / 50.0));
                double best = 1e300;
                for (int i = 1; i <= reference.NbLines(); ++i) {
                    GeomAPI_ProjectPointOnCurve projection(p, reference.Line(i));
                    if (projection.NbPoints() > 0) best = std::min(best, projection.LowerDistance());
                }
                FK_CHECK(best <= 1e-5);
            }
    }
    FK_CHECK(compared >= 6);
}

// Fianco estruso da una spline chiusa (anche razionale) contro un cilindro
// obliquo e contro un altro fianco estruso: curve sulle superfici e
// completezza verificata sulle generatrici.
FK_TEST(MarchingExtrusionSurfaces) {
    std::mt19937 rng(902);
    for (int trial = 0; trial < 6; ++trial) {
        const ProfileSegment profile = closedSpline(Vec2(0, 0), 6.0, trial % 2 == 1);
        const Frame3 base = randomFrame(rng, 2.0);
        const ExtrusionSurface a(embedCurve(profile.curve, base), base.zDir());
        const Vec3 axis = normalized(base.xDir() + uniform(rng, -0.5, 0.5) * base.yDir() + uniform(rng, -0.8, 0.8) * base.zDir());
        const Box bounds = Box{base.origin(), base.origin()}.padded(12.0);
        GeneralizedCylinder ga;
        generalizedCylinder(a, ga);
        if (trial < 3) {
            const CylindricalSurface b(Frame3(base.origin() + uniform(rng, -1, 1) * base.yDir() - 20.0 * axis, axis, base.zDir()),
                                       uniform(rng, 1.5, 3.0));
            const SurfaceIntersection result = intersectSurfaces(a, b, bounds, {});
            FK_CHECK(!result.curves.empty());
            checkOnSurfaces(result, a, b);
            checkComplete(result, ga, b, bounds);
        } else {
            const ProfileSegment other = closedSpline(Vec2(0, 0), 3.0, trial % 2 == 0);
            const Frame3 frame(base.origin() - 15.0 * axis, axis, base.zDir());
            const ExtrusionSurface b(embedCurve(other.curve, frame), axis);
            const SurfaceIntersection result = intersectSurfaces(a, b, bounds, {});
            FK_CHECK(!result.curves.empty());
            checkOnSurfaces(result, a, b);
            checkComplete(result, ga, b, bounds);
        }
    }
}

// Direzioni parallele: rette esatte; superfici coincidenti segnalate.
FK_TEST(MarchingParallelDirections) {
    const CylindricalSurface a(Frame3(), 3.0), b(Frame3(Vec3(4, 0, 5), Vec3(0, 0, -1), Vec3(1, 0, 0)), 2.0);
    const SurfaceIntersection result = intersectSurfaces(a, b, cube(20.0), {});
    FK_CHECK(result.curves.size() == 2);
    for (const IntersectionCurve &c : result.curves) {
        FK_CHECK(c.curve->type() == CurveType::Line);
        FK_CHECK(projectPoint(a, c.curve->point(c.range.lo)).distance <= 1e-12);
        FK_CHECK(projectPoint(b, c.curve->point(c.range.hi)).distance <= 1e-12);
    }
    const CylindricalSurface same(Frame3(Vec3(0, 0, 7), Vec3(0, 0, 1), Vec3(0, 1, 0)), 3.0);
    FK_CHECK(intersectSurfaces(a, same, cube(20.0), {}).coincident);
    const CylindricalSurface touching(Frame3(Vec3(5, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.0);
    const SurfaceIntersection tangent = intersectSurfaces(a, touching, cube(20.0), {});
    FK_CHECK(tangent.curves.empty() && tangent.tangentCurves.size() == 1);
}

// Curva con cilindro e superficie estrusa contro GeomAPI_IntCS.
FK_TEST(MarchingCurveSurface) {
    std::mt19937 rng(903);
    for (int trial = 0; trial < 20; ++trial) {
        const CylindricalSurface cylinder(randomFrame(rng, 2.0), uniform(rng, 2.0, 5.0));
        CurvePtr<3> curve;
        Interval range;
        Handle(Geom_Curve) occtCurve;
        if (trial % 2 == 0) {
            const Frame3 f = randomFrame(rng, 3.0);
            auto circle = std::make_shared<Circle<3>>(makeCircle(f, uniform(rng, 2.0, 6.0)));
            curve = circle;
            range = {0.0, kTwoPi};
            occtCurve = new Geom_Circle(gp_Ax2(toPnt(f.origin()), toDir(f.zDir()), toDir(f.xDir())), circle->radius());
        } else {
            auto spline = std::make_shared<BSplineCurve<3>>(randomBSpline<3>(rng, trial % 4 == 1));
            curve = spline;
            range = spline->domain();
            occtCurve = toOcct(*spline);
        }
        const CurveSurfaceIntersection ours = intersectCurveSurface(*curve, range, cylinder, 1e-9);
        GeomAPI_IntCS reference(occtCurve, occtCylinder(cylinder));
        FK_CHECK(reference.IsDone());
        std::vector<Vec3> expected;
        for (int i = 1; i <= reference.NbPoints(); ++i) {
            const Vec3 p = fromOcct(reference.Point(i));
            bool duplicate = false;
            for (const Vec3 &q : expected) duplicate = duplicate || distance(p, q) <= 1e-7;
            if (!duplicate) expected.push_back(p);
        }
        FK_CHECK(ours.parameters.size() == expected.size());
        for (double t : ours.parameters) {
            const Vec3 p = curve->point(t);
            FK_CHECK(projectPoint(cylinder, p).distance <= 1e-9);
            double best = 1e300;
            for (const Vec3 &q : expected) best = std::min(best, distance(p, q));
            FK_CHECK(best <= 1e-7);
        }
    }
}

// Cilindri di raggio uguale con assi incidenti: due ellissi che si incrociano
// in due punti di tangenza. I punti sono vertici delle curve (che finiscono
// li') e la lunghezza totale e' quella delle due ellissi (semiassi r sqrt 2 e r).
FK_TEST(MarchingSingularPoints) {
    const double r = 3.0;
    const CylindricalSurface a(Frame3(), r), b(Frame3(Vec3(-10, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), r);
    const SurfaceIntersection result = intersectSurfaces(a, b, cube(20.0), {});
    FK_CHECK(result.singularPoints.size() == 2);
    FK_CHECK(result.tangentPoints.empty());
    checkOnSurfaces(result, a, b);
    double total = 0.0;
    for (const IntersectionCurve &c : result.curves) {
        total += arcLength(*c.curve, c.range);
        // Ogni curva va da un punto di tangenza all'altro.
        for (double t : {c.range.lo, c.range.hi}) {
            double best = 1e300;
            for (const Vec3 &p : result.singularPoints) best = std::min(best, distance(p, c.curve->point(t)));
            FK_CHECK(best <= 1e-12);
        }
    }
    // Perimetro dell'ellisse per Gauss-Legendre composta.
    const double major = r * std::sqrt(2.0), minor = r;
    double perimeter = 0.0;
    const int panels = 2000;
    for (int i = 0; i < panels; ++i)
        for (double node : {-0.5773502691896258, 0.5773502691896258}) {
            const double t = kTwoPi * (i + 0.5 + 0.5 * node) / panels;
            perimeter += 0.5 * kTwoPi / panels * std::hypot(major * std::sin(t), minor * std::cos(t));
        }
    FK_CHECK_NEAR(total, 2.0 * perimeter, 1e-8 * perimeter);

    // Contatto isolato (un punto, nessuna curva) e ansa a otto (un cilindro
    // che tocca l'interno dell'altro): rami che partono e tornano nel punto.
    const CylindricalSurface touching(Frame3(Vec3(-10, 5, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 2.0);
    const SurfaceIntersection isolated = intersectSurfaces(a, touching, cube(20.0), {});
    FK_CHECK(isolated.curves.empty() && isolated.isolatedPoints.size() == 1);
    const CylindricalSurface inner(Frame3(Vec3(-10, -1.5, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 1.5);
    const SurfaceIntersection eight = intersectSurfaces(a, inner, cube(20.0), {});
    FK_CHECK(eight.singularPoints.size() == 1 && eight.tangentPoints.empty() && !eight.curves.empty());
    // Le due anse arrivano fino a x = +-3 (y = 0, z = 0).
    FK_CHECK(distanceToCurves(eight, Vec3(3, 0, 0)) <= 1e-6 && distanceToCurves(eight, Vec3(-3, 0, 0)) <= 1e-6);
    checkOnSurfaces(eight, a, inner);
}

namespace {

gp_Ax3 occtAxes(const Frame3 &f) { return gp_Ax3(toPnt(f.origin()), toDir(f.zDir()), toDir(f.xDir())); }

Handle(Geom_Surface) occtSurface(const Surface &surface) {
    switch (surface.type()) {
    case SurfaceType::Plane: return new Geom_Plane(occtAxes(static_cast<const Plane &>(surface).frame()));
    case SurfaceType::Cylinder: {
        const auto &c = static_cast<const CylindricalSurface &>(surface);
        return new Geom_CylindricalSurface(occtAxes(c.frame()), c.radius());
    }
    case SurfaceType::Cone: {
        const auto &c = static_cast<const ConicalSurface &>(surface);
        return new Geom_ConicalSurface(occtAxes(c.frame()), c.semiAngle(), c.referenceRadius());
    }
    case SurfaceType::Sphere: {
        const auto &c = static_cast<const SphericalSurface &>(surface);
        return new Geom_SphericalSurface(occtAxes(c.frame()), c.radius());
    }
    case SurfaceType::Torus: {
        const auto &c = static_cast<const ToroidalSurface &>(surface);
        return new Geom_ToroidalSurface(occtAxes(c.frame()), c.majorRadius(), c.minorRadius());
    }
    case SurfaceType::BSpline: return toOcct(static_cast<const BSplineSurface &>(surface));
    default: break;
    }
    throw std::invalid_argument("occtSurface");
}

// Curve nostre e di GeomAPI_IntSS a confronto (nel box): ogni punto delle une vicino alle altre.
void compareWithOcct(const Surface &a, const Surface &b, const Box &bounds, int &compared) {
    SurfaceIntersection ours;
    try {
        ours = intersectSurfaces(a, b, bounds, {});
    } catch (const std::exception &error) {
        reportFailure(__FILE__, __LINE__, error.what());
        return;
    }
    checkOnSurfaces(ours, a, b);
    GeomAPI_IntSS reference(occtSurface(a), occtSurface(b), 1e-9);
    if (!reference.IsDone()) return;
    ++compared;
    const Box inner = bounds.padded(-1e-3);
    auto in = [&](const Vec3 &p) {
        for (int k = 0; k < 3; ++k)
            if (p[k] < inner.lo[k] || p[k] > inner.hi[k]) return false;
        return true;
    };
    for (int i = 1; i <= reference.NbLines(); ++i) {
        const Handle(Geom_Curve) line = reference.Line(i);
        const double t0 = line->FirstParameter(), t1 = line->LastParameter();
        if (!std::isfinite(t0) || !std::isfinite(t1) || std::fabs(t1 - t0) > 1e6) continue;
        for (int j = 0; j <= 80; ++j) {
            const Vec3 p = fromOcct(line->Value(t0 + (t1 - t0) * j / 80.0));
            if (in(p)) {
                // Le curve di GeomAPI_IntSS tra superfici B-spline sono
                // approssimate (anche 5e-5 fuori dalle superfici): la loro
                // distanza dalle superfici fa parte della tolleranza.
                const double offSurfaces = std::max(projectPoint(a, p).distance, projectPoint(b, p).distance);
                const double gap = distanceToCurves(ours, p);
                FK_CHECK(gap <= 1e-5 + 2.0 * offSurfaces);
            }
        }
    }
    for (const IntersectionCurve &c : ours.curves)
        for (int j = 0; j <= 50; ++j) {
            const Vec3 x = c.curve->point(c.range.lo + c.range.length() * j / 50.0);
            if (!in(x)) continue;
            double best = 1e300;
            for (int i = 1; i <= reference.NbLines(); ++i) {
                GeomAPI_ProjectPointOnCurve projection(toPnt(x), reference.Line(i));
                if (projection.NbPoints() > 0) best = std::min(best, projection.LowerDistance());
            }
            FK_CHECK(best <= 1e-4);
        }
}

}

// Coppie qualsiasi di superfici (semi dalla suddivisione delle forme NURBS):
// sfere, coni, tori, piani, cilindri e B-spline, contro GeomAPI_IntSS.
FK_TEST(MarchingGeneralSurfacesAgainstOcct) {
    std::mt19937 rng(911);
    int compared = 0;
    const Box bounds = cube(20.0);
    for (int trial = 0; trial < 4; ++trial) {
        const Frame3 f1(randomVec<3>(rng, 1.0), randomDirection(rng), randomDirection(rng));
        const Frame3 f2(f1.origin() + randomVec<3>(rng, 2.0), randomDirection(rng), randomDirection(rng));
        const SphericalSurface sphere(f1, uniform(rng, 3.0, 5.0));
        const CylindricalSurface cylinder(f2, uniform(rng, 1.0, 2.5));
        const SphericalSurface other(f2, uniform(rng, 2.0, 4.0));
        const ToroidalSurface torus(f1, 6.0, uniform(rng, 1.0, 2.0));
        const Plane plane(Frame3(f1.origin() + randomVec<3>(rng, 1.0), randomDirection(rng), randomDirection(rng)));
        const ConicalSurface cone(f2, uniform(rng, 0.3, 0.8), uniform(rng, 1.0, 2.0));
        compareWithOcct(sphere, cylinder, bounds, compared);
        compareWithOcct(sphere, other, bounds, compared);
        compareWithOcct(torus, plane, bounds, compared);
        compareWithOcct(torus, cylinder, bounds, compared);
        compareWithOcct(sphere, cone, bounds, compared);
        compareWithOcct(torus, other, bounds, compared);
        const BSplineSurface patch = randomBSplineSurface(rng, trial % 2 == 1, 3.0);
        compareWithOcct(patch, plane, bounds, compared);
        compareWithOcct(patch, sphere, bounds, compared);
    }
    FK_CHECK(compared >= 24);
}

// Contatto di ordine superiore lontano dai poli: il piano z = 0 tangente alla
// "sella di scimmia" z = u^3 - 3 u v^2 (pezza di Bezier esatta, poli dai
// blossom) nell'origine. Le superfici coincidono al secondo ordine e vi si
// incrociano tre rette (u = 0, u = +-sqrt(3) v), lisce nel punto: il
// tracciamento le attraversa (le booleane dividono le curve nei punti comuni).
FK_TEST(MarchingHigherOrderContactBranches) {
    std::vector<Vec3> poles;
    for (int i = 0; i <= 3; ++i)
        for (int j = 0; j <= 2; ++j) {
            const double um = (2.0 * i - 3.0) / 3.0, u3 = (3 - i) % 2 ? -1.0 : 1.0, vm = j - 1.0, v2 = (2 - j) % 2 ? -1.0 : 1.0;
            poles.push_back(Vec3(um, vm, u3 - 3.0 * um * v2));
        }
    const BSplineSurface saddle(3, 2, {-1, -1, -1, -1, 1, 1, 1, 1}, {-1, -1, -1, 1, 1, 1}, 4, 3, poles);
    FK_CHECK_NEAR(saddle.point(0.3, -0.7).z(), 0.027 - 3.0 * 0.3 * 0.49, 1e-14);
    const Plane plane{Frame3()};
    const SurfaceIntersection result = intersectSurfaces(plane, saddle, cube(1.5), {});
    FK_CHECK(result.tangentPoints.empty());
    FK_CHECK(result.curves.size() == 3);
    for (const IntersectionCurve &curve : result.curves) {
        FK_CHECK(projectPoint(*curve.curve, Vec3(0, 0, 0), curve.range).distance <= 1e-9);
        for (int k = 0; k <= 16; ++k) {
            const Vec3 p = curve.curve->point(curve.range.lo + curve.range.length() * k / 16.0);
            const double lines = std::min({std::fabs(p.x()), std::fabs(p.x() - std::sqrt(3.0) * p.y()), std::fabs(p.x() + std::sqrt(3.0) * p.y())});
            FK_CHECK(lines <= 1e-8);
            FK_CHECK(std::fabs(p.z()) <= 1e-8);
        }
    }
    checkOnSurfaces(result, plane, saddle);
}
