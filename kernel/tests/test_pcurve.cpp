#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_ToroidalSurface.hxx>

#include "fk_body_check.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

// Curva 3D che sta esattamente sulla superficie: C(t) = S(q(t)), con q nel
// piano dei parametri. Serve a provare le SP-curve con un riferimento esatto (q).
class CurveOnSurface final : public Curve<3> {
public:
    CurveOnSurface(SurfacePtr surface, CurvePtr<2> q) : surface_(std::move(surface)), q_(std::move(q)) {}

    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return q_->domain(); }
    std::vector<double> breakpoints(const Interval &range) const override { return q_->breakpoints(range); }
    void evaluate(double t, int order, Vec3 *out) const override {
        if (order > 2) throw std::invalid_argument("CurveOnSurface: ordine > 2");
        Vec2 q[3];
        q_->evaluate(t, std::max(order, 2), q);
        Vec3 d[9];
        surface_->evaluate(q[0][0], q[0][1], 2, d);
        auto s = [&](int k, int l) { return d[Surface::derivativeIndex(k, l, 2)]; };
        out[0] = s(0, 0);
        if (order >= 1) out[1] = q[1][0] * s(1, 0) + q[1][1] * s(0, 1);
        if (order >= 2)
            out[2] = q[1][0] * q[1][0] * s(2, 0) + 2.0 * q[1][0] * q[1][1] * s(1, 1) + q[1][1] * q[1][1] * s(0, 2)
                   + q[2][0] * s(1, 0) + q[2][1] * s(0, 1);
    }

private:
    SurfacePtr surface_;
    CurvePtr<2> q_;
};

Handle(Geom_Curve) occtCurve(const Curve<3> &curve) {
    switch (curve.type()) {
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<3> &>(curve);
        return new Geom_Circle(gp_Ax2(toPnt(c.center()), toDir(cross(c.xAxis(), c.yAxis())), toDir(c.xAxis())), c.radius());
    }
    case CurveType::Ellipse: {
        const auto &e = static_cast<const Ellipse<3> &>(curve);
        return new Geom_Ellipse(gp_Ax2(toPnt(e.center()), toDir(cross(e.xAxis(), e.yAxis())), toDir(e.xAxis())), e.xRadius(),
                                e.yRadius());
    }
    case CurveType::BSpline:
        return toOcct(static_cast<const BSplineCurve<3> &>(curve));
    default:
        throw std::logic_error("occtCurve: tipo non gestito");
    }
}

// La stessa superficie in OCCT (parametrizzazioni identiche).
Handle(Geom_Surface) occtSurface(const Surface &surface) {
    switch (surface.type()) {
    case SurfaceType::Plane:
        return new Geom_Plane(toAx3(static_cast<const Plane &>(surface).frame()));
    case SurfaceType::Cylinder: {
        const auto &s = static_cast<const CylindricalSurface &>(surface);
        return new Geom_CylindricalSurface(toAx3(s.frame()), s.radius());
    }
    case SurfaceType::Cone: {
        const auto &s = static_cast<const ConicalSurface &>(surface);
        return new Geom_ConicalSurface(toAx3(s.frame()), s.semiAngle(), s.referenceRadius());
    }
    case SurfaceType::Sphere: {
        const auto &s = static_cast<const SphericalSurface &>(surface);
        return new Geom_SphericalSurface(toAx3(s.frame()), s.radius());
    }
    case SurfaceType::Torus: {
        const auto &s = static_cast<const ToroidalSurface &>(surface);
        return new Geom_ToroidalSurface(toAx3(s.frame()), s.majorRadius(), s.minorRadius());
    }
    case SurfaceType::Extrusion: {
        const auto &s = static_cast<const ExtrusionSurface &>(surface);
        return new Geom_SurfaceOfLinearExtrusion(occtCurve(*s.curve()), toDir(s.direction()));
    }
    default:
        throw std::logic_error("occtSurface: tipo non gestito");
    }
}

