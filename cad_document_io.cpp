#include "cad_document_io.h"
#include "cad_workspace.h"

#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <QBuffer>
#include <QSet>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>

#include "cad_constraints.h"
#include "cad_model_history.h"
#include "cad_element_names.h"
#include "cad_snapshot_chunks.h"
#include "cad_topology_ref.h"
#include "fk_body_io.h"
#include "fk_classify.h"
#include "fk_parallel.h"
#include "forgecad_source_hash.h"

namespace ForgeCad {
namespace {

constexpr char kMagic[4] = {'F', 'C', 'A', 'D'};
// Versioni: 1 prima; 2 aggiunge il piano degli schizzi su una faccia (SketchFrame, faceSource);
// 3 i vincoli geometrici come oggetti (i vecchi dati si convertono all'apertura);
// 4 la posizione delle quote e l'orientamento degli assi del documento;
// 5 taglio ed estensione delle superfici (piano e punto del taglio, tipo dell'estensione);
// 6 scala dei corpi, smussi con due distanze o distanza e angolo; 7 elica, sweep, loft;
// 8 corpi importati (STEP/IGES) e piani di costruzione, schizzi sui piani di costruzione;
// 9 corpi importati come testo STEP; 10 ripetizioni dei corpi; 11 ripetizioni
// parametriche negli schizzi (vincolo Pattern), estrusioni fino a un
// riferimento e fuse con altri solidi, booleane con piu' strumenti.
// 12 curve di riferimento prese dai corpi (nodi e grado), spostamento dei corpi.
// 13 assi di simmetria degli schizzi, simmetrie (terzo riferimento dei vincoli), quote di raggio e diametro dall'asse.
// 14 copia dei corpi calcolati dopo la definizione (stessa definizione del 13).
// 15 curve guida, continuita' G0/G1/G2 e influenze del loft.
// 16 collegamento delle coppie di maniglie tangenti delle spline.
// 17 catene parziali di schizzo per sweep e guide del loft.
// 18 grado di continuita' imposto dalle guide del loft.
// 19 identita' persistenti delle feature e corpi logici della storyboard.
// 20 riferimenti persistenti a feature e sotto-entita' topologiche; 21
// estrusione nei due versi; 22 colore della mesh dei corpi logici.
// 23 offset di superficie e cucitura.
// 24 loft e sweep di superficie (senza coperchi), superficie rigata (le due
// catene di riferimenti), superficie planare (bordi scelti nella vista).
// 25 svuotamento dei solidi; 26 unita' lineare preferita del documento;
// 27 filettature parametriche su facce cilindriche o coniche; 28 componenti
// connesse separate prodotte dall'eliminazione delle facce. 29 riferimenti di
// faccia dei raccordi; 30 asse della rivoluzione scelto nella vista;
// 31 settore delle quote d'angolo (`SketchConstraint::angleSides`).
// 32 mantenimento della cucitura dell’offset.
// 37 quota associativa degli offset di schizzo.
// 38 taglio superficie con la proiezione di uno schizzo e curve proiettate.
// 39 spessore delle superfici (lato e direzione).
// 40 facce adiacenti e continuita' dei profili per loft e superficie rigata.
// 41 vincolo Pierce associativo a una faccia esterna.
// 42 sezioni e guide loft da bordi; 43 ordine delle guide.
// 44 estrazione associativa di curve 3D, segmento e vista salvata del taglio.
// 45 schizzi spaziali con entita' e punti XYZ in double.
// 50 identita' del documento, check-in archiviato e base della copia di lavoro.
constexpr quint16 kVersion = kDocumentFormatVersion;
constexpr quint8 kZlib = 1;

void write(QDataStream &out, const CurveObject &curve) {
    out << qint32(curve.tool) << curve.controlPoints << curve.weights << curve.tangentHandles << qint32(curve.sides)
        << curve.construction;
    out << curve.knots << qint32(curve.degree);  // formato 12
    out << curve.tangentLinked;                  // formato 16
}

void read(QDataStream &in, CurveObject &curve, quint16 version) {
    qint32 tool = 0, sides = 0;
    in >> tool >> curve.controlPoints >> curve.weights >> curve.tangentHandles >> sides >> curve.construction;
    curve.tool = DrawingTool(tool);
    curve.sides = sides;
    if (version >= 12) {
        qint32 degree = 3;
        in >> curve.knots >> degree;
        curve.degree = degree;
    }
    if (version >= 16) in >> curve.tangentLinked;
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

void writeRef(QDataStream &out, const ConstraintRef &r) { out << qint32(r.kind) << qint32(r.element) << qint32(r.point); }
void readRef(QDataStream &in, ConstraintRef &r) {
    qint32 kind = -1, element = -1, point = -1;
    in >> kind >> element >> point;
    r = {kind, element, point};
}

void writePathRef(QDataStream &out, const SketchPathRef &path) {
    out << qint32(path.sketch) << path.segments << path.curves;
}
bool readPathRef(QDataStream &in, SketchPathRef &path) {
    qint32 sketch = -1;
    in >> sketch >> path.segments >> path.curves;
    path.sketch = sketch;
    return in.status() == QDataStream::Ok;
}

void writePattern(QDataStream &out, const SketchPatternData &p) {
    out << qint32(p.kind) << quint32(p.sources.size());
    for (const ConstraintRef &r : p.sources) writeRef(out, r);
    out << quint32(p.copies.size());
    for (const ConstraintRef &r : p.copies) writeRef(out, r);
    out << qint32(p.count) << qint32(p.count2) << p.spacing << p.spacing2 << p.angle << p.spread << p.dimensioned << p.dimensioned2;
    for (const ConstraintRef &r : {p.direction, p.direction2, p.center, p.axis}) writeRef(out, r);
    out << p.directionAngle << p.directionAngle2 << p.centerPoint << p.axisPoint << p.axisDirection;
}

bool readCount(QDataStream &in, quint32 &count);

bool readPattern(QDataStream &in, SketchPatternData &p) {
    qint32 kind = 0, count = 0, count2 = 0;
    quint32 n = 0;
    in >> kind;
    if (!readCount(in, n)) return false;
    p.sources.resize(int(n));
    for (ConstraintRef &r : p.sources) readRef(in, r);
    if (!readCount(in, n)) return false;
    p.copies.resize(int(n));
    for (ConstraintRef &r : p.copies) readRef(in, r);
    in >> count >> count2 >> p.spacing >> p.spacing2 >> p.angle >> p.spread >> p.dimensioned >> p.dimensioned2;
    for (ConstraintRef *r : {&p.direction, &p.direction2, &p.center, &p.axis}) readRef(in, *r);
    in >> p.directionAngle >> p.directionAngle2 >> p.centerPoint >> p.axisPoint >> p.axisDirection;
    p.kind = kind;
    p.count = count;
    p.count2 = count2;
    return in.status() == QDataStream::Ok;
}

void writeRefs(QDataStream &out, const QVector<GeometryRef> &refs);
bool readRefs(QDataStream &in, QVector<GeometryRef> &refs, quint16 version);

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
        if (c.type == ConstraintType::Pierce) {
            out << bool(c.pierce);
            if (c.pierce) {
                writeRefs(out,{c.pierce->surface});
                out << c.pierce->name << c.pierce->error << c.pierce->segments << quint32(c.pierce->curves.size());
                for (const auto &curve : c.pierce->curves) write(out,curve);
            }
        }
        if (c.type == ConstraintType::Pattern) writePattern(out, c.pattern);  // formato 11
        writeRef(out, c.third);                                               // formato 13
        out << qint32(c.angleSides);                                          // formato 31
        if (c.type == ConstraintType::Offset) {
            out << quint32(c.offset.sources.size());
            for (const auto &r : c.offset.sources) writeRef(out, r);
            out << quint32(c.offset.copies.size());
            for (const auto &r : c.offset.copies) writeRef(out, r);
            out << c.offset.reverse << c.offset.bothSides << c.offset.roundCorners;
        }
    }
    out << qint32(sketch.datumPlane);  // formato 8
    out << sketch.symmetryAxes;        // formato 13
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
    for (CurveObject &curve : sketch.curves) read(in, curve, version);
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
            if (c.type == ConstraintType::Pierce) {
                if (version<41) return false;
                bool present=false; in >> present;
                if (present) {
                    auto data=std::make_shared<SketchPierceData>();
                    QVector<GeometryRef> refs;
                    if (!readRefs(in,refs,version) || refs.size()!=1) return false;
                    data->surface=refs.first();
                    quint32 n=0;
                    in >> data->name >> data->error >> data->segments;
                    if (!readCount(in,n)) return false;
                    data->curves.resize(int(n));
                    for (auto &curve : data->curves) read(in,curve,version);
                    c.pierce=data;
                }
            }
            if (c.type == ConstraintType::Pattern && (version < 11 || !readPattern(in, c.pattern))) return false;
            if (version >= 13) readRef(in, c.third);
            if (version >= 31) {
                qint32 sides = 0;
                in >> sides;
                c.angleSides = sides & 3;
            }
            if (c.type == ConstraintType::Offset) {
                if (version < 37) return false;
                quint32 n = 0;
                if (!readCount(in,n)) return false;
                c.offset.sources.resize(int(n));
                for (auto &r : c.offset.sources) readRef(in,r);
                if (!readCount(in,n)) return false;
                c.offset.copies.resize(int(n));
                for (auto &r : c.offset.copies) readRef(in,r);
                in >> c.offset.reverse >> c.offset.bothSides >> c.offset.roundCorners;
            }
        }
    }
    if (version >= 8) {
        qint32 datum = -1;
        in >> datum;
        sketch.datumPlane = datum;
    }
    if (version >= 13) in >> sketch.symmetryAxes;
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
            << r.point.z << r.featureId << qint32(r.point.subshape) << qint32(r.point.geometry) << qint32(r.point.context);
}

