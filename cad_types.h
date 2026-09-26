#ifndef FORGECAD_TYPES_H
#define FORGECAD_TYPES_H

#include <QColor>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <memory>

#include <TopoDS_Shape.hxx>

namespace ForgeCad::Kernel {
class Body;
}
namespace ForgeCad {
// B-rep del kernel proprio (kernel/), immutabile e condiviso tra le istantanee dell'Undo.
using ForgeBody = std::shared_ptr<const Kernel::Body>;
}

// Precisione: tutte le coordinate del modello sono in double (QPointF usa
// qreal = double). I float (QVector3D) servono solo per la visualizzazione.

// Gli ultimi cinque sono strumenti di modifica (non creano curve): taglia,
// estendi, spezza, raccordo e smusso tra segmenti.
// Select: nessuna creazione, il clic seleziona (e' lo strumento all'apertura di uno schizzo).
// Rectangle (due angoli) e CenterRectangle (centro e un angolo) creano quattro
// segmenti orizzontali e verticali collegati; Ellipse e' una curva. I valori
// sono salvati nei file: i nuovi strumenti vanno in fondo.
enum class DrawingTool { Line, Polyline, Spline, Nurbs, Circle, Arc, Polygon, ConstructionLine, Trim, Extend, Split, Fillet, Chamfer, Select,
                         Rectangle, CenterRectangle, Ellipse };
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
// `samples` e' solo un'approssimazione per disegnare e selezionare a schermo.
// Le entita' di costruzione (`construction`) non fanno parte dei profili:
// servono da riferimento (assi di rivoluzione, agganci).
struct CurveObject {
    DrawingTool tool = DrawingTool::Spline;
    QVector<QPointF> controlPoints;
    QVector<double> weights;
    QVector<QPair<QPointF, QPointF>> tangentHandles;
    int sides = 0;
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
    PointOnCurve, Fix, Distance, Angle, Radius, Diameter
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
    // Sistema esplicito del piano (plane == kFacePlane, o customFrame sui piani
    // di riferimento: gli schizzi nuovi prendono gli assi dello schermo della
    // vista normale al piano con l'orientamento degli assi del documento).
    SketchFrame frame;
    bool customFrame = false;
    QString faceSource;  // corpo da cui viene il piano (solo per l'albero)
    // Vincoli geometrici (oggetti): coincidenze, orizzontale/verticale,
    // parallelismo, quote... I vecchi dati (codici in `constraints`,
    // `segmentLengths`, `segmentAngles`, `coincidentConstraints`) si
    // convertono in questi all'apertura dei file vecchi e poi restano vuoti o
    // neutri (codici -1, lunghezze 0, angoli -1).
    QVector<SketchConstraint> geometricConstraints;

    bool isConstructionSegment(int index) const { return constructionSegments.contains(index); }
};

enum class BooleanOperation { Union = 0, Intersection = 1, Difference = 2 };

// Kernel geometrico con cui si costruiscono i corpi (menu Opzioni): OpenCASCADE
// o il kernel proprio di ForgeCAD (kernel/).
enum class GeometryKernel { OpenCascade = 0, Forge = 1 };

// Approssimazione della forma esatta usata SOLO per il disegno a schermo.
struct BodyDisplay {
    QVector<QVector3D> vertices;   // tre vertici per triangolo
    QVector<QVector3D> normals;    // una normale per vertice
    QVector<QVector<QVector3D>> edges;
    int quality = -1;
};

// Funzione che genera un corpo che non e' una booleana (operation = -1).
enum class BodyFeature { Extrusion = 0, Revolution = 1, Primitive = 2, Blend = 3 };

// Spigolo di un corpo identificato da un suo punto (coordinate del modello):
// dopo una rigenerazione si prende lo spigolo piu' vicino.
struct EdgePoint {
    double x = 0.0, y = 0.0, z = 0.0;
};

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

// Corpo della scena, definito in modo parametrico:
//  - estrusione (operation = -1, feature Extrusion): profili chiusi dello
//    schizzo `sketchIndex` estrusi di `distance` lungo la normale del piano;
//  - rivoluzione (feature Revolution): profili chiusi dello schizzo
//    `sketchIndex` ruotati di `revolveAngle` gradi (con segno: verso
//    destrorso attorno all'asse orientato; 360 = giro completo) attorno al
//    segmento `revolveAxis` dello schizzo (-1 = asse X, -2 = asse Y del piano);
//  - primitiva (feature Primitive): `primitive`;
//  - raccordo o smusso (feature Blend): gli spigoli `blendEdges` del corpo
//    `firstBody` raccordati con raggio `blendSize` (o smussati a distanza
//    `blendSize` se `blendChamfer`);
//  - booleana: `operation` tra i corpi `firstBody` e `secondBody`.
// Il B-rep esatto rigenerato dalla definizione e' `shape` (OpenCASCADE) o
// `forgeBody` (kernel proprio), secondo `kernel`; l'altro resta vuoto.
struct ExtrusionObject {
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
    PrimitiveParameters primitive;
    bool blendChamfer = false;
    double blendSize = 1.0;
    QVector<EdgePoint> blendEdges;
    int firstBody = -1;
    int secondBody = -1;
    TopoDS_Shape shape;
    ForgeCad::ForgeBody forgeBody;
    GeometryKernel kernel = GeometryKernel::OpenCascade;
    QString error;
    BodyDisplay display;
};

// Orientamento degli assi del modello sullo schermo: le direzioni del modello
// che nella vista frontale puntano a destra, in alto e verso l'osservatore
// (terna destrorsa ortonormale). Di default Z in alto e Y verso il fondo.
struct AxesOrientation {
    double right[3] = {1.0, 0.0, 0.0};
    double up[3] = {0.0, 0.0, 1.0};
    double toward[3] = {0.0, -1.0, 0.0};
};

// Stato del documento soggetto a Undo/Redo (l'orientamento degli assi si
// salva con il documento ma non torna indietro con l'Undo).
struct DocumentState {
    QVector<SketchObject> sketches;
    QVector<ExtrusionObject> extrusions;
    AxesOrientation orientation;
    bool orientationSet = false;  // letto dal file (altrimenti quello predefinito delle opzioni)
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
