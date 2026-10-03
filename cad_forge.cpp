#include "cad_forge.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>

#include <QRegularExpression>

#include "cad_cuda_tessellation.h"
#include "cad_curve_solver.h"
#include "cad_kernel.h"
#include "cad_topology_ref.h"
#include "fk_blend.h"
#include "fk_boolean.h"
#include "fk_bspline.h"
#include "fk_classify.h"
#include "fk_extrude.h"
#include "fk_curve_algo.h"
#include "fk_loft.h"
#include "fk_sweep.h"
#include "fk_intersect.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"
#include "fk_mass.h"
#include "fk_offset.h"
#include "fk_planar.h"
#include "fk_sew.h"
#include "fk_boundary.h"
#include "fk_shell.h"
#include "fk_step.h"
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

// Segmenti e curve dello schizzo (curveGeometry di cad_curve_solver), senza costruzione.
std::vector<ProfileSegment> forgeSketchSegments(const SketchObject &sketch) {
    constexpr double confusion = 1.0e-7;
    std::vector<ProfileSegment> result;
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index)) continue;
        const SketchSegment &segment = sketch.segments.at(index);
        const Vec2 a = toVec(segment.first), b = toVec(segment.second);
        if (distance(a, b) > confusion) result.push_back(lineSegment(a, b));
    }
    for (const CurveObject &curve : sketch.curves) {
        if (curve.construction) continue;
        for (ProfileSegment &piece : curveGeometry(curve)) result.push_back(std::move(piece));
    }
    return result;
}

void forgeSketchFrame(const SketchObject &sketch, double distance, Frame3 &frame, double &height) {
    frame = sketchAxes(sketch);
    height = dot(extrusionVector(sketch, distance), frame.zDir());
}