bool readRefs(QDataStream &in, QVector<GeometryRef> &refs, quint16 version) {
    quint32 count = 0;
    if (!readCount(in, count)) return false;
    refs.resize(int(count));
    for (GeometryRef &r : refs) {
        qint32 kind = -1, index = -1, elementKind = -1, element = -1, point = -1;
        in >> kind >> index >> elementKind >> element >> point >> r.point.x >> r.point.y >> r.point.z;
        r.kind = kind;
        r.index = index;
        r.element = {elementKind, element, point};
        if (version >= 20) in >> r.featureId >> r.point.subshape >> r.point.geometry >> r.point.context;
    }
    return in.status() == QDataStream::Ok;
}

// Tutti i riferimenti a sotto-entita' B-rep della feature, in un ordine fisso
// (formato 49: i nomi topologici si scrivono e si leggono in quest'ordine).
template <class Body, class Visit>
void forEachEdgePoint(Body &body, Visit visit) {
    for (auto &e : body.blendEdges) visit(e);
    visit(body.trimKeep);
    visit(body.scaleCenter);
    visit(body.helix.reference);
    for (auto &e : body.offsetFaces) visit(e);
    visit(body.thread.face);
    visit(body.trimToolKeep);
    for (auto &r : body.datum.refs) visit(r.point);
    for (auto &r : body.pattern.refs) visit(r.point);
    visit(body.extentRef.point);
    visit(body.move.axis.point);
    for (auto *refs : {&body.ruledFirst, &body.ruledSecond, &body.planarRefs, &body.loftStartFaces, &body.loftEndFaces, &body.loftSections,
                       &body.loftEdgeGuides, &body.curveExtraction.sources})
        for (auto &r : *refs) visit(r.point);
    visit(body.revolveAxisRef.point);
    visit(body.draftNeutral.point);
    visit(body.thickenDirection.point);
    visit(body.curveExtraction.source.point);
    visit(body.curveExtraction.tool.point);
    visit(body.sweepProfileRef.point);
    visit(body.sweepPathRef.point);
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
    // Formato 11: fine delle estrusioni e fusione di estrusioni/sweep, strumenti delle booleane.
    out << qint32(body.extent);
    writeRefs(out, {body.extentRef});
    out << qint32(body.mergeOperation) << body.mergeAuto << body.mergeBodies << body.booleanTools;
    // Formato 12: spostamento.
    const TransformParameters &m = body.move;
    for (double v : m.translation) out << v;
    writeRefs(out, {m.axis});
    out << m.angle << m.copy;
    // Formato 15: guide e continuita' del loft.
    out << quint32(body.loftGuides.size());
    for (int sketch : body.loftGuides) out << qint32(sketch);
    out << qint32(body.loftStartContinuity) << qint32(body.loftEndContinuity) << body.loftGuideInfluence
        << body.loftStartInfluence << body.loftEndInfluence;
    // Formato 17: porzioni di schizzo scelte graficamente.
    out << body.pathSegments << body.pathCurves << quint32(body.loftGuidePaths.size());
    for (const SketchPathRef &path : body.loftGuidePaths) writePathRef(out, path);
    out << qint32(body.loftGuideContinuity);
    // Formato 19: identita' persistente nella storyboard.
    out << body.featureId << body.modelBodyId << body.suppressed;
    // Formato 20: firme topologiche dei punti usati direttamente dalle feature.
    for (const EdgePoint &e : body.blendEdges) out << qint32(e.subshape) << qint32(e.geometry) << qint32(e.context);
    for (const EdgePoint *p : {&body.trimKeep, &body.scaleCenter, &body.helix.reference})
        out << qint32(p->subshape) << qint32(p->geometry) << qint32(p->context);
    // Formato 21: estrusione simmetrica o nei due versi.
    out << qint32(body.extrudeSides) << body.distance2;
    // Formato 23: facce dell'offset di superficie, parametri della cucitura.
    out << quint32(body.offsetFaces.size());
    for (const EdgePoint &e : body.offsetFaces) out << e.x << e.y << e.z << qint32(e.subshape) << qint32(e.geometry) << qint32(e.context);
    out << body.sewTolerance << body.sewSolid;
    // Formato 24: superfici da loft e sweep, rigata e planare.
    out << body.loftSurface << body.sweepSurface;
    writeRefs(out, body.ruledFirst);
    writeRefs(out, body.ruledSecond);
    writeRefs(out, body.planarRefs);
    // Formato 27: filettatura.
    const ThreadParameters &t = body.thread;
    out << qint32(t.standard) << t.designation << t.pitch << t.length << t.leftHanded << t.reverse
        << t.face.x << t.face.y << t.face.z << qint32(t.face.subshape) << qint32(t.face.geometry) << qint32(t.face.context);
    // Formato 28: indice della componente prodotta da DeleteFace.
    out << qint32(body.deleteComponent);
    // Formato 29: riferimenti di faccia dei raccordi e degli smussi (tutti i
    // bordi) e la feature della base su cui sono stati presi.
    for (const EdgePoint &e : body.blendEdges) out << qint32(e.role);
    out << body.blendBaseFeature;
    // Formato 30: asse della rivoluzione dato da un riferimento.
    writeRefs(out, {body.revolveAxisRef});
    out << body.offsetSew;
    // Formato 33: taglio reciproco di due corpi/superfici.
    out << body.trimBoth << body.trimToolKeep.x << body.trimToolKeep.y << body.trimToolKeep.z
        << qint32(body.trimToolKeep.subshape) << qint32(body.trimToolKeep.geometry) << qint32(body.trimToolKeep.context);
    // Formato 34: estrusione di sola superficie.
    out << body.extrudeSurface;
    // Formato 35: superficie di riempimento.
    out << qint32(body.fillContinuity) << body.fillInfluence << body.fillGuideWeight;
    // Formato 36: sformo parametrico a piano neutro.
    writeRefs(out, {body.draftNeutral});
    out << body.draftAngle << body.draftReverse;
    // Formato 38: proiezione di uno schizzo (taglio e curve).
    out << body.trimProject << qint32(body.projectionMode) << body.projectionReverse;
    // Formato 39: spessore delle superfici.
    out << qint32(body.thickenSide);
    writeRefs(out, {body.thickenDirection});
    writeRefs(out, body.loftStartFaces);
    writeRefs(out, body.loftEndFaces);
    writeRefs(out, body.loftSections);
    writeRefs(out, body.loftEdgeGuides);
    out << quint32(body.loftGuideOrder.size());
    for (int index : body.loftGuideOrder) out << qint32(index);
    const auto &c = body.curveExtraction;
    writeRefs(out, {c.source, c.tool});
    out << c.trimmed << c.projected << c.keepStart << c.cutHint;
    for (double v : c.view.origin) out << v;
    for (double v : c.view.normal) out << v;
    for (double v : c.view.xAxis) out << v;
    out << qint32(body.spatialSketch.nextId) << quint32(body.spatialSketch.entities.size());
    for(const auto &entity:body.spatialSketch.entities) {
        out << qint32(entity.id) << entity.spline << entity.closed << quint32(entity.points.size());
        for(const auto &point:entity.points)out << point.x << point.y << point.z;
    }
    // Formato 46: condizioni per guida loft e influenza per faccia fill.
    out << quint32(body.loftGuideContinuities.size());
    for (int value : body.loftGuideContinuities) out << qint32(value);
    out << quint32(body.loftGuideInfluences.size());
    for (double value : body.loftGuideInfluences) out << value;
    out << quint32(body.fillFaceInfluences.size());
    for (double value : body.fillFaceInfluences) out << value;
    out << qint32(body.fillShapeFace) << body.fillShapeWeight;
    writeRefs(out,body.curveExtraction.sources);
    // Formato 48: sezione e percorso sweep da bordi e curve 3D.
    writeRefs(out, {body.sweepProfileRef, body.sweepPathRef});
    // Formato 49: nomi topologici persistenti dei riferimenti (genealogia).
    QStringList names;
    forEachEdgePoint(body, [&](const EdgePoint &e) { names.append(e.name); });
    out << names;
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
        if (!readRefs(in, d.refs, version)) return false;
        d.mode = mode;
        in >> d.distance >> d.angle >> d.flip >> d.onCurve >> d.size;
    }
    if (version >= 10) {
        PatternParameters &p = body.pattern;
        qint32 kind = 0, count = 0, count2 = 0;
        in >> kind;
        if (!readRefs(in, p.refs, version)) return false;
        in >> count >> count2 >> p.spacing >> p.spacing2 >> p.angle >> p.spread >> p.flip >> p.flip2 >> p.keepOriginal >> p.featureOnly;
        p.kind = kind;
        p.count = count;
        p.count2 = count2;
    }
    if (version >= 11) {
        qint32 extent = 0, merge = 0;
        QVector<GeometryRef> refs;
        in >> extent;
        if (!readRefs(in, refs, version) || refs.size() != 1) return false;
        in >> merge >> body.mergeAuto >> body.mergeBodies >> body.booleanTools;
        body.extent = extent;
        body.extentRef = refs.first();
        body.mergeOperation = merge;
    }
    if (version >= 12) {
        TransformParameters &m = body.move;
        for (double &v : m.translation) in >> v;
        QVector<GeometryRef> refs;
        if (!readRefs(in, refs, version) || refs.size() != 1) return false;
        m.axis = refs.first();
        in >> m.angle >> m.copy;
    }
    if (version >= 15) {
        quint32 guides = 0;
        if (!readCount(in, guides)) return false;
        body.loftGuides.clear();
        for (quint32 k = 0; k < guides; ++k) {
            qint32 sketch = -1;
            in >> sketch;
            body.loftGuides.append(sketch);
        }
        qint32 start = 0, end = 0;
        in >> start >> end >> body.loftGuideInfluence >> body.loftStartInfluence >> body.loftEndInfluence;
        body.loftStartContinuity = start;
        body.loftEndContinuity = end;
        if (start < 0 || start > (version >= 40 ? 3 : 2) || end < 0 || end > (version >= 40 ? 3 : 2) || body.loftGuideInfluence < 0.0 || body.loftGuideInfluence > 1.0
            || body.loftStartInfluence < 0.0 || body.loftStartInfluence > 1.0 || body.loftEndInfluence < 0.0 || body.loftEndInfluence > 1.0)
            return false;
    }
    if (version >= 17) {
        quint32 paths = 0;
        in >> body.pathSegments >> body.pathCurves;
        if (!readCount(in, paths)) return false;
        body.loftGuidePaths.clear();
        for (quint32 k = 0; k < paths; ++k) {
            SketchPathRef path;
            if (!readPathRef(in, path)) return false;
            body.loftGuidePaths.append(std::move(path));
        }
    }
    if (version >= 18) {
        qint32 continuity = 1;
        in >> continuity;
        if (continuity < 0 || continuity > 2) return false;
        body.loftGuideContinuity = continuity;
    } else {
        // Fino al formato 17 le guide trasferivano sempre anche la curvatura.
        body.loftGuideContinuity = 2;
    }
    if (version >= 19) in >> body.featureId >> body.modelBodyId >> body.suppressed;
    if (version >= 20) {
        for (EdgePoint &e : body.blendEdges) in >> e.subshape >> e.geometry >> e.context;
        for (EdgePoint *p : {&body.trimKeep, &body.scaleCenter, &body.helix.reference}) in >> p->subshape >> p->geometry >> p->context;
    }
    if (version >= 21) {
        qint32 sides = 0;
        in >> sides >> body.distance2;
        if (sides < 0 || sides > 2 || !std::isfinite(body.distance2)) return false;
        body.extrudeSides = sides;
    }
    if (version >= 23) {
        quint32 faces = 0;
        if (!readCount(in, faces)) return false;
        body.offsetFaces.resize(int(faces));
        for (EdgePoint &e : body.offsetFaces) {
            qint32 subshape = -1, geometry = -1, context = -1;
            in >> e.x >> e.y >> e.z >> subshape >> geometry >> context;
            e.subshape = subshape, e.geometry = geometry, e.context = context;
        }
        in >> body.sewTolerance >> body.sewSolid;
        if (!std::isfinite(body.sewTolerance) || body.sewTolerance <= 0.0) return false;
    }
    if (version >= 24) {
        in >> body.loftSurface >> body.sweepSurface;
        if (!readRefs(in, body.ruledFirst, version) || !readRefs(in, body.ruledSecond, version) || !readRefs(in, body.planarRefs, version))
            return false;
    }
    if (version >= 27) {
        ThreadParameters &t = body.thread;
        qint32 standard = 0, subshape = -1, geometry = -1, context = -1;
        in >> standard >> t.designation >> t.pitch >> t.length >> t.leftHanded >> t.reverse
           >> t.face.x >> t.face.y >> t.face.z >> subshape >> geometry >> context;
        t.standard = standard;
        t.face.subshape = subshape;
        t.face.geometry = geometry;
        t.face.context = context;
        if (standard < 0 || standard > 7 || !std::isfinite(t.pitch) || t.pitch <= 0.0 || !std::isfinite(t.length) || t.length < 0.0)
            return false;
    }
    if (version >= 28) {
        qint32 component = -1;
        in >> component;
        if (component < -1) return false;
        body.deleteComponent = component;
    }
    if (version >= 29)
        for (EdgePoint &e : body.blendEdges) {
            qint32 role = kEdgePointEdge;
            in >> role;
            if (role != kEdgePointEdge && role != kEdgePointFaceBoundary) return false;
            e.role = role;
        }
    if (version >= 29) in >> body.blendBaseFeature;
    if (version >= 30) {
        QVector<GeometryRef> refs;
        if (!readRefs(in, refs, version) || refs.size() != 1) return false;
        body.revolveAxisRef = refs.first();
    }
    body.offsetSew = false;  // i documenti precedenti conservano il risultato originale
    if (version >= 32) in >> body.offsetSew;
    if (version >= 33)
        in >> body.trimBoth >> body.trimToolKeep.x >> body.trimToolKeep.y >> body.trimToolKeep.z
           >> body.trimToolKeep.subshape >> body.trimToolKeep.geometry >> body.trimToolKeep.context;
    if (version >= 34) in >> body.extrudeSurface;
    if (version >= 35) {
        qint32 continuity = 1;
        in >> continuity >> body.fillInfluence >> body.fillGuideWeight;
        if (continuity < 0 || continuity > 2 || !std::isfinite(body.fillInfluence) || !std::isfinite(body.fillGuideWeight)) return false;
        body.fillContinuity = continuity;
    }
    if (version >= 36) {
        QVector<GeometryRef> refs;
        if (!readRefs(in, refs, version) || refs.size() != 1) return false;
        body.draftNeutral = refs.first();
        in >> body.draftAngle >> body.draftReverse;
        if (!std::isfinite(body.draftAngle)) return false;
    }
    if (version >= 38) {
        qint32 mode = 0;
        in >> body.trimProject >> mode >> body.projectionReverse;
        if (mode < 0 || mode > 2) return false;
        body.projectionMode = mode;
    }
    if (version >= 39) {
        qint32 side = 0;
        in >> side;
        QVector<GeometryRef> refs;
        if (side < 0 || side > 2 || !readRefs(in, refs, version) || refs.size() != 1) return false;
        body.thickenSide = side;
        body.thickenDirection = refs.first();
    }
    if (version >= 40 && (!readRefs(in, body.loftStartFaces, version) || !readRefs(in, body.loftEndFaces, version))) return false;
    if (version >= 42 && (!readRefs(in, body.loftSections, version) || !readRefs(in, body.loftEdgeGuides, version))) return false;
    if (version >= 43) {
        quint32 count;
        if (!readCount(in, count)) return false;
        const int total = body.loftGuides.size() + body.loftGuidePaths.size() + body.loftEdgeGuides.size();
        if (count && count != quint32(total)) return false;
        QVector<bool> seen(total, false);
        for (quint32 k = 0; k < count; ++k) {
            qint32 index; in >> index;
            if (index < 0 || index >= total || seen[index]) return false;
            seen[index] = true; body.loftGuideOrder.append(index);
        }
    }
    if (version >= 44) {
        auto &c = body.curveExtraction;
        QVector<GeometryRef> refs;
        if (!readRefs(in, refs, version) || refs.size() != 2) return false;
        c.source = refs[0]; c.tool = refs[1];
        in >> c.trimmed >> c.projected >> c.keepStart >> c.cutHint;
        if (!std::isfinite(c.cutHint) || c.cutHint < 0 || c.cutHint > 1) return false;
        for (double &v : c.view.origin) { in >> v; if (!std::isfinite(v)) return false; }
        for (double &v : c.view.normal) { in >> v; if (!std::isfinite(v)) return false; }
        for (double &v : c.view.xAxis) { in >> v; if (!std::isfinite(v)) return false; }
    }
    if(version>=45) {
        qint32 next;quint32 count;
        in >> next;if(next<1 || !readCount(in,count))return false;
        body.spatialSketch.nextId=next;QSet<int> ids;
        for(quint32 k=0;k<count;++k) {
            SpatialCurve3D entity;qint32 id;quint32 points;
            in >> id >> entity.spline >> entity.closed;
            if(id<=0 || id>=next || ids.contains(id) || !readCount(in,points))return false;
            ids.insert(id);entity.id=id;
            for(quint32 j=0;j<points;++j) {
                SpatialPoint point;in >> point.x >> point.y >> point.z;
                if(!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))return false;
                entity.points.append(point);
            }
            body.spatialSketch.entities.append(entity);
        }
    }
    if (version >= 46) {
        const int total = body.loftGuides.size() + body.loftGuidePaths.size() + body.loftEdgeGuides.size();
        quint32 count;
        if (!readCount(in, count) || (count && count != quint32(total))) return false;
        for (quint32 i = 0; i < count; ++i) { qint32 value; in >> value; if (value < 0 || value > 2) return false; body.loftGuideContinuities.append(value); }
        if (!readCount(in, count) || (count && count != quint32(total))) return false;
        for (quint32 i = 0; i < count; ++i) { double value; in >> value; if (!std::isfinite(value) || value < 0 || value > 1) return false; body.loftGuideInfluences.append(value); }
        if (!readCount(in, count) || (count && count != quint32(body.ruledSecond.size()))) return false;
        for (quint32 i = 0; i < count; ++i) { double value; in >> value; if (!std::isfinite(value) || value <= 0) return false; body.fillFaceInfluences.append(value); }
    }
    if(version>=47){qint32 face;in>>face>>body.fillShapeWeight;
        if(face < -1 || face>=body.ruledSecond.size() || !std::isfinite(body.fillShapeWeight) || body.fillShapeWeight<0)return false;
        body.fillShapeFace=face;
        if(!readRefs(in,body.curveExtraction.sources,version))return false;
    }
    if (version >= 48) {
        QVector<GeometryRef> refs;
        if (!readRefs(in, refs, version) || refs.size() != 2) return false;
        body.sweepProfileRef = refs[0]; body.sweepPathRef = refs[1];
    }
    if (version >= 49) {
        QStringList names;
        in >> names;
        int count = 0;
        forEachEdgePoint(body, [&](EdgePoint &) { ++count; });
        if (in.status() != QDataStream::Ok || names.size() != count) return false;
        int next = 0;
        forEachEdgePoint(body, [&](EdgePoint &e) { e.name = names.at(next++); });
    }
    const BodyFeature last = version >= 45 ? BodyFeature::SpatialSketch : version >= 44 ? BodyFeature::ExtractedCurve : version >= 39 ? BodyFeature::Thicken : version >= 38 ? BodyFeature::ProjectedCurve : version >= 36 ? BodyFeature::Draft : version >= 35 ? BodyFeature::FillSurface : version >= 27 ? BodyFeature::Thread : version >= 25 ? BodyFeature::Shell : BodyFeature::PlanarSurface;
    if (int(body.feature) < 0 || int(body.feature) > int(last)) return false;
    return in.status() == QDataStream::Ok;
}

}

