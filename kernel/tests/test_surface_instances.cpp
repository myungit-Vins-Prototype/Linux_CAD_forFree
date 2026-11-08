#include "fk_test.h"
#include "fk_bspline_surface.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"
#include "fk_step.h"
#include "fk_sew.h"
#include "fk_tessellate.h"
#include "fk_mass.h"
#include "fk_blend.h"
#include "fk_curve_algo.h"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include "fk_parallel.h"

using namespace ForgeCad::Kernel;

namespace {
std::shared_ptr<const BSplineSurface> sample(bool rational, int nu = 4, int nv = 4) {
    auto knots = [](int n) {
        std::vector<double> k(4, 0);
        for (int i = 1; i < n - 3; ++i) k.push_back(double(i) / (n - 3));
        k.insert(k.end(), 4, 1);
        return k;
    };
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j) {
            poles.emplace_back(3.0 * i / (nu - 1), 2.0 * j / (nv - 1), 0.1 * i * j / (nu * nv));
            if (rational) weights.push_back(1.0 + 0.1 * ((i + j) % 3));
        }
    return std::make_shared<BSplineSurface>(3, 3, knots(nu), knots(nv), nu, nv, poles, weights);
}
Body sheet(const SurfacePtr &s) { return Body::buildSheet({}, {}, {{s, true, {}}}); }
}

FK_TEST(SurfaceInstanceExactJetsAndPoles) {
    const Transform3 t = Transform3::translation({2, -3, 4}) * Transform3::scaling({}, 2.5) *
        Transform3::reflection({0.3, 0.2, 0.1}, normalized(Vec3(1, 2, 3)));
    for (bool rational : {false, true}) {
        const auto base = sample(rational);
        const BSplineSurface image(base, t);
        FK_CHECK(image.basis() == base);
        FK_CHECK(&image.uKnots() == &base->uKnots());
        FK_CHECK(&image.vKnots() == &base->vKnots());
        FK_CHECK(image.isRational() == rational);
        for (int i = 0; i < base->uPoleCount(); ++i)
            for (int j = 0; j < base->vPoleCount(); ++j) {
                FK_CHECK(distance(image.pole(i, j), t.applyToPoint(base->pole(i, j))) == 0);
                FK_CHECK(image.weight(i, j) == base->weight(i, j));
            }
        for (int order = 0; order <= 4; ++order)
            for (double u : {0., .17, .5, 1.})
                for (double v : {0., .23, .8, 1.}) {
                    Vec3 a[25], b[25];
                    for (auto &p : b) p = Vec3(11, 12, 13);
                    base->evaluate(u, v, order, a);
                    image.evaluate(u, v, order, b);
                    for (int i = 0; i <= order; ++i)
                        for (int j = 0; j <= order; ++j) {
                            const int k = Surface::derivativeIndex(i, j, order);
                            if (i + j > order) continue;
                            const Vec3 expected = i + j == 0 ? t.applyToPoint(a[k]) : t.applyToVector(a[k]);
                            FK_CHECK(distance(b[k], expected) == 0);
                        }
                    FK_CHECK(distance(image.uIso(u)->point(v), image.point(u, v)) < 2e-14);
                    FK_CHECK(distance(image.vIso(v)->point(u), image.point(u, v)) < 2e-14);
                }
        const Vec3 expectedNormal = -normalized(t.applyToVector(base->normal(.31, .73)));
        FK_CHECK(distance(image.normal(.31, .73), expectedNormal) < 1e-14);
    }
}

FK_TEST(SurfaceInstanceFlattenedAndImmutable) {
    const auto base = sample(true);
    const auto before = base->point(.17, .43);
    const auto t = Transform3::reflection({0.2, -.7, 1.1}, normalized(Vec3(2, 1, -1)));
    auto image = std::make_shared<BSplineSurface>(base, t);
    BSplineSurface twice(image, t);
    FK_CHECK(twice.basis() == base);
    FK_CHECK(distance(twice.point(.17, .43), before) < 1e-14);
    FK_CHECK(distance(base->point(.17, .43), before) == 0);
    const auto body = sheet(base);
    const auto mirrored = mirrorBody(body, {.2, -.7, 1.1}, normalized(Vec3(2, 1, -1)));
    const auto &s = static_cast<const BSplineSurface &>(*mirrored.face(mirrored.faces().front()).surface);
    FK_CHECK(s.basis() == base);
    FK_CHECK(checkBody(mirrored).empty());
}

