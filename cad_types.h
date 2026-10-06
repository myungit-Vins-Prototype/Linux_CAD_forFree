#ifndef FORGECAD_TYPES_H
#define FORGECAD_TYPES_H

#include <QByteArray>
#include <QColor>
#include <QPair>
#include <QMatrix4x4>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <memory>

namespace ForgeCad::Kernel {
class Body;
struct RayFaceIndex;
template <int N>
class Curve;
}
namespace ForgeCad {
// B-rep del kernel proprio (kernel/), immutabile e condiviso tra le istantanee dell'Undo.
using ForgeBody = std::shared_ptr<const Kernel::Body>;
// Curva esatta del kernel proprio (le funzioni curva: elica, spirale).
using ForgeCurve = std::shared_ptr<const Kernel::Curve<3>>;
}

// Precisione: tutte le coordinate del modello sono in double (QPointF usa
// qreal = double). I float (QVector3D) servono solo per la visualizzazione.

// Gli ultimi cinque sono strumenti di modifica (non creano curve): taglia,
// estendi, spezza, raccordo e smusso tra segmenti.
// Select: nessuna creazione, il clic seleziona (e' lo strumento all'apertura di uno schizzo).
// Rectangle (due angoli) e CenterRectangle (centro e un angolo) creano quattro
// segmenti orizzontali e verticali collegati; Ellipse e' una curva. I valori
// sono salvati nei file: i nuovi strumenti vanno in fondo.
// Converted: curva di riferimento presa da uno spigolo, una curva o una
// sezione di un corpo (B-spline razionale esatta: poli, pesi, nodi espansi e
// grado in CurveObject); ConvertEdges: lo strumento che le crea con un clic.
// Dimension: lo strumento Quota (prima si scelgono le entita', poi si mette la quota).
enum class DrawingTool { Line, Polyline, Spline, Nurbs, Circle, Arc, Polygon, ConstructionLine, Trim, Extend, Split, Fillet, Chamfer, Select,
                         Rectangle, CenterRectangle, Ellipse, ThreePointArc, TangentArc, Converted, ConvertEdges, Dimension };
enum class SnapKind { None, Endpoint, Midpoint, Nearest };

enum class ReferencePlane { XY, XZ, YZ };

enum class DisplayMode { Wireframe, Mesh, MeshWithEdges };

using SketchSegment = QPair<QPointF, QPointF>;

// Entita' curva dello schizzo, in coordinate del piano di schizzo.
// La geometria esatta e' definita dai soli parametri:
//  - Spline: punti di passaggio + maniglie tangenti (Bezier cubiche C1 a tratti)
//  - Nurbs: poli, pesi (vuoto = tutti 1), nodi uniformi "clamped", grado <= 3
//  - Circle: centro, punto sulla circonferenza
//  - Arc: centro, punto iniziale (definisce il raggio), punto finale (definisce l'angolo)
//  - Polygon: centro, primo vertice, numero di lati
//  - Ellipse: centro, estremo di un semiasse (lunghezza e direzione), punto
//    sull'altro semiasse (la sua lunghezza e' la distanza dal centro: il
//    punto sta sulla perpendicolare)
//  - Rectangle / CenterRectangle: due angoli / centro e angolo (solo per
//    l'anteprima: il rettangolo diventa quattro segmenti)
//  - ThreePointArc / TangentArc: solo strumenti di disegno, creano un Arc
// `samples` e' solo un'approssimazione per disegnare e selezionare a schermo.
// Le entita' di costruzione (`construction`) non fanno parte dei profili:
// servono da riferimento (assi di rivoluzione, agganci).
struct CurveObject {
    DrawingTool tool = DrawingTool::Spline;
    QVector<QPointF> controlPoints;
    QVector<double> weights;
    QVector<QPair<QPointF, QPointF>> tangentHandles;
    // Per ogni punto di una spline, le due maniglie restano allineate e
    // opposte. Le lunghezze possono essere quotate separatamente.
    QVector<bool> tangentLinked;
    int sides = 0;
    QVector<double> knots;  // Converted: nodi espansi (poli + grado + 1)
    int degree = 3;         // Converted
    QVector<QPointF> samples;
    bool numericallyValid = false;
    bool construction = false;
};

struct CoincidentConstraint {
    int firstKind = 0;
    int firstElement = -1;
    int firstPoint = -1;
    int secondKind = 0;
    int secondElement = -1;
    int secondPoint = -1;
};

