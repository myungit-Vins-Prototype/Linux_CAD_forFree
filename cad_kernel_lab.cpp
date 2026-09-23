#include "cad_kernel_lab.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>

#include <chrono>
#include <cmath>

#include "cad_kernel.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_curve_algo.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_bspline_surface.h"
#include "fk_primitives.h"
#include "fk_surface_algo.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

// Ogni proiezione si ripete fino a 20 volte o ~1.5 ms, per un tempo medio
// stabile senza rallentare il movimento del mouse.
constexpr int kTimingRepetitions = 20;
constexpr std::chrono::microseconds kTimingBudget(1500);

gp_Pnt toPnt(const Vec3 &v) { return gp_Pnt(v.x(), v.y(), v.z()); }
QVector3D toDisplay(const Vec3 &v) { return QVector3D(float(v.x()), float(v.y()), float(v.z())); }

void compressKnots(const std::vector<double> &knots, TColStd_Array1OfReal &distinct, TColStd_Array1OfInteger &multiplicities) {
    std::vector<double> values;
    std::vector<int> counts;
    for (double knot : knots) {
        if (!values.empty() && values.back() == knot) ++counts.back();
        else { values.push_back(knot); counts.push_back(1); }
    }
    distinct.Resize(1, int(values.size()), false);
    multiplicities.Resize(1, int(values.size()), false);
    for (std::size_t i = 0; i < values.size(); ++i) {
        distinct.SetValue(int(i) + 1, values[i]);
        multiplicities.SetValue(int(i) + 1, counts[i]);
    }
}

Handle(Geom_BSplineCurve) toOcct(const BSplineCurve<3> &curve) {
    TColgp_Array1OfPnt poles(1, curve.poleCount());
    TColStd_Array1OfReal weights(1, curve.poleCount());
    for (int i = 0; i < curve.poleCount(); ++i) {
        poles.SetValue(i + 1, toPnt(curve.poles()[i]));
        weights.SetValue(i + 1, curve.weight(i));
    }
    TColStd_Array1OfReal knots(1, 1);
    TColStd_Array1OfInteger multiplicities(1, 1);
    compressKnots(curve.knots(), knots, multiplicities);
    return new Geom_BSplineCurve(poles, weights, knots, multiplicities, curve.degree());
}

Handle(Geom_BSplineSurface) toOcct(const BSplineSurface &surface) {
    TColgp_Array2OfPnt poles(1, surface.uPoleCount(), 1, surface.vPoleCount());
    TColStd_Array2OfReal weights(1, surface.uPoleCount(), 1, surface.vPoleCount());
    for (int i = 0; i < surface.uPoleCount(); ++i)
        for (int j = 0; j < surface.vPoleCount(); ++j) {
            poles.SetValue(i + 1, j + 1, toPnt(surface.pole(i, j)));
            weights.SetValue(i + 1, j + 1, surface.weight(i, j));
        }
    TColStd_Array1OfReal uKnots(1, 1), vKnots(1, 1);
    TColStd_Array1OfInteger uMults(1, 1), vMults(1, 1);
    compressKnots(surface.uKnots(), uKnots, uMults);
    compressKnots(surface.vKnots(), vKnots, vMults);
    return new Geom_BSplineSurface(poles, weights, uKnots, vKnots, uMults, vMults, surface.uDegree(), surface.vDegree());
}

// Nodi clamped uniformi su [0, 1].
std::vector<double> uniformKnots(int degree, int poleCount) {
    std::vector<double> knots(degree + 1, 0.0);
    const int spans = poleCount - degree;
    for (int i = 1; i < spans; ++i) knots.push_back(double(i) / spans);
    knots.insert(knots.end(), degree + 1, 1.0);
    return knots;
}

