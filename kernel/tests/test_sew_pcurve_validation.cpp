#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_cancel.h"
#include "fk_curve_fit_bounds.h"
#include "fk_history.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include <cmath>
#include "fk_pcurve.h"
#include "fk_sew.h"
#include "fk_surface.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace ForgeCad::Kernel;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

// Reverse the exact fixture's normals and boundary traversal together.
Body boxSheet(Vec3 origin, double side, bool invert) {
  const Body box = makeBox(Frame3(origin, {0,0,1}, {1,0,0}),side,side,side);
  if (!invert) return facesAsSheet(box,box.faces());
  std::vector<Vec3> points;
  std::vector<Body::BuildEdge> edges;
  std::vector<Body::BuildFace> faces;
  for (auto v:box.vertices()) points.push_back(box.vertex(v).point);
  for (auto e:box.edges()) {
    const auto &edge=box.edge(e);
    edges.push_back({box.edgeStart(e).index,box.edgeEnd(e).index,edge.curve,edge.range,edge.tolerance});
  }
  for (auto f:box.faces()) {
    const auto &face=box.face(f);
    auto &copy=faces.emplace_back(Body::BuildFace{face.surface,!face.sense,{}});
    for (auto l:face.loops) {
      auto &loop=copy.loops.emplace_back();
      for (auto u:box.loopFins(l)) {
        const auto &fin=box.fin(u);
        loop.push_back({fin.edge.index,!fin.sense,fin.pcurve,fin.pcurveTolerance});
      }
      std::reverse(loop.begin(),loop.end());
    }
  }
  return Body::buildSheet(points,edges,faces);
}
void multishellCase(int depth, unsigned mask, const std::vector<int> &order) {
  std::vector<Body> sheets;
  sheets.push_back(boxSheet({0,0,0},2,mask&1));
  sheets.push_back(boxSheet(depth ? Vec3{.5,.5,.5}:Vec3{10,0,0},1,mask&2));
  if (depth==2) sheets.push_back(boxSheet({.75,.75,.75},.5,mask&4));
  std::vector<const Body*> operands;
  for (int i:order) operands.push_back(&sheets.at(i));
  BodyHistory history;
  const auto r=sewSheets(operands,1e-6,true,&history);
  require(r.closed && r.solid && r.freeEdges==0,"closed shells did not produce a solid");
  require(r.shells==int(sheets.size()) && r.body.shells().size()==sheets.size(),"shell lost by sewing");
  require(checkBody(r.body,{true,true}).empty(),"invalid sewn topology or geometry");
  const double expected=depth==0?9:depth==1?7:7.125;
  const double volume=massProperties(r.body).volume;
  if (std::abs(volume-expected)>1e-8)
    throw std::runtime_error("volume="+std::to_string(volume)+" expected="+std::to_string(expected));
  const SolidClassifier classifier(r.body,linearResolution());
  require(classifier.classify({.25,.25,.25})==PointLocation::Inside,"outer material outside");
  require(classifier.classify({20,20,20})==PointLocation::Outside,"exterior inside");
  require(classifier.classify(depth?Vec3{.6,.6,.6}:Vec3{10.5,.5,.5})==
          (depth?PointLocation::Outside:PointLocation::Inside),"cavity or disjoint component misclassified");
  if (depth==2) require(classifier.classify({1,1,1})==PointLocation::Inside,"depth-two material island outside");
  require(history.operandCount()==int(operands.size()),"genealogy lost operand");
  for (auto f:r.body.faces()) {
    require(history.isDeclared(EntityKind::Face,f.index),"face genealogy not declared");
    const auto &origins=history.origins(EntityKind::Face,f.index);
    require(origins.size()==1,"face origin not unique");
    const auto &o=origins.front();
    require(o.kind==EntityKind::Face && o.operand>=0 && o.operand<int(operands.size()) &&
            operands[o.operand]->contains(FaceId(o.index)),"face origin invalid");
    require(r.body.face(f).surface==operands[o.operand]->face(FaceId(o.index)).surface,
            "face origin reassociated to another surface");
  }
  for (std::size_t i=0;i<operands.size();++i)
    for (auto f:operands[i]->faces())
      require(history.descendants(EntityKind::Face,int(i),f.index).size()==1,"source face lost or duplicated");
}


