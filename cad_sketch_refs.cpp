#include "cad_sketch_refs.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_kernel.h"
#include "fk_boolean.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_precision.h"
#include "fk_sheet.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void appendSegment(SketchObject &sketch, const QPointF &a, const QPointF &b, bool construction) {
    sketch.segments.append({a, b});
    sketch.constraints.append(-1);
    sketch.segmentLengths.append(0.0);
    sketch.segmentAngles.append(-1.0);
    if (construction) sketch.constructionSegments.append(sketch.segments.size() - 1);
}

// Il solido sta tutto dalla parte n . (x - origin) <= tolerance? Certezza
// esatta, altrimenti falso (e si usa la booleana). Il massimo della funzione
// lineare h sul bordo del solido si raggiunge sul bordo di una faccia (i suoi
// edge: estremi e radici di h = tolerance dentro il tratto, esatte) o in un
// punto critico interno (normale della superficie parallela a n): piani,
// cilindri e superfici estruse non ne hanno di propri (lungo le rette della
// superficie h e' lineare, e la retta per un punto interno esce dalla faccia
// sul bordo); il cono ha il vertice; la sfera i due punti c +- r n; il toro
// quattro punti (o due cerchi se n e' parallelo all'asse). Un punto critico
// sopra la soglia che sta nella faccia rompe la condizione; un cerchio critico
// sopra la soglia non puo' toccare il bordo (che sta sotto) e si prova in un punto.
bool onOneSide(const Body &body, const Vec3 &origin, const Vec3 &n, double tolerance) {
    const auto height = [&](const Vec3 &p) { return dot(n, p - origin); };
    for (VertexId v : body.vertices())
        if (height(body.vertex(v).point) > tolerance) return false;
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (!edge.curve) continue;
        if (height(edge.curve->point(edge.range.lo)) > tolerance || height(edge.curve->point(edge.range.hi)) > tolerance) return false;
        const PlaneRoots<3> roots = planeRoots<3>(*edge.curve, edge.range, n, dot(n, origin) + tolerance, 1e-3 * tolerance);
        if (!roots.coincident.empty()) return false;
        const double margin = 1e-12 * std::max(1.0, edge.range.length());
        for (double t : roots.parameters)
            if (t > edge.range.lo + margin && t < edge.range.hi - margin) return false;
    }
    const auto inside = [&](FaceId f, const Vec3 &p) {
        return height(p) > tolerance && classifyPointOnFace(body, f, p, 1e-3 * tolerance) != PointLocation::Outside;
    };
    for (FaceId f : body.faces()) {
        const Surface &surface = *body.face(f).surface;
        switch (surface.type()) {
        case SurfaceType::Plane:
        case SurfaceType::Cylinder:
        case SurfaceType::Extrusion:
            break;
        case SurfaceType::Cone:
            if (height(static_cast<const ConicalSurface &>(surface).apex()) > tolerance) return false;
            break;
        case SurfaceType::Sphere: {
            const auto &sphere = static_cast<const SphericalSurface &>(surface);
            if (inside(f, sphere.frame().origin() + sphere.radius() * n)) return false;
            break;
        }
        case SurfaceType::Torus: {
            const auto &torus = static_cast<const ToroidalSurface &>(surface);
            const Frame3 &frame = torus.frame();
            const double R = torus.majorRadius(), r = torus.minorRadius();
            if (!(R > r)) return false;  // toro a fuso: punti singolari
            const Vec3 c = frame.origin(), a = frame.zDir();
            const double na = dot(n, a);
            if (std::fabs(na) >= 1.0 - 1e-12) {
                // Il cerchio piu' alto (v = +-pi/2), tutto dentro o tutto fuori: un suo punto.
                if (inside(f, c + R * frame.xDir() + (na > 0.0 ? r : -r) * a)) return false;
                break;
            }
            const Vec3 p = normalized(n - na * a);
            const double np = norm(n - na * a);
            for (double sigma : {1.0, -1.0})
                for (double s : {1.0, -1.0}) {
                    const double cosV = sigma * s * np, sinV = s * na;
                    if (inside(f, c + (R + r * cosV) * sigma * p + r * sinV * a)) return false;
                }
            break;
        }
        case SurfaceType::BSpline: {
            // Inviluppo convesso dei poli (pesi positivi): basta che i poli stiano sotto la soglia.
            const auto &spline = static_cast<const BSplineSurface &>(surface);
            for (int i = 0; i < spline.uPoleCount(); ++i)
                for (int j = 0; j < spline.vPoleCount(); ++j)
                    if (!(spline.weight(i, j) > 0.0) || height(spline.pole(i, j)) > tolerance) return false;
            break;
        }
        default:
            return false;  // rivoluzioni: non si sa dire in modo esatto
        }
    }
    return true;
}