// Esegue `work` e ne restituisce il tempo medio in microsecondi.
template <class Work>
double timeAverage(const Work &work) {
    const auto start = std::chrono::steady_clock::now();
    int repetitions = 0;
    do {
        work(repetitions);
        ++repetitions;
    } while (repetitions < kTimingRepetitions && std::chrono::steady_clock::now() - start < kTimingBudget);
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / repetitions;
}

Vec2 toVec(const QPointF &point) { return Vec2(point.x(), point.y()); }

ProfileSegment lineSegment(const Vec2 &a, const Vec2 &b) {
    return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}};
}

// Stessa geometria esatta di ForgeCad::curveGeometry (cad_curve_solver.cpp),
// ma con le curve del nuovo kernel.
std::vector<ProfileSegment> sketchSegments(const SketchObject &sketch) {
    constexpr double confusion = 1.0e-7;  // Precision::Confusion()
    std::vector<ProfileSegment> result;
    for (const SketchSegment &segment : sketch.segments) {
        const Vec2 a = toVec(segment.first), b = toVec(segment.second);
        if (distance(a, b) > confusion) result.push_back(lineSegment(a, b));
    }
    for (const CurveObject &curve : sketch.curves) {
        const int count = curve.controlPoints.size();
        switch (curve.tool) {
        case DrawingTool::Spline: {
            // Bezier cubiche C1 a tratti come B-spline di grado 3 (nodi interni tripli).
            if (count < 2 || curve.tangentHandles.size() != count) break;
            std::vector<Vec2> poles;
            for (int i = 0; i < count; ++i) {
                if (i > 0) poles.push_back(toVec(curve.tangentHandles.at(i).first));
                poles.push_back(toVec(curve.controlPoints.at(i)));
                if (i + 1 < count) poles.push_back(toVec(curve.tangentHandles.at(i).second));
            }
            std::vector<double> knots;
            std::vector<int> multiplicities;
            for (int i = 0; i < count; ++i) {
                knots.push_back(double(i));
                multiplicities.push_back(i == 0 || i == count - 1 ? 4 : 3);
            }
            auto spline = std::make_shared<BSplineCurve<2>>(3, expandKnots(knots, multiplicities), std::move(poles));
            result.push_back({spline, spline->domain()});
            break;
        }
        case DrawingTool::Nurbs: {
            if (count < 2) break;
            const int degree = std::min(3, count - 1);
            std::vector<Vec2> poles;
            std::vector<double> weights;
            bool validWeights = true;
            for (int i = 0; i < count; ++i) {
                poles.push_back(toVec(curve.controlPoints.at(i)));
                const double weight = curve.weights.size() == count ? curve.weights.at(i) : 1.0;
                validWeights &= weight > 0.0;
                weights.push_back(weight);
            }
            if (!validWeights) break;
            const int spans = count - degree;
            std::vector<double> knots;
            std::vector<int> multiplicities;
            for (int i = 0; i <= spans; ++i) {
                knots.push_back(double(i) / spans);
                multiplicities.push_back(i == 0 || i == spans ? degree + 1 : 1);
            }
            auto nurbs = std::make_shared<BSplineCurve<2>>(degree, expandKnots(knots, multiplicities), std::move(poles), std::move(weights));
            result.push_back({nurbs, nurbs->domain()});
            break;
        }
        case DrawingTool::Circle: {
            if (count < 2) break;
            const double radius = distance(toVec(curve.controlPoints.at(0)), toVec(curve.controlPoints.at(1)));
            if (radius <= confusion) break;
            result.push_back({std::make_shared<Circle<2>>(makeCircle(toVec(curve.controlPoints.at(0)), radius)), {0.0, kTwoPi}});
            break;
        }
        case DrawingTool::Arc: {
            if (count < 3) break;
            const Vec2 center = toVec(curve.controlPoints.at(0)), start = toVec(curve.controlPoints.at(1)),
                       end = toVec(curve.controlPoints.at(2));
            const double radius = distance(center, start);
            if (radius <= confusion || distance(center, end) <= confusion) break;
            const double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
            double endAngle = std::atan2(end.y() - center.y(), end.x() - center.x());
            while (endAngle <= startAngle + 1.0e-12) endAngle += kTwoPi;  // Precision::Angular()
            result.push_back({std::make_shared<Circle<2>>(makeCircle(center, radius)), {startAngle, endAngle}});
            break;
        }
        case DrawingTool::Polygon: {
            if (count < 2 || curve.sides < 3) break;
            const Vec2 center = toVec(curve.controlPoints.at(0)), vertex = toVec(curve.controlPoints.at(1));
            const double radius = distance(center, vertex);
            if (radius <= confusion) break;
            const double startAngle = std::atan2(vertex.y() - center.y(), vertex.x() - center.x());
            std::vector<Vec2> corners;
            for (int side = 0; side < curve.sides; ++side) {
                const double angle = startAngle + kTwoPi * side / curve.sides;
                corners.push_back(side == 0 ? vertex : center + Vec2(radius * std::cos(angle), radius * std::sin(angle)));
            }
            for (int side = 0; side < curve.sides; ++side)
                result.push_back(lineSegment(corners[side], corners[(side + 1) % curve.sides]));
            break;
        }
        default:
            break;
        }
    }
    return result;
}

