#pragma once
#include "fk_surface.h"
#include "fk_history.h"
#include "fk_topology.h"
#include <functional>
namespace ForgeCad::Kernel::detail {
struct MappingJet {
    Vec3 p, pu, pv, puv;
};
using MappingJetFunction = std::function<MappingJet(double, double, int, int)>;
// Fit Hermite cubico/quintico adattivo; budget geometrico fissato prima della costruzione.
SurfacePtr fitMappingSurface(std::vector<double> us, std::vector<double> vs,
    const std::function<Vec3(double, double)> &value, const MappingJetFunction &jet, double tolerance,
    double &deviation, double derivativeFraction = .001, int degree = 3);
// Una faccia che cade esattamente su un carrier puo' conservarlo,
// trasferendo le SP-curve mediante l'inversa nota della mappa.
struct MappingFaceImage {
    SurfacePtr surface;
    std::function<Vec2(const Vec3 &)> parameters;
    bool sense = true;
};
using MappingFaceFunction = std::function<MappingFaceImage(FaceId)>;
Body mapBodyByFunction(const Body &source, const std::function<Vec3(const Vec3 &)> &map,
    const std::function<Vec3(const Vec3 &)> &carrierMap, double tolerance, BodyHistory *history, double *deviation,
    const std::vector<Plane> &cuts = { }, double derivativeFraction = .001, int degree = 3, const MappingFaceFunction &faceImage = {});
}
