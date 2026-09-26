#include "cad_document_io.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QSaveFile>

namespace ForgeCad {
namespace {

constexpr char kMagic[4] = {'F', 'C', 'A', 'D'};
constexpr quint16 kVersion = 1;
constexpr quint8 kZlib = 1;

void write(QDataStream &out, const CurveObject &curve) {
    out << qint32(curve.tool) << curve.controlPoints << curve.weights << curve.tangentHandles << qint32(curve.sides)
        << curve.construction;
}

void read(QDataStream &in, CurveObject &curve) {
    qint32 tool = 0, sides = 0;
    in >> tool >> curve.controlPoints >> curve.weights >> curve.tangentHandles >> sides >> curve.construction;
    curve.tool = DrawingTool(tool);
    curve.sides = sides;
}

void write(QDataStream &out, const CoincidentConstraint &c) {
    out << qint32(c.firstKind) << qint32(c.firstElement) << qint32(c.firstPoint) << qint32(c.secondKind)
        << qint32(c.secondElement) << qint32(c.secondPoint);
}

void read(QDataStream &in, CoincidentConstraint &c) {
    qint32 v[6] = {};
    for (qint32 &value : v) in >> value;
    c = {v[0], v[1], v[2], v[3], v[4], v[5]};
}

void write(QDataStream &out, const SketchObject &sketch) {
    out << sketch.name << qint32(sketch.plane) << sketch.segments << sketch.constraints << sketch.segmentLengths
        << sketch.segmentAngles << sketch.visible << sketch.constructionSegments;
    out << quint32(sketch.curves.size());
    for (const CurveObject &curve : sketch.curves) write(out, curve);
    out << quint32(sketch.coincidentConstraints.size());
    for (const CoincidentConstraint &c : sketch.coincidentConstraints) write(out, c);
}

// Numero di elementi di un vettore, rifiutato se il file e' finito o troppo corto.
bool readCount(QDataStream &in, quint32 &count) {
    in >> count;
    return in.status() == QDataStream::Ok && count <= quint32(in.device()->bytesAvailable());
}

bool read(QDataStream &in, SketchObject &sketch) {
    qint32 plane = 0;
    in >> sketch.name >> plane >> sketch.segments >> sketch.constraints >> sketch.segmentLengths >> sketch.segmentAngles
        >> sketch.visible >> sketch.constructionSegments;
    sketch.plane = plane;
    quint32 count = 0;
    if (!readCount(in, count)) return false;
    sketch.curves.resize(int(count));
    for (CurveObject &curve : sketch.curves) read(in, curve);
    if (!readCount(in, count)) return false;
    sketch.coincidentConstraints.resize(int(count));
    for (CoincidentConstraint &c : sketch.coincidentConstraints) read(in, c);
    // Gli array paralleli dei segmenti devono restare allineati.
    const int segments = sketch.segments.size();
    sketch.constraints.resize(segments);
    sketch.segmentLengths.resize(segments);
    sketch.segmentAngles.resize(segments);
    return in.status() == QDataStream::Ok;
}

void write(QDataStream &out, const ExtrusionObject &body) {
    out << body.name << qint32(body.sketchIndex) << qint32(body.plane) << body.distance << body.visible
        << qint32(body.operation) << qint32(body.feature) << qint32(body.revolveAxis) << body.revolveAngle;
    const PrimitiveParameters &p = body.primitive;
    out << qint32(p.kind) << qint32(p.plane);
    for (double v : p.origin) out << v;
    for (double v : p.size) out << v;
    out << body.blendChamfer << body.blendSize << quint32(body.blendEdges.size());
    for (const EdgePoint &e : body.blendEdges) out << e.x << e.y << e.z;
    out << qint32(body.firstBody) << qint32(body.secondBody);
}

bool read(QDataStream &in, ExtrusionObject &body) {
    qint32 sketchIndex = 0, plane = 0, operation = 0, feature = 0, revolveAxis = 0;
    in >> body.name >> sketchIndex >> plane >> body.distance >> body.visible >> operation >> feature >> revolveAxis
        >> body.revolveAngle;
    body.sketchIndex = sketchIndex;
    body.plane = plane;
    body.operation = operation;
    body.feature = BodyFeature(feature);
    body.revolveAxis = revolveAxis;
    PrimitiveParameters &p = body.primitive;
    qint32 kind = 0, primitivePlane = 0;
    in >> kind >> primitivePlane;
    p.kind = PrimitiveKind(kind);
    p.plane = primitivePlane;
    for (double &v : p.origin) in >> v;
    for (double &v : p.size) in >> v;
    quint32 count = 0;
    in >> body.blendChamfer >> body.blendSize;
    if (!readCount(in, count)) return false;
    body.blendEdges.resize(int(count));
    for (EdgePoint &e : body.blendEdges) in >> e.x >> e.y >> e.z;
    qint32 first = -1, second = -1;
    in >> first >> second;
    body.firstBody = first;
    body.secondBody = second;
    return in.status() == QDataStream::Ok;
}

}

QString saveDocumentFile(const QString &path, const DocumentState &state) {
    QByteArray payload;
    {
        QBuffer buffer(&payload);
        buffer.open(QIODevice::WriteOnly);
        QDataStream out(&buffer);
        out.setVersion(QDataStream::Qt_6_0);
        out.setFloatingPointPrecision(QDataStream::DoublePrecision);
        out << quint32(state.sketches.size());
        for (const SketchObject &sketch : state.sketches) write(out, sketch);
        out << quint32(state.extrusions.size());
        for (const ExtrusionObject &body : state.extrusions) write(out, body);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("Impossibile scrivere %1: %2").arg(path, file.errorString());
    QDataStream out(&file);
    out.writeRawData(kMagic, 4);
    out << kVersion << kZlib;
    const QByteArray compressed = qCompress(payload, 9);
    out.writeRawData(compressed.constData(), int(compressed.size()));
    if (out.status() != QDataStream::Ok || !file.commit()) return QStringLiteral("Errore di scrittura su %1: %2").arg(path, file.errorString());
    return {};
}

QString loadDocumentFile(const QString &path, DocumentState &state) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Impossibile aprire %1: %2").arg(path, file.errorString());
    const QByteArray data = file.readAll();
    if (data.size() < 7 || !data.startsWith(QByteArray(kMagic, 4))) return QStringLiteral("%1 non e' un file ForgeCAD.").arg(path);
    QDataStream header(data);
    header.skipRawData(4);
    quint16 version = 0;
    quint8 compression = 0;
    header >> version >> compression;
    if (version > kVersion) return QStringLiteral("Il file e' stato scritto da una versione piu' recente di ForgeCAD (formato %1).").arg(version);
    if (compression != kZlib) return QStringLiteral("Compressione sconosciuta nel file.");
    const QByteArray payload = qUncompress(data.mid(7));
    if (payload.isEmpty()) return QStringLiteral("Il file e' danneggiato (dati compressi non validi).");
    QBuffer buffer;
    buffer.setData(payload);
    buffer.open(QIODevice::ReadOnly);
    QDataStream in(&buffer);
    in.setVersion(QDataStream::Qt_6_0);
    in.setFloatingPointPrecision(QDataStream::DoublePrecision);
    DocumentState loaded;
    quint32 count = 0;
    if (!readCount(in, count)) return QStringLiteral("Il file e' danneggiato.");
    loaded.sketches.resize(int(count));
    for (SketchObject &sketch : loaded.sketches)
        if (!read(in, sketch)) return QStringLiteral("Il file e' danneggiato (schizzi).");
    if (!readCount(in, count)) return QStringLiteral("Il file e' danneggiato.");
    loaded.extrusions.resize(int(count));
    for (ExtrusionObject &body : loaded.extrusions)
        if (!read(in, body)) return QStringLiteral("Il file e' danneggiato (corpi).");
    state = std::move(loaded);
    return {};
}

}
