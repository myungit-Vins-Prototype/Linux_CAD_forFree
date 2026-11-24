#include "fk_tpms_spline.h"

#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_tpms_field.h"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace ForgeCad::Kernel {
namespace {

using Cubic = std::array<Vec3, 4>;
constexpr double chartLimit = 1.0 / 24.0;

struct Chart {
    std::array<FinId, 3> fins;
    std::array<Vec3, 3> points;
    Vec3 dualU, dualV;

    double deviation(const Vec3 &delta) const {
        return std::max(std::abs(dot(dualU, delta)), std::abs(dot(dualV, delta)));
    }
};

struct WallSample {
    double field;
    Vec3 gradient;
};

WallSample wallSample(const SheetGyroid &pattern, const Vec3 &point) {
    const auto x = tpmsPhase(point.x(), pattern.origin.x(), pattern.cellPitch);
    const auto y = tpmsPhase(point.y(), pattern.origin.y(), pattern.cellPitch);
    const auto z = tpmsPhase(point.z(), pattern.origin.z(), pattern.cellPitch);
    const double xy = x.sine * y.cosine, yz = y.sine * z.cosine, zx = z.sine * x.cosine;
    const double value = xy + yz + zx;
    const Vec3 gradient{x.cosine * y.cosine - z.sine * x.sine,
                        y.cosine * z.cosine - x.sine * y.sine,
                        z.cosine * x.cosine - y.sine * z.sine};
    const double length = norm(gradient);
    const double bandScale = (pattern.wallThickness / pattern.cellPitch) * kPi;
    const double distanceScale = pattern.cellPitch / (kTwoPi * 1.7);
    const double field = (bandScale * length - std::abs(value)) * distanceScale;
    if (!(length > 1e-14) || value == 0.0) return {field, {}};

    const double crossXY = -x.cosine * y.sine, crossXZ = -z.cosine * x.sine, crossYZ = -y.cosine * z.sine;
    const Vec3 hessianGradient{
        -(xy + zx) * gradient.x() + crossXY * gradient.y() + crossXZ * gradient.z(),
        crossXY * gradient.x() - (xy + yz) * gradient.y() + crossYZ * gradient.z(),
        crossXZ * gradient.x() + crossYZ * gradient.y() - (zx + yz) * gradient.z()
    };
    return {field, ((bandScale / length) * hessianGradient - std::copysign(1.0, value) * gradient) / 1.7};
}

double permittedScale(const Vec3 &delta, const Chart &chart, double movement) {
    return std::min({1.0, chartLimit / std::max(chart.deviation(delta), chartLimit),
                           movement / std::max(norm(delta), movement)});
}

Vec3 normalComponent(const Vec3 &delta, const Vec3 &gradient) {
    const double length2 = squaredNorm(gradient);
    return length2 > 1e-8 ? (dot(delta, gradient) / length2) * gradient : Vec3{};
}

Cubic straightControls(const Vec3 &start, const Vec3 &end) {
    const Vec3 third = (end - start) / 3.0;
    return {start, start + third, end - third, end};
}

constexpr int binomial[4][4] = {{1}, {1,1}, {1,2,1}, {1,3,3,1}};
constexpr int factorial[4] = {1,1,2,6};
constexpr int indices[10][3] = {
    {3,0,0},{0,3,0},{0,0,3},{2,1,0},{1,2,0},{0,2,1},{0,1,2},{1,0,2},{2,0,1},{1,1,1}
};
const std::vector<double> cubicKnots{0,0,0,0,1,1,1,1};

SurfacePtr triangleSpline(const std::array<Vec3, 10> &triangular) {
    // Espande il polinomio nei tre baricentri (1-u-v,u,v), poi
    // converte i coefficienti nella base di Bernstein tensoriale.
    Vec3 powers[4][4]{};
    const Vec3 origin = triangular[0];
    for (std::size_t n = 0; n < triangular.size(); ++n) {
        const int i = indices[n][0], j = indices[n][1], k = indices[n][2];
        const double multiplicity = 6.0 / (factorial[i] * factorial[j] * factorial[k]);
        for (int a = 0; a <= i; ++a)
            for (int b = 0; b <= i - a; ++b) {
                const int sign = (a+b)%2 ? -1 : 1;
                const double c = sign * multiplicity * binomial[i][a] * binomial[i-a][b];
                powers[j+a][k+b] += c * (triangular[n] - origin);
            }
    }
    std::vector<Vec3> poles;
    poles.reserve(16);
    for (int i = 0; i <= 3; ++i)
        for (int j = 0; j <= 3; ++j) {
            Vec3 pole = origin;
            for (int a = 0; a <= i; ++a)
                for (int b = 0; b <= j; ++b)
                    pole += (double(binomial[i][a] * binomial[j][b]) / (binomial[3][a] * binomial[3][b])) * powers[a][b];
            poles.push_back(pole);
        }
    return std::make_shared<BSplineSurface>(3, 3, cubicKnots, cubicKnots, 4, 4, std::move(poles));
}

}