namespace {

// Snapshot dello stato calcolato, in un blocco compresso dopo la definizione.
// Dal formato interno 2 comprende sia il B-rep esatto sia la mesh pronta per
// il viewport. La definizione parametrica resta sempre la fonte autorevole:
// una modifica o una rigenerazione esplicita sostituisce immediatamente lo
// snapshot. Il tag rende leggibili anche le cache precedenti (solo B-rep).
constexpr quint32 kBodyCacheTag = 0x46434332;  // "FCC2"
constexpr quint32 kBodyCacheVersion = 7;
constexpr qsizetype kMaxDisplayValues = 200000000;

QByteArray definitionHash(const QByteArray &payload) { return QCryptographicHash::hash(payload, QCryptographicHash::Sha256); }

bool hasDisplaySnapshot(const BodyDisplay &display) {
    return display.quality >= 0
        && (!display.vertices.isEmpty() || !display.edges.isEmpty() || !display.constructionCurves.isEmpty());
}

void writeDisplay(QDataStream &out, const BodyDisplay &display) {
    const auto precision = out.floatingPointPrecision();
    out.setFloatingPointPrecision(QDataStream::SinglePrecision); // QVector3D contiene float: stessi bit, meno byte.
    out << qint32(display.quality) << display.vertices << display.normals << display.edges << display.edgeIds
        << display.constructionCurves << display.faceEdges << display.faceIds << display.faceLabelPoints << display.triangleFaces;
    out.setFloatingPointPrecision(precision);
}

bool reasonableDisplay(const BodyDisplay &display) {
    qsizetype values = display.vertices.size() + display.normals.size() + display.edgeIds.size()
                     + display.faceIds.size() + display.faceLabelPoints.size() + display.triangleFaces.size();
    for (const QVector<QVector3D> &line : display.edges) values += line.size();
    for (const QVector<QVector3D> &line : display.constructionCurves) values += line.size();
    for (const QVector<int> &face : display.faceEdges) values += face.size();
    return values <= kMaxDisplayValues
        && (display.normals.isEmpty() || display.normals.size() == display.vertices.size())
        && display.edgeIds.size() <= display.edges.size()
        && display.faceIds.size() == display.faceLabelPoints.size()
        && (display.triangleFaces.isEmpty() || display.triangleFaces.size() * 3 == display.vertices.size());
}

bool readDisplay(QDataStream &in, BodyDisplay &display, quint32 cacheVersion) {
    qint32 quality = -1;
    const auto precision = in.floatingPointPrecision();
    if (cacheVersion >= 5) in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    in >> quality >> display.vertices >> display.normals >> display.edges >> display.edgeIds
       >> display.constructionCurves >> display.faceEdges;
    if (cacheVersion >= 3) in >> display.faceIds >> display.faceLabelPoints;
    if (cacheVersion >= 4) in >> display.triangleFaces;
    in.setFloatingPointPrecision(precision);
    display.quality = quality;
    display.rayIndex.reset();
    display.instancedBase.reset();
    display.instanceTransforms.clear();
    return in.status() == QDataStream::Ok && quality >= 0 && reasonableDisplay(display);
}

// Solo copertura e origini dichiarate: nessuna inferenza al salvataggio.
// Le dimensioni delle tabelle si ricavano dai B-rep al caricamento.
QByteArray writeDeclaredHistory(const DeclaredHistorySnapshot &snapshot, const QVector<ExtrusionObject> &bodies) {
    if (!snapshot.history) return {};
    const auto &history = *snapshot.history;
    bool any = false;
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < history.resultSize(Kernel::EntityKind(k)); ++r)
            any = any || history.isDeclared(Kernel::EntityKind(k), r);
    if (!any || history.operandCount() != snapshot.operandIndices.size()) return {};
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    const bool hasLabels = snapshot.labels && std::any_of(snapshot.labels->begin(), snapshot.labels->end(),
        [](const auto &labels) { return !labels.empty(); });
    out << quint32(hasLabels ? 2 : 1) << quint32(snapshot.operandIndices.size()); // schema 1 rimane leggibile anche nelle cache 6
    for (int operand : snapshot.operandIndices) out << qint32(operand) << quint64(bodies.at(operand).featureId);
    for (int k = 0; k < 3; ++k) {
        const auto kind = Kernel::EntityKind(k);
        QVector<int> covered;
        for (int r = 0; r < history.resultSize(kind); ++r)
            if (history.isDeclared(kind, r)) covered.append(r);
        out << quint32(covered.size());
        for (int r : covered) {
            const auto &links = history.origins(kind, r);
            out << qint32(r) << quint32(links.size());
            for (const auto &link : links)
                out << qint32(link.operand) << quint8(link.kind) << qint32(link.index) << quint8(link.relation);
        }
    }
    if (hasLabels) {
        QVector<int> faces;
        for (std::size_t f = 0; f < snapshot.labels->size(); ++f) if (!snapshot.labels->at(f).empty()) faces.append(int(f));
        out << quint32(faces.size());
        for (int face : faces) {
            const auto &labels = snapshot.labels->at(std::size_t(face));
            out << qint32(face) << quint32(labels.size());
            for (const auto &label : labels) out << QString::fromStdString(label);
        }
    }
    return out.status() == QDataStream::Ok ? data : QByteArray();
}

