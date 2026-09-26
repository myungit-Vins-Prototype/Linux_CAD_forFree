#ifndef FORGECAD_FK_BLEND_H
#define FORGECAD_FK_BLEND_H

#include <vector>

#include "fk_topology.h"

// Raccordi (fillet) e smussi (chamfer) sugli spigoli di un solido.
//
// Ogni spigolo si tratta nella sua sezione: il piano normale a uno spigolo
// rettilineo, o il semipiano per l'asse di uno spigolo circolare. Li' le due
// facce sono rette o cerchi (piano; cilindro con l'asse parallelo allo
// spigolo; per gli spigoli circolari piano perpendicolare all'asse, cilindro,
// cono, sfera e toro coassiali), il raccordo e' il cerchio tangente a
// entrambe (intersezione delle curve spostate del raggio) e lo smusso la
// corda tra i punti a distanza `size` dallo spigolo. La zona tra lo spigolo e
// il raccordo (o lo smusso), estrusa lungo lo spigolo o fatta ruotare
// attorno all'asse, si toglie dal solido se lo spigolo e' convesso o si
// aggiunge se e' concavo (booleane esatte): le facce nuove stanno su
// cilindri, tori, piani e coni esatti.
//
// Non gestiti (std::domain_error): altri tipi di facce, spigoli rettilinei
// le cui facce vicine agli estremi non sono piani perpendicolari (lo spigolo
// finisce contro una faccia obliqua), spigoli circolari aperti, tre o piu'
// spigoli scelti in uno stesso vertice (la pezza sferica d'angolo), raggi
// che non stanno nelle facce.
namespace ForgeCad::Kernel {

// `size`: raggio del raccordo o distanza dello smusso dallo spigolo su
// ciascuna faccia.
Body blendEdges(const Body &body, const std::vector<EdgeId> &edges, double size, bool chamfer);

// Edge del body piu' vicino al punto (non valido se nessuno dista meno di `tolerance`).
EdgeId nearestEdge(const Body &body, const Vec3 &point, double tolerance);

}

#endif
