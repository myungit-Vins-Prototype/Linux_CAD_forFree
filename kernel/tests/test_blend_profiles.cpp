#include "fk_blend.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_curve_ops.h"
#include "fk_extrude.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_profile.h"
#include "fk_test.h"
#include <algorithm>
#include <limits>
using namespace ForgeCad::Kernel;
namespace {
const Surface &patch(const Body &body) {
  for (FaceId f : body.faces())
    if (body.face(f).surface->type() == SurfaceType::BSpline)
      return *body.face(f).surface;
  throw std::runtime_error("faccia del raccordo assente");
}
double middleRadius(const Surface &s) {
  Vec3 d[9];
  s.evaluate(.5 * (s.uDomain().lo + s.uDomain().hi),
             .5 * (s.vDomain().lo + s.vDomain().hi), 2, d);
  const auto v = d[Surface::derivativeIndex(0, 1, 2)],
             vv = d[Surface::derivativeIndex(0, 2, 2)];
  return std::pow(norm(v), 3) / norm(cross(v, vv));
}
} // namespace
FK_TEST(BlendProfilesConicFamilies) {
  const Body base = makeBox(Frame3(), 20, 16, 30);
  const auto e = nearestEdge(base, {0, 0, 15}, 1e-8);
  double previous = 0;
  for (double rho : {.4, .5, .65}) {
    BlendProfileSpec spec;
    spec.rho = rho;
    const Body result = blendProfileEdges(base, {e}, 3, spec, false);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK(result.faces().size() == 7);
    FK_CHECK_NEAR(middleRadius(patch(result)),
                  3 * std::sqrt(.5) * (1 - rho) / rho, 1e-7);
    const double volume = massProperties(result).volume;
    FK_CHECK(volume > previous && volume < 20 * 16 * 30);
    previous = volume;
    const auto reopened = readBodyBinary(writeBodyBinary(result));
    FK_CHECK(checkBody(reopened, {true, true}).empty());
    FK_CHECK_NEAR(massProperties(reopened).volume, volume, 1e-6);
  }
  for (int family = 0; family < 3; ++family) {
    BlendProfileSpec spec;
    spec.rho = .4;
    spec.conicFamily = family;
    spec.conicRadius = family == 0 ? 3 : family == 1 ? 3 * std::sqrt(.5) : 1;
    auto result = blendProfileEdges(base, {e}, 3, spec, false);
    FK_CHECK(checkBody(result, {true, true}).empty());
    FK_CHECK_NEAR(middleRadius(patch(result)), spec.conicRadius, 1e-7);
  }
}
FK_TEST(BlendProfilesVariableArcLength) {
  Body base = makeBox(Frame3(), 20, 16, 30);
  const auto e = nearestEdge(base, {0, 0, 15}, 1e-8);
  auto &edge = base.edge(e);
  const Vec3 p0 = edge.curve->point(edge.range.lo),
             p1 = edge.curve->point(edge.range.hi);
  edge.curve = std::make_shared<BSplineCurve<3>>(
      2, std::vector<double>{0, 0, 0, 1, 1, 1},
      std::vector<Vec3>{p0, p0 + .1 * (p1 - p0), p1});
  edge.range = {0, 1};
  base.fin(edge.forward).pcurve.reset();
  base.fin(edge.backward).pcurve.reset();
  for (bool smooth : {false, true})
    for (bool reverse : {false, true}) {
      BlendProfileSpec spec;
      spec.radii = {2, 3, 4, 3.5, 2.5};
      spec.smooth = smooth;
      spec.reverse = reverse;
      const Body result = blendProfileEdges(base, {e}, 4, spec, false);
      FK_CHECK(checkBody(result, {true, true}).empty());
      const Surface &surface = patch(result);
      for (int i = 0; i < 5; ++i) {
        const double station = i / 4.;
        const double t = (-.2 + std::sqrt(.04 + 3.2 * station)) / 1.6;
        const Vec3 contact = surface.point(t, surface.vDomain().lo);
        FK_CHECK_NEAR(contact.x() + contact.y(),
                      spec.radii[reverse ? 4 - i : i], 2e-7);
        FK_CHECK_NEAR(contact.z(), (p0 + station * (p1 - p0)).z(), 2e-7);
      }
      FK_CHECK_NEAR(
          massProperties(readBodyBinary(writeBodyBinary(result))).volume,
          massProperties(result).volume, 1e-6);
    }
}
FK_TEST(BlendProfilesValidationAndNoPropagation) {
  const Body base = makeBox(Frame3(), 20, 16, 30);
  auto e = nearestEdge(base, {0, 0, 15}, 1e-8);
  const Body circular = blendEdges(base, {e}, 3, false, nullptr, false);
  FK_CHECK(checkBody(circular, {true, true}).empty());
  BlendProfileSpec spec;
  spec.rho = 1;
  FK_CHECK_THROWS(blendProfileEdges(base, {e}, 3, spec, false));
  spec.rho = .4;
  spec.radii = {2, -1, 2};
  FK_CHECK_THROWS(blendProfileEdges(base, {e}, 3, spec, false));
  spec.radii = {2, 3};
  spec.paths = {{{e}, {2, 3}, true, false}};
  FK_CHECK_THROWS(
      blendProfileEdges(base, {e, base.edges().back()}, 3, spec, false));
  spec.paths.clear();
  spec.radii.clear();
  spec.conicRadius = 3;
  spec.conicFamily = 2;
  FK_CHECK_THROWS(blendProfileEdges(base, {e}, 3, spec, false));
  FK_CHECK(checkBody(base, {true, true}).empty());
}