// Lunghezza degli spigoli OCCT escluse le cuciture delle facce periodiche.
double occtEdgeLengthWithoutSeams(const TopoDS_Shape &shape) {
    TopTools_IndexedMapOfShape seams;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next())
        for (TopExp_Explorer edges(faces.Current(), TopAbs_EDGE); edges.More(); edges.Next())
            if (BRep_Tool::IsClosed(TopoDS::Edge(edges.Current()), TopoDS::Face(faces.Current()))) seams.Add(edges.Current());
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    double length = 0.0;
    for (int i = 1; i <= edges.Extent(); ++i) {
        if (seams.Contains(edges(i))) continue;
        BRepAdaptor_Curve curve(TopoDS::Edge(edges(i)));
        length += GCPnts_AbscissaPoint::Length(curve, 1e-12);
    }
    return length;
}

int countShapes(const TopoDS_Shape &shape, TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent();
}

}

struct KernelLab::Impl {
    KernelLabShape shape = KernelLabShape::None;
    std::shared_ptr<const Surface> surface;
    Handle(Geom_Surface) occt;
    Interval u, v;
    Vec3 center;
    QVector<QVector<QVector3D>> wireframe;
    QString summary;

    void build(KernelLabShape newShape);
    void buildWireframe();
};

void KernelLab::Impl::build(KernelLabShape newShape) {
    shape = newShape;
    surface.reset();
    occt.Nullify();
    wireframe.clear();
    summary.clear();
    center = Vec3();
    switch (shape) {
    case KernelLabShape::None:
    case KernelLabShape::SketchExtrusion:  // costruita da compareSketchExtrusion
    case KernelLabShape::DocumentBody:     // costruita da compareDocumentBody
        return;
    case KernelLabShape::Sphere: {
        auto sphere = std::make_shared<SphericalSurface>(Frame3(), 1.5);
        occt = new Geom_SphericalSurface(gp_Ax3(), 1.5);
        u = sphere->uDomain();
        v = sphere->vDomain();
        surface = sphere;
        break;
    }
    case KernelLabShape::Torus: {
        auto torus = std::make_shared<ToroidalSurface>(Frame3(), 1.6, 0.55);
        occt = new Geom_ToroidalSurface(gp_Ax3(), 1.6, 0.55);
        u = torus->uDomain();
        v = torus->vDomain();
        surface = torus;
        break;
    }
    case KernelLabShape::Cone: {
        const Frame3 frame(Vec3(0.0, 0.0, -1.2), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0));
        surface = std::make_shared<ConicalSurface>(frame, 0.45, 0.25);
        occt = new Geom_ConicalSurface(gp_Ax3(gp_Pnt(0.0, 0.0, -1.2), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 0.45, 0.25);
        u = {0.0, kTwoPi};
        v = {0.0, 2.6};
        center = Vec3(0.0, 0.0, 0.0);
        break;
    }
    case KernelLabShape::BSpline: {
        // Lamiera ondulata NURBS 6x6, grado 3x3, con un polo centrale pesante.
        constexpr int count = 6;
        std::vector<Vec3> poles;
        std::vector<double> weights;
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < count; ++j) {
                const double x = -2.2 + 4.4 * i / (count - 1), y = -2.2 + 4.4 * j / (count - 1);
                poles.push_back(Vec3(x, y, 0.9 * std::sin(1.3 * x) * std::cos(1.1 * y)));
                weights.push_back((i == 2 || i == 3) && (j == 2 || j == 3) ? 2.5 : 1.0);
            }
        auto bspline = std::make_shared<BSplineSurface>(3, 3, uniformKnots(3, count), uniformKnots(3, count), count,
                                                        count, std::move(poles), std::move(weights));
        occt = toOcct(*bspline);
        u = bspline->uDomain();
        v = bspline->vDomain();
        surface = bspline;
        break;
    }
    case KernelLabShape::Revolution: {
        // Profilo di un vaso nel piano XZ, ruotato attorno a Z.
        const std::vector<Vec3> poles{Vec3(0.6, 0, -1.5), Vec3(1.5, 0, -1.1), Vec3(0.5, 0, -0.3),
                                      Vec3(0.35, 0, 0.4), Vec3(1.3, 0, 1.0), Vec3(0.8, 0, 1.6)};
        auto meridian = std::make_shared<BSplineCurve<3>>(3, uniformKnots(3, 6), poles);
        surface = std::make_shared<RevolutionSurface>(meridian, Vec3(), Vec3(0, 0, 1));
        occt = new Geom_SurfaceOfRevolution(toOcct(*meridian), gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
        u = {0.0, kTwoPi};
        v = meridian->domain();
        break;
    }
    case KernelLabShape::PrismBody:
    case KernelLabShape::CylinderBody: {
        const Frame3 frame(Vec3(0.0, 0.0, -0.6), Vec3(0.0, 0.0, 1.0), Vec3(1.0, 0.0, 0.0));
        std::vector<Vec2> outer;  // stella a 7 punte
        for (int i = 0; i < 14; ++i) {
            const double angle = kTwoPi * i / 14.0, radius = i % 2 ? 1.3 : 2.4;
            outer.push_back(Vec2(radius * std::cos(angle), radius * std::sin(angle)));
        }
        const std::vector<std::vector<Vec2>> holes{
            {Vec2(-0.9, -0.3), Vec2(-0.3, -0.3), Vec2(-0.3, 0.3), Vec2(-0.9, 0.3)},
            {Vec2(0.3, -0.2), Vec2(0.9, -0.5), Vec2(0.8, 0.4)}};
        const Body body = shape == KernelLabShape::PrismBody ? makePrism(frame, outer, holes, 1.2)
                                                              : makeCylinder(frame, 1.4, 1.2);
        for (EdgeId e : body.edges()) {
            QVector<QVector3D> polyline;
            const Edge &edge = body.edge(e);
            for (int i = 0; i <= 64; ++i) polyline.append(toDisplay(edge.curve->point(edge.range.lo + edge.range.length() * i / 64.0)));
            wireframe.append(polyline);
        }
        const TopologyCounts counts = body.counts();
        const std::vector<CheckIssue> issues = checkBody(body);
        const MassProperties mass = massProperties(body);
        summary = QStringLiteral("Solido B-rep: V %1, E %2, F %3, anelli %4, genere %5 | checkBody: %6"
                                 " | volume %7, area %8, baricentro (%9, %10, %11)")
                      .arg(counts.vertices).arg(counts.edges).arg(counts.faces).arg(counts.rings())
                      .arg(shellGenus(body, body.shells().front()))
                      .arg(issues.empty() ? QStringLiteral("valido")
                                          : QStringLiteral("%1 problemi (%2)").arg(issues.size())
                                                .arg(QString::fromStdString(issues.front().message)))
                      .arg(mass.volume, 0, 'f', 9).arg(mass.area, 0, 'f', 9)
                      .arg(mass.centroid.x(), 0, 'f', 6).arg(mass.centroid.y(), 0, 'f', 6).arg(mass.centroid.z(), 0, 'f', 6);
        return;
    }
    case KernelLabShape::Extrusion: {
        // Curva a S nel piano XY estrusa lungo Z.
        const std::vector<Vec3> poles{Vec3(-2.2, -0.6, 0), Vec3(-1.2, 1.4, 0), Vec3(0.0, -1.4, 0),
                                      Vec3(1.2, 1.4, 0), Vec3(2.2, -0.6, 0)};
        auto curve = std::make_shared<BSplineCurve<3>>(3, uniformKnots(3, 5), poles);
        surface = std::make_shared<ExtrusionSurface>(curve, Vec3(0, 0, 1));
        occt = new Geom_SurfaceOfLinearExtrusion(toOcct(*curve), gp_Dir(0, 0, 1));
        u = curve->domain();
        v = {-1.2, 1.2};
        break;
    }
    }
    buildWireframe();
}

