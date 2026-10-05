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
// `sameState`: i riferimenti sono stati presi su questa base (anche rigenerata):
// gli ID valgono piu' dei punti. Altrimenti (riordino) si cercano per geometria.
ForgeBody forgeBlend(const ForgeBody &base, const QVector<EdgePoint> &points, double size, bool chamfer, QString *error, const ChamferSpec &spec = {},
                     bool sameState = true);
// Falso se il risultato conserva la topologia e area/volume della base entro
// la tolleranza numerica: la booleana del raccordo e' stata un no-op.
bool forgeBlendHasEffect(const ForgeBody &base, const ForgeBody &result);

struct ThreadFaceInfo {
    double diameter = 0.0;
    double length = 0.0;
    double taper = 0.0;  // angolo del raggio rispetto all'asse, radianti
    bool internal = false;
    bool conical = false;
};
// Dati nominali della faccia scelta e filettatura modellata mediante profilo
// elicoidale: aggiunto all'esterno di un albero, sottratto all'interno di un foro.
bool forgeThreadFaceInfo(const Kernel::Body &body, const EdgePoint &face, ThreadFaceInfo &info, QString *error);
ForgeBody forgeThread(const ForgeBody &base, const ThreadParameters &parameters, QString *error);

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
// Offset di superficie: lamina a distanza `distance` (lungo la normale
// uscente; negativa verso l'interno) dalle facce `faces` di `base` (vuoto:
// tutte). Le facce tangenti tra loro restano cucite in una superficie, lungo
// gli spigoli vivi le superfici si separano: `summary` lo dice.
// Il corpo senza le facce scelte: lamina con le facce restanti (fk_sew facesAsSheet).
// Guscio: il solido svuotato con lo spessore verso l'interno, le facce scelte tolte (fk_shell).
ForgeBody forgeShell(const ForgeBody &base, const QVector<EdgePoint> &openFaces, double thickness, QString *error);
// `component` -1 conserva tutte le componenti nello stesso B-rep (file
// precedenti); un valore >= 0 restituisce una sola componente connessa.
ForgeBody forgeDeleteFaces(const ForgeBody &base, const QVector<EdgePoint> &faces, QString *error, int component = -1);
QVector<ForgeBody> forgeDeleteFacesSeparated(const ForgeBody &base, const QVector<EdgePoint> &faces, QString *error);
ForgeBody forgeOffsetFaces(const ForgeBody &base, const QVector<EdgePoint> &faces, double distance, QString *error, QString *summary = nullptr);
// Cucitura delle superfici `sheets` (anche solidi: valgono le loro facce) in
// una sola entro `tolerance`; con `solid` e il risultato chiuso, un solido.
// `summary` dice se e' chiusa e quanti bordi restano liberi.
ForgeBody forgeSew(const QVector<ForgeBody> &sheets, double tolerance, bool solid, QString *error, QString *summary = nullptr);

// Base dell'elica da uno spigolo circolare (source 1) o da una faccia
// cilindrica o conica (source 2) del body, vicino a `point` (come
// helixBaseFromShape di cad_features).
bool forgeHelixBase(const Kernel::Body &body, int source, const EdgePoint &point, HelixBase &base, QString *error);
// Sweep del profilo dello schizzo lungo il percorso (fk_sweep; mode 0 torsione
// minima, 1 Frenet, 2 orientamento costante): solido dai contorni chiusi,
// lamina dalle catene aperte. Con `surface` sempre una lamina senza coperchi
// (fk_sweep sweepSheet: un contorno chiuso da' un tubo aperto).
ForgeBody forgeSweep(const SketchObject &profile, const std::vector<Kernel::PathSegment> &path, int mode, QString *error, bool surface = false);
// Loft per le sezioni (fk_loft): un contorno chiuso per schizzo (solido) o
// una catena aperta (lamina). Le guide sono catene 3D che attraversano tutte
// le sezioni; continuita' e influenze sono i parametri persistenti G0/G1/G2.
// Con `surface` le sezioni chiuse danno il tubo senza coperchi (loftSheet).
ForgeBody forgeLoft(const QVector<SketchObject> &sections, const QVector<SketchObject> &guides, bool ruled,
                    int startContinuity, int endContinuity, int guideContinuity, double guideInfluence, double startInfluence,
                    double endInfluence, QString *error, bool surface = false);
// Superficie rigata tra due catene di curve 3D (fk_loft ruledSurface): le
// catene sono entrambe aperte o entrambe chiuse; verso e partenza della
// seconda si accordano alla prima.
ForgeBody forgeRuledSurface(const std::vector<Kernel::PathSegment> &first, const std::vector<Kernel::PathSegment> &second, QString *error);
// Superficie planare dei contorni chiusi dello schizzo (come la base di
// un'estrusione, normale lungo quella dello schizzo; fk_planar planarSheet).
ForgeBody forgePlanarSketch(const SketchObject &sketch, QString *error);
// Superficie planare delimitata da tratti 3D complanari: i tratti si
// raggruppano in contorni chiusi per estremi comuni, poi planarSheet.
// Superficie tra curve (patch di Coons, fk_boundary) del contorno chiuso dei tratti.
ForgeBody forgeBoundarySurface(const std::vector<Kernel::PathSegment> &segments, QString *error);
ForgeBody forgePlanarCurves(const std::vector<Kernel::PathSegment> &segments, QString *error);
inline ForgeBody forgeLoft(const QVector<SketchObject> &sections, bool ruled, QString *error) {
    return forgeLoft(sections, {}, ruled, 0, 0, 1, 1.0, 1.0, 1.0, error);
}

// Corpo importato: ExtrusionObject::importData e' il testo STEP (fk_step) del
// corpo scritto all'importazione, con la stessa geometria del file letto.
ForgeBody forgeImported(const QByteArray &data, QString *error);

// Approssimazione per la visualizzazione (quality 0/1/2), come tessellate().
void forgeTessellate(const Kernel::Body &body, int quality, BodyDisplay &display);
void forgeSurfaceConstructionCurves(const Kernel::Body &body, BodyDisplay &display, int divisions = 4,
                                    bool allFaces = false, const QVector<int> &faceFilter = {});
// Visualizzazione locale del raccordo: solo le superfici nuove rispetto alla
// base, con i loro bordi e le isoparametriche. Il B-rep completo resta separato.
void forgeBlendPreviewDisplay(const Kernel::Body &base, const Kernel::Body &result, int quality,
                              BodyDisplay &display, int divisions = 4);
// Visualizzazione locale di un'estrusione fusa o sottratta: solo le facce del
// risultato che non appartenevano ai corpi modificati. Questi possono cosi'
// restare opachi sotto la patch dell'anteprima.
void forgeExtrusionPreviewDisplay(const QVector<ForgeBody> &bases, const Kernel::Body &result, int quality,
                                  BodyDisplay &display, int divisions = 4, BodyDisplay *retainedDisplay = nullptr);

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

// Facce di `result` create dalla funzione `feature` (stessa regola di
// forgeFaceOwner, con la catena fino a `feature` compresa). Per ogni faccia si
// usa il punto della sua etichetta (`faceIds`/`labelPoints` di BodyDisplay)
// riportato esattamente sulla superficie. Serve solo a evidenziare a video.
QVector<int> forgeFeatureFaces(const Kernel::Body &result, const QVector<int> &faceIds, const QVector<QVector3D> &labelPoints,
                               int feature, const std::vector<std::pair<int, ForgeBody>> &chain);

}

#endif
