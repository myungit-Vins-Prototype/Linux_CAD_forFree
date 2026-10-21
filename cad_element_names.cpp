#include "cad_element_names.h"

#include <QThreadPool>
#include <QThreadStorage>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <set>

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
    bool registered = false;
    quint64 featureId = 0; // zero: registrazione provvisoria di un'anteprima
    std::shared_ptr<const BodyHistory> declared;
    std::shared_ptr<const FaceAliases> declaredLabels;
    QVector<GenealogyOperand> operandInfo;
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

// Sotto registryMutex. Dopo la registrazione operands viene svuotato dal
// worker: operandInfo resta invece immutabile per tutta la vita della voce.
std::vector<ForgeBody> operationOperands(const Entry &entry) {
    if (!entry.registered) return entry.operands;
    std::vector<ForgeBody> operands;
    for (const auto &operand : entry.operandInfo) operands.push_back(operand.body.lock());
    return operands;
}

// Espande solo dichiarazioni gia' emesse dai risultati intermedi, fermandosi
// ai B-rep degli operandi della feature. Non confronta geometria e non entra
// nella storia di una feature che e' gia' un operando effettivo.
std::shared_ptr<const BodyHistory> rebaseDeclaredHistory(const Entry &entry, const Body &result, const std::vector<ForgeBody> &expected,
                                                      FaceAliases *labels = nullptr) {
    if (!entry.declared) return nullptr;
    const auto operands = operationOperands(entry);
    if (operands == expected) {
        if (labels && entry.declaredLabels) *labels = *entry.declaredLabels;
        return entry.declared;
    }
    if (operands.size() != std::size_t(entry.declared->operandCount())) return nullptr;
    std::vector<const Body *> raw;
    for (const auto &operand : expected) raw.push_back(operand.get());
    BodyHistory rebased = makeHistory(raw, result);
    std::set<const Body *> path{&result};
    std::function<bool(const ForgeBody &, EntityKind, int, HistoryRelation, std::vector<HistoryLink> &, std::vector<std::string> &)> follow;
    follow = [&](const ForgeBody &operand, EntityKind kind, int index, HistoryRelation relation, std::vector<HistoryLink> &links,
                 std::vector<std::string> &aliases) {
        if (!operand) return false;
        bool matched = false;
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (operand == expected[i]) {
                links.push_back({int(i), kind, index, relation});
                matched = true;
            }
        if (matched) return true;
        if (!path.insert(operand.get()).second) return false;
        bool complete = false;
        const auto found = registry.find(operand.get());
        if (found != registry.end() && found->second->body.lock() == operand && found->second->declared
            && found->second->declared->isDeclared(kind, index)) {
            if (labels && kind == EntityKind::Face && relation != HistoryRelation::Generated && found->second->declaredLabels
                && index < int(found->second->declaredLabels->size())) {
                const auto &emitted = found->second->declaredLabels->at(std::size_t(index));
                aliases.insert(aliases.end(), emitted.begin(), emitted.end());
            }
            const auto sources = operationOperands(*found->second);
            complete = true;
            for (const auto &link : found->second->declared->origins(kind, index)) {
                if (link.operand < 0 || link.operand >= int(sources.size())
                    || !follow(sources[std::size_t(link.operand)], link.kind, link.index, std::max(relation, link.relation), links, aliases)) {
                    complete = false;
                    break;
                }
            }
        }
        path.erase(operand.get());
        return complete;
    };
    bool any = false;
    for (int k = 0; k < 3; ++k) {
        const auto kind = EntityKind(k);
        for (int r = 0; r < rebased.resultSize(kind); ++r) {
            if (!entry.declared->isDeclared(kind, r)) continue;
            std::vector<HistoryLink> links;
            std::vector<std::string> aliases;
            if (labels && kind == EntityKind::Face && entry.declaredLabels && r < int(entry.declaredLabels->size()))
                aliases = entry.declaredLabels->at(std::size_t(r));
            bool complete = true;
            for (const auto &link : entry.declared->origins(kind, r))
                if (link.operand < 0 || link.operand >= int(operands.size())
                    || !follow(operands[std::size_t(link.operand)], link.kind, link.index, link.relation, links, aliases)) {
                    complete = false;
                    break;
                }
            if (complete) {
                rebased.declareOrigins(kind, r, std::move(links));
                if (labels && kind == EntityKind::Face && !aliases.empty()) {
                    std::sort(aliases.begin(), aliases.end());
                    aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());
                    if (labels->size() <= std::size_t(r)) labels->resize(std::size_t(r) + 1);
                    labels->at(std::size_t(r)) = std::move(aliases);
                }
                any = true;
            }
        }
    }
    return any ? std::make_shared<const BodyHistory>(std::move(rebased)) : nullptr;
}