void crossingPrismsRejected() {
  const Body a=makeBox(Frame3({-2,-.5,-.5},{0,0,1},{1,0,0}),4,1,1);
  const Body b=makeBox(Frame3({-.5,-2,-.25},{0,0,1},{1,0,0}),1,4,.5);
  // Every vertex is outside the other prism, but their interiors overlap.
  for (const auto &pair:std::vector<std::pair<const Body*,const Body*>>{{&a,&b},{&b,&a}}) {
    const SolidClassifier classifier(*pair.second,linearResolution());
    for (auto v:pair.first->vertices())
      require(classifier.classify(pair.first->vertex(v).point)==PointLocation::Outside,
              "crossing fixture has a vertex in the other prism");
  }
  require(SolidClassifier(a,linearResolution()).classify({0,0,0})==PointLocation::Inside &&
          SolidClassifier(b,linearResolution()).classify({0,0,0})==PointLocation::Inside,
          "crossing fixture interiors do not overlap");
  for (bool reverse:{false,true}) {
    BodyHistory history=makeHistory({&a},a); // Failure must clear existing output.
    bool rejected=false;
    try { sewSheets(reverse?std::vector<const Body*>{&b,&a}:std::vector<const Body*>{&a,&b},1e-6,true,&history); }
    catch (const std::domain_error &) {rejected=true;}
    require(rejected,"crossing closed shells accepted as disjoint or nested solids");
    require(history.operandCount()==0 && history.resultSize(EntityKind::Face)==0 &&
            history.resultSize(EntityKind::Edge)==0 && history.resultSize(EntityKind::Vertex)==0,
            "rejected crossing published partial genealogy");
  }
}
void concentricSphereCavity(unsigned mask, bool reverse) {
  const Vec3 center{3,-2,4};
  const Frame3 frame(center,{0,0,1},{1,0,0});
  Body outer=makeSphere(frame,2), inner=makeSphere(frame,1);
  require(outer.faces().size()==1 && inner.faces().size()==1 &&
          outer.edges().empty() && inner.edges().empty(),"sphere fixture is not a closed edge-free face");
  const auto outerFace=outer.faces().front(), innerFace=inner.faces().front();
  const bool outward=outer.face(outerFace).sense;
  if (mask&1) outer.face(outerFace).sense=!outer.face(outerFace).sense;
  if (mask&2) inner.face(innerFace).sense=!inner.face(innerFace).sense;
  BodyHistory history;
  const auto result=sewSheets(reverse?std::vector<const Body*>{&inner,&outer}:
                                     std::vector<const Body*>{&outer,&inner},1e-6,true,&history);
  require(result.closed && result.solid && result.freeEdges==0 && result.shells==2,
          "closed edge-free sphere shells lost during sewing");
  require(checkBody(result.body,{true,true}).empty(),"sphere cavity invalid");
  const auto mass=massProperties(result.body);
  require(std::abs(mass.volume-4*kPi*7/3)<1e-8,"sphere cavity volume incorrect");
  require(distance(mass.centroid,center)<1e-9,"sphere cavity centroid incorrect");
  const SolidClassifier classifier(result.body,linearResolution());
  require(classifier.classify(center)==PointLocation::Outside,"sphere cavity center is material");
  require(classifier.classify(center+Vec3{1.5,0,0})==PointLocation::Inside,"sphere wall is outside");
  require(classifier.classify(center+Vec3{3,0,0})==PointLocation::Outside,"sphere exterior is material");
  for (auto f:result.body.faces()) {
    const auto &face=result.body.face(f);
    const bool isOuter=face.surface==outer.face(outerFace).surface;
    require(face.surface==(isOuter?outer.face(outerFace).surface:inner.face(innerFace).surface),
            "sphere sewing changed support surface");
    require(face.sense==(isOuter?outward:!outward),"sphere cavity normal orientation incorrect");
    const auto &origins=history.origins(EntityKind::Face,f.index);
    require(history.isDeclared(EntityKind::Face,f.index) && origins.size()==1 &&
            origins.front().operand==(isOuter?(reverse?1:0):(reverse?0:1)),
            "sphere cavity face genealogy incorrect");
  }
}


