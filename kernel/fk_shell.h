#ifndef FORGECAD_FK_SHELL_H
#define FORGECAD_FK_SHELL_H

#include <vector>

#include "fk_topology.h"

// Guscio (svuotamento) di un solido con lo spessore verso l'interno: il
// materiale che resta e' quello a distanza al piu' `thickness` dalle facce
// tenute; le facce `removed` si tolgono e lasciano l'apertura.
//
// Costruzione esatta con le booleane:
//   guscio = solido ∩ pelle,  pelle = ∪ lastre ∪ tubi,
// con la lastra di ogni faccia tenuta il volume tra la faccia e la sua
// superficie a distanza verso l'interno (facce piane: estrusione della
// regione della faccia, esatta; facce curve: superficie a distanza di
// fk_offset, rigate lungo i bordi e cucitura) e un tubo di raggio `thickness`
// attorno a ogni spigolo concavo tra due facce tenute (lungo gli spigoli
// concavi la superficie interna e' un raccordo di raggio pari allo spessore,
// come l'offset esatto; lungo quelli convessi le lastre si sovrappongono e
// l'interno ha lo spigolo vivo). Senza facce tolte il risultato ha una cavita'
// chiusa (shell di vuoto).
//
// std::domain_error: corpo non solido, spessore non positivo, tutte le facce
// tolte, spessore oltre il raggio di curvatura di una faccia, booleane non
// riuscite.
namespace ForgeCad::Kernel {

Body shellBody(const Body &solid, const std::vector<FaceId> &removed, double thickness);

}

#endif
