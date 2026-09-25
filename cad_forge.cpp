#include "cad_forge.h"

#include <gp_Ax3.hxx>

#include <algorithm>
#include <cmath>
#include <exception>

#include "cad_kernel.h"
#include "fk_boolean.h"
#include "fk_bspline.h"
#include "fk_classify.h"
#include "fk_extrude.h"
#include "fk_intersect.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_tessellate.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

Vec2 toVec(const QPointF &point) { return Vec2(point.x(), point.y()); }
QVector3D toDisplay(const Vec3 &v) { return QVector3D(float(v.x()), float(v.y()), float(v.z())); }

ProfileSegment lineSegment(const Vec2 &a, const Vec2 &b) {
    return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}};
}

}

// Stessa geometria esatta di ForgeCad::curveGeometry (cad_curve_solver.cpp),
// ma con le curve del nuovo kernel.
std::vector<ProfileSegment> forgeSketchSegments(const SketchObject &sketch) {
    constexpr double confusion = 1.0e-7;  // Precision::Confusion()
    std::vector<ProfileSegment> result;
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index)) continue;
        const SketchSegment &segment = sketch.segments.at(index);
        const Vec2 a = toVec(segment.first), b = toVec(segment.second);
        if (distance(a, b) > confusion) result.push_back(lineSegment(a, b));
    }
    for (const CurveObject &curve : sketch.curves) {
        if (curve.construction) continue;
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

void forgeSketchFrame(const SketchObject &sketch, double distance, Frame3 &frame, double &height) {
    const gp_Ax3 axes = sketchAxes(sketch.plane);
    auto fromDir = [](const gp_Dir &d) { return Vec3(d.X(), d.Y(), d.Z()); };
    frame = Frame3(Vec3(axes.Location().X(), axes.Location().Y(), axes.Location().Z()), fromDir(axes.Direction()),
                   fromDir(axes.XDirection()));
    height = extrusionVector(sketch.plane, distance).Dot(gp_Vec(axes.Direction()));
}

ForgeBody forgeExtrusion(const SketchObject &sketch, double distance, QString *error) {
    if (std::abs(distance) <= 1.0e-7) {
        setError(error, QStringLiteral("La distanza di estrusione e' nulla."));
        return nullptr;
    }
    try {
        const std::vector<ProfileSegment> segments = forgeSketchSegments(sketch);
        if (segments.empty()) {
            setError(error, QStringLiteral("Lo schizzo non contiene geometria."));
            return nullptr;
        }
        const Profile profile = buildProfile(segments, kSketchConnectionTolerance);
        Frame3 frame;
        double height;
        forgeSketchFrame(sketch, distance, frame, height);
        // Nessun contorno chiuso: le catene aperte diventano una superficie
        // (lamina), come fa OCCT con i fili aperti.
        if (profile.regions.empty()) return std::make_shared<const Body>(makeSheetExtrusion(frame, profile.chains, height));
        // Piu' regioni: unione (disgiunta) dei loro prismi.
        Body result = makeExtrusion(frame, profile.regions.front(), height);
        for (std::size_t i = 1; i < profile.regions.size(); ++i)
            result = booleanOperation(result, makeExtrusion(frame, profile.regions[i], height), Kernel::BooleanOperation::Unite);
        return std::make_shared<const Body>(std::move(result));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Estrusione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeRevolution(const SketchObject &sketch, int axis, double angleDegrees, QString *error) {
    QPointF point, direction;
    if (!sketchRevolutionAxis(sketch, axis, point, direction, error)) return nullptr;
    if (std::abs(angleDegrees) <= 1.0e-9) {
        setError(error, QStringLiteral("L'angolo di rivoluzione e' nullo."));
        return nullptr;
    }
    const int side = revolutionProfileSide(sketch, point, direction, error);
    if (side == 0) return nullptr;
    // Il kernel vuole il profilo nel semipiano x >= 0 del piano XZ del suo
    // sistema, con l'asse lungo Z: coordinate (x, y) = (distanza dall'asse,
    // posizione lungo l'asse). E' un movimento rigido del piano (rotazione e
    // traslazione: archi e curve restano esatti e con lo stesso verso). Con il
    // profilo a sinistra dell'asse lo si percorre al contrario e l'angolo
    // cambia segno (la stessa rotazione).
    const QPointF axisDirection = side > 0 ? -direction : direction;
    const QPointF normal(axisDirection.y(), -axisDirection.x());  // a destra dell'asse
    const double angle = (side > 0 ? -angleDegrees : angleDegrees) * kPi / 180.0;
    const auto local = [&](const QPointF &q) {
        const QPointF r = q - point;
        double x = r.x() * normal.x() + r.y() * normal.y();
        // Estremi sull'asse entro la tolleranza di collegamento dello schizzo.
        if (std::abs(x) <= kSketchConnectionTolerance) x = 0.0;
        return QPointF(x, r.x() * axisDirection.x() + r.y() * axisDirection.y());
    };
    const auto localHandle = [&](const QPointF &q) {
        const QPointF r = q - point;
        return QPointF(r.x() * normal.x() + r.y() * normal.y(), r.x() * axisDirection.x() + r.y() * axisDirection.y());
    };
    SketchObject profileSketch = sketch;
    for (SketchSegment &segment : profileSketch.segments) segment = qMakePair(local(segment.first), local(segment.second));
    for (CurveObject &curve : profileSketch.curves) {
        for (QPointF &control : curve.controlPoints) control = local(control);
        for (QPair<QPointF, QPointF> &handles : curve.tangentHandles) handles = qMakePair(localHandle(handles.first), localHandle(handles.second));
    }
    try {
        const std::vector<ProfileSegment> segments = forgeSketchSegments(profileSketch);
        const Profile profile = buildProfile(segments, kSketchConnectionTolerance);
        if (profile.regions.empty()) {
            setError(error, QStringLiteral("La rivoluzione richiede un profilo chiuso."));
            return nullptr;
        }
        const gp_Ax3 axes = sketchAxes(sketch.plane);
        const Vec3 xs(axes.XDirection().X(), axes.XDirection().Y(), axes.XDirection().Z());
        const Vec3 ys(axes.YDirection().X(), axes.YDirection().Y(), axes.YDirection().Z());
        const gp_Pnt origin = sketchToWorld(point, sketch.plane);
        const Frame3 frame(Vec3(origin.X(), origin.Y(), origin.Z()), axisDirection.x() * xs + axisDirection.y() * ys,
                           normal.x() * xs + normal.y() * ys);
        Body result = makeRevolution(frame, profile.regions.front(), angle);
        for (std::size_t i = 1; i < profile.regions.size(); ++i)
            result = booleanOperation(result, makeRevolution(frame, profile.regions[i], angle), Kernel::BooleanOperation::Unite);
        return std::make_shared<const Body>(std::move(result));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Rivoluzione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgePrimitive(const PrimitiveParameters &parameters, QString *error) {
    const QString invalid = primitiveError(parameters);
    if (!invalid.isEmpty()) {
        setError(error, invalid);
        return nullptr;
    }
    const gp_Ax3 axes = primitiveAxes(parameters);
    auto fromDir = [](const gp_Dir &d) { return Vec3(d.X(), d.Y(), d.Z()); };
    const Frame3 frame(Vec3(axes.Location().X(), axes.Location().Y(), axes.Location().Z()), fromDir(axes.Direction()),
                       fromDir(axes.XDirection()));
    const double *size = parameters.size;
    try {
        Body body;
        switch (parameters.kind) {
        case PrimitiveKind::Box: body = makeBox(frame, size[0], size[1], size[2]); break;
        case PrimitiveKind::Cylinder: body = makeCylinder(frame, size[0], size[1]); break;
        case PrimitiveKind::Sphere: body = makeSphere(frame, size[0]); break;
        case PrimitiveKind::Cone: body = makeCone(frame, size[0], size[1], size[2]); break;
        case PrimitiveKind::Torus: body = makeTorus(frame, size[0], size[1]); break;
        }
        return std::make_shared<const Body>(std::move(body));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Primitiva non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeBoolean(const ForgeBody &first, const ForgeBody &second, ::BooleanOperation operation, QString *error) {
    if (!first || !second) {
        setError(error, QStringLiteral("Uno degli operandi non ha geometria valida."));
        return nullptr;
    }
    // Lamina con solido: il kernel tiene la parte della lamina dentro
    // (intersezione) o fuori (differenza) e rifiuta gli altri casi.
    try {
        Body result = booleanOperation(*first, *second, Kernel::BooleanOperation(int(operation)));
        if (result.faces().empty()) {
            setError(error, QStringLiteral("Il risultato dell'operazione e' vuoto: i due solidi non si sovrappongono."));
            return nullptr;
        }
        return std::make_shared<const Body>(std::move(result));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("L'operazione booleana non e' riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

void forgeTessellate(const Body &body, int quality, BodyDisplay &display) {
    display = {};
    display.quality = quality;
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (edge.curve) box.add(curveBox(*edge.curve, edge.range));
    }
    for (FaceId f : body.faces()) box.add(faceBox(body, f));
    if (box.isEmpty()) return;
    const double diagonal = std::max(box.diagonal(), 1e-9);
    TessellationOptions options;
    options.deflection = diagonal * (quality <= 0 ? 4.0e-3 : quality == 1 ? 1.0e-3 : 2.0e-4);
    options.angle = quality <= 0 ? 0.5 : quality == 1 ? 0.25 : 0.1;
    const Tessellation mesh = tessellate(body, options);
    for (const FaceMesh &face : mesh.faces)
        for (const std::array<int, 3> &triangle : face.triangles)
            for (int index : triangle) {
                display.vertices.append(toDisplay(face.points[std::size_t(index)]));
                display.normals.append(toDisplay(face.normals[std::size_t(index)]));
            }
    for (const std::vector<Vec3> &edge : mesh.edges) {
        QVector<QVector3D> polyline;
        for (const Vec3 &point : edge) polyline.append(toDisplay(point));
        if (polyline.size() >= 2) display.edges.append(polyline);
    }
}

bool forgeIntersectRay(const Body &body, const QVector3D &origin, const QVector3D &direction, double &distance) {
    try {
        double t = 0.0;
        if (!firstRayHit(body, Vec3(origin.x(), origin.y(), origin.z()), Vec3(direction.x(), direction.y(), direction.z()), 1e-7, t))
            return false;
        distance = t;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

}
