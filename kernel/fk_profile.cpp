#include "fk_profile.h"

#include <algorithm>
#include <numeric>

#include "fk_curve_ops.h"
#include "fk_quadrature.h"

namespace ForgeCad::Kernel {
namespace {

// Integrale di f(t) sul tratto, spezzato ai breakpoint della curva.
template <class F>
double integrateSegment(const ProfileSegment &segment, const F &f, double tolerance) {
    const std::vector<double> breaks = segment.curve->breakpoints(segment.range);
    double sum = 0.0;
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
        if (breaks[i + 1] > breaks[i]) sum += detail::integrate(f, breaks[i], breaks[i + 1], tolerance);
    return sum;
}

}

ProfileSegment reversed(const ProfileSegment &segment) {
    return {reversedCurve<2>(segment.curve), {-segment.range.hi, -segment.range.lo}};
}

ProfileLoop reversed(const ProfileLoop &loop) {
    ProfileLoop result;
    for (auto it = loop.segments.rbegin(); it != loop.segments.rend(); ++it) result.segments.push_back(reversed(*it));
    return result;
}

double signedArea(const ProfileLoop &loop) {
    double twice = 0.0;
    for (const ProfileSegment &segment : loop.segments) {
        const Curve<2> &curve = *segment.curve;
        twice += integrateSegment(segment, [&curve](double t) {
            Vec2 d[2];
            curve.evaluate(t, 1, d);
            return cross(d[0], d[1]);
        }, 1.0e-13);
    }
    return 0.5 * twice;
}

double area(const ProfileRegion &region) {
    double result = signedArea(region.outer);
    for (const ProfileLoop &hole : region.holes) result += signedArea(hole);  // negative
    return result;
}

int windingNumber(const ProfileLoop &loop, const Vec2 &p) {
    double angle = 0.0;
    for (const ProfileSegment &segment : loop.segments) {
        const Curve<2> &curve = *segment.curve;
        angle += integrateSegment(segment, [&curve, &p](double t) {
            Vec2 d[2];
            curve.evaluate(t, 1, d);
            const Vec2 r = d[0] - p;
            return cross(r, d[1]) / squaredNorm(r);
        }, 1.0e-6);
    }
    return int(std::lround(angle / kTwoPi));
}

Profile buildProfile(const std::vector<ProfileSegment> &segments, double tolerance) {
    Profile profile;
    std::vector<ProfileLoop> loops;
    std::vector<bool> used(segments.size(), false);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        ProfileLoop chain{{segments[i]}};
        const Vec2 chainStart = segments[i].start();
        Vec2 current = segments[i].end();
        bool closed = distance(current, chainStart) <= tolerance;
        while (!closed) {
            bool extended = false;
            for (std::size_t j = 0; j < segments.size() && !extended; ++j) {
                if (used[j]) continue;
                if (distance(segments[j].start(), current) <= tolerance) {
                    chain.segments.push_back(segments[j]);
                } else if (distance(segments[j].end(), current) <= tolerance) {
                    chain.segments.push_back(reversed(segments[j]));
                } else {
                    continue;
                }
                used[j] = extended = true;
                current = chain.segments.back().end();
            }
            if (!extended) break;
            closed = distance(current, chainStart) <= tolerance;
        }
        if (closed) loops.push_back(std::move(chain));
        else ++profile.openChains;
    }

    // Annidamento: dal loop piu' grande al piu' piccolo; il genitore e' il
    // contenitore piu' piccolo.
    std::vector<double> areas;
    for (const ProfileLoop &loop : loops) areas.push_back(signedArea(loop));
    std::vector<std::size_t> order(loops.size());
    std::iota(order.begin(), order.end(), 0);
    order.erase(std::remove_if(order.begin(), order.end(),
                               [&](std::size_t i) { return std::fabs(areas[i]) <= tolerance * tolerance; }),
                order.end());
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return std::fabs(areas[a]) > std::fabs(areas[b]); });
    std::vector<int> depth(loops.size(), 0), parent(loops.size(), -1);
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::size_t inner = order[k];
        const Vec2 probe = loops[inner].segments.front().start();
        for (std::size_t m = 0; m < k; ++m) {
            const std::size_t outer = order[m];
            if (windingNumber(loops[outer], probe) != 0) {
                ++depth[inner];
                parent[inner] = int(outer);
            }
        }
    }
    auto oriented = [&](std::size_t i, bool counterClockwise) {
        return (areas[i] > 0.0) == counterClockwise ? loops[i] : reversed(loops[i]);
    };
    for (std::size_t outer : order) {
        if (depth[outer] % 2 != 0) continue;
        ProfileRegion region;
        region.outer = oriented(outer, true);
        for (std::size_t hole : order)
            if (parent[hole] == int(outer) && depth[hole] == depth[outer] + 1) region.holes.push_back(oriented(hole, false));
        profile.regions.push_back(std::move(region));
    }
    return profile;
}

}