// Toglie i livelli di TrimmedCurve (il tratto resta quello dato).
const Curve<3> *basisOf(const Curve<3> *curve) {
    while (curve && curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<3> *>(curve)->basis().get();
    return curve;
}

}

bool nearestBodyEdge(const Body &body, const Vec3 &point, CurvePtr<3> &curve, Interval &range) {
    double best = std::numeric_limits<double>::max();
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (!edge.curve) continue;
        const double d = projectPoint(*edge.curve, point, edge.range).distance;
        if (d < best) best = d, curve = edge.curve, range = edge.range;
    }
    return best < std::numeric_limits<double>::max();
}

QString appendProjectedCurve(SketchObject &sketch, const CurvePtr<3> &curve, const Interval &range, bool construction, bool fixed,
                             QVector<SketchEntity> *created) {
    if (!curve || !(range.hi > range.lo)) return QStringLiteral("Curva non valida.");
    const Frame3 frame = sketchAxes(sketch);
    const auto local = [&](const Vec3 &p) {
        const Vec3 q = frame.toLocal(p);
        return QPointF(q.x(), q.y());
    };
    const int segmentsBefore = sketch.segments.size(), curvesBefore = sketch.curves.size();
    try {
        const Curve<3> *basis = basisOf(curve.get());
        if (basis->type() == CurveType::Line) {
            const QPointF a = local(curve->point(range.lo)), b = local(curve->point(range.hi));
            if (std::hypot(b.x() - a.x(), b.y() - a.y()) <= kSketchConnectionTolerance)
                return QStringLiteral("Lo spigolo e' perpendicolare al piano dello schizzo: la sua proiezione e' un punto.");
            appendSegment(sketch, a, b, construction);
        } else if (basis->type() == CurveType::Circle
                   && std::fabs(std::fabs(dot(normalized(cross(static_cast<const Circle<3> *>(basis)->xAxis(), static_cast<const Circle<3> *>(basis)->yAxis())),
                                              frame.zDir())) - 1.0) <= 1e-12) {
            // Cerchio in un piano parallelo: cerchio o arco (antiorario attorno alla normale dello schizzo).
            const auto *circle = static_cast<const Circle<3> *>(basis);
            const Vec3 axis = normalized(cross(circle->xAxis(), circle->yAxis()));
            CurveObject object;
            object.construction = construction;
            const QPointF center = local(circle->center());
            if (range.hi - range.lo >= kTwoPi - 1e-12) {
                object.tool = DrawingTool::Circle;
                object.controlPoints = {center, local(curve->point(range.lo))};
            } else {
                object.tool = DrawingTool::Arc;
                const QPointF start = local(curve->point(range.lo)), end = local(curve->point(range.hi));
                const bool counterClockwise = dot(axis, frame.zDir()) > 0.0;
                object.controlPoints = {center, counterClockwise ? start : end, counterClockwise ? end : start};
            }
            recalculateCurve(object);
            sketch.curves.append(object);
        } else {
            // B-spline razionale esatta (le eliche e le altre curve "Other" dalla loro B-spline).
            BSplineCurve<3> spline(1, {0.0, 0.0, 1.0, 1.0}, {Vec3(), Vec3(1, 0, 0)});
            if (const auto *helix = dynamic_cast<const HelixCurve *>(basis)) {
                const BSplineCurve<3> full = helixBSpline(*helix);
                std::vector<BSplineCurve<3>> pieces = standardBezierPieces(full, range);
                int degree = 1;
                for (const BSplineCurve<3> &piece : pieces) degree = std::max(degree, piece.degree());
                spline = joinBezierPieces(standardBezierPieces(full, range, degree));
            } else {
                std::vector<BSplineCurve<3>> pieces = standardBezierPieces(*curve, range);
                int degree = 1;
                for (const BSplineCurve<3> &piece : pieces) degree = std::max(degree, piece.degree());
                spline = joinBezierPieces(standardBezierPieces(*curve, range, degree));
            }
            // Curva in un piano perpendicolare allo schizzo (i poli proiettati stanno
            // su una retta): la proiezione e' il segmento tra i punti estremi della curva lungo la retta.
            std::vector<QPointF> projected;
            for (const Vec3 &p : spline.poles()) projected.push_back(local(p));
            QPointF a = projected.front(), b = projected.front();
            double span = 0.0;
            for (const QPointF &p : projected)
                for (const QPointF &q : projected)
                    if (std::hypot(p.x() - q.x(), p.y() - q.y()) > span) span = std::hypot(p.x() - q.x(), p.y() - q.y()), a = p, b = q;
            bool collinear = span > kSketchConnectionTolerance;
            for (const QPointF &p : projected) {
                const QPointF d = b - a, r = p - a;
                collinear = collinear && std::fabs(d.x() * r.y() - d.y() * r.x()) / span <= 1e-9 * std::max(1.0, span);
            }
            if (collinear) {
                // Estremi di e . C(t) (e = la retta nel piano, in 3D): campioni e Newton sulle derivate esatte.
                const Vec3 e = normalized(frame.xDir() * ((b - a).x() / span) + frame.yDir() * ((b - a).y() / span));
                double lowest = std::numeric_limits<double>::max(), highest = -lowest;
                Vec3 low, high;
                std::vector<double> breaks = spline.breakpoints(spline.domain());
                for (std::size_t k = 0; k + 1 < breaks.size(); ++k)
                    for (int i = 0; i <= 32; ++i)
                        for (const double sign : {1.0, -1.0}) {
                            double t = breaks[k] + (breaks[k + 1] - breaks[k]) * i / 32.0;
                            for (int iteration = 0; iteration < 30; ++iteration) {
                                Vec3 d[3];
                                spline.evaluate(t, 2, d);
                                const double g = sign * dot(d[1], e), h = sign * dot(d[2], e);
                                if (!(h < 0.0)) break;  // cerca il massimo di sign * e . C
                                const double next = std::clamp(t - g / h, breaks[k], breaks[k + 1]);
                                if (std::fabs(next - t) <= 1e-15 * std::max(1.0, std::fabs(t))) break;
                                t = next;
                            }
                            const Vec3 p = spline.point(t);
                            const double v = dot(p, e);
                            if (v < lowest) lowest = v, low = p;
                            if (v > highest) highest = v, high = p;
                        }
                appendSegment(sketch, local(low), local(high), construction);
                goto done;
            }
            {
            CurveObject object;
            object.tool = DrawingTool::Converted;
            object.construction = construction;
            object.degree = spline.degree();
            for (const Vec3 &p : spline.poles()) object.controlPoints.append(local(p));
            for (double w : spline.weights()) object.weights.append(w);
            for (double k : spline.knots()) object.knots.append(k);
            recalculateCurve(object);
            if (!object.numericallyValid) return QStringLiteral("La proiezione della curva sul piano dello schizzo e' degenere.");
            sketch.curves.append(object);
            }
        }
    done:;
    } catch (const std::exception &failure) {
        return QStringLiteral("Proiezione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
    for (int i = segmentsBefore; i < sketch.segments.size(); ++i) {
        if (created) created->append({0, i});
        if (fixed) sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{0, i, -1}}));
    }
    for (int i = curvesBefore; i < sketch.curves.size(); ++i) {
        if (created) created->append({1, i});
        if (fixed) sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{1, i, -1}}));
    }
    return {};
}