ForgeBody forgeExtrusion(const SketchObject &sketch, double distance, QString *error, double start) {
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
        if (start != 0.0) frame = Frame3(frame.origin() + extrusionVector(sketch, start), frame.zDir(), frame.xDir());
        // Nessun contorno chiuso: le catene aperte diventano una superficie
        // (lamina).
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
        if (profile.regions.empty() && profile.chains.empty()) {
            setError(error, QStringLiteral("Lo schizzo non ha un profilo da far ruotare."));
            return nullptr;
        }
        const Frame3 axes = sketchAxes(sketch);
        const Vec3 xs = axes.xDir(), ys = axes.yDir();
        const Frame3 frame(sketchToWorld(point, sketch), axisDirection.x() * xs + axisDirection.y() * ys,
                           normal.x() * xs + normal.y() * ys);
        // Nessun contorno chiuso: le catene aperte diventano una superficie
        // (lamina), come nell'estrusione.
        if (profile.regions.empty()) return std::make_shared<const Body>(makeSheetRevolution(frame, profile.chains, angle));
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
    const Frame3 frame = primitiveAxes(parameters);
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

ForgeBody forgeBlend(const ForgeBody &base, const QVector<EdgePoint> &points, double size, bool chamfer, QString *error, const ChamferSpec &spec) {
    if (!base) {
        setError(error, QStringLiteral("Il corpo da raccordare non ha geometria valida."));
        return nullptr;
    }
    if (points.isEmpty()) {
        setError(error, QStringLiteral("Nessuno spigolo scelto."));
        return nullptr;
    }
    try {
        Box box;
        for (VertexId v : base->vertices()) box.add(base->vertex(v).point);
        for (FaceId f : base->faces()) box.add(faceBox(*base, f));
        const double reach = 1e-3 * std::max(1.0, box.diagonal());
        std::vector<EdgeId> edges;
        for (const EdgePoint &point : points) {
            const EdgeId e = resolveEdgeReference(*base, point, reach);
            if (!e.valid()) {
                setError(error, QStringLiteral("Uno degli spigoli scelti non esiste piu' nel corpo."));
                return nullptr;
            }
            if (std::find(edges.begin(), edges.end(), e) == edges.end()) edges.push_back(e);
        }
        if (!chamfer || spec.mode == 0) return std::make_shared<const Body>(blendEdges(*base, edges, size, chamfer));
        // Smusso asimmetrico: le distanze di ogni spigolo con la stessa regola di buildBlend.
        std::vector<ChamferSides> sides;
        for (EdgeId e : edges) {
            const Edge &edge = base->edge(e);
            const double t = 0.5 * (edge.range.lo + edge.range.hi);
            const Vec3 p = edge.curve->point(t);
            double normals[2][3];
            Vec3 outward[2];
            int k = 0;
            for (FinId fin : {edge.forward, edge.backward}) {
                const Face &face = base->face(base->finFace(fin));
                const SurfaceProjection projection = projectPoint(*face.surface, p);
                const Vec3 n = normalAt(*face.surface, projection.u, projection.v);
                outward[k] = face.sense ? n : -n;
                normals[k][0] = outward[k].x();
                normals[k][1] = outward[k].y();
                normals[k][2] = outward[k].z();
                ++k;
            }
            bool firstIsReference = true;
            double onReference = 0.0, onOther = 0.0;
            if (!chamferDistances(spec, size, normals[0], normals[1], firstIsReference, onReference, onOther, error)) return nullptr;
            sides.push_back({outward[firstIsReference ? 0 : 1], onReference, onOther});
        }
        return std::make_shared<const Body>(chamferEdges(*base, edges, sides));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("%1 non riuscito: %2").arg(chamfer ? QStringLiteral("Smusso") : QStringLiteral("Raccordo"),
                                                               QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

namespace {

// Lo strumento del taglio: il corpo, o una lamina piana grande sul piano di riferimento.
Body trimTool(const Body &sheet, const ForgeBody &tool, int plane) {
    if (tool) return *tool;
    Box box;
    for (VertexId v : sheet.vertices()) box.add(sheet.vertex(v).point);
    for (FaceId f : sheet.faces()) box.add(faceBox(sheet, f));
    const double reach = 10.0 * (box.diagonal() + norm(0.5 * (box.lo + box.hi))) + 10.0;
    return makePlaneSheet(sketchAxes(plane), reach);
}

}

ForgeBody forgeTrimSheet(const ForgeBody &sheet, const ForgeBody &tool, int plane, const EdgePoint &keep, QString *error) {
    if (!sheet || !sheet->isSheet()) {
        setError(error, QStringLiteral("Si tagliano solo le superfici (estrusioni di profili aperti)."));
        return nullptr;
    }
    try {
        return std::make_shared<const Body>(trimSheet(*sheet, trimTool(*sheet, tool, plane), Vec3(keep.x, keep.y, keep.z)));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Taglio non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

QVector<SheetPiece> forgeSheetPieces(const ForgeBody &sheet, const ForgeBody &tool, int plane, QString *error) {
    QVector<SheetPiece> result;
    if (!sheet || !sheet->isSheet()) {
        setError(error, QStringLiteral("Si tagliano solo le superfici (estrusioni di profili aperti)."));
        return result;
    }
    try {
        for (const Body &piece : splitSheet(*sheet, trimTool(*sheet, tool, plane))) {
            // Un punto della parte: il baricentro del triangolo piu' grande della sua tassellazione.
            TessellationOptions options;
            options.deflection = 1e-2;
            const Tessellation mesh = tessellate(piece, options);
            double largest = -1.0;
            SheetPiece info;
            for (const FaceMesh &face : mesh.faces)
                for (const auto &t : face.triangles) {
                    const Vec3 &a = face.points[std::size_t(t[0])], &b = face.points[std::size_t(t[1])], &c = face.points[std::size_t(t[2])];
                    const double area = norm(cross(b - a, c - a));
                    if (area > largest) {
                        largest = area;
                        const Vec3 m = (a + b + c) / 3.0;
                        info.point = {m.x(), m.y(), m.z()};
                    }
                }
            for (FaceId f : piece.faces()) info.area += faceArea(piece, f, 1e-9);
            result.append(info);
        }
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Taglio non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        result.clear();
    }
    return result;
}

ForgeBody forgeScale(const ForgeBody &base, double factor, int mode, const EdgePoint &point, QString *error) {
    if (!base) {
        setError(error, QStringLiteral("Il corpo da scalare non ha geometria valida."));
        return nullptr;
    }
    try {
        Vec3 center(point.x, point.y, point.z);
        if (mode == 0) center = Vec3();
        if (mode == 1) {
            if (base->isSheet()) {
                setError(error, QStringLiteral("Il baricentro vale per i solidi: per una superficie scegli l'origine o un punto."));
                return nullptr;
            }
            center = massProperties(*base).centroid;
        }
        return std::make_shared<const Body>(scaleBody(*base, center, factor));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Scala non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeExtendSheet(const ForgeBody &sheet, const QVector<EdgePoint> &points, double distance, bool linear, QString *error) {
    if (!sheet || !sheet->isSheet()) {
        setError(error, QStringLiteral("Si estendono solo le superfici (estrusioni di profili aperti)."));
        return nullptr;
    }
    if (points.isEmpty()) {
        setError(error, QStringLiteral("Nessun bordo scelto."));
        return nullptr;
    }
    try {
        Box box;
        for (VertexId v : sheet->vertices()) box.add(sheet->vertex(v).point);
        const double reach = 1e-3 * std::max(1.0, box.diagonal());
        std::vector<EdgeId> edges;
        for (const EdgePoint &point : points) {
            const EdgeId e = resolveEdgeReference(*sheet, point, reach);
            if (!e.valid()) {
                setError(error, QStringLiteral("Uno dei bordi scelti non esiste piu' nella superficie."));
                return nullptr;
            }
            if (std::find(edges.begin(), edges.end(), e) == edges.end()) edges.push_back(e);
        }
        return std::make_shared<const Body>(extendSheet(*sheet, edges, distance, linear));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Estensione non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeShell(const ForgeBody &base, const QVector<EdgePoint> &points, double thickness, QString *error) {
    if (!base || base->isSheet()) {
        setError(error, QStringLiteral("Il guscio richiede un solido."));
        return nullptr;
    }
    if (!(thickness > 0.0)) {
        setError(error, QStringLiteral("Lo spessore del guscio deve essere maggiore di zero."));
        return nullptr;
    }
    try {
        Box box;
        for (VertexId v : base->vertices()) box.add(base->vertex(v).point);
        const double reach = 1e-3 * std::max(1.0, box.diagonal());
        std::vector<FaceId> removed;
        for (const EdgePoint &point : points) {
            const FaceId f = resolveFaceReference(*base, point, reach);
            if (!f.valid()) {
                setError(error, QStringLiteral("Una delle facce dell'apertura non esiste piu' nel corpo."));
                return nullptr;
            }
            if (std::find(removed.begin(), removed.end(), f) == removed.end()) removed.push_back(f);
        }
        return std::make_shared<const Body>(shellBody(*base, removed, thickness));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Guscio non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeDeleteFaces(const ForgeBody &base, const QVector<EdgePoint> &points, QString *error) {
    if (!base) {
        setError(error, QStringLiteral("Il corpo di partenza non ha geometria."));
        return nullptr;
    }
    if (points.isEmpty()) {
        setError(error, QStringLiteral("Nessuna faccia da eliminare."));
        return nullptr;
    }
    try {
        Box box;
        for (VertexId v : base->vertices()) box.add(base->vertex(v).point);
        const double reach = 1e-3 * std::max(1.0, box.diagonal());
        std::vector<FaceId> removed;
        for (const EdgePoint &point : points) {
            const FaceId f = resolveFaceReference(*base, point, reach);
            if (!f.valid()) {
                setError(error, QStringLiteral("Una delle facce scelte non esiste piu' nel corpo."));
                return nullptr;
            }
            removed.push_back(f);
        }
        std::vector<FaceId> kept;
        for (FaceId f : base->faces())
            if (std::find(removed.begin(), removed.end(), f) == removed.end()) kept.push_back(f);
        if (kept.empty()) {
            setError(error, QStringLiteral("Non resta nessuna faccia: elimina il corpo invece delle facce."));
            return nullptr;
        }
        return std::make_shared<const Body>(facesAsSheet(*base, kept));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Eliminazione delle facce non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeOffsetFaces(const ForgeBody &base, const QVector<EdgePoint> &points, double distance, QString *error, QString *summary) {
    if (!base) {
        setError(error, QStringLiteral("Il corpo di partenza non ha geometria."));
        return nullptr;
    }
    if (!(std::fabs(distance) > 0.0)) {
        setError(error, QStringLiteral("La distanza dell'offset deve essere diversa da zero."));
        return nullptr;
    }
    try {
        std::vector<FaceId> faces;
        if (points.isEmpty()) {
            faces = base->faces();
        } else {
            Box box;
            for (VertexId v : base->vertices()) box.add(base->vertex(v).point);
            const double reach = 1e-3 * std::max(1.0, box.diagonal());
            for (const EdgePoint &point : points) {
                const FaceId f = resolveFaceReference(*base, point, reach);
                if (!f.valid()) {
                    setError(error, QStringLiteral("Una delle facce scelte non esiste piu' nel corpo."));
                    return nullptr;
                }
                if (std::find(faces.begin(), faces.end(), f) == faces.end()) faces.push_back(f);
            }
        }
        const OffsetResult result = offsetFaces(*base, faces, distance);
        if (summary) {
            *summary = result.shells == 1 ? QStringLiteral("una superficie cucita")
                                          : QStringLiteral("%1 superfici separate").arg(result.shells);
            if (result.sharpEdges > 0)
                *summary += QStringLiteral(" (%1 spigoli vivi tra le facce: li' le superfici a distanza si staccano)").arg(result.sharpEdges);
        }
        return std::make_shared<const Body>(result.body);
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Offset non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeSew(const QVector<ForgeBody> &sheets, double tolerance, bool solid, QString *error, QString *summary) {
    std::vector<const Body *> bodies;
    for (const ForgeBody &sheet : sheets) {
        if (!sheet) {
            setError(error, QStringLiteral("Una delle superfici non ha geometria."));
            return nullptr;
        }
        bodies.push_back(sheet.get());
    }
    if (bodies.empty()) {
        setError(error, QStringLiteral("Scegli le superfici da cucire."));
        return nullptr;
    }
    try {
        const SewResult result = sewSheets(bodies, tolerance, solid);
        if (solid && !result.closed) {
            setError(error, QStringLiteral("Le superfici non chiudono un volume: restano %1 bordi liberi (prova una tolleranza "
                                           "maggiore, o togli \"Crea un solido\" per una superficie cucita).").arg(result.freeEdges));
            return nullptr;
        }
        if (summary) {
            *summary = result.solid ? QStringLiteral("solido chiuso")
                                    : result.closed ? QStringLiteral("superficie chiusa")
                                                    : QStringLiteral("superficie con %1 bordi liberi").arg(result.freeEdges);
            if (result.shells > 1) *summary += QStringLiteral(", %1 parti non collegate").arg(result.shells);
        }
        return std::make_shared<const Body>(result.body);
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Cucitura non riuscita: %1").arg(QString::fromUtf8(failure.what())));
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

ForgeBody forgeImported(const QByteArray &data, QString *error) {
    try {
        StepReadResult read = readStep(data.toStdString());
        if (read.bodies.empty()) {
            setError(error, QStringLiteral("Il corpo importato non contiene geometria."));
            return nullptr;
        }
        return std::make_shared<const Body>(std::move(read.bodies.front().body));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Il corpo importato non si legge (%1).").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

bool forgeHelixBase(const Body &body, int source, const EdgePoint &point, HelixBase &base, QString *error) {
    base = HelixBase();
    try {
        // Estensione lungo l'asse (dall'origine `from`) dei vertici e dei campioni degli edge della faccia.
        const auto axialRange = [&](FaceId f, const Vec3 &from, const Vec3 &axis, double &lo, double &hi) {
            lo = 1e300, hi = -1e300;
            for (LoopId l : body.face(f).loops)
                for (FinId fin : body.loopFins(l)) {
                    const Edge &e = body.edge(body.fin(fin).edge);
                    for (int i = 0; i <= 16; ++i) {
                        const double z = dot(e.curve->point(e.range.lo + e.range.length() * i / 16.0) - from, axis);
                        lo = std::min(lo, z), hi = std::max(hi, z);
                    }
                }
        };
        const auto circularAxis = [](const Surface &s, Vec3 &origin, Vec3 &axis) {
            if (s.type() == SurfaceType::Cylinder) {
                const Frame3 &f = static_cast<const CylindricalSurface &>(s).frame();
                origin = f.origin(), axis = f.zDir();
                return true;
            }
            if (s.type() == SurfaceType::Cone) {
                const Frame3 &f = static_cast<const ConicalSurface &>(s).frame();
                origin = f.origin(), axis = f.zDir();
                return true;
            }
            return false;
        };
        if (source == 1) {
            const EdgeId best = resolveEdgeReference(body, point, 1e300);
            if (!best.valid()) throw std::domain_error("nessuno spigolo");
            const Curve<3> *curve = body.edge(best).curve.get();
            while (curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<3> *>(curve)->basis().get();
            if (curve->type() != CurveType::Circle) {
                setError(error, QStringLiteral("Lo spigolo scelto non e' circolare."));
                return false;
            }
            const auto &circle = static_cast<const Circle<3> &>(*curve);
            base.origin = circle.center();
            base.axis = normalized(cross(circle.xAxis(), circle.yAxis()));
            base.radius = circle.radius();
            for (FinId fin : {body.edge(best).forward, body.edge(best).backward}) {
                if (!fin.valid()) continue;
                const FaceId f = body.finFace(fin);
                Vec3 origin, axis;
                if (!circularAxis(*body.face(f).surface, origin, axis)) continue;
                const Vec3 offset = base.origin - origin;
                if (norm(cross(axis, base.axis)) > 1e-9 || norm(offset - dot(offset, axis) * axis) > 1e-6) continue;
                double lo, hi;
                axialRange(f, base.origin, base.axis, lo, hi);
                if (std::fabs(lo) > std::fabs(hi)) base.axis = -base.axis, std::swap(lo, hi), lo = -lo, hi = -hi;
                base.length = std::fabs(hi) > std::fabs(lo) ? std::fabs(hi) : std::fabs(lo);
                break;
            }
        } else {
            Box box;
            for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
            const double tolerance = 1e-6 * std::max(1.0, box.diagonal());
            const FaceId best = resolveFaceReference(body, point, 1e3 * tolerance);
            if (!best.valid()) throw std::domain_error("il punto non sta su una faccia del corpo");
            const Surface &surface = *body.face(best).surface;
            Vec3 origin, axis;
            if (!circularAxis(surface, origin, axis)) {
                setError(error, QStringLiteral("La faccia scelta non e' cilindrica ne' conica."));
                return false;
            }
            double lo, hi;
            axialRange(best, origin, axis, lo, hi);
            base.origin = origin + lo * axis;
            base.axis = axis;
            base.length = hi - lo;
            if (surface.type() == SurfaceType::Cylinder) {
                base.radius = static_cast<const CylindricalSurface &>(surface).radius();
            } else {
                const auto &cone = static_cast<const ConicalSurface &>(surface);
                // Sul cono del kernel il raggio alla quota z lungo l'asse e' R + z tan(a).
                const double a = cone.semiAngle();
                base.radius = cone.referenceRadius() + lo * std::tan(a);
                base.hasTaper = true;
                base.taper = a;
                if (base.radius < 1e-9 && base.length > 0.0) {
                    base.origin = origin + hi * axis;
                    base.radius = cone.referenceRadius() + hi * std::tan(a);
                    base.axis = -axis;
                    base.taper = -a;
                }
            }
        }
        base.xRef = helixReference(base.axis);
        return true;
    } catch (const std::exception &failure) {
        setError(error, QString::fromUtf8(failure.what()));
        return false;
    }
}

ForgeBody forgeSweep(const SketchObject &profileSketch, const std::vector<PathSegment> &path, int mode, QString *error, bool surface) {
    try {
        const std::vector<ProfileSegment> segments = forgeSketchSegments(profileSketch);
        if (segments.empty()) {
            setError(error, QStringLiteral("Il profilo dello sweep non contiene geometria."));
            return nullptr;
        }
        const Profile profile = buildProfile(segments, kSketchConnectionTolerance);
        Frame3 frame;
        double height;
        forgeSketchFrame(profileSketch, 0.0, frame, height);
        const SweepOrientation orientation = mode == 1 ? SweepOrientation::Frenet : mode == 2 ? SweepOrientation::Fixed : SweepOrientation::MinimalTwist;
        if (surface) {
            // Lamina: i contorni (anche i fori) diventano tubi, le catene superfici.
            std::vector<ProfileLoop> loops;
            for (const ProfileRegion &region : profile.regions) {
                loops.push_back(region.outer);
                for (const ProfileLoop &hole : region.holes) loops.push_back(hole);
            }
            for (const ProfileLoop &chain : profile.chains) loops.push_back(chain);
            return std::make_shared<const Body>(sweepSheet(frame, loops, path, orientation));
        }
        if (!profile.regions.empty()) return std::make_shared<const Body>(sweepRegions(frame, profile.regions, path, orientation));
        return std::make_shared<const Body>(sweepChains(frame, profile.chains, path, orientation));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Sweep non riuscito: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeLoft(const QVector<SketchObject> &sketches, const QVector<SketchObject> &guideSketches, bool ruled,
                    int startContinuity, int endContinuity, int guideContinuity, double guideInfluence, double startInfluence,
                    double endInfluence, QString *error, bool surface) {
    if (sketches.size() < 2) {
        setError(error, QStringLiteral("Il loft richiede almeno due sezioni."));
        return nullptr;
    }
    try {
        std::vector<LoftSection> sections;
        int closedCount = 0;
        for (const SketchObject &sketch : sketches) {
            const Profile profile = buildProfile(forgeSketchSegments(sketch), kSketchConnectionTolerance);
            LoftSection section;
            double height;
            forgeSketchFrame(sketch, 0.0, section.frame, height);
            if (profile.regions.size() == 1 && profile.regions.front().holes.empty() && profile.chains.empty()) {
                section.loop = profile.regions.front().outer;
                ++closedCount;
            } else if (profile.regions.empty() && profile.chains.size() == 1) {
                section.loop = profile.chains.front();
            } else {
                setError(error, QStringLiteral("Ogni sezione del loft (\"%1\") deve avere un solo contorno chiuso senza fori, o una sola catena.").arg(sketch.name));
                return nullptr;
            }
            sections.push_back(std::move(section));
        }
        if (closedCount != 0 && closedCount != int(sections.size())) {
            setError(error, QStringLiteral("Le sezioni del loft devono essere tutte chiuse o tutte aperte."));
            return nullptr;
        }
        LoftOptions options;
        options.ruled = ruled;
        options.startContinuity = startContinuity;
        options.endContinuity = endContinuity;
        options.guideContinuity = guideContinuity;
        options.guideInfluence = guideInfluence;
        options.startInfluence = startInfluence;
        options.endInfluence = endInfluence;
        for (const SketchObject &guide : guideSketches) {
            std::vector<PathSegment> path;
            QString pathError;
            if (!sketchPath(guide, path, &pathError)) {
                setError(error, QStringLiteral("Curva guida \"%1\" non valida: %2").arg(guide.name, pathError));
                return nullptr;
            }
            if (distance(path.front().curve->point(path.front().range.lo), path.back().curve->point(path.back().range.hi))
                <= kSketchConnectionTolerance) {
                setError(error, QStringLiteral("La curva guida \"%1\" deve essere una catena aperta.").arg(guide.name));
                return nullptr;
            }
            options.guides.push_back(std::move(path));
        }
        return std::make_shared<const Body>(closedCount > 0 && !surface ? loftSolid(sections, options) : loftSheet(sections, options));
    } catch (const std::exception &failure) {
        QString detail = QString::fromUtf8(failure.what());
        const auto marker = [&](const QString &name) {
            const QString prefix = QStringLiteral("[[") + name + QStringLiteral("=");
            const int begin = detail.indexOf(prefix);
            if (begin < 0) return -1;
            const int end = detail.indexOf(QStringLiteral("]]"), begin);
            bool ok = false;
            const int value = end > begin ? detail.mid(begin + prefix.size(), end - begin - prefix.size()).toInt(&ok) : -1;
            return ok ? value : -1;
        };
        const int section = marker(QStringLiteral("loft-section"));
        const int guide = marker(QStringLiteral("loft-guide"));
        const QString sectionMarker = section >= 0 ? QStringLiteral(" [[loft-section=%1]]").arg(section) : QString();
        detail.remove(QRegularExpression(QStringLiteral("\\s*\\[\\[loft-(section|guide)=\\d+\\]\\]")));
        if (section >= 0 && section < sketches.size()) {
            const QString sectionName = sketches.at(section).name;
            const QString guideName = guide >= 0 && guide < guideSketches.size() ? guideSketches.at(guide).name : QStringLiteral("Guida %1").arg(guide + 1);
            if (detail.contains(QStringLiteral("non incontra una sezione")))
                detail = QStringLiteral("La curva guida \"%1\" non incontra la sezione \"%2\".%3").arg(guideName, sectionName, sectionMarker);
            else
                detail = QStringLiteral("%1 — sezione \"%2\".%3").arg(detail, sectionName, sectionMarker);
        }
        setError(error, QStringLiteral("Loft non riuscito: %1").arg(detail));
        return nullptr;
    }
}

ForgeBody forgeRuledSurface(const std::vector<PathSegment> &first, const std::vector<PathSegment> &second, QString *error) {
    if (first.empty() || second.empty()) {
        setError(error, QStringLiteral("Scegli le due curve della superficie rigata."));
        return nullptr;
    }
    try {
        return std::make_shared<const Body>(ruledSurface(first, second));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Superficie rigata non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgeBoundarySurface(const std::vector<PathSegment> &segments, QString *error) {
    if (segments.empty()) {
        setError(error, QStringLiteral("Scegli le curve del contorno."));
        return nullptr;
    }
    try {
        return std::make_shared<const Body>(boundarySheet(segments));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Superficie tra curve non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgePlanarSketch(const SketchObject &sketch, QString *error) {
    try {
        const std::vector<ProfileSegment> segments = forgeSketchSegments(sketch);
        const Profile profile = buildProfile(segments, kSketchConnectionTolerance);
        if (profile.regions.empty()) {
            setError(error, QStringLiteral("Lo schizzo \"%1\" non ha contorni chiusi.").arg(sketch.name));
            return nullptr;
        }
        Frame3 frame;
        double height;
        forgeSketchFrame(sketch, 0.0, frame, height);
        return std::make_shared<const Body>(planarSheet(frame, profile.regions));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Superficie planare non riuscita: %1").arg(QString::fromUtf8(failure.what())));
        return nullptr;
    }
}

ForgeBody forgePlanarCurves(const std::vector<PathSegment> &segments, QString *error) {
    if (segments.empty()) {
        setError(error, QStringLiteral("Scegli i bordi della superficie planare."));
        return nullptr;
    }
    try {
        // Contorni: tratti collegati per gli estremi (unione per componenti).
        Box box;
        std::vector<Vec3> ends;
        for (const PathSegment &segment : segments) {
            ends.push_back(segment.curve->point(segment.range.lo));
            ends.push_back(segment.curve->point(segment.range.hi));
            box.add(ends[ends.size() - 2]);
            box.add(ends.back());
        }
        const double tolerance = kSketchConnectionTolerance * std::max(1.0, box.diagonal());
        std::vector<int> parent(segments.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = int(i);
        const std::function<int(int)> root = [&](int i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
        for (size_t i = 0; i < segments.size(); ++i)
            for (size_t j = i + 1; j < segments.size(); ++j)
                for (int a = 0; a < 2; ++a)
                    for (int b = 0; b < 2; ++b)
                        if (distance(ends[2 * i + a], ends[2 * j + b]) <= tolerance) parent[root(int(i))] = root(int(j));
        std::map<int, std::vector<PathSegment>> groups;
        for (size_t i = 0; i < segments.size(); ++i) groups[root(int(i))].push_back(segments[i]);
        std::vector<std::vector<PathSegment>> loops;
        for (auto &group : groups) loops.push_back(std::move(group.second));
        return std::make_shared<const Body>(planarSheet(loops, tolerance));
    } catch (const std::exception &failure) {
        setError(error, QStringLiteral("Superficie planare non riuscita: %1").arg(QString::fromUtf8(failure.what())));
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
    display.rayIndex = std::make_shared<RayFaceIndex>(body);
    box.add(display.rayIndex->bounds);
    if (box.isEmpty()) return;
    const double diagonal = std::max(box.diagonal(), 1e-9);
    TessellationOptions options;
    options.deflection = diagonal * (quality <= 0 ? 4.0e-3 : quality == 1 ? 1.0e-3 : 2.0e-4);
    options.angle = quality <= 0 ? 0.5 : quality == 1 ? 0.25 : 0.1;
    const std::unique_ptr<SurfaceBatchEvaluator> accelerator = makeTessellationAccelerator();
    options.accelerator = accelerator.get();
    const Tessellation mesh = tessellate(body, options);
    for (const FaceMesh &face : mesh.faces)
        for (const std::array<int, 3> &triangle : face.triangles)
            for (int index : triangle) {
                display.vertices.append(toDisplay(face.points[std::size_t(index)]));
                display.normals.append(toDisplay(face.normals[std::size_t(index)]));
            }
    std::map<int, int> edgeDisplay;
    for (std::size_t e = 0; e < mesh.edges.size(); ++e) {
        const std::vector<Vec3> &edge = mesh.edges[e];
        QVector<QVector3D> polyline;
        for (const Vec3 &point : edge) polyline.append(toDisplay(point));
        if (polyline.size() >= 2) {
            edgeDisplay[mesh.edgeIds[e].index] = int(display.edges.size());
            display.edges.append(polyline);
            display.edgeIds.append(mesh.edgeIds[e].index);
        }
    }
    for (FaceId f : body.faces()) {
        if (display.faceEdges.size() <= f.index) display.faceEdges.resize(f.index + 1);
        auto &indices = display.faceEdges[f.index];
        for (LoopId l : body.face(f).loops)
            for (FinId fin : body.loopFins(l)) {
                const auto found = edgeDisplay.find(body.fin(fin).edge.index);
                if (found != edgeDisplay.end() && !indices.contains(found->second)) indices.append(found->second);
            }
    }
}

void forgeSurfaceConstructionCurves(const Body &body, BodyDisplay &display, int divisions, bool allCurvedFaces,
                                    const QVector<int> &faceFilter) {
    divisions = std::clamp(divisions, 2, 12);
    const int sideFaces = allCurvedFaces ? int(body.faces().size())
                                         : int(body.faces().size()) - (body.isSheet() ? 0 : 2); // i due coperchi del loft solido sono in fondo
    int facePosition = 0;
    for (FaceId face : body.faces()) {
        if (facePosition++ >= sideFaces) continue;
        if (!faceFilter.isEmpty() && !faceFilter.contains(face.index)) continue;
        const Surface &surface = *body.face(face).surface;
        if (allCurvedFaces && surface.type() == SurfaceType::Plane) continue;
        if (surface.type() == SurfaceType::Plane) {
            if (body.face(face).loops.size() != 1) continue;
            const std::vector<FinId> fins = body.loopFins(body.face(face).loops.front());
            if (fins.size() != 4) continue;
            Vec3 corner[4];
            for (int k = 0; k < 4; ++k) corner[k] = body.vertex(body.finStart(fins[std::size_t(k)])).point;
            for (int line = 1; line < divisions; ++line) {
                const double t = double(line) / divisions;
                for (int family = 0; family < 2; ++family) {
                    const Vec3 a = family == 0 ? (1.0 - t) * corner[0] + t * corner[3] : (1.0 - t) * corner[0] + t * corner[1];
                    const Vec3 b = family == 0 ? (1.0 - t) * corner[1] + t * corner[2] : (1.0 - t) * corner[3] + t * corner[2];
                    display.constructionCurves.append({toDisplay(a), toDisplay(b)});
                }
            }
            continue;
        }
        if (!allCurvedFaces && surface.type() != SurfaceType::BSpline) continue;
        Interval u = surface.uDomain(), v = surface.vDomain();
        if (allCurvedFaces) {
            double uLo = std::numeric_limits<double>::infinity(), uHi = -uLo;
            double vLo = std::numeric_limits<double>::infinity(), vHi = -vLo;
            for (LoopId loop : body.face(face).loops)
                for (FinId fin : body.loopFins(loop)) {
                    const Edge &edge = body.edge(body.fin(fin).edge);
                    for (int sample = 0; sample <= 12; ++sample) {
                        const double t = edge.range.lo + edge.range.length() * sample / 12.0;
                        Vec2 uv;
                        if (body.fin(fin).pcurve) uv = body.fin(fin).pcurve->point(t);
                        else {
                            const SurfaceProjection projection = projectPoint(surface, edge.curve->point(t));
                            uv = Vec2(projection.u, projection.v);
                        }
                        uLo = std::min(uLo, uv.x()); uHi = std::max(uHi, uv.x());
                        vLo = std::min(vLo, uv.y()); vHi = std::max(vHi, uv.y());
                    }
                }
            if (std::isfinite(uLo) && uHi > uLo) u = {uLo, uHi};
            if (std::isfinite(vLo) && vHi > vLo) v = {vLo, vHi};
        }
        if (!std::isfinite(u.lo) || !std::isfinite(u.hi) || !std::isfinite(v.lo) || !std::isfinite(v.hi)) continue;
        constexpr int samples = 32;
        for (int line = 1; line < divisions; ++line) {
            QVector<QVector3D> curve;
            const double fixed = u.lo + u.length() * line / divisions;
            for (int sample = 0; sample <= samples; ++sample)
                curve.append(toDisplay(surface.point(fixed, v.lo + v.length() * sample / samples)));
            display.constructionCurves.append(std::move(curve));
        }
        for (int line = 1; line < divisions; ++line) {
            QVector<QVector3D> curve;
            const double fixed = v.lo + v.length() * line / divisions;
            for (int sample = 0; sample <= samples; ++sample)
                curve.append(toDisplay(surface.point(u.lo + u.length() * sample / samples, fixed)));
            display.constructionCurves.append(std::move(curve));
        }
    }
}

void forgeBlendPreviewDisplay(const Body &base, const Body &result, int quality, BodyDisplay &display, int divisions) {
    display = {};
    display.quality = quality;
    std::set<const Surface *> oldSurfaces;
    for (FaceId face : base.faces()) oldSurfaces.insert(base.face(face).surface.get());
    QVector<int> patchFaces;
    std::set<int> patchFaceSet, patchEdges;
    for (FaceId face : result.faces())
        if (!oldSurfaces.count(result.face(face).surface.get())) {
            patchFaces.append(face.index);
            patchFaceSet.insert(face.index);
            for (LoopId loop : result.face(face).loops)
                for (FinId fin : result.loopFins(loop)) patchEdges.insert(result.fin(fin).edge.index);
        }
    if (patchFaces.isEmpty()) return;

    Box box;
    for (VertexId vertex : result.vertices()) box.add(result.vertex(vertex).point);
    const double diagonal = std::max(box.diagonal(), 1e-9);
    TessellationOptions options;
    options.deflection = diagonal * (quality <= 0 ? 4.0e-3 : quality == 1 ? 1.0e-3 : 2.0e-4);
    options.angle = quality <= 0 ? 0.5 : quality == 1 ? 0.25 : 0.1;
    const std::unique_ptr<SurfaceBatchEvaluator> accelerator = makeTessellationAccelerator();
    options.accelerator = accelerator.get();
    const Tessellation mesh = tessellate(result, options);
    for (const FaceMesh &face : mesh.faces) {
        if (!patchFaceSet.count(face.face.index)) continue;
        for (const std::array<int, 3> &triangle : face.triangles)
            for (int index : triangle) {
                display.vertices.append(toDisplay(face.points[std::size_t(index)]));
                display.normals.append(toDisplay(face.normals[std::size_t(index)]));
            }
    }
    for (std::size_t edgeIndex = 0; edgeIndex < mesh.edges.size(); ++edgeIndex) {
        if (!patchEdges.count(mesh.edgeIds[edgeIndex].index)) continue;
        QVector<QVector3D> polyline;
        for (const Vec3 &point : mesh.edges[edgeIndex]) polyline.append(toDisplay(point));
        if (polyline.size() >= 2) display.edges.append(std::move(polyline));
    }
    forgeSurfaceConstructionCurves(result, display, divisions, true, patchFaces);
}

bool forgePickFace(const Body &body, const QVector3D &origin, const QVector3D &direction, FaceHit &hit, const Kernel::RayFaceIndex *index,
                   const Kernel::Interval *window) {
    try {
        double t = 0.0;
        FaceId f;
        if (!firstRayHit(body, Vec3(origin.x(), origin.y(), origin.z()), Vec3(direction.x(), direction.y(), direction.z()), 1e-7, t, &f, index, window)
            || !f.valid())
            return false;
        hit = {};
        hit.distance = t;
        hit.face = f.index;
        const Face &face = body.face(f);
        if (face.surface->type() == SurfaceType::Plane) {
            const Frame3 &frame = static_cast<const Plane &>(*face.surface).frame();
            const Vec3 normal = face.sense ? frame.zDir() : -frame.zDir();
            hit.planar = true;
            for (int k = 0; k < 3; ++k) {
                hit.point[k] = frame.origin()[k];
                hit.normal[k] = normal[k];
            }
        }
        for (LoopId l : face.loops)
            for (FinId fin : body.loopFins(l)) {
                const Edge &edge = body.edge(body.fin(fin).edge);
                if (!edge.curve) continue;
                const Vec3 p = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
                hit.edges.append(edgeReference(body, body.fin(fin).edge, p));
            }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

int forgeFaceOwner(const Body &picked, int face, const Vec3 &point, const std::vector<std::pair<int, ForgeBody>> &chain) {
    try {
        const FaceId pickedFace(face);
        const Face &f = picked.face(pickedFace);
        if (!f.surface) return -1;
        const SurfaceProjection onPicked = projectPoint(*f.surface, point);
        const Vec3 normal = f.surface->normal(onPicked.u, onPicked.v);
        constexpr double kTolerance = 1e-6;
        for (const auto &[index, body] : chain) {
            if (!body) continue;
            for (FaceId g : body->faces()) {
                const Face &candidate = body->face(g);
                if (!candidate.surface) continue;
                // Gli edge tolleranti (STEP) allargano la tolleranza della faccia.
                double tolerance = kTolerance;
                for (LoopId l : candidate.loops)
                    for (FinId fin : body->loopFins(l)) tolerance = std::max(tolerance, body->edge(body->fin(fin).edge).tolerance);
                const Box box = faceBox(*body, g).padded(tolerance);
                bool inside = true;
                for (int k = 0; k < 3; ++k) inside = inside && point[k] >= box.lo[k] && point[k] <= box.hi[k];
                if (!inside) continue;
                bool same = sameSurface(*candidate.surface, *f.surface, tolerance);
                // Le superfici che sameSurface riconosce solo se sono lo stesso
                // oggetto (B-spline, rivoluzioni) possono essere copie (corpi
                // riletti dal documento): stesso tipo, punto sulla superficie e
                // normale parallela.
                if (!same && candidate.surface->type() == f.surface->type()) {
                    const SurfaceProjection on = projectPoint(*candidate.surface, point);
                    same = on.distance <= tolerance && std::fabs(dot(candidate.surface->normal(on.u, on.v), normal)) > 1.0 - 1e-6;
                }
                if (same && classifyPointOnFace(*body, g, point, tolerance) != PointLocation::Outside) return index;
            }
        }
    } catch (const std::exception &) {
    }
    return -1;
}

bool forgeIntersectRay(const Body &body, const QVector3D &origin, const QVector3D &direction, double &distance, const Kernel::RayFaceIndex *index) {
    try {
        double t = 0.0;
        if (!firstRayHit(body, Vec3(origin.x(), origin.y(), origin.z()), Vec3(direction.x(), direction.y(), direction.z()), 1e-7, t, nullptr, index))
            return false;
        distance = t;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

}
