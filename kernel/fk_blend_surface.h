#ifndef FORGECAD_FK_BLEND_SURFACE_H
#define FORGECAD_FK_BLEND_SURFACE_H

#include <vector>

#include "fk_blend.h"
#include "fk_topology.h"

// Raccordi e smussi a palla rotolante tra due facce di tipo qualsiasi
// (piani, cilindri, coni, sfere, tori, estrusioni, rivoluzioni, B-spline),
// lungo spigoli di forma qualsiasi: le curve d'intersezione del marching
// (innesti tra cilindri, cilindri obliqui su una piastra, fori su superfici
// curve), ellissi, spline.
//
// Sezione: per ogni parametro t dello spigolo E il centro c della palla di
// raggio r sta nel piano normale a E in E(t) e alla distanza r da entrambe
// le facce, dalla parte dell'angolo tra le facce minore di 180 gradi:
//     S_A(uA, vA) + k N_A(uA, vA) = S_B(uB, vB) + k N_B(uB, vB),
//     (S_A + k N_A - E(t)) . T(t) = 0,
// con N le normali uscenti, k = -r sugli spigoli convessi (la palla sta nel
// materiale) e +r sui concavi. Newton sulle quattro incognite (derivate
// esatte delle normali dalle derivate seconde delle superfici); le derivate
// della soluzione rispetto a t per differenziazione implicita. La sezione del
// raccordo e' l'arco esatto dal punto di contatto su A a quello su B, come
// quadrica razionale: poli pA, c + (a + b) / (1 + cos q), pB con peso
// cos(q / 2) nel mezzo (q angolo dell'arco, a e b i raggi ai contatti).
// Lungo lo spigolo le tre righe (in coordinate omogenee) sono cubiche di
// Hermite a tratti con gli stessi nodi, raffinate finche' la superficie si
// scosta meno di 1e-9 dagli archi veri (come le curve del marching e i
// raccordi di OCCT). Smusso: i punti delle due facce alla distanza d
// (corda) dallo spigolo nel piano normale, e la rigata tra le due curve.
//
// Niente booleane: le facce A e B si accorciano sulle curve di contatto
// (isoparametriche v = 0 e v = 1 della superficie nuova), tra loro entra la
// faccia del raccordo. Gli spigoli formano catene tra le stesse due facce:
//  - catene chiuse (tutto un loop di A e di B), con vertici lisci in cui
//    arrivano solo i due spigoli della catena: i raccordi vi si incontrano
//    sull'arco della sezione; un edge chiuso da solo si divide in due;
//  - estremi di catene aperte contro una faccia piana normale allo spigolo
//    (il vertice ha tre spigoli): la faccia prende l'arco della sezione.
//
// Non gestiti (std::domain_error): angoli vivi nella catena, estremi
// contro facce non piane o non normali allo spigolo, raggi che fanno
// uscire i contatti dalle facce o che superano la curvatura (la curva dei
// centri tornerebbe indietro), facce che si ripiegano (arco vicino a 180 gradi).
// Le sezioni iniziali e il fitting delle pezze di una catena sono calcolati
// in parallelo; il montaggio topologico delle facce resta ordinato.
namespace ForgeCad::Kernel {

// Raccordo (o smusso di distanza `size`) delle catene formate dagli edge.
// `sides` (facoltativo, uno per edge): smussi asimmetrici, vedi chamferEdges.
Body blendSurfaceChains(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer, const std::vector<ChamferSides> *sides = nullptr);

// Edge di `selected` nelle catene (tratti lisci tra le stesse due facce) che
// contengono un edge di `seeds`.
std::vector<EdgeId> surfaceChainRuns(const Body &body, const std::vector<EdgeId> &selected, const std::vector<EdgeId> &seeds);

}

#endif
