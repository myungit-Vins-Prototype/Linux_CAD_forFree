#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_cancel.h"
#include "fk_curve_fit_bounds.h"
#include "fk_offset.h"
#include "fk_primitives.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace ForgeCad::Kernel;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Options {
  bool rational = false, trim = false, reverse = false, pose = false,
       reverseSpecs = false;
  double scale = 1;
  Interval range{0, 1};
};
Body source(const Options &o) {
  Frame3 frame;
  if (o.pose)
    frame = Frame3({3, -4, 2}, {1, 2, 3}, {2, -1, .4});
  Body body = makeBox(frame, o.scale, o.scale, o.scale);
  const auto id = body.edges().front();
  auto &edge = body.edge(id);
  const auto oldRange = edge.range;
  const Vec3 a = edge.curve->point(oldRange.lo),
             b = edge.curve->point(oldRange.hi);
  const double x = .1003, y = .1013, amp = .00001;
  std::vector<double> knots = {0, 0, 0, 0, x, x, x, y, y, y, 1, 1, 1, 1};
  for (auto &k : knots)
    k = o.range.lo + o.range.length() * k;
  const std::vector<double> q = {0,
                                 x / 3,
                                 2 * x / 3,
                                 x,
                                 x + (y - x) / 3 + amp,
                                 y - (y - x) / 3 + amp,
                                 y,
                                 y + (1 - y) / 3,
                                 y + 2 * (1 - y) / 3,
                                 1};
  std::vector<double> weights;
  if (o.rational) {
    weights.assign(q.size(), 1.);
    weights[4] = 1.2;
    weights[5] = .8;
  }
  std::vector<Vec3> poles;
  for (auto t : q)
    poles.push_back(a + t * (b - a));
  if (o.reverse) {
    std::reverse(poles.begin(), poles.end());
    std::reverse(weights.begin(), weights.end());
    std::reverse(knots.begin(), knots.end());
    for (auto &k : knots)
      k = o.range.lo + o.range.hi - k;
  }
  edge.curve = std::make_shared<BSplineCurve<3>>(3, knots, poles, weights);
  edge.range = o.range;
  for (auto finId : std::vector<FinId>{edge.forward, edge.backward}) {
    auto &fin = body.fin(finId);
    const auto old = fin.pcurve;
    const Vec2 uv0 = old->point(oldRange.lo), uv1 = old->point(oldRange.hi);
    std::vector<Vec2> uv;
    for (auto t : q)
      uv.push_back(uv0 + t * (uv1 - uv0));
    if (o.reverse)
      std::reverse(uv.begin(), uv.end());
    fin.pcurve = std::make_shared<BSplineCurve<2>>(3, knots, uv, weights);
    fin.pcurveTolerance = 0;
    if (o.trim)
      fin.pcurve =
          std::make_shared<TrimmedCurve<2>>(fin.pcurve, o.range.lo, o.range.hi);
    if (o.reverse)
      fin.sense = !fin.sense;
  }
  if (o.reverse)
    std::swap(edge.forward, edge.backward);
  if (o.trim)
    edge.curve =
        std::make_shared<TrimmedCurve<3>>(edge.curve, o.range.lo, o.range.hi);
  return body;
}
void hex(std::ostream &s, double x) {
  s << '"' << std::hexfloat << x << std::defaultfloat << '"';
}
template <int N> void vec(std::ostream &s, const Vec<N> &v) {
  s << '[';
  for (int i = 0; i < N; ++i) {
    if (i)
      s << ',';
    hex(s, v[i]);
  }
  s << ']';
}
void numbers(std::ostream &s, const std::vector<double> &a) {
  s << '[';
  bool first = true;
  for (double v : a) {
    if (!first)
      s << ',';
    first = false;
    hex(s, v);
  }
  s << ']';
}
template <int N> void curve(std::ostream &s, const CurvePtr<N> &c) {
  s << '{';
  if (c->type() == CurveType::Line) {
    auto &l = static_cast<const Line<N> &>(*c);
    s << "\"type\":\"line\",\"origin\":";
    vec(s, l.origin());
    s << ",\"direction\":";
    vec(s, l.direction());
  } else if (c->type() == CurveType::BSpline) {
    auto &b = static_cast<const BSplineCurve<N> &>(*c);
    s << "\"type\":\"bspline\",\"degree\":" << b.degree() << ",\"knots\":";
    numbers(s, b.knots());
    s << ",\"weights\":";
    numbers(s, b.weights());
    s << ",\"poles\":[";
    bool first = true;
    for (auto p : b.poles()) {
      if (!first)
        s << ',';
      first = false;
      vec(s, p);
    }
    s << ']';
  } else if (c->type() == CurveType::Trimmed) {
    auto &t = static_cast<const TrimmedCurve<N> &>(*c);
    s << "\"type\":\"trimmed\",\"range\":[";
    hex(s, t.domain().lo);
    s << ',';
    hex(s, t.domain().hi);
    s << "],\"basis\":";
    curve(s, t.basis());
  } else if constexpr (N == 3) {
    if (c->type() == CurveType::Transformed) {
      auto &t = static_cast<const TransformedCurve &>(*c);
      s << "\"type\":\"transformed\",\"matrix\":[";
      for (int i = 0; i < 3; ++i) {
        if (i)
          s << ',';
        s << '[';
        for (int j = 0; j < 3; ++j) {
          if (j)
            s << ',';
          hex(s, t.transform().matrix(i, j));
        }
        s << ']';
      }
      s << "],\"translation\":";
      vec(s, t.transform().translationPart());
      s << ",\"basis\":";
      curve(s, t.basis());
    } else
      throw std::runtime_error("unsupported exporter curve");
  } else
    throw std::runtime_error("unsupported exporter curve");
  s << '}';
}
void fins(std::ostream &s, const Body &body) {
  s << '[';
  bool first = true;
  for (auto id : body.fins()) {
    auto &fin = body.fin(id);
    auto &edge = body.edge(fin.edge);
    auto faceId = body.finFace(id);
    auto &face = body.face(faceId);
    require(face.surface->type() == SurfaceType::Plane,
            "exporter requires final Plane");
    auto &f = static_cast<const Plane &>(*face.surface).frame();
    if (!first)
      s << ',';
    first = false;
    s << "{\"fin\":" << id.index << ",\"edge\":" << fin.edge.index
      << ",\"face\":" << faceId.index
      << ",\"sense\":" << (fin.sense ? "true" : "false") << ",\"range\":[";
    hex(s, edge.range.lo);
    s << ',';
    hex(s, edge.range.hi);
    s << "],\"budget\":";
    hex(s, fin.pcurveTolerance);
    s << ",\"plane\":{\"origin\":";
    vec(s, f.origin());
    s << ",\"x\":";
    vec(s, f.xDir());
    s << ",\"y\":";
    vec(s, f.yDir());
    s << ",\"z\":";
    vec(s, f.zDir());
    s << "},\"curve\":";
    curve(s, edge.curve);
    s << ",\"pcurve\":";
    curve(s, fin.pcurve);
    s << '}';
  }
  s << ']';
}
void exportCase(const std::string &directory, const std::string &name,
                const Body &body, const OffsetResult &out, double tol) {
  if (directory.empty())
    return;
  std::ofstream s(directory + "/" + name + ".json");
  s << "{\"case\":\"" << name << "\",\"tolerance\":";
  hex(s, tol);
  s << ",\"source\":";
  fins(s, body);
  s << ",\"actual\":";
  fins(s, out.body);
  s << ",\"face_origins\":[";
  bool first = true;
  for (auto f : out.body.faces()) {
    if (!first)
      s << ',';
    first = false;
    s << "{\"actual\":" << f.index << ",\"source\":[";
    bool g = true;
    for (auto origin : out.history.origins(EntityKind::Face, f.index)) {
      if (!g)
        s << ',';
      g = false;
      s << origin.index;
    }
    s << "]}";
  }
  s << "]}\n";
  std::ofstream(directory + "/" + name + ".source.body", std::ios::binary)
      << writeBodyBinary(body);
  std::ofstream(directory + "/" + name + ".actual.body", std::ios::binary)
      << writeBodyBinary(out.body);
}
void publicCase(const Options &o, bool joined, const std::string &name,
                const std::string &directory) {
  Body body = source(o);
  const auto before = writeBodyBinary(body);
  require(checkBody(body).empty(), "invalid original topology");
  double sourceResidual = 0;
  for (auto id : body.fins()) {
    const auto &fin = body.fin(id);
    const auto &e = body.edge(fin.edge);
    const auto &face = body.face(body.finFace(id));
    for (int i = 0; i <= 2000; ++i) {
      double t = e.range.lo + e.range.length() * i / 2000.;
      auto uv = fin.pcurve->point(t);
      sourceResidual =
          std::max(sourceResidual, distance(face.surface->point(uv.x(), uv.y()),
                                            e.curve->point(t)));
    }
  }
  require(sourceResidual <= 2e-14 * std::max(1., o.scale),
          "source original pcurves violate exact construction");
  const double tol = 1e-7 * o.scale;
  OffsetResult out;
  if (joined) {
    std::vector<FaceOffset> specs;
    for (auto f : body.faces())
      specs.push_back({f, 0});
    if (o.reverseSpecs)
      std::reverse(specs.begin(), specs.end());
    out = offsetJoinedFaces(body, specs, tol);
  } else
    out = offsetFaces(body, body.faces(), .01 * o.scale, tol, true);
  require(writeBodyBinary(body) == before,
          "public operation modified original body");
  require(checkBody(out.body).empty(), "invalid emitted topology");
  exportCase(directory, name, body, out, tol);
  double worstRatio = 0, worst = 0, at = 0, allowed = 0;
  int badFin = -1;
  for (auto id : out.body.fins()) {
    const auto &fin = out.body.fin(id);
    const auto &e = out.body.edge(fin.edge);
    const auto &face = out.body.face(out.body.finFace(id));
    require(e.range.isFinite() && e.range.lo < e.range.hi,
            "invalid final edge range");
    for (int i = 0; i <= 20000; ++i) {
      const double t = e.range.lo + e.range.length() * i / 20000.;
      auto uv = fin.pcurve->point(t);
      double error =
          distance(face.surface->point(uv.x(), uv.y()), e.curve->point(t));
      if (error > worst) {
        worst = error;
        at = t;
        badFin = id.index;
        allowed = fin.pcurveTolerance;
      }
      worstRatio = std::max(
          worstRatio, error / std::max(fin.pcurveTolerance, 1e-15 * o.scale));
    }
  }
  std::cout << " source_residual=" << sourceResidual
            << " actual_residual=" << worst << " fin_budget=" << allowed
            << " at=" << at << " fin=" << badFin << " ratio=" << worstRatio;
  require(worstRatio <= 1.,
          "emitted fin S(PC(t))-E(t) exceeds its published 3D tolerance");
}
void failures() {
  Body b = source({});
  const auto before = writeBodyBinary(b);
  std::vector<FaceOffset> spec;
  for (auto f : b.faces())
    spec.push_back({f, 0});
  bool rejected = false;
  spec.front().distance = std::numeric_limits<double>::quiet_NaN();
  try {
    offsetJoinedFaces(b, spec, 1e-7);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected && writeBodyBinary(b) == before,
          "invalid data changed original body or were accepted");
  spec.front().distance = 0;
  auto token = std::make_shared<std::atomic<bool>>(true);
  CancellationScope scope(token);
  bool cancelled = false;
  try {
    offsetJoinedFaces(b, spec, 1e-7);
  } catch (const OperationCancelled &) {
    cancelled = true;
  }
  require(cancelled && writeBodyBinary(b) == before,
          "cancelled operation changed original body or published result");
}
#ifdef HAVE_PLANE_PCURVE_PROVIDER
void providerContracts() {
  auto plane = std::make_shared<Plane>(Frame3());
  auto edge = std::make_shared<BSplineCurve<3>>(
      3, std::vector<double>{-2, -2, -2, -2, 5, 5, 5, 5},
      std::vector<Vec3>{
          {0, 0, 5e-7}, {.3, .1, 5e-7}, {.7, .1, 5e-7}, {1, 0, 5e-7}});
  auto data = detail::planePCurveErrorBound(edge, plane, {-2, 5});
  require(bool(data.projection) && bool(data.residual),
          "recognized Plane provider missing");
  const std::array<Vec3, 4> correct = {Vec3(0, 0, 0), Vec3(.3, .1, 0),
                                       Vec3(.7, .1, 0), Vec3(1, 0, 0)};
  const double projection = data.projection({-2, 5}, correct),
               residual = data.residual({-2, 5}, correct);
  require(std::isfinite(projection) && projection < 1e-12,
          "exact projected candidate rejected");
  require(std::isfinite(residual) && residual >= 5e-7 && residual < 5.01e-7,
          "normal edge residual omitted from 3D fin bound");
  auto wrong = correct;
  wrong[1][1] += 1e-3;
  require(data.residual({-2, 5}, wrong) > 1e-5,
          "wrong UV pole accepted by residual bound");
  std::weak_ptr<const Curve<3>> weak = edge;
  std::weak_ptr<const Surface> weakPlane = plane;
  edge.reset();
  plane.reset();
  require(weak.expired() && weakPlane.expired(),
          "provider retained input geometry");
  require(data.residual({-2, 5}, correct) >= 5e-7,
          "provider lost owned source coefficients");
  auto singular = std::make_shared<Plane>(
      Frame3::fromAxes({}, {1, 0, 0}, {2, 0, 0}, {0, 0, 1}));
  bool rejected = false;
  try {
    detail::planePCurveErrorBound(
        std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 0)), singular, {0, 1});
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected,
          "recognized singular Plane silently unsupported or accepted");

  // Deliberately corrupt an otherwise valid local curve after construction to
  // exercise factory validation, rather than the BSpline constructor guard.
  for (int kind = 0; kind < 3; ++kind) {
    auto corrupt = std::make_shared<BSplineCurve<3>>(
        1, std::vector<double>{0, 0, 1, 1},
        std::vector<Vec3>{{0, 0, 0}, {1, 0, 0}}, std::vector<double>{1, 1});
    if (kind < 2)
      const_cast<std::vector<double> &>(corrupt->weights())[1] =
          kind == 0 ? 0 : std::numeric_limits<double>::quiet_NaN();
    else
      const_cast<std::vector<Vec3> &>(corrupt->poles())[1][0] =
          std::numeric_limits<double>::infinity();
    bool invalidRejected = false;
    try {
      detail::planePCurveErrorBound(corrupt, std::make_shared<Plane>(Frame3()),
                                    {0, 1});
    } catch (const std::exception &) {
      invalidRejected = true;
    }
    require(invalidRejected,
            "recognized invalid source silently unsupported or accepted");
  }
  struct Unsupported final : Curve<3> {
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return {0, 1}; }
    void evaluate(double t, int order, Vec3 *out) const override {
      out[0] = {t, t * t, 0};
      for (int i = 1; i <= order; ++i)
        out[i] = {};
    }
  };
  auto unsupported =
      detail::planePCurveErrorBound(std::make_shared<Unsupported>(),
                                    std::make_shared<Plane>(Frame3()), {0, 1});
  require(!unsupported.projection && !unsupported.residual,
          "unsupported source was partially certified");
  // A nontrivial source wrapper and nested trim must retain the original
  // parameter and translation, rather than fitting rounded local controls.
  auto basis = std::make_shared<BSplineCurve<3>>(
      3, std::vector<double>{-2, -2, -2, -2, 5, 5, 5, 5},
      std::vector<Vec3>{{0, 0, 0}, {.3, .1, 0}, {.7, .1, 0}, {1, 0, 0}});
  auto wrapped = std::make_shared<TrimmedCurve<3>>(
      std::make_shared<TransformedCurve>(basis,
                                         Transform3::translation({4, -8, 2})),
      -2, 5);
  auto wrappedPlane =
      std::make_shared<Plane>(Frame3({4, -8, 2}, {0, 0, 1}, {1, 0, 0}));
  auto wrappedData =
      detail::planePCurveErrorBound(wrapped, wrappedPlane, {-2, 5});
  require(bool(wrappedData.residual) &&
              wrappedData.residual({-2, 5}, correct) < 1e-12,
          "wrapped source changed source parameter or plane origin");
  auto token = std::make_shared<std::atomic<bool>>(true);
  CancellationScope scope(token);
  bool cancelled = false;
  try {
    data.residual({-2, 5}, correct);
  } catch (const OperationCancelled &) {
    cancelled = true;
  }
  require(cancelled, "provider query ignored cancellation");
}
#endif
} // namespace
int main(int argc, char **argv) {
  std::string directory = argc > 1 ? argv[1] : "";
  int passed = 0, failed = 0;
  std::cout << std::setprecision(17);
  auto run = [&](std::string name, auto f) {
    std::cout << name << std::flush;
    try {
      f();
      ++passed;
      std::cout << " PASS\n";
    } catch (const std::exception &e) {
      ++failed;
      std::cout << " FAIL " << e.what() << '\n';
    } catch (const OperationCancelled &) {
      ++failed;
      std::cout << " FAIL unexpected cancellation\n";
    }
  };
  std::vector<std::pair<std::string, Options>> cases = {
      {"joined_polynomial", {}},
      {"joined_rational", {true}},
      {"joined_shift_trim", {false, true, false, false, false, 1, {-3, 5}}},
      {"joined_reverse", {false, false, true, false, true}},
      {"joined_pose", {false, false, false, true}},
      {"joined_scale", {false, false, false, false, false, 1000}}};
  for (auto entry : cases)
    run(entry.first,
        [&] { publicCase(entry.second, true, entry.first, directory); });
  run("faces_trim_polynomial",
      [&] { publicCase({}, false, "faces_trim_polynomial", directory); });
  run("faces_trim_rational", [&] {
    Options o;
    o.rational = true;
    publicCase(o, false, "faces_trim_rational", directory);
  });
  run("public_atomic_invalid_cancel", [] { failures(); });
#ifdef HAVE_PLANE_PCURVE_PROVIDER
  run("provider_original_plane_residual_lifetime_rejection_cancel",
      [] { providerContracts(); });
#endif
  std::cout << passed << " passed " << failed << " failed\n";
  return failed ? 1 : 0;
}
