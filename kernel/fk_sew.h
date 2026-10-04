#ifndef FORGECAD_FK_SEW_H
#define FORGECAD_FK_SEW_H

#include <string>
#include <vector>

#include "fk_topology.h"

// Cucitura di superfici (lamine) in una sola lamina e, se il risultato e'
// chiuso, in un solido.
namespace ForgeCad::Kernel {

struct SewResult {
    Body body;
    bool closed = false;   // ogni edge in due facce
    bool solid = false;    // body e' un solido (chiuso e chiesto)
    int freeEdges = 0;     // bordi rimasti liberi
    int shells = 0;
    std::vector<std::string> notes;
};

// Le facce di `bodies` (lamine o solidi: valgono tutte le facce) in un body
// solo: vertici uniti entro `tolerance`, edge con gli stessi estremi e la
// stessa geometria (punto medio entro 10 tolleranze) condivisi; un bordo su
// cui finisce un vertice di un'altra superficie (giunzione a T) si divide li'.
// Se si chiede un solido e il primo tentativo resta aperto, una lamina formata
// da una sola faccia cilindrica acquisisce i loop di trim dei bordi liberi che
// giacciono sul suo supporto e dentro il suo intervallo assiale.
// I versi delle facce diventano coerenti attraverso gli edge comuni. Se ogni
// edge ha due facce e `makeSolid`, il risultato e' un solido con la normale
// uscente (se il volume viene negativo le facce si girano); altrimenti una
// lamina. Gli edge che si toccano solo entro la tolleranza diventano
// tolleranti. std::domain_error se il body non e' valido.
SewResult sewSheets(const std::vector<const Body *> &bodies, double tolerance, bool makeSolid);

// Le facce `faces` di `body` come lamina a parte (con gli edge comuni tra loro).
Body facesAsSheet(const Body &body, const std::vector<FaceId> &faces);

}

#endif
