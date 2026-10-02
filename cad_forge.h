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
// `start`: la base parte dal piano dello schizzo spostato di `start` nel verso
// di extrusionVector (estrusioni simmetriche o nei due versi).
ForgeBody forgeExtrusion(const SketchObject &sketch, double distance, QString *error, double start = 0.0);

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
// Loft per le sezioni (fk_loft): un contorno chiuso per schizzo (solido) o
// una catena aperta (lamina). Le guide sono catene 3D che attraversano tutte
// le sezioni; continuita' e influenze sono i parametri persistenti G0/G1/G2.
ForgeBody forgeLoft(const QVector<SketchObject> &sections, const QVector<SketchObject> &guides, bool ruled,
                    int startContinuity, int endContinuity, int guideContinuity, double guideInfluence, double startInfluence,
                    double endInfluence, QString *error);
inline ForgeBody forgeLoft(const QVector<SketchObject> &sections, bool ruled, QString *error) {
    return forgeLoft(sections, {}, ruled, 0, 0, 1, 1.0, 1.0, 1.0, error);
}

// Corpo importato: ExtrusionObject::importData e' il testo STEP (fk_step) del
// corpo scritto all'importazione, con la stessa geometria del file letto.
ForgeBody forgeImported(const QByteArray &data, QString *error);

// Approssimazione per la visualizzazione (quality 0/1/2), come tessellate().
void forgeTessellate(const Kernel::Body &body, int quality, BodyDisplay &display);
void forgeSurfaceConstructionCurves(const Kernel::Body &body, BodyDisplay &display, int divisions = 4,
                                    bool allCurvedFaces = false, const QVector<int> &faceFilter = {});
// Visualizzazione locale del raccordo: solo le superfici nuove rispetto alla
// base, con i loro bordi e le isoparametriche. Il B-rep completo resta separato.
void forgeBlendPreviewDisplay(const Kernel::Body &base, const Kernel::Body &result, int quality,
                              BodyDisplay &display, int divisions = 4);

// Faccia del body colpita per prima dal raggio, con il suo piano (se e'
// piana) e un punto interno di ogni suo spigolo.
// `window`: vedi firstRayHit (la zona del punto colpito sulla tassellazione).
bool forgePickFace(const Kernel::Body &body, const QVector3D &origin, const QVector3D &direction, FaceHit &hit, const Kernel::RayFaceIndex *index = nullptr,
                   const Kernel::Interval *window = nullptr);

// Distanza lungo il raggio del primo punto del body colpito (geometria esatta).
bool forgeIntersectRay(const Kernel::Body &body, const QVector3D &origin, const QVector3D &direction, double &distance, const Kernel::RayFaceIndex *index = nullptr);

// Funzione che ha creato la faccia `face` di `picked` nel punto `point`:
// la prima di `chain` (le funzioni da cui il corpo deriva, in ordine, con
// i loro corpi) che ha una faccia sulla stessa superficie e che contiene il
// punto. Una faccia tagliata o accorciata da una funzione successiva resta
// della funzione che l'ha creata; un raccordo, un foro o una faccia spostata
// sono della funzione che li ha fatti. -1 se nessuna (non dovrebbe: il corpo
// stesso e' l'ultimo della catena).
int forgeFaceOwner(const Kernel::Body &picked, int face, const Kernel::Vec3 &point,
                   const std::vector<std::pair<int, ForgeBody>> &chain);

}

#endif