// Piano di schizzo su una faccia piana di un corpo (SketchObject::plane =
// kFacePlane): origine e assi esatti nel modello. L'asse Y dello schizzo e' la
// proiezione di Z del modello sul piano (di Y se il piano e' orizzontale),
// X = Y x normale; l'origine e' la proiezione dell'origine del modello.
constexpr int kFacePlane = 3;
struct SketchFrame {
    double origin[3] = {0.0, 0.0, 0.0};
    double xAxis[3] = {1.0, 0.0, 0.0};
    double normal[3] = {0.0, 0.0, 1.0};  // uscente dalla faccia: l'estrusione positiva aggiunge materiale
};

// Riferimento di un vincolo geometrico: un'entita' dello schizzo, un suo
// punto o un riferimento del piano.
//  - kind 0: segmento `element` (point -1: la retta; 0 / 1: gli estremi);
//  - kind 1: curva `element` (point -1: la curva; k: il suo punto di controllo
//    k: per cerchi, archi, ellissi e poligoni 0 e' il centro);
//  - kind 2: riferimento del piano (element 0 origine, 1 asse X, 2 asse Y).
// Le maniglie tangenti delle spline sono punti della curva: point =
// kHandlePoint + 2 k + lato (lato 0 la maniglia entrante del punto k, 1 l'uscente).
constexpr int kHandlePoint = 1 << 20;
inline bool isHandlePoint(int point) { return point >= kHandlePoint; }
inline int handlePoint(int k, int side) { return kHandlePoint + 2 * k + side; }
struct ConstraintRef {
    int kind = -1;
    int element = -1;
    int point = -1;

    bool isPoint() const { return point >= 0 || (kind == 2 && element == 0); }
    bool operator==(const ConstraintRef &other) const { return kind == other.kind && element == other.element && point == other.point; }
    bool operator!=(const ConstraintRef &other) const { return !(*this == other); }
};

// Tipi di vincolo (valori salvati nei file: i nuovi in fondo). Le quote
// (Distance, Angle, Radius, Diameter) hanno un valore: lunghezze in unita'
// del modello, angoli in gradi.
enum class ConstraintType {
    Coincident = 0, Horizontal, Vertical, Parallel, Perpendicular, Collinear, Tangent, Equal, Concentric, Midpoint,
    PointOnCurve, Fix, Distance, Angle, Radius, Diameter, Pattern, Symmetric, AxisRadius, AxisDiameter,
    HorizontalDistance, VerticalDistance
};
// HorizontalDistance / VerticalDistance: quota lungo l'asse X (o Y) dello
// schizzo tra due punti (anche di entita' diverse) o tra gli estremi di un
// segmento: |dx| (o |dy|) = value.
// Symmetric: first e second simmetrici rispetto alla retta `third` (punti;
// segmenti, con gli estremi accoppiati come dice value: 0 inizio con inizio,
// 1 inizio con fine; cerchi e archi: centri simmetrici e raggi uguali).
// AxisRadius / AxisDiameter: distanza (o il doppio) di un punto o di un
// segmento parallelo dall'asse `second` (asse di simmetria, linea di
// costruzione o asse del piano): le quote di raggio e diametro di un profilo
// di rivoluzione.

// Ripetizione parametrica nello schizzo (vincolo Pattern): le copie restano
// l'immagine delle entita' di partenza, qualunque cosa cambi. Tipi come
// SketchPattern (0 lineare anche a griglia, 1 circolare, 2 specchio).
//  - sources: le entita' ripetute (point -1); copies: per ogni istanza (1, 2,
//    ... nell'ordine: lineare riga per riga, j esterno) una copia per
//    sorgente, kind -1 se la copia e' stata eliminata;
//  - il passo (spacing, spacing2) o l'angolo (gradi: totale se spread, 360 =
//    giro diviso in parti uguali, altrimenti il passo) sono incognite del
//    risolutore: con `dimensioned` (`dimensioned2`) sono quote (valore
//    fissato), altrimenti restano libere (gradi di liberta');
//  - direction/direction2: una retta dello schizzo (segmento o asse) o, con
//    kind -1, l'angolo fisso directionAngle (gradi dall'asse X);
//  - center: un punto dello schizzo o, con kind -1, centerPoint;
//  - axis: la retta dello specchio (segmento o asse) o, con kind -1, la retta
//    per axisPoint lungo axisDirection.
struct SketchPatternData {
    int kind = 0;
    QVector<ConstraintRef> sources;
    QVector<ConstraintRef> copies;
    int count = 3, count2 = 1;
    double spacing = 10.0, spacing2 = 10.0;
    double angle = 360.0;
    bool spread = true;
    bool dimensioned = true, dimensioned2 = true;
    ConstraintRef direction, direction2;
    double directionAngle = 0.0, directionAngle2 = 90.0;
    ConstraintRef center;
    QPointF centerPoint;
    ConstraintRef axis;
    QPointF axisPoint, axisDirection{0.0, 1.0};
    int instances() const { return kind == 2 ? 1 : kind == 1 ? count - 1 : count * qMax(1, count2) - 1; }
};

