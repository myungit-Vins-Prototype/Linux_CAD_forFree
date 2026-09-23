#ifndef FORGECAD_FK_BOOLEAN_H
#define FORGECAD_FK_BOOLEAN_H

#include "fk_topology.h"

// Booleane tra solidi B-rep esatti (unione, intersezione, differenza).
//
// Procedimento (lo stesso schema di Parasolid e di BOPAlgo di OCCT):
//  1. intersezione faccia-faccia: curve esatte con un piano (fk_intersect),
//     tracciate tra due superfici non piane (fk_marching, B-spline con le
//     loro SP-curve entro 1e-9), limitate alla parte che sta in entrambe le facce;
//  2. i punti estremi di questi archi dividono gli edge dei due body e gli
//     archi stessi;
//  3. ogni faccia viene divisa dagli archi che la attraversano: grafo degli
//     archi e dei pezzi di bordo, cicli con la faccia a sinistra (ordine
//     angolare attorno alla normale), cicli raggruppati in facce nello spazio
//     (u, v) (esterni e fori, fasce sulle superfici periodiche);
//  4. ogni pezzo di faccia e' dentro, fuori o sopra (facce complanari)
//     l'altro solido: dal verso della faccia che lo taglia o con un raggio;
//  5. si tengono i pezzi richiesti dall'operazione (girando quelli di B
//     nella differenza) e si cuciono edge e vertici coincidenti.
// Il risultato passa da checkBody; se non e' valido si lancia
// std::domain_error invece di restituire un body sbagliato.
//
// Coppie di facce gestite: piano con piano (anche complanari), cilindro, sfera
// o superficie estrusa; cilindri e superfici estruse tra loro (tutti i
// fianchi dei solidi estrusi, in qualsiasi direzione). Non gestiti
// (std::domain_error): le altre coppie di superfici non piane, superfici non
// piane coincidenti, edge che giacciono su una superficie non piana
// dell'altro solido, contatti tangenti.
namespace ForgeCad::Kernel {

// Stessa numerazione dell'app: 0 unione, 1 intersezione, 2 differenza.
enum class BooleanOperation { Unite = 0, Intersect = 1, Subtract = 2 };

struct BooleanOptions {
    double tolerance = 1e-6;  // distanza sotto la quale due punti sono lo stesso vertice
};

Body booleanOperation(const Body &a, const Body &b, BooleanOperation operation, const BooleanOptions &options = {});

}

#endif
