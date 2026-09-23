#ifndef FORGECAD_FK_PRIMITIVES_H
#define FORGECAD_FK_PRIMITIVES_H

#include <vector>

#include "fk_topology.h"

// Solidi elementari: casi particolari di makeExtrusion (fk_extrude.h),
// costruiti con operatori di Eulero e dotati di geometria esatta.
namespace ForgeCad::Kernel {

// Prisma retto: poligono `outer` con eventuali fori `holes` (nel piano XY di
// `frame`, qualsiasi verso di percorrenza) estruso di `height` lungo Z
// (makeExtrusion con soli segmenti). Genere = numero di fori. Poligoni
// semplici, fori interni e disgiunti.
Body makePrism(const Frame3 &frame, const std::vector<Vec2> &outer, const std::vector<std::vector<Vec2>> &holes,
               double height);

// Parallelepipedo [0, dx] x [0, dy] x [0, dz] nel sistema `frame`.
Body makeBox(const Frame3 &frame, double dx, double dy, double dz);

// Cilindro di asse Z del sistema, base nell'origine. Alla Parasolid: due
// edge circolari chiusi, il fianco ha due loop e nessuna cucitura.
Body makeCylinder(const Frame3 &frame, double radius, double height);

// Geometria "poliedrica" per le entita' che ne sono prive: segmenti di retta
// per gli edge (range = lunghezza) e piani per le facce (normale di Newell
// dai loop, uscente se i loop sono orientati secondo la convenzione).
void assignPolyhedralGeometry(Body &body);

}

#endif