bool readDeclaredHistory(const QByteArray &data, int index, const QVector<ExtrusionObject> &bodies, DeclaredHistorySnapshot &snapshot) {
    if (data.isEmpty()) return true;
    const auto &body = bodies.at(index);
    if (!body.forgeBody) return false;
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0, count = 0;
    in >> version >> count;
    const auto expected = featureGenealogyOperandIndices(body, index, bodies);
    if (in.status() != QDataStream::Ok || (version != 1 && version != 2) || count != quint32(expected.size())) return false;
    std::vector<const Kernel::Body *> operands;
    for (int operand : expected) {
        qint32 savedIndex = -1;
        quint64 savedId = 0;
        in >> savedIndex >> savedId;
        if (in.status() != QDataStream::Ok || savedIndex != operand || savedId != bodies.at(operand).featureId) return false;
        operands.push_back(bodies.at(operand).forgeBody.get());
    }
    const auto alive = [](const Kernel::Body &shape, Kernel::EntityKind kind, int entity) {
        if (kind == Kernel::EntityKind::Face) return shape.contains(Kernel::FaceId(entity));
        if (kind == Kernel::EntityKind::Edge) return shape.contains(Kernel::EdgeId(entity));
        return shape.contains(Kernel::VertexId(entity));
    };
    Kernel::BodyHistory history = Kernel::makeHistory(operands, *body.forgeBody);
    for (int k = 0; k < 3; ++k) {
        const auto kind = Kernel::EntityKind(k);
        quint32 entities = 0;
        in >> entities;
        if (in.status() != QDataStream::Ok || entities > quint32(history.resultSize(kind))
            || quint64(entities) * 8 > quint64(in.device()->bytesAvailable())) return false;
        for (quint32 r = 0; r < entities; ++r) {
            qint32 entity = -1;
            quint32 origins = 0;
            in >> entity >> origins;
            if (in.status() != QDataStream::Ok || !alive(*body.forgeBody, kind, entity) || history.isDeclared(kind, entity)
                || quint64(origins) * 10 > quint64(in.device()->bytesAvailable())) return false;
            std::vector<Kernel::HistoryLink> links;
            for (quint32 l = 0; l < origins; ++l) {
                qint32 operand = -1, source = -1;
                quint8 sourceKind = 255, relation = 255;
                in >> operand >> sourceKind >> source >> relation;
                if (in.status() != QDataStream::Ok || operand < 0 || operand >= int(operands.size()) || sourceKind > 2 || relation > 2
                    || !alive(*operands[std::size_t(operand)], Kernel::EntityKind(sourceKind), source)) return false;
                links.push_back({operand, Kernel::EntityKind(sourceKind), source, Kernel::HistoryRelation(relation)});
            }
            history.declareOrigins(kind, entity, std::move(links));
            if (history.origins(kind, entity).size() != origins) return false; // origini duplicate
        }
    }
    Kernel::FaceAliases labels;
    if (version == 2) {
        quint32 faces = 0;
        in >> faces;
        if (in.status() != QDataStream::Ok || faces > quint32(history.resultSize(Kernel::EntityKind::Face))
            || quint64(faces) * 8 > quint64(in.device()->bytesAvailable())) return false;
        labels.resize(std::size_t(history.resultSize(Kernel::EntityKind::Face)));
        for (quint32 f = 0; f < faces; ++f) {
            qint32 face = -1; quint32 aliases = 0;
            in >> face >> aliases;
            if (in.status() != QDataStream::Ok || !body.forgeBody->contains(Kernel::FaceId(face))
                || !history.isDeclared(Kernel::EntityKind::Face, face) || !labels[std::size_t(face)].empty()
                || aliases == 0 || quint64(aliases) * 4 > quint64(in.device()->bytesAvailable())) return false;
            auto &cell = labels[std::size_t(face)];
            for (quint32 a = 0; a < aliases; ++a) {
                QString label; in >> label;
                if (in.status() != QDataStream::Ok || label.isEmpty() || label.size() > 4096) return false;
                const auto text = label.toStdString();
                if (std::find(cell.begin(), cell.end(), text) != cell.end()) return false;
                cell.push_back(text);
            }
        }
    }
    if (in.status() != QDataStream::Ok || !in.atEnd()) return false;
    snapshot.history = std::make_shared<const Kernel::BodyHistory>(std::move(history));
    snapshot.operandIndices = expected;
    if (!labels.empty()) snapshot.labels = std::make_shared<const Kernel::FaceAliases>(std::move(labels));
    return true;
}

