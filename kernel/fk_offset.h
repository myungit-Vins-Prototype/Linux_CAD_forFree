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
    Body body;                       // lamina (o piu' lamine nello stesso body, una per gruppo di facce tangenti)
    int shells = 0;                  // gruppi di facce unite
    int sharpEdges = 0;              // spigoli vivi tra le facce scelte: li' le superfici restano separate
    std::vector<std::string> notes;  // correzioni (edge tolleranti)
};

// Lamina a distanza `distance` dalle facce `faces` di `body`, lungo la loro
// normale uscente (negativa: verso l'interno). Le facce che si toccano lungo un
// edge tangente (normali parallele, entro 1e-3 radianti) restano unite lungo
// l'edge a distanza, cucite in una sola superficie; lungo gli spigoli vivi le
// superfici a distanza si staccano (una si allontana dall'altra o la
// attraversa) e il risultato ha piu' shell (sharpEdges le conta).
// std::domain_error se una faccia degenera.
OffsetResult offsetFaces(const Body &body, const std::vector<FaceId> &faces, double distance, double tolerance = 1e-7);

}

#endif