// Distanza tra parametri tenendo conto dei periodi.
double parameterDistance(const Surface &surface, Vec2 a, const Vec2 &b) {
    if (surface.isUPeriodic()) a[0] += surface.uPeriod() * std::round((b[0] - a[0]) / surface.uPeriod());
    if (surface.isVPeriodic()) a[1] += surface.vPeriod() * std::round((b[1] - a[1]) / surface.vPeriod());
    return distance(a, b);
}

bool valid(const Body &body) {
    const std::vector<CheckIssue> issues = checkBody(body);
    for (const CheckIssue &issue : issues) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    return issues.empty();
}

// Ogni fin ha l'SP-curve esatta e i suoi punti coincidono con i parametri
// che OCCT trova proiettando la curva dell'edge sulla stessa superficie.
void checkAgainstOcct(const Body &body) {
    for (FinId f : body.fins()) {
        const Fin &fin = body.fin(f);
        const Edge &edge = body.edge(fin.edge);
        const Surface &surface = *body.face(body.finFace(f)).surface;
        FK_CHECK(fin.pcurve && fin.pcurveTolerance == 0.0);
        if (!fin.pcurve) continue;
        const Handle(Geom_Surface) reference = occtSurface(surface);
        double worst = 0.0;
        for (int i = 0; i <= 10; ++i) {
            const double t = edge.range.lo + edge.range.length() * i / 10.0;
            GeomAPI_ProjectPointOnSurf projection(toPnt(edge.curve->point(t)), reference);
            if (projection.NbPoints() == 0) {
                reportFailure(__FILE__, __LINE__, "OCCT non proietta il punto");
                continue;
            }
            double u, v;
            projection.LowerDistanceParameters(u, v);
            const Vec2 ours = fin.pcurve->point(t);
            const double gap = parameterDistance(surface, ours, Vec2(u, v));
            // Sulla linea di chiusura di una superficie chiusa ma non periodica
            // (estrusione di una spline chiusa) u = inizio e u = fine sono lo
            // stesso punto: OCCT puo' scegliere l'altro.
            if (gap > 1e-7 && (i == 0 || i == 10) && distance(surface.point(ours[0], ours[1]), surface.point(u, v)) <= 1e-7) continue;
            worst = std::max(worst, gap);
        }
        FK_CHECK(worst <= 1e-7);
    }
}

}

// SP-curve esatte su tutte le fin delle estrusioni (piani, cilindri,
// superfici estruse), confrontate con le proiezioni di OCCT.
FK_TEST(PCurvesOfExtrusions) {
    std::mt19937 rng(300);
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 frame = randomFrame(rng, 50.0);
        const double height = uniform(rng, 0.5, 30.0) * (trial % 3 == 0 ? -1.0 : 1.0);
        std::vector<ProfileSegment> segments;
        switch (trial % 3) {
        case 0:
            segments = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
            segments.push_back(arcSegment(Vec2(-4, 0), 2.0, 0.0, kTwoPi));
            segments.push_back(closedSpline(Vec2(4, 0), 2.0, false));
            break;
        case 1:
            segments = {closedSpline(Vec2(), 8.0, true),
                        {std::make_shared<Ellipse<2>>(makeEllipse(Vec2(0.5, 0.3), 2.0, 1.0)), {0.0, kTwoPi}}};
            break;
        default: {
            auto spline = std::make_shared<BSplineCurve<2>>(
                3, std::vector<double>{0, 0, 0, 0, 0.5, 1, 1, 1, 1},
                std::vector<Vec2>{Vec2(-5, 0), Vec2(-4, 4), Vec2(0, 6), Vec2(4, 2), Vec2(5, 0)});
            segments = {{spline, spline->domain()}, lineSegment(Vec2(5, 0), Vec2(0, -3)), lineSegment(Vec2(0, -3), Vec2(-5, 0))};
            break;
        }
        }
        const Profile profile = buildProfile(segments, 1e-6);
        FK_CHECK(profile.regions.size() == 1);
        if (profile.regions.size() != 1) continue;
        const Body body = makeExtrusion(frame, profile.regions.front(), height);
        FK_CHECK(valid(body));
        checkAgainstOcct(body);
    }
}

