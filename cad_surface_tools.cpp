#include "cad_surface_tools.h"
#include "cad_datum.h"
#include "cad_element_names.h"
#include "cad_topology_ref.h"
#include "fk_surface_tools.h"
#include "fk_sheet.h"
#include "fk_sew.h"
#include "fk_intersect.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ForgeCad {
namespace {
void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
} // namespace
QByteArray surfaceBasisHash(const ForgeBody &body) {
    if (!body || body->faces().size() != 1)
        return {};
    const auto *s = dynamic_cast<const Kernel::BSplineSurface *>(body->face(body->faces().front()).surface.get());
    if (!s)
        return {};
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << s->uDegree() << s->vDegree() << s->uPoleCount() << s->vPoleCount();
    for (const auto *knots : {&s->uKnots(), &s->vKnots()}) {
        out << quint32(knots->size());
        for (double k : *knots)
            out << k;
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
void buildSurfaceTool(ExtrusionObject &body, int index, const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies) {
    using namespace Kernel;
    try {
        const auto &p = body.surfaceTool;
        require(p.mode >= 0 && p.mode <= 2, "Tipo di funzione superficie non valido.");
        Body result;
        BodyHistory history;
        const auto base = [&] {
            require(body.firstBody >= 0 && body.firstBody < index && body.firstBody < bodies.size() &&
                        bool(bodies[body.firstBody].forgeBody),
                    "Superficie sorgente non disponibile.");
            return bodies[body.firstBody].forgeBody;
        };
        if (p.mode == 1) {
            const auto family = [&](const QVector<GeometryRef> &refs) {
                std::vector<PathSegment> curves;
                for (const auto &ref : refs) {
                    std::vector<PathSegment> pieces;
                    QString error;
                    require(geometryRefPath(ref, index, sketches, bodies, pieces, &error), qPrintable(error));
                    require(pieces.size() == 1,
                            "Ogni riga della rete deve indicare una singola curva continua; usare Estrai bordo3D per concatenare.");
                    curves.push_back(pieces.front());
                }
                return curves;
            };
            FaceAliases labels;
            result = curveNetworkSheet(family(body.ruledFirst), family(body.ruledSecond), p.tolerance, &labels);
            body.forgeBody = std::make_shared<const Body>(std::move(result));
            registerDeclaredHistory(body.forgeBody, {}, makeHistory({}, *body.forgeBody), std::move(labels));
            body.notice = QStringLiteral("Rete a celle di Coons, continuita' interna G0. Curve interpolate entro %1 mm.").arg(p.tolerance);
        } else if (p.mode == 0) {
            const auto source = base();
            require(body.blendEdges.size() == 1, "Scegliere un solo bordo libero da estendere.");
            const auto edge = resolveEdgeReference(*source, body.blendEdges.front(), 1e-5);
            require(edge.valid(), "Bordo di estensione mancante o ambiguo.");
            const auto targets = p.targets.isEmpty() ? QVector<GeometryRef>{body.extentRef} : p.targets;
            require(!targets.isEmpty() && targets.size() <= 256, "Scegliere da1a256 riferimenti finali.");
            std::vector<Body> pieces;
            QMap<int, std::vector<FaceId>> faces;
            for (const auto &ref : targets) {
                if (ref.kind == 5) {
                    require(ref.index >= 0 && ref.index < index && ref.index < bodies.size(), "Riferimento finale non disponibile.");
                    const auto other = bodies[ref.index].forgeBody;
                    require(bool(other), "Riferimento finale privo di geometria.");
                    const auto face = resolveFaceReference(*other, ref.point, 1e-5);
                    require(face.valid(), "Faccia finale mancante o ambigua.");
                    auto &selected = faces[ref.index];
                    require(std::find(selected.begin(), selected.end(), face) == selected.end(), "Faccia finale duplicata.");
                    selected.push_back(face);
                } else {
                    ResolvedRef target;
                    QString error;
                    require(resolveGeometryRef(ref, index, sketches, bodies, target, &error) && target.hasPlane,
                            qPrintable(error.isEmpty() ? QStringLiteral("Scegliere piani o facce finali.") : error));
                    Box box;
                    for (const auto v : source->vertices()) box.add(source->vertex(v).point);
                    pieces.push_back(makePlaneSheet(normalFrame(target.direction, target.point),
                        std::max(1.0, box.diagonal() + distance((box.lo + box.hi) / 2, target.point) + 2 * p.maximum)));
                }
            }
            for (auto it = faces.cbegin(); it != faces.cend(); ++it)
                pieces.push_back(facesAsSheet(*bodies[it.key()].forgeBody, it.value()));
            std::vector<const Body *> operands;
            for (const auto &piece : pieces) operands.push_back(&piece);
            const Body cutter = pieces.size() == 1 ? pieces.front() : sewSheets(operands, p.tolerance, false).body;
            result = extendSheetTo(*source, edge, cutter, p.maximum, body.extendLinear, p.tolerance, &history);
            body.forgeBody = std::make_shared<const Body>(std::move(result));
            registerDeclaredHistory(body.forgeBody, {source}, std::move(history));
        } else {
            const auto source = base();
            require(p.basis.size() == 32 && surfaceBasisHash(source) == p.basis,
                    "La griglia NURBS a monte e' cambiata: riselezionare i poli.");
            std::vector<SurfacePoleChange> edits;
            for (const auto &e : p.edits)
                edits.push_back({e.u, e.v, Vec3(e.dx, e.dy, e.dz), e.weightFactor});
            result = editSheetPoles(*source, edits, p.tolerance, &history);
            body.forgeBody = std::make_shared<const Body>(std::move(result));
            registerDeclaredHistory(body.forgeBody, {source}, std::move(history));
        }
        body.solid = false;
    } catch (const std::exception &e) {
        body.forgeBody.reset();
        body.error = QString::fromUtf8(e.what());
    }
}
QByteArray surfaceToolsExtras(const QVector<ExtrusionObject> &bodies) {
    QJsonArray records;
    for (int i = 0; i < bodies.size(); ++i) {
        const auto &b = bodies[i];
        if (b.feature != BodyFeature::SurfaceTools && b.fillFaceContinuities.isEmpty())
            continue;
        const auto &p = b.surfaceTool;
        QJsonArray edits, contacts;
        for (const auto &e : p.edits)
            edits.append(QJsonArray{e.u, e.v, e.dx, e.dy, e.dz, e.weightFactor});
        for (int c : b.fillFaceContinuities)
            contacts.append(c);
        QJsonObject record{{"index", i},
                                   {"mode", p.mode},
                                   {"maximum", p.maximum},
                                   {"tolerance", p.tolerance},
                                   {"basis", QString::fromLatin1(p.basis.toHex())},
                                   {"edits", edits},
                                   {"contacts", contacts}};
        if (!p.targets.isEmpty()) {
            QJsonArray targets;
            for (const auto &r : p.targets)
                targets.append(QJsonArray{r.kind, r.index, QString::number(r.featureId), r.point.x, r.point.y, r.point.z,
                    r.point.subshape, r.point.geometry, r.point.context, r.point.role, r.point.name});
            record["targets"] = targets;
        }
        records.append(record);
    }
    return records.isEmpty() ? QByteArray()
                             : QJsonDocument(QJsonObject{{"schema", 1}, {"features", records}}).toJson(QJsonDocument::Compact);
}
QString applySurfaceToolsExtras(const QByteArray &bytes, QVector<ExtrusionObject> &output) {
    auto bodies = output;
    try {
        QSet<int> present;
        if (!bytes.isEmpty()) {
            require(bytes.size() <= 4 * 1024 * 1024, "Estensioni superfici oltre4MiB.");
            QJsonParseError error;
            const auto root = QJsonDocument::fromJson(bytes, &error).object();
            require(error.error == QJsonParseError::NoError && root.size() == 2 && root["schema"].toDouble(-1) == 1 &&
                        root["features"].isArray(),
                    "Estensioni superfici danneggiate.");
            const auto integer = [](const QJsonValue &v, int low, int high) {
                const double d = v.toDouble(NAN);
                require(std::isfinite(d) && d == std::floor(d) && d >= low && d <= high, "Intero superficie non valido.");
                return int(d);
            };
            for (const auto &entry : root["features"].toArray()) {
                require(entry.isObject(), "Record superficie non valido.");
                const auto o = entry.toObject();
                require(o.size() == 7 || (o.size() == 8 && o.contains("targets")), "Record superficie incompleto.");
                const int i = integer(o["index"], 0, bodies.size() - 1);
                require(!present.contains(i), "Estensione superficie duplicata.");
                present.insert(i);
                auto &b = bodies[i];
                require(b.feature == BodyFeature::SurfaceTools || b.feature == BodyFeature::FillSurface,
                        "Estensione su un tipo di funzione incompatibile.");
                auto &p = b.surfaceTool;
                p.mode = integer(o["mode"], 0, 2);
                p.maximum = o["maximum"].toDouble(NAN);
                p.tolerance = o["tolerance"].toDouble(NAN);
                require(std::isfinite(p.maximum) && p.maximum > 0 && p.maximum <= 1e6 && std::isfinite(p.tolerance) &&
                            p.tolerance >= 1e-9 && p.tolerance <= 0.01,
                        "Limiti superficie non validi.");
                require(o["basis"].isString(), "Base NURBS assente.");
                const auto hex = o["basis"].toString().toLatin1();
                p.basis = QByteArray::fromHex(hex);
                require((hex.isEmpty() || hex.size() == 64) && p.basis.toHex() == hex, "Impronta base NURBS non valida.");
                require(o["edits"].isArray() && o["contacts"].isArray(), "Tabelle superficie mancanti.");
                require(o["edits"].toArray().size() <= 4096, "Troppi poli modificati.");
                p.edits.clear();
                QSet<quint64> poles;
                for (const auto &row : o["edits"].toArray()) {
                    require(row.isArray() && row.toArray().size() == 6, "Polo incompleto.");
                    const auto a = row.toArray();
                    SurfacePoleEdit e;
                    e.u = integer(a[0], 0, 4095);
                    e.v = integer(a[1], 0, 4095);
                    e.dx = a[2].toDouble(NAN);
                    e.dy = a[3].toDouble(NAN);
                    e.dz = a[4].toDouble(NAN);
                    e.weightFactor = a[5].toDouble(NAN);
                    const quint64 key = quint64(e.u) * 4096 + e.v;
                    require(!poles.contains(key) && std::isfinite(e.dx) && std::isfinite(e.dy) && std::isfinite(e.dz) &&
                                std::isfinite(e.weightFactor) && e.weightFactor > 0 && e.weightFactor <= 1e6,
                            "Polo duplicato o valori non validi.");
                    poles.insert(key);
                    p.edits.append(e);
                }
                p.targets.clear();
                if (o.contains("targets")) {
                    require(o["targets"].isArray() && !o["targets"].toArray().isEmpty() && o["targets"].toArray().size() <= 256 &&
                            b.feature == BodyFeature::SurfaceTools && p.mode == 0, "Destinazioni superficie non valide.");
                    QSet<QString> selected;
                    for (const auto &value : o["targets"].toArray()) {
                        require(value.isArray() && value.toArray().size() == 11, "Riferimento finale incompleto.");
                        const auto a = value.toArray();
                        GeometryRef r;
                        r.kind = integer(a[0], 1, 8);
                        require(r.kind == 1 || r.kind == 5 || r.kind == 8, "Tipo di riferimento finale non valido.");
                        r.index = integer(a[1], 0, r.kind == 1 ? 2 : i - 1);
                        bool idValid = false;
                        require(a[2].isString(), "Identita' del riferimento finale assente.");
                        r.featureId = a[2].toString().toULongLong(&idValid);
                        require(idValid && QString::number(r.featureId) == a[2].toString(), "Identita' del riferimento finale non valida.");
                        r.point.x = a[3].toDouble(NAN); r.point.y = a[4].toDouble(NAN); r.point.z = a[5].toDouble(NAN);
                        require(std::isfinite(r.point.x) && std::isfinite(r.point.y) && std::isfinite(r.point.z), "Punto finale non valido.");
                        r.point.subshape = integer(a[6], -1, 2147483647);
                        r.point.geometry = integer(a[7], -1, 2147483647);
                        r.point.context = integer(a[8], -1, 2147483647);
                        r.point.role = integer(a[9], 0, 1);
                        require(a[10].isString() && a[10].toString().size() <= 4096, "Nome topologico finale non valido.");
                        r.point.name = a[10].toString();
                        const QString key = QStringLiteral("%1/%2/%3").arg(r.kind).arg(r.index).arg(r.kind == 5 ? r.point.subshape : -1);
                        require(!selected.contains(key), "Riferimento finale duplicato.");
                        selected.insert(key);
                        p.targets.append(r);
                    }
                    b.extentRef = p.targets.front();
                }
                b.fillFaceContinuities.clear();
                for (const auto &c : o["contacts"].toArray())
                    b.fillFaceContinuities.append(integer(c, -1, 2));
                require(b.fillFaceContinuities.isEmpty() ||
                            (b.feature == BodyFeature::FillSurface && b.fillFaceContinuities.size() == b.ruledSecond.size()),
                        "Continuita' per faccia incompatibili.");
                if (b.feature == BodyFeature::SurfaceTools) {
                    require(b.fillFaceContinuities.isEmpty(), "Contatti fill su operazione superficie.");
                    if (p.mode == 0)
                        require(b.blendEdges.size() == 1 && b.extentRef.kind >= 0, "Estensione senza bordo o destinazione.");
                    if (p.mode == 1)
                        require(b.ruledFirst.size() >= 2 && b.ruledFirst.size() <= 16 && b.ruledSecond.size() >= 2 &&
                                    b.ruledSecond.size() <= 16,
                                "Rete senza due famiglie valide.");
                    if (p.mode == 2)
                        require(p.basis.size() == 32 && !p.edits.isEmpty(), "Modifica poli senza base o modifiche.");
                }
            }
        }
        for (int i = 0; i < bodies.size(); ++i)
            require(bodies[i].feature != BodyFeature::SurfaceTools || present.contains(i), "Funzione superficie senza parametri.");
        output = std::move(bodies);
        return {};
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
}
} // namespace ForgeCad
