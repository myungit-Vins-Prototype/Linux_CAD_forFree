#include "cad_export.h"

#include <QFileInfo>
#include <QBuffer>
#include <QDataStream>
#include <QLocale>
#include <QSaveFile>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cad_cuda_tessellation.h"
#include "fk_curve.h"
#include "fk_iges.h"
#include "fk_intersect.h"
#include "fk_step.h"
#include "fk_tessellate.h"
#include "fk_topology.h"

namespace ForgeCad {

namespace {

// L'esportazione non viene mai ridotta. Solo la copia destinata alla GPU ha
// un tetto, per evitare che una mesh estrema renda pesante la navigazione.
constexpr quint64 kPreviewTriangleLimit = 80000;
constexpr quint64 kPreviewPolygonLimit = 80000;

QVector3D displayPoint(const Kernel::Vec3 &point) {
    return QVector3D(float(point[0]), float(point[1]), float(point[2]));
}

quint64 previewReservoirSlot(quint64 seen) {
    // SplitMix64: rende il campione uniforme e deterministico su tutta la
    // mesh, invece di mostrare soltanto le prime facce incontrate.
    quint64 value = seen + 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return (value ^ (value >> 31)) % seen;
}

void appendPreviewTriangle(BodyDisplay &preview,
                           const Kernel::Vec3 &a, const Kernel::Vec3 &b, const Kernel::Vec3 &c,
                           const Kernel::Vec3 &na, const Kernel::Vec3 &nb, const Kernel::Vec3 &nc,
                           quint64 &seen, bool &limited) {
    ++seen;
    quint64 slot = preview.vertices.size() / 3;
    if (slot >= kPreviewTriangleLimit) {
        limited = true;
        slot = previewReservoirSlot(seen);
        if (slot >= kPreviewTriangleLimit) return;
    }
    const QVector3D points[] = {displayPoint(a), displayPoint(b), displayPoint(c)};
    const QVector3D normals[] = {displayPoint(na), displayPoint(nb), displayPoint(nc)};
    if (slot * 3 == quint64(preview.vertices.size())) {
        for (int i = 0; i < 3; ++i) {
            preview.vertices.append(points[i]);
            preview.normals.append(normals[i]);
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            preview.vertices[int(slot * 3 + quint64(i))] = points[i];
            preview.normals[int(slot * 3 + quint64(i))] = normals[i];
        }
    }
}

void appendPreviewPolygon(BodyDisplay &preview, const QVector<QVector3D> &polygon,
                          quint64 &seen, bool &limited) {
    if (polygon.size() < 3) return;
    ++seen;
    quint64 slot = preview.edges.size();
    if (slot >= kPreviewPolygonLimit) {
        limited = true;
        slot = previewReservoirSlot(seen);
        if (slot >= kPreviewPolygonLimit) return;
    }
    QVector<QVector3D> closed = polygon;
    closed.append(polygon.front());
    if (slot == quint64(preview.edges.size())) preview.edges.append(std::move(closed));
    else preview.edges[int(slot)] = std::move(closed);
}

struct ExportMeshes {
    std::vector<Kernel::Tessellation> bodies;
    StlExportOptions options;
    int attempts = 0;
    QString error;
};

bool validMeshOptions(const StlExportOptions &options) {
    return std::isfinite(options.deflection) && options.deflection > 0.0
        && std::isfinite(options.angle) && options.angle > 0.0 && options.angle <= 180.0
        && std::isfinite(options.maxEdgeLength) && options.maxEdgeLength >= 0.0;
}

// Dimensione del corpo piu' grande, indipendente dalla sua posizione nella
// scena. Include i box delle curve, anche dei bordi chiusi con un solo vertice.
double exportModelSize(const QVector<ExportBody> &bodies) {
    double size = 0.0;
    for (const ExportBody &body : bodies) {
        if (!body.body) continue;
        Kernel::Box box;
        for (Kernel::VertexId vertex : body.body->vertices()) box.add(body.body->vertex(vertex).point);
        for (Kernel::EdgeId id : body.body->edges()) {
            const Kernel::Edge &edge = body.body->edge(id);
            if (!edge.curve) continue;
            box.add(Kernel::curveBox(*edge.curve, edge.range));
        }
        size = std::max(size, box.diagonal());
    }
    return size;
}

// La mesh riuscita passa direttamente agli scrittori e all'anteprima: non
// si tassella di nuovo dopo la ricerca dei parametri. Tutte le facce di un
// corpo usano gli stessi campioni dei bordi, anche nei tentativi successivi.
ExportMeshes prepareExportMeshes(const QVector<ExportBody> &bodies, const StlExportOptions &requested,
                                 Kernel::SurfaceBatchEvaluator *accelerator) {
    ExportMeshes result;
    result.options = requested;
    if (!validMeshOptions(requested)) {
        result.error = QStringLiteral("Parametri della mesh non validi.");
        return result;
    }
    bool found = false;
    for (const ExportBody &body : bodies) found = found || bool(body.body);
    if (!found) {
        result.error = QStringLiteral("Non ci sono solidi o superfici da esportare.");
        return result;
    }
    StlExportOptions base = requested;
    if (requested.automaticRefinement) {
        const double size = exportModelSize(bodies);
        // Evita di saturare il limite di vertici prima di poter cercare una
        // mesh valida, anche con valori salvati da un documento molto piccolo.
        if (base.maxEdgeLength > 0.0) base.maxEdgeLength = std::max(base.maxEdgeLength, size / 100.0);
        base.deflection = std::max(base.deflection, size / 100000.0);
    }
    bool densityLimited = false;
    const int limit = requested.automaticRefinement ? 9 : 1;
    for (int attempt = 0; attempt < limit; ++attempt) {
        // Le triangolazioni dei contorni sottili non sono monotone rispetto
        // alla densita': prova sia piu' fine sia piu' grossolano. Per un limite
        // di vertici/lato, invece, passa direttamente al tentativo piu' largo.
        if (attempt % 2 == 1 && densityLimited) continue;
        const bool finer = attempt % 2 == 1;
        const double factor = attempt == 0 ? 1.0 : std::pow(2.0, (attempt + 1) / 2);
        result.options = base;
        if (attempt > 0) {
            result.options.deflection = base.deflection * (finer ? 1.0 / factor : factor);
            // Raffinare lo scarto basta a recuperare contorni sottili senza
            // imporre triangoli enormemente piu' numerosi sui piani.
            if (!finer) result.options.maxEdgeLength = base.maxEdgeLength * factor;
            result.options.angle = std::min(90.0, base.angle * (finer ? 1.0 / std::sqrt(factor) : std::sqrt(factor)));
        }
        Kernel::TessellationOptions tessellation;
        tessellation.deflection = result.options.deflection;
        tessellation.angle = result.options.angle * M_PI / 180.0;
        tessellation.maxEdgeLength = result.options.maxEdgeLength;
        tessellation.accelerator = accelerator;
        ++result.attempts;
        result.bodies.clear();
        result.error.clear();
        densityLimited = false;
        for (const ExportBody &body : bodies) {
            if (!body.body) continue;
            Kernel::Tessellation mesh;
            try {
                mesh = Kernel::tessellate(*body.body, tessellation);
            } catch (const std::bad_alloc &) {
                throw;
            } catch (const std::exception &failure) {
                result.error = QStringLiteral("Tassellazione non riuscita su %1: %2.")
                    .arg(body.name, QString::fromUtf8(failure.what()));
                break;
            }
            if (mesh.failedFaces > 0 || mesh.faces.size() != body.body->faces().size()) {
                result.error = QStringLiteral("Tassellazione non riuscita per %1 facce del corpo %2.")
                    .arg(std::max(1, mesh.failedFaces)).arg(body.name);
                break;
            }
            for (const Kernel::FaceMesh &face : mesh.faces) {
                if (face.triangles.empty()) {
                    result.error = QStringLiteral("La faccia F%1 del corpo %2 non contiene triangoli.").arg(face.face.index).arg(body.name);
                    break;
                }
                for (const auto &triangle : face.triangles) {
                    const Kernel::Vec3 &a = face.points[std::size_t(triangle[0])];
                    const Kernel::Vec3 &b = face.points[std::size_t(triangle[1])];
                    const Kernel::Vec3 &c = face.points[std::size_t(triangle[2])];
                    const double longest = std::max({Kernel::distance(a, b), Kernel::distance(b, c), Kernel::distance(c, a)});
                    if (!std::isfinite(longest)) {
                        result.error = QStringLiteral("Coordinate della mesh non valide sul corpo %1.").arg(body.name);
                        break;
                    }
                    if (tessellation.maxEdgeLength > 0.0 && longest > tessellation.maxEdgeLength * (1.0 + 1e-9)) {
                        densityLimited = true;
                        result.error = QStringLiteral("La densita' richiesta supera il limite di raffinamento su %1; aumentare il lato massimo.").arg(body.name);
                        break;
                    }
                }
                if (!result.error.isEmpty()) break;
            }
            if (!result.error.isEmpty()) break;
            result.bodies.push_back(std::move(mesh));
        }
        if (result.error.isEmpty()) return result;
    }
    result.bodies.clear();
    if (requested.automaticRefinement)
        result.error = QStringLiteral("Raffinamento automatico non riuscito dopo %1 tentativi. %2").arg(result.attempts).arg(result.error);
    return result;
}

}

QString exportSuffix(ExportFormat format) {
    return format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces ? QStringLiteral("igs") : QStringLiteral("step");
}

QString exportBodies(const QString &path, const QVector<ExportBody> &bodies, ExportFormat format) {
    const bool iges = format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces;
    std::vector<Kernel::ExchangeBody> exchange;
    for (const ExportBody &body : bodies) {
        if (!body.body && !body.curve) continue;
        Kernel::ExchangeBody e;
        // IGES e le stringhe STEP senza codifica sono ASCII: lettere accentate
        // senza accento (è -> e), il resto '_' (fk_step codifica comunque il resto).
        QString name = body.name;
        if (iges) {
            QString ascii;
            for (const QChar c : name.normalized(QString::NormalizationForm_D))
                if (c.unicode() < 128) ascii += c;
                else if (c.category() != QChar::Mark_NonSpacing) ascii += QLatin1Char('_');
            name = ascii;
        }
        e.name = name.toStdString();
        if (body.body) e.body = *body.body;
        e.curve = body.curve;
        if (body.curve) e.curveRange = body.curve->domain();
        e.hasColor = format != ExportFormat::StepAP203;
        const QColor color = body.color.isValid() ? body.color : QColor::fromRgbF(0.25, 0.65, 0.90);
        e.color[0] = color.redF(), e.color[1] = color.greenF(), e.color[2] = color.blueF();
        exchange.push_back(std::move(e));
    }
    if (exchange.empty()) return QStringLiteral("Non ci sono corpi da esportare.");
    std::string content;
    try {
        const std::string fileName = QFileInfo(path).fileName().toStdString();
        if (iges) {
            Kernel::IgesWriteOptions options;
            options.mode = format == ExportFormat::IgesSolids ? Kernel::IgesMode::Solids : Kernel::IgesMode::Surfaces;
            options.fileName = fileName;
            content = Kernel::writeIges(exchange, options);
        } else {
            Kernel::StepWriteOptions options;
            options.schema = format == ExportFormat::StepAP203 ? Kernel::StepSchema::AP203
                             : format == ExportFormat::StepAP214 ? Kernel::StepSchema::AP214
                                                                 : Kernel::StepSchema::AP242;
            options.fileName = fileName;
            content = Kernel::writeStep(exchange, options);
        }
    } catch (const std::exception &failure) {
        return QStringLiteral("Esportazione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content.data(), qint64(content.size())) != qint64(content.size()) || !file.commit())
        return QStringLiteral("Impossibile scrivere %1.").arg(path);
    return {};
}

StlBuildResult buildBinaryStl(const QVector<ExportBody> &bodies, const StlExportOptions &options) {
    StlBuildResult result;
    if (!validMeshOptions(options)) {
        result.error = QStringLiteral("Parametri della mesh STL non validi.");
        return result;
    }
    result.data.reserve(84);
    QBuffer buffer(&result.data);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    QByteArray header(80, '\0');
    const QByteArray signature("ForgeCAD binary STL");
    std::memcpy(header.data(), signature.constData(), std::size_t(signature.size()));
    stream.writeRawData(header.constData(), header.size());
    stream << quint32(0); // aggiornato quando il conteggio e' noto

    const std::unique_ptr<Kernel::SurfaceBatchEvaluator> accelerator = makeTessellationAccelerator();
    int solidBodies = 0;
    quint64 previewTrianglesSeen = 0, previewPolygonsSeen = 0;
    try {
        ExportMeshes prepared = prepareExportMeshes(bodies, options, accelerator.get());
        result.usedOptions = prepared.options;
        result.tessellationAttempts = prepared.attempts;
        if (!prepared.error.isEmpty()) {
            result.error = prepared.error;
            result.data.clear();
            return result;
        }
        std::size_t meshIndex = 0;
        for (const ExportBody &body : bodies) {
            if (!body.body) continue;
            ++solidBodies;
            const Kernel::Tessellation &mesh = prepared.bodies[meshIndex++];

            for (const Kernel::FaceMesh &face : mesh.faces)
                for (const std::array<int, 3> &triangle : face.triangles) {
                    if (result.triangleCount >= std::numeric_limits<quint32>::max()) {
                        result.error = QStringLiteral("La mesh supera il limite STL di 4.294.967.295 triangoli.");
                        result.data.clear();
                        return result;
                    }
                    const Kernel::Vec3 &a = face.points[std::size_t(triangle[0])];
                    const Kernel::Vec3 &b = face.points[std::size_t(triangle[1])];
                    const Kernel::Vec3 &c = face.points[std::size_t(triangle[2])];
                    Kernel::Vec3 normal = Kernel::cross(b - a, c - a);
                    const double length = Kernel::norm(normal);
                    if (length <= 1e-20) continue; // gli STL non devono contenere faccette degeneri ai poli
                    normal /= length;
                    appendPreviewTriangle(result.preview, a, b, c, normal, normal, normal,
                                          previewTrianglesSeen, result.previewLimited);
                    appendPreviewPolygon(result.preview, {displayPoint(a), displayPoint(b), displayPoint(c)},
                                         previewPolygonsSeen, result.previewLimited);
                    for (double value : {normal[0], normal[1], normal[2],
                                         a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]})
                        stream << float(value);
                    stream << quint16(0);
                    ++result.triangleCount;
                }
        }
    } catch (const std::exception &failure) {
        result.error = QStringLiteral("Tassellazione STL non riuscita: %1").arg(QString::fromUtf8(failure.what()));
        result.data.clear();
        return result;
    }
    buffer.close();
    if (stream.status() != QDataStream::Ok) {
        result.error = QStringLiteral("Memoria insufficiente durante la costruzione della mesh STL.");
        result.data.clear();
        return result;
    }
    if (solidBodies == 0 || result.triangleCount == 0) {
        result.error = QStringLiteral("Non ci sono solidi o superfici triangolabili da esportare in STL.");
        result.data.clear();
        return result;
    }
    const quint32 count = qToLittleEndian(quint32(result.triangleCount));
    std::memcpy(result.data.data() + 80, &count, sizeof(count));
    return result;
}

QString saveBinaryStl(const QString &path, const QByteArray &data) {
    if (data.size() < 84) return QStringLiteral("La mesh STL e' vuota.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return QStringLiteral("Impossibile scrivere %1.").arg(path);
    return {};
}

namespace {

struct QuadCandidate {
    int first = -1, second = -1;
    std::array<int, 4> vertices{};
    double score = 0.0;
};

bool convexQuad(const Kernel::FaceMesh &face, const std::array<int, 4> &quad) {
    double sign = 0.0;
    for (int i = 0; i < 4; ++i) {
        const Kernel::Vec2 &a = face.parameters[std::size_t(quad[std::size_t(i)])];
        const Kernel::Vec2 &b = face.parameters[std::size_t(quad[std::size_t((i + 1) % 4)])];
        const Kernel::Vec2 &c = face.parameters[std::size_t(quad[std::size_t((i + 2) % 4)])];
        const double turn = Kernel::cross(b - a, c - b);
        if (std::fabs(turn) <= 1e-14) return false;
        if (sign == 0.0) sign = turn;
        else if (sign * turn < 0.0) return false;
    }
    return true;
}

std::vector<QuadCandidate> quadCandidates(const Kernel::FaceMesh &face) {
    std::map<std::pair<int, int>, std::vector<int>> owners;
    const auto key = [](int a, int b) { return std::pair<int, int>{std::min(a, b), std::max(a, b)}; };
    for (std::size_t t = 0; t < face.triangles.size(); ++t) {
        const std::array<int, 3> &triangle = face.triangles[t];
        for (int k = 0; k < 3; ++k)
            owners[key(triangle[std::size_t(k)], triangle[std::size_t((k + 1) % 3)])].push_back(int(t));
    }
    std::vector<QuadCandidate> result;
    for (const auto &[shared, triangles] : owners) {
        if (triangles.size() != 2) continue;
        const auto opposite = [&](int triangle) {
            for (int vertex : face.triangles[std::size_t(triangle)])
                if (vertex != shared.first && vertex != shared.second) return vertex;
            return -1;
        };
        const int a = opposite(triangles[0]), b = opposite(triangles[1]);
        if (a < 0 || b < 0 || a == b) continue;
        std::array<int, 4> quad{a, shared.first, b, shared.second};
        if (!convexQuad(face, quad)) continue;
        bool repeatedPoint = false;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                repeatedPoint = repeatedPoint
                    || Kernel::squaredNorm(face.points[std::size_t(quad[std::size_t(i)])]
                                           - face.points[std::size_t(quad[std::size_t(j)])]) <= 1e-24;
        if (repeatedPoint) continue;
        const Kernel::Vec3 &p0 = face.points[std::size_t(quad[0])];
        const Kernel::Vec3 &p1 = face.points[std::size_t(quad[1])];
        const Kernel::Vec3 &p2 = face.points[std::size_t(quad[2])];
        Kernel::Vec3 averageNormal;
        for (int vertex : quad) averageNormal += face.normals[std::size_t(vertex)];
        if (Kernel::dot(Kernel::cross(p1 - p0, p2 - p1), averageNormal) < 0.0)
            std::swap(quad[1], quad[3]);
        const double diagonal = Kernel::distance(face.points[std::size_t(shared.first)], face.points[std::size_t(shared.second)]);
        double boundary = 0.0;
        for (int i = 0; i < 4; ++i)
            boundary += Kernel::distance(face.points[std::size_t(quad[std::size_t(i)])],
                                         face.points[std::size_t(quad[std::size_t((i + 1) % 4)])]);
        result.push_back({triangles[0], triangles[1], quad, diagonal / std::max(boundary, 1e-30)});
    }
    // Si accoppiano prima le coppie la cui diagonale condivisa e' lunga
    // rispetto al perimetro: normalmente sono le due meta' naturali del quad.
    std::sort(result.begin(), result.end(), [](const QuadCandidate &a, const QuadCandidate &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    return result;
}

QString validateMeshOptions(const StlExportOptions &options) {
    if (!validMeshOptions(options))
        return QStringLiteral("Parametri della mesh non validi.");
    return {};
}

}

ObjBuildResult buildQuadObj(const QVector<ExportBody> &bodies, const StlExportOptions &options) {
    ObjBuildResult result;
    result.error = validateMeshOptions(options);
    if (!result.error.isEmpty()) return result;
    QBuffer buffer(&result.data);
    buffer.open(QIODevice::WriteOnly);
    QTextStream stream(&buffer);
    stream.setLocale(QLocale::c());
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(12);
    stream << "# ForgeCAD quad-dominant OBJ\n# units: millimeter\n";
    const auto discard = [&] {
        // QTextStream puo' avere ancora testo in attesa: svuotalo e chiudi
        // il buffer prima di eliminare i dati, altrimenti il distruttore
        // riscriverebbe un OBJ parziale dopo un fallimento.
        stream.flush();
        buffer.close();
        result.data.clear();
    };

    const std::unique_ptr<Kernel::SurfaceBatchEvaluator> accelerator = makeTessellationAccelerator();
    quint64 nextVertex = 1, nextNormal = 1;
    quint64 previewTrianglesSeen = 0, previewPolygonsSeen = 0;
    int meshBodies = 0;
    try {
        ExportMeshes prepared = prepareExportMeshes(bodies, options, accelerator.get());
        result.usedOptions = prepared.options;
        result.tessellationAttempts = prepared.attempts;
        if (!prepared.error.isEmpty()) {
            result.error = prepared.error;
            discard();
            return result;
        }
        std::size_t meshIndex = 0;
        for (const ExportBody &body : bodies) {
            if (!body.body) continue;
            ++meshBodies;
            QString name = body.name;
            name.replace(QLatin1Char('\n'), QLatin1Char('_'));
            name.replace(QLatin1Char('\r'), QLatin1Char('_'));
            name.replace(QLatin1Char('#'), QLatin1Char('_'));
            stream << "o " << name << '\n';
            const Kernel::Tessellation &mesh = prepared.bodies[meshIndex++];

            // I punti dello stesso edge valutati dalle due superfici possono
            // differire entro la tolleranza B-rep. Si saldano spazialmente;
            // le normali restano per-corner, quindi gli spigoli vivi non
            // vengono visualmente smussati in Blender.
            double weldTolerance = std::max(1e-10, result.usedOptions.deflection * 1e-7);
            for (Kernel::EdgeId edge : body.body->edges())
                weldTolerance = std::max(weldTolerance, 1.01 * body.body->edge(edge).tolerance);
            using Cell = std::array<qint64, 3>;
            std::map<Cell, std::vector<std::pair<Kernel::Vec3, quint64>>> bodyVertices;
            std::map<quint64, Kernel::Vec3> bodyVertexPoints;
            struct BoundaryUse { int count = 0; quint64 from = 0, to = 0; };
            std::map<std::pair<quint64, quint64>, BoundaryUse> boundaryUses;
            const auto recordPolygon = [&](const std::vector<quint64> &polygon) {
                for (std::size_t i = 0; i < polygon.size(); ++i) {
                    const quint64 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
                    BoundaryUse &use = boundaryUses[{std::min(a, b), std::max(a, b)}];
                    ++use.count;
                    use.from = a; use.to = b;
                }
            };
            const auto cell = [&](const Kernel::Vec3 &point) {
                return Cell{qint64(std::floor(point[0] / weldTolerance)), qint64(std::floor(point[1] / weldTolerance)),
                            qint64(std::floor(point[2] / weldTolerance))};
            };
            auto vertexIndex = [&](const Kernel::Vec3 &point) {
                const Cell home = cell(point);
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz) {
                            const auto found = bodyVertices.find(Cell{home[0] + dx, home[1] + dy, home[2] + dz});
                            if (found == bodyVertices.end()) continue;
                            for (const auto &[candidate, index] : found->second)
                                if (Kernel::distance(candidate, point) <= weldTolerance) return index;
                        }
                const quint64 index = nextVertex++;
                bodyVertices[home].push_back({point, index});
                bodyVertexPoints[index] = point;
                stream << "v " << point[0] << ' ' << point[1] << ' ' << point[2] << '\n';
                return index;
            };
            for (const Kernel::FaceMesh &face : mesh.faces) {
                for (const std::array<int, 3> &triangle : face.triangles) {
                    const Kernel::Vec3 &a = face.points[std::size_t(triangle[0])];
                    const Kernel::Vec3 &b = face.points[std::size_t(triangle[1])];
                    const Kernel::Vec3 &c = face.points[std::size_t(triangle[2])];
                    if (Kernel::norm(Kernel::cross(b - a, c - a)) <= 1e-20) continue;
                    appendPreviewTriangle(result.preview, a, b, c,
                        face.normals[std::size_t(triangle[0])], face.normals[std::size_t(triangle[1])],
                        face.normals[std::size_t(triangle[2])], previewTrianglesSeen, result.previewLimited);
                }
                std::vector<quint64> positionIndices;
                positionIndices.reserve(face.points.size());
                for (const Kernel::Vec3 &point : face.points) positionIndices.push_back(vertexIndex(point));
                const quint64 normalBase = nextNormal;
                for (const Kernel::Vec3 &normal : face.normals) {
                    const double length = Kernel::norm(normal);
                    const Kernel::Vec3 unit = length > 0.0 ? normal / length : Kernel::Vec3(0, 0, 1);
                    stream << "vn " << unit[0] << ' ' << unit[1] << ' ' << unit[2] << '\n';
                }
                nextNormal += face.normals.size();
                stream << "g face_" << face.face.index << '\n';
                std::vector<char> used(face.triangles.size(), 0);
                for (const QuadCandidate &candidate : quadCandidates(face)) {
                    if (used[std::size_t(candidate.first)] || used[std::size_t(candidate.second)]) continue;
                    used[std::size_t(candidate.first)] = used[std::size_t(candidate.second)] = 1;
                    std::vector<quint64> polygon;
                    stream << 'f';
                    for (int vertex : candidate.vertices) {
                        const quint64 position = positionIndices[std::size_t(vertex)];
                        polygon.push_back(position);
                        stream << ' ' << position << "//" << normalBase + quint64(vertex);
                    }
                    stream << '\n';
                    recordPolygon(polygon);
                    QVector<QVector3D> previewPolygon;
                    for (int vertex : candidate.vertices)
                        previewPolygon.append(displayPoint(face.points[std::size_t(vertex)]));
                    appendPreviewPolygon(result.preview, previewPolygon, previewPolygonsSeen, result.previewLimited);
                    ++result.quadCount;
                }
                for (std::size_t t = 0; t < face.triangles.size(); ++t) {
                    if (used[t]) continue;
                    const std::array<int, 3> &triangle = face.triangles[t];
                    const Kernel::Vec3 &a = face.points[std::size_t(triangle[0])];
                    const Kernel::Vec3 &b = face.points[std::size_t(triangle[1])];
                    const Kernel::Vec3 &c = face.points[std::size_t(triangle[2])];
                    if (Kernel::norm(Kernel::cross(b - a, c - a)) <= 1e-20) continue;
                    std::vector<quint64> polygon;
                    stream << 'f';
                    for (int vertex : triangle) {
                        const quint64 position = positionIndices[std::size_t(vertex)];
                        polygon.push_back(position);
                        stream << ' ' << position << "//" << normalBase + quint64(vertex);
                    }
                    stream << '\n';
                    recordPolygon(polygon);
                    appendPreviewPolygon(result.preview,
                        {displayPoint(a), displayPoint(b), displayPoint(c)},
                        previewPolygonsSeen, result.previewLimited);
                    ++result.triangleCount;
                }
            }
            // Un solido deve arrivare in Blender chiuso. Nei poli e nei punti
            // singolari la tassellazione puo' produrre faccette di area nulla,
            // omesse sopra, lasciando piccoli loop di 3/4 lati. Li richiudiamo
            // con la direzione opposta alle facce adiacenti; le lamine restano
            // intenzionalmente aperte.
            if (!body.body->isSheet()) {
                std::map<quint64, std::vector<quint64>> adjacency;
                for (const auto &[edge, use] : boundaryUses)
                    if (use.count == 1) {
                        adjacency[edge.first].push_back(edge.second);
                        adjacency[edge.second].push_back(edge.first);
                    }
                std::set<std::pair<quint64, quint64>> visited;
                for (const auto &[start, neighbours] : adjacency) {
                    if (neighbours.size() != 2) continue;
                    const auto firstEdge = std::minmax(start, neighbours.front());
                    if (visited.count({firstEdge.first, firstEdge.second})) continue;
                    std::vector<quint64> loop{start};
                    quint64 previous = 0, current = start;
                    do {
                        const auto &nextCandidates = adjacency[current];
                        if (nextCandidates.size() != 2) { loop.clear(); break; }
                        const quint64 next = nextCandidates[0] != previous ? nextCandidates[0] : nextCandidates[1];
                        visited.insert({std::min(current, next), std::max(current, next)});
                        previous = current; current = next;
                        if (current != start) loop.push_back(current);
                    } while (current != start && loop.size() <= adjacency.size());
                    if (loop.size() < 3 || current != start) continue;
                    const BoundaryUse &direction = boundaryUses[{std::min(loop[0], loop[1]), std::max(loop[0], loop[1])}];
                    if (direction.from == loop[0] && direction.to == loop[1]) std::reverse(loop.begin(), loop.end());
                    stream << "# repaired solid boundary\n";
                    if (loop.size() == 4) {
                        QVector<QVector3D> previewPolygon;
                        for (quint64 vertex : loop) previewPolygon.append(displayPoint(bodyVertexPoints.at(vertex)));
                        appendPreviewPolygon(result.preview, previewPolygon, previewPolygonsSeen, result.previewLimited);
                        stream << "f " << loop[0] << ' ' << loop[1] << ' ' << loop[2] << ' ' << loop[3] << '\n';
                        ++result.quadCount;
                    } else {
                        for (std::size_t i = 1; i + 1 < loop.size(); ++i) {
                            appendPreviewPolygon(result.preview,
                                {displayPoint(bodyVertexPoints.at(loop[0])), displayPoint(bodyVertexPoints.at(loop[i])),
                                 displayPoint(bodyVertexPoints.at(loop[i + 1]))},
                                previewPolygonsSeen, result.previewLimited);
                            stream << "f " << loop[0] << ' ' << loop[i] << ' ' << loop[i + 1] << '\n';
                            ++result.triangleCount;
                        }
                    }
                }
            }
        }
    } catch (const std::exception &failure) {
        result.error = QStringLiteral("Tassellazione OBJ non riuscita: %1").arg(QString::fromUtf8(failure.what()));
        discard();
        return result;
    }
    stream.flush();
    buffer.close();
    if (stream.status() != QTextStream::Ok) {
        result.error = QStringLiteral("Memoria insufficiente durante la costruzione della mesh OBJ.");
        discard();
    } else if (meshBodies == 0 || result.quadCount + result.triangleCount == 0) {
        result.error = QStringLiteral("Non ci sono solidi o superfici esportabili in OBJ.");
        discard();
    }
    return result;
}

QString saveQuadObj(const QString &path, const QByteArray &data) {
    if (data.isEmpty()) return QStringLiteral("La mesh OBJ e' vuota.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return QStringLiteral("Impossibile scrivere %1.").arg(path);
    return {};
}

}
