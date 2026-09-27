#ifndef FORGECAD_FORGE_H
#define FORGECAD_FORGE_H

#include <QByteArray>
#include <QString>
#include <QVector3D>
#include <vector>

#include "cad_features.h"
#include "cad_types.h"
#include "fk_math.h"
#include "fk_profile.h"
#include "fk_sweep.h"

// Modellazione con il kernel proprio (kernel/, ForgeCad::Kernel): la geometria
// esatta dello schizzo (le curve di cad_curve_solver), il collegamento degli
// estremi entro kSketchConnectionTolerance e le regole di annidamento dei
// contorni di fk_profile.
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
// Smussi: `spec` (ChamferSpec) per due distanze o distanza e angolo.
ForgeBody forgeBlend(const ForgeBody &base, const QVector<EdgePoint> &points, double size, bool chamfer, QString *error, const ChamferSpec &spec = {});

// Booleana esatta (con la fusione delle facce sulla stessa superficie).
ForgeBody forgeBoolean(const ForgeBody &first, const ForgeBody &second, BooleanOperation operation, QString *error);

// Superfici (lamine). Taglio: `sheet` divisa dal corpo `tool` (lamina o
// solido) o, se `tool` e' nullo, dal piano di riferimento `plane`; resta la
// parte piu' vicina a `keep` (fk_sheet trimSheet).
ForgeBody forgeTrimSheet(const ForgeBody &sheet, const ForgeBody &tool, int plane, const EdgePoint &keep, QString *error);
// Le parti in cui lo strumento divide la superficie, con un punto di ciascuna
// (per scegliere quella da tenere) e la sua area.
QVector<SheetPiece> forgeSheetPieces(const ForgeBody &sheet, const ForgeBody &tool, int plane, QString *error);
// Scala uniforme di `factor` attorno all'origine (mode 0), al baricentro del
// solido (1) o a `point` (2), esatta (fk_transform scaleBody).
ForgeBody forgeScale(const ForgeBody &base, double factor, int mode, const EdgePoint &point, QString *error);
// Estensione dei bordi di `sheet` piu' vicini ai punti (fk_sheet extendSheet).
ForgeBody forgeExtendSheet(const ForgeBody &sheet, const QVector<EdgePoint> &points, double distance, bool linear, QString *error);

// Base dell'elica da uno spigolo circolare (source 1) o da una faccia
// cilindrica o conica (source 2) del body, vicino a `point` (come
// helixBaseFromShape di cad_features).
bool forgeHelixBase(const Kernel::Body &body, int source, const EdgePoint &point, HelixBase &base, QString *error);
// Sweep del profilo dello schizzo lungo il percorso (fk_sweep; mode 0 torsione
// minima, 1 Frenet, 2 orientamento costante): solido dai contorni chiusi,
// lamina dalle catene aperte.
ForgeBody forgeSweep(const SketchObject &profile, const std::vector<Kernel::PathSegment> &path, int mode, QString *error);
// Loft per le sezioni (fk_loft): un contorno chiuso per schizzo (solido) o una catena aperta (lamina).
ForgeBody forgeLoft(const QVector<SketchObject> &sections, bool ruled, QString *error);

// Corpo importato: ExtrusionObject::importData e' il testo STEP (fk_step) del
// corpo scritto all'importazione, con la stessa geometria del file letto.
ForgeBody forgeImported(const QByteArray &data, QString *error);

// Approssimazione per la visualizzazione (quality 0/1/2), come tessellate().
void forgeTessellate(const Kernel::Body &body, int quality, BodyDisplay &display);

// Faccia del body colpita per prima dal raggio, con il suo piano (se e'
// piana) e un punto interno di ogni suo spigolo.
bool forgePickFace(const Kernel::Body &body, const QVector3D &origin, const QVector3D &direction, FaceHit &hit);

// Distanza lungo il raggio del primo punto del body colpito (geometria esatta).
bool forgeIntersectRay(const Kernel::Body &body, const QVector3D &origin, const QVector3D &direction, double &distance);

}

#endif
