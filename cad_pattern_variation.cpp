#include "cad_pattern_variation.h"
#include "cad_constraints.h"
#include "cad_feature_dependencies.h"
#include "cad_feature_build.h"
#include "cad_geometry_refs.h"
#include "cad_sketch_refs.h"
#include "cad_datum.h"
#include "cad_kernel.h"
#include "cad_pattern.h"
#include "cad_forge.h"
#include "fk_cancel.h"
#include "fk_topology.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ForgeCad {
namespace {
// Un solo catalogo governa elenco, validazione e applicazione: nessun campo
// numerico interno (ID, enum, tolleranze del solver) viene scambiato per una quota.
template <class Visit> void featureNumbers(ExtrusionObject &b, Visit visit) {
    const auto add = [&](const QString &key, const QString &label, auto &value,
                         const QString &unit = QStringLiteral("mm"), double lo = -1e9, double hi = 1e9) {
        visit(QString::number(int(b.feature)) + QLatin1Char('/') + key, label, value, unit, lo, hi);
    };
    const auto vector = [&](const QString &key, const QString &label, QVector<double> &values, const QString &unit,
                            double lo, double hi) {
        for (int i = 0; i < values.size(); ++i)
            add(key + QString::number(i), label + QStringLiteral(" %1").arg(i + 1), values[i], unit, lo, hi);
    };
    if (b.operation >= 0 || b.suppressed)
        return;
    switch (b.feature) {
    case BodyFeature::Extrusion:
        if (b.extent == 0)
            add("distance", "Distanza", b.distance);
        if (b.extrudeSides == 2)
            add("distance2", "Seconda distanza", b.distance2, "mm", 0);
        break;
    case BodyFeature::Revolution:
        add("angle", "Angolo", b.revolveAngle, "°", -360, 360);
        break;
    case BodyFeature::Primitive: {
        const QString prefix = QStringLiteral("primitive%1/").arg(int(b.primitive.kind));
        for (int i = 0; i < 3; ++i)
            add(prefix + "origin" + QString::number(i), QStringLiteral("Origine %1").arg(QStringLiteral("XYZ").at(i)),
                b.primitive.origin[i]);
        const QStringList labels =
            b.primitive.kind == PrimitiveKind::Box        ? QStringList{"Larghezza", "Profondità", "Altezza"}
            : b.primitive.kind == PrimitiveKind::Cylinder ? QStringList{"Raggio", "Altezza"}
            : b.primitive.kind == PrimitiveKind::Cone     ? QStringList{"Raggio base", "Raggio sommità", "Altezza"}
            : b.primitive.kind == PrimitiveKind::Torus    ? QStringList{"Raggio maggiore", "Raggio minore"}
                                                          : QStringList{"Raggio"};
        for (int i = 0; i < labels.size(); ++i)
            add(prefix + "size" + QString::number(i), labels[i], b.primitive.size[i], "mm",
                b.primitive.kind == PrimitiveKind::Cone && i < 2 ? 0 : 1e-9);
        break;
    }
    case BodyFeature::Blend:
        if (b.blendChamfer) {
            add("chamfer", "Distanza smusso", b.blendSize, "mm", 1e-9);
            if (b.chamferSpec.mode)
                add("chamferSecond" + QString::number(b.chamferSpec.mode), "Seconda misura smusso",
                    b.chamferSpec.second, b.chamferSpec.mode == 2 ? "°" : "mm", 1e-9,
                    b.chamferSpec.mode == 2 ? 89.999999 : 1e9);
        } else {
            auto &f = b.chamferSpec.fillet;
            if (f.mode == 0 || f.mode == 3)
                add("radius", "Raggio raccordo", b.blendSize, "mm", 1e-9);
            if (f.mode == 1) {
                if (f.conicControl)
                    add("conicRadius", "Raggio conico", f.conicRadius, "mm", 1e-9);
                else
                    add("rho", "Rho conico", f.rho, "", 1e-9, 1 - 1e-9);
                add("conicSize", "Dimensione raccordo", b.blendSize, "mm", 1e-9);
            }
            if (f.mode == 2) {
                if (f.paths.isEmpty())
                    vector("radii/", "Raggio stazione", f.radii, "mm", 1e-9, 1e9);
                for (int i = 0; i < f.paths.size(); ++i)
                    vector(QStringLiteral("path%1/").arg(i), QStringLiteral("Percorso %1 · raggio").arg(i + 1),
                           f.paths[i].radii, "mm", 1e-9, 1e9);
            }
        }
        break;
    case BodyFeature::SheetExtend:
        add("extent", "Estensione", b.blendSize);
        break;
    case BodyFeature::SurfaceOffset:
        add("distance", "Distanza offset", b.distance);
        break;
    case BodyFeature::Shell:
    case BodyFeature::Thicken:
        add("thickness", "Spessore", b.distance, "mm", 1e-9);
        break;
    case BodyFeature::Scale:
        add("scale", "Fattore di scala", b.scaleFactor, "", 1e-9);
        if (b.scaleCenterMode == 2) {
            add("cx", "Centro X", b.scaleCenter.x);
            add("cy", "Centro Y", b.scaleCenter.y);
            add("cz", "Centro Z", b.scaleCenter.z);
        }
        break;
    case BodyFeature::Helix:
        if (b.helix.spiral || b.helix.mode != 1)
            add("pitch", "Passo", b.helix.pitch, "mm", 1e-9);
        if (b.helix.spiral || b.helix.mode != 2)
            add("turns", "Giri", b.helix.turns, "", 1e-9);
        if (!b.helix.spiral && b.helix.mode != 0)
            add("height", "Altezza", b.helix.height, "mm", 1e-9);
        if (!b.helix.spiral)
            add("taper", "Conicità", b.helix.taper, "°", -89.999, 89.999);
        add("start", "Angolo iniziale", b.helix.startAngle, "°");
        break;
    case BodyFeature::DatumPlane:
        if (b.datum.mode >= 0 && b.datum.mode < datumModes().size()) {
            const auto &mode = datumModes()[b.datum.mode];
            if (mode.distance)
                add("distance", "Distanza piano", b.datum.distance);
            if (mode.angle)
                add("angle", "Angolo piano", b.datum.angle, "°");
        }
        add("size", "Dimensione piano", b.datum.size, "mm", 0);
        break;
    case BodyFeature::Transform:
        for (int i = 0; i < 3; ++i)
            add("translation" + QString::number(i), QStringLiteral("Traslazione %1").arg(QStringLiteral("XYZ").at(i)),
                b.move.translation[i]);
        add("rotation", "Rotazione", b.move.angle, "°");
        break;
    case BodyFeature::Pattern:
        if (b.patternComponent > 0 || b.pattern.kind == 2)
            break;
        add("count", "Istanze", b.pattern.count, "", b.pattern.kind == 0 ? 1 : 2, 500);
        if (b.pattern.kind == 0) {
            add("spacing", "Passo", b.pattern.spacing);
            if (b.pattern.count2 > 1) {
                add("count2", "Istanze 2", b.pattern.count2, "", 1, 500);
                add("spacing2", "Passo 2", b.pattern.spacing2);
            }
        } else
            add("angle", "Angolo ripetizione", b.pattern.angle, "°", -360, 360);
        break;
    case BodyFeature::Thread:
        add("pitch", "Passo filetto", b.thread.pitch, "mm", 1e-9);
        add("length", "Lunghezza filetto", b.thread.length, "mm", 0);
        break;
    case BodyFeature::Draft:
        add("angle", "Angolo sformo", b.draftAngle, "°", -89.999, 89.999);
        break;
    case BodyFeature::Loft:
    case BodyFeature::Ruled:
        if (b.loftStartContinuity)
            add("startInfluence", "Influenza iniziale", b.loftStartInfluence, "", 0, 1);
        if (b.loftEndContinuity)
            add("endInfluence", "Influenza finale", b.loftEndInfluence, "", 0, 1);
        if (b.feature == BodyFeature::Loft &&
            (!b.loftGuides.isEmpty() || !b.loftGuidePaths.isEmpty() || !b.loftEdgeGuides.isEmpty())) {
            if (b.loftGuideInfluences.isEmpty())
                add("guideInfluence", "Influenza guide", b.loftGuideInfluence, "", 0, 1);
            vector("guide/", "Influenza guida", b.loftGuideInfluences, "", 0, 1);
        }
        break;
    case BodyFeature::FillSurface:
        add("influence", "Influenza facce", b.fillInfluence, "", 0, 1e9);
        if (!b.ruledFirst.isEmpty())
            add("guideWeight", "Peso guide", b.fillGuideWeight, "", 0, 1e9);
        if (b.fillShapeFace >= 0)
            add("shapeWeight", "Peso forma", b.fillShapeWeight, "", 0, 1e9);
        vector("face/", "Influenza faccia", b.fillFaceInfluences, "", 0, 1e9);
        break;
    case BodyFeature::Sew:
        add("tolerance", "Tolleranza cucitura", b.sewTolerance, "mm", 1e-9, 1);
        break;
    case BodyFeature::SurfaceTools:
        if (b.surfaceTool.mode == 0)
            add("maximum", "Estensione massima", b.surfaceTool.maximum, "mm", 1e-9);
        if (b.surfaceTool.mode == 1)
            add("tolerance", "Tolleranza rete", b.surfaceTool.tolerance, "mm", 1e-9, 1);
        if (b.surfaceTool.mode == 2)
            for (int i = 0; i < b.surfaceTool.edits.size(); ++i) {
                auto &e = b.surfaceTool.edits[i];
                const auto key = QStringLiteral("pole%1,%2/").arg(e.u).arg(e.v);
                const auto name = QStringLiteral("Polo %1,%2 · ").arg(e.u).arg(e.v);
                add(key + "x", name + "X", e.dx);
                add(key + "y", name + "Y", e.dy);
                add(key + "z", name + "Z", e.dz);
                add(key + "w", name + "Peso", e.weightFactor, "", 1e-9);
            }
        break;
    case BodyFeature::SpatialSketch:
        for (auto &e : b.spatialSketch.entities)
            for (int i = 0; i < e.points.size(); ++i) {
                auto &p = e.points[i];
                const auto key = QStringLiteral("curve%1/point%2/").arg(e.id).arg(i);
                const auto name = QStringLiteral("Curva %1 · punto %2 · ").arg(e.id).arg(i + 1);
                add(key + "x", name + "X", p.x);
                add(key + "y", name + "Y", p.y);
                add(key + "z", name + "Z", p.z);
            }
        break;
    default:
        break;
    }
}

template <class Visit> void sketchNumbers(SketchObject &s, Visit visit) {
    for (auto &c : s.geometricConstraints) {
        if (isDimension(c.type))
            visit(c, QStringLiteral("value"), describeConstraint(s, c), c.value,
                  c.type == ConstraintType::Angle ? QStringLiteral("°") : QStringLiteral("mm"), -1e9, 1e9);
        if (c.type == ConstraintType::Pattern && c.pattern.kind != 2) {
            auto &p = c.pattern;
            visit(c, "count", "Numero istanze schizzo", p.count, "", p.kind == 0 ? 1. : 2., 500.);
            if (p.kind == 0) {
                if (p.dimensioned)
                    visit(c, "spacing", "Passo schizzo", p.spacing, "mm", -1e9, 1e9);
                if (p.count2 > 1) {
                    visit(c, "count2", "Numero istanze schizzo 2", p.count2, "", 1., 500.);
                    if (p.dimensioned2)
                        visit(c, "spacing2", "Passo schizzo 2", p.spacing2, "mm", -1e9, 1e9);
                }
            } else if (p.dimensioned)
                visit(c, "angle", "Angolo ripetizione schizzo", p.angle, "°", -360., 360.);
        }
    }
}

// Il piano contiene nodi schizzo (negativi -1-index) e feature (non negativi).
struct Plan {
    QVector<int> order;
    QHash<int, QVector<int>> deps;
};
int referenceNode(const GeometryRef &ref, const QVector<SketchObject> &s, const QVector<ExtrusionObject> &b) {
    if (!isBodyRef(ref) && !isSketchRef(ref))
        return std::numeric_limits<int>::min();
    int i = sketchReferenceSourceIndex(ref, s, b);
    if (i < 0)
        throw std::runtime_error("Riferimento sorgente mancante");
    return isSketchRef(ref) ? -1 - i : i;
}
Plan makePlan(int root, const QVector<SketchObject> &s, const QVector<ExtrusionObject> &b) {
    Plan plan;
    QSet<int> seen, active;
    std::function<void(int)> visit = [&](int n) {
        if (seen.contains(n))
            return;
        if (active.contains(n))
            throw std::runtime_error("Dipendenze cicliche nella sorgente");
        active.insert(n);
        QVector<int> deps;
        const auto ref = [&](const GeometryRef &r) {
            int k = referenceNode(r, s, b);
            if (k != std::numeric_limits<int>::min() && !deps.contains(k))
                deps.append(k);
        };
        if (n < 0) {
            int i = -1 - n;
            if (i < 0 || i >= s.size())
                throw std::runtime_error("Schizzo sorgente mancante");
            const auto &sketch = s[i];
            if (sketch.datumPlane >= 0)
                deps.append(sketch.datumPlane);
            visitSketchGeometryRefs(sketch, [&](const auto &r, auto, int) { ref(r); });
        } else {
            if (n >= b.size() || n > root)
                throw std::runtime_error("Dipendenza della sorgente non valida");
            deps = bodyOperands(b[n]);
            for (int d : deps)
                if (d < 0)
                    throw std::runtime_error("Operando della sorgente mancante");
            for (int i : sketchesOf(b[n])) {
                if (i < 0 || i >= s.size())
                    throw std::runtime_error("Schizzo sorgente mancante");
                deps.append(-1 - i);
            }
        }
        for (int d : deps)
            visit(d);
        active.remove(n);
        seen.insert(n);
        plan.deps[n] = deps;
        plan.order.append(n);
    };
    visit(root);
    return plan;
}

Plan patternPlan(const ExtrusionObject &pattern, const QVector<SketchObject> &s, QVector<ExtrusionObject> b) {
    const int root = pattern.firstBody;
    if (root < 0 || root >= b.size())
        throw std::runtime_error("Sorgente mancante");
    if (pattern.pattern.featureOnly) {
        auto &top = b[root];
        if (top.operation >= 0) {
            top.operation = -1;
            top.feature = BodyFeature::Sew;
            top.firstBody = top.secondBody;
        } else {
            top.mergeOperation = 0;
            top.mergeBodies.clear();
        }
    }
    return makePlan(root, s, b);
}

// Il piano di rigenerazione comprende gli antenati; il catalogo pubblico no.
Plan parameterScope(const ExtrusionObject &pattern, const QVector<SketchObject> &s,
                    const QVector<ExtrusionObject> &b) {
    Plan scope;
    const int root = pattern.firstBody;
    if (!pattern.pattern.featureOnly || root < 0 || root >= b.size() || !patternFeatureSupported(b[root]))
        return scope;
    const auto appendFeature = [&](int index) {
        if (index < 0 || index >= b.size()) return;
        for (int sketch : sketchesOf(b[index]))
            if (sketch >= 0 && sketch < s.size() && !scope.order.contains(-1-sketch))
                scope.order.append(-1-sketch);
        if (!scope.order.contains(index)) scope.order.append(index);
    };
    if (b[root].operation >= 0) {
        appendFeature(b[root].secondBody);
        for (int tool : b[root].booleanTools) appendFeature(tool);
    } else appendFeature(root);
    return scope;
}

QVector<PatternParameter> catalog(const Plan &plan, QVector<SketchObject> s, QVector<ExtrusionObject> b) {
    QVector<PatternParameter> out;
    for (int n : plan.order) {
        PatternVariation ref;
        ref.sketch = n < 0;
        ref.ownerId = n < 0 ? s[-1 - n].storyId : b[n].featureId;
        if (!ref.ownerId)
            continue;
        const auto append = [&](const QString &key, const QString &name, auto &value, const QString &unit, double lo,
                                double hi) {
            PatternParameter p;
            p.reference = ref;
            p.reference.parameter = key;
            p.label = (n < 0 ? s[-1 - n].name : b[n].name) + QStringLiteral(" · ") + name;
            p.value = double(value);
            p.minimum = lo;
            p.maximum = hi;
            p.unit = unit;
            p.integer = std::is_integral_v<std::decay_t<decltype(value)>>;
            out.append(p);
        };
        if (n < 0)
            sketchNumbers(s[-1 - n], [&](const auto &c, const auto &key, const auto &name, auto &value,
                                         const auto &unit, double lo, double hi) {
                ref.parameterId = c.parameterId;
                if (ref.parameterId)
                    append(QString::number(int(c.type)) + "/" + key, name, value, unit, lo, hi);
            });
        else
            featureNumbers(b[n], append);
    }
    return out;
}
} // namespace

