#ifndef FORGECAD_FK_MARCHING_H
#define FORGECAD_FK_MARCHING_H

#include <vector>

#include "fk_intersect.h"

// Intersezione tra due superfici non piane ("marching", come Parasolid e
// IntWalk di OCCT).
//
// Il tracciamento e' generale: da un punto comune alle due superfici si
// avanza lungo t = n_A x n_B (predittore) e si torna sull'intersezione con
// Newton sulle quattro incognite (u, v, s, t): S_A(u, v) = S_B(s, t) piu' il
// piano normale alla direzione di avanzamento (correttore). Il passo si
// adatta finche' la cubica di Hermite tra due punti (in 3D e nelle SP-curve
// di entrambe le superfici) resta entro `fitTolerance` dall'intersezione vera
// in tre punti interni; i nodi delle superfici (curvatura discontinua) e i
// bordi dei domini non periodici diventano punti della curva. Il risultato
// e' una B-spline cubica C1 con le sue due SP-curve, con lo stesso parametro.
//
// I punti di partenza sono il problema difficile. Qui sono completi per i
// cilindri generalizzati (cilindri e superfici estruse, cioe' tutti i fianchi
// dei solidi estrusi): con S_A = C_A(u) + v D_A e S_B = C_B(s) + w D_B e
// n = D_A x D_B, un punto e' comune se e solo se n.C_A(u) = n.C_B(s), una
// curva di livello di una funzione separabile nel piano (u, s). Ogni sua
// componente chiusa ha un punto in cui n.C_A'(u) = 0 o n.C_B'(s) = 0: le
// generatrici di A e di B in quei parametri (e agli estremi dei domini non
// periodici), intersecate con l'altra superficie, danno un punto su ogni
// componente chiusa. Le componenti che attraversano il bordo di una faccia
// si trovano dai punti in cui gli edge incontrano l'altra superficie (vedi
// intersectCurveSurface), che il chiamante passa come `seeds`.
// Direzioni parallele: la sezione nel piano normale da' rette esatte.
//
// Le altre coppie di superfici (sfere, coni, tori, B-spline, rivoluzione)
// non sono ancora gestite: std::domain_error.
namespace ForgeCad::Kernel {

struct IntersectionCurve {
    CurvePtr<3> curve;
    Interval range;
    // SP-curve sulle due superfici, stesso parametro della curva; nullptr per
    // le curve esatte (rette), le cui SP-curve si calcolano con exactPCurve.
    CurvePtr<2> pcurves[2];
    double deviation = 0.0;  // scarto massimo misurato (curva e SP-curve), 0 per le curve esatte
    bool closed = false;     // curva chiusa (primo punto = ultimo)
};

struct SurfaceIntersection {
    std::vector<IntersectionCurve> curves;
    bool coincident = false;                       // le superfici hanno una parte in comune
    std::vector<Vec3> tangentPoints;               // punti di contatto tangente (tracciamento interrotto)
    std::vector<IntersectionCurve> tangentCurves;  // rette di tangenza (direzioni parallele)
};

struct SurfaceIntersectionOptions {
    double tolerance = 1e-6;       // due punti piu' vicini sono lo stesso punto
    double fitTolerance = 1e-9;  // scarto massimo della curva e delle SP-curve
};

// Componenti di A inter B dentro `bounds` che passano per un punto di `seeds`
// (punti comuni alle due superfici, entro la tolleranza) o che hanno un punto
// critico dentro `bounds` (in particolare tutte le curve chiuse contenute nel
// box). Le curve aperte finiscono appena fuori dal box o sul bordo di un
// dominio non periodico.
SurfaceIntersection intersectSurfaces(const Surface &a, const Surface &b, const Box &bounds, const std::vector<Vec3> &seeds,
                                      const SurfaceIntersectionOptions &options = {});

}

#endif
