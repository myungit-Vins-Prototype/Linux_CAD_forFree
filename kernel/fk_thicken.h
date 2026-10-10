#ifndef FORGECAD_FK_THICKEN_H
#define FORGECAD_FK_THICKEN_H

#include <vector>

#include "fk_topology.h"

// Spessore di una superficie (lamina): il solido tra la superficie e una sua
// copia a distanza, chiuso dalle pareti lungo i bordi liberi.
namespace ForgeCad::Kernel {

// Da che parte va lo spessore rispetto al verso di riferimento (la normale
// delle facce, o la direzione data): tutto da quella parte, tutto dalla parte
// opposta, o meta' per parte.
enum class ThickenSide { Forward = 0, Backward = 1, Both = 2 };

struct ThickenOptions {
    double thickness = 1.0;  // > 0
    ThickenSide side = ThickenSide::Forward;
    // Lungo la normale (useDirection falso): superficie a distanza (fk_offset)
    // e pareti rigate lungo la normale ai bordi. Le facce devono essere unite
    // da spigoli tangenti; lo spessore non puo' superare il raggio di
    // curvatura dalla parte in cui va.
    // Lungo una direzione (useDirection vero): copia traslata della superficie
    // e pareti estruse lungo la direzione (esatte: piani per i bordi rettilinei,
    // superfici estruse per gli altri). Ogni retta parallela alla direzione
    // deve incontrare la superficie una volta sola: la normale non deve mai
    // essere perpendicolare alla direzione ne' cambiare verso rispetto a lei.
    // Lo spessore si misura lungo la direzione.
    // Scarto ammesso delle superfici a distanza (lungo la normale). Le facce
    // con salti di normale ai nodi (raccordi G1 solo numericamente) si
    // dividono in pezze dove il salto per lo spessore supera 10 volte questo
    // valore: con 1e-7 sono centinaia di pezze (minuti). 0: si provano in
    // ordine 5e-5 e 2e-5 per mm di spessore (almeno 1), poi 1e-5, 1e-6 e
    // 1e-7 mm, e vale la prima che riesce.
    double offsetTolerance = 0.0;
    bool useDirection = false;
    Vec3 direction{0.0, 0.0, 1.0};
    double tolerance = 1e-6;
};

// `faces`: le facce del body da ispessire (vuoto: tutte); possono essere anche
// facce di un solido. Il risultato e' un solido. std::domain_error con il
// motivo se non si puo' costruire.
Body thickenSheet(const Body &body, const std::vector<FaceId> &faces, const ThickenOptions &options);

}

#endif
