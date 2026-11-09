#pragma once
#include "fk_surface_tools.h"
#include <array>
#include <memory>
namespace ForgeCad::Kernel {
struct SurfacePoleContinuityReport {
    int connected = 0, tangent = 0, sharp = 0, protectedJoints = 0, followers = 0;
    double gap = 0, angle = 0; // mm e radianti, verifica campionata
};
// Vincoli sui giunti topologici: G0 e G1 esistente, senza ricerca per prossimita'.
// Le righe compatibili usano equazioni esatte sui poli razionali; i giunti
// non rappresentabili sono protetti. Con active i poli del gesto corrente
// restano rigidi e gli altri comandi sono preferenze; senza active tutti i
// valori risolti sono rigidi, per ricostruire esattamente lo stato salvato.
class SurfacePoleConstraints {
  public:
    SurfacePoleConstraints(const Body &body, const std::vector<SurfaceFacePoleChange> &bases, int continuity,
                           double tolerance);
    std::vector<SurfaceFacePoleChange> propagate(const std::vector<SurfaceFacePoleChange> &drivers,
                                                 SurfacePoleContinuityReport *report = nullptr,
                                                 const std::vector<std::array<int, 3>> *active = nullptr) const;
    SurfacePoleContinuityReport report() const;
    double refitDeviation() const;

  private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
std::vector<FaceId> connectedSurfacePoleFaces(const Body &body, const std::vector<FaceId> &seeds);
SurfacePoleContinuityReport checkSurfacePoleContinuity(const Body &reference, const Body &result,
                                                       const std::vector<FaceId> &faces, double tolerance);
} // namespace ForgeCad::Kernel
