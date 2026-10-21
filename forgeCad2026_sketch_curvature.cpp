#include "forgeCad2026_sketch_curvature.h"
#include "cad_curve_solver.h"
#include "cad_shape_analysis.h"
#include "fk_curve_ops.h"
namespace ForgeCad::Gui {
const QVector<ForgeCad::OverlayVertex> &SketchCurvatureCache::lines(int index, const CurveObject &curve, double scale) {
    if (entries_.size() <= index) entries_.resize(index+1);
    auto &cache = entries_[index];
    const auto &old = cache.source;
    if (!cache.valid || cache.scale != scale || old.tool != curve.tool
        || old.controlPoints != curve.controlPoints || old.tangentHandles != curve.tangentHandles
        || old.weights != curve.weights || old.knots != curve.knots || old.degree != curve.degree) {
        cache.lines.clear(); cache.source=curve; cache.scale=scale; cache.valid=true;
        try {
            for (const auto &segment : ForgeCad::curveGeometry(curve)) {
                ExtrusionObject object;
                object.curve=std::make_shared<ForgeCad::Kernel::TrimmedCurve<3>>(
                    ForgeCad::Kernel::embedCurve(segment.curve, ForgeCad::Kernel::Frame3{}), segment.range.lo, segment.range.hi);
                cache.lines += ForgeCad::analyzeShape(object,4,scale,.1,.001).lines;
            }
        } catch (const std::exception &) { cache.lines.clear(); }
    }
    return cache.lines;
}
}