// Vincolo geometrico dello schizzo, mantenuto dal risolutore (cad_constraints).
struct SketchConstraint {
    ConstraintType type = ConstraintType::Coincident;
    ConstraintRef first, second;  // second.kind < 0: vincolo su un solo riferimento
    double value = 0.0;           // quote; Tangent tra cerchi: 0 esterna, 1 interna
    QVector<QPointF> positions;   // Fix: posizioni fissate dei punti del riferimento
    // Quote: dove sta la quota nel disegno (coordinate dello schizzo: il punto
    // per cui passa la linea di misura, la direzione del raggio, il raggio
    // dell'arco dell'angolo); se non e' stata spostata, una posizione di default.
    QPointF placement;
    bool placed = false;
    SketchPatternData pattern;  // solo per Pattern
    ConstraintRef third;        // Symmetric: la retta di simmetria
};

struct SketchObject {
    QString name;
    int plane = 0;  // 0 XY, 1 XZ, 2 YZ, kFacePlane: su una faccia (frame)
    QVector<SketchSegment> segments;
    QVector<int> constraints;
    QVector<double> segmentLengths;
    QVector<double> segmentAngles;
    QVector<CurveObject> curves;
    QVector<CoincidentConstraint> coincidentConstraints;
    bool visible = true;
    // Segmenti di costruzione: indici in `segments` (linee di riferimento che
    // non entrano nei profili, per esempio l'asse di una rivoluzione).
    QVector<int> constructionSegments;
    // Assi di simmetria: segmenti di costruzione (indici in `segments`)
    // disegnati come linee d'asse; fanno da riferimento a simmetrie e quote di
    // raggio e diametro.
    QVector<int> symmetryAxes;
    // Sistema esplicito del piano (plane == kFacePlane, o customFrame sui piani
    // di riferimento: gli schizzi nuovi prendono gli assi dello schermo della
    // vista normale al piano con l'orientamento degli assi del documento).
    SketchFrame frame;
    bool customFrame = false;
    QString faceSource;  // corpo da cui viene il piano (solo per l'albero)
    // Schizzo su un piano di costruzione (plane == kFacePlane): l'indice del
    // corpo DatumPlane; quando il piano si rigenera `frame` lo segue. -1: fisso.
    int datumPlane = -1;
    // Vincoli geometrici (oggetti): coincidenze, orizzontale/verticale,
    // parallelismo, quote... I vecchi dati (codici in `constraints`,
    // `segmentLengths`, `segmentAngles`, `coincidentConstraints`) si
    // convertono in questi all'apertura dei file vecchi e poi restano vuoti o
    // neutri (codici -1, lunghezze 0, angoli -1).
    QVector<SketchConstraint> geometricConstraints;

    bool isConstructionSegment(int index) const { return constructionSegments.contains(index); }
};

enum class BooleanOperation { Union = 0, Intersection = 1, Difference = 2 };

// Approssimazione della forma esatta usata SOLO per il disegno a schermo.
struct BodyDisplay {
    QVector<QVector3D> vertices;   // tre vertici per triangolo
    QVector<QVector3D> normals;    // una normale per vertice
    QVector<QVector<QVector3D>> edges;
    QVector<int> edgeIds;               // polilinea visualizzata -> EdgeId del B-rep
    QVector<int> faceIds;               // punto etichetta -> FaceId del B-rep
    QVector<int> triangleFaces;         // triangolo -> FaceId del B-rep (vuoto se non noto)
    QVector<QVector3D> faceLabelPoints;  // posizione delle etichette topologiche delle facce
    QVector<QVector<QVector3D>> constructionCurves; // isoparametriche U/V delle anteprime
    QVector<QVector<int>> faceEdges; // faccia B-rep -> polilinee, senza ricerche geometriche durante il disegno
    std::shared_ptr<const ForgeCad::Kernel::RayFaceIndex> rayIndex;
    // Ripetizioni disgiunte: il risultato finale resta sopra per selezione,
    // bordi e misure, ma il renderer puo' disegnare questa mesh base con una
    // sola chiamata instanced. Vuoti negli altri casi e nei file salvati.
    std::shared_ptr<const BodyDisplay> instancedBase;
    QVector<QMatrix4x4> instanceTransforms;
    int quality = -1;
};