QByteArray bodyCache(const QByteArray &payload, const DocumentState &state) {
    QByteArray cache;
    QBuffer buffer(&cache);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out.setVersion(QDataStream::Qt_6_0);
    out << QByteArray(FORGECAD_SOURCE_HASH) << definitionHash(payload) << kBodyCacheTag << kBodyCacheVersion
        << quint32(state.extrusions.size());
    SnapshotChunkWriter chunks(out);
    for (int index = 0; index < state.extrusions.size(); ++index) {
        const ExtrusionObject &body = state.extrusions.at(index);
        std::string data;
        if (body.forgeBody) {
            try {
                data = Kernel::writeBodyBinary(*body.forgeBody);
            } catch (const std::exception &) {
                data.clear();  // geometria che il formato non conosce: si ricalcolera'
            }
        }
        out << quint8(data.empty() ? 0 : 1);
        if (!data.empty()) {
            out << body.error;
            chunks.write(QByteArray(data.data(), qsizetype(data.size())));
        }
        // Gli stadi nascosti conservano il B-rep esatto; la loro mesh si
        // rigenera rapidamente se servira' alla vista durante il rollback.
        const bool display = body.visible && hasDisplaySnapshot(body.display);
        out << quint8(display ? 1 : 0);
        if (display) writeDisplay(out, body.display);
        const auto declared = data.empty() ? DeclaredHistorySnapshot() : declaredHistorySnapshot(body, index, state.extrusions);
        chunks.write(writeDeclaredHistory(declared, state.extrusions));
    }
    return out.status() == QDataStream::Ok ? qCompress(cache, 6) : QByteArray();
}

