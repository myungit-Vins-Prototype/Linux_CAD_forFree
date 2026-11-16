#include "fk_affine_surface_fit.h"
#include "fk_cancel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ForgeCad::Kernel::detail {
SurfaceCurveFitData affineSurfaceCompositionErrorBound(const SurfacePtr &surface, const CurvePtr<2> &pcurve,
                                                       const Interval &range) {
    cancellationPoint();
    if (!surface || !pcurve || !range.isFinite() || !(range.lo < range.hi) || !std::isfinite(range.length()))
        throw std::domain_error("fit UV affine: sorgente o intervallo non valido");
    if (surface->type() != SurfaceType::Extrusion)
        return {};
    const Curve<2> *source = pcurve.get();
    while (source->type() == CurveType::Trimmed) {
        cancellationPoint();
        source = static_cast<const TrimmedCurve<2> *>(source)->basis().get();
    }
    std::vector<AffineUVSpan> spans;
    if (source->type() == CurveType::Line) {
        const auto &line = static_cast<const Line<2> &>(*source);
        spans.push_back({range, {}, line.origin(), line.direction(), true});
    } else if (source->type() == CurveType::BSpline) {
        const auto &curve = static_cast<const BSplineCurve<2> &>(*source);
        if (curve.degree() != 1)
            return {};
        if (curve.poleCount() > 100000)
            throw std::domain_error("fit UV affine: budget poli esaurito");
        if (range.lo < curve.domain().lo || range.hi > curve.domain().hi)
            throw std::domain_error("fit UV affine: intervallo fuori dominio UV");
        for (int i = 1; i < curve.poleCount(); ++i) {
            cancellationPoint();
            const Interval anchor{curve.knots()[i], curve.knots()[i + 1]};
            if (!(anchor.lo < anchor.hi) || anchor.hi <= range.lo || anchor.lo >= range.hi)
                continue;
            if (curve.weight(i - 1) != curve.weight(i))
                return {};
            spans.push_back({{std::max(range.lo, anchor.lo), std::min(range.hi, anchor.hi)}, anchor,
                             curve.poles()[i - 1], curve.poles()[i], false});
        }
    } else {
        return {};
    }
    return extrusionAffineCompositionErrorBound(surface, spans, range);
}
}