// Funzione che genera un corpo che non e' una booleana (operation = -1).
// SheetTrim / SheetExtend: taglio ed estensione di una superficie (lamina,
// estrusione di un profilo aperto): i valori si salvano nei file.
// Scale: scala uniforme di un corpo. Helix: curva (elica o spirale), non un
// solido: serve da percorso agli sweep. Sweep: profilo lungo un percorso.
// Loft: superficie o solido per una successione di sezioni. Imported: forma
// letta da un file STEP/IGES. DatumPlane: piano di costruzione (niente solido).
// Pattern: ripetizione (lineare, circolare, specchio) di un corpo o di una funzione.
// Transform: spostamento e rotazione di un corpo (o di una sua copia).
// SurfaceOffset: lamina a distanza `distance` dalle facce `offsetFaces` del
// corpo firstBody (vuoto: tutte), le facce tangenti cucite in una superficie.
// Sew: cucitura delle superfici firstBody + booleanTools in una (solido se
// chiusa e sewSolid), entro sewTolerance.
// Ruled: superficie rigata tra le catene di curve ruledFirst e ruledSecond.
// PlanarSurface: lamina piana delimitata dai contorni chiusi dello schizzo
// sketchIndex (planarRefs vuoto) o dai bordi planarRefs scelti nella vista.
// DeleteFace: il corpo firstBody senza le facce offsetFaces (lamina con le
// facce restanti: un solido diventa una superficie aperta).
// BoundarySurface: superficie tra curve (patch di Coons) delimitata dal
// contorno chiuso dei riferimenti curva planarRefs (3 o 4 lati).
// Shell: il solido firstBody svuotato con pareti di spessore `distance` verso
// l'interno, le facce offsetFaces tolte per l'apertura (nessuna: cavita' chiusa).
enum class BodyFeature { Extrusion = 0, Revolution = 1, Primitive = 2, Blend = 3, SheetTrim = 4, SheetExtend = 5, Scale = 6, Helix = 7, Sweep = 8, Loft = 9,
                         Imported = 10, DatumPlane = 11, Pattern = 12, Transform = 13, SurfaceOffset = 14, Sew = 15, Ruled = 16, PlanarSurface = 17,
                         DeleteFace = 18, BoundarySurface = 19, Shell = 20, Thread = 21 };

// Riferimento leggero a una sotto-entita' del B-rep. `subshape` e' l'ID
// topologico al momento della scelta, `geometry` il tipo di curva/superficie.
// Il punto resta sia una firma geometrica sia il fallback per i file vecchi.
struct EdgePoint {
    double x = 0.0, y = 0.0, z = 0.0;
    int subshape = -1;
    int geometry = -1;
    int context = -1;
    // Nei raccordi e negli smussi (blendEdges): 0 = lo spigolo, 1 = tutti i
    // bordi della faccia (riferimento di faccia). La faccia si ritrova anche
    // dopo che altre feature ne hanno cambiato i bordi (riordino della storia),
    // e i suoi bordi si prendono sullo stato corrente.
    int role = 0;
};
inline constexpr int kEdgePointEdge = 0;
inline constexpr int kEdgePointFaceBoundary = 1;

// Faccia di un corpo sotto il puntatore (geometria esatta): per scegliere i
// suoi bordi (raccordi e smussi) e, se e' piana, per schizzarci sopra.
struct FaceHit {
    double distance = 0.0;  // lungo il raggio di vista
    int face = -1;          // indice della faccia nel corpo
    bool planar = false;
    double point[3] = {0.0, 0.0, 0.0};   // un punto del piano (se planar)
    double normal[3] = {0.0, 0.0, 1.0};  // normale uscente (se planar)
    QVector<EdgePoint> edges;            // un punto interno di ogni spigolo della faccia (senza cuciture)
};

// Solidi elementari. Il sistema del solido ha l'origine in `origin` e gli assi
// del piano di riferimento `plane` (come gli schizzi: Z = normale del piano).
//  - Box: parallelepipedo [0, size0] x [0, size1] x [0, size2];
//  - Cylinder: raggio size0, altezza size1 lungo Z;
//  - Sphere: centro nell'origine, raggio size0;
//  - Cone: raggio size0 alla base (z = 0), size1 in cima (z = size2), uno dei due puo' essere 0;
//  - Torus: raggio maggiore size0 e minore size1, nel piano XY del sistema.
enum class PrimitiveKind { Box = 0, Cylinder = 1, Sphere = 2, Cone = 3, Torus = 4 };

struct PrimitiveParameters {
    PrimitiveKind kind = PrimitiveKind::Box;
    int plane = 0;
    double origin[3] = {0.0, 0.0, 0.0};
    double size[3] = {1.0, 1.0, 1.0};
};