void KernelLab::Impl::buildWireframe() {
    constexpr int lines = 14, samples = 96;
    for (int direction = 0; direction < 2; ++direction) {
        for (int line = 0; line <= lines; ++line) {
            QVector<QVector3D> polyline;
            for (int sample = 0; sample <= samples; ++sample) {
                const double a = double(line) / lines, b = double(sample) / samples;
                const double uu = u.lo + u.length() * (direction == 0 ? a : b);
                const double vv = v.lo + v.length() * (direction == 0 ? b : a);
                polyline.append(toDisplay(surface->point(uu, vv)));
            }
            wireframe.append(polyline);
        }
    }
}

KernelLab::KernelLab() : impl_(std::make_unique<Impl>()) {}
KernelLab::~KernelLab() = default;

void KernelLab::setShape(KernelLabShape shape) { impl_->build(shape); }
KernelLabShape KernelLab::shape() const { return impl_->shape; }
QVector3D KernelLab::center() const { return toDisplay(impl_->center); }
const QVector<QVector<QVector3D>> &KernelLab::wireframe() const { return impl_->wireframe; }
QString KernelLab::summary() const { return impl_->summary; }

QString KernelLab::shapeName() const {
    switch (impl_->shape) {
    case KernelLabShape::Sphere: return QStringLiteral("sfera");
    case KernelLabShape::Torus: return QStringLiteral("toro");
    case KernelLabShape::Cone: return QStringLiteral("cono");
    case KernelLabShape::BSpline: return QStringLiteral("superficie NURBS");
    case KernelLabShape::Revolution: return QStringLiteral("superficie di rivoluzione");
    case KernelLabShape::Extrusion: return QStringLiteral("superficie estrusa");
    case KernelLabShape::PrismBody: return QStringLiteral("solido: prisma con due fori");
    case KernelLabShape::CylinderBody: return QStringLiteral("solido: cilindro senza cucitura");
    case KernelLabShape::SketchExtrusion: return QStringLiteral("estrusione dello schizzo col nuovo kernel");
    case KernelLabShape::DocumentBody: return QStringLiteral("corpo del documento col nuovo kernel");
    default: return QString();
    }
}