void applyBodyCache(const QByteArray &compressed, const QByteArray &payload, DocumentState &state, bool previewCache) {
    (void)previewCache;
    const QByteArray cache = qUncompress(compressed);
    if (cache.isEmpty()) return;
    QDataStream in(cache);
    in.setVersion(QDataStream::Qt_6_0);
    QByteArray sourceHash, payloadHash;
    quint32 marker = 0, cacheVersion = 1, count = 0;
    in >> sourceHash >> payloadHash >> marker;
    (void)sourceHash;  // informativa: lo snapshot resta valido tra versioni del kernel
    if (marker == kBodyCacheTag) in >> cacheVersion >> count;
    else count = marker;  // cache storica: il terzo valore era direttamente il numero dei corpi
    if (in.status() != QDataStream::Ok || cacheVersion > kBodyCacheVersion
        || payloadHash != definitionHash(payload) || count != quint32(state.extrusions.size()))
        return;

    struct CachedBody {
        QString error;
        QByteArray data;
        QByteArray declared;
        BodyDisplay display;
        bool hasBody = false;
        bool hasDisplay = false;
    };
    std::vector<CachedBody> records(count);
    SnapshotChunkReader chunks(in);
    for (quint32 i = 0; i < count; ++i) {
        quint8 has = 0;
        in >> has;
        records[i].hasBody = has != 0;
        if (has) {
            in >> records[i].error;
            if (cacheVersion >= 5) records[i].data = chunks.read();
            else in >> records[i].data;
        }
        if (cacheVersion >= 2) {
            quint8 hasDisplay = 0;
            in >> hasDisplay;
            records[i].hasDisplay = hasDisplay != 0;
            if (records[i].hasDisplay && !readDisplay(in, records[i].display, cacheVersion)) return;
            // Le mesh precedenti possono contenere vertici prodotti mentre
            // l'upload CUDA era ancora in corso. Conservare i B-rep esatti,
            // rigenerando solo il display al primo caricamento.
            if (cacheVersion < 5) {
                records[i].hasDisplay = false;
                records[i].display = {};
            }
        }
        if (cacheVersion >= 6) records[i].declared = chunks.read();
        if (in.status() != QDataStream::Ok) return;
    }
    if (!in.atEnd()) return;

    // I body sono indipendenti nello snapshot: lettura binaria e indici per il
    // picking vengono ricostruiti su tutti i core. Eventuali record non piu'
    // leggibili ricadranno nella normale rigenerazione parametrica.
    std::vector<ForgeBody> bodies(count);
    std::vector<std::shared_ptr<const Kernel::RayFaceIndex>> rayIndices(count);
    Kernel::parallelFor(count, Kernel::threadCount(0), [&](std::size_t i) {
        if (!records[i].hasBody) return;
        try {
            bodies[i] = std::make_shared<const Kernel::Body>(
                Kernel::readBodyBinary(std::string(records[i].data.constData(), std::size_t(records[i].data.size()))));
            if (records[i].hasDisplay) {
                try {
                    rayIndices[i] = std::make_shared<const Kernel::RayFaceIndex>(*bodies[i]);
                } catch (const std::exception &) {
                    rayIndices[i].reset();
                }
            }
        } catch (const std::exception &) {
            bodies[i].reset();
        }
    });
    for (quint32 i = 0; i < count; ++i)
        if (records[i].hasBody && !bodies[i]) return;  // snapshot atomico: niente dipendenze miste
    // Si validano anche tutte le dichiarazioni prima di applicare lo snapshot
    // o di registrarle. Una cache incoerente ricade nella rigenerazione dalla
    // definizione, evitando B-rep salvati con nomi di provenienza sbagliati.
    QVector<ExtrusionObject> snapshotBodies = state.extrusions;
    for (quint32 i = 0; i < count; ++i) snapshotBodies[int(i)].forgeBody = bodies[i];
    std::vector<DeclaredHistorySnapshot> histories(count);
    for (quint32 i = 0; i < count; ++i)
        if (!readDeclaredHistory(records[i].declared, int(i), snapshotBodies, histories[i])) return;
    for (quint32 i = 0; i < count; ++i) {
        ExtrusionObject &body = state.extrusions[int(i)];
        if (!bodies[i]) continue;
        body.forgeBody = std::move(bodies[i]);
        body.error = records[i].error;
        if (records[i].hasDisplay) {
            body.display = std::move(records[i].display);
            body.display.rayIndex = std::move(rayIndices[i]);
        }
        // Lo snapshot e' uno stato CAD completo, non un risultato usa-e-getta
        // della build: puo' aprirsi anche dopo un aggiornamento del kernel.
        // Alla prima modifica della storia cachedGeometry viene azzerato e il
        // corpo torna a essere calcolato dalla definizione parametrica.
        body.cachedGeometry = true;
        if (histories[i].history) {
            std::vector<ForgeBody> operands;
            for (int operand : histories[i].operandIndices) operands.push_back(snapshotBodies.at(operand).forgeBody);
            registerDeclaredHistory(body.forgeBody, operands, *histories[i].history,
                                   histories[i].labels ? *histories[i].labels : Kernel::FaceAliases{});
        }
    }
}

}

