#pragma once

#include <optional>

#include "fk_classify.h"

namespace ForgeCad::Kernel {
// Inviluppo UV conservativo sul tratto richiesto. Le curve generiche e
// le spline fuori dominio non forniscono un box per escludere candidati.
std::optional<Box> conservativePCurveBox(const CurvePtr<2> &curve, const Interval &range);

// Classifica parametri gia' noti sulla superficie: stesso trim B-rep del
// classificatore 3D, senza riproiettare globalmente il punto. Il corpo deve
// restare immutabile e vivo; il budget e' una distanza fisica in mm.
class FaceParameterClassifier {
public:
    FaceParameterClassifier(const Body &body, FaceId face, double tolerance);
    PointLocation classify(const Vec2 &parameters) const;
private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
}