KernelLabProbe KernelLab::probe(double x, double y, double z) const {
    KernelLabProbe result;
    if (!impl_->surface) return result;
    const Vec3 p(x, y, z);
    result.cursor = toDisplay(p);

    SurfaceProjection ours;
    result.oursMicroseconds = timeAverage([&](int) { ours = projectPoint(*impl_->surface, p, impl_->u, impl_->v); });
    result.valid = true;
    result.ours = toDisplay(ours.point);
    result.oursDistance = ours.distance;
    try {
        result.normal = toDisplay(impl_->surface->normal(ours.u, ours.v));
        result.hasNormal = true;
    } catch (const std::domain_error &) {
        result.hasNormal = false;  // punto singolare (polo, vertice)
    }

    try {
        result.occtMicroseconds = timeAverage([&](int repetition) {
            GeomAPI_ProjectPointOnSurf projector(toPnt(p), impl_->occt, impl_->u.lo, impl_->u.hi, impl_->v.lo, impl_->v.hi);
            if (repetition == 0 && projector.NbPoints() > 0) {
                result.occtFound = true;
                const gp_Pnt nearest = projector.NearestPoint();
                result.occt = QVector3D(float(nearest.X()), float(nearest.Y()), float(nearest.Z()));
                result.occtDistance = projector.LowerDistance();
                result.pointGap = distance(Vec3(nearest.X(), nearest.Y(), nearest.Z()), ours.point);
                result.tie = result.pointGap > 1.0e-6
                          && std::fabs(result.occtDistance - result.oursDistance) <= 1.0e-9 * std::max(1.0, result.oursDistance);
            }
        });
    } catch (const Standard_Failure &) {
        result.occtFound = false;
    }
    return result;
}

