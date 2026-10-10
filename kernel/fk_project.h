#ifndef FORGECAD_FK_PROJECT_H
#define FORGECAD_FK_PROJECT_H

#include <memory>
#include <string>
#include <vector>

#include "fk_bspline.h"
#include "fk_profile.h"
#include "fk_topology.h"

// Proiezione di curve su un body lungo una direzione: ogni punto C(t) va nel
// primo punto della semiretta C(t) + s d (s >= 0) che sta su una faccia (la
// prima faccia incontrata, come le "curve proiettate" e le "linee di
// divisione" degli altri CAD). Sulla faccia il punto e' esatto (Newton sulla
// superficie); la curva risultante e' una cubica di Hermite C1 nello stesso
// parametro della curva di partenza, entro la tolleranza.
namespace ForgeCad::Kernel {

struct ProjectionOptions {
    double tolerance = 1e-8;      // scarto massimo delle curve proiettate dalla proiezione vera
    double edgeTolerance = 1e-5;  // un tratto entro questa distanza da un edge del body lo segue
    // Un tratto con gli estremi sul bordo della faccia (entro edgeTolerance) e
    // tutto entro questa distanza dal bordo lo segue anche se non coincide: la
    // corda di un bordo curvo corto copiata come segmento. Tagliarlo
    // lascerebbe solo una scheggia piu' stretta di questa distanza.
    double followTolerance = 1e-3;
    int threads = 0;              // 0 = i core della macchina
};

// Tratto della proiezione su una faccia.
//  - Le transizioni tra facce cadono sugli edge (Newton sull'edge): i tratti
//    vicini hanno lo stesso estremo, che sta sull'edge.
//  - `onEdge`: tutto il tratto coincide (entro edgeTolerance, o entro
//    followTolerance con gli estremi sul bordo) con edge gia' esistenti del
//    body (per esempio un bordo copiato in uno schizzo): non divide nulla;
//    `edgeGap` e' lo scarto massimo misurato. `partialEdge`: solo in parte (non gestito dal taglio).
//  - Estremi sul bordo della faccia (loOnEdge/hiOnEdge): il punto dell'edge.
struct ProjectedPiece {
    FaceId face;
    std::shared_ptr<const BSplineCurve<3>> curve;
    Interval range;
    double deviation = 0.0;
    bool onEdge = false, partialEdge = false;
    double edgeGap = 0.0;
    bool loOnEdge = false, hiOnEdge = false;
    Vec3 loPoint, hiPoint;
};

// `direction` non nulla (si normalizza). I tratti in cui la semiretta non
// incontra il body mancano. Un salto tra facce non adiacenti (una faccia ne
// copre un'altra) lascia un estremo libero sulla faccia coperta.
std::vector<ProjectedPiece> projectCurve(const Body &body, const Curve<3> &curve, const Interval &range, const Vec3 &direction,
                                         const ProjectionOptions &options = {});

// Piu' curve insieme (in parallelo): i tratti di ciascuna, nello stesso ordine.
struct CurveSpan {
    CurvePtr<3> curve;
    Interval range;
};
std::vector<std::vector<ProjectedPiece>> projectCurves(const Body &body, const std::vector<CurveSpan> &curves, const Vec3 &direction,
                                                       const ProjectionOptions &options = {});

// Tratti consecutivi (estremi entro 1e-6) uniti in curve: una B-spline cubica
// per ogni catena, con nodi C0 nei passaggi tra i tratti. `closed` dice se la
// catena si chiude.
struct ProjectedChain {
    std::shared_ptr<const BSplineCurve<3>> curve;
    bool closed = false;
};
std::vector<ProjectedChain> joinProjectedPieces(const std::vector<ProjectedPiece> &pieces, double tolerance = 1e-6);

// Taglio di un body con un profilo piano proiettato (regioni e catene aperte
// di un profilo nel piano XY di `frame`) lungo `direction`:
//  - RemoveInside: si tolgono le parti delle facce colpite per prime che
//    stanno dentro le regioni (fori o fessure nella superficie);
//  - KeepInside: restano solo quelle;
//  - SplitOnly: le facce si dividono lungo le curve e restano tutte (linea
//    di divisione).
// I tratti del profilo che seguono edge gia' esistenti non creano tagli: la
// regione e' delimitata da quegli edge. Il risultato di RemoveInside e di
// KeepInside e' una lamina. std::domain_error se la proiezione non incontra il
// body, se un tratto segue un edge solo in parte o se non si toglie nulla.
enum class ProjectedCutMode { RemoveInside = 0, KeepInside = 1, SplitOnly = 2 };

struct ProjectedCutReport {
    int cuts = 0;           // tratti impressi
    int followedEdges = 0;  // tratti che seguono edge esistenti
    double followedGap = 0.0;  // il loro scarto massimo dagli edge
    int removed = 0;        // pezzi di faccia tolti
    double deviation = 0.0;
    std::vector<std::string> notes;
};

Body projectedProfileCut(const Body &body, const Frame3 &frame, const std::vector<ProfileRegion> &regions,
                         const std::vector<ProfileLoop> &chains, const Vec3 &direction, ProjectedCutMode mode,
                         ProjectedCutReport *report = nullptr, const ProjectionOptions &options = {});

}

#endif
