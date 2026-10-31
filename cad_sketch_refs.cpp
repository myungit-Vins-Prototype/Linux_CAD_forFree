#include "cad_sketch_refs.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "cad_constraints.h"
#include "cad_datum.h"
#include "cad_feature_dependencies.h"
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
#include "fk_offset.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "cad_topology_ref.h"

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
bool needsProjectionFit(const Curve<3> &curve) {
    if(curve.type()==CurveType::Trimmed) return needsProjectionFit(*static_cast<const TrimmedCurve<3>&>(curve).basis());
    if(curve.type()==CurveType::Transformed) return needsProjectionFit(*static_cast<const TransformedCurve&>(curve).basis());
    return curve.type()==CurveType::Other;
}

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
                // La proiezione e' affine: poli e nodi della B-spline
                // approssimata dell'elica possono essere proiettati senza
                // scomporla e ricomporla due volte in tratti di Bezier.
                const BSplineCurve<3> full = helixBSpline(*helix, kLinearResolution);
                if (std::fabs(range.lo - full.domain().lo) <= 1e-12
                    && std::fabs(range.hi - full.domain().hi) <= 1e-12) {
                    spline = full;
                } else {
                    spline = joinBezierPieces(standardBezierPieces(full, range));
                }
            } else if (needsProjectionFit(*curve)) {
                // Le curve procedurali (ad esempio isoparametriche) non
                // hanno necessariamente una forma razionale: le approssima
                // il fitter adattivo, poi la proiezione affine dei poli.
                spline=*fitCurve([&](double t){return curve->point(t);},range,curve->breakpoints(range),kLinearResolution);
            } else {
                std::vector<BSplineCurve<3>> pieces = standardBezierPieces(*curve, range);
                int degree = 1;
                for (const BSplineCurve<3> &piece : pieces) degree = std::max(degree, piece.degree());
                bool sameDegree = true;
                for (const BSplineCurve<3> &piece : pieces) sameDegree = sameDegree && piece.degree() == degree;
                spline = joinBezierPieces(sameDegree ? pieces : standardBezierPieces(*curve, range, degree));
            }
            // Curva in un piano perpendicolare allo schizzo (i poli proiettati stanno
            // su una retta): la proiezione e' il segmento tra i punti estremi della curva lungo la retta.
            std::vector<QPointF> projected;
            for (const Vec3 &p : spline.poles()) projected.push_back(local(p));
            QPointF a = projected.front(), b = projected.front();
            double span = 0.0;
            // Due scansioni lineari bastano a scegliere una direzione:
            // non serve cercare la coppia piu' lontana fra tutti i poli.
            for (const QPointF &p : projected) {
                const double d=std::hypot(p.x()-a.x(),p.y()-a.y());
                if(d>span){span=d;b=p;}
            }
            a=b;span=0;
            for (const QPointF &p : projected) {
                const double d=std::hypot(p.x()-a.x(),p.y()-a.y());
                if(d>span){span=d;b=p;}
            }
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
    try {
        const Frame3 frame = sketchAxes(sketch);
        Box box;
        for (FaceId f : body.faces()) box.add(faceBox(body, f));
        const Vec3 middle = 0.5 * (box.lo + box.hi);
        const double half = 2.0 * std::max(1.0, box.diagonal()) + distance(frame.origin(), middle);
        if (body.isSheet()) {
            const Body plane = makePlaneSheet(frame, half);
            const auto curves = bodySectionCurves(plane, body);
            if (curves.empty()) return QStringLiteral("Il piano dello schizzo non interseca la superficie.");
            for (const auto &section : curves) {
                const QString error = appendProjectedCurve(sketch, section.curve, section.range, construction, fixed, created);
                if (!error.isEmpty()) return error;
            }
            return {};
        }
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

QString refreshSurfacePierce(const SketchObject &sketch, SketchConstraint &constraint,
                            const Body &body, const GeometryRef &surface, const QString &name) {
    auto data = std::make_shared<SketchPierceData>();
    data->surface = surface;
    data->name = name;
    const auto fail = [&](const QString &error) {
        data->error = error;
        constraint.pierce = data;
        return error;
    };
    try {
        if (surface.kind != 5) return fail(QStringLiteral("Scegli una faccia della superficie o del solido."));
        const FaceId face = resolveFaceReference(body, surface.point, 1e-6);
        if (!face.valid()) return fail(QStringLiteral("La faccia del vincolo Pierce non esiste piu'."));
        const Frame3 frame = sketchAxes(sketch);
        const Surface &geometry = *body.face(face).surface;
        if (geometry.type() == SurfaceType::Plane) {
            const Frame3 &plane = static_cast<const Plane &>(geometry).frame();
            if (norm(cross(plane.zDir(),frame.zDir())) <= 1e-12
                && std::fabs(dot(plane.origin()-frame.origin(),frame.zDir())) <= kLinearResolution)
                return fail(QStringLiteral("La faccia e' complanare allo schizzo: non definisce un'intersezione Pierce."));
        }
        SketchObject section;
        section.plane = sketch.plane;
        section.frame = sketch.frame;
        section.customFrame = sketch.customFrame;
        const Body sheet = facesAsSheet(body,{face});
        const QString error = appendSectionCurves(section,sheet,true,false);
        if (!error.isEmpty()) return fail(error);
        data->segments = std::move(section.segments);
        data->curves = std::move(section.curves);
        constraint.pierce = data;
        return {};
    } catch (const std::exception &error) {
        return fail(QStringLiteral("Pierce non riuscito: %1").arg(QString::fromUtf8(error.what())));
    }
}

QString appendProjectedSketchEntity(SketchObject &sketch, const SketchObject &source, SketchEntity entity,
                                    bool construction, QVector<SketchEntity> *created) {
    try {
        const Frame3 from = sketchAxes(source);
        std::vector<ProfileSegment> pieces;
        if (entity.kind == 0 && entity.index >= 0 && entity.index < source.segments.size()) {
            const SketchSegment &segment = source.segments.at(entity.index);
            const Vec2 a(segment.first.x(), segment.first.y()), b(segment.second.x(), segment.second.y());
            const double length = distance(a, b);
            if (length <= kSketchConnectionTolerance) return QStringLiteral("Il segmento sorgente e' un punto.");
            pieces.push_back({std::make_shared<Line<2>>(a, (b - a) / length), {0.0, length}});
        } else if (entity.kind == 1 && entity.index >= 0 && entity.index < source.curves.size()) {
            const auto geometry = curveGeometry(source.curves.at(entity.index));
            for (const auto &piece : geometry) pieces.push_back(piece);
        } else return QStringLiteral("Entita' dello schizzo sorgente non valida.");
        if (pieces.empty()) return QStringLiteral("La curva sorgente non contiene geometria utilizzabile.");
        SketchObject work = sketch;
        QVector<SketchEntity> made;
        for (const ProfileSegment &piece : pieces) {
            const QString error = appendProjectedCurve(work, embedCurve(piece.curve, from), piece.range,
                                                       construction, true, &made);
            if (!error.isEmpty()) return error;
        }
        sketch = std::move(work);
        if (created) *created += made;
        return {};
    } catch (const std::exception &failure) {
        return QStringLiteral("Proiezione dello schizzo non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
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
int sketchReferenceSourceIndex(const GeometryRef &ref, const QVector<SketchObject> &sketches,
                              const QVector<ExtrusionObject> &bodies) {
    if (isSketchRef(ref)) {
        if (ref.featureId) {
            for (int i = 0; i < sketches.size(); ++i) if (sketches[i].storyId == ref.featureId) return i;
            return -1;
        }
        return ref.index >= 0 && ref.index < sketches.size() ? ref.index : -1;
    }
    if (isBodyRef(ref)) {
        if (ref.featureId) {
            for (int i = 0; i < bodies.size(); ++i) if (bodies[i].featureId == ref.featureId) return i;
            return -1;
        }
        return ref.index >= 0 && ref.index < bodies.size() ? ref.index : -1;
    }
    return ref.index;
}

namespace {
bool sameSketchSource(const GeometryRef &a, const GeometryRef &b) {
    return a.kind == b.kind && (a.featureId || b.featureId ? a.featureId == b.featureId : a.index == b.index)
        && a.element == b.element && a.point.subshape == b.point.subshape && a.point.name == b.point.name
        && (a.kind != 9 || (a.point.x == b.point.x && a.point.y == b.point.y && a.point.z == b.point.z));
}

QString projectLinkedReference(SketchObject &target, GeometryRef ref, bool contact, bool construction, double halfLength,
                               const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    try {
    if (isSketchRef(ref) || isBodyRef(ref)) {
        ref.index = sketchReferenceSourceIndex(ref, sketches, bodies);
        if (ref.index < 0) return QStringLiteral("La sorgente del riferimento associativo e' stata eliminata.");
        if (isBodyRef(ref) && (bodies[ref.index].suppressed || !bodies[ref.index].error.isEmpty()))
            return QStringLiteral("La sorgente del riferimento associativo non e' disponibile.");
        if (isSketchRef(ref) && (!sketches[ref.index].referenceError.isEmpty() || !sketches[ref.index].supportError.isEmpty()))
            return QStringLiteral("Lo schizzo sorgente ha riferimenti non risolti.");
    }
    if (ref.kind == 11) {
        if (ref.index < 0 || !bodies[ref.index].forgeBody) return QStringLiteral("Il corpo della sezione associativa non e' disponibile.");
        return appendSectionCurves(target, *bodies[ref.index].forgeBody, construction, false);
    }
    ResolvedRef resolved;
    QString error;
    if (!resolveGeometryRef(ref, bodies.size(), sketches, bodies, resolved, &error))
        return error.isEmpty() ? QStringLiteral("Riferimento associativo non risolto.") : error;
    if (ref.kind == 5 && contact) {
        if (ref.index < 0 || !bodies[ref.index].forgeBody || resolved.face < 0)
            return QStringLiteral("La faccia della sezione associativa non e' disponibile.");
        const Body sheet = facesAsSheet(*bodies[ref.index].forgeBody, {FaceId(resolved.face)});
        return appendSectionCurves(target, sheet, construction, false);
    }
    const auto appendPoint = [&](const Vec3 &point) {
        const auto p = worldToSketch(point, target);
        for (const auto &segment : target.segments)
            if (segment.first == segment.second && std::hypot(p.x()-segment.first.x(),p.y()-segment.first.y()) <= kSketchConnectionTolerance) return;
        appendSegment(target, p, p, construction);
    };
    if (ref.kind == 0 || ref.kind == 3 || ref.kind == 6 || ref.kind == 10) {
        if (!resolved.hasPoint) return QStringLiteral("Il riferimento non definisce un punto.");
        appendPoint(resolved.point); return {};
    }
    if (ref.kind == 1 || ref.kind == 8 || ref.kind == 2 || (ref.kind == 5 && resolved.hasPlane)) {
        QPointF point, direction;
        if (resolved.hasPlane) {
            SketchFrame plane;
            for (int i = 0; i < 3; ++i) { plane.origin[i] = resolved.point[i]; plane.normal[i] = resolved.direction[i]; }
            error = sketchPlaneReference(target, plane, point, direction);
        } else if (resolved.hasLine) {
            error = sketchAxisReference(target, resolved.point, resolved.direction, point, direction);
            if (!error.isEmpty()) { appendPoint(resolved.point); return {}; }
        } else return QStringLiteral("Il riferimento non definisce un piano o un asse.");
        if (!error.isEmpty()) return error;
        appendSegment(target, point - halfLength * direction, point + halfLength * direction, construction);
        return {};
    }
    std::vector<PathSegment> path;
    if (!geometryRefPath(ref, bodies.size(), sketches, bodies, path, &error)) return error;
    const Frame3 frame = sketchAxes(target);
    for (const auto &piece : path) {
        if (contact) {
            const auto roots = planeRoots<3>(*piece.curve, piece.range, frame.zDir(), dot(frame.zDir(), frame.origin()), 1e-9);
            for (double t : roots.parameters) appendPoint(piece.curve->point(t));
        } else {
            error = appendProjectedCurve(target, piece.curve, piece.range, construction, false);
            if (!error.isEmpty()) {
                // Una retta normale al piano si proietta in un punto esatto.
                const Vec3 a = piece.curve->point(piece.range.lo), b = piece.curve->point(piece.range.hi);
                if (basisOf(piece.curve.get())->type() == CurveType::Line
                    && norm(cross(b - a, frame.zDir())) <= 1e-10 * std::max(1.0, norm(b - a))) appendPoint(a);
                else return error;
            }
        }
    }
    if (target.segments.isEmpty() && target.curves.isEmpty())
        return QStringLiteral("La curva non attraversa il piano dello schizzo.");
    return {};
    } catch (const std::exception &error) {
        return QStringLiteral("Riferimento associativo non risolto: %1").arg(QString::fromUtf8(error.what()));
    }
}
}

QString appendLinkedSketchReference(SketchObject &sketch, GeometryRef source,
                                   const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
                                   bool contact, bool construction, double halfLength, QVector<SketchEntity> *created) {
    if (isSketchRef(source) || isBodyRef(source)) {
        source.index = sketchReferenceSourceIndex(source, sketches, bodies);
        if (source.index < 0) return QStringLiteral("Sorgente del riferimento mancante.");
        source.featureId = isSketchRef(source) ? sketches[source.index].storyId : bodies[source.index].featureId;
    }
    for (const auto &link : sketch.linkedReferences)
        if (link && link->contact == contact && sameSketchSource(link->source, source)) {
            if (created) for (const auto &entity : link->entities) created->append({entity.kind, entity.element});
            return link->error;
        }
    SketchObject projected;
    projected.plane = sketch.plane; projected.frame = sketch.frame; projected.customFrame = sketch.customFrame;
    const QString error = projectLinkedReference(projected, source, contact, construction, halfLength, sketches, bodies);
    if (!error.isEmpty()) return error;
    if ((source.kind == 1 || source.kind == 2) && projected.segments.size() == 1 && projected.curves.isEmpty()) {
        const auto &next = projected.segments.front();
        const QPointF d = next.second - next.first;
        for (const auto &link : sketch.linkedReferences) if (link && (link->source.kind == 1 || link->source.kind == 2))
            for (const auto &entity : link->entities) if (entity.kind == 0) {
                const auto &old = sketch.segments[entity.element];
                const QPointF e = old.second - old.first, gap = old.first - next.first;
                const auto cross2 = [](const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); };
                if (std::hypot(d.x(), d.y()) > 1e-10 && std::hypot(e.x(), e.y()) > 1e-10
                    && std::abs(cross2(d, e)) <= 1e-12 * std::hypot(d.x(), d.y()) * std::hypot(e.x(), e.y())
                    && std::abs(cross2(d, gap)) <= 1e-10 * std::hypot(d.x(), d.y())) {
                    if (created) created->append({0, entity.element});
                    return {};
                }
            }
    }
    auto data = std::make_shared<SketchLinkedReference>();
    data->source = source; data->contact = contact; data->construction = construction; data->halfLength = halfLength;
    for (const auto &segment : projected.segments) {
        const int index = sketch.segments.size();
        appendSegment(sketch, segment.first, segment.second, construction);
        data->entities.append({0, index, -1});
        if (created) created->append({0, index});
    }
    for (const auto &curve : projected.curves) {
        const int index = sketch.curves.size(); sketch.curves.append(curve);
        data->entities.append({1, index, -1});
        if (created) created->append({1, index});
    }
    sketch.linkedReferences.append(data);
    return {};
}

QString refreshLinkedSketchReferences(SketchObject &sketch, const QVector<SketchObject> &sketches,
                                     const QVector<ExtrusionObject> &bodies) {
    sketch.referenceError.clear();
    for (auto &link : sketch.linkedReferences) {
        if (!link) { sketch.referenceError = QStringLiteral("Riferimento associativo mancante."); continue; }
        auto data = std::make_shared<SketchLinkedReference>(*link);
        SketchObject projected;
        projected.plane = sketch.plane; projected.frame = sketch.frame; projected.customFrame = sketch.customFrame;
        data->error = projectLinkedReference(projected, data->source, data->contact, data->construction, data->halfLength, sketches, bodies);
        int s = 0, c = 0;
        for (const auto &entity : data->entities) {
            if (entity.kind == 0) {
                if (entity.element < 0 || entity.element >= sketch.segments.size() || s >= projected.segments.size()) break;
                ++s;
            } else {
                if (entity.element < 0 || entity.element >= sketch.curves.size() || c >= projected.curves.size()) break;
                const auto &old = sketch.curves[entity.element], &next = projected.curves[c];
                if (old.tool != next.tool || old.controlPoints.size() != next.controlPoints.size()) break;
                ++c;
            }
        }
        if (data->error.isEmpty() && (s + c != data->entities.size() || s != projected.segments.size() || c != projected.curves.size()))
            data->error = QStringLiteral("La topologia del riferimento e' cambiata: selezionare nuovamente la sorgente.");
        if (data->error.isEmpty()) {
            s = c = 0;
            for (const auto &entity : data->entities) {
                if (entity.kind == 0) sketch.segments[entity.element] = projected.segments[s++];
                else sketch.curves[entity.element] = projected.curves[c++];
            }
            data->source.index = sketchReferenceSourceIndex(data->source, sketches, bodies);
        } else sketch.referenceError = data->error;
        link = std::move(data);
    }
    return sketch.referenceError;
}

}