bool samePatternParameter(const PatternVariation &a, const PatternVariation &b) {
    return a.sketch == b.sketch && a.ownerId == b.ownerId && a.parameterId == b.parameterId &&
           a.parameter == b.parameter;
}
QVector<PatternParameter> patternParameters(const ExtrusionObject &pattern, const QVector<SketchObject> &s,
                                            const QVector<ExtrusionObject> &b, QString *error) {
    try {
        return catalog(parameterScope(pattern, s, b), s, b);
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
namespace {
QString resizeVariantPatterns(int index, const SketchObject &original, QVector<SketchObject> &sketches,
                              QVector<ExtrusionObject> &bodies) {
    auto &sketch = sketches[index];
    for (const auto &old : original.geometricConstraints) {
        if (old.type != ConstraintType::Pattern)
            continue;
        int at = -1;
        for (int i = 0; i < sketch.geometricConstraints.size(); ++i)
            if (sketch.geometricConstraints[i].parameterId == old.parameterId) {
                at = i;
                break;
            }
        if (at < 0)
            return QStringLiteral("Un vincolo è stato eliminato dalla variazione del conteggio.");
        auto values = sketch.geometricConstraints[at].pattern;
        if (values.count == old.pattern.count && values.count2 == old.pattern.count2)
            continue;
        const auto before = sketch;
        sketch.geometricConstraints[at].pattern = old.pattern;
        const auto edited = editSketchPattern(sketch, at, values);
        if (!edited.error.isEmpty())
            return edited.error;
        // Il comando ricrea la constraint in fondo: conserva la sua identita.
        auto &created = sketch.geometricConstraints.last();
        created.parameterId = old.parameterId;
        for (const auto &c : before.geometricConstraints)
            if (c.parameterId != old.parameterId) {
                bool present = false;
                for (const auto &after : sketch.geometricConstraints)
                    if (c.parameterId == after.parameterId) {
                        present = true;
                        break;
                    }
                if (!present)
                    return QStringLiteral(
                        "Il conteggio eliminerebbe un'entità vincolata. Modifica prima i vincoli sulle copie.");
            }
        QVector<int> segmentMap = edited.segmentMap, curveMap(before.curves.size(), -1);
        QSet<int> removed;
        for (const auto &r : old.pattern.copies)
            if (r.kind == 1)
                removed.insert(r.element);
        for (int i = 0, next = 0; i < curveMap.size(); ++i)
            if (!removed.contains(i))
                curveMap[i] = next++;
        for (int i = 0; i < old.pattern.copies.size() && i < created.pattern.copies.size(); ++i) {
            const auto &from = old.pattern.copies[i], &to = created.pattern.copies[i];
            if (from.kind != to.kind)
                continue;
            if (from.kind == 0 && from.element >= 0 && from.element < segmentMap.size())
                segmentMap[from.element] = to.element;
            if (from.kind == 1 && from.element >= 0 && from.element < curveMap.size())
                curveMap[from.element] = to.element;
        }
        QString failure;
        const auto remap = [&](GeometryRef &ref) {
            if (!isSketchRef(ref) || (ref.featureId ? ref.featureId != sketch.storyId : ref.index != index) ||
                ref.element.element < 0)
                return;
            const auto &map = ref.element.kind == 0 ? segmentMap : curveMap;
            if (ref.element.kind != 0 && ref.element.kind != 1)
                return;
            ref.element.element = map.value(ref.element.element, -1);
            if (ref.element.element < 0)
                failure = QStringLiteral("Il conteggio elimina un'entità usata da un riferimento.");
        };
        for (auto &other : sketches)
            visitSketchGeometryRefs(other, [&](auto &ref, auto, int) { remap(ref); });
        for (auto &b : bodies) {
            visitGeometryRefs(b, [&](auto &ref, auto, int) { remap(ref); });
            if (b.sketchIndex == index && b.feature == BodyFeature::Revolution && b.revolveAxis >= 0) {
                b.revolveAxis = segmentMap.value(b.revolveAxis, -1);
                if (b.revolveAxis < 0)
                    failure = QStringLiteral("Asse della rivoluzione eliminato dal conteggio.");
            }
            // I percorsi legacy usano indici nudi: non riassociare a copie diverse.
            if ((b.pathSketch == index && (!b.pathSegments.isEmpty() || !b.pathCurves.isEmpty())) ||
                (b.sketchIndex == index && b.feature == BodyFeature::Helix && b.helix.curve >= 0))
                failure = QStringLiteral("Il conteggio modifica uno schizzo usato da un percorso parziale: usa il "
                                         "riferimento all'intero schizzo.");
            for (const auto &path : b.loftGuidePaths)
                if (path.sketch == index)
                    failure = QStringLiteral("Il conteggio modifica una guida parziale della loft.");
        }
        if (!failure.isEmpty())
            return failure;
    }
    return {};
}

QString refreshVariantSketch(SketchObject &sketch, const QVector<SketchObject> &s, const QVector<ExtrusionObject> &b) {
    if (sketch.plane == kFacePlane && (sketch.support || sketch.datumPlane >= 0)) {
        SketchFrame plane;
        if (sketch.datumPlane >= 0) {
            if (sketch.datumPlane >= b.size() || !b[sketch.datumPlane].datumValid)
                return QStringLiteral("Piano di appoggio non valido");
            plane = b[sketch.datumPlane].datumFrame;
        } else {
            ResolvedRef resolved;
            QString error;
            if (!resolveGeometryRef(*sketch.support, b.size(), s, b, resolved, &error) || !resolved.hasPlane)
                return error.isEmpty() ? QStringLiteral("Appoggio non planare") : error;
            plane = faceSketchFrame(resolved.point, resolved.direction);
        }
        const Kernel::Vec3 normal(plane.normal[0], plane.normal[1], plane.normal[2]);
        const Kernel::Vec3 origin(plane.origin[0], plane.origin[1], plane.origin[2]);
        const Kernel::Vec3 previous(sketch.frame.origin[0], sketch.frame.origin[1], sketch.frame.origin[2]);
        Kernel::Vec3 x(sketch.frame.xAxis[0], sketch.frame.xAxis[1], sketch.frame.xAxis[2]);
        x = x - dot(x, normal) * normal;
        if (norm(x) > 1e-3)
            x = x / norm(x);
        else
            x = Kernel::Vec3(plane.xAxis[0], plane.xAxis[1], plane.xAxis[2]);
        const auto o = previous - dot(previous - origin, normal) * normal;
        for (int k = 0; k < 3; ++k) {
            sketch.frame.origin[k] = o[k];
            sketch.frame.xAxis[k] = x[k];
            sketch.frame.normal[k] = normal[k];
        }
        sketch.supportError.clear();
    }
    auto error = refreshLinkedSketchReferences(sketch, s, b);
    if (!error.isEmpty())
        return error;
    for (auto &c : sketch.geometricConstraints)
        if (c.pierce) {
            auto ref = c.pierce->surface;
            int i = sketchReferenceSourceIndex(ref, s, b);
            if (i < 0 || i >= b.size())
                return QStringLiteral("Riferimento Pierce mancante");
            ref.index = i;
            if (ref.kind == 9)
                error = refreshCurvePierce(sketch, c, ref, s, b);
            else if (b[i].forgeBody)
                error = refreshSurfacePierce(sketch, c, *b[i].forgeBody, ref, b[i].name);
            else
                error = QStringLiteral("Superficie Pierce mancante");
            if (!error.isEmpty())
                return error;
        }
    const auto solved = solveSketch(sketch);
    return solved.ok ? QString() : solved.error;
}
} // namespace
ForgeBody forgeVariablePattern(const ExtrusionObject &pattern, int index, const QVector<SketchObject> &sketches,
                               const QVector<ExtrusionObject> &bodies, QString *error) {
    const auto fail = [&](const QString &message) -> ForgeBody {
        if (error)
            *error = message;
        return {};
    };
    int occurrence = 0;
    try {
        if (!pattern.pattern.featureOnly)
            return fail(QStringLiteral("Le variazioni richiedono Una funzione; non sono disponibili per Il corpo intero."));
        if (pattern.pattern.kind == 2)
            return fail(QStringLiteral("Le variazioni sono disponibili nelle ripetizioni lineari e circolari."));
        if (pattern.firstBody < 0 || pattern.firstBody >= index || index > bodies.size())
            return fail(QStringLiteral("Sorgente della ripetizione non valida."));
        const auto plan = patternPlan(pattern, sketches, bodies);
        const auto parameters = catalog(parameterScope(pattern, sketches, bodies), sketches, bodies);
        QVector<int> selected;
        for (const auto &v : pattern.pattern.variations) {
            int found = -1;
            for (int i = 0; i < parameters.size(); ++i)
                if (samePatternParameter(v, parameters[i].reference)) {
                    found = i;
                    break;
                }
            if (found < 0)
                return fail(QStringLiteral("Variazione %1: il parametro non esiste più o non appartiene alla sorgente. "
                                           "Riselezionalo nella tabella Variazioni.")
                                .arg(selected.size() + 1));
            if (selected.contains(found))
                return fail(QStringLiteral("Parametro ripetuto nella tabella: %1.").arg(parameters[found].label));
            if (!std::isfinite(v.increment) || (parameters[found].integer && v.increment != std::trunc(v.increment)))
                return fail(QStringLiteral("Incremento non valido per %1.").arg(parameters[found].label));
            selected.append(found);
        }
        // Aprendo la tabella gli incrementi sono zero: riusa il percorso delle
        // copie identiche, evitando di rigenerare inutilmente una sorgente costosa.
        if (std::all_of(pattern.pattern.variations.begin(), pattern.pattern.variations.end(),
                        [](const auto &v) { return v.increment == 0; })) {
            auto identical = pattern;
            identical.pattern.variations.clear();
            identical.patternComponent = -1;
            // Evita che i risultati derivati del documento riducano l'aggregato.
            auto context = bodies;
            for (auto &b : context)
                if (b.feature == BodyFeature::Pattern && b.patternComponent > 0 && b.firstBody == index)
                    b.patternComponent = -1;
            buildGeometry(identical, index, sketches, context);
            if (!identical.error.isEmpty())
                return fail(identical.error);
            return identical.forgeBody;
        }
        std::vector<Kernel::Transform3> placements;
        if (!patternPlacements(pattern.pattern, index, sketches, bodies, placements, error))
            return {};
        const auto &base = bodies[pattern.firstBody];
        if (pattern.pattern.featureOnly && !patternFeatureSupported(base))
            return fail(QStringLiteral("La sorgente non supporta la ripetizione della funzione."));
        ForgeBody fixedTarget;
        BooleanOperation operation = BooleanOperation::Union;
        if (pattern.pattern.featureOnly) {
            QVector<int> targets = base.operation >= 0 ? QVector<int>{base.firstBody} : base.mergeBodies;
            operation = base.operation >= 0        ? BooleanOperation(base.operation)
                        : base.mergeOperation == 2 ? BooleanOperation::Difference
                                                   : BooleanOperation::Union;
            if (pattern.secondBody >= 0) {
                if (pattern.secondBody <= pattern.firstBody || pattern.secondBody >= index ||
                    bodies[pattern.secondBody].modelBodyId != base.modelBodyId)
                    return fail(QStringLiteral("Destinazione della funzione non valida."));
                if (pattern.pattern.excluded.contains(0))
                    return fail(
                        QStringLiteral("L'originale appartiene alle lavorazioni successive e non può essere escluso."));
                targets = {pattern.secondBody};
            }
            for (int i : targets) {
                if (i < 0 || i >= index || !bodies[i].forgeBody)
                    return fail(QStringLiteral("Corpo di destinazione non valido."));
                fixedTarget = fixedTarget
                                  ? forgeBoolean(fixedTarget, bodies[i].forgeBody, BooleanOperation::Union, error)
                                  : bodies[i].forgeBody;
                if (!fixedTarget)
                    return {};
            }
        }
        const int total = pattern.pattern.count * (pattern.pattern.kind == 0 ? pattern.pattern.count2 : 1);
        std::vector<ForgeBody> copies;
        const bool exposed =
            !pattern.pattern.featureOnly && (pattern.patternComponent >= 0 || hasDerivedPatternResults(index, bodies));
        int sourceComponents = 1;
        if (exposed && base.forgeBody && base.forgeBody->shells().size() > 1) {
            sourceComponents = forgePatternComponents(base.forgeBody, error).size();
            if (sourceComponents < 1)
                return {};
        }
        int placement = 0;
        for (occurrence = 0; occurrence < total; ++occurrence) {
            Kernel::cancellationPoint();
            if (pattern.pattern.excluded.contains(occurrence))
                continue;
            if (occurrence == 0 && pattern.pattern.featureOnly && pattern.secondBody >= 0)
                continue;
            auto s = sketches;
            auto b = bodies;
            QSet<int> dirty;
            if (occurrence > 0)
                for (int row = 0; row < selected.size(); ++row) {
                    const auto &p = parameters[selected[row]];
                    const auto &v = pattern.pattern.variations[row];
                    if (v.increment == 0)
                        continue;
                    const double value = p.value + double(occurrence) * v.increment;
                    if (!std::isfinite(value) || value < p.minimum || value > p.maximum ||
                        (p.integer && value != std::trunc(value)))
                        return fail(QStringLiteral("Istanza %1 · %2: valore %3 fuori intervallo [%4, %5].")
                                        .arg(occurrence + 1)
                                        .arg(p.label)
                                        .arg(value, 0, 'g', 12)
                                        .arg(p.minimum)
                                        .arg(p.maximum));
                    bool assigned = false;
                    for (int n : plan.order) {
                        if ((n < 0) != v.sketch || (n < 0 ? s[-1 - n].storyId : b[n].featureId) != v.ownerId)
                            continue;
                        const auto apply = [&](const QString &key, const QString &, auto &field, const QString &,
                                               double, double) {
                            if (key == v.parameter) {
                                field = static_cast<std::decay_t<decltype(field)>>(value);
                                assigned = true;
                            }
                        };
                        if (n < 0)
                            sketchNumbers(s[-1 - n], [&](const auto &c, const auto &key, const auto &name, auto &field,
                                                         const auto &unit, double lo, double hi) {
                                if (c.parameterId == v.parameterId)
                                    apply(QString::number(int(c.type)) + "/" + key, name, field, unit, lo, hi);
                            });
                        else
                            featureNumbers(b[n], apply);
                        if (assigned)
                            dirty.insert(n);
                    }
                    if (!assigned)
                        return fail(QStringLiteral("Parametro non risolvibile: %1.").arg(p.label));
                }
            // Ripetizione funzione: lo strumento e' rigenerato senza fondere la base.
            if (pattern.pattern.featureOnly && base.operation < 0) {
                b[pattern.firstBody].mergeOperation = 0;
                dirty.insert(pattern.firstBody);
            }
            for (int n : plan.order) {
                for (int d : plan.deps.value(n))
                    if (dirty.contains(d))
                        dirty.insert(n);
                if (!dirty.contains(n))
                    continue;
                Kernel::cancellationPoint();
                QString failure;
                if (n < 0) {
                    failure = resizeVariantPatterns(-1 - n, sketches[-1 - n], s, b);
                    if (failure.isEmpty())
                        failure = refreshVariantSketch(s[-1 - n], s, b);
                } else {
                    if (n == pattern.firstBody && pattern.pattern.featureOnly && base.operation >= 0)
                        continue;
                    auto result = b[n];
                    result.cachedGeometry = false;
                    result.display = {};
                    buildGeometry(result, n, s, b);
                    failure = result.error;
                    b[n] = std::move(result);
                    if (failure.isEmpty() && !hasGeometry(b[n]))
                        failure = QStringLiteral("Geometria assente");
                }
                if (!failure.isEmpty())
                    return fail(QStringLiteral("Istanza %1 · %2: %3")
                                    .arg(occurrence + 1)
                                    .arg(n < 0 ? s[-1 - n].name : b[n].name, failure));
            }
            ForgeBody shape = b[pattern.firstBody].forgeBody;
            if (pattern.pattern.featureOnly && base.operation >= 0) {
                std::vector<ForgeBody> tools;
                for (int i : QVector<int>{base.secondBody} + base.booleanTools) {
                    if (i < 0 || i >= index || !b[i].forgeBody)
                        return fail(QStringLiteral("Strumento non valido"));
                    tools.push_back(b[i].forgeBody);
                }
                shape = forgePatternBodies(tools, error);
            }
            if (!shape)
                return fail(QStringLiteral("Istanza %1 senza geometria valida.").arg(occurrence + 1));
            if (exposed) {
                const int count = shape->shells().size() == 1 ? 1 : forgePatternComponents(shape, error).size();
                if (count != sourceComponents)
                    return fail(
                        QStringLiteral("Istanza %1: la variazione cambia il numero di corpi separati della sorgente "
                                       "(%2 → %3). I riferimenti alle occorrenze non possono essere conservati; ripeti "
                                       "le singole funzioni.")
                            .arg(occurrence + 1)
                            .arg(sourceComponents)
                            .arg(count));
            }
            if (occurrence > 0)
                shape = forgePattern(shape, {placements.at(placement++)}, false, error);
            if (!shape)
                return {};
            copies.push_back(shape);
        }
        if (copies.empty())
            return fixedTarget && pattern.secondBody >= 0 ? fixedTarget
                                                          : fail(QStringLiteral("Nessuna occorrenza attiva."));
        auto result = forgePatternBodies(copies, error);
        if (result && fixedTarget)
            result = forgeBoolean(fixedTarget, result, operation, error);
        if (result && result->faces().empty())
            return fail(QStringLiteral("Il risultato della ripetizione è vuoto."));
        return result;
    } catch (const Kernel::OperationCancelled &) {
        throw;
    } catch (const std::exception &e) {
        return fail(QStringLiteral("Istanza %1: %2").arg(occurrence + 1).arg(QString::fromUtf8(e.what())));
    }
}
} // namespace ForgeCad