// Elica (cilindrica o conica) o spirale piana (di Archimede). La base da' asse,
// raggio iniziale e riferimento dell'angolo:
//  - source 0: cerchio o arco `curve` dello schizzo sketchIndex (-1: il primo):
//    asse = normale dello schizzo, angolo 0 verso il punto del cerchio;
//  - source 1: spigolo circolare del corpo firstBody piu' vicino a `reference`
//    (asse verso la faccia cilindrica o conica coassiale, se c'e');
//  - source 2: faccia cilindrica o conica del corpo firstBody che contiene
//    `reference` (parte dall'estremo della faccia; sul cono la conicita' e'
//    quella della faccia).
// Modi dell'elica: 0 passo e giri, 1 altezza e giri, 2 altezza e passo (il
// terzo valore si ricava); la spirale usa passo (crescita del raggio per giro)
// e giri. Conicita' in gradi (positiva: il raggio cresce lungo l'asse),
// angolo di partenza in gradi; sinistrorsa = oraria guardando lungo l'asse.
struct HelixParameters {
    bool spiral = false;
    int mode = 0;
    double pitch = 1.0;
    double turns = 5.0;
    double height = 5.0;
    double taper = 0.0;
    double startAngle = 0.0;
    bool leftHanded = false;
    bool reverse = false;
    int source = 0;
    int curve = -1;
    EdgePoint reference;
};

// Riferimento geometrico di un piano di costruzione, scelto nella vista.
// Tipi (valori salvati nei file):
//  - 0 origine del modello;
//  - 1 piano di riferimento `index` (0 XY, 1 XZ, 2 YZ);
//  - 2 asse del modello `index` (0 X, 1 Y, 2 Z);
//  - 3 vertice, 4 spigolo, 5 faccia del corpo `index`: quello piu' vicino a
//    `point` (dopo una rigenerazione, come gli spigoli dei raccordi);
//  - 6 punto dello schizzo `index` (`element`: kind 0 estremo di un segmento,
//    1 punto di una curva, come ConstraintRef);
//  - 7 entita' dello schizzo `index` (`element` con point -1: segmento o curva);
//  - 8 piano di costruzione (corpo DatumPlane `index`);
//  - 9 curva del corpo `index` (elica, spirale).
//  - 10 estremo della curva del corpo `index` (`element.point`: 0 inizio,
//    1 fine). Il punto salvato serve solo al picking; la risoluzione usa il
//    dominio esatto della curva corrente.
struct GeometryRef {
    int kind = -1;
    int index = -1;
    ConstraintRef element;
    EdgePoint point;
    quint64 featureId = 0;  // proprietario persistente; index e' la cache operativa
};

// Asse della rivoluzione dato da un riferimento (ExtrusionObject::revolveAxisRef):
// -3 resta "segmento eliminato".
constexpr int kRevolveAxisReference = -4;

// Piano di costruzione. Modi (i riferimenti in `refs`, nell'ordine):
//  0 parallelo a un piano a distanza `distance`;
//  1 per tre punti;
//  2 normale a una curva: curva e punto (il piano passa per il punto della
//    curva piu' vicino, o per il punto stesso se !onCurve);
//  3 per una retta e un punto;
//  4 parallelo a un piano per un punto;
//  5 per una retta, ad angolo `angle` (gradi) da un piano;
//  6 piano medio tra due piani (paralleli; altrimenti il bisettore).
// `flip` gira la normale; `size` e' la mezza misura a video (0: automatica).
struct DatumParameters {
    int mode = 0;
    QVector<GeometryRef> refs;
    double distance = 10.0;
    double angle = 45.0;
    bool flip = false;
    bool onCurve = true;
    double size = 0.0;
};

// Una catena scelta dentro uno schizzo. Gli indici separati mantengono la
// distinzione fra segmenti e curve e permettono di usare piu' percorsi
// disconnessi appartenenti allo stesso schizzo.
struct SketchPathRef {
    int sketch = -1;
    QVector<int> segments;
    QVector<int> curves;
    bool empty() const { return segments.isEmpty() && curves.isEmpty(); }
    bool operator==(const SketchPathRef &other) const {
        return sketch == other.sketch && segments == other.segments && curves == other.curves;
    }
};