QString KernelLab::compareSketchExtrusion(const SketchObject &sketch, double distance, const TopoDS_Shape &occtShape) {
    impl_->build(KernelLabShape::None);
    const Profile profile = buildProfile(sketchSegments(sketch), kSketchConnectionTolerance);
    if (profile.regions.empty())
        return QStringLiteral("Lo schizzo non ha contorni chiusi: il nuovo kernel non estrude ancora profili aperti.");

    const gp_Ax3 axes = sketchAxes(sketch.plane);
    auto fromDir = [](const gp_Dir &d) { return Vec3(d.X(), d.Y(), d.Z()); };
    const Frame3 frame(Vec3(axes.Location().X(), axes.Location().Y(), axes.Location().Z()), fromDir(axes.Direction()),
                       fromDir(axes.XDirection()));
    const double height = extrusionVector(sketch.plane, distance).Dot(gp_Vec(axes.Direction()));

    int vertices = 0, edges = 0, faces = 0, genus = 0, issues = 0, fins = 0, exactPCurves = 0;
    double volume = 0.0, surfaceArea = 0.0, length = 0.0;
    Vec3 moment;  // somma di volume x baricentro dei solidi
    QString firstIssue;
    try {
        for (const ProfileRegion &region : profile.regions) {
            const Body body = makeExtrusion(frame, region, height);
            const TopologyCounts counts = body.counts();
            vertices += counts.vertices;
            edges += counts.edges;
            faces += counts.faces;
            genus += shellGenus(body, body.shells().front());
            for (FinId f : body.fins()) {
                ++fins;
                exactPCurves += body.fin(f).pcurve && body.fin(f).pcurveTolerance == 0.0;
            }
            // Proprieta' di massa integrate sulle facce esatte del B-rep.
            const MassProperties mass = massProperties(body);
            volume += mass.volume;
            surfaceArea += mass.area;
            moment += mass.volume * mass.centroid;
            const std::vector<CheckIssue> found = checkBody(body);
            issues += int(found.size());
            if (!found.empty() && firstIssue.isEmpty()) firstIssue = QString::fromStdString(found.front().message);
            for (EdgeId e : body.edges()) {
                const Edge &edge = body.edge(e);
                length += arcLength(*edge.curve, edge.range);
                QVector<QVector3D> polyline;
                for (int i = 0; i <= 64; ++i) polyline.append(toDisplay(edge.curve->point(edge.range.lo + edge.range.length() * i / 64.0)));
                impl_->wireframe.append(polyline);
            }
        }
    } catch (const std::exception &error) {
        impl_->wireframe.clear();
        return QStringLiteral("Il nuovo kernel non e' riuscito a estrudere lo schizzo: %1").arg(QString::fromUtf8(error.what()));
    }
    impl_->shape = KernelLabShape::SketchExtrusion;

    QString comparison;
    if (!occtShape.IsNull()) {
        // Due riferimenti OCCT per il volume: l'area del profilo (integrazione
        // adattiva) per l'altezza, affidabile; e BRepGProp::VolumeProperties,
        // che sulle facce estruse da spline sbaglia anche dello 0.7%.
        double profileArea = 0.0;
        TopoDS_Shape occtProfile;
        bool closed = false;
        if (buildSketchProfile(sketch, occtProfile, closed, nullptr) && closed)
            for (TopExp_Explorer face(occtProfile, TopAbs_FACE); face.More(); face.Next()) {
                GProp_GProps properties;
                BRepGProp::SurfaceProperties(face.Current(), properties, 1e-12);
                profileArea += std::fabs(properties.Mass());
            }
        GProp_GProps properties, surface;
        BRepGProp::VolumeProperties(occtShape, properties);
        BRepGProp::SurfaceProperties(occtShape, surface, 1e-12);
        const double occtLength = occtEdgeLengthWithoutSeams(occtShape);
        const Vec3 centroid = volume != 0.0 ? moment / volume : Vec3();
        const gp_Pnt occtCentroid = properties.CentreOfMass();
        comparison = QStringLiteral(" | volume: nuovo %1, OCCT area x altezza %2, OCCT BRepGProp %3"
                                    " | area: nuovo %4, OCCT %5 | baricentro: nuovo (%6, %7, %8), OCCT (%9, %10, %11)"
                                    " | spigoli: nuovo %12, OCCT senza cuciture %13 | facce OCCT %14")
                         .arg(volume, 0, 'f', 9).arg(profileArea * std::fabs(height), 0, 'f', 9)
                         .arg(std::fabs(properties.Mass()), 0, 'f', 9)
                         .arg(surfaceArea, 0, 'f', 9).arg(surface.Mass(), 0, 'f', 9)
                         .arg(centroid.x(), 0, 'f', 6).arg(centroid.y(), 0, 'f', 6).arg(centroid.z(), 0, 'f', 6)
                         .arg(occtCentroid.X(), 0, 'f', 6).arg(occtCentroid.Y(), 0, 'f', 6).arg(occtCentroid.Z(), 0, 'f', 6)
                         .arg(length, 0, 'f', 9).arg(occtLength, 0, 'f', 9)
                         .arg(countShapes(occtShape, TopAbs_FACE));
    }
    impl_->summary = QStringLiteral("Nuovo kernel: %1 solidi, V %2 E %3 F %4, genere %5, SP-curve esatte %8/%9, checkBody: %6%7")
                         .arg(profile.regions.size()).arg(vertices).arg(edges).arg(faces).arg(genus)
                         .arg(issues == 0 ? QStringLiteral("valido") : QStringLiteral("%1 problemi (%2)").arg(issues).arg(firstIssue))
                         .arg(comparison).arg(exactPCurves).arg(fins);
    return QString();
}


