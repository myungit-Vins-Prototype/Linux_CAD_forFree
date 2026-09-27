#ifndef FORGECAD_KERNEL_H
#define FORGECAD_KERNEL_H

#include "cad_types.h"
#include "fk_math.h"

// Sistemi di riferimento degli schizzi e regole comuni della modellazione
// (la geometria la costruisce cad_forge con il kernel proprio, kernel/).
namespace ForgeCad {

// Tolleranza con cui gli estremi di due entita' dello schizzo sono considerati
// coincidenti quando si formano i contorni.
constexpr double kSketchConnectionTolerance = 1.0e-6;

// Sistema di riferimento dei piani di schizzo: 0 = XY, 1 = XZ, 2 = YZ.
// Le coordinate (x, y) dello schizzo sono gli assi X e Y del sistema.
Kernel::Frame3 sketchAxes(int plane);
Kernel::Vec3 sketchToWorld(const QPointF &point, int plane);
QVector3D sketchToDisplay(const QPointF &point, int plane);
QPointF worldToSketch(const Kernel::Vec3 &point, int plane);
Kernel::Vec3 extrusionVector(int plane, double distance);
// Le stesse per uno schizzo: piani di riferimento o piano su una faccia
// (kFacePlane, SketchObject::frame; l'estrusione positiva va lungo la normale uscente).
Kernel::Frame3 sketchAxes(const SketchObject &sketch);
Kernel::Vec3 sketchToWorld(const QPointF &point, const SketchObject &sketch);
QVector3D sketchToDisplay(const QPointF &point, const SketchObject &sketch);
QPointF worldToSketch(const Kernel::Vec3 &point, const SketchObject &sketch);
Kernel::Vec3 extrusionVector(const SketchObject &sketch, double distance);
// Sistema dello schizzo sul piano per `point` con normale uscente `normal`
// (unitaria; vedi SketchFrame): l'asse Y dello schizzo e' `up` proiettato sul piano.
SketchFrame faceSketchFrame(const Kernel::Vec3 &point, const Kernel::Vec3 &normal, const Kernel::Vec3 &up = Kernel::Vec3(0, 0, 1));
// Sistema di uno schizzo nuovo sul piano di riferimento `plane` (0 XY, 1 XZ,
// 2 YZ) con l'orientamento degli assi: la normale verso l'osservatore della
// vista standard in cui il piano si vede di fronte, X a destra e Y in alto sullo schermo.
SketchFrame referenceSketchFrame(int plane, const AxesOrientation &orientation);

// Asse di rivoluzione `axis` di un corpo (ExtrusionObject::revolveAxis) nel
// piano dello schizzo: un punto e la direzione unitaria.
bool sketchRevolutionAxis(const SketchObject &sketch, int axis, QPointF &point, QPointF &direction, QString *error);

// Lato dell'asse su cui sta il profilo (entita' di costruzione escluse):
// +1 a sinistra della direzione, -1 a destra, 0 (con l'errore) se lo
// attraversa o giace sull'asse.
int revolutionProfileSide(const SketchObject &sketch, const QPointF &point, const QPointF &direction, QString *error);

// Sistema di una primitiva (origine e assi del piano di riferimento) e
// controllo delle sue dimensioni (messaggio vuoto se valide).
Kernel::Frame3 primitiveAxes(const PrimitiveParameters &parameters);
QString primitiveError(const PrimitiveParameters &parameters);

// Distanze dello smusso sulla faccia di riferimento e sull'altra, date le
// normali uscenti delle due facce (0 di riferimento se `firstIsReference`);
// falso se l'angolo non si puo' fare. Faccia di riferimento: normale piu'
// verso +Z, poi +X, poi +Y; con la distanza e l'angolo la seconda distanza
// e' d sin(theta) / sin(phi + theta), phi = pi - acos(n1 . n2).
bool chamferDistances(const ChamferSpec &spec, double size, const double normal0[3], const double normal1[3], bool &firstIsReference,
                      double &onReference, double &onOther, QString *error);

}

#endif
