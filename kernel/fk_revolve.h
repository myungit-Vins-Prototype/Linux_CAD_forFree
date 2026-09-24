#ifndef FORGECAD_FK_REVOLVE_H
#define FORGECAD_FK_REVOLVE_H

#include "fk_profile.h"
#include "fk_topology.h"

// Solido di rivoluzione (giro completo) di una regione piana.
//
// La regione sta nel semipiano x >= 0 del piano XZ di `frame`: il punto (x, y)
// del profilo e' frame.origin + x X + y Z e l'asse di rotazione e' Z. Ogni
// tratto del profilo genera una faccia sulla superficie esatta: piano (tratto
// perpendicolare all'asse), cilindro (parallelo), cono (obliquo), sfera
// (arco con il centro sull'asse), toro (arco con il centro fuori dall'asse),
// superficie di rivoluzione (altre curve). I tratti che giacciono sull'asse
// non generano facce. Ogni vertice fuori dall'asse diventa un edge circolare
// chiuso; le facce periodiche non hanno cucitura (due loop, o uno solo se un
// estremo del tratto sta sull'asse: la faccia ha un polo). Come in Parasolid.
namespace ForgeCad::Kernel {

Body makeRevolution(const Frame3 &frame, const ProfileRegion &region);

}

#endif
