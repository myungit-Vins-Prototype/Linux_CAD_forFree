#include "cad_element_names.h"

#include <QThreadPool>
#include <QThreadStorage>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>

#include "cad_curve_solver.h"
#include "cad_feature_dependencies.h"
#include "cad_kernel.h"
#include "fk_curve_algo.h"
#include "fk_intersect.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

using Labeler = std::function<FaceLabels(const Body &, const BodyHistory &)>;

struct Entry {
    std::weak_ptr<const Body> body;
    std::vector<ForgeBody> operands;  // tengono in vita gli operandi finche' serve la genealogia
    bool sameIndex = false;
    std::string tag;
    Labeler labeler;
    std::once_flag once;
    std::shared_ptr<const BodyHistory> history;
    std::shared_ptr<const ElementNames> names;
};

std::mutex registryMutex;
std::map<const Body *, std::shared_ptr<Entry>> registry;

std::shared_ptr<Entry> entryOf(const Body &body) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto it = registry.find(&body);
    if (it == registry.end()) return nullptr;
    // Un indirizzo riusato da un B-rep nuovo non eredita la voce di uno morto.
    const std::shared_ptr<const Body> alive = it->second->body.lock();
    if (alive.get() != &body) return nullptr;
    return it->second;
}

void compute(Entry &entry) {
    const std::shared_ptr<const Body> body = entry.body.lock();
    if (!body) return;
    std::vector<const Body *> operands;
    std::vector<std::shared_ptr<const ElementNames>> operandNames;
    for (const ForgeBody &operand : entry.operands) {
        operands.push_back(operand.get());
        operandNames.push_back(operand ? elementNamesOf(*operand) : nullptr);
    }
    std::shared_ptr<const BodyHistory> history;
    try {
        if (entry.sameIndex && operands.size() == 1 && operands[0])
            history = std::make_shared<BodyHistory>(sameIndexHistory(*operands[0], *body));
    } catch (const std::exception &) {
        history.reset();
    }
    try {
        if (!history) history = std::make_shared<BodyHistory>(inferHistory(operands, *body));
        FaceLabels labels;
        if (entry.labeler) labels = entry.labeler(*body, *history);
        std::vector<const ElementNames *> raw;
        for (const auto &names : operandNames) raw.push_back(names.get());
        auto names = std::make_shared<ElementNames>(propagateNames(*body, *history, raw, entry.tag, labels.empty() ? nullptr : &labels));
        names->body = body.get();
        entry.history = history;
        entry.names = names;
    } catch (const std::exception &) {
        // Senza genealogia i riferimenti si risolvono come prima (ID e punto).
        entry.history.reset();
        entry.names.reset();
    }
    entry.operands.clear();  // la genealogia e' calcolata: gli operandi non servono piu'
}

std::shared_ptr<Entry> computed(const Body &body) {
    std::shared_ptr<Entry> entry = entryOf(body);
    if (!entry) return nullptr;
    std::call_once(entry->once, [&] { compute(*entry); });
    return entry;
}

// Operandi della genealogia: gli stadi che la feature modifica o consuma.
QVector<int> historyOperands(const ExtrusionObject &body) {
    QVector<int> operands;
    const auto add = [&](int index) {
        if (index >= 0 && !operands.contains(index)) operands.append(index);
    };
    if (body.operation >= 0) {
        add(body.firstBody);
        add(body.secondBody);
        for (int tool : body.booleanTools) add(tool);
        return operands;
    }
    switch (body.feature) {
    case BodyFeature::Extrusion:
    case BodyFeature::Revolution:
    case BodyFeature::Sweep:
    case BodyFeature::Loft:
        if (mergingFeature(body) && body.mergeOperation != 0)
            for (int merged : body.mergeBodies) add(merged);
        return operands;
    case BodyFeature::Sew:
        add(body.firstBody);
        for (int tool : body.booleanTools) add(tool);
        return operands;
    case BodyFeature::SheetTrim:
        add(body.firstBody);
        if (body.trimBoth) add(body.secondBody);
        return operands;
    case BodyFeature::Primitive:
    case BodyFeature::Imported:
    case BodyFeature::DatumPlane:
    case BodyFeature::Helix:
    case BodyFeature::Ruled:
    case BodyFeature::PlanarSurface:
    case BodyFeature::BoundarySurface:
    case BodyFeature::FillSurface:
    case BodyFeature::ProjectedCurve:
    case BodyFeature::ExtractedCurve:
    case BodyFeature::SpatialSketch:
        return operands;
    default:
        // Feature dello stesso corpo (raccordi, smussi, estensioni, scala,
        // spostamenti, ripetizioni, offset, guscio, spessore, sformo...).
        add(body.firstBody);
        return operands;
    }
}

