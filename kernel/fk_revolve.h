#ifndef FORGECAD_FK_REVOLVE_H
#define FORGECAD_FK_REVOLVE_H

#include "fk_profile.h"
#include "fk_history.h"
#include "fk_topology.h"

// Solido di rivoluzione (giro completo) di una regione piana.
//
// La regione sta nel semipiano x >= 0 del piano XZ di `frame`: il punto (x, y)
// del profilo e' frame.origin + x X + y Z e l'asse di rotazione e' Z. Ogni
// tratto del profilo genera una faccia sulla superficie esatta: piano (tratto
// perpendicolare all'asse), cilindro (parallelo), cono (obliquo), sfera
// (arco con il centro sull'asse), toro (arco con il centro fuori dall'asse),
// superficie di rivoluzione (altre curve). I tratti che giacciono sull'asse
// non generano facce. Ogni vertice fuori dall'asse diventa un edge circolare
// chiuso; le facce periodiche non hanno cucitura (due loop, o uno solo se un
// estremo del tratto sta sull'asse: la faccia ha un polo). Come in Parasolid.
namespace ForgeCad::Kernel {

enum class RevolutionFaceRole : unsigned char { Start, End, Side };
struct RevolutionFaceOrigin {
    FaceId face;
    RevolutionFaceRole role = RevolutionFaceRole::Side;
    int loop = -1, segment = -1;  // indici nel profilo di ingresso, prima di invertire i loop
    int source = -1;             // ProfileSegment::source; i tratti sull'asse non generano facce
};
struct RevolutionProvenance {
    // Piu' origini per faccia se il taglio angolare unisce superfici equivalenti.
    std::vector<RevolutionFaceOrigin> faces;
};

Body makeRevolution(const Frame3 &frame, const ProfileRegion &region);

// Rivoluzione parziale: la regione ruota di `angle` radianti attorno a Z
// (verso destrorso, negativo al contrario; |angle| >= 2 pi e' il giro
// completo). E' il giro completo intersecato con il cuneo dei due semipiani
// per l'asse ad angolo 0 (il piano del profilo) e `angle`: le facce stanno
// sulle stesse superfici esatte, piu' le due facce piane del profilo.
Body makeRevolution(const Frame3 &frame, const ProfileRegion &region, double angle, RevolutionProvenance *provenance = nullptr);

// Lamina di rivoluzione: catene aperte (tratti in ordine, nel semipiano x >= 0
// del piano XZ di `frame`) ruotate di `angle` attorno a Z (|angle| >= 2 pi:
// giro completo). Le facce sono quelle di makeRevolution; i cerchi degli
// estremi fuori dall'asse sono bordi liberi, gli estremi sull'asse poli. La
// normale sta a destra del verso della catena. Il giro parziale e' il giro
// completo intersecato con il cuneo, come per i solidi.
Body makeSheetRevolution(const Frame3 &frame, const std::vector<ProfileLoop> &chains, double angle, RevolutionProvenance *provenance = nullptr);

// Solidi elementari di rivoluzione attorno a Z di `frame`, base nell'origine.
// Sfera di centro l'origine; cono (o tronco) di raggio `baseRadius` a z = 0 e
// `topRadius` a z = height (uno dei due puo' essere nullo); toro di raggio
// maggiore `majorRadius` e minore `minorRadius` (minore < maggiore) nel piano XY.
Body makeSphere(const Frame3 &frame, double radius, FaceAliases *labels = nullptr);
Body makeCone(const Frame3 &frame, double baseRadius, double topRadius, double height, FaceAliases *labels = nullptr);
Body makeTorus(const Frame3 &frame, double majorRadius, double minorRadius, FaceAliases *labels = nullptr);

}

#endif
