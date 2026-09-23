#ifndef FORGECAD_TYPES_H
#define FORGECAD_TYPES_H

#include <QColor>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <TopoDS_Shape.hxx>

// Precisione: tutte le coordinate del modello sono in double (QPointF usa
// qreal = double). I float (QVector3D) servono solo per la visualizzazione.

enum class DrawingTool { Line, Polyline, Spline, Nurbs, Circle, Arc, Polygon };
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
struct CurveObject {
    DrawingTool tool = DrawingTool::Spline;
    QVector<QPointF> controlPoints;
    QVector<double> weights;
    QVector<QPair<QPointF, QPointF>> tangentHandles;
    int sides = 0;
    QVector<QPointF> samples;
    bool numericallyValid = false;
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
};

enum class BooleanOperation { Union = 0, Intersection = 1, Difference = 2 };

// Approssimazione della forma esatta usata SOLO per il disegno a schermo.
struct BodyDisplay {
    QVector<QVector3D> vertices;   // tre vertici per triangolo
    QVector<QVector3D> normals;    // una normale per vertice
    QVector<QVector<QVector3D>> edges;
    int quality = -1;
};

// Corpo della scena, definito in modo parametrico:
//  - estrusione (operation = -1): profili chiusi dello schizzo `sketchIndex`
//    estrusi di `distance` lungo la normale del piano;
//  - booleana: `operation` tra i corpi `firstBody` e `secondBody`.
// `shape` e' il B-rep esatto (OpenCASCADE) rigenerato dalla definizione.
struct ExtrusionObject {
    QString name;
    int sketchIndex = -1;
    int plane = 0;
    double distance = 1.0;
    bool solid = false;
    bool visible = true;
    int operation = -1;
    int firstBody = -1;
    int secondBody = -1;
    TopoDS_Shape shape;
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