FK_TEST(BlendProfilesTangentPropagationToggle) {
  std::vector<ProfileSegment> profile;
  const auto line = [&](Vec2 a, Vec2 b) {
    profile.push_back(
        {std::make_shared<Line<2>>(a, b - a), {0, distance(a, b)}});
  };
  const auto arc = [&](Vec2 center, double from, double to) {
    profile.push_back(
        {std::make_shared<Circle<2>>(makeCircle(center, 1.5)), {from, to}});
  };
  line({1.5, 0}, {8.5, 0});
  arc({8.5, 1.5}, -kHalfPi, 0);
  line({10, 1.5}, {10, 4.5});
  arc({8.5, 4.5}, 0, kHalfPi);
  line({8.5, 6}, {1.5, 6});
  arc({1.5, 4.5}, kHalfPi, kPi);
  line({0, 4.5}, {0, 1.5});
  arc({1.5, 1.5}, kPi, 1.5 * kPi);
  const Body base =
      makeExtrusion(Frame3(), buildProfile(profile, 1e-9).regions.front(), 3);
  const auto original = writeBodyBinary(base);
  const auto seed = nearestEdge(base, {5, 0, 3}, 1e-8);
  for (bool propagate : {false, true}) {
    const auto result = blendEdges(base, {seed}, .5, false, nullptr, propagate);
    FK_CHECK(checkBody(result, {true, true}).empty());
    // Il bordo opposto non selezionato deve restare intatto soltanto
    // quando la propagazione della catena tangente e' disattivata.
    FK_CHECK(nearestEdge(result, {10, 3, 3}, 1e-7).valid() == !propagate);
    FK_CHECK(writeBodyBinary(base) == original);
  }
  std::vector<EdgeId> top;
  for (auto e : base.edges())
    if (std::fabs(base.vertex(base.edgeStart(e)).point.z() - 3) < 1e-8 &&
        std::fabs(base.vertex(base.edgeEnd(e)).point.z() - 3) < 1e-8)
      top.push_back(e);
  const auto paths = blendPaths(base, top);
  FK_CHECK(top.size() == 8 && paths.size() == 1 && paths.front().closed);
  FK_CHECK_NEAR(paths.front().length, 20.0 + 3.0 * kPi, 1e-8);
  BlendProfileSpec variable;
  variable.radii = {.5, .7, .5, .6, .5};
  const auto varied = blendProfileEdges(base, top, .7, variable, false);
  FK_CHECK(checkBody(varied, {true, true}).empty());
  FK_CHECK(varied.faces().size() == 18);
}

FK_TEST(BlendProfilesTangentPathStations) {
  Body base = makeBox(Frame3(), 20, 16, 30);
  const auto first = nearestEdge(base, {0, 0, 15}, 1e-8);
  const auto original = base.edge(first);
  const double split = original.range.lo + .4 * original.range.length();
  const auto second =
      base.semv(first, original.curve->point(split), split).edge;
  const auto paths = blendPaths(base, {second, first});
  FK_CHECK(paths.size() == 1 && paths.front().edges.size() == 2 &&
           !paths.front().closed);
  FK_CHECK_NEAR(paths.front().length, 30, 1e-9);
  BlendProfileSpec spec;
  spec.radii = {1, 2, 3, 2, 1};
  const auto result = blendProfileEdges(base, {second, first}, 3, spec, false);
  FK_CHECK(checkBody(result, {true, true}).empty());
  for (int i = 0; i < 5; ++i) {
    const auto station = blendPathPoint(base, paths.front(), i / 4.);
    bool found = false;
    for (FaceId f : result.faces()) {
      const auto &surface = *result.face(f).surface;
      if (surface.type() != SurfaceType::BSpline ||
          station.parameter < surface.uDomain().lo - 1e-8 ||
          station.parameter > surface.uDomain().hi + 1e-8)
        continue;
      const auto contact =
          surface.point(station.parameter, surface.vDomain().lo);
      if (std::fabs(contact.z() - station.point.z()) < 1e-7) {
        FK_CHECK_NEAR(norm(contact - station.point), spec.radii[std::size_t(i)],
                      2e-7);
        found = true;
      }
    }
    FK_CHECK(found);
  }
}
FK_TEST(BlendProfilesIndependentPaths) {
  const auto base = makeBox(Frame3(), 20, 16, 30);
  const auto a = nearestEdge(base, {0, 0, 15}, 1e-8),
             b = nearestEdge(base, {20, 16, 15}, 1e-8);
  const auto paths = blendPaths(base, {a, b});
  FK_CHECK(paths.size() == 2);
  BlendProfileSpec spec;
  spec.paths = {{{a}, {1, 2, 1}, true, false}, {{b}, {3, 4, 3}, true, false}};
  const auto result = blendProfileEdges(base, {a, b}, 4, spec, false);
  FK_CHECK(checkBody(result, {true, true}).empty());
  for (int i = 0; i < 2; ++i) {
    const auto station = blendPathPoint(base, paths[std::size_t(i)], .5);
    double nearest = 1e100;
    for (FaceId f : result.faces()) {
      const auto &surface = *result.face(f).surface;
      if (surface.type() != SurfaceType::BSpline)
        continue;
      nearest = std::min(
          nearest, norm(surface.point(station.parameter, surface.vDomain().lo) -
                        station.point));
    }
    FK_CHECK_NEAR(nearest, i == 0 ? 2 : 4, 2e-7);
  }
  const auto restored = readBodyBinary(writeBodyBinary(result));
  FK_CHECK(checkBody(restored, {true, true}).empty());
  FK_CHECK_NEAR(massProperties(restored).volume, massProperties(result).volume,
                1e-6);
}
