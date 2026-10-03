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
    if (!(options.deflection > 0.0) || !(options.angle > 0.0 && options.angle <= 180.0)
        || options.maxEdgeLength < 0.0) {
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

    Kernel::TessellationOptions tessellation;
    tessellation.deflection = options.deflection;
    tessellation.angle = options.angle * M_PI / 180.0;
    tessellation.maxEdgeLength = options.maxEdgeLength;
    const std::unique_ptr<Kernel::SurfaceBatchEvaluator> accelerator = makeTessellationAccelerator();
    tessellation.accelerator = accelerator.get();
    int solidBodies = 0;
    quint64 previewTrianglesSeen = 0, previewPolygonsSeen = 0;
    try {
        for (const ExportBody &body : bodies) {
            if (!body.body) continue;
            ++solidBodies;
            const Kernel::Tessellation mesh = Kernel::tessellate(*body.body, tessellation);
            if (mesh.failedFaces > 0) {
                result.error = QStringLiteral("Tassellazione STL non riuscita per %1 facce del corpo %2.")
                    .arg(mesh.failedFaces).arg(body.name);
                result.data.clear();
                return result;
            }
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
                    if (options.maxEdgeLength > 0.0) {
                        const double longest = std::max({Kernel::distance(a, b), Kernel::distance(b, c), Kernel::distance(c, a)});
                        if (longest > options.maxEdgeLength * (1.0 + 1e-9)) {
                            result.error = QStringLiteral(
                                "La densita' richiesta supera il limite di raffinamento su %1; aumentare il lato massimo.").arg(body.name);
                            result.data.clear();
                            return result;
                        }
                    }
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
    if (!(options.deflection > 0.0) || !(options.angle > 0.0 && options.angle <= 180.0)
        || options.maxEdgeLength < 0.0)
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

    Kernel::TessellationOptions tessellation;
    tessellation.deflection = options.deflection;
    tessellation.angle = options.angle * M_PI / 180.0;
    tessellation.maxEdgeLength = options.maxEdgeLength;
    const std::unique_ptr<Kernel::SurfaceBatchEvaluator> accelerator = makeTessellationAccelerator();
    tessellation.accelerator = accelerator.get();
    quint64 nextVertex = 1, nextNormal = 1;
    quint64 previewTrianglesSeen = 0, previewPolygonsSeen = 0;
    int meshBodies = 0;
    try {
        for (const ExportBody &body : bodies) {
            if (!body.body) continue;
            ++meshBodies;
            QString name = body.name;
            name.replace(QLatin1Char('\n'), QLatin1Char('_'));
            name.replace(QLatin1Char('\r'), QLatin1Char('_'));
            name.replace(QLatin1Char('#'), QLatin1Char('_'));
            stream << "o " << name << '\n';
            const Kernel::Tessellation mesh = Kernel::tessellate(*body.body, tessellation);
            if (mesh.failedFaces > 0) {
                result.error = QStringLiteral("Tassellazione OBJ non riuscita per %1 facce del corpo %2.")
                    .arg(mesh.failedFaces).arg(body.name);
                result.data.clear();
                return result;
            }
            // I punti dello stesso edge valutati dalle due superfici possono
            // differire entro la tolleranza B-rep. Si saldano spazialmente;
            // le normali restano per-corner, quindi gli spigoli vivi non
            // vengono visualmente smussati in Blender.
            double weldTolerance = std::max(1e-10, options.deflection * 1e-7);
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
                if (options.maxEdgeLength > 0.0)
                    for (const std::array<int, 3> &triangle : face.triangles) {
                        const Kernel::Vec3 &a = face.points[std::size_t(triangle[0])];
                        const Kernel::Vec3 &b = face.points[std::size_t(triangle[1])];
                        const Kernel::Vec3 &c = face.points[std::size_t(triangle[2])];
                        if (std::max({Kernel::distance(a, b), Kernel::distance(b, c), Kernel::distance(c, a)})
                            > options.maxEdgeLength * (1.0 + 1e-9)) {
                            result.error = QStringLiteral(
                                "La densita' richiesta supera il limite di raffinamento su %1; aumentare il lato massimo.").arg(body.name);
                            result.data.clear();
                            return result;
                        }
                    }
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
        result.data.clear();
        return result;
    }
    stream.flush();
    buffer.close();
    if (stream.status() != QTextStream::Ok) {
        result.error = QStringLiteral("Memoria insufficiente durante la costruzione della mesh OBJ.");
        result.data.clear();
    } else if (meshBodies == 0 || result.quadCount + result.triangleCount == 0) {
        result.error = QStringLiteral("Non ci sono solidi o superfici esportabili in OBJ.");
        result.data.clear();
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
