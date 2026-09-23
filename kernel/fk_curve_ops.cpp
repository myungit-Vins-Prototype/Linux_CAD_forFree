#include "fk_curve_ops.h"

#include <algorithm>
#include <stdexcept>

#include "fk_bspline.h"

namespace ForgeCad::Kernel {

template <int N>
CurvePtr<N> reversedCurve(const CurvePtr<N> &curve) {
    if (!curve) throw std::invalid_argument("reversedCurve: curva nulla");
    switch (curve->type()) {
    case CurveType::Line: {
        const auto &line = static_cast<const Line<N> &>(*curve);
        return std::make_shared<Line<N>>(line.origin(), -line.direction());
    }
    case CurveType::Circle: {
        const auto &circle = static_cast<const Circle<N> &>(*curve);
        return std::make_shared<Circle<N>>(circle.center(), circle.xAxis(), -circle.yAxis(), circle.radius());
    }
    case CurveType::Ellipse: {
        const auto &ellipse = static_cast<const Ellipse<N> &>(*curve);
        return std::make_shared<Ellipse<N>>(ellipse.center(), ellipse.xAxis(), -ellipse.yAxis(), ellipse.xRadius(),
                                            ellipse.yRadius());
    }
    case CurveType::BSpline: {
        const auto &spline = static_cast<const BSplineCurve<N> &>(*curve);
        std::vector<double> knots(spline.knots().rbegin(), spline.knots().rend());
        for (double &knot : knots) knot = -knot;
        std::vector<Vec<N>> poles(spline.poles().rbegin(), spline.poles().rend());
        std::vector<double> weights(spline.weights().rbegin(), spline.weights().rend());
        return std::make_shared<BSplineCurve<N>>(spline.degree(), std::move(knots), std::move(poles), std::move(weights));
    }
    case CurveType::Trimmed: {
        const auto &trimmed = static_cast<const TrimmedCurve<N> &>(*curve);
        return std::make_shared<TrimmedCurve<N>>(reversedCurve<N>(trimmed.basis()), -trimmed.domain().hi, -trimmed.domain().lo);
    }
    case CurveType::Transformed:
        if constexpr (N == 3) {
            const auto &transformed = static_cast<const TransformedCurve &>(*curve);
            return std::make_shared<TransformedCurve>(reversedCurve<3>(transformed.basis()), transformed.transform());
        }
        break;
    default:
        break;
    }
    throw std::invalid_argument("reversedCurve: tipo di curva non supportato");
}

CurvePtr<3> embedCurve(const CurvePtr<2> &curve, const Frame3 &frame, double z) {
    if (!curve) throw std::invalid_argument("embedCurve: curva nulla");
    auto point = [&](const Vec2 &p) { return frame.toGlobal(Vec3(p.x(), p.y(), z)); };
    auto direction = [&](const Vec2 &d) { return frame.directionToGlobal(Vec3(d.x(), d.y(), 0.0)); };
    switch (curve->type()) {
    case CurveType::Line: {
        const auto &line = static_cast<const Line<2> &>(*curve);
        return std::make_shared<Line<3>>(point(line.origin()), direction(line.direction()));
    }
    case CurveType::Circle: {
        const auto &circle = static_cast<const Circle<2> &>(*curve);
        return std::make_shared<Circle<3>>(point(circle.center()), direction(circle.xAxis()), direction(circle.yAxis()),
                                           circle.radius());
    }
    case CurveType::Ellipse: {
        const auto &ellipse = static_cast<const Ellipse<2> &>(*curve);
        return std::make_shared<Ellipse<3>>(point(ellipse.center()), direction(ellipse.xAxis()), direction(ellipse.yAxis()),
                                            ellipse.xRadius(), ellipse.yRadius());
    }
    case CurveType::BSpline: {
        const auto &spline = static_cast<const BSplineCurve<2> &>(*curve);
        std::vector<Vec3> poles;
        for (const Vec2 &pole : spline.poles()) poles.push_back(point(pole));
        return std::make_shared<BSplineCurve<3>>(spline.degree(), spline.knots(), std::move(poles), spline.weights());
    }
    case CurveType::Trimmed: {
        const auto &trimmed = static_cast<const TrimmedCurve<2> &>(*curve);
        return std::make_shared<TrimmedCurve<3>>(embedCurve(trimmed.basis(), frame, z), trimmed.domain().lo, trimmed.domain().hi);
    }
    default:
        break;
    }
    throw std::invalid_argument("embedCurve: tipo di curva non supportato");
}

template CurvePtr<2> reversedCurve<2>(const CurvePtr<2> &);
template CurvePtr<3> reversedCurve<3>(const CurvePtr<3> &);

}