FK_TEST(SurfaceInstanceSharesBezierPolesAndBounds) {
    const auto base = sample(true, 8, 7);
    const auto t = Transform3::reflection({1, 2, 0}, normalized(Vec3(1, -2, 3)));
    const BSplineSurface image(base, t);
    const auto source = base->cachedBezierPatches(), patches = image.cachedBezierPatches();
    FK_CHECK(source->size() == patches->size());
    FK_CHECK(patches == image.cachedBezierPatches());
    for (std::size_t k = 0; k < source->size(); ++k) {
        const auto &p = (*patches)[k];
        FK_CHECK(p.basis().get() == &(*source)[k]);
        const double u = p.uDomain().lo + .31 * p.uDomain().length();
        const double v = p.vDomain().lo + .63 * p.vDomain().length();
        FK_CHECK(distance(p.point(u, v), image.point(u, v)) < 1e-14);
    }
    const auto tree = image.cachedPatchTree();
    FK_CHECK(!tree->nodes.empty());
    for (const auto &node : tree->nodes) {
        const auto p = image.point(.5 * (node.u.lo + node.u.hi), .5 * (node.v.lo + node.v.hi));
        for (int axis = 0; axis < 3; ++axis) {
            FK_CHECK(p[axis] >= node.lo[axis] - 1e-13);
            FK_CHECK(p[axis] <= node.hi[axis] + 1e-13);
        }
    }
    const auto p = image.point(.37, .62);
    const auto projected = projectPoint(image, p + .1 * image.normal(.37, .62));
    FK_CHECK(distance(image.point(projected.u, projected.v), p) < 1e-9);
}

FK_TEST(SurfaceInstanceBinarySharingAndLegacyRead) {
    const auto base = sample(true, 100, 80);
    const auto t = Transform3::reflection({}, {1, 0, 0});
    const auto image = std::make_shared<BSplineSurface>(base, t);
    const Body body = Body::buildSheet({}, {}, {{base, true, {}}, {image, false, {}}});
    const auto data = writeBodyBinary(body), single = writeBodyBinary(sheet(base));
    FK_CHECK(data.size() < single.size() + 256);
    const auto copy = readBodyBinary(data);
    const auto &readBase = copy.face(copy.faces()[0]).surface;
    const auto &readImage = static_cast<const BSplineSurface &>(*copy.face(copy.faces()[1]).surface);
    FK_CHECK(readImage.basis().get() == readBase.get());
    FK_CHECK(writeBodyBinary(copy) == data);
    for (double u : {0., .1, .6, 1.})
        for (double v : {0., .3, 1.}) FK_CHECK(distance(readImage.point(u, v), image->point(u, v)) == 0);
    auto legacy = single;
    const std::uint32_t version = 1;
    std::memcpy(&legacy[4], &version, sizeof(version));
    const auto old = readBodyBinary(legacy);
    FK_CHECK(distance(old.face(old.faces()[0]).surface->point(.2, .4), base->point(.2, .4)) == 0);
}

FK_TEST(SurfaceInstanceRejectsInvalidPlacementAndRecords) {
    const auto base = sample(false);
    FK_CHECK_THROWS(BSplineSurface(nullptr, Transform3()));
    FK_CHECK_THROWS(BSplineSurface(base, Transform3::translation({std::numeric_limits<double>::quiet_NaN(), 0, 0})));
    double m[3][3] = {{1, 1, 0}, {0, 1, 0}, {0, 0, 1}};
    FK_CHECK_THROWS(BSplineSurface(base, Transform3::fromParts(m, {})));
    m[0][1] = std::numeric_limits<double>::infinity();
    FK_CHECK_THROWS(BSplineSurface(base, Transform3::fromParts(m, {})));
    auto data = writeBodyBinary(sheet(std::make_shared<BSplineSurface>(base, Transform3::reflection({}, {1, 0, 0}))));
    const auto record = data.find(std::string("\3\200", 2));
    FK_CHECK(record != std::string::npos);
    if (record == std::string::npos) return;
    auto corrupt = data;
    const std::int32_t selfReference = 1;
    std::memcpy(&corrupt[record + 2], &selfReference, sizeof(selfReference));
    FK_CHECK_THROWS(readBodyBinary(corrupt));
    corrupt = data;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::memcpy(&corrupt[record + 6], &nan, sizeof(nan));
    FK_CHECK_THROWS(readBodyBinary(corrupt));
    FK_CHECK_THROWS(readBodyBinary(data.substr(0, record + 50)));
    const std::uint32_t version = 1;
    std::memcpy(&data[4], &version, sizeof(version));
    FK_CHECK_THROWS(readBodyBinary(data));
}