void obliqueSpherePair(double separation, bool touching) {
  const Vec3 direction=normalized(Vec3{1,2,3});
  const Vec3 innerCenter=separation*direction;
  const Body outer=makeSphere(Frame3(),2);
  const Body inner=makeSphere(Frame3(innerCenter,{0,0,1},{1,0,0}),1);
  for (bool reverse:{false,true}) {
    const std::vector<const Body*> operands=reverse?
        std::vector<const Body*>{&inner,&outer}:std::vector<const Body*>{&outer,&inner};
    BodyHistory history=makeHistory({&outer},outer);
    if (touching) {
      bool rejected=false;
      try {sewSheets(operands,1e-6,true,&history);}
      catch (const std::domain_error &) {rejected=true;}
      require(rejected,"oblique point-tangent sphere shells accepted");
      require(history.operandCount()==0 && history.resultSize(EntityKind::Face)==0 &&
              history.resultSize(EntityKind::Edge)==0 && history.resultSize(EntityKind::Vertex)==0,
              "point-tangent sphere rejection published genealogy");
      continue;
    }
    const auto result=sewSheets(operands,1e-6,true,&history);
    require(result.closed && result.solid && result.shells==2 && result.freeEdges==0,
            "separated or strictly nested spheres rejected");
    require(checkBody(result.body,{true,true}).empty(),"oblique sphere pair invalid");
    const bool nested=separation<1;
    const auto mass=massProperties(result.body);
    require(std::abs(mass.volume-4*kPi*(nested?7:9)/3)<1e-8,
            "oblique valid sphere volume incorrect");
    const Vec3 centroid=(nested?-1.0/7:1.0/9)*innerCenter;
    require(distance(mass.centroid,centroid)<1e-9,"oblique sphere centroid incorrect");
    const SolidClassifier classifier(result.body,linearResolution());
    require(classifier.classify(innerCenter)==(nested?PointLocation::Outside:PointLocation::Inside),
            "oblique sphere center classification incorrect");
    require(classifier.classify(-1.5*direction)==PointLocation::Inside,
            "oblique outer sphere material classified outside");
  }
}


void disjointShellsWithNestedBounds() {
  // Same U-shaped domain as the exchange robustness fixture, built directly
  // as a prism so this sewing regression does not depend on a Boolean.
  const Body concave=makePrism(Frame3(),
      {{0,0},{4,0},{4,4},{3,4},{3,1},{1,1},{1,4},{0,4}}, {},4);
  const Body cube=makeBox(Frame3({1.5,2,1},{0,0,1},{1,0,0}),1,1,1);
  require(std::abs(massProperties(concave).volume-40)<1e-9,"U-prism fixture volume incorrect");
  for (auto v:cube.vertices())
    for (int axis=0;axis<3;++axis)
      require(cube.vertex(v).point[axis]>0 && cube.vertex(v).point[axis]<4,
              "cube bounds are not strictly inside U-prism bounds");
  for (bool reverse:{false,true}) {
    const auto result=sewSheets(reverse?std::vector<const Body*>{&cube,&concave}:
                                       std::vector<const Body*>{&concave,&cube},1e-6,true);
    require(result.closed && result.solid && result.shells==2 && result.freeEdges==0,
            "disjoint shells with nested bounds did not produce two closed shells");
    require(checkBody(result.body,{true,true}).empty(),"nested-bounds sewing invalid");
    require(std::abs(massProperties(result.body).volume-41)<1e-9,
            "nested AABB cube mistaken for a cavity");
    const SolidClassifier classifier(result.body,linearResolution());
    require(classifier.classify({2,2.5,1.5})==PointLocation::Inside,
            "cube in concave exterior notch classified as a void");
    require(classifier.classify({2,3.5,1.5})==PointLocation::Outside,
            "free notch space classified as material");
    require(classifier.classify({.5,2,2})==PointLocation::Inside,
            "U-prism material classified outside");
  }
}

