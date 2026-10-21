#ifndef FORGECAD_FK_MICRO_H
#define FORGECAD_FK_MICRO_H

#include <vector>

#include "fk_topology.h"

// Micro-geometrie di un B-rep: facce sottili, spigoli corti, spigoli quasi
// tangenti (pochi gradi tra le normali). Sono di solito rumore di una feature
// a monte (le strisce larghe 0.008 mm tra le meta' dell'offset di un loft
// riparametrizzato male) e fanno fallire raccordi e offset: si segnalano
// prima, con la misura, per correggerle dove nascono. Solo diagnostica: il
// body non cambia.
namespace ForgeCad::Kernel {

struct MicroFeatureOptions {
    double thinWidth = 0.05;     // facce piu' strette di cosi' lungo la maggior parte del bordo
    double shortEdge = 0.05;     // spigoli piu' corti di cosi'
    double minAngle = 1e-3;      // radianti: sotto e' una giunzione tangente
    double maxAngle = 0.175;     // radianti (10 gradi): sopra e' uno spigolo vivo voluto
};

struct MicroFeature {
    enum class Kind { ThinFace, ShortEdge, NearTangentEdge };
    Kind kind;
    int index = -1;       // FaceId o EdgeId
    double measure = 0.0; // larghezza, lunghezza o angolo (radianti)
    Vec3 location;        // un punto dell'entita'
};

std::vector<MicroFeature> findMicroFeatures(const Body &body, const MicroFeatureOptions &options = {});

}

#endif
