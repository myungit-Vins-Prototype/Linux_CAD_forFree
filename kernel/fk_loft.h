#ifndef FORGECAD_FK_LOFT_H
#define FORGECAD_FK_LOFT_H

#include <vector>

#include "fk_profile.h"
#include "fk_sweep.h"
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

// Una guida e' una catena 3D che deve attraversare una volta ogni sezione.
// La prima guida fissa la cucitura/corrispondenza dei contorni chiusi; le
// altre sono validate come riferimenti trasversali e saranno usate dalle
// condizioni di forma del loft liscio.
struct LoftOptions {
    bool ruled = false;
    std::vector<std::vector<PathSegment>> guides;
    int startContinuity = 0;  // 0 G0, 1 G1, 2 G2
    int endContinuity = 0;
    int guideContinuity = 1;  // G0 solo attraversamento, G1 tangente, G2 anche curvatura
    double guideInfluence = 1.0;
    double startInfluence = 1.0;
    double endInfluence = 1.0;
};

// Solido: tutte le sezioni chiuse, facce di testa piane.
Body loftSolid(const std::vector<LoftSection> &sections, bool ruled = false);
Body loftSolid(const std::vector<LoftSection> &sections, const LoftOptions &options);

// Lamina (Body::buildSheet). Sezioni tutte aperte: superficie tra le catene.
// Sezioni tutte chiuse: tubo senza coperchi, con la stessa corrispondenza dei
// loop del solido (verso, partenza, guide, continuita' G0/G1/G2); le sue facce
// laterali coincidono con quelle di loftSolid sulle stesse sezioni. Sezioni
// miste (chiuse e aperte): std::domain_error.
Body loftSheet(const std::vector<LoftSection> &sections, bool ruled = false);
Body loftSheet(const std::vector<LoftSection> &sections, const LoftOptions &options);

// Superficie rigata tra due catene 3D qualsiasi (spigoli, curve di schizzi su
// piani diversi, eliche: le HelixCurve si usano come la loro B-spline entro
// 1e-9), non necessariamente piane. I tratti di ogni catena sono consecutivi
// ma in verso qualsiasi: si concatenano per estremi entro 1e-6 della scala.
// Le due catene sono entrambe aperte o entrambe chiuse (altrimenti
// std::domain_error). Corrispondenza come il loft rigato: stesso numero di
// tratti -> tratto con tratto, altrimenti divisione nell'unione delle frazioni
// di ascissa curvilinea dei vertici. Verso: catene aperte con l'inizio della
// seconda dalla parte dell'inizio della prima (minimo di |A0-B0| + |A1-B1|);
// catene chiuse con lo stesso verso di rotazione (normali di Newell) e la
// partenza della seconda nel punto piu' vicino alla partenza della prima.
// Superficie esatta: lineare in v tra le righe dei poli omogenei dei tratti
// (piano esatto dove le pezze sono piane). Lamina: spigoli = le due curve
// (stesso tipo) e le rette nei vertici corrispondenti.
Body ruledSurface(const std::vector<PathSegment> &first, const std::vector<PathSegment> &second);

}

#endif
