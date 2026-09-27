#include "cad_document_io.h"

#include <vector>

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
// 4 la posizione delle quote e l'orientamento degli assi del documento;
// 5 taglio ed estensione delle superfici (piano e punto del taglio, tipo dell'estensione);
// 6 scala dei corpi, smussi con due distanze o distanza e angolo; 7 elica, sweep, loft;
// 8 corpi importati (STEP/IGES) e piani di costruzione, schizzi sui piani di costruzione.
constexpr quint16 kVersion = 10;
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
    out << qint32(sketch.datumPlane);  // formato 8
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
    if (version >= 8) {
        qint32 datum = -1;
        in >> datum;
        sketch.datumPlane = datum;
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

// Riferimenti geometrici (piani di costruzione, ripetizioni).
void writeRefs(QDataStream &out, const QVector<GeometryRef> &refs) {
    out << quint32(refs.size());
    for (const GeometryRef &r : refs)
        out << qint32(r.kind) << qint32(r.index) << qint32(r.element.kind) << qint32(r.element.element) << qint32(r.element.point) << r.point.x << r.point.y
            << r.point.z;
}

bool readRefs(QDataStream &in, QVector<GeometryRef> &refs) {
    quint32 count = 0;
    if (!readCount(in, count)) return false;
    refs.resize(int(count));
    for (GeometryRef &r : refs) {
        qint32 kind = -1, index = -1, elementKind = -1, element = -1, point = -1;
        in >> kind >> index >> elementKind >> element >> point >> r.point.x >> r.point.y >> r.point.z;
        r.kind = kind;
        r.index = index;
        r.element = {elementKind, element, point};
    }
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
    out << qint32(body.trimPlane) << body.trimKeep.x << body.trimKeep.y << body.trimKeep.z << body.extendLinear;
    out << body.scaleFactor << qint32(body.scaleCenterMode) << body.scaleCenter.x << body.scaleCenter.y << body.scaleCenter.z;
    out << qint32(body.chamferSpec.mode) << body.chamferSpec.second << body.chamferSpec.flip;
    // Formato 7: elica, sweep, loft.
    const HelixParameters &h = body.helix;
    out << h.spiral << qint32(h.mode) << h.pitch << h.turns << h.height << h.taper << h.startAngle << h.leftHanded << h.reverse
        << qint32(h.source) << qint32(h.curve) << h.reference.x << h.reference.y << h.reference.z;
    out << qint32(body.sweepPath) << qint32(body.pathSketch) << qint32(body.sweepMode);
    out << quint32(body.loftSketches.size());
    for (int sketch : body.loftSketches) out << qint32(sketch);
    out << body.loftRuled;
    // Formato 8: forma importata, piano di costruzione.
    out << body.importData << body.importSource;
    const DatumParameters &d = body.datum;
    out << qint32(d.mode);
    writeRefs(out, d.refs);
    out << d.distance << d.angle << d.flip << d.onCurve << d.size;
    // Formato 10: ripetizione.
    const PatternParameters &r = body.pattern;
    out << qint32(r.kind);
    writeRefs(out, r.refs);
    out << qint32(r.count) << qint32(r.count2) << r.spacing << r.spacing2 << r.angle << r.spread << r.flip << r.flip2 << r.keepOriginal << r.featureOnly;
}

// `extras` (solo formato 5): i file scritti durante lo sviluppo del formato 5
// possono avere anche i campi della scala (1) e dello smusso (2).
bool read(QDataStream &in, ExtrusionObject &body, quint16 version, int extras) {
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
    if (version >= 5) {
        qint32 trimPlane = 0;
        in >> trimPlane >> body.trimKeep.x >> body.trimKeep.y >> body.trimKeep.z >> body.extendLinear;
        body.trimPlane = trimPlane;
    }
    if (version >= 6 || extras >= 1) {
        qint32 centerMode = 0;
        in >> body.scaleFactor >> centerMode >> body.scaleCenter.x >> body.scaleCenter.y >> body.scaleCenter.z;
        body.scaleCenterMode = centerMode;
    }
    if (version >= 6 || extras >= 2) {
        qint32 chamferMode = 0;
        in >> chamferMode >> body.chamferSpec.second >> body.chamferSpec.flip;
        body.chamferSpec.mode = chamferMode;
    }
    if (version >= 7) {
        HelixParameters &h = body.helix;
        qint32 mode = 0, source = 0, curve = -1, sweepPath = 0, pathSketch = -1, sweepMode = 0;
        in >> h.spiral >> mode >> h.pitch >> h.turns >> h.height >> h.taper >> h.startAngle >> h.leftHanded >> h.reverse >> source >> curve
            >> h.reference.x >> h.reference.y >> h.reference.z;
        h.mode = mode;
        h.source = source;
        h.curve = curve;
        in >> sweepPath >> pathSketch >> sweepMode;
        body.sweepPath = sweepPath;
        body.pathSketch = pathSketch;
        body.sweepMode = sweepMode;
        quint32 sections = 0;
        if (!readCount(in, sections)) return false;
        body.loftSketches.clear();
        for (quint32 k = 0; k < sections; ++k) {
            qint32 sketch = -1;
            in >> sketch;
            body.loftSketches.append(sketch);
        }
        in >> body.loftRuled;
    }
    if (version >= 8) {
        in >> body.importData >> body.importSource;
        // Formato 8: la forma importata era un B-rep binario di OpenCASCADE,
        // che il kernel non legge: il corpo resta vuoto (va importato di nuovo).
        if (version < 9) body.importData.clear();
        DatumParameters &d = body.datum;
        qint32 mode = 0;
        in >> mode;
        if (!readRefs(in, d.refs)) return false;
        d.mode = mode;
        in >> d.distance >> d.angle >> d.flip >> d.onCurve >> d.size;
    }
    if (version >= 10) {
        PatternParameters &p = body.pattern;
        qint32 kind = 0, count = 0, count2 = 0;
        in >> kind;
        if (!readRefs(in, p.refs)) return false;
        in >> count >> count2 >> p.spacing >> p.spacing2 >> p.angle >> p.spread >> p.flip >> p.flip2 >> p.keepOriginal >> p.featureOnly;
        p.kind = kind;
        p.count = count;
        p.count2 = count2;
    }
    if (int(body.feature) < 0 || int(body.feature) > int(BodyFeature::Pattern)) return false;
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

namespace {

QString parsePayload(const QByteArray &payload, quint16 version, int extras, DocumentState &loaded) {
    QBuffer buffer;
    buffer.setData(payload);
    buffer.open(QIODevice::ReadOnly);
    QDataStream in(&buffer);
    in.setVersion(QDataStream::Qt_6_0);
    in.setFloatingPointPrecision(QDataStream::DoublePrecision);
    quint32 count = 0;
    if (!readCount(in, count)) return QStringLiteral("Il file e' danneggiato.");
    loaded.sketches.resize(int(count));
    for (SketchObject &sketch : loaded.sketches)
        if (!read(in, sketch, version)) return QStringLiteral("Il file e' danneggiato (schizzi).");
    if (!readCount(in, count)) return QStringLiteral("Il file e' danneggiato.");
    loaded.extrusions.resize(int(count));
    for (ExtrusionObject &body : loaded.extrusions)
        if (!read(in, body, version, extras)) return QStringLiteral("Il file e' danneggiato (corpi).");
    if (version >= 4) {
        for (double *axis : {loaded.orientation.right, loaded.orientation.up, loaded.orientation.toward})
            for (int k = 0; k < 3; ++k) in >> axis[k];
        if (in.status() != QDataStream::Ok) return QStringLiteral("Il file e' danneggiato (orientamento degli assi).");
        loaded.orientationSet = true;
    }
    if (!buffer.atEnd()) return QStringLiteral("Il file e' danneggiato (dati in piu' alla fine).");
    return {};
}

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
    // Formato 5: tre varianti (senza e con i campi della scala e dello smusso),
    // vale la prima che si legge tutta.
    QString error;
    DocumentState loaded;
    for (int extras : version == 5 ? std::vector<int>{2, 1, 0} : std::vector<int>{0}) {
        loaded = DocumentState();
        error = parsePayload(payload, version, extras, loaded);
        if (error.isEmpty()) break;
    }
    if (!error.isEmpty()) return error;
    state = std::move(loaded);
    return {};
}

}