struct Options {
  bool affine = false, reverseInputs = false, reverseTarget = false,
       trim = false;
  double scale = 1;
  Interval targetRange{0, 1};
  bool rational = false;
};
Body sourceSheet(bool upper, const Options &options) {
  const double scale = options.scale, y = upper ? 0 : -scale;
  const std::vector<Vec3> vertices = {
      {0, y, 0}, {scale, y, 0}, {scale, y + scale, 0}, {0, y + scale, 0}};
  std::vector<Body::BuildEdge> edges;
  for (int i = 0; i < 4; ++i) {
    int a = i, b = (i + 1) % 4;
    if (!upper && i == 2)
      std::swap(a, b);
    edges.push_back(
        {a,
         b,
         std::make_shared<Line<3>>(vertices[a], vertices[b] - vertices[a]),
         {0, scale},
         0});
  }
  Body::BuildFace face;
  face.surface = std::make_shared<Plane>(Frame3());
  face.loops = {{{0, true, nullptr, 0},
                 {1, true, nullptr, 0},
                 {2, upper, nullptr, 0},
                 {3, true, nullptr, 0}}};
  if (upper) {
    const int degree = options.rational ? 4 : 128;
    // A positive, stable Bernstein lobe: peak1.12e-7 at3/128,
    // maximum at the old64-point grid9.7273e-8. The rest is t or t².
    constexpr double hiddenPole = 4.940214000122462e-7;
    std::vector<Vec3> poles;
    std::vector<Vec2> uv;
    std::vector<double> weights;
    std::vector<double> knots(degree + 1, options.targetRange.lo);
    knots.insert(knots.end(), degree + 1, options.targetRange.hi);
    for (int i = 0; i <= degree; ++i) {
      const double base = options.affine
                              ? double(i) / degree
                              : double(i) * (i - 1) / (degree * (degree - 1));
      const double weight = options.rational ? 1 + .2 * i / degree : 1.;
      const double q =
          scale * (options.rational ? 1.2 * base / weight
                                    : base + (i == 3 ? hiddenPole : 0));
      if (options.rational)
        weights.push_back(weight);
      poles.emplace_back(q, 0, 0);
      uv.emplace_back(q, 0);
    }
    if (options.reverseTarget) {
      std::reverse(poles.begin(), poles.end());
      std::reverse(uv.begin(), uv.end());
      std::reverse(weights.begin(), weights.end());
      std::swap(edges[0].start, edges[0].end);
      face.loops[0][0].sense = false;
    }
    edges[0].curve =
        std::make_shared<BSplineCurve<3>>(degree, knots, poles, weights);
    edges[0].range = options.targetRange;
    face.loops[0][0].pcurve =
        std::make_shared<BSplineCurve<2>>(degree, knots, uv, weights);
    if (options.trim) {
      edges[0].curve = std::make_shared<TrimmedCurve<3>>(
          edges[0].curve, options.targetRange.lo, options.targetRange.hi);
      face.loops[0][0].pcurve = std::make_shared<TrimmedCurve<2>>(
          face.loops[0][0].pcurve, options.targetRange.lo,
          options.targetRange.hi);
    }
  }
  auto body = Body::buildSheet(vertices, edges, {face});
  require(computePCurves(body) == 0,
          "original pcurves could not be constructed");
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
void exportFins(std::ostream &s, const Body &body, double legacyCap,
                int operand = -1, const BodyHistory *history = nullptr,
                int lowerOperand = -1) {
  s << '[';
  bool first = true;
  for (auto id : body.fins()) {
    const auto &fin = body.fin(id);
    const auto &edge = body.edge(fin.edge);
    const auto faceId = body.finFace(id);
    const auto &face = body.face(faceId);
    require(face.surface->type() == SurfaceType::Plane,
            "export requires actual Plane support");
    int originOperand = operand, originFace = faceId.index;
    if (history) {
      const auto &origins = history->origins(EntityKind::Face, faceId.index);
      require(origins.size() == 1 && origins.front().kind == EntityKind::Face,
              "explicit face provenance missing");
      originOperand = origins.front().operand;
      originFace = origins.front().index;
    }
    const bool migrated =
        history && originOperand == lowerOperand && !body.isLaminar(fin.edge);
    const auto &frame = static_cast<const Plane &>(*face.surface).frame();
    if (!first)
      s << ',';
    first = false;
    s << "{\"fin\":" << id.index << ",\"edge\":" << fin.edge.index
      << ",\"face\":" << faceId.index << ",\"source_operand\":" << originOperand
      << ",\"source_face\":" << originFace
      << ",\"migrated\":" << (migrated ? "true" : "false")
      << ",\"sense\":" << (fin.sense ? "true" : "false") << ",\"range\":[";
    hex(s, edge.range.lo);
    s << ',';
    hex(s, edge.range.hi);
    s << "],\"budget\":";
    hex(s, history ? legacyCap : fin.pcurveTolerance);
    s << ",\"legacy_cap\":";
    hex(s, legacyCap);
    s << ",\"fin_tolerance\":";
    hex(s, fin.pcurveTolerance);
    s << ",\"edge_tolerance\":";
    hex(s, edge.tolerance);
    s << ",\"standard_resolution\":";
    hex(s, linearResolution());
    s << ",\"plane\":{\"origin\":";
    vec(s, frame.origin());
    s << ",\"x\":";
    vec(s, frame.xDir());
    s << ",\"y\":";
    vec(s, frame.yDir());
    s << ",\"z\":";
    vec(s, frame.zDir());
    s << "},\"curve\":";
    curve(s, edge.curve);
    s << ",\"pcurve\":";
    curve(s, fin.pcurve);
    s << '}';
  }
  s << ']';
}
void exportCase(const std::string &directory, const std::string &name,
                const Body &upper, const Body &lower, const SewResult &result,
                const BodyHistory &history, const Options &options, double cap,
                double tolerance) {
  if (directory.empty())
    return;
  const int upperOperand = options.reverseInputs ? 1 : 0,
            lowerOperand = 1 - upperOperand;
  std::ofstream s(directory + "/" + name + ".json");
  s << "{\"case\":\"" << name << "\",\"tolerance\":";
  hex(s, tolerance);
  s << ",\"standard_resolution\":";
  hex(s, linearResolution());
  s << ",\"legacy_cap\":";
  hex(s, cap);
  s << ",\"source\":";
  std::ostringstream top, bottom;
  exportFins(top, upper, cap, upperOperand);
  exportFins(bottom, lower, cap, lowerOperand);
  auto a = top.str(), b = bottom.str();
  s << '[' << a.substr(1, a.size() - 2) << ',' << b.substr(1, b.size() - 2)
    << ']';
  s << ",\"actual\":";
  exportFins(s, result.body, cap, -1, &history, lowerOperand);
  s << "}\n";
}
void publicCase(const Options &options, const std::string &name,
                const std::string &directory) {
  auto upper = sourceSheet(true, options), lower = sourceSheet(false, options);
  require(checkBody(upper).empty() && checkBody(lower).empty(),
          "original sheets fail topology/geometric checker");
  const auto upperBefore = writeBodyBinary(upper),
             lowerBefore = writeBodyBinary(lower);
  const auto upperEdge = upper.edges().front();
  const auto originalTarget = upper.edge(upperEdge).curve;
  const auto lowerFin =
      lower.loopFins(lower.face(lower.faces().front()).loops.front())[2];
  const auto &sourceFin = lower.fin(lowerFin);
  const auto &sourceEdge = lower.edge(sourceFin.edge);
  const auto &sourcePlane = *lower.face(lower.finFace(lowerFin)).surface;
  const double tolerance = 1e-8 * options.scale;
  const double original = pcurveDeviation(
      sourcePlane, *sourceEdge.curve, *sourceFin.pcurve, sourceEdge.range, 16);
  const double cap = original + std::max(1e-7, 10 * tolerance);
  require(original <= linearResolution(),
          "source original fin is inconsistent before sewing");
  BodyHistory history;
  const auto result =
      options.reverseInputs
          ? sewSheets({&lower, &upper}, tolerance, false, &history)
          : sewSheets({&upper, &lower}, tolerance, false, &history);
  require(upperBefore == writeBodyBinary(upper) &&
              lowerBefore == writeBodyBinary(lower),
          "sewing mutated an original body");
  require(result.body.faces().size() == 2 && result.body.edges().size() == 7 &&
              result.freeEdges == 6 && result.shells == 1 && !result.closed,
          "operation omitted the intended shared edge or changed its sheet "
          "topology");
  require(checkBody(result.body).empty(),
          "assembled result fails original checker");
  exportCase(directory, name, upper, lower, result, history, options, cap,
             tolerance);
  const int lowerOperand = options.reverseInputs ? 0 : 1;
  int migratedCount = 0;
  double worst = 0, published = 0, edgeTolerance = 0, at = 0;
  for (auto id : result.body.fins()) {
    const auto &fin = result.body.fin(id);
    const auto &edge = result.body.edge(fin.edge);
    const auto faceId = result.body.finFace(id);
    const auto &face = result.body.face(faceId);
    const auto &origins = history.origins(EntityKind::Face, faceId.index);
    require(origins.size() == 1 && origins.front().kind == EntityKind::Face &&
                origins.front().index == 0,
            "declared face provenance lost");
    if (result.body.isLaminar(fin.edge))
      continue;
    require(edge.curve == originalTarget &&
                edge.range.lo == options.targetRange.lo &&
                edge.range.hi == options.targetRange.hi,
            "sewing changed the chosen target curve or its original parameter");
    if (origins.front().operand != lowerOperand)
      continue;
    ++migratedCount;
    const auto evaluate = [&](double t) {
      const auto uv = fin.pcurve->point(t);
      const double error =
          distance(face.surface->point(uv.x(), uv.y()), edge.curve->point(t));
      if (error > worst) {
        worst = error;
        at = t;
        published = fin.pcurveTolerance;
        edgeTolerance = edge.tolerance;
      }
    };
    for (int i = 0; i <= 256; ++i)
      evaluate(edge.range.lo + edge.range.length() * i / 256.);
    const double peak = options.reverseTarget ? 1 - 3. / 128 : 3. / 128;
    evaluate(edge.range.lo + edge.range.length() * peak);
#ifdef HAVE_PLANE_SEW_PROVIDER
    require(fin.pcurveTolerance > 0 && fin.pcurveTolerance <= cap,
            "migrated fin does not publish a finite bound within the fixed "
            "legacy cap");
    const double bound = detail::requirePCurveOnPlane(
        edge.curve, face.surface, fin.pcurve, edge.range, fin.pcurveTolerance);
    require(std::isfinite(bound) && bound <= fin.pcurveTolerance,
            "actual emitted fin violates its published certificate");
#endif
  }
  require(migratedCount == 1,
          "explicit source face does not identify exactly one remapped fin");
  std::cout << " source_original=" << original << " residual=" << worst
            << " legacy_cap=" << cap << " fin_tolerance=" << published
            << " edge_tolerance=" << edgeTolerance << " witness_t=" << at;
  require(worst <= cap,
          "actual remapped fin exceeds the original legacy 3D budget");
}
void publicFailures() {
  auto upper = sourceSheet(true, {}), lower = sourceSheet(false, {});
  const auto a = writeBodyBinary(upper), b = writeBodyBinary(lower);
  auto token = std::make_shared<std::atomic<bool>>(true);
  CancellationScope scope(token);
  bool cancelled = false;
  try {
    sewSheets({&upper, &lower}, 1e-8, false);
  } catch (const OperationCancelled &) {
    cancelled = true;
  }
  require(cancelled && a == writeBodyBinary(upper) &&
              b == writeBodyBinary(lower),
          "cancelled sewing changed source bodies or returned output");
}
#ifdef HAVE_PLANE_SEW_PROVIDER
void normalResidual() {
  auto plane = std::make_shared<Plane>(Frame3());
  auto edge = std::make_shared<Line<3>>(Vec3(0, 0, 5e-8), Vec3(1, 0, 0));
  double deviation = 123;
  const auto pc =
      detail::fitPCurveOnPlane(edge, plane, {-2, 5}, 1e-8, 1e-7, &deviation);
  require(pc && deviation >= 5e-8 && deviation <= 1e-7,
          "normal target residual was discarded or the legacy cap was raised");
  require(detail::requirePCurveOnPlane(edge, plane, pc, {-2, 5}, deviation) <=
              deviation,
          "returned final candidate lacks a whole-domain residual bound");
  auto far = std::make_shared<Line<3>>(Vec3(0, 0, 2e-7), Vec3(1, 0, 0));
  deviation = 123;
  bool rejected = false;
  try {
    detail::fitPCurveOnPlane(far, plane, {-2, 5}, 1e-8, 1e-7, &deviation);
  } catch (const std::domain_error &) {
    rejected = true;
  }
  require(rejected && deviation == 123,
          "off-plane rejection published partial deviation or widened the cap");
}
void physicalMetric() {
  auto basis = std::make_shared<BSplineCurve<3>>(
      4, std::vector<double>{0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
      std::vector<Vec3>{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0}});
  const Vec3 origin(4, -8, 2);
  auto edge = std::make_shared<TransformedCurve>(
      std::make_shared<TrimmedCurve<3>>(basis, 0, 1),
      Transform3::translation(origin));
  auto plane = std::make_shared<Plane>(
      Frame3::fromAxes(origin, {1e-12, 0, 0}, {.25, 1, 0}, {0, 0, 1}));
  double deviation = 123;
  const auto pc =
      detail::fitPCurveOnPlane(edge, plane, {0, 1}, 1e-8, 1e-7, &deviation);
  require(
      pc && deviation <= 1e-7,
      "physical Plane metric rejected supported original scaled/sheared axes");
  double physical = 0, parameter = 0;
  for (int i = 0; i <= 100; ++i) {
    const double t = i / 100.;
    const auto uv = pc->point(t);
    physical = std::max(physical,
                        distance(plane->point(uv.x(), uv.y()), edge->point(t)));
    parameter = std::max(parameter, std::fabs(uv.x() - 1e12 * t * t * t * t));
  }
  require(physical <= 1e-8 && parameter > 1.,
          "test did not distinguish physical mm from UV coordinate error");
  require(detail::requirePCurveOnPlane(edge, plane, pc, {0, 1}, deviation) <=
              deviation,
          "actual controls lost wrapper or Plane metric provenance");
  std::cout << " physical=" << physical << " uv_coordinate_error=" << parameter;
}
void candidateContracts() {
  auto plane = std::make_shared<Plane>(Frame3());
  auto edge = std::make_shared<Line<3>>(Vec3(), Vec3(1, 0, 0));
  double deviation = 123;
  const auto pc =
      detail::fitPCurveOnPlane(edge, plane, {-2, 5}, 1e-8, 1e-7, &deviation);
  const auto *spline = dynamic_cast<const BSplineCurve<2> *>(pc.get());
  require(spline, "expected actual C0 cubic pcurve");
  auto poles = spline->poles();
  poles[1][1] += 1e-4;
  auto wrong = std::make_shared<BSplineCurve<2>>(
      spline->degree(), spline->knots(), poles, spline->weights());
  bool rejected = false;
  try {
    detail::requirePCurveOnPlane(edge, plane, wrong, {-2, 5}, 1e-7);
  } catch (const std::domain_error &) {
    rejected = true;
  }
  require(rejected,
          "a corrupted original emitted control was silently accepted");
  deviation = 123;
  rejected = false;
  try {
    detail::fitPCurveOnPlane(edge, plane, {-2, 5}, 1e-8,
                             std::numeric_limits<double>::quiet_NaN(),
                             &deviation);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected && deviation == 123,
          "recognized invalid budget published a partial bound");
  auto token = std::make_shared<std::atomic<bool>>(true);
  CancellationScope scope(token);
  bool cancelled = false;
  try {
    detail::fitPCurveOnPlane(edge, plane, {-2, 5}, 1e-8, 1e-7, &deviation);
  } catch (const OperationCancelled &) {
    cancelled = true;
  }
  require(cancelled && deviation == 123,
          "cancelled Plane construction published partial deviation");
}
#endif
} // namespace
int main(int argc, char **argv) {
  const std::string directory = argc > 1 ? argv[1] : "";
  int passed = 0, failed = 0;
  std::cout << std::setprecision(17);
  const auto run = [&](const std::string &name, const auto &fn) {
    std::cout << name << std::flush;
    try {
      fn();
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
  run("disjoint_shells_with_nested_AABB",[] {disjointShellsWithNestedBounds();});
  run("oblique_spheres_internal_point_tangency_rejected",[] {obliqueSpherePair(1,true);});
  run("oblique_spheres_external_point_tangency_rejected",[] {obliqueSpherePair(3,true);});
  run("oblique_spheres_strictly_nested_valid",[] {obliqueSpherePair(.5,false);});
  run("oblique_spheres_disjoint_valid",[] {obliqueSpherePair(3.1,false);});
  run("crossing_prisms_all_vertices_outside_rejected",[] {crossingPrismsRejected();});
  for (unsigned mask=0;mask<4;++mask)
    for (bool reverse:{false,true})
      run("concentric_spheres_orientation"+std::to_string(mask)+(reverse?"_BA":"_AB"),
          [&] {concentricSphereCavity(mask,reverse);});
  // Exhaust every component orientation and operand permutation.
  for (int depth=0;depth<=2;++depth)
    for (unsigned mask=0;mask<(depth==2?8u:4u);++mask) {
      std::vector<int> order=depth==2?std::vector<int>{0,1,2}:std::vector<int>{0,1};
      do {
        std::string name="multishell_depth"+std::to_string(depth)+"_orientation"+std::to_string(mask)+"_order";
        for (int i:order) name+=std::to_string(i);
        run(name,[&] {multishellCase(depth,mask,order);});
      } while (std::next_permutation(order.begin(),order.end()));
    }
  std::vector<std::pair<std::string, Options>> cases = {
      {"nonaffine_polynomial", {}},
      {"nonaffine_reverse_inputs", {false, true}},
      {"nonaffine_shift_trim", {false, false, false, true, 1, {-3, 5}}},
      {"nonaffine_reverse_target", {false, false, true}},
      {"nonaffine_scale", {false, false, false, false, 1000}},
      {"reuse_sampled_same_parameter", {true}},
      {"reuse_affine_range", {true, false, false, false, 1, {-3, 5}}}};
  Options rational;
  rational.rational = true;
  cases.push_back({"rational_original_weights", rational});
  for (const auto &entry : cases)
    run(entry.first, [&] { publicCase(entry.second, entry.first, directory); });
  run("public_cancellation_atomic", [] { publicFailures(); });
#ifdef HAVE_PLANE_SEW_PROVIDER
  run("plane_fit_original_normal_and_atomic_budget", [] { normalResidual(); });
  run("plane_actual_controls_invalid_budget_and_cancellation",
      [] { candidateContracts(); });
  run("plane_fit_physical_metric_original_wrappers", [] { physicalMetric(); });
#endif
  std::cout << passed << " passed " << failed << " failed\n";
  return failed ? 1 : 0;
}
