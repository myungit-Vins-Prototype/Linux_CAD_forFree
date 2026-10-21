#ifndef FORGECAD2026_SKETCH_CURVATURE_H
#define FORGECAD2026_SKETCH_CURVATURE_H
#include "cad_types.h"
#include "cad_overlay_renderer.h"
namespace ForgeCad::Gui {
// Possiede e invalida i pettini delle curve; i punti restano nel piano locale.
class SketchCurvatureCache {
public:
    void resize(qsizetype count) { entries_.resize(count); }
    void clear() { entries_.clear(); }
    const QVector<OverlayVertex> &lines(int index, const CurveObject &curve, double scale);
private:
    struct Entry {
        CurveObject source;
        double scale = -1;
        QVector<ForgeCad::OverlayVertex> lines;
        bool valid = false;
    };
    QVector<Entry> entries_;
};
}
#endif
