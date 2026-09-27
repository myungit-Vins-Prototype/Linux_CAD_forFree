#ifndef FORGECAD_FK_EXCHANGE_H
#define FORGECAD_FK_EXCHANGE_H

#include <string>
#include <vector>

#include "fk_topology.h"

// Interno ai formati di scambio (fk_step, fk_iges): il B-rep letto da un file
// (vertici, edge con la curva e gli estremi, facce con la superficie, il verso
// e i loop di fin) diventa un Body del kernel.
//  - Il tratto di ogni edge viene dalle proiezioni dei vertici sulla curva (i
//    file non lo danno); sulle curve periodiche va dal primo al secondo nel
//    verso della curva, un edge chiuso fa il giro intero.
//  - Le cuciture (edge usati due volte dalla stessa faccia) delle superfici
//    periodiche nel kernel si tolgono e i loop si dividono dove passavano;
//    sulle altre (B-spline chiuse) restano, con le due SP-curve sui due bordi
//    del dominio (quella giusta dal verso della fin).
//  - Gli edge che stanno sulle facce solo entro la precisione del file
//    diventano tolleranti per lo scarto misurato (come in Parasolid), i vertici
//    per la distanza dagli estremi delle curve.
//  - Poi computePCurves e checkBody (eccezione std::domain_error con il motivo).
namespace ForgeCad::Kernel::detail {

struct RawEdge {
    int start = -1, end = -1;  // la curva va da start a end nel verso del parametro
    CurvePtr<3> curve;
    bool hasRange = false;     // tratto noto (altrimenti dalle proiezioni dei vertici)
    Interval range;
};

struct RawFin {
    int edge = -1;
    bool sense = true;  // nel verso dell'edge
};

struct RawFace {
    SurfacePtr surface;
    bool sense = true;  // la normale della faccia e' quella della superficie
    std::vector<std::vector<RawFin>> loops;
};

struct RawModel {
    std::vector<Vec3> points;
    std::vector<RawEdge> edges;
    std::vector<RawFace> faces;
};

// Numeri reali dei file, indipendenti dal locale (un'applicazione Qt imposta
// quello dell'utente, e con la virgola decimale snprintf e strtod
// sbaglierebbero): 17 cifre significative con il punto decimale sempre
// presente ("1.", "2.5E-07"; il double si rilegge uguale) e lettura con un
// eventuale segno '+' iniziale. parseReal restituisce il numero di caratteri
// letti (0 se non c'e' un numero).
std::string formatReal(double x);
std::size_t parseReal(const char *begin, const char *end, double &value);

// Solido (solid) o lamina. `notes` riceve le correzioni fatte.
Body assembleBody(const RawModel &model, bool solid, std::vector<std::string> *notes = nullptr);

// Facce sciolte (IGES 144, superfici senza topologia): vertici uniti entro
// `tolerance`, edge con gli stessi estremi e la stessa geometria condivisi, versi
// delle facce resi coerenti attraverso gli edge comuni. Vero se il risultato e'
// chiuso (ogni edge in due facce).
bool sewModel(RawModel &model, double tolerance);

}

#endif
