#pragma once
#include "fk_surface_tools.h"
#include "fk_pcurve.h"
#include "fk_curve_algo.h"
namespace surface_fixture {
using namespace ForgeCad::Kernel;
inline PathSegment line(const Vec3 &a, const Vec3 &b) {
    return {std::make_shared<Line<3>>(a, b - a), {0, distance(a, b)}};
}
inline Body patch(bool hole = false) {
    std::vector<Vec3> poles;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            poles.emplace_back(10.0 * i / 3, 10.0 * j / 3, 0);
    Body::BuildFace face;
    face.surface = std::make_shared<BSplineSurface>(3, 3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                    std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1}, 4, 4, poles);
    std::vector<Vec3> vertices;
    std::vector<Body::BuildEdge> edges;
    const auto loop = [&](const std::vector<Vec3> &points) {
        std::vector<Body::BuildFin> fins;
        const int first = vertices.size();
        for (const auto &p : points)
            vertices.push_back(p);
        for (int i = 0; i < int(points.size()); ++i) {
            const auto &a = points[i], &b = points[(i + 1) % points.size()];
            const auto piece = line(a, b);
            const auto uv =
                std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0, 0, piece.range.hi, piece.range.hi},
                                                  std::vector<Vec2>{Vec2(a.x() / 10, a.y() / 10), Vec2(b.x() / 10, b.y() / 10)});
            fins.push_back({int(edges.size()), true, uv, 1e-9});
            edges.push_back({first + i, first + (i + 1) % int(points.size()), piece.curve, piece.range, 1e-9});
        }
        face.loops.push_back(std::move(fins));
    };
    loop({Vec3(0, 0, 0), Vec3(10, 0, 0), Vec3(10, 10, 0), Vec3(0, 10, 0)});
    if (hole)
        loop({Vec3(3, 3, 0), Vec3(3, 7, 0), Vec3(7, 7, 0), Vec3(7, 3, 0)});
    return Body::buildSheet(vertices, edges, {face});
}
inline EdgeId edgeNear(const Body &body, const Vec3 &p) {
    EdgeId best;
    double d = 1e300;
    for (const auto e : body.edges()) {
        const auto &edge = body.edge(e);
        const double q = projectPoint(*edge.curve, p, edge.range).distance;
        if (q < d) {
            d = q;
            best = e;
        }
    }
    return best;
}
} // namespace surface_fixture
