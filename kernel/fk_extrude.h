#ifndef FORGECAD_FK_EXTRUDE_H
#define FORGECAD_FK_EXTRUDE_H

#include "fk_profile.h"
#include "fk_topology.h"

// Estrusione lineare di un profilo piano in un solido B-rep esatto.
namespace ForgeCad::Kernel {

// Solido ottenuto estrudendo la regione (nel piano XY di `frame`) di
// `height` lungo la Z del sistema (anche negativa). Topologia costruita con
// operatori di Eulero, geometria esatta:
//  - base e coperchio piani, spigoli = curve del profilo alle due quote;
//  - facce laterali: piani per i segmenti, cilindri per gli archi di
//    cerchio, superfici estruse per ellissi, spline e NURBS;
//  - spigoli verticali solo tra tratti consecutivi: un loop fatto di una
//    sola curva chiusa (cerchio, spline chiusa) da' una faccia laterale con
//    due loop, senza cucitura.
// Ogni fin riceve la sua SP-curve esatta (computePCurves).
// Se due tratti consecutivi si toccano solo entro una tolleranza > 1e-7, i
// vertici diventano "tolleranti" (tolleranza locale = distanza).
Body makeExtrusion(const Frame3 &frame, const ProfileRegion &region, double height);

}

#endif