namespace {
Body boundedSheet(const std::shared_ptr<const BSplineSurface> &s) {
    const std::vector<Vec2> uv{{0,0}, {1,0}, {1,1}, {0,1}};
    std::vector<Vec3> points;
    for (const auto &p : uv) points.push_back(s->point(p.x(), p.y()));
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFin> fins;
    for (int i = 0; i < 4; ++i) {
        const bool sense = i < 2;
        const auto curve = i == 0 ? s->vIso(0) : i == 1 ? s->uIso(1) : i == 2 ? s->vIso(1) : s->uIso(0);
        edges.push_back({sense ? i : (i+1)%4, sense ? (i+1)%4 : i, curve, {0,1}});
        auto pcurve = std::make_shared<BSplineCurve<2>>(1, std::vector<double>{0,0,1,1},
            std::vector<Vec2>{uv[sense ? i : (i+1)%4], uv[sense ? (i+1)%4 : i]});
        fins.push_back({i, sense, pcurve, 1e-12});
    }
    return Body::buildSheet(points, edges, {{s, true, {fins}}});
}
}

FK_TEST(SurfaceInstanceStepReplicaRoundTrip) {
    const auto base = sample(true, 8, 7);
    const Body original = boundedSheet(base);
    const auto t = Transform3::translation({7, 3, 5});
    ExchangeBody first, second;
    first.body = original; first.name = "original";
    second.body = transformBody(original, t); second.name = "replica";
    for (auto schema : {StepSchema::AP203, StepSchema::AP214, StepSchema::AP242}) {
        StepWriteOptions options; options.schema = schema;
        const auto text = writeStep({first, second}, options);
        FK_CHECK(text.find("SURFACE_REPLICA") != std::string::npos);
        FK_CHECK(text.find("CARTESIAN_TRANSFORMATION_OPERATOR_3D") != std::string::npos);
        const auto restored = readStep(text, {1});
        FK_CHECK(restored.bodies.size() == 2);
        if (restored.bodies.size() != 2) continue;
        for (const auto &b : restored.bodies) {
            FK_CHECK(checkBody(b.body).empty());
            const auto &s = *b.body.face(b.body.faces().front()).surface;
            const bool image = b.name == "replica";
            for (double u : {0., .17, .5, 1.})
                for (double v : {0., .29, .7, 1.}) {
                    const auto expected = image ? t.applyToPoint(base->point(u,v)) : base->point(u,v);
                    FK_CHECK(distance(s.point(u,v), expected) < 3e-14);
                }
        }
        // Il lettore conserva la stessa definizione, senza ricreare i poli.
        const auto &s = static_cast<const BSplineSurface &>(*restored.bodies[1].body.face(restored.bodies[1].body.faces()[0]).surface);
        FK_CHECK(bool(s.basis()));
    }
}

FK_TEST(SurfaceInstanceStepRejectsCycles) {
    ExchangeBody b;
    b.body = transformBody(boundedSheet(sample(false)), Transform3::translation({7, 3, 5}));
    auto text = writeStep({b});
    const auto tag = text.find("=SURFACE_REPLICA(");
    FK_CHECK(tag != std::string::npos);
    if (tag == std::string::npos) return;
    const auto begin = text.rfind('#', tag);
    const auto id = text.substr(begin, tag - begin);
    const auto parent = text.find('#', tag);
    const auto comma = text.find(',', parent);
    text.replace(parent, comma - parent, id);
    const auto result = readStep(text, {1});
    FK_CHECK(result.bodies.empty());
    bool cycle = false;
    for (const auto &note : result.notes) cycle = cycle || note.find("ciclic") != std::string::npos;
    FK_CHECK(cycle);
}

