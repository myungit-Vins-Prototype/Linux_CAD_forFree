#ifndef FORGECAD_FK_CLASSIFY_H
#define FORGECAD_FK_CLASSIFY_H

#include <vector>

#include "fk_intersect.h"
#include "fk_topology.h"

// Posizione di un punto rispetto a una faccia o a un solido, sulla geometria
// esatta.
namespace ForgeCad::Kernel {

enum class PointLocation { Outside, Inside, Boundary };

// Distanza del punto dal bordo della faccia (curve esatte degli edge).
double distanceToFaceBoundary(const Body &body, FaceId face, const Vec3 &point);

// Punto (che sta sulla superficie della faccia) dentro la faccia, fuori o
// sul bordo (entro tolerance). Nello spazio (u, v): una semiretta verticale
// dal punto e la prima SP-curve che incontra dice da che parte sta il dominio
// (la faccia e' a sinistra delle fin). Funziona anche sulle superfici
// periodiche in u senza cucitura. Servono le SP-curve di tutte le fin.
PointLocation classifyPointOnFace(const Body &body, FaceId face, const Vec3 &point, double tolerance);

// Parametro t >= 0 del primo punto in cui la semiretta origin + t direction
// incontra una faccia del body (falso se non la incontra) e, se `face` non e'
// nullo, quella faccia. Serve alla selezione a video.
bool firstRayHit(const Body &body, const Vec3 &origin, const Vec3 &direction, double tolerance, double &t, FaceId *face = nullptr);

// Box che contiene la faccia.
Box faceBox(const Body &body, FaceId face);

// Punto dentro, fuori o sul bordo dei solidi del body (raggio: parita' delle
// intersezioni con le facce; si cambia direzione se il raggio sfiora una
// faccia o passa su un edge).
class SolidClassifier {
public:
    SolidClassifier(const Body &body, double tolerance);
    PointLocation classify(const Vec3 &point) const;

private:
    const Body &body_;
    double tolerance_;
    std::vector<FaceId> faces_;
    std::vector<Box> boxes_;
};

}

#endif
