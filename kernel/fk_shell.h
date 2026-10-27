#ifndef FORGECAD_FK_SHELL_H
#define FORGECAD_FK_SHELL_H

#include <vector>
#include "fk_topology.h"

// Svuotamento verso l'interno con giunzioni a spigolo vivo. La cavita' si
// costruisce dalle superfici offset, prolungate e rifilate alle intersezioni;
// le facce `removed` delimitano le aperture alla quota originale. L'esterno
// del solido resta quello di ingresso: non si aggiungono raccordi agli spigoli.
// I raccordi analitici convessi (cilindrici, sferici e toroidali) con raggio
// residuo nullo o negativo si eliminano dalla cavita', contraendo le fasce
// tra i supporti e le pezze d'angolo. I raccordi con raggio residuo positivo
// si conservano. Senza aperture il risultato ha una cavita' chiusa.
// Errori espliciti per supporti mancanti/non riconosciuti, cavita' invertita,
// superfici singolari o intersezioni che non convergono entro tolleranza.
namespace ForgeCad::Kernel {
class BodyHistory;

Body shellBody(const Body &solid, const std::vector<FaceId> &removed, double thickness, BodyHistory *history = nullptr);

}

#endif
