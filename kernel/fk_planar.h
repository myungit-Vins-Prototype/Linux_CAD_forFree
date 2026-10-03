#ifndef FORGECAD_FK_PLANAR_H
#define FORGECAD_FK_PLANAR_H

#include <vector>

#include "fk_profile.h"
#include "fk_sweep.h"
#include "fk_topology.h"

// Superfici planari: lamine piane delimitate da curve (il "riempimento" di un
// contorno chiuso). Geometria esatta: la faccia e' un Plane, gli edge sono le
// curve date (stesso tipo, stesso tratto), le SP-curve quelle esatte nel
// sistema del piano.
namespace ForgeCad::Kernel {

// Lamina piana delimitata da loop chiusi di curve 3D complanari.
//  - Ogni elemento di `loops` e' una catena chiusa di tratti in ordine e verso
//    qualsiasi: i tratti si concatenano per estremi entro `tolerance` (i
//    vertici prendono la tolleranza dello scarto, se supera 1e-7). Un tratto
//    solo chiuso (cerchio, spline chiusa) e' un loop valido.
//  - Piano: normale di Newell dei campioni dei loop (quella del loop piu'
//    grande, con il verso di quella del primo loop) e baricentro dei campioni.
//    Poi la verifica esatta per tipo: rette dagli estremi, cerchi ed ellissi
//    dal piano della conica (parallelo) e dal centro (sul piano), B-spline da
//    tutti i poli, curve limitate dalla base, le altre da campioni fitti.
//  - Annidamento come negli schizzi: un loop dentro un altro e' un foro, dentro
//    un foro di nuovo materiale (profondita' pari = materiale); ogni loop
//    esterno diventa una faccia con i fori che contiene direttamente. I loop
//    devono essere disgiunti.
//  - Le facce hanno la normale verso la normale di Newell del primo loop: il
//    loop esterno e' antiorario attorno a lei, i fori orari.
// std::domain_error se un loop non si chiude, se le curve non stanno in un
// piano o se un loop e' degenere (area nulla).
Body planarSheet(const std::vector<std::vector<PathSegment>> &loops, double tolerance = 1e-6);

// Lamina piana delle regioni di un profilo (piano XY di `frame`): una faccia
// per regione, come la base di un'estrusione ma con la normale lungo +Z del
// sistema. Contorni antiorari e fori orari (come da buildProfile); un loop
// con il verso sbagliato si gira.
Body planarSheet(const Frame3 &frame, const std::vector<ProfileRegion> &regions);

}

#endif
