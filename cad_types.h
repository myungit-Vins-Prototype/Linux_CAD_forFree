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
enum class DrawingTool { Line, Polyline, Spline, Nurbs, Circle, Arc, Polygon, ConstructionLine, Trim, Extend, Split, Fillet, Chamfer, Select };
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

struct SketchObject {
    QString name;
    int plane = 0;
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

// Stato del documento soggetto a Undo/Redo.
struct DocumentState {
    QVector<SketchObject> sketches;
    QVector<ExtrusionObject> extrusions;
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
