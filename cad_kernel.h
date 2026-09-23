#ifndef FORGECAD_KERNEL_H
#define FORGECAD_KERNEL_H

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include "cad_types.h"

// Modellazione solida esatta (B-rep OpenCASCADE, double, tolleranza 1e-7).
// La tassellazione esiste solo in tessellate() ed e' usata solo per disegnare.
namespace ForgeCad {

// Tolleranza con cui gli estremi di due entita' dello schizzo sono considerati
// coincidenti quando si formano i contorni.
constexpr double kSketchConnectionTolerance = 1.0e-6;

// Sistema di riferimento dei piani di schizzo: 0 = XY, 1 = XZ, 2 = YZ.
// Le coordinate (x, y) dello schizzo sono gli assi X e Y del sistema.
gp_Ax3 sketchAxes(int plane);
gp_Pnt sketchToWorld(const QPointF &point, int plane);
QVector3D sketchToDisplay(const QPointF &point, int plane);
QPointF worldToSketch(const gp_Pnt &point, int plane);
gp_Vec extrusionVector(int plane, double distance);

// Profilo dello schizzo: facce piane (con eventuali fori, anche multiple)
// formate dai contorni chiusi; se non ci sono contorni chiusi, i fili aperti.
// `closed` indica se il profilo e' fatto di facce.
bool buildSketchProfile(const SketchObject &sketch, TopoDS_Shape &profile, bool &closed, QString *error);

// Estrusione del profilo dello schizzo: solido se il profilo e' chiuso,
// altrimenti superficie.
TopoDS_Shape buildExtrusion(const SketchObject &sketch, double distance, bool &solid, QString *error);

// Operazione booleana esatta; le facce complanari del risultato vengono unificate.
TopoDS_Shape booleanOperation(const TopoDS_Shape &first, const TopoDS_Shape &second,
                              BooleanOperation operation, QString *error);

bool isSolidShape(const TopoDS_Shape &shape);

// Approssimazione per la visualizzazione (quality 0/1/2).
void tessellate(const TopoDS_Shape &shape, int quality, BodyDisplay &display);

// Distanza lungo il raggio del primo punto della forma esatta colpito.
bool intersectRay(const TopoDS_Shape &shape, const QVector3D &origin, const QVector3D &direction,
                  double &distance);

}

#endif