FK_TEST(SurfaceInstanceStepReadsStandardMirrorAndScale) {
    ExchangeBody first, second;
    const auto base = sample(true);
    first.body = boundedSheet(base);
    const auto transform = Transform3::translation({7,3,5}) * Transform3::scaling({}, 2) * Transform3::reflection({}, {1,0,0});
    second.body = transformBody(first.body, transform);
    auto text = writeStep({first, second});
    const auto baseTag = text.find("=(BOUNDED_SURFACE()");
    const auto imageTag = text.find("=(BOUNDED_SURFACE()", baseTag + 1);
    FK_CHECK(baseTag != std::string::npos && imageTag != std::string::npos);
    if (baseTag == std::string::npos || imageTag == std::string::npos) return;
    const auto baseStart = text.rfind('#', baseTag), imageStart = text.rfind('#', imageTag);
    const auto baseId = text.substr(baseStart, baseTag - baseStart);
    const auto imageId = text.substr(imageStart, imageTag - imageStart);
    text.replace(imageStart, text.find(';', imageTag) - imageStart + 1,
        imageId + "=SURFACE_REPLICA(''," + baseId + ",#999005);");
    text.insert(text.find("ENDSEC;", text.find("DATA;")),
        "#999001=DIRECTION('',(-1.,0.,0.));\n#999002=DIRECTION('',(0.,1.,0.));\n"
        "#999003=DIRECTION('',(0.,0.,1.));\n#999004=CARTESIAN_POINT('',(7.,3.,5.));\n"
        "#999005=CARTESIAN_TRANSFORMATION_OPERATOR_3D('',#999001,#999002,#999004,2.,#999003);\n");
    const auto restored = readStep(text, {1});
    FK_CHECK(restored.bodies.size() == 2);
    if (restored.bodies.size() != 2) return;
    const auto &surface = *restored.bodies[1].body.face(restored.bodies[1].body.faces()[0]).surface;
    for (double u : {0., .31, .7, 1.}) for (double v : {0., .17, .8, 1.})
        FK_CHECK(distance(surface.point(u,v), transform.applyToPoint(base->point(u,v))) < 2e-14);
}

namespace {
Body polarWedge() {
    const std::vector<Vec3> left{{15,0,0},{15,0,12},{8,0,30},{0,0,30}};
    std::vector<Vec3> right;
    const auto turn=Transform3::rotation({},{0,0,1},kTwoPi/18);
    for(const auto &p:left)right.push_back(turn.applyToPoint(p));
    const std::vector<double> knots{0,0,0,0,1,1,1,1};
    const auto curve=[&](const std::vector<Vec3> &p){return std::make_shared<BSplineCurve<3>>(int(p.size())-1,
        p.size()==4?knots:std::vector<double>{0,0,1,1},p);};
    const auto uv=[](Vec2 a,Vec2 b){return std::make_shared<BSplineCurve<2>>(1,std::vector<double>{0,0,1,1},std::vector<Vec2>{a,b});};
    auto poles=left;poles.insert(poles.end(),right.begin(),right.end());
    Body::BuildFace face;
    face.surface=std::make_shared<BSplineSurface>(1,3,std::vector<double>{0,0,1,1},knots,2,4,poles);
    face.loops={{{3,true,uv({0,0},{1,0}),1e-10},{2,true,uv({1,0},{1,1}),1e-10},
                 {1,false,uv({0,1},{1,1}),1e-10},{0,false,uv({0,0},{0,1}),1e-10}}};
    return Body::buildSheet({left.front(),right.front(),left.back()},
        {{0,2,curve(left),{0,1}},{2,2,curve({left.back(),left.back()}),{0,1}},
         {1,2,curve(right),{0,1}},{0,1,curve({left.front(),right.front()}),{0,1}}},{face});
}
}

