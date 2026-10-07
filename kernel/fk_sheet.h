#ifndef FORGECAD_FK_SHEET_H
#define FORGECAD_FK_SHEET_H

#include <vector>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_topology.h"

// Operazioni sulle lamine (superfici aperte, Body::isSheet): taglio con uno
// strumento ed estensione oltre i bordi.
namespace ForgeCad::Kernel {

// Taglio: la lamina si divide lungo le curve in cui la attraversa `tool`
// (un'altra lamina o un solido, vedi splitSheet) e resta la regione piu'
// vicina a `keep`. std::domain_error se lo strumento non la divide.
Body trimSheet(const Body &sheet, const Body &tool, const Vec3 &keep, double tolerance = 1e-6);

// Lamina piana quadrata di lato 2 `halfSize` nel piano XY di `frame`, centrata
// nell'origine (strumento di taglio per i piani di riferimento), con la
// normale del piano.
Body makePlaneSheet(const Frame3 &frame, double halfSize);

// Estensione: ogni edge di bordo scelto si sposta di `distance` verso
// l'esterno lungo la superficie della sua faccia, che cresce di una striscia.
//  - `linear` falso ("stessa superficie"): la striscia sta sulla superficie
//    della faccia prolungata (piani, cilindri, coni, B-spline, superfici estruse: oltre
//    la fine della curva base il suo polinomio prosegue, esatto) e alla fine
//    si fonde con la faccia;
//  - `linear` vero: la striscia e' la rigata tangente alla superficie
//    lungo l'edge (un piano: bordi a u costante delle superfici estruse e
//    dei cilindri), in continuita' G1; lungo le rette della superficie e'
//    come la stessa superficie.
// La distanza si misura lungo la superficie; sulle B-spline lungo la
// trasversale isoparametrica al punto medio del bordo.
// Sui piani sono ammessi anche bordi rifilati curvi: la nuova frontiera e' la
// loro parallela complanare. Sulle altre superfici gli edge devono essere
// isoparametrici nella loro faccia (i bordi delle lamine estruse: in alto, in
// basso e agli estremi del profilo), con velocita' costante lungo la direzione
// dell'estensione. Estendendo edge consecutivi di facce vicine le strisce si
// uniscono lungo lo spigolo comune. Per le pezze B-spline adiacenti la
// giunzione viene ricalcolata sulle superfici prolungate: se gli estremi
// sono diversi, il bordo comune termina sul piu' vicino e prosegue libero.
Body extendSheet(const Body &sheet, const std::vector<EdgeId> &edges, double distance, bool linear = false);

// B-spline uguale alla curva sul suo dominio e prolungata (stesso polinomio
// del primo e dell'ultimo tratto, anche razionale) fino a [lo, hi].
BSplineCurve<3> extendBSpline(const BSplineCurve<3> &curve, double lo, double hi);

// Prolungamento tensoriale, conserva esattamente la pezza originale.
BSplineSurface extendBSplineSurface(const BSplineSurface &surface, const Interval &u, const Interval &v);

}

#endif