// Ripetizione (BodyFeature::Pattern) del corpo `firstBody`; le copie si
// uniscono al corpo in un solo body. Tipi:
//  - 0 lineare: `count` istanze lungo la direzione di refs[0] ogni `spacing` e,
//    se count2 > 1, `count2` lungo refs[1] ogni `spacing2` (griglia);
//  - 1 circolare: `count` istanze attorno all'asse refs[0]; `angle` (gradi)
//    e' l'angolo totale se `spread` (360: il giro in parti uguali; altrimenti
//    tra la prima e l'ultima istanza), il passo altrimenti;
//  - 2 specchio: l'immagine rispetto al piano refs[0] (con il corpo se
//    `keepOriginal`, altrimenti da sola).
// I riferimenti (GeometryRef, come per i piani di costruzione) si risolvono a
// ogni rigenerazione: per una direzione una retta (asse del modello, spigolo
// rettilineo, faccia cilindrica o conica = il suo asse, segmento di uno
// schizzo) o la normale di un piano; per l'asse una retta; per lo specchio un
// piano (di riferimento, di costruzione, faccia piana). `flip`/`flip2` girano
// il verso. `featureOnly`: se il corpo e' un'unione o una differenza si ripete
// il suo secondo operando (lo strumento: fori, sporgenze) e l'operazione si
// applica una volta sola al primo (ripetizione della funzione).
struct PatternParameters {
    int kind = 0;
    QVector<GeometryRef> refs;
    int count = 3, count2 = 1;
    double spacing = 10.0, spacing2 = 10.0;
    double angle = 360.0;
    bool spread = true;
    bool flip = false, flip2 = false;
    bool keepOriginal = true;
    bool featureOnly = false;
};

// Spostamento di un corpo (BodyFeature::Transform): prima la rotazione di
// `angle` gradi attorno alla retta `axis` (GeometryRef risolto a ogni
// rigenerazione: asse del modello, spigolo rettilineo, asse di una faccia
// cilindrica, segmento di uno schizzo; verso destrorso), poi la traslazione.
// Con `copy` il corpo di partenza resta visibile (il risultato e' una copia).
struct TransformParameters {
    double translation[3] = {0.0, 0.0, 0.0};
    GeometryRef axis{2, 2, {}, {}};
    double angle = 0.0;
    bool copy = false;
};

// Filettatura reale ricavata da una faccia cilindrica o conica del corpo
// `firstBody`. Standard: 0 ISO M, 1 UNC, 2 UNF, 3 BSPP/G, 4 BSPT/R,
// 5 NPT, 6 ISO trapezoidale Tr, 7 ACME. Il diametro e il verso interno/esterno
// sono letti dalla faccia; pitch e length sono sempre millimetri.
struct ThreadParameters {
    int standard = 0;
    QString designation;
    double pitch = 1.5;
    double length = 0.0;  // 0: tutta la lunghezza della faccia
    bool leftHanded = false;
    bool reverse = false;
    EdgePoint face;
};

// Corpo della scena, definito in modo parametrico:
//  - estrusione (operation = -1, feature Extrusion): profili chiusi dello
//    schizzo `sketchIndex` estrusi di `distance` lungo la normale del piano;
//  - rivoluzione (feature Revolution): profili chiusi dello schizzo
//    `sketchIndex` ruotati di `revolveAngle` gradi (con segno: verso
//    destrorso attorno all'asse orientato; 360 = giro completo) attorno al
//    segmento `revolveAxis` dello schizzo (-1 = asse X, -2 = asse Y del piano;
//    kRevolveAxisReference: la retta `revolveAxisRef` scelta nella vista, che
//    deve stare nel piano dello schizzo);
//  - primitiva (feature Primitive): `primitive`;
//  - raccordo o smusso (feature Blend): gli spigoli `blendEdges` del corpo
//    `firstBody` raccordati con raggio `blendSize` (o smussati a distanza
//    `blendSize` se `blendChamfer`);
//  - booleana: `operation` tra i corpi `firstBody` e `secondBody`.
// Il B-rep esatto rigenerato dalla definizione e' `forgeBody` (kernel proprio).
// Smusso: mode 0 la stessa distanza sulle due facce; 1 due distanze (la
// seconda in `second`); 2 distanza e angolo (gradi, in `second`, tra lo
// smusso e la faccia della distanza). La prima distanza sta sulla faccia di
// riferimento: quella con la normale uscente piu' verso +Z (a parita' +X,
// poi +Y) nel punto medio dello spigolo; con `flip` sull'altra.
struct ChamferSpec {
    int mode = 0;
    double second = 1.0;
    bool flip = false;
};

// Parte di una superficie divisa da uno strumento (finestra del taglio): un
// suo punto e la sua area.
struct SheetPiece {
    EdgePoint point;
    double area = 0.0;
};

