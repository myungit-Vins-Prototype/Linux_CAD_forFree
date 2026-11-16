#include "fk_curve_projection.h"
#include "fk_bspline.h"
#include <cmath>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
CurvePtr<2> project(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame,
                    const Transform3 &transform) {
    cancellationPoint();
    const auto point = [&](const Vec3 &p) {
        const Vec3 q = frame.toLocal(transform.applyToPoint(p));
        return Vec2(q.x(), q.y());
    };
    switch (source->type()) {
    case CurveType::BSpline: {
        const auto &spline = static_cast<const BSplineCurve<3> &>(*source);
        std::vector<Vec2> poles;
        poles.reserve(spline.poles().size());
        for (const Vec3 &p : spline.poles()) {
            cancellationPoint();
            poles.push_back(point(p));
        }
        return std::make_shared<BSplineCurve<2>>(spline.degree(), spline.knots(), std::move(poles),
                                                 spline.weights());
    }
    case CurveType::Line: {
        const auto &line = static_cast<const Line<3> &>(*source);
        return std::make_shared<BSplineCurve<2>>(
            1, std::vector<double>{range.lo, range.lo, range.hi, range.hi},
            std::vector<Vec2>{point(line.point(range.lo)), point(line.point(range.hi))});
    }
    case CurveType::Trimmed: {
        const auto &trim = static_cast<const TrimmedCurve<3> &>(*source);
        // The reconstructed trim needs its whole natural interval, even when
        // the caller requests a smaller range (notably for a trimmed line).
        auto basis = project(trim.basis(), trim.domain(), frame, transform);
        return basis ? std::make_shared<TrimmedCurve<2>>(basis, trim.domain().lo, trim.domain().hi)
                     : CurvePtr<2>{};
    }
    case CurveType::Transformed: {
        const auto &moved = static_cast<const TransformedCurve &>(*source);
        return project(moved.basis(), range, frame, transform * moved.transform());
    }
    case CurveType::Circle:
    case CurveType::Ellipse:
    case CurveType::Other:
        return {};
    }
    throw std::domain_error("projectCurveAffine: tipo curva non valido");
}
} // namespace
CurvePtr<2> projectCurveAffine(const CurvePtr<3> &source, const Interval &range, const Frame3 &frame) {
    cancellationPoint();
    if (!source || !range.isFinite() || !(range.lo < range.hi) || !std::isfinite(range.length()))
        throw std::domain_error("projectCurveAffine: curva o intervallo non valido");
    auto result = project(source, range, frame, Transform3());
    cancellationPoint();
    return result;
}
} // namespace ForgeCad::Kernel
