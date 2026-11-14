#pragma once
#include "cad_drawing_dimension.h"
#include <algorithm>
#include <cmath>

namespace ForgeCad {
inline QString validateDrawingLocalSection(const DrawingView &view) {
    if (!view.localSection)
        return {};
    const auto &section = *view.localSection;
    if (view.kind != DrawingViewKind::ProjectedEdges || view.sectionStyle != DrawingSectionStyle::Removed ||
        view.step || view.bend || view.sectionReferences)
        return QStringLiteral("La sezione locale richiede una vista proiettata senza altri tagli.");
    if (!std::isfinite(section.front) || !std::isfinite(section.depth) || section.depth <= 0 ||
        section.depth > 1e7 || std::abs(section.front) > 1e7 || std::abs(section.front - section.depth) > 1e7)
        return QStringLiteral("Piano iniziale o profondita' della sezione locale non validi.");
    const auto &p = section.outline;
    if (p.size() < 3 || p.size() > 4096)
        return QStringLiteral("Il contorno della sezione locale richiede da 3 a 4096 vertici.");
    long double area = 0;
    const auto cross = [](Kernel::Vec2 a, Kernel::Vec2 b) {
        return (long double)a.x() * b.y() - (long double)a.y() * b.x();
    };
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (!std::isfinite(p[i].x()) || !std::isfinite(p[i].y()) || std::abs(p[i].x()) > 1e7 ||
            std::abs(p[i].y()) > 1e7 || Kernel::distance(p[i], p[(i + 1) % p.size()]) <= view.tolerance)
            return QStringLiteral("Contorno non finito o con vertici coincidenti.");
        area += cross(p[i] - p[0], p[(i + 1) % p.size()] - p[0]);
    }
    if (std::abs(area) <= view.tolerance * view.tolerance)
        return QStringLiteral("Il contorno della sezione locale ha area nulla.");
    const auto on = [](Kernel::Vec2 a, Kernel::Vec2 b, Kernel::Vec2 q) {
        return q.x() >= std::min(a.x(), b.x()) && q.x() <= std::max(a.x(), b.x()) &&
               q.y() >= std::min(a.y(), b.y()) && q.y() <= std::max(a.y(), b.y());
    };
    for (std::size_t i = 0; i < p.size(); ++i)
        for (std::size_t j = i + 1; j < p.size(); ++j) {
            if (j == i + 1 || (i == 0 && j + 1 == p.size()))
                continue;
            const auto a = p[i], b = p[(i + 1) % p.size()], c = p[j], d = p[(j + 1) % p.size()];
            const auto u = cross(b - a, c - a), v = cross(b - a, d - a), w = cross(d - c, a - c),
                       z = cross(d - c, b - c);
            if (((u < 0 && v > 0) || (u > 0 && v < 0)) && ((w < 0 && z > 0) || (w > 0 && z < 0)))
                return QStringLiteral("Il contorno della sezione locale si interseca.");
            if ((u == 0 && on(a, b, c)) || (v == 0 && on(a, b, d)) || (w == 0 && on(c, d, a)) ||
                (z == 0 && on(c, d, b)))
                return QStringLiteral("Il contorno della sezione locale si tocca o si sovrappone.");
        }
    return {};
}
inline DrawingView drawingLocalSectionPlane(const DrawingView &view) {
    auto cut = view;
    const double level = view.localSection->front - view.localSection->depth;
    cut.localSection.reset();
    cut.kind = DrawingViewKind::Section;
    cut.sectionStyle = DrawingSectionStyle::Full;
    cut.sectionReferences = false;
    cut.plane = Kernel::Frame3::fromAxes(view.plane.toGlobal(Kernel::Vec3(0, 0, level)), view.plane.xDir(),
                                         view.plane.yDir(), view.plane.zDir());
    return cut;
}
// Unisce le due proiezioni della stessa sorgente. I bordi originali mantengono
// un unico riferimento; le intersezioni del piano locale restano entita' distinte.
inline std::vector<std::size_t> mergeDrawingLocalCurves(const DrawingView &view,
                                                        std::vector<DrawingCurve> &outside,
                                                        const std::vector<DrawingCurve> &inside) {
    std::vector<std::size_t> mapping;
    const auto original = outside.size();
    for (const auto &curve : inside) {
        std::size_t match = outside.size();
        if (curve.kind != DrawingCurveKind::Section)
            for (std::size_t i = 0; i < original; ++i) {
                const auto &old = outside[i];
                if (old.featureId != curve.featureId || old.kind != curve.kind ||
                    old.sourceEdge != curve.sourceEdge || old.sourceFace != curve.sourceFace ||
                    old.contourSide != curve.contourSide || old.edgeName != curve.edgeName)
                    continue;
                if (curve.sourceEdge.valid()) {
                    match = i;
                    break;
                }
                const auto a = drawingPrimitive(view, old), b = drawingPrimitive(view, curve);
                if (a.kind != b.kind)
                    continue;
                if (a.kind == DrawingPrimitive::Kind::Line) {
                    const auto d = a.lineSecond - a.lineFirst;
                    const double length = Kernel::norm(d);
                    if (length <= view.tolerance)
                        continue;
                    const auto cross = [](Kernel::Vec2 x, Kernel::Vec2 y) {
                        return x.x() * y.y() - x.y() * y.x();
                    };
                    if (std::abs(cross(b.lineFirst - a.lineFirst, d)) / length <= view.tolerance &&
                        std::abs(cross(b.lineSecond - a.lineFirst, d)) / length <= view.tolerance) {
                        match = i;
                        break;
                    }
                } else if (a.kind == DrawingPrimitive::Kind::Conic && a.circular && b.circular &&
                           Kernel::distance(a.center, b.center) <= view.tolerance &&
                           std::abs(Kernel::norm(a.firstAxis) - Kernel::norm(b.firstAxis)) <=
                               view.tolerance) {
                    match = i;
                    break;
                }
            }
        if (match == outside.size())
            outside.push_back(curve);
        mapping.push_back(match);
    }
    return mapping;
}

} // namespace ForgeCad