// Etichette delle facce nuove di un'estrusione: coperchi lungo la direzione e
// fianchi per entita' dello schizzo (segmento L<i>, curva C<i>.<tratto>).
Labeler extrusionLabeler(const SketchObject &sketch, double extrusionDistance) {
    struct Keyed {
        std::string key;
        ProfileSegment segment;
    };
    std::vector<Keyed> keyed;
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index)) continue;
        const SketchSegment &s = sketch.segments.at(index);
        const Vec2 a(s.first.x(), s.first.y()), b(s.second.x(), s.second.y());
        const double length = Kernel::distance(a, b);
        if (length <= 1e-12) continue;
        ProfileSegment segment;
        segment.curve = std::make_shared<Line<2>>(a, (b - a) / length);
        segment.range = Interval{0.0, length};
        keyed.push_back({"L" + std::to_string(index), segment});
    }
    for (int index = 0; index < sketch.curves.size(); ++index) {
        if (sketch.curves.at(index).construction) continue;
        int piece = 0;
        for (ProfileSegment &segment : curveGeometry(sketch.curves.at(index)))
            keyed.push_back({"C" + std::to_string(index) + "." + std::to_string(piece++), std::move(segment)});
    }
    const Frame3 frame = sketchAxes(sketch);
    Vec3 direction = extrusionVector(sketch, extrusionDistance);
    if (norm(direction) > 0) direction = direction / norm(direction);
    return [keyed, frame, direction](const Body &body, const BodyHistory &history) {
        FaceLabels labels;
        for (FaceId face : body.faces()) {
            if (!history.origins(EntityKind::Face, face.index).empty()) continue;  // ereditata
            Vec3 point;
            Vec2 uv;
            if (!faceInteriorPoint(body, face, point, &uv)) continue;
            const Face &f = body.face(face);
            Vec3 normal = f.surface->normal(uv.x(), uv.y());
            if (!f.sense) normal = -normal;
            std::string label;
            const double along = dot(normal, direction);
            if (f.surface->type() == SurfaceType::Plane && std::abs(along) > 1 - 1e-9) {
                label = along > 0 ? "cap.end" : "cap.start";
            } else {
                const Vec3 local = frame.toLocal(point);
                const Vec2 p(local.x(), local.y());
                double best = 1e-6 * (1.0 + norm(p));
                for (const Keyed &k : keyed) {
                    const double d = projectPoint(*k.segment.curve, p, k.segment.range).distance;
                    if (d <= best) best = d, label = "side." + k.key;
                }
            }
            if (label.empty()) continue;
            if (std::size_t(face.index) >= labels.size()) labels.resize(std::size_t(face.index) + 1);
            labels[std::size_t(face.index)] = label;
        }
        return labels;
    };
}