std::shared_ptr<Entry> entryOf(const Body &body) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto it = registry.find(&body);
    if (it == registry.end()) return nullptr;
    if (!it->second->registered) return nullptr;
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
        if (!entry.declared && entry.sameIndex && operands.size() == 1 && operands[0])
            history = std::make_shared<BodyHistory>(sameIndexHistory(*operands[0], *body));
    } catch (const std::exception &) {
        history.reset();
    }
    try {
        if (!history) history = std::make_shared<BodyHistory>(resolveHistory(operands, *body, entry.declared.get()));
        FaceLabels labels;
        const auto faces = body->faces();
        if (entry.labeler && std::any_of(faces.begin(), faces.end(), [&](FaceId f) { return !history->isDeclared(EntityKind::Face, f.index); }))
            labels = entry.labeler(*body, *history);
        std::vector<const ElementNames *> raw;
        for (const auto &names : operandNames) raw.push_back(names.get());
        auto names = std::make_shared<ElementNames>(propagateNames(*body, *history, raw, entry.tag, labels.empty() ? nullptr : &labels,
                                                                 entry.declaredLabels.get()));
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
        if (body.extent != 0 && body.extentRef.kind == 5) add(body.extentRef.index);
        if (mergingFeature(body) && body.mergeOperation != 0)
            for (int merged : body.mergeBodies) add(merged);
        return operands;
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
            if (history.isDeclared(EntityKind::Face, face.index)) continue;
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

void registerDeclaredHistory(const ForgeBody &result, const std::vector<ForgeBody> &operands, BodyHistory history, FaceAliases labels) {
    if (!result) return;
    auto entry = std::make_shared<Entry>();
    entry->body = result;
    entry->operands = operands;
    entry->declared = std::make_shared<const BodyHistory>(std::move(history));
    if (!labels.empty()) entry->declaredLabels = std::make_shared<const FaceAliases>(std::move(labels));
    std::lock_guard<std::mutex> lock(registryMutex);
    for (auto it = registry.begin(); it != registry.end();)
        it = it->second->body.expired() ? registry.erase(it) : std::next(it);
    // I risultati gia' registrati sono immutabili, anche durante il calcolo
    // asincrono. Non sostituirne le dichiarazioni a posteriori.
    registry.emplace(result.get(), std::move(entry));
}

void registerImportedHistory(const ForgeBody &body) {
    if(!body)return;
    auto history=makeHistory({},*body);FaceAliases labels(std::size_t(history.resultSize(EntityKind::Face)));
    for(FaceId f:body->faces()){history.declareOrigins(EntityKind::Face,f.index,{});labels[std::size_t(f.index)]={"F"+std::to_string(f.index)};}
    for(EdgeId e:body->edges())history.declareOrigins(EntityKind::Edge,e.index,{});
    for(VertexId v:body->vertices())history.declareOrigins(EntityKind::Vertex,v.index,{});
    registerDeclaredHistory(body,{},std::move(history),std::move(labels));
}

QVector<int> featureGenealogyOperandIndices(const ExtrusionObject &body, int index, const QVector<ExtrusionObject> &bodies) {
    QVector<int> indices;
    if (!body.forgeBody) return indices;
    QVector<int> operands=historyOperands(body);
    // La ripetizione della funzione ricostruisce target e utensile dagli
    // operandi della funzione originale, prima di ripetere lo strumento.
    if(body.operation<0&&body.feature==BodyFeature::Pattern&&body.pattern.featureOnly
       &&body.firstBody>=0&&body.firstBody<index&&body.firstBody<bodies.size())
        operands=historyOperands(bodies.at(body.firstBody));
    for (int operand : operands) {
        if (operand < 0 || operand >= index || operand >= bodies.size()) continue;
        const ForgeBody &geometry = bodies.at(operand).forgeBody;
        if (geometry && geometry != body.forgeBody) indices.append(operand);
    }
    return indices;
}

DeclaredHistorySnapshot declaredHistorySnapshot(const ExtrusionObject &body, int index, const QVector<ExtrusionObject> &bodies) {
    DeclaredHistorySnapshot snapshot;
    if (!body.forgeBody) return snapshot;
    const QVector<int> indices = featureGenealogyOperandIndices(body, index, bodies);
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto found = registry.find(body.forgeBody.get());
    if (found == registry.end()) return snapshot;
    const Entry &entry = *found->second;
    if (entry.body.lock() != body.forgeBody || !entry.declared) return snapshot;
    std::vector<ForgeBody> expected;
    for (int operand : indices) expected.push_back(bodies.at(operand).forgeBody);
    FaceAliases labels;
    snapshot.history = rebaseDeclaredHistory(entry, *body.forgeBody, expected, &labels);
    if (!labels.empty()) snapshot.labels = std::make_shared<const FaceAliases>(std::move(labels));
    snapshot.operandIndices = indices;
    return snapshot;
}

void registerFeatureGenealogy(const ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
                              const QVector<ExtrusionObject> &bodies) {
    if (!body.forgeBody) return;
    auto entry = std::make_shared<Entry>();
    entry->body = body.forgeBody;
    entry->registered = true;
    entry->featureId = body.featureId;
    for (int operand : featureGenealogyOperandIndices(body, index, bodies)) {
        const ForgeBody &geometry = bodies.at(operand).forgeBody;
        entry->operands.push_back(geometry);
        entry->operandInfo.append({operand, bodies.at(operand).featureId, bodies.at(operand).name, geometry});
    }
    entry->sameIndex = body.feature == BodyFeature::Transform || body.feature == BodyFeature::Scale || body.feature == BodyFeature::Draft;
    entry->tag = body.featureId ? "f" + std::to_string(body.featureId) : "i" + std::to_string(index);
    if (!body.suppressed) {
        if (body.operation < 0 && body.feature == BodyFeature::Extrusion && body.sketchIndex >= 0 && body.sketchIndex < sketches.size())
            entry->labeler = [sketch = sketches.at(body.sketchIndex), distance = body.distance](const Body &shape, const BodyHistory &history) {
                return extrusionLabeler(sketch, distance)(shape, history);
            };
        else if (body.operation < 0 && body.feature == BodyFeature::Primitive)
            entry->labeler = primitiveLabeler(body.primitive);
    }
    std::lock_guard<std::mutex> lock(registryMutex);
    for (auto it = registry.begin(); it != registry.end();)
        it = it->second->body.expired() ? registry.erase(it) : std::next(it);
    const auto found = registry.find(body.forgeBody.get());
    if (found != registry.end()) {
        // Una feature con ID persistente resta immutabile. La conferma di
        // un'anteprima senza ID sostituisce invece la registrazione temporanea:
        // nessuna feature della storia ha ancora consumato quei nomi.
        if (found->second->registered && (found->second->featureId || !body.featureId)) return;
        FaceAliases labels;
        entry->declared = rebaseDeclaredHistory(*found->second, *body.forgeBody, entry->operands, &labels);
        if (!labels.empty()) entry->declaredLabels = std::make_shared<const FaceAliases>(std::move(labels));
        found->second = std::move(entry);
    } else registry.emplace(body.forgeBody.get(), std::move(entry));
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

QVector<GenealogyOperand> genealogyOperandsOf(const Body &body) {
    const auto entry = entryOf(body);
    return entry ? entry->operandInfo : QVector<GenealogyOperand>();
}

namespace {
QString entityId(EntityKind kind, int index) {
    return QStringLiteral("%1%2").arg(kind == EntityKind::Face ? QChar(u'F') : kind == EntityKind::Edge ? QChar(u'E') : QChar(u'V')).arg(index);
}
QString evolutionLabel(Evolution value, int pieces) {
    switch (value) {
    case Evolution::Unchanged: return QStringLiteral("invariata");
    case Evolution::Modified: return QStringLiteral("modificata");
    case Evolution::Split: return QStringLiteral("divisa (%1 pezzi)").arg(pieces);
    case Evolution::Merged: return QStringLiteral("unita");
    case Evolution::Deleted: return QStringLiteral("eliminata");
    }
    return {};
}
QString originLabel(const HistoryLink &link, const QVector<GenealogyOperand> &operands) {
    QString text = entityId(link.kind, link.index);
    if (link.operand < 0 || link.operand >= operands.size()) return text + QStringLiteral(" (operando non disponibile)");
    const auto &operand = operands.at(link.operand);
    if (const auto body = operand.body.lock()) {
        const QString name = elementName(*body, link.kind, link.index);
        if (!name.isEmpty()) text += QStringLiteral(" [%1]").arg(name);
    }
    return QStringLiteral("%1 di %2 (feature #%3, ID %4)").arg(text, operand.name).arg(operand.index).arg(operand.featureId);
}
}

QString entityEvolutionText(const Body &body, EntityKind kind, int index) {
    const auto history = featureHistoryOf(body);
    const auto operands = genealogyOperandsOf(body);
    QString text = entityId(kind, index);
    const QString name = elementName(body, kind, index);
    if (!name.isEmpty()) text += QStringLiteral(" [%1]").arg(name);
    if (!history) return text + QStringLiteral(": genealogia non disponibile");
    if (history->isDeclared(kind, index)) text += QStringLiteral(" (origine dichiarata dall'algoritmo)");
    const auto &origins = history->origins(kind, index);
    if (origins.empty()) {
        const auto entry = entryOf(body);
        if (kind == EntityKind::Face && entry && entry->declaredLabels && index >= 0 && index < int(entry->declaredLabels->size())) {
            QStringList roles;
            for (const auto &label : entry->declaredLabels->at(std::size_t(index))) {
                if (label == "cap.start") roles << QStringLiteral("coperchio iniziale");
                else if (label == "cap.end") roles << QStringLiteral("coperchio finale");
                else if (label.rfind("side.", 0) == 0) roles << QStringLiteral("fianco dal tratto %1").arg(QString::fromStdString(label.substr(5)));
                else roles << QString::fromStdString(label);
            }
            if (!roles.isEmpty()) return text + QStringLiteral(": generata, ") + roles.join(QStringLiteral("; "));
        }
        return text + QStringLiteral(": generata (nessuna provenienza identificata)");
    }
    QStringList inherited, generated;
    for (const auto &link : origins) {
        const QString source = originLabel(link, operands);
        if (link.relation == HistoryRelation::Generated) {
            const QString type = link.kind == EntityKind::Edge ? QStringLiteral("spigolo")
                : link.kind == EntityKind::Face ? QStringLiteral("faccia") : QStringLiteral("vertice");
            generated << type + QChar(u' ') + source;
        } else {
            const auto descendants = history->descendants(link.kind, link.operand, link.index);
            inherited << QStringLiteral("%1 da %2").arg(evolutionLabel(history->evolution(link.kind, link.operand, link.index), int(descendants.size())), source);
        }
    }
    if (!inherited.isEmpty()) text += QStringLiteral(": ") + inherited.join(QStringLiteral("; "));
    if (!generated.isEmpty()) text += (inherited.isEmpty() ? QStringLiteral(": generata da ") : QStringLiteral("; generata anche da ")) + generated.join(QStringLiteral("; "));
    return text;
}

QString featureEvolutionText(const Body &body) {
    const auto history = featureHistoryOf(body);
    if (!history) return QStringLiteral("Evoluzione delle entita': genealogia non disponibile.");
    QStringList text{QStringLiteral("Evoluzione delle entita' rispetto agli operandi precedenti:")};
    for (const auto face : body.faces()) text << entityEvolutionText(body, EntityKind::Face, face.index);
    for (const auto edge : body.edges()) text << entityEvolutionText(body, EntityKind::Edge, edge.index);
    for (const auto vertex : body.vertices()) text << entityEvolutionText(body, EntityKind::Vertex, vertex.index);
    const auto operands = genealogyOperandsOf(body);
    for (int operand = 0; operand < operands.size(); ++operand) {
        const auto source = operands.at(operand).body.lock();
        if (!source) continue;
        const auto deleted = [&](EntityKind kind, int index) {
            if (history->deleted(kind, operand, index))
                text << QStringLiteral("%1: eliminata / consumata").arg(originLabel({operand, kind, index, HistoryRelation::Same}, operands));
        };
        for (const auto face : source->faces()) deleted(EntityKind::Face, face.index);
        for (const auto edge : source->edges()) deleted(EntityKind::Edge, edge.index);
        for (const auto vertex : source->vertices()) deleted(EntityKind::Vertex, vertex.index);
    }
    return text.join(QChar(u'\n'));
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