QString appendSectionCurves(SketchObject &sketch, const Body &body, bool construction, bool fixed, QVector<SketchEntity> *created) {
    if (body.isSheet()) return QStringLiteral("La sezione vale per i solidi.");
    try {
        const Frame3 frame = sketchAxes(sketch);
        Box box;
        for (FaceId f : body.faces()) box.add(faceBox(body, f));
        const Vec3 middle = 0.5 * (box.lo + box.hi);
        const double half = 2.0 * std::max(1.0, box.diagonal()) + distance(frame.origin(), middle);
        // Piano d'appoggio (il solido tutto da una parte: lo schizzo su una sua
        // faccia piana): la sezione sono le facce piane che stanno nel piano, e le
        // curve i loro bordi, gli edge esatti del corpo (quelli tra due facce del
        // piano sono interni). Niente booleana, che con tutto il bordo sulla
        // lamina e' lenta.
        const Vec3 normal = frame.zDir();
        if (onOneSide(body, frame.origin(), normal, kLinearResolution) || onOneSide(body, frame.origin(), -normal, kLinearResolution)) {
            std::vector<FaceId> onPlane;
            for (FaceId f : body.faces()) {
                const Surface &surface = *body.face(f).surface;
                if (surface.type() != SurfaceType::Plane) continue;
                const Frame3 &plane = static_cast<const Plane &>(surface).frame();
                if (norm(cross(plane.zDir(), normal)) <= 1e-12 && std::fabs(dot(plane.origin() - frame.origin(), normal)) <= kLinearResolution)
                    onPlane.push_back(f);
            }
            const auto inPlane = [&](FaceId f) { return std::find(onPlane.begin(), onPlane.end(), f) != onPlane.end(); };
            int count = 0;
            for (EdgeId e : body.edges()) {
                const Edge &edge = body.edge(e);
                if (!edge.curve || body.isLaminar(e) || inPlane(body.finFace(edge.forward)) == inPlane(body.finFace(edge.backward))) continue;
                const QString error = appendProjectedCurve(sketch, edge.curve, edge.range, construction, fixed, created);
                if (!error.isEmpty()) return error;
                ++count;
            }
            if (count == 0) return QStringLiteral("Il piano dello schizzo non taglia il solido.");
            return {};
        }
        // La parte del piano dentro il solido: i suoi bordi sono la sezione.
        const Body sheet = makePlaneSheet(frame, half);
        const Body inside = booleanOperation(sheet, body, Kernel::BooleanOperation::Intersect);
        int count = 0;
        for (EdgeId e : inside.edges()) {
            if (!inside.isLaminar(e)) continue;
            const Edge &edge = inside.edge(e);
            const QString error = appendProjectedCurve(sketch, edge.curve, edge.range, construction, fixed, created);
            if (!error.isEmpty()) return error;
            ++count;
        }
        if (count == 0) return QStringLiteral("Il piano dello schizzo non taglia il solido.");
    } catch (const std::exception &failure) {
        return QStringLiteral("Sezione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
    return {};
}

QString appendSketchContactReferences(SketchObject &sketch, const SketchObject &source, QVector<SketchEntity> *created) {
    try {
        const Frame3 target = sketchAxes(sketch), from = sketchAxes(source);
        const Vec3 normal = target.zDir();
        const double offset = dot(normal, target.origin());
        const double scale = std::max({1.0, norm(target.origin()), norm(from.origin())});
        const double tolerance = 1e-8 * scale;
        const bool coplanar = norm(cross(normal, from.zDir())) <= 1e-10
                           && std::fabs(dot(normal, from.origin()) - offset) <= tolerance;
        int added = 0;
        const auto fixedPoint = [&](const Vec3 &world) {
            const Vec3 local = target.toLocal(world);
            const QPointF point(local.x(), local.y());
            for (int index : sketch.constructionSegments) {
                if (index < 0 || index >= sketch.segments.size()) continue;
                const SketchSegment &segment = sketch.segments.at(index);
                const auto gap = [](const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); };
                if (gap(segment.first, segment.second) <= kSketchConnectionTolerance
                    && gap(segment.first, point) <= kSketchConnectionTolerance) return;
            }
            const int index = int(sketch.segments.size());
            appendSegment(sketch, point, point, true);
            sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{0, index, -1}}));
            if (created) created->append({0, index});
            ++added;
        };
        const auto addCurve = [&](const CurvePtr<3> &curve, const Interval &range) {
            if (coplanar) {
                QVector<SketchEntity> localCreated;
                const QString error = appendProjectedCurve(sketch, curve, range, true, true, &localCreated);
                if (!error.isEmpty()) throw std::domain_error(error.toStdString());
                if (created) *created += localCreated;
                added += localCreated.size();
                return;
            }
            const PlaneRoots<3> roots = planeRoots<3>(*curve, range, normal, offset, tolerance);
            for (const Interval &coincident : roots.coincident) {
                QVector<SketchEntity> localCreated;
                const QString error = appendProjectedCurve(sketch, curve, coincident, true, true, &localCreated);
                if (!error.isEmpty()) throw std::domain_error(error.toStdString());
                if (created) *created += localCreated;
                added += localCreated.size();
            }
            for (double parameter : roots.parameters) fixedPoint(curve->point(parameter));
        };
        for (int index = 0; index < source.segments.size(); ++index) {
            if (source.isConstructionSegment(index)) continue;
            const SketchSegment &segment = source.segments.at(index);
            const Vec3 a = from.toGlobal(Vec3(segment.first.x(), segment.first.y(), 0.0));
            const Vec3 b = from.toGlobal(Vec3(segment.second.x(), segment.second.y(), 0.0));
            const double length = distance(a, b);
            if (length <= kSketchConnectionTolerance) continue;
            addCurve(std::make_shared<Line<3>>(a, (b - a) / length), {0.0, length});
        }
        for (const CurveObject &object : source.curves) {
            if (object.construction) continue;
            for (const ProfileSegment &piece : curveGeometry(object)) addCurve(embedCurve(piece.curve, from), piece.range);
        }
        if (added == 0)
            return coplanar ? QStringLiteral("Lo schizzo non contiene entita' utilizzabili.")
                            : QStringLiteral("Lo schizzo non attraversa il piano attivo.");
        return {};
    } catch (const std::exception &failure) {
        return QStringLiteral("Contatti tra schizzi non riusciti: %1").arg(QString::fromUtf8(failure.what()));
    }
}

