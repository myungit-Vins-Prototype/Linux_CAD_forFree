#ifndef FORGECAD_FK_BOOLEAN_H
#define FORGECAD_FK_BOOLEAN_H

#include <functional>

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
//     nella differenza) e si cuciono edge e vertici coincidenti; dove i due
//     solidi si toccano soltanto (lungo uno spigolo, in un punto) edge e
//     vertici restano distinti, cosi' il risultato e' sempre una varieta';
//  6. facce ed edge sulla stessa geometria si fondono (fk_unify.h).
// Il risultato passa da checkBody; se non e' valido si lancia
// std::domain_error invece di restituire un body sbagliato.
//
// Coppie di facce gestite: piano con piano (anche complanari), cilindro, sfera
// o superficie estrusa; cilindri e superfici estruse tra loro (tutti i
// fianchi dei solidi estrusi, in qualsiasi direzione). Non gestiti
// (std::domain_error): le altre coppie di superfici non piane, superfici non
// piane coincidenti, edge che giacciono su una superficie non piana
// dell'altro solido. I contatti tangenti (rette e punti di tangenza, rami
// che si incrociano nei punti di tangenza) sono gestiti; restano esclusi i
// contatti di ordine superiore (rami tangenti tra loro).
namespace ForgeCad::Kernel {

// Stessa numerazione dell'app: 0 unione, 1 intersezione, 2 differenza.
enum class BooleanOperation { Unite = 0, Intersect = 1, Subtract = 2 };

struct BooleanOptions {
    double tolerance = 1e-6;  // distanza sotto la quale due punti sono lo stesso vertice
    // Fonde facce ed edge sulla stessa geometria (fk_unify.h), come fa l'app
    // con ShapeUpgrade_UnifySameDomain dopo le booleane di OCCT.
    bool unifySameDomain = true;
    // Thread per le intersezioni tra coppie di facce e per la successiva
    // divisione/classificazione delle singole facce: 0 = i core della
    // macchina, 1 = in sequenza. Il risultato non dipende dal numero.
    int threads = 0;
};

Body booleanOperation(const Body &a, const Body &b, BooleanOperation operation, const BooleanOptions &options = {});

// Divide la lamina lungo le curve in cui la attraversa `tool` (una lamina o
// un solido: le sue facce fanno da lame, anche oltre la lamina) e ne
// restituisce le regioni separate dai tagli, ognuna come lamina (i tagli
// diventano edge di bordo). Le facce della lamina si dividono come nelle
// booleane; i pezzi si raggruppano attraverso gli edge che non sono tagli.
// Se lo strumento non la attraversa del tutto (un taglio che finisce dentro
// una faccia) la regione resta una sola.
std::vector<Body> splitSheet(const Body &sheet, const Body &tool, const BooleanOptions &options = {});

// Curva da imprimere su una faccia (imprintCurves): il tratto `range` di
// `curve`, che giace sulla faccia `face` entro `tolerance`. Un estremo su un
// edge del body (loOnEdge/hiOnEdge) diventa il punto dell'edge
// loPoint/hiPoint, che divide l'edge (anche se la curva vi arriva solo entro
// uno scarto piccolo: il vertice diventa tollerante).
struct ImprintCurve {
    FaceId face;
    CurvePtr<3> curve;
    Interval range;
    double tolerance = 0.0;
    bool loOnEdge = false, hiOnEdge = false;
    Vec3 loPoint, hiPoint;
};

// Divide le facce del body lungo le curve (come le facce delle booleane lungo
// le curve d'intersezione, ma con curve date) e toglie i pezzi di faccia per
// cui `remove(faccia di partenza, punto interno)` e' vero. Se toglie qualcosa
// il risultato e' una lamina. `removed` riceve il numero dei pezzi tolti.
// Le facce sulla stessa superficie si fondono solo con options.unifySameDomain
// (spento, i pezzi divisi restano facce distinte).
Body imprintCurves(const Body &body, const std::vector<ImprintCurve> &curves, const std::function<bool(FaceId, const Vec3 &)> &remove = {},
                   const BooleanOptions &options = {}, int *removed = nullptr);

}

#endif