// Primitive: le facce piane per direzione della normale nel sistema della
// primitiva (+x, -z...), le altre per tipo di superficie.
Labeler primitiveLabeler(const PrimitiveParameters &primitive) {
    const Frame3 frame = primitiveAxes(primitive);
    return [frame](const Body &body, const BodyHistory &) {
        FaceLabels labels;
        for (FaceId face : body.faces()) {
            const Face &f = body.face(face);
            std::string label;
            if (f.surface->type() == SurfaceType::Plane) {
                Vec3 point;
                Vec2 uv;
                if (!faceInteriorPoint(body, face, point, &uv)) continue;
                Vec3 normal = f.surface->normal(uv.x(), uv.y());
                if (!f.sense) normal = -normal;
                const Vec3 n = frame.directionToLocal(normal);
                const char *axes[3] = {"x", "y", "z"};
                for (int k = 0; k < 3; ++k)
                    if (std::abs(n[k]) > 1 - 1e-9) label = std::string(n[k] > 0 ? "+" : "-") + axes[k];
                if (label.empty()) continue;
            } else {
                static const char *types[] = {"plane", "cylinder", "cone", "sphere", "torus", "extrusion", "revolution", "bspline"};
                label = types[int(f.surface->type())];
            }
            if (std::size_t(face.index) >= labels.size()) labels.resize(std::size_t(face.index) + 1);
            labels[std::size_t(face.index)] = label;
        }
        return labels;
    };
}

QThreadStorage<QStringList> warnings;

}

void registerFeatureGenealogy(const ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
                              const QVector<ExtrusionObject> &bodies) {
    if (!body.forgeBody) return;
    auto entry = std::make_shared<Entry>();
    entry->body = body.forgeBody;
    for (int operand : historyOperands(body)) {
        if (operand < 0 || operand >= index || operand >= bodies.size()) continue;
        const ForgeBody &geometry = bodies.at(operand).forgeBody;
        if (geometry && geometry != body.forgeBody) entry->operands.push_back(geometry);
    }
    entry->sameIndex = body.feature == BodyFeature::Transform || body.feature == BodyFeature::Scale || body.feature == BodyFeature::Draft;
    entry->tag = body.featureId ? "f" + std::to_string(body.featureId) : "i" + std::to_string(index);
    if (!body.suppressed) {
        if (body.operation < 0 && body.feature == BodyFeature::Extrusion && body.sketchIndex >= 0 && body.sketchIndex < sketches.size())
            entry->labeler = extrusionLabeler(sketches.at(body.sketchIndex), body.distance);
        else if (body.operation < 0 && body.feature == BodyFeature::Primitive)
            entry->labeler = primitiveLabeler(body.primitive);
    }
    std::lock_guard<std::mutex> lock(registryMutex);
    for (auto it = registry.begin(); it != registry.end();)
        it = it->second->body.expired() ? registry.erase(it) : std::next(it);
    // Lo stesso B-rep (feature soppressa, anteprima confermata) tiene la voce che ha.
    if (!registry.emplace(body.forgeBody.get(), std::move(entry)).second) return;
    // La genealogia si prepara in sottofondo (un solo thread a bassa priorita',
    // per non togliere tempo alle anteprime): alla prima scelta di un
    // riferimento i nomi sono gia' pronti. Le anteprime scartate nel frattempo
    // non si calcolano.
    // Locale statico: distrutto (aspettando il lavoro in corso) prima del registro.
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    const std::weak_ptr<const Body> weak = body.forgeBody;
    pool.start([weak] {
        if (const std::shared_ptr<const Body> alive = weak.lock()) computed(*alive);
    }, -1);
}

std::shared_ptr<const ElementNames> elementNamesOf(const Body &body) {
    const std::shared_ptr<Entry> entry = computed(body);
    return entry ? entry->names : nullptr;
}

std::shared_ptr<const BodyHistory> featureHistoryOf(const Body &body) {
    const std::shared_ptr<Entry> entry = computed(body);
    return entry ? entry->history : nullptr;
}

QString elementName(const Body &body, EntityKind kind, int index) {
    const std::shared_ptr<const ElementNames> names = elementNamesOf(body);
    return names ? QString::fromStdString(names->primary(kind, index)) : QString();
}

void clearReferenceWarnings() { warnings.localData().clear(); }

void addReferenceWarning(const QString &warning) {
    QStringList &list = warnings.localData();
    if (!list.contains(warning)) list.append(warning);
}

QStringList takeReferenceWarnings() {
    QStringList list = warnings.localData();
    warnings.localData().clear();
    return list;
}

}
