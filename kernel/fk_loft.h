#ifndef FORGECAD_FK_LOFT_H
#define FORGECAD_FK_LOFT_H

#include <vector>

#include "fk_profile.h"
#include "fk_topology.h"

// Loft: superficie (o solido) che passa per una successione di sezioni piane
// (come BRepOffsetAPI_ThruSections di OCCT).
//
// Ogni sezione e' un loop chiuso (solido, con le facce di testa piane nella
// prima e nell'ultima sezione) o una catena aperta (lamina), nel piano XY del
// suo sistema. Corrispondenza tra le sezioni:
//  - verso: i loop girano tutti attorno alla direzione del loft (dal
//    baricentro di una sezione a quello della successiva), le catene hanno
//    l'inizio dalla stessa parte;
//  - punto di partenza di ogni loop: il vertice (o, su una curva chiusa sola,
//    il punto) piu' vicino alla partenza della sezione precedente, rispetto ai
//    baricentri;
//  - le sezioni si dividono agli stessi valori dell'ascissa curvilinea
//    normalizzata (l'unione di quelli dei vertici di tutte), cosi' hanno lo
//    stesso numero di tratti (un cerchio verso un quadrato: quattro archi).
// Ogni tratto diventa una NURBS esatta (tratti di Bezier in forma standard,
// grado comune, nodi comuni); la superficie tra i tratti corrispondenti ha
// righe (poli omogenei) che interpolano quelli delle sezioni:
//  - rigata: lineare tra sezioni consecutive (una faccia per campata);
//  - liscia: B-spline di grado min(3, sezioni - 1) che interpola tutte le
//    sezioni (parametri dalle distanze tra i baricentri, nodi per media).
// Le sezioni sono isoparametriche esatte della superficie; gli spigoli
// trasversali sono le curve delle sezioni (esatte, dello stesso tipo), quelli
// longitudinali le righe dei vertici. La geometria tra le sezioni e' una
// scelta di definizione (come in ogni CAD): quella rigata coincide con OCCT
// quando la corrispondenza e' la stessa (tronchi di cono e di piramide).
namespace ForgeCad::Kernel {

struct LoftSection {
    Frame3 frame;
    ProfileLoop loop;  // chiuso (solidi) o catena aperta (lamine)
};

Body loftSolid(const std::vector<LoftSection> &sections, bool ruled = false);
Body loftSheet(const std::vector<LoftSection> &sections, bool ruled = false);

}

#endif