FK_TEST(SurfaceInstancePreservesSingularTrimsAndMesh) {
    const Body source=polarWedge();
    const auto original=writeBodyBinary(source);
    FK_CHECK(checkBody(source).empty());
    for(double scale:{0.25,1.0,3.0})for(int i=0;i<18;++i){
        const auto t=Transform3::translation({17,-23,11})*Transform3::scaling({},scale)
            *Transform3::rotation({},{0,0,1},i*kTwoPi/18);
        const auto moved=transformBody(source,t);
        FK_CHECK(checkBody(moved).empty());
        for(auto f:source.fins()){
            FK_CHECK(moved.fin(f).pcurve==source.fin(f).pcurve);
            FK_CHECK_NEAR(moved.fin(f).pcurveTolerance,scale*source.fin(f).pcurveTolerance,1e-20);
        }
        TessellationOptions o;o.deflection=0.02*scale;o.threads=1;
        const auto mesh=tessellate(moved,o);FK_CHECK(mesh.failedFaces==0&&mesh.faces.size()==1);
        for(const auto &face:mesh.faces)for(const auto &tri:face.triangles){
            const auto n=cross(face.points[tri[1]]-face.points[tri[0]],face.points[tri[2]]-face.points[tri[0]]);
            if(norm(n)>1e-8*scale*scale)
                FK_CHECK(dot(n,face.normals[tri[0]]+face.normals[tri[1]]+face.normals[tri[2]])>=0);
        }
    }
    FK_CHECK(writeBodyBinary(source)==original);
}

FK_TEST(SurfaceInstanceSewPolarWedgesAndCap) {
    const Body source=polarWedge();std::vector<Body> wedges;
    for(int i=0;i<18;++i)wedges.push_back(transformBody(source,Transform3::rotation({},{0,0,1},i*kTwoPi/18)));
    std::vector<const Body*> inputs;for(const auto &b:wedges)inputs.push_back(&b);
    std::vector<Vec3> points;std::vector<Body::BuildEdge> edges;std::vector<Body::BuildFin> loop;
    for(int i=0;i<18;++i)points.push_back(Transform3::rotation({},{0,0,1},i*kTwoPi/18).applyToPoint({15,0,0}));
    for(int i=0;i<18;++i){edges.push_back({i,(i+1)%18,std::make_shared<Line<3>>(points[i],points[(i+1)%18]-points[i]),{0,distance(points[i],points[(i+1)%18])}});loop.push_back({i,true,{}});}
    const Body cap=Body::buildSheet(points,edges,{{std::make_shared<Plane>(Frame3()),true,{loop}}});
    for(bool reverse:{false,true}){
        const auto sewn=sewSheets(inputs,1e-6,false);
        FK_CHECK(!sewn.closed&&!sewn.solid&&sewn.shells==1&&sewn.freeEdges==18);
        FK_CHECK(sewn.body.faces().size()==18&&sewn.body.edges().size()==36&&checkBody(sewn.body).empty());
        double area=0;for(auto f:sewn.body.faces())area+=faceArea(sewn.body,f);
        FK_CHECK_NEAR(area,18*faceArea(source,source.faces().front()),1e-8*area);
        auto closedInputs=inputs;closedInputs.push_back(&cap);
        const auto closed=sewSheets(closedInputs,1e-6,true);
        FK_CHECK(closed.closed&&closed.solid&&closed.shells==1&&closed.freeEdges==0);
        FK_CHECK(closed.body.faces().size()==19&&checkBody(closed.body).empty());
        FK_CHECK(massProperties(closed.body).volume>0);
        std::reverse(inputs.begin(),inputs.end());
    }
}

