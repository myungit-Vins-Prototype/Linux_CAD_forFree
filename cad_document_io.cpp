#include "cad_document_io.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QSaveFile>

#include "cad_constraints.h"

namespace ForgeCad {
namespace {

constexpr char kMagic[4] = {'F', 'C', 'A', 'D'};
// Versioni: 1 prima; 2 aggiunge il piano degli schizzi su una faccia (SketchFrame, faceSource);
// 3 i vincoli geometrici come oggetti (i vecchi dati si convertono all'apertura);
// 4 la posizione delle quote e l'orientamento degli assi del documento.
constexpr quint16 kVersion = 4;
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
    for (double v : sketch.frame.origin) out << v;
    for (double v : sketch.frame.xAxis) out << v;
    for (double v : sketch.frame.normal) out << v;
    out << sketch.faceSource << sketch.customFrame;
    out << quint32(sketch.geometricConstraints.size());
    for (const SketchConstraint &c : sketch.geometricConstraints) {
        out << qint32(c.type);
        for (const ConstraintRef &r : {c.first, c.second}) out << qint32(r.kind) << qint32(r.element) << qint32(r.point);
        out << c.value << c.positions << c.placement << c.placed;
    }
}

// Numero di elementi di un vettore, rifiutato se il file e' finito o troppo corto.
bool readCount(QDataStream &in, quint32 &count) {
    in >> count;
    return in.status() == QDataStream::Ok && count <= quint32(in.device()->bytesAvailable());
}

bool read(QDataStream &in, SketchObject &sketch, quint16 version) {
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
    if (version >= 2) {
        for (double &v : sketch.frame.origin) in >> v;
        for (double &v : sketch.frame.xAxis) in >> v;
        for (double &v : sketch.frame.normal) in >> v;
        in >> sketch.faceSource;
    }
    if (version >= 4) in >> sketch.customFrame;
    if (version >= 3) {
        quint32 constraintCount = 0;
        if (!readCount(in, constraintCount)) return false;
        sketch.geometricConstraints.resize(int(constraintCount));
        for (SketchConstraint &c : sketch.geometricConstraints) {
            qint32 type = 0;
            in >> type;
            c.type = ConstraintType(type);
            for (ConstraintRef *r : {&c.first, &c.second}) {
                qint32 kind = -1, element = -1, point = -1;
                in >> kind >> element >> point;
                *r = {kind, element, point};
            }
            in >> c.value >> c.positions;
            if (version >= 4) in >> c.placement >> c.placed;
        }
    }
    if (sketch.plane < 0 || sketch.plane > kFacePlane) return false;
    // Gli array paralleli dei segmenti devono restare allineati.
    const int segments = sketch.segments.size();
    sketch.constraints.resize(segments);
    sketch.segmentLengths.resize(segments);
    sketch.segmentAngles.resize(segments);
    if (in.status() != QDataStream::Ok) return false;
    migrateLegacyConstraints(sketch);  // i file vecchi: codici, quote e coincidenze diventano vincoli
    return true;
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
        for (const double *axis : {state.orientation.right, state.orientation.up, state.orientation.toward})
            for (int k = 0; k < 3; ++k) out << axis[k];
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
        if (!read(in, sketch, version)) return QStringLiteral("Il file e' danneggiato (schizzi).");
    if (!readCount(in, count)) return QStringLiteral("Il file e' danneggiato.");
    loaded.extrusions.resize(int(count));
    for (ExtrusionObject &body : loaded.extrusions)
        if (!read(in, body)) return QStringLiteral("Il file e' danneggiato (corpi).");
    if (version >= 4) {
        for (double *axis : {loaded.orientation.right, loaded.orientation.up, loaded.orientation.toward})
            for (int k = 0; k < 3; ++k) in >> axis[k];
        if (in.status() != QDataStream::Ok) return QStringLiteral("Il file e' danneggiato (orientamento degli assi).");
        loaded.orientationSet = true;
    }
    state = std::move(loaded);
    return {};
}

}