// Tipi attesi: sul fianco del cilindro i cerchi e sul piano i cerchi restano cerchi.
FK_TEST(PCurveTypes) {
    const Body cylinder = makeCylinder(Frame3(Vec3(1, 2, 3), Vec3(1, 1, 0), Vec3(0, 0, 1)), 2.0, 5.0);
    for (FinId f : cylinder.fins()) {
        const Fin &fin = cylinder.fin(f);
        const SurfaceType surface = cylinder.face(cylinder.finFace(f)).surface->type();
        FK_CHECK(fin.pcurve && fin.pcurveTolerance == 0.0);
        if (!fin.pcurve) continue;
        FK_CHECK(fin.pcurve->type() == (surface == SurfaceType::Plane ? CurveType::Circle : CurveType::Line));
    }
    checkAgainstOcct(cylinder);
}

// Isoparametriche di sfera, toro e cono ed elica sul cilindro: esatte.
FK_TEST(ExactPCurvesOnAnalyticSurfaces) {
    std::mt19937 rng(301);
    for (int trial = 0; trial < 10; ++trial) {
        const Frame3 frame = randomFrame(rng, 50.0);
        const double r = uniform(rng, 1.0, 10.0);
        struct Case {
            SurfacePtr surface;
            CurvePtr<3> curve;
            Interval range;
        };
        std::vector<Case> cases;
        auto sphere = std::make_shared<SphericalSurface>(frame, r);
        const double latitude = uniform(rng, -1.2, 1.2), longitude = uniform(rng, 0.0, kTwoPi);
        cases.push_back({sphere, sphere->vIso(latitude), {0.3, 5.0}});           // parallelo
        cases.push_back({sphere, sphere->uIso(longitude), {-1.0, 1.2}});         // meridiano
        cases.push_back({sphere, reversedCurve<3>(sphere->vIso(latitude)), {-5.0, -0.3}});  // parallelo percorso al contrario
        auto torus = std::make_shared<ToroidalSurface>(frame, 3.0 * r, r);
        cases.push_back({torus, torus->uIso(longitude), {0.0, kTwoPi}});         // meridiano chiuso
        cases.push_back({torus, torus->vIso(uniform(rng, 0.0, kTwoPi)), {1.0, 7.0}});  // parallelo oltre il periodo
        auto cone = std::make_shared<ConicalSurface>(frame, uniform(rng, 0.2, 1.0), r);
        cases.push_back({cone, cone->uIso(longitude), {-2.0, 3.0}});             // generatrice
        cases.push_back({cone, cone->vIso(1.5), {0.0, kTwoPi}});                 // cerchio chiuso
        auto cylinder = std::make_shared<CylindricalSurface>(frame, r);
        const double pitch = uniform(rng, 0.1, 2.0);
        cases.push_back({cylinder, std::make_shared<CurveOnSurface>(cylinder, std::make_shared<Line<2>>(Vec2(0.5, 0), Vec2(1, pitch))),
                         {0.0, 20.0}});  // elica di tre giri
        for (const Case &c : cases) {
            const CurvePtr<2> pcurve = exactPCurve(*c.surface, c.curve, c.range);
            FK_CHECK(pcurve != nullptr);
            if (!pcurve) continue;
            FK_CHECK(pcurveDeviation(*c.surface, *c.curve, *pcurve, c.range) <= 1e-9 * r);
            // Il parametro u iniziale cade nel dominio di base.
            const double u0 = pcurve->point(c.range.lo)[0];
            FK_CHECK(u0 >= -1e-12 && u0 < kTwoPi + 1e-12);
        }
    }
}