namespace {

// Sistema del piano di schizzo e altezza con segno lungo la sua normale.
void sketchFrame(const SketchObject &sketch, double distance, Frame3 &frame, double &height) {
    const gp_Ax3 axes = sketchAxes(sketch.plane);
    auto fromDir = [](const gp_Dir &d) { return Vec3(d.X(), d.Y(), d.Z()); };
    frame = Frame3(Vec3(axes.Location().X(), axes.Location().Y(), axes.Location().Z()), fromDir(axes.Direction()),
                   fromDir(axes.XDirection()));
    height = extrusionVector(sketch.plane, distance).Dot(gp_Vec(axes.Direction()));
}

// Corpo del documento col nuovo kernel (lancia std::exception se non riesce).
Body documentBody(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies, int index, int depth) {
    if (index < 0 || index >= bodies.size() || depth > 64) throw std::invalid_argument("riferimento a un corpo inesistente");
    const ExtrusionObject &object = bodies.at(index);
    if (object.operation >= 0) {
        const Body first = documentBody(sketches, bodies, object.firstBody, depth + 1);
        const Body second = documentBody(sketches, bodies, object.secondBody, depth + 1);
        return booleanOperation(first, second, BooleanOperation(object.operation));
    }
    if (object.sketchIndex < 0 || object.sketchIndex >= sketches.size()) throw std::invalid_argument("schizzo inesistente");
    const SketchObject &sketch = sketches.at(object.sketchIndex);
    const Profile profile = buildProfile(sketchSegments(sketch), kSketchConnectionTolerance);
    if (profile.regions.empty()) throw std::invalid_argument("lo schizzo non ha contorni chiusi");
    Frame3 frame;
    double height;
    sketchFrame(sketch, object.distance, frame, height);
    // Piu' regioni: unione (disgiunta) dei loro prismi.
    Body result = makeExtrusion(frame, profile.regions.front(), height);
    for (std::size_t i = 1; i < profile.regions.size(); ++i)
        result = booleanOperation(result, makeExtrusion(frame, profile.regions[i], height), BooleanOperation::Unite);
    return result;
}

}

