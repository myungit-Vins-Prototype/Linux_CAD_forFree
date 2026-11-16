#include "fk_sweep_atlas.h"
#include "fk_nurbs.h"
#include <map>
#include <stdexcept>
namespace ForgeCad::Kernel::detail {
// Exact rectangular parametrizations of the two analytic sweep motions.
// Parameters are profile first, path second, also for revolutions.
std::shared_ptr<const BSplineSurface> translationChart(const Curve<3> &section, const Interval &range,
                                                       const Vec3 &displacement) {
    const BSplineCurve<3> c = joinBezierPieces(standardBezierPieces(section, range));
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < c.poleCount(); ++i) {
        poles.push_back(c.poles()[std::size_t(i)]);
        poles.push_back(c.poles()[std::size_t(i)] + displacement);
        weights.push_back(c.weight(i));
        weights.push_back(c.weight(i));
    }
    return std::make_shared<BSplineSurface>(c.degree(), 1, c.knots(), std::vector<double>{0, 0, 1, 1}, c.poleCount(), 2,
                                            std::move(poles), std::move(weights));
}

std::shared_ptr<const BSplineSurface> rotationChart(const Curve<3> &section, const Interval &range, const Vec3 &center,
                                                    const Vec3 &axis, double angle) {
    const BSplineCurve<3> c = joinBezierPieces(standardBezierPieces(section, range));
    const auto circle = conicArcToBSpline<2>(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1), 0, angle);
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < c.poleCount(); ++i) {
        const Vec3 q = c.poles()[std::size_t(i)] - center;
        const Vec3 parallel = dot(q, axis) * axis, radial = q - parallel;
        for (int j = 0; j < circle.poleCount(); ++j) {
            const Vec2 &cs = circle.poles()[std::size_t(j)];
            poles.push_back(center + parallel + cs.x() * radial + cs.y() * cross(axis, radial));
            weights.push_back(c.weight(i) * circle.weight(j));
        }
    }
    return std::make_shared<BSplineSurface>(c.degree(), circle.degree(), c.knots(), circle.knots(), c.poleCount(),
                                            circle.poleCount(), std::move(poles), std::move(weights));
}
// Una frontiera immersa non esclude da sola regioni annidate con molteplicita'
// due. Le intersezioni fra contorni sono controllate dall'atlante dei lati;
// qui verifichiamo il nesting della sezione iniziale, che il moto continuo
// preserva finche' le frontiere non si incontrano.
void requireProfileNesting(const std::vector<ProfileLoop> &loops, const std::vector<int> &regions,
                           const Frame3 &frame) {
    std::map<int, std::vector<ProfileLoop>> grouped;
    for (std::size_t i = 0; i < loops.size(); ++i)
        if (regions[i] >= 0)
            grouped[regions[i]].push_back(loops[i]);
    std::vector<detail::CapDomainIndex> domains;
    std::vector<Vec2> representatives;
    for (const auto &entry : grouped) {
        const auto &contours = entry.second;
        detail::CapDomainIndex domain(contours, frame, linearResolution());
        for (std::size_t h = 1; h < contours.size(); ++h) {
            const Vec2 point = contours[h].segments.front().start();
            if (domain.classifyPoint(point, 0) != detail::CapBoxLocation::Inside)
                throw std::domain_error("sweep: foro fuori dal contorno esterno o nesting non risolto");
            for (std::size_t other = 1; other < contours.size(); ++other)
                if (other != h && domain.classifyPoint(point, int(other)) != detail::CapBoxLocation::Outside)
                    throw std::domain_error("sweep: fori annidati o nesting non risolto");
        }
        domains.push_back(std::move(domain));
        representatives.push_back(contours.front().segments.front().start());
    }
    for (std::size_t i = 0; i < domains.size(); ++i)
        for (std::size_t j = 0; j < domains.size(); ++j)
            if (i != j && domains[j].classifyPoint(representatives[i]) != detail::CapBoxLocation::Outside)
                throw std::domain_error("sweep: regioni sovrapposte o nesting non risolto");
}

void requireDisjointSweepCaps(const Body &body, const std::vector<SweepSurfaceChart> &charts,
                              const std::vector<SweepCap> &caps) {
    std::vector<CapDomainIndex> domains;
    for (const auto &cap : caps) {
        domains.emplace_back(body, cap.face, linearResolution());
        const auto validation =
            validateSweepSurfaceCap(charts, domains.back(), cap.families, cap.boundaryV, linearResolution());
        if (validation.status != SweepSurfaceValidationStatus::Certified)
            throw std::domain_error("sweep: intersezione testa/lato non esclusa (" + validation.diagnostic + ")");
    }
    for (std::size_t i = 0; i < domains.size(); ++i)
        for (std::size_t j = i + 1; j < domains.size(); ++j)
            if (!capDomainsDisjoint(domains[i], domains[j], linearResolution()))
                throw std::domain_error("sweep: sovrapposizione delle teste non esclusa");
}

} // namespace ForgeCad::Kernel::detail
