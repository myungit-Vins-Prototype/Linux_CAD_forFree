#ifndef FORGECAD_FK_BOUNDARY_H
#define FORGECAD_FK_BOUNDARY_H

#include <vector>

#include "fk_sweep.h"
#include "fk_topology.h"

// Superficie tra curve (patch di Coons bilineare): una lamina di una faccia
// delimitata da un contorno chiuso di curve 3D qualsiasi, anche sghembo.
//
// I tratti (in ordine e verso qualsiasi) si concatenano per estremi in un
// contorno chiuso; il contorno si divide in lati negli angoli vivi (tangenti
// che non continuano). Con 3 o 4 angoli i lati sono quelli; con meno di tre
// si aggiungono punti di divisione a meta' lunghezza dei lati piu' lunghi fino
// a quattro lati (un cerchio: quattro quarti). Piu' di quattro angoli: errore.
//
// Ogni lato, nel parametro s in [0, 1] proporzionale alla lunghezza dei suoi
// tratti, si approssima con una cubica di Hermite C1 entro `tolerance` (fitCurve
// di fk_offset); la superficie
//   S(u, v) = (1 - v) C0(u) + v C1(u) + (1 - u) D0(v) + u D1(v) - B(u, v)
// (B il bilineare degli angoli) e' una B-spline bicubica esatta di quelle
// curve: dopo aver reso compatibili i nodi delle coppie opposte, i poli sono
// P_ij = (1 - eta_j) C0_i + eta_j C1_i + (1 - xi_i) D0_j + xi_i D1_j - B(xi_i, eta_j)
// con xi, eta le ascisse di Greville (una funzione affine ha per poli i suoi
// valori nelle ascisse di Greville). Con tre lati il quarto e' un punto: la
// faccia ha un lato degenere (un polo nell'angolo).
//
// Gli edge della lamina sono le curve date (esatte); la superficie le
// interpola entro `tolerance` (edge tolleranti se serve, come negli STEP).
// std::domain_error: contorno aperto o ramificato, piu' di quattro angoli,
// lati di lunghezza nulla, risultato non valido.
namespace ForgeCad::Kernel {

Body boundarySheet(const std::vector<PathSegment> &pieces, double tolerance = 1e-9);

}

#endif