QString KernelLab::compareDocumentBody(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies, int index) {
    impl_->build(KernelLabShape::None);
    Body body;
    try {
        body = documentBody(sketches, bodies, index, 0);
    } catch (const std::exception &error) {
        return QStringLiteral("Il nuovo kernel non e' riuscito a ricostruire il corpo: %1").arg(QString::fromUtf8(error.what()));
    }
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        QVector<QVector3D> polyline;
        for (int i = 0; i <= 64; ++i) polyline.append(toDisplay(edge.curve->point(edge.range.lo + edge.range.length() * i / 64.0)));
        impl_->wireframe.append(polyline);
    }
    impl_->shape = KernelLabShape::DocumentBody;

    const TopologyCounts counts = body.counts();
    const std::vector<CheckIssue> issues = checkBody(body);
    QString mass;
    try {
        const MassProperties properties = massProperties(body);
        mass = QStringLiteral("volume %1, area %2, baricentro (%3, %4, %5)")
                   .arg(properties.volume, 0, 'f', 9).arg(properties.area, 0, 'f', 9)
                   .arg(properties.centroid.x(), 0, 'f', 6).arg(properties.centroid.y(), 0, 'f', 6).arg(properties.centroid.z(), 0, 'f', 6);
    } catch (const std::exception &error) {
        mass = QStringLiteral("proprieta' di massa non calcolate (%1)").arg(QString::fromUtf8(error.what()));
    }
    QString occt;
    const TopoDS_Shape &shape = bodies.at(index).shape;
    if (!shape.IsNull()) {
        GProp_GProps volume, surface;
        BRepGProp::VolumeProperties(shape, volume);
        BRepGProp::SurfaceProperties(shape, surface, 1e-12);
        const gp_Pnt c = volume.CentreOfMass();
        occt = QStringLiteral(" | OCCT: volume %1, area %2, baricentro (%3, %4, %5), facce %6")
                   .arg(std::fabs(volume.Mass()), 0, 'f', 9).arg(surface.Mass(), 0, 'f', 9)
                   .arg(c.X(), 0, 'f', 6).arg(c.Y(), 0, 'f', 6).arg(c.Z(), 0, 'f', 6).arg(countShapes(shape, TopAbs_FACE));
    }
    impl_->summary = QStringLiteral("Nuovo kernel: V %1 E %2 F %3, shell %4, checkBody: %5 | %6%7")
                         .arg(counts.vertices).arg(counts.edges).arg(counts.faces).arg(counts.shells)
                         .arg(issues.empty() ? QStringLiteral("valido") : QString::fromStdString(issues.front().message))
                         .arg(mass).arg(occt);
    return QString();
}

}
