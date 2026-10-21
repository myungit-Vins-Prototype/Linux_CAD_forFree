#ifndef FORGECAD_FK_EXTRUDE_H
#define FORGECAD_FK_EXTRUDE_H

#include "fk_profile.h"
#include "fk_topology.h"

// Estrusione lineare di un profilo piano in un solido B-rep esatto.
namespace ForgeCad::Kernel {

enum class ExtrusionFaceRole { Start, End, Side };
struct ExtrusionFaceOrigin {
    FaceId face;
    ExtrusionFaceRole role = ExtrusionFaceRole::Side;
    int loop = -1, segment = -1; // 0 contorno esterno, 1+ fori; tratto nel loop di ingresso
    int source = -1; // ProfileSegment::source, anche dopo inversione del profilo
};
struct ExtrusionProvenance {
    std::vector<ExtrusionFaceOrigin> faces;
};

// Solido ottenuto estrudendo la regione (nel piano XY di `frame`) di
// `height` lungo la Z del sistema (anche negativa). Topologia costruita con
// operatori di Eulero, geometria esatta:
//  - base e coperchio piani, spigoli = curve del profilo alle due quote;
//  - facce laterali: piani per i segmenti, cilindri per gli archi di
//    cerchio, superfici estruse per ellissi, spline e NURBS;
//  - spigoli verticali solo tra tratti consecutivi: un loop fatto di una
//    sola curva chiusa (cerchio, spline chiusa) da' una faccia laterale con
//    due loop, senza cucitura.
// Ogni fin riceve la sua SP-curve esatta (computePCurves).
// Se due tratti consecutivi si toccano solo entro una tolleranza > 1e-7, i
// vertici diventano "tolleranti" (tolleranza locale = distanza).
Body makeExtrusion(const Frame3 &frame, const ProfileRegion &region, double height, ExtrusionProvenance *provenance = nullptr);

// Lamina: estrusione di catene aperte di tratti (un profilo che non si
// chiude), una faccia per tratto con le stesse superfici dei fianchi di
// makeExtrusion; gli spigoli verticali interni sono condivisi, quelli agli
// estremi e le curve in basso e in alto sono il bordo della lamina.
Body makeSheetExtrusion(const Frame3 &frame, const std::vector<ProfileLoop> &chains, double height, ExtrusionProvenance *provenance = nullptr);

}

#endif
