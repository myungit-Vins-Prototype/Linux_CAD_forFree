#ifndef FORGECAD_FK_FILL_H
#define FORGECAD_FK_FILL_H

#include <memory>
#include <string>
#include <vector>

#include "fk_sweep.h"
#include "fk_topology.h"
#include "fk_history.h"

// Superficie originale ForgeCAD, delimitata da curve 3D esatte e vincoli
// sulle facce adiacenti. Il contorno proiettato sul piano medio di Newell
// deve essere semplice; le guide devono proiettarsi al suo interno.
//
// Raccordo a pendenza finita: S(u,v) = O + u X + v Y + h(u,v) N, con h
// B-spline bicubica. Le ascisse di Greville fissano la proiezione all'identita'
// sull'intero rettangolo, anche fra i campioni: l'influenza non puo' ripiegare
// questa mappatura. G1 impone h_d = -(d.n)/(N.n); G2 impone
// h_dd = II(S_d,S_d)/(N.n). La seconda forma fondamentale e' valutata sul
// supporto nella direzione fisica e con la velocita' della derivata richiesta.
//
// Forma: energia ||D2 h||^2 + L^2 ||D3 h||^2 (proxy della flessione e della
// variazione di curvatura nel piano medio), L = 0.15 * larghezza. Le influenze
// dei supporti agiscono localmente: suggerimento di curvatura e fascia interna
// di continuazione del getto, limitata alla sua scala geometrica. Con guide
// la fascia ha priorita' minore; senza supporti influence regola L.
// In G1 la curvatura trasversale del supporto e' un suggerimento morbido,
// attenuato vicino ai vertici, distinto dalla continuita' G2. Aggiornamenti
// duali limitati privilegiano posizione del bordo e G1 rispetto alla forma;
// scarti e incompatibilita' restano misurati, non si promette un fit esatto.
// Il contorno ha priorita' sulla G1; conflitti dei piani tangenti ai vertici
// e tangenti dei bordi fuori da tali piani sono segnalati nel rapporto.
// Il campionamento segue le lunghezze delle singole campate delle curve.
//
// Forma direzionale opzionale: shapeContact sceglie un supporto; il campo di
// curvatura del suo profilo viene trasportato fino ai bordi opposti lungo il
// flusso parametrico del supporto, come obiettivo interno morbido. Sul bordo
// scelto la curvatura trasversale guida la forma (attenuata ai vertici).
// Posizione/G1 degli altri contatti rimangono prioritari. Peso zero ripristina
// la forma libera. Non equivale alla copia esatta dell'intera superficie di
// supporto e non garantisce compatibilita' di qualsiasi rete di vincoli.
//
// Contatti quasi verticali o rivolti fuori dal contorno: percorso parametrico
// 3D con continuazione della faccia (necessario per i tappi cilindrici),
// influenza sulla derivata trasversale ed energia di flessione. L'interno
// viene verificato separatamente per finitezza e Jacobiano proiettato positivo
// su nove campioni per cella nel trim; non e' una certificazione globale.
//
// Vicino ai bordi in tangenza le guide sfumano in una fascia larga 0.15 volte
// il contorno, per non imporre una pendenza incompatibile al contatto.
// Raffinamento fino alla tolleranza di posizione o a maxSpans. Il rapporto
// riporta gli scarti reali; lo scarto residuo del contorno diventa tolleranza
// degli edge. I bordi 3D originali non vengono rifittati o rimodellati.
namespace ForgeCad::Kernel {

struct FillContact {
    std::shared_ptr<const Body> body;
    FaceId face;
    double influence = -1.0; // -1 eredita FillOptions::influence; altrimenti > 0

};

struct FillOptions {
    int continuity = 1;          // 0 contatto, 1 tangenza, 2 curvatura (sui tratti con una faccia adiacente)
    double influence = 1.0;      // controllo di forma (1 naturale); non scala la G1 sul raccordo proiettabile
    double guideWeight = 1.0;    // peso delle guide rispetto al contorno (0 = ignorate)
    double tolerance = 1e-6;     // scarto voluto sul contorno e sulle guide
    int shapeContact = -1;       // indice del contatto che guida le sezioni interne; -1 forma libera
    double shapeWeight = 1.0;   // forza del controllo direzionale, separata dalla tangenza
    int maxSpans = 96;           // celle per lato al massimo
};

struct FillReport {
    double boundaryDeviation = 0.0;  // scarto massimo dal contorno (mm)
    double guideDeviation = 0.0;     // scarto massimo dalle guide (mm), fuori dalle fasce vicine ai tratti in tangenza
    double guideBlendDeviation = 0.0;  // scarto delle guide nelle fasce (vi pesano meno, fino a zero sul bordo)
    double tangentAngle = 0.0;       // angolo massimo fra piani tangenti, indipendente dal verso delle normali (radianti)
    double curvatureDeviation = 0.0; // scarto massimo della curvatura normale trasversale (1/mm), G2
    int contactPieces = 0;           // tratti del contorno con una faccia adiacente
    int spans = 0;                   // celle per lato della griglia finale
    std::vector<std::string> notes;
};

// std::domain_error: contorno aperto, contorno o guide che si sovrappongono
// nella proiezione, guide fuori dal contorno, risultato non valido.
Body fillSurface(const std::vector<PathSegment> &boundary, const std::vector<PathSegment> &guides,
                 const std::vector<FillContact> &contacts, const FillOptions &options = {}, FillReport *report = nullptr, FaceAliases *labels = nullptr);

}

#endif
