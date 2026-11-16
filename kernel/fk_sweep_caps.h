#ifndef FORGECAD_FK_SWEEP_CAPS_H
#define FORGECAD_FK_SWEEP_CAPS_H
#include "fk_profile.h"
#include "fk_sweep_surface_validation.h"
#include "fk_topology.h"
#include <cstddef>
#include <memory>
namespace ForgeCad::Kernel::detail {
enum class CapBoxLocation { Outside, Inside, Unresolved };
struct CapDomainOptions {
    std::size_t maxVisits = 100000;
    int maxDepth = 40;
};
// Conservativo rispetto ai trim razionali, con fascia fisica tolerance.
// Curve che incontrano la scatola non vengono sostituite da campioni.
// B-spline non clamped/tipi non supportati restituiscono Unresolved.
// loop=-1: primo loop esterno meno gli altri (fori), indipendente dai versi;
// >=0: singolo loop per test di nesting.
class CapDomainIndex {
  public:
    CapDomainIndex(const Body &body, FaceId planarFace, double tolerance);
    CapDomainIndex(const std::vector<ProfileLoop> &loops, const Frame3 &plane, double tolerance);
    CapBoxLocation classifyBox(const Vec2 &lo, const Vec2 &hi, int loop = -1,
                               const CapDomainOptions &options = {}) const;
    // Variante per operazioni composte: decrementa il budget condiviso.
    CapBoxLocation classifyBoxBudget(const Vec2 &lo, const Vec2 &hi, int loop, const CapDomainOptions &options,
                                     std::size_t &remaining) const;
    CapBoxLocation classifyPoint(const Vec2 &point, int loop = -1, const CapDomainOptions &options = {}) const;
    const Frame3 &frame() const;
    std::pair<Vec2, Vec2> bounds() const;
    std::size_t loopCount() const;

  private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
// Tutte le famiglie laterali sono controllate; solo quelle dichiarate possono
// usare il collare del bordo v genealogico.
SweepSurfaceValidationResult validateSweepSurfaceCap(const std::vector<SweepSurfaceChart> &charts,
                                                     const CapDomainIndex &cap,
                                                     const std::vector<int> &boundaryFamilies, double boundaryV,
                                                     double tolerance,
                                                     const SweepSurfaceValidationOptions &options = {});
// Prova disgiunzione fra facce di testa con trim/fori conservati.
// Falso significa contatto/sovrapposizione oppure verifica non conclusa.
bool capDomainsDisjoint(const CapDomainIndex &a, const CapDomainIndex &b, double tolerance,
                        const CapDomainOptions &options = {});
} // namespace ForgeCad::Kernel::detail
#endif
