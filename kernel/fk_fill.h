#ifndef FORGECAD_FK_FILL_H
#define FORGECAD_FK_FILL_H

#include <memory>
#include <string>
#include <vector>

#include "fk_sweep.h"
#include "fk_topology.h"

// Superficie di riempimento: una lamina di una faccia delimitata da un
// contorno chiuso di curve 3D, che passa per curve guida interne e, lungo i
// tratti del contorno che stanno sul bordo di facce di altri corpi, continua
// quelle facce in tangenza (G1) o in curvatura (G2).
//
// Parametri: (u, v) = proiezione ortogonale sul piano medio del contorno
// (normale di Newell); il contorno proiettato deve essere un poligono semplice
// e le guide devono proiettarsi dentro. La superficie e' una B-spline
// bicubica su una griglia uniforme del rettangolo che contiene il contorno,
// la soluzione ai minimi quadrati di:
//   - posizione sul contorno e sulle guide (campioni nei loro (u, v)),
//   - G1: derivata trasversale S_d = lambda D (D la continuazione della faccia
//     adiacente, normale al bordo nel suo piano tangente; lambda =
//     `influence` mm per mm del piano dei parametri),
//   - G2: S_dd . n = kappa lambda^2 (kappa la curvatura normale della faccia
//     adiacente in D; la parte tangente di S_dd presa dalla soluzione
//     precedente, qualche giro),
// piu' l'energia di flessione (lastra sottile) per le zone senza dati. Vicino
// ai tratti in tangenza le guide pesano meno (una fascia di 0.15 volte il
// contorno, nulla nel primo terzo): la superficie passa con gradualita' dalla
// tangenza della faccia alla guida, anche quando le due non sono compatibili. La
// griglia si infittisce finche' contorno e guide stanno entro `tolerance`
// (o fino a `maxSpans` celle per lato). Gli edge sono le curve date (esatte):
// lo scarto residuo del contorno diventa la tolleranza degli edge.
//
// Condizioni incompatibili (una guida che arriva sul bordo con un angolo
// diverso dalla tangenza chiesta) non hanno soluzione esatta: il risultato e'
// il compromesso dei minimi quadrati e il rapporto riporta gli scarti.
namespace ForgeCad::Kernel {

struct FillContact {
    std::shared_ptr<const Body> body;
    FaceId face;
};

struct FillOptions {
    int continuity = 1;          // 0 contatto, 1 tangenza, 2 curvatura (sui tratti con una faccia adiacente)
    double influence = 1.0;      // lunghezza della tangente trasversale (1: velocita' unitaria)
    double guideWeight = 1.0;    // peso delle guide rispetto al contorno (0 = ignorate)
    double tolerance = 1e-6;     // scarto voluto sul contorno e sulle guide
    int maxSpans = 96;           // celle per lato al massimo
};

struct FillReport {
    double boundaryDeviation = 0.0;  // scarto massimo dal contorno (mm)
    double guideDeviation = 0.0;     // scarto massimo dalle guide (mm), fuori dalle fasce vicine ai tratti in tangenza
    double guideBlendDeviation = 0.0;  // scarto delle guide nelle fasce (vi pesano meno, fino a zero sul bordo)
    double tangentAngle = 0.0;       // scarto massimo delle normali sui tratti in tangenza (radianti)
    double curvatureDeviation = 0.0; // scarto massimo della curvatura normale trasversale (1/mm), G2
    int contactPieces = 0;           // tratti del contorno con una faccia adiacente
    int spans = 0;                   // celle per lato della griglia finale
    std::vector<std::string> notes;
};

// std::domain_error: contorno aperto, contorno o guide che si sovrappongono
// nella proiezione, guide fuori dal contorno, risultato non valido.
Body fillSurface(const std::vector<PathSegment> &boundary, const std::vector<PathSegment> &guides,
                 const std::vector<FillContact> &contacts, const FillOptions &options = {}, FillReport *report = nullptr);

}

#endif