struct ExtrusionObject {
    // Identita' persistente della feature e del corpo logico a cui appartiene.
    // Gli indici nel vettore restano il formato operativo delle dipendenze;
    // questi identificatori non cambiano quando la storyboard viene riordinata.
    quint64 featureId = 0;
    quint64 modelBodyId = 0;  // 0: elemento di riferimento, non uno stadio di un corpo
    bool suppressed = false;
    QString name;
    int sketchIndex = -1;
    int plane = 0;
    double distance = 1.0;
    bool solid = false;
    bool visible = true;
    int operation = -1;
    BodyFeature feature = BodyFeature::Extrusion;
    int revolveAxis = -1;
    double revolveAngle = 360.0;
    GeometryRef revolveAxisRef;  // con revolveAxis == kRevolveAxisReference
    PrimitiveParameters primitive;
    bool blendChamfer = false;
    ChamferSpec chamferSpec;
    double blendSize = 1.0;
    QVector<EdgePoint> blendEdges;  // anche i bordi di SheetExtend (blendSize = distanza)
    // Feature della base su cui sono stati scelti blendEdges (0: non nota). Se
    // la base e' ancora quella (anche rigenerata) gli ID valgono piu' dei
    // punti; dopo un riordino della storia si cerca per geometria.
    quint64 blendBaseFeature = 0;
    // SheetTrim: firstBody = superficie, secondBody = corpo strumento (-1: il
    // piano di riferimento trimPlane), trimKeep un punto della parte da tenere.
    int trimPlane = 0;
    EdgePoint trimKeep;
    bool extendLinear = false;  // SheetExtend: tangente (rigata) invece della stessa superficie
    // Scale: firstBody = corpo, fattore uniforme e centro (0 origine, 1 baricentro del solido, 2 il punto scaleCenter).
    double scaleFactor = 1.0;
    int scaleCenterMode = 0;
    EdgePoint scaleCenter;
    // Helix: parametri dell'elica o spirale (la base nello schizzo sketchIndex o nel corpo firstBody).
    HelixParameters helix;
    // Sweep: profilo dello schizzo sketchIndex lungo il percorso: lo schizzo
    // pathSketch (sweepPath 0) o la curva firstBody (1, un'elica). sweepMode: 0
    // torsione minima, 1 Frenet, 2 orientamento costante.
    int sweepPath = 0;
    int pathSketch = -1;
    QVector<int> pathSegments;
    QVector<int> pathCurves;
    int sweepMode = 0;
    // Sweep di superficie: lamina senza coperchi (un profilo chiuso da' un
    // tubo aperto alle estremita'); niente fusione con i solidi.
    bool sweepSurface = false;
    // Loft: sezioni e curve guida (schizzi, nell'ordine), rigato o liscio.
    // La continuita' 0/1/2 corrisponde a G0/G1/G2; le influenze sono [0, 1].
    QVector<int> loftSketches;
    QVector<int> loftGuides;
    QVector<SketchPathRef> loftGuidePaths;
    bool loftRuled = false;
    int loftStartContinuity = 0;
    int loftEndContinuity = 0;
    int loftGuideContinuity = 1;
    double loftGuideInfluence = 1.0;
    double loftStartInfluence = 1.0;
    double loftEndInfluence = 1.0;
    // Loft di superficie: lamina senza coperchi (sezioni chiuse: un tubo).
    bool loftSurface = false;
    // Imported: il body letto dal file come testo STEP scritto dal kernel
    // (fk_step, numeri a 17 cifre: la stessa geometria) e il nome del file d'origine.
    QByteArray importData;
    QString importSource;
    // DatumPlane: definizione e, dopo la costruzione, il piano (origine = centro
    // a video, asse X, normale) in `datumFrame` se `datumValid`.
    DatumParameters datum;
    // Pattern: la ripetizione del corpo firstBody.
    PatternParameters pattern;
    // Thread: filetto parametrico sulla faccia cilindrica/conica scelta.
    ThreadParameters thread;
    // Extrusion: condizione di fine (0 la distanza `distance`; 1 fino a un
    // punto; 2 fino a uno spigolo, nel suo punto piu' vicino al riferimento;
    // 3 fino a una faccia o a un piano) con il riferimento `extentRef`
    // (GeometryRef come i piani di costruzione).
    int extent = 0;
    GeometryRef extentRef;
    // Extrusion: versi rispetto al piano dello schizzo (0 uno solo; 1
    // simmetrica: `distance` in tutto, meta' per parte, solo con la fine a
    // distanza; 2 due versi: la fine scelta da una parte e `distance2` > 0
    // dall'altra).
    int extrudeSides = 0;
    double distance2 = 1.0;
    // Estrusione, rivoluzione e sweep: fusione del risultato con altri solidi (mergeOperation 0
    // corpo nuovo, 1 unione, 2 sottrazione) nei corpi `mergeBodies` (indici
    // minori, nascosti come gli operandi delle booleane); `mergeAuto` dice che
    // sono stati scelti da soli, tra quelli che hanno punti in comune con la
    // funzione. `mergeProbe` (non salvato): alla costruzione mergeBodies
    // sono i candidati e restano solo quelli che la toccano.
    int mergeOperation = 0;
    bool mergeAuto = true;
    QVector<int> mergeBodies;
    bool mergeProbe = false;
    // Booleane: gli strumenti oltre a secondBody (A op B op C ...).
    QVector<int> booleanTools;
    // Transform: spostamento del corpo firstBody.
    TransformParameters move;
    // SurfaceOffset: le facce scelte (vuoto: tutte le facce del corpo).
    // DeleteFace: le facce da togliere (almeno una); Shell: le facce dell'apertura.
    QVector<EdgePoint> offsetFaces;
    // DeleteFace multi-risultato: -1 = tutte le componenti (file storici),
    // altrimenti la componente connessa esposta da questo corpo logico.
    int deleteComponent = -1;
    // Sew: tolleranza della cucitura e solido se il risultato e' chiuso.
    double sewTolerance = 1e-5;
    bool sewSolid = true;
    // Ruled: le due curve, ognuna una catena di riferimenti curva (GeometryRef
    // come i piani di costruzione: spigoli dei corpi, entita' degli schizzi,
    // curve come le eliche), usati per intero; il verso e il punto di
    // partenza della seconda si accordano alla prima da soli.
    QVector<GeometryRef> ruledFirst, ruledSecond;
    // PlanarSurface: bordi scelti nella vista (spigoli, entita' degli schizzi,
    // curve), raggruppati in contorni chiusi per estremi comuni; vuoto: i
    // contorni chiusi dello schizzo sketchIndex.
    QVector<GeometryRef> planarRefs;
    SketchFrame datumFrame;
    bool datumValid = false;
    int firstBody = -1;
    int secondBody = -1;
    ForgeCad::ForgeCurve curve;  // funzioni curva (Helix): la curva esatta
    ForgeCad::ForgeBody forgeBody;
    QString error;
    BodyDisplay display;
    // forgeBody, display ed error vengono dallo snapshot salvato: alla prima
    // apertura si usano invece di ricalcolare e ritassellare il corpo.
    bool cachedGeometry = false;
};