FK_TEST(SurfaceInstanceZeroRadiusAtHighValenceCusp) {
    const Body wedge = polarWedge();
    std::vector<Body> copies;
    for (int i = 0; i < 18; ++i)
        copies.push_back(transformBody(wedge, Transform3::rotation({}, {0,0,1}, i*kTwoPi/18)));
    std::vector<const Body*> inputs;
    for (const auto &b : copies) inputs.push_back(&b);
    const Body base = sewSheets(inputs, 1e-6, false).body;
    const auto original = writeBodyBinary(base);
    std::vector<EdgeId> seams;
    for (auto e : base.edges()) if (!base.isLaminar(e)) seams.push_back(e);
    FK_CHECK(seams.size() == 18);
    BlendProfileSpec spec;
    // Il verso della legge si conserva anche invertendo esplicitamente
    // ogni tabella, indipendentemente dal verso topologico dell'edge.
    for (auto e : seams) {
        const auto path = blendPaths(base, {e}).front();
        const bool tipFirst = blendPathPoint(base, path, 0).point.z() > 15;
        spec.paths.push_back({{e}, tipFirst ? std::vector<double>{.3,0} : std::vector<double>{0,.3}, false, true});
    }
    const Body result = blendProfileEdges(base, seams, .3, spec, false);
    FK_CHECK(result.faces().size() == 36 && result.shells().size() == 1);
    FK_CHECK(checkBody(result, {true,true}).empty());
    FK_CHECK(writeBodyBinary(base) == original);
    for (auto f : result.faces()) {
        if (f.index < 18) continue;
        const auto &s = *result.face(f).surface;
        const auto ur = s.uDomain();
        const double tip = s.point(ur.lo,.5).z() > s.point(ur.hi,.5).z() ? ur.lo : ur.hi;
        for (int j = 0; j <= 8; ++j)
            FK_CHECK(distance(s.point(tip,j/8.), {0,0,30}) < 1e-10);
        EdgeId source; double best = std::numeric_limits<double>::infinity();
        const auto middle = s.point((ur.lo+ur.hi)/2,.5);
        for (auto e : seams) {
            const auto &edge = base.edge(e);
            const auto q = projectPoint(*edge.curve, middle, edge.range);
            if (q.distance < best) { best = q.distance; source = e; }
        }
        const auto &edge = base.edge(source);
        const double length = arcLength(*edge.curve,edge.range);
        for (double f : {.2,.5,.8}) {
            const double u = ur.lo + f*ur.length();
            const auto a = s.point(u,0), b = s.point(u,.5), c = s.point(u,1);
            const double radius = distance(a,b)*distance(b,c)*distance(c,a)/(2*norm(cross(b-a,c-a)));
            const double fraction = arcLength(*edge.curve,{edge.range.lo,u}) / length;
            const bool sourceTipFirst = edge.curve->point(edge.range.lo).z() > 15;
            FK_CHECK_NEAR(radius,.3*(sourceTipFirst ? fraction : 1-fraction),1e-7);
            for (double v : {0.,1.}) {
                const auto contact = s.point(u,v);
                double gap = std::numeric_limits<double>::infinity();
                Vec3 normal;
                for (auto fin : {edge.forward,edge.backward}) {
                    const auto &support = *base.face(base.finFace(fin)).surface;
                    const auto q = projectPoint(support,contact);
                    if (q.distance < gap) { gap = q.distance; normal = support.normal(q.u,q.v); }
                }
                FK_CHECK(gap < 1e-7);
                FK_CHECK(std::fabs(dot(s.normal(u,v),normal)) > 1-1e-8);
            }
        }
    }
    TessellationOptions options; options.deflection = .04; options.threads = 1;
    const auto mesh = tessellate(result,options);
    FK_CHECK(mesh.failedFaces == 0 && mesh.faces.size() == 36);
    for (const auto &face : mesh.faces) for (const auto &t : face.triangles) {
        const auto n = cross(face.points[t[1]]-face.points[t[0]],face.points[t[2]]-face.points[t[0]]);
        if (norm(n) > 1e-8)
            FK_CHECK(dot(n,face.normals[t[0]]+face.normals[t[1]]+face.normals[t[2]]) >= 0);
    }
    FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)), {true,true}).empty());
    // Lo zero interno, negativo o su tutta la legge non definisce un
    // raccordo valido; un raggio positivo alla cuspide resta non gestito.
    BlendProfileSpec invalid;
    for (const auto &radii : {std::vector<double>{0,0}, {1,0,1}, {1,-1}, {1,1e-10}}) {
        invalid.radii = radii;
        FK_CHECK_THROWS(blendProfileEdges(base, {seams.front()}, .3, invalid, false));
    }
    FK_CHECK_THROWS(blendEdges(base, {seams.front()}, .3, false, nullptr, false));
}


