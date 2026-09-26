#ifndef FORGECAD_FORGE_H
#define FORGECAD_FORGE_H

#include <QString>
#include <QVector3D>
#include <vector>

#include "cad_types.h"
#include "fk_math.h"
#include "fk_profile.h"

// Modellazione con il kernel proprio (kernel/, ForgeCad::Kernel): l'alternativa
// a cad_kernel (OpenCASCADE) che si sceglie dal menu Opzioni. Stessa
// geometria esatta dello schizzo (le curve di cad_curve_solver), stesso
// collegamento degli estremi entro kSketchConnectionTolerance e stesse regole
// di annidamento dei contorni.
namespace ForgeCad {

// Tratti esatti dello schizzo nel suo piano (nel nuovo kernel).
std::vector<Kernel::ProfileSegment> forgeSketchSegments(const SketchObject &sketch);

// Sistema del piano di schizzo e altezza con segno lungo la sua normale.
void forgeSketchFrame(const SketchObject &sketch, double distance, Kernel::Frame3 &frame, double &height);

// Estrusione dei contorni chiusi dello schizzo (piu' regioni: un solo body
// con piu' solidi) o, se non ce ne sono, delle catene aperte (una lamina,
// Body::isSheet). nullptr e messaggio in `error` se non riesce.
ForgeBody forgeExtrusion(const SketchObject &sketch, double distance, QString *error);

// Rivoluzione dei contorni chiusi dello schizzo attorno al suo asse `axis`
// (ExtrusionObject::revolveAxis) di `angleDegrees` gradi, come buildRevolution.
ForgeBody forgeRevolution(const SketchObject &sketch, int axis, double angleDegrees, QString *error);

// Solido elementare (parallelepipedo, cilindro, sfera, cono, toro), come buildPrimitive.
ForgeBody forgePrimitive(const PrimitiveParameters &parameters, QString *error);

// Raccordo o smusso degli spigoli di `base` piu' vicini ai punti (fk_blend).
ForgeBody forgeBlend(const ForgeBody &base, const QVector<EdgePoint> &points, double size, bool chamfer, QString *error);

// Booleana esatta (con la fusione delle facce sulla stessa superficie).
ForgeBody forgeBoolean(const ForgeBody &first, const ForgeBody &second, BooleanOperation operation, QString *error);

// Approssimazione per la visualizzazione (quality 0/1/2), come tessellate().
void forgeTessellate(const Kernel::Body &body, int quality, BodyDisplay &display);

// Faccia del body colpita per prima dal raggio (come pickFace di cad_kernel).
bool forgePickFace(const Kernel::Body &body, const QVector3D &origin, const QVector3D &direction, FaceHit &hit);

// Distanza lungo il raggio del primo punto del body colpito (geometria esatta).
bool forgeIntersectRay(const Kernel::Body &body, const QVector3D &origin, const QVector3D &direction, double &distance);

}

#endif
