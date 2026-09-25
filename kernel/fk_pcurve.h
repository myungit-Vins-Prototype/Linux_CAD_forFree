#ifndef FORGECAD_FK_PCURVE_H
#define FORGECAD_FK_PCURVE_H

#include "fk_precision.h"
#include "fk_topology.h"

// SP-curve (Parasolid) / pcurve (OCCT): la curva di un edge nello spazio dei
// parametri (u, v) di una faccia, con lo stesso parametro dell'edge:
// S(p(t)) = C(t) per t nel range dell'edge.
//
// La curva 3D dell'edge resta la geometria di riferimento. L'SP-curve e'
// esatta quando esiste una forma chiusa:
//  - su un piano: la curva espressa nel sistema del piano (rette, cerchi,
//    ellissi, B-spline e NURBS restano del loro tipo);
//  - altrove, quando (u(t), v(t)) e' affine in t: isoparametriche (cerchi
//    sui cilindri, coni, sfere, tori e rivoluzioni, generatrici, curve base
//    delle estrusioni), eliche sui cilindri. Si ipotizza la forma affine dai
//    parametri di alcuni punti e la si verifica in 3D.
// Negli altri casi si approssima con una B-spline cubica C1 (Hermite a tratti
// sui punti invertiti, raffinata finche' lo scarto 3D e' sotto la
// tolleranza): e' quello che fanno Parasolid e OCCT (ProjLib) per le curve
// d'intersezione.
namespace ForgeCad::Kernel {

// Scarto di default delle SP-curve approssimate.
constexpr double kPCurveTolerance = 1e-7;

// SP-curve esatta, o nullptr se la curva non sta sulla superficie entro
// `tolerance` o non ha una forma esatta riconosciuta. Sulle superfici
// periodiche u(range.lo) cade nel dominio di base.
CurvePtr<2> exactPCurve(const Surface &surface, const CurvePtr<3> &curve, const Interval &range,
                        double tolerance = kLinearResolution);

// SP-curve approssimata (B-spline cubica C1) con scarto 3D <= tolerance, o
// nullptr se non si riesce (curva fuori dalla superficie, passaggio per un
// polo). `deviation`: scarto massimo misurato.
CurvePtr<2> fitPCurve(const Surface &surface, const CurvePtr<3> &curve, const Interval &range,
                      double tolerance = kPCurveTolerance, double *deviation = nullptr);

// Scarto massimo |S(p(t)) - C(t)| su `samples` + 1 punti per tratto liscio.
double pcurveDeviation(const Surface &surface, const Curve<3> &curve, const Curve<2> &pcurve, const Interval &range,
                       int samples = 16);

// Assegna l'SP-curve alle fin che non l'hanno: esatta se possibile
// (pcurveTolerance = 0), altrimenti approssimata. Restituisce il numero di
// fin rimaste senza SP-curve.
int computePCurves(Body &body, double tolerance = kPCurveTolerance);

// Cammini di un loop lungo la linea di un polo della sua faccia (sfera,
// vertice del cono): dove una fin arriva nel polo con u = `from` e la
// successiva (indice `nextFin` in loopFins) riparte con u congruo a `to`, il
// contorno nello spazio (u, v) segue la linea v = quota del polo (vedi
// poleWalk in fk_surface_algo.h). Servono le SP-curve delle fin.
struct LoopPoleWalk {
    double v = 0.0, from = 0.0, to = 0.0;
    std::size_t nextFin = 0;
};
std::vector<LoopPoleWalk> loopPoleWalks(const Body &body, LoopId loop, double tolerance = 1e-6);

}

#endif