// Corpo logico mostrato nella storyboard. Le feature conservano i risultati
// intermedi, mentre nella scena viene mostrato soltanto `tipFeatureId`.
struct ModelBody {
    quint64 id = 0;
    QString name;
    bool visible = true;
    quint64 tipFeatureId = 0;
    // Colore delle facce nel viewport e nei formati di scambio che lo
    // supportano. Non valido = azzurro predefinito (solido o superficie).
    QColor meshColor;
};

// Orientamento degli assi del modello sullo schermo: le direzioni del modello
// che nella vista frontale puntano a destra, in alto e verso l'osservatore
// (terna destrorsa ortonormale). Di default Z in alto e Y verso il fondo.
struct AxesOrientation {
    double right[3] = {1.0, 0.0, 0.0};
    double up[3] = {0.0, 0.0, 1.0};
    double toward[3] = {0.0, -1.0, 0.0};
};

// Unita' lineare mostrata dall'interfaccia. La geometria e tutte le
// definizioni parametriche restano sempre in millimetri: cambiare questa
// preferenza non modifica ne' rigenera il modello.
enum class LengthUnit { Millimeter = 0, Centimeter = 1, Meter = 2, Inch = 3, Foot = 4 };

// Stato del documento soggetto a Undo/Redo (l'orientamento degli assi si
// salva con il documento ma non torna indietro con l'Undo).
struct DocumentState {
    QVector<SketchObject> sketches;
    QVector<ExtrusionObject> extrusions;
    QVector<ModelBody> modelBodies;
    AxesOrientation orientation;
    bool orientationSet = false;  // letto dal file (altrimenti quello predefinito delle opzioni)
    LengthUnit lengthUnit = LengthUnit::Millimeter;
    bool lengthUnitSet = false;
};

// Sfondo della scena. Con la sfumatura attiva i due colori sono distribuiti
// lungo la direzione `angle` (gradi, 0 = da sinistra a destra, 90 = dal basso
// verso l'alto); `position` (0..1) e' il punto in cui i colori si mescolano al 50%.
// Con `affectsLighting` i colori dello sfondo illuminano anche gli oggetti.
struct BackgroundSettings {
    bool gradient = true;
    QColor startColor = QColor(2, 4, 6);
    QColor endColor = QColor(9, 17, 27);
    float angle = 90.0f;
    float position = 0.5f;
    bool affectsLighting = true;
    float lightingStrength = 0.6f;
};

#endif
