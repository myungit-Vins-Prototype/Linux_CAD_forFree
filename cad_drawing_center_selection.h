#pragma once
#include "cad_drawing_centers.h"
#include "cad_drawing_sheet.h"
#include <algorithm>
#include <stdexcept>

namespace ForgeCad {
inline DrawingDimension drawingCenterSupport(const DrawingCenterMark &mark) {
    DrawingDimension d;
    d.id = mark.id;
    d.viewId = mark.viewId;
    d.measure = DrawingMeasure::SectionLength;
    d.section = mark.reference.curve;
    d.references = {mark.reference};
    return d;
}
inline DrawingCenterMark drawingCenterMark(const DrawingView &view, const DrawingCurve &curve,
                                           const DrawingModelSnapshot &model, bool axes = false) {
    if (drawingPrimitive(view, curve).kind != DrawingPrimitive::Kind::Conic)
        throw std::invalid_argument("Scegli il bordo di un cerchio, arco o ellisse.");
    DrawingCenterMark mark;
    mark.viewId = view.id;
    mark.axes = axes;
    mark.reference = drawingAttachment(view, curve, model, DrawingPointRole::Curve);
    return mark;
}
inline bool sameDrawingCenter(const DrawingCenterMark &a, const DrawingCenterMark &b) {
    const auto &x = a.reference.curve, &y = b.reference.curve;
    return a.viewId == b.viewId && x.featureId == y.featureId && x.kind == y.kind &&
           x.faceName == y.faceName && x.edgeName == y.edgeName && x.contourSide == y.contourSide &&
           x.requiresWitness == y.requiresWitness &&
           (!x.requiresWitness || (x.witness.x() == y.witness.x() && x.witness.y() == y.witness.y()));
}
inline DrawingCenterGeometry drawingSelectedCenters(const DrawingView &view,
                                                    const QVector<DrawingCenterMark> &marks,
                                                    const DrawingModelSnapshot &model,
                                                    const std::vector<DrawingCurve> &curves,
                                                    int *missing = nullptr) {
    std::vector<bool> crosses(curves.size()), axes(curves.size());
    for (const auto &mark : marks)
        if (mark.viewId == view.id) {
            const auto resolved = resolveDrawingAttachment(view, model, curves, mark.reference);
            if (resolved.status != DrawingDimensionStatus::Ready || !resolved.curve ||
                drawingPrimitive(view, *resolved.curve).kind != DrawingPrimitive::Kind::Conic) {
                if (missing)
                    ++*missing;
                continue;
            }
            const auto found = std::find_if(curves.begin(), curves.end(),
                                            [&](const auto &curve) { return &curve == resolved.curve; });
            if (found == curves.end()) {
                if (missing)
                    ++*missing;
                continue;
            }
            const auto i = std::size_t(found - curves.begin());
            crosses[i] = true;
            axes[i] = axes[i] || mark.axes;
        }
    auto result = drawingCenterGeometry(view, curves, crosses);
    result.axes = drawingCenterGeometry(view, curves, axes).axes;
    return result;
}
} // namespace ForgeCad