FK_TEST(BodyReadPoolExactSharingAndRecordIdentity) {
    const auto base = sample(true, 8, 7);
    const auto bytes = writeBodyBinary(sheet(base));
    const auto pool = makeBodyReadPool();
    const auto first = readBodyBinary(bytes, *pool), second = readBodyBinary(bytes, *pool);
    FK_CHECK(first.face(first.faces().front()).surface == second.face(second.faces().front()).surface);
    FK_CHECK(writeBodyBinary(first) == writeBodyBinary(readBodyBinary(bytes)));
    const Body distinct = Body::buildSheet({}, {}, {{sample(true), true, {}}, {sample(true), false, {}}});
    const auto duplicateBytes = writeBodyBinary(distinct);
    const auto copy = readBodyBinary(duplicateBytes, *pool);
    FK_CHECK(copy.face(copy.faces()[0]).surface != copy.face(copy.faces()[1]).surface);
    FK_CHECK(writeBodyBinary(copy) == writeBodyBinary(readBodyBinary(duplicateBytes)));
    const auto image = std::make_shared<BSplineSurface>(base, Transform3::translation({2,3,4}));
    const auto placedBytes = writeBodyBinary(sheet(image));
    const auto placed = readBodyBinary(placedBytes, *pool), again = readBodyBinary(placedBytes, *pool);
    FK_CHECK(placed.face(placed.faces()[0]).surface == again.face(again.faces()[0]).surface);
    FK_CHECK(writeBodyBinary(placed) == writeBodyBinary(readBodyBinary(placedBytes)));
    const auto otherPool = makeBodyReadPool();
    const auto independent = readBodyBinary(bytes, *otherPool);
    FK_CHECK(first.face(first.faces()[0]).surface != independent.face(independent.faces()[0]).surface);
}

FK_TEST(BodyReadPoolRejectsNearDuplicatesAndSurvivesPool) {
    const auto base = sample(true);
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i=0;i<base->uPoleCount();++i) for(int j=0;j<base->vPoleCount();++j) {
        poles.push_back(base->pole(i,j)); weights.push_back(base->weight(i,j));
    }
    const auto pool = makeBodyReadPool();
    const auto original = readBodyBinary(writeBodyBinary(sheet(base)), *pool);
    poles[0][0] = -0.0; // geometricamente uguale, bit differenti
    const auto signedZero = std::make_shared<BSplineSurface>(3,3,base->uKnots(),base->vKnots(),4,4,poles,weights);
    const auto changed = readBodyBinary(writeBodyBinary(sheet(signedZero)), *pool);
    FK_CHECK(original.face(original.faces()[0]).surface != changed.face(changed.faces()[0]).surface);
    weights[1] = std::nextafter(weights[1], 2.);
    const auto near = std::make_shared<BSplineSurface>(3,3,base->uKnots(),base->vKnots(),4,4,poles,weights);
    const auto almost = readBodyBinary(writeBodyBinary(sheet(near)), *pool);
    FK_CHECK(changed.face(changed.faces()[0]).surface != almost.face(almost.faces()[0]).surface);
    Body retained;
    { const auto temporary = makeBodyReadPool(); retained = readBodyBinary(writeBodyBinary(sheet(base)), *temporary); }
    FK_CHECK(writeBodyBinary(retained) == writeBodyBinary(readBodyBinary(writeBodyBinary(sheet(base)))));
    FK_CHECK_THROWS(readBodyBinary(std::string_view("FKBD"), *pool));
}

FK_TEST(BodyReadPoolConcurrentDocuments) {
    const auto bytes = writeBodyBinary(sheet(sample(true,8,7)));
    const auto pool = makeBodyReadPool();
    std::vector<Body> bodies(16);
    parallelFor(bodies.size(), 8, [&](std::size_t i) { bodies[i] = readBodyBinary(bytes, *pool); });
    const auto first = bodies[0].face(bodies[0].faces()[0]).surface;
    for (const auto &body : bodies) {
        FK_CHECK(body.face(body.faces()[0]).surface == first);
        FK_CHECK(writeBodyBinary(body) == writeBodyBinary(readBodyBinary(bytes)));
    }
}
