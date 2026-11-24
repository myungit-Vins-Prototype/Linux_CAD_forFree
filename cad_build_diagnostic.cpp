#include "cad_build_diagnostic.h"
#include "cad_feature_dependencies.h"
#include "cad_kernel.h"
#include "fk_tessellate.h"
#include "fk_topology.h"

#include <set>

#include <QStringList>
#include <algorithm>
#include <cmath>
#include <optional>

namespace ForgeCad {
namespace {
bool finitePoint(const QVector3D &p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z());
}

// Un riquadro di contesto usa le posizioni disponibili nel documento.
// Conserva la distinzione fra gli operandi e una causa localizzata dal kernel.
struct DisplayBounds {
    std::optional<QVector3D> lo;
    QVector3D hi;
    void add(const QVector3D &point) {
        if (!finitePoint(point)) return;
        if (!lo) { lo = hi = point; return; }
        for (int axis = 0; axis < 3; ++axis) {
            (*lo)[axis] = std::min((*lo)[axis], point[axis]);
            hi[axis] = std::max(hi[axis], point[axis]);
        }
    }
    void appendTo(BuildDiagnostic &diagnostic) const {
        if (!lo) return;
        if (*lo == hi) { diagnostic.paths.append(QVector<QVector3D>{hi}); return; }
        for (int axis = 0; axis < 3; ++axis) {
            if ((*lo)[axis] == hi[axis]) continue;
            const int a = (axis + 1) % 3, b = (axis + 2) % 3;
            for (int i = 0; i <= int((*lo)[a] != hi[a]); ++i)
                for (int j = 0; j <= int((*lo)[b] != hi[b]); ++j) {
                    QVector3D start = *lo;
                    if (i) start[a] = hi[a];
                    if (j) start[b] = hi[b];
                    QVector3D end = start;
                    end[axis] = hi[axis];
                    diagnostic.paths.append({start, end});
                }
        }
    }
};

void addSketchBounds(DisplayBounds &bounds, const SketchObject &sketch) {
    // Anche il piano dello schizzo può essere la causa del fallimento.
    // In quel caso è disponibile il nome, ma nessuna posizione affidabile.
    std::optional<Kernel::Frame3> frame;
    try { frame = sketchAxes(sketch); }
    catch (const std::logic_error &) { return; }
    const auto add = [&](const QPointF &uv) {
        const auto point = frame->toGlobal({uv.x(), uv.y(), 0.0});
        bounds.add(QVector3D(point.x(), point.y(), point.z()));
    };
    for (const auto &s : sketch.segments) {
        add(s.first);
        add(s.second);
    }
    for (const auto &c : sketch.curves)
        for (const auto &p : c.samples.isEmpty() ? c.controlPoints : c.samples) add(p);
}

}

void appendDiagnosticCurve(BuildDiagnostic &diagnostic, const Kernel::Curve<3> &curve, const Kernel::Interval &range) {
    try {
        QVector<QVector3D> path;
        for (double t : Kernel::sampleCurve(curve, range, Kernel::TessellationOptions{})) {
            const auto point = curve.point(t);
            path.append(QVector3D(point.x(), point.y(), point.z()));
        }
        if (path.size() >= 2) diagnostic.paths.append(std::move(path));
    } catch (const std::exception &) {
        // La diagnosi di costruzione resta disponibile anche per un bordo non disegnabile.
    }
}

BuildDiagnostic sketchReferenceDiagnostic(const SketchObject &sketch) {
    BuildDiagnostic diagnostic;
    const auto addReference = [&](const GeometryRef &ref) {
        if (ref.kind < 3 || ref.kind > 5) return;
        if (ref.point.subshape < 0 && ref.point.geometry < 0 && ref.point.name.isEmpty()) return;
        const QVector3D p(ref.point.x, ref.point.y, ref.point.z);
        if (finitePoint(p)) diagnostic.paths.append(QVector<QVector3D>{p});
    };
    if (!sketch.supportError.isEmpty() && sketch.support) addReference(*sketch.support);
    for (const auto &link : sketch.linkedReferences)
        if (link && !link->error.isEmpty()) addReference(link->source);
    if (!diagnostic.paths.isEmpty()) {
        diagnostic.context = QStringLiteral("%1 · Ultima posizione dei riferimenti non risolti").arg(sketch.name);
    } else {
        DisplayBounds bounds;
        addSketchBounds(bounds, sketch);
        bounds.appendTo(diagnostic);
        diagnostic.context = QStringLiteral("%1 · Posizione esatta del problema non disponibile.").arg(sketch.name);
    }
    return diagnostic;
}