QString appendSketchContactReference(SketchObject &sketch, const SketchObject &source, SketchEntity entity,
                                     QVector<SketchEntity> *created) {
    SketchObject isolated = source;
    isolated.segments.clear();
    isolated.curves.clear();
    isolated.constructionSegments.clear();
    isolated.geometricConstraints.clear();
    isolated.coincidentConstraints.clear();
    isolated.constraints.clear();
    isolated.segmentLengths.clear();
    if (entity.kind == 0 && entity.index >= 0 && entity.index < source.segments.size()) {
        isolated.segments.append(source.segments.at(entity.index));
    } else if (entity.kind == 1 && entity.index >= 0 && entity.index < source.curves.size()) {
        CurveObject curve = source.curves.at(entity.index);
        curve.construction = false;
        isolated.curves.append(std::move(curve));
    } else {
        return QStringLiteral("Entita' dello schizzo sorgente non valida.");
    }
    return appendSketchContactReferences(sketch, isolated, created);
}

}

namespace ForgeCad {
QString sketchPlaneReference(const SketchObject &sketch, const SketchFrame &plane, QPointF &point, QPointF &direction) {
    const auto frame = sketchAxes(sketch);
    const Kernel::Vec3 n(plane.normal[0], plane.normal[1], plane.normal[2]);
    const Kernel::Vec3 origin(plane.origin[0], plane.origin[1], plane.origin[2]);
    const double a = Kernel::dot(n, frame.xDir()), b = Kernel::dot(n, frame.yDir());
    const double c = Kernel::dot(n, frame.origin() - origin), squared = a * a + b * b;
    if (squared < 1e-20)
        return QStringLiteral("Il piano e' parallelo o coincidente con lo schizzo: non definisce una retta di riferimento.");
    point = QPointF(-a * c / squared, -b * c / squared);
    direction = QPointF(-b, a) / std::sqrt(squared);
    return {};
}
QString sketchAxisReference(const SketchObject &sketch, const Kernel::Vec3 &origin, const Kernel::Vec3 &axis,
                            QPointF &point, QPointF &direction) {
    const auto frame = sketchAxes(sketch);
    point = worldToSketch(origin, sketch);
    direction = QPointF(Kernel::dot(axis, frame.xDir()), Kernel::dot(axis, frame.yDir()));
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-10)
        return QStringLiteral("L'asse e' normale allo schizzo: la sua proiezione e' un punto, non una retta.");
    direction /= length;
    return {};
}
int appendFixedReferenceLine(SketchObject &sketch, const QPointF &point, const QPointF &direction, double halfLength) {
    const auto cross = [](const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); };
    for (int i : sketch.constructionSegments) {
        if (i < 0 || i >= sketch.segments.size()) continue;
        const auto &s = sketch.segments.at(i);
        const QPointF d = s.second - s.first;
        if (std::fabs(cross(d, direction)) > 1e-12 * std::max(1.0, std::hypot(d.x(), d.y()))
            || std::fabs(cross(s.first - point, direction)) > 1e-12 * std::max({1.0, std::hypot(point.x(), point.y()), std::hypot(s.first.x(), s.first.y())})) continue;
        for (const auto &c : sketch.geometricConstraints)
            if (c.type == ConstraintType::Fix && c.first == ConstraintRef{0, i, -1}) return i;
    }
    const int index = int(sketch.segments.size());
    appendSegment(sketch, point - halfLength * direction, point + halfLength * direction, true);
    sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{0, index, -1}}));
    return index;
}
}
