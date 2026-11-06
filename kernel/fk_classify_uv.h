#pragma once
#include "fk_classify.h"
namespace ForgeCad::Kernel {
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
