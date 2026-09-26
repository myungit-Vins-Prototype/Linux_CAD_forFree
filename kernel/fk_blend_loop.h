#ifndef FORGECAD_FK_BLEND_LOOP_H
#define FORGECAD_FK_BLEND_LOOP_H

#include <vector>

#include "fk_topology.h"

// Raccordi e smussi dei bordi di forma libera (spline, NURBS, ellissi, e i
// segmenti e gli archi che li continuano) tra una faccia piana T e i suoi
// fianchi paralleli alla normale di T (i bordi superiori e inferiori dei
// solidi estrusi, anche i fori e le tasche).
//
// La palla di raggio r rotola tangente a T e al fianco: il suo centro sta a
// distanza r da entrambi, e la sezione del raccordo nel piano normale al
// bordo e' un quarto di cerchio esatto, con il punto di controllo di mezzo nel
// bordo stesso (NURBS quadratica razionale). La superficie e' il prodotto
// tensoriale di quell'arco con tre curve lungo il bordo: la sua traslata di r
// lungo il fianco, il bordo e la sua parallela a distanza r in T (una curva
// parallela non e' una B-spline: le tre curve sono cubiche di Hermite a
// tratti che coincidono con quelle vere entro 1e-9, come le curve del
// marching; OCCT approssima allo stesso modo i suoi raccordi). Lo smusso e'
// la rigata tra le due curve a distanza d.
//
// Niente booleane: la faccia T si accorcia sulla parallela, il fianco sulla
// traslata, e tra loro entra la faccia del raccordo. I bordi scelti formano
// catene nei loop di T:
//  - nei vertici lisci (tangenti continue) le facce dei raccordi si
//    incontrano sull'arco della sezione, e lo spigolo verticale tra due
//    fianchi (se c'e') si accorcia;
//  - negli angoli vivi convessi (il fianco fa uno spigolo verticale) i due
//    raccordi si tagliano lungo la loro curva d'intersezione (a mitra);
//  - agli estremi di una catena aperta la faccia che segue deve essere un
//    piano normale al bordo: vi resta l'arco della sezione.
// Un loop di un solo edge chiuso si divide prima in due.
//
// Non gestiti (std::domain_error): fianchi non paralleli alla normale di T,
// angoli vivi concavi, estremi contro facce non normali, raggi maggiori del
// raggio di curvatura del bordo (la parallela avrebbe una cuspide) o che
// escono dalle facce.
namespace ForgeCad::Kernel {

// L'edge e' un bordo di una faccia piana il cui altro lato e' un fianco
// parallelo alla normale del piano (e quindi si raccorda con blendPlanarChains).
bool isPlanarChainEdge(const Body &body, EdgeId edge);

// Edge di `selected` nei tratti consecutivi dei loop delle facce piane che
// contengono un edge di `seeds` (i bordi di forma libera e quello che li continua).
std::vector<EdgeId> planarChainRuns(const Body &body, const std::vector<EdgeId> &selected, const std::vector<EdgeId> &seeds);

// Raccordo (o smusso) delle catene formate dagli edge scelti nei loop delle
// facce piane: `edges` sono tutti gli edge da raccordare, ognuno deve
// soddisfare isPlanarChainEdge.
Body blendPlanarChains(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer);

}

#endif