BuildDiagnostic featureDiagnosticContext(const ExtrusionObject &feature, const QVector<SketchObject> &sketches,
                                         const QVector<ExtrusionObject> &bodies) {
    BuildDiagnostic diagnostic;
    for (const auto &resolution : feature.referenceResolutions) {
        if (resolution.method != TopologyResolutionMethod::Missing && resolution.method != TopologyResolutionMethod::Ambiguous) continue;
        const auto &r = resolution.reference;
        if (r.subshape < 0 && r.name.isEmpty() && r.geometry < 0) continue;
        const QVector3D p(r.x, r.y, r.z);
        if (finitePoint(p)) diagnostic.paths.append(QVector<QVector3D>{p});
    }
    if (!diagnostic.paths.isEmpty()) {
        diagnostic.context = QStringLiteral("Ultima posizione dei riferimenti non risolti");
        return diagnostic;
    }
    // Se il comando ha risolto spigoli o facce, indica le entità su cui ha
    // lavorato, senza attribuire a una di esse la causa precisa del fallimento.
    std::set<std::pair<int, int>> drawnEdges;
    for (const auto &r : feature.referenceResolutions) {
        if (r.sourceIndex < 0 || r.sourceIndex >= bodies.size() || r.resolvedSubshape < 0) continue;
        const auto &source = bodies.at(r.sourceIndex);
        if (!source.forgeBody || (r.sourceFeatureId && r.sourceFeatureId != source.featureId)) continue;
        const auto &shape = *source.forgeBody;
        const auto edge = [&](Kernel::EdgeId id) {
            if (!shape.contains(id) || !drawnEdges.emplace(r.sourceIndex, id.index).second) return;
            const auto &e = shape.edge(id);
            if (e.curve) appendDiagnosticCurve(diagnostic, *e.curve, e.range);
        };
        if (r.kind == 4) edge(Kernel::EdgeId(r.resolvedSubshape));
        if (r.kind == 5 && shape.contains(Kernel::FaceId(r.resolvedSubshape))) {
            for (auto loop : shape.face(Kernel::FaceId(r.resolvedSubshape)).loops)
                for (auto fin : shape.loopFins(loop)) edge(shape.fin(fin).edge);
        }
        if (r.kind == 3 && shape.contains(Kernel::VertexId(r.resolvedSubshape))) {
            const auto &p = shape.vertex(Kernel::VertexId(r.resolvedSubshape)).point;
            diagnostic.paths.append(QVector<QVector3D>{QVector3D(p.x(), p.y(), p.z())});
        }
    }
    if (!diagnostic.paths.isEmpty()) {
        diagnostic.context = QStringLiteral("Riferimenti usati dalla funzione. Posizione esatta del problema non disponibile.");
        return diagnostic;
    }
    QStringList names;
    for (int index : bodyOperands(feature)) {
        if (index < 0 || index >= bodies.size()) continue;
        const auto &body = bodies[index];
        const QString name = body.name.isEmpty() ? QStringLiteral("Corpo %1").arg(index + 1) : body.name;
        if (!names.contains(name)) names.append(name);
        DisplayBounds bounds;
        if (body.forgeBody) {
            for (auto v : body.forgeBody->vertices()) {
                const auto &p = body.forgeBody->vertex(v).point;
                bounds.add(QVector3D(p.x(), p.y(), p.z()));
            }
        }
        for (const auto &p : body.display.faceLabelPoints) bounds.add(p);
        bounds.appendTo(diagnostic);
    }
    for (int index : sketchesOf(feature)) {
        if (index < 0 || index >= sketches.size()) continue;
        const auto &sketch = sketches[index];
        const QString name = sketch.name.isEmpty() ? QStringLiteral("Schizzo %1").arg(index + 1) : sketch.name;
        if (!names.contains(name)) names.append(name);
        DisplayBounds bounds;
        addSketchBounds(bounds, sketch);
        bounds.appendTo(diagnostic);
    }
    if (!names.isEmpty())
        diagnostic.context = QStringLiteral("Geometria di ingresso (%1). Posizione esatta non disponibile.").arg(names.join(QStringLiteral(", ")));
    else if (!feature.name.isEmpty())
        diagnostic.context = QStringLiteral("%1 · Posizione geometrica non disponibile.").arg(feature.name);
    else
        diagnostic.context = QStringLiteral("Posizione geometrica non disponibile.");
    return diagnostic;
}

} // namespace ForgeCad