QString canonicalDocumentPath(const QString &path) {
    const QFileInfo info(path);
    const QString existing = info.canonicalFilePath();
    if (!existing.isEmpty()) return existing;
    const QString parent = info.dir().canonicalPath();
    return QDir::cleanPath(parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).filePath(info.fileName()));
}

DocumentFileLock::DocumentFileLock(const QString &path)
    : path_(canonicalDocumentPath(path)), lock_(path_ + QStringLiteral(".lock")) {
    lock_.setStaleLockTime(0);
}

QString DocumentFileLock::acquire() {
    if (lock_.isLocked()) return owns(path_) ? QString() : QStringLiteral("Il blocco esclusivo del documento e' stato perso.");
    if (!lock_.tryLock(0)) {
        if (lock_.error() == QLockFile::LockFailedError) {
            qint64 pid = 0; QString host, app;
            const bool known = lock_.getLockInfo(&pid, &host, &app);
            return QStringLiteral("Il documento e' gia' aperto o bloccato da un'altra sessione.\n%1%2")
                .arg(path_, known ? QStringLiteral("\nComputer: %1 — Applicazione: %2 — Processo: %3").arg(host, app).arg(pid) : QString());
        }
        return QStringLiteral("Impossibile ottenere l'accesso esclusivo a %1.\nVerifica la connessione e i permessi di scrittura nella cartella del documento.").arg(path_);
    }
    QFile marker(lock_.fileName());
    if (!marker.open(QIODevice::ReadOnly) || (identity_ = marker.readAll()).isEmpty()) {
        lock_.unlock();
        return QStringLiteral("Impossibile verificare il blocco esclusivo di %1.").arg(path_);
    }
    return {};
}

bool DocumentFileLock::owns(const QString &path) const {
    if (!lock_.isLocked() || identity_.isEmpty() || canonicalDocumentPath(path) != path_) return false;
    QFile marker(lock_.fileName());
    return marker.open(QIODevice::ReadOnly) && marker.readAll() == identity_;
}