// Curve generiche sulla superficie: nessuna forma esatta, B-spline
// approssimata entro la tolleranza e vicina alla curva q dei parametri.
FK_TEST(FittedPCurves) {
    std::mt19937 rng(302);
    for (int trial = 0; trial < 12; ++trial) {
        const Frame3 frame = randomFrame(rng, 50.0);
        SurfacePtr surface;
        CurvePtr<2> q;
        switch (trial % 4) {
        case 0:  // lamiera NURBS, curva B-spline casuale nel suo dominio
            surface = std::make_shared<BSplineSurface>(randomBSplineSurface(rng, true));
            break;
        case 1:
            surface = std::make_shared<SphericalSurface>(frame, uniform(rng, 2.0, 20.0));
            break;
        case 2:
            surface = std::make_shared<ToroidalSurface>(frame, 10.0, uniform(rng, 1.0, 5.0));
            break;
        default:
            surface = std::make_shared<CylindricalSurface>(frame, uniform(rng, 1.0, 10.0));
            break;
        }
        const Interval ud = surface->uDomain(), vd = surface->vDomain();
        auto inside = [&](double a, double b) {  // punto dentro il dominio, lontano dai poli
            const double v = vd.isFinite() ? vd.lo + (0.15 + 0.7 * b) * vd.length() : 10.0 * (b - 0.5);
            return Vec2(ud.lo + (0.1 + 0.8 * a) * ud.length(), v);
        };
        std::vector<Vec2> poles;
        for (int i = 0; i < 6; ++i) poles.push_back(inside(uniform(rng, 0, 1), uniform(rng, 0, 1)));
        q = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 0.3, 0.6, 1, 1, 1, 1}, poles);
        const auto curve = std::make_shared<CurveOnSurface>(surface, q);
        const Interval range{0.0, 1.0};
        FK_CHECK(exactPCurve(*surface, curve, range) == nullptr);
        double deviation = -1.0;
        const CurvePtr<2> fitted = fitPCurve(*surface, curve, range, 1e-7, &deviation);
        FK_CHECK(fitted != nullptr);
        if (!fitted) continue;
        FK_CHECK(deviation >= 0.0 && deviation <= 1e-7);
        FK_CHECK(pcurveDeviation(*surface, *curve, *fitted, range, 64) <= 1e-7);
        double worst = 0.0;
        for (int i = 0; i <= 50; ++i)
            worst = std::max(worst, parameterDistance(*surface, fitted->point(i / 50.0), q->point(i / 50.0)));
        FK_CHECK(worst <= 1e-5);
    }
}

// Gli operatori che cambiano gli edge mantengono le SP-curve coerenti.
FK_TEST(PCurvesSurviveEdgeOperations) {
    Body body = makeCylinder(Frame3(), 3.0, 4.0);
    for (EdgeId e : body.edges()) body.reverseEdge(e);
    FK_CHECK(valid(body));
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        const double middle = 0.5 * (edge.range.lo + edge.range.hi);
        body.semv(e, edge.curve->point(middle), middle);
    }
    FK_CHECK(valid(body));

    // Un'SP-curve sbagliata viene segnalata.
    Body wrong = makeBox(Frame3(), 1, 2, 3);
    Fin &fin = wrong.fin(wrong.fins().front());
    fin.pcurve = std::make_shared<Line<2>>(Vec2(0.5, 0.5), Vec2(1, 0));
    bool found = false;
    for (const CheckIssue &issue : checkBody(wrong)) found = found || issue.code == CheckCode::PCurveOffEdge;
    FK_CHECK(found);
}

// Proprieta' di massa con le SP-curve e con l'inversione: stesso risultato.
FK_TEST(MassWithAndWithoutPCurves) {
    const std::vector<ProfileSegment> segments = [] {
        std::vector<ProfileSegment> s = roundedRectangle(Vec2(-10, -6), 20.0, 12.0, 2.5);
        s.push_back(closedSpline(Vec2(4, 0), 2.0, true));
        return s;
    }();
    const Body body = makeExtrusion(Frame3(Vec3(3, -2, 7), Vec3(1, 2, 3), Vec3(1, 0, 0)), buildProfile(segments, 1e-6).regions.front(), 6.0);
    Body stripped = body;
    for (FinId f : stripped.fins()) stripped.fin(f).pcurve = nullptr;
    const MassProperties a = massProperties(body), b = massProperties(stripped);
    FK_CHECK_NEAR(a.volume, b.volume, 1e-11 * b.volume);
    FK_CHECK_NEAR(a.area, b.area, 1e-11 * b.area);
    FK_CHECK(distance(a.centroid, b.centroid) <= 1e-10);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) FK_CHECK_NEAR(a.inertia[i][j], b.inertia[i][j], 1e-11 * std::fabs(b.inertia[2][2]));
}
