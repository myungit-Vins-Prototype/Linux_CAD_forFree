#ifndef FORGECAD_FK_SWEEP_H
#define FORGECAD_FK_SWEEP_H

#include <vector>

#include "fk_profile.h"
#include "fk_history.h"
#include "fk_topology.h"

// Sweep: un profilo piano trascinato lungo un percorso (estrusione su un
// percorso, come BRepOffsetAPI_MakePipe di OCCT).
//
// Il percorso e' una catena di curve 3D con la tangente continua (G1):
// segmenti, archi, spline di uno schizzo, un'elica esatta (HelixCurve). Il
// profilo resta dove e' disegnato e si muove rigidamente con un sistema
// mobile (T tangente, N, B) del percorso:
//   X(v) = P(v) + F(v) F(0)^T (Q - P(0)),  F = [T N B],
// con P(0) l'inizio del percorso. Il sistema:
//  - MinimalTwist: a torsione minima (rotation minimizing frame; come il
//    CorrectedFrenet di OCCT): sui percorsi piani N = b x T con b la normale
//    del piano (esatto), altrimenti trasporto di Bishop integrato con
//    controllo adattivo dell'errore, definito anche su rettilinei e flessi; sui
//    percorsi chiusi lo scarto di rotazione alla fine si distribuisce lungo il
//    percorso;
//  - Frenet: T, N normale principale, B binormale (servono curvatura
//    positiva ovunque e nessun flesso: eliche, archi, spline senza tratti
//    rettilinei);
//  - Fixed: il profilo trasla senza ruotare.
//
// Superfici esatte dove il moto e' semplice: sui tratti rettilinei il
// profilo trasla (piani, cilindri, superfici estruse); sugli archi con
// MinimalTwist o Frenet ruota attorno all'asse dell'arco (piani, cilindri,
// coni, sfere, tori). Se il ponte conservativo al carrier analitico non e'
// disponibile, si usa direttamente la parametrizzazione NURBS del moto.
// Sugli altri tratti ogni
// curva del profilo, come NURBS esatta con poli Q_i e pesi w_i, genera la
// superficie con righe X_i(v) (pesi costanti lungo v) approssimate da
// quintiche di Hermite C2 entro 1e-9 (scala del percorso), come i raccordi e
// OCCT; gli spigoli longitudinali sono le righe dei vertici (isoparametriche
// esatte), quelli trasversali le curve del profilo spostate (esatte, dello
// stesso tipo). Facce di testa piane. Un loop di una sola curva chiusa si
// divide in due tratti (facce senza cucitura).
//
// Prima di pubblicare il B-rep vengono controllate anche le superfici laterali,
// le teste con i loro fori e il nesting delle regioni. La verifica globale usa
// limiti conservativi sui carrier razionali effettivi e, per i carrier analitici,
// sui coefficienti di una parametrizzazione equivalente. Le cuciture dichiarate
// ammettono soltanto contatti confinati entro linearResolution(). Un budget
// esaurito o una separazione non dimostrata restituiscono std::domain_error;
// non sono interpretati come prova di validita'. Il calcolo e' cancellabile.
//
// std::domain_error: percorso non continuo o con angoli vivi, profilo
// parallelo al percorso all'inizio, raggio di curvatura del percorso minore
// della distanza del profilo (le sezioni si incrocerebbero), Frenet su tratti
// rettilinei o flessi, risultato non valido o controllo globale non concluso.
namespace ForgeCad::Kernel {

enum class SweepOrientation { MinimalTwist = 0, Frenet = 1, Fixed = 2 };

struct PathSegment {
    CurvePtr<3> curve;
    Interval range;  // percorso da range.lo a range.hi
};

// Solido: le regioni (nel piano XY di `profileFrame`) lungo il percorso.
Body sweepRegions(const Frame3 &profileFrame, const std::vector<ProfileRegion> &regions, const std::vector<PathSegment> &path,
                  SweepOrientation orientation = SweepOrientation::MinimalTwist, FaceAliases *labels = nullptr);

// Lamina: catene aperte (profilo che non si chiude) lungo il percorso.
Body sweepChains(const Frame3 &profileFrame, const std::vector<ProfileLoop> &chains, const std::vector<PathSegment> &path,
                 SweepOrientation orientation = SweepOrientation::MinimalTwist, FaceAliases *labels = nullptr);

// Lamina generale: loop chiusi e catene aperte, anche insieme, lungo il
// percorso; mai coperchi. Un loop e' chiuso se la fine dell'ultimo tratto
// torna all'inizio del primo entro 1e-6 della sua misura (almeno 1e-6):
// diventa un tubo aperto alle estremita', orientato come il contorno dei
// solidi (antiorario attorno alla normale di `profileFrame`, cosi' le normali
// delle facce escono dal tubo); un loop di una sola curva chiusa si divide in
// due tratti come in sweepRegions. Le catene aperte sono trattate come in
// sweepChains (se il profilo ha solo catene il risultato coincide con
// sweepChains). Ogni loop o catena da' una shell. Su un percorso chiuso un
// loop chiuso da' una shell chiusa senza bordo (un toro come lamina: region
// non solida, isSheet() vero); per il solido racchiuso usare sweepRegions.
// std::domain_error come gli altri sweep; inoltre con loop chiusi il percorso
// deve uscire dal piano del profilo e il loop deve avere area non nulla.
Body sweepSheet(const Frame3 &profileFrame, const std::vector<ProfileLoop> &loops, const std::vector<PathSegment> &path,
                SweepOrientation orientation = SweepOrientation::MinimalTwist, FaceAliases *labels = nullptr);

}

#endif