QString encodeDocument(const DocumentState &state, QByteArray &bytes, bool bodies) {
    try {
        if (state.version.documentId.isNull() || (!state.version.checkinId.isNull() && state.version.checkinId == state.baseCheckinId))
            return QStringLiteral("Identita' del documento non valida.");
        DocumentState normalized = state;
        normalizeModelHistory(normalized);
        upgradeTopologyReferences(normalized.extrusions);
        QByteArray payload;
        {
            QBuffer buffer(&payload);
            buffer.open(QIODevice::WriteOnly);
            QDataStream out(&buffer);
            out.setVersion(QDataStream::Qt_6_0);
            out.setFloatingPointPrecision(QDataStream::DoublePrecision);
            out << quint32(normalized.sketches.size());
            for (const SketchObject &sketch : normalized.sketches) write(out, sketch);
            out << quint32(normalized.extrusions.size());
            for (const ExtrusionObject &body : normalized.extrusions) write(out, body);
            for (const double *axis : {normalized.orientation.right, normalized.orientation.up, normalized.orientation.toward})
                for (int k = 0; k < 3; ++k) out << axis[k];
            out << quint32(normalized.modelBodies.size());
            for (const ModelBody &body : normalized.modelBodies)
                out << body.id << body.name << body.visible << body.tipFeatureId << body.meshColor;
            out << qint32(normalized.lengthUnit);
            out << normalized.version.documentId << normalized.version.checkinId << normalized.baseCheckinId;
        }
        QByteArray prepared;
        QDataStream out(&prepared, QIODevice::WriteOnly);
        out.writeRawData(kMagic, 4);
        out << kVersion << kZlib;
        const QByteArray compressed = qCompress(payload, 9);
        out << quint32(compressed.size());
        out.writeRawData(compressed.constData(), int(compressed.size()));
        if (bodies) {
            const QByteArray cache = bodyCache(payload, normalized);
            out.writeRawData(cache.constData(), int(cache.size()));
        }
        if (out.status() != QDataStream::Ok) return QStringLiteral("Codifica del documento fallita.");
        bytes = std::move(prepared);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

void forkDocumentIdentity(DocumentState &state) {
    state.version = {QUuid::createUuid(), {}};
    state.baseCheckinId = {};
}

QString saveDocumentFile(const QString &path, const DocumentState &state, bool bodies, const DocumentFileLock *lock) {
    if (!state.version.checkinId.isNull())
        return QStringLiteral("Documento archiviato: usare una copia di lavoro o Salva con nome.");
    std::unique_ptr<DocumentFileLock> temporary;
    if (!lock) {
        temporary = std::make_unique<DocumentFileLock>(path);
        const QString error = temporary->acquire();
        if (!error.isEmpty()) return error;
        lock = temporary.get();
    }
    if (!lock->owns(path)) return QStringLiteral("Salvataggio annullato: il documento non ha piu' un blocco esclusivo valido.");
    QByteArray bytes;
    const auto error = encodeDocument(state, bytes, bodies);
    if (!error.isEmpty()) return error;
    QSaveFile file(lock->path());
    if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("Impossibile scrivere %1: %2").arg(path, file.errorString());
    if (file.write(bytes) != bytes.size()) return QStringLiteral("Errore di scrittura su %1: %2").arg(path, file.errorString());
    if (!lock->owns(path)) {
        file.cancelWriting();
        return QStringLiteral("Salvataggio annullato: il blocco esclusivo del documento e' stato perso.");
    }
    WorkspacePaths workspace;
    const auto workspaceError = ensureDocumentWorkspace(lock->path(), workspace);
    if (!workspaceError.isEmpty()) return QStringLiteral("Salvataggio annullato: %1").arg(workspaceError);
    if (!lock->owns(path)) return QStringLiteral("Salvataggio annullato: blocco esclusivo perso durante la preparazione del workspace.");
    if (!file.commit()) return QStringLiteral("Errore di scrittura su %1: %2").arg(path, file.errorString());
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
    if (version >= 19) {
        if (!readCount(in, count)) return QStringLiteral("Il file e' danneggiato (storyboard).");
        loaded.modelBodies.resize(int(count));
        for (ModelBody &body : loaded.modelBodies) {
            in >> body.id >> body.name >> body.visible >> body.tipFeatureId;
            if (version >= 22) in >> body.meshColor;
        }
        if (in.status() != QDataStream::Ok) return QStringLiteral("Il file e' danneggiato (storyboard).");
    }
    if (version >= 26) {
        qint32 unit = 0;
        in >> unit;
        if (in.status() != QDataStream::Ok) return QStringLiteral("Il file e' danneggiato (unita' di misura).");
        if (unit < int(LengthUnit::Millimeter) || unit > int(LengthUnit::Foot))
            return QStringLiteral("Il file contiene un'unita' di misura non valida.");
        loaded.lengthUnit = LengthUnit(unit);
        loaded.lengthUnitSet = true;
    } else {
        // Tutti i formati precedenti mostravano e accettavano millimetri.
        loaded.lengthUnit = LengthUnit::Millimeter;
        loaded.lengthUnitSet = true;
    }
    if (version >= 50) {
        in >> loaded.version.documentId >> loaded.version.checkinId >> loaded.baseCheckinId;
        if (in.status() != QDataStream::Ok || loaded.version.documentId.isNull()
            || (!loaded.version.checkinId.isNull() && loaded.version.checkinId == loaded.baseCheckinId))
            return QStringLiteral("Il file e' danneggiato (identita' del documento).");
    }
    if (!buffer.atEnd()) return QStringLiteral("Il file e' danneggiato (dati in piu' alla fine).");
    normalizeModelHistory(loaded);
    return {};
}

}

QString loadDocumentFile(const QString &path, DocumentState &state, bool previewCache) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Impossibile aprire %1: %2").arg(path, file.errorString());
    const QByteArray data = file.readAll();
    return decodeDocument(data, state, previewCache);
}

QString decodeDocument(const QByteArray &data, DocumentState &state, bool previewCache) {
    if (data.size() < 7 || !data.startsWith(QByteArray(kMagic, 4))) return QStringLiteral("Dati non riconosciuti come documento ForgeCAD.");
    QDataStream header(data);
    header.skipRawData(4);
    quint16 version = 0;
    quint8 compression = 0;
    header >> version >> compression;
    if (version > kVersion) return QStringLiteral("Il file e' stato scritto da una versione piu' recente di ForgeCAD (formato %1).").arg(version);
    if (compression != kZlib) return QStringLiteral("Compressione sconosciuta nel file.");
    QByteArray compressed = data.mid(7), cache;
    if (version >= 14) {
        quint32 size = 0;
        header >> size;
        if (header.status() != QDataStream::Ok || qsizetype(size) > data.size() - 11) return QStringLiteral("Il file e' danneggiato (lunghezza dei dati).");
        compressed = data.mid(11, qsizetype(size));
        cache = data.mid(11 + qsizetype(size));
    }
    const QByteArray payload = qUncompress(compressed);
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
    if (!cache.isEmpty()) applyBodyCache(cache, payload, loaded, previewCache);
    state = std::move(loaded);
    return {};
}

}