std::size_t fitGyroidFaces(Body &body, const std::vector<FaceId> &candidates,
                           const SheetGyroid &pattern, double samplingPitch) {
    const double movement = std::min(.25 * samplingPitch, .10 * pattern.wallThickness);
    std::unordered_map<int, Chart> charts;
    charts.reserve(candidates.size());
    for (const auto face : candidates) {
        cancellationPoint();
        const auto &surface = body.face(face);
        if (surface.surface->type() != SurfaceType::Plane || surface.loops.size() != 1) continue;
        const auto fins = body.loopFins(surface.loops.front());
        if (fins.size() != 3 || std::any_of(fins.begin(), fins.end(), [&](FinId fin) {
                return body.edge(body.fin(fin).edge).curve->type() != CurveType::Line;
            })) continue;
        Chart chart;
        for (int i = 0; i < 3; ++i) {
            chart.fins[i] = fins[i];
            chart.points[i] = body.vertex(body.finStart(fins[i])).point;
        }
        const Vec3 u = chart.points[1] - chart.points[0], v = chart.points[2] - chart.points[0];
        const Vec3 crossUV = cross(u, v);
        const double norm2 = squaredNorm(crossUV);
        if (!(norm2 > 0)) continue;
        chart.dualU = cross(v, crossUV) / norm2;
        chart.dualV = cross(crossUV, u) / norm2;
        charts.emplace(face.index, std::move(chart));
    }
    std::unordered_map<int, Cubic> edges;
    edges.reserve(charts.size());
    for (const auto id : body.edges()) {
        cancellationPoint();
        auto &edge = body.edge(id);
        if (!edge.forward.valid() || !edge.backward.valid()) continue;
        const auto a = charts.find(body.finFace(edge.forward).index);
        const auto b = charts.find(body.finFace(edge.backward).index);
        if (a == charts.end() || b == charts.end() || a == b) continue;
        const Vec3 start = body.vertex(body.finStart(edge.forward)).point;
        const Vec3 end = body.vertex(body.finStart(edge.backward)).point;
        auto controls = straightControls(start, end);
        const Vec3 tangent = (end - start) / 3.0;
        const Vec3 d0 = -normalComponent(tangent, wallSample(pattern, start).gradient);
        const Vec3 d1 = normalComponent(tangent, wallSample(pattern, end).gradient);
        controls[1] += std::min(permittedScale(d0, a->second, movement), permittedScale(d0, b->second, movement)) * d0;
        controls[2] += std::min(permittedScale(d1, a->second, movement), permittedScale(d1, b->second, movement)) * d1;
        edges.emplace(id.index, controls);
        edge.curve = std::make_shared<BSplineCurve<3>>(3, cubicKnots, std::vector<Vec3>(controls.begin(), controls.end()));
        edge.range = {0,1};
    }
    const std::array<Vec2,3> uv{Vec2(0,0), Vec2(1,0), Vec2(0,1)};
    for (const auto face : candidates) {
        cancellationPoint();
        const auto found = charts.find(face.index);
        if (found == charts.end()) continue;
        const auto &chart = found->second;
        const auto &p = chart.points;
        std::array<Vec3,10> controls{p[0],p[1],p[2]};
        const Vec3 middle = (p[0] + p[1] + p[2]) / 3.0;
        Vec3 displacement;
        for (int i = 0; i < 3; ++i) {
            auto &fin = body.fin(chart.fins[i]);
            const auto &edge = body.edge(fin.edge);
            const int next = (i + 1) % 3;
            const auto linear = straightControls(p[i], p[next]);
            auto segment = linear;
            if (const auto cached = edges.find(fin.edge.index); cached != edges.end()) {
                segment = cached->second;
                if (!fin.sense) std::reverse(segment.begin(), segment.end());
            }
            controls[3+2*i] = segment[1];
            controls[4+2*i] = segment[2];
            displacement += segment[1] - linear[1] + segment[2] - linear[2];
            const Vec2 start = uv[fin.sense ? i : next], end = uv[fin.sense ? next : i];
            fin.pcurve = std::make_shared<BSplineCurve<2>>(1,
                std::vector<double>{edge.range.lo,edge.range.lo,edge.range.hi,edge.range.hi},
                std::vector<Vec2>{start,end});
            fin.pcurveTolerance = 0.0;
        }
        // Al baricentro i poli dei bordi valgono 1/9 e il polo
        // interno 2/9. Una correzione di Newton riduce il residuo.
        const auto sample = wallSample(pattern, middle + displacement / 9.0);
        const double gradient2 = squaredNorm(sample.gradient);
        const Vec3 adjustment = gradient2 > 1e-8 ? (-4.5 * sample.field / gradient2) * sample.gradient : Vec3{};
        controls[9] = middle + permittedScale(adjustment, chart, movement) * adjustment;
        // Limiti sui poli in coordinate baricentriche: ogni derivata
        // proiettata devia al massimo 6/24 dalla mappa affine. La
        // proiezione sul triangolo resta iniettiva.
        body.face(face).surface = triangleSpline(controls);
        body.face(face).sense = true;
    }
    return charts.size();
}
}
