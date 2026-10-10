#ifndef FORGECAD_FK_OFFSET_H
#define FORGECAD_FK_OFFSET_H

#include <functional>
#include <string>
#include <vector>

#include "fk_bspline.h"
#include "fk_topology.h"

// Superfici a distanza costante (offset) e lamine a distanza dalle facce di un
// corpo.
//
// La superficie a distanza d da S e' O(u, v) = S(u, v) + d N(u, v), con
// N = Su x Sv unitaria, e ha la stessa parametrizzazione (u, v) di S:
//  - piani, cilindri, coni, sfere e tori restano dello stesso tipo (esatti:
//    piano spostato, raggio R + d, cono con il raggio R + d cos(a) e l'origine
//    spostata di -d sin(a) lungo l'asse, raggio minore r + d);
//  - estrusioni e rivoluzioni restano estrusioni (della curva base a distanza)
//    e rivoluzioni (del meridiano a distanza): la normale non dipende da v
//    (estrusione) e ruota con u (rivoluzione), quindi basta la curva;
//  - le B-spline diventano B-spline bicubiche C1 (Hermite a tratti con nodi
//    tripli, la stessa costruzione delle SP-curve) raffinate finche' lo scarto
//    da O nello stesso (u, v) e' sotto la tolleranza.
// Le curve a distanza (curva base, meridiano, bordi delle facce) sono esatte
// quando lo sono (rette, cerchi, curve traslate su un piano), altrimenti
// cubiche di Hermite C1 nello stesso parametro entro la tolleranza (come le
// parallele dei raccordi e le curve del marching).
namespace ForgeCad::Kernel {

// La superficie a distanza `distance` lungo Su x Sv, sulla finestra `u` x `v`
// (serve alle B-spline; le altre valgono su tutto il dominio).
// std::domain_error se degenera (raggio nullo o negativo, punti singolari,
// cuspidi: la distanza supera il raggio di curvatura).
SurfacePtr offsetSurface(const Surface &surface, double distance, const Interval &u, const Interval &v, double tolerance = 1e-7);

// B-spline cubica C1 (nodi interni tripli) che approssima f su `range` entro
// `tolerance`, con lo stesso parametro; `breaks` (dentro range) sono punti in
// cui f puo' avere un angolo (nodi delle curve di partenza). `deviation`
// riceve lo scarto misurato. std::domain_error se non si arriva alla tolleranza.
std::shared_ptr<BSplineCurve<3>> fitCurve(const std::function<Vec3(double)> &f, const Interval &range,
                                          const std::vector<double> &breaks, double tolerance, double *deviation = nullptr);

struct OffsetResult {
    Body body;                       // lamina (o piu' lamine nello stesso body)
    int shells = 0;                  // gruppi di facce unite
    int sharpEdges = 0;              // spigoli vivi tra le facce scelte: li' le superfici restano separate
    std::vector<std::string> notes;  // correzioni (edge tolleranti)
};

// Lamina a distanza `distance` dalle facce `faces` di `body`, lungo la loro
// normale uscente (negativa: verso l'interno). Le facce che si toccano lungo un
// edge tangente (entro 1e-3 radianti e scarto dell'offset entro tolleranza) restano unite lungo
// l'edge a distanza, cucite in una sola superficie; lungo gli spigoli vivi le
// superfici a distanza si staccano (una si allontana dall'altra o la
// attraversa) e il risultato ha piu' shell (sharpEdges le conta).
// Con preserveSeams=true si intersecano localmente i prolungamenti anche
// lungo gli spigoli vivi: bordi rifilati condivisi e vertici comuni a tutte
// le facce incidenti. Se non converge entro tolleranza, l'operazione fallisce
// esplicitamente (non restituisce una cucitura aperta o fuori tolleranza).
// Le facce B-spline rettangolari con discontinuita' interne vengono divise
// in pezze regolari e i nuovi bordi partecipano allo stesso trim/cuci.
// Il default false conserva il comportamento dei chiamanti del kernel;
// il comando interattivo abilita la cucitura per le nuove feature.
// Superfici, coppie adiacenti e curve dei bordi sono calcolate in parallelo su
// un numero di worker limitato ai core; la topologia finale viene assemblata in
// ordine deterministico dopo il completamento di ciascuna fase.
// std::domain_error se una faccia degenera.
// `joinAngle` > 0: gli edge con le normali delle due facce entro questo
// angolo (radianti) restano condivisi come quelli tangenti, con la normale
// media; l'edge a distanza diventa tollerante per lo scarto misurato (le pieghe
// piccole di uno sweep lungo una spline solo C1). 0: solo gli edge tangenti.
OffsetResult offsetFaces(const Body &body, const std::vector<FaceId> &faces, double distance, double tolerance = 1e-7, bool preserveSeams = false,
                         double joinAngle = 0.0);

}

#endif
