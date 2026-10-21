#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepFill.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Face.hxx>

#include <cmath>

#include "fk_body_check.h"
#include "fk_helix.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_tessellate.h"
#include "fk_test_profiles.h"

using namespace fktest;

namespace {

double totalArea(const Body &body) {
    double area = 0.0;
    for (FaceId f : body.faces()) area += faceArea(body, f);
    return area;
}

// Lamina valida: checkBody senza problemi, tassellazione completa.
void checkSheet(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK(body.isSheet());
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

void checkSolid(const Body &body) {
    for (const CheckIssue &issue : checkBody(body)) reportFailure(__FILE__, __LINE__, describe(issue.code) + ": " + issue.message);
    FK_CHECK(!body.isSheet());
    TessellationOptions options;
    options.deflection = 0.01;
    FK_CHECK(tessellate(body, options).failedFaces == 0);
}

LoftSection circleAt(double z, double r, const Vec3 &shift = Vec3()) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z) + shift, Vec3(0, 0, 1), Vec3(1, 0, 0));
    s.loop.segments = {arcSegment(Vec2(0, 0), r, 0.0, kTwoPi)};
    return s;
}

// Quadrato di lato a: 4 segmenti a partire dall'angolo (a/2, a/2), antiorario.
LoftSection cornerSquareAt(double z, double a) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const double h = a / 2;
    const std::vector<Vec2> c{Vec2(h, h), Vec2(-h, h), Vec2(-h, -h), Vec2(h, -h)};
    for (std::size_t i = 0; i < c.size(); ++i) s.loop.segments.push_back(lineSegment(c[i], c[(i + 1) % c.size()]));
    return s;
}

// Quadrato a partire da meta' del lato destro (5 tratti, come in test_loft).
LoftSection midSquareAt(double z, double a) {
    LoftSection s;
    s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
    const std::vector<Vec2> c{Vec2(a / 2, 0), Vec2(a / 2, a / 2), Vec2(-a / 2, a / 2), Vec2(-a / 2, -a / 2), Vec2(a / 2, -a / 2)};
    for (std::size_t i = 0; i < c.size(); ++i) s.loop.segments.push_back(lineSegment(c[i], c[(i + 1) % c.size()]));
    return s;
}

// La sezione come catena 3D (curve nello spazio).
std::vector<PathSegment> chainOf(const LoftSection &s) {
    std::vector<PathSegment> chain;
    for (const ProfileSegment &segment : s.loop.segments) chain.push_back({embedCurve(segment.curve, s.frame), segment.range});
    return chain;
}

PathSegment segment3(const Vec3 &a, const Vec3 &b) { return {std::make_shared<Line<3>>(a, normalized(b - a)), {0.0, distance(a, b)}}; }

}

FK_TEST(LoftSheetTubeOfCylinder) {
    // Due cerchi uguali paralleli: area laterale del cilindro 2 pi r h, lamina aperta.
    const double r = 1.5, h = 2.0;
    for (bool ruled : {true, false}) {
        const Body tube = loftSheet({circleAt(0.0, r), circleAt(h, r)}, ruled);
        checkSheet(tube);
        FK_CHECK_NEAR(totalArea(tube), kTwoPi * r * h, 1e-9 * kTwoPi * r * h);
    }
}

FK_TEST(LoftSheetTubeMatchesSolidSides) {
    // Cerchio -> quadrato rigato: le facce del tubo sono quelle laterali del solido.
    {
        const double r = 1.5, a = 2.0;
        const std::vector<LoftSection> sections{circleAt(0.0, r), midSquareAt(3.0, a)};
        const Body tube = loftSheet(sections, true);
        const Body solid = loftSolid(sections, true);
        checkSheet(tube);
        checkSolid(solid);
        const double lateral = totalArea(solid) - kPi * r * r - a * a;
        FK_CHECK_NEAR(totalArea(tube), lateral, 1e-9 * lateral);
    }
    // Liscio a tre sezioni (cerchio, quadrato, cerchio spostato).
    {
        const std::vector<LoftSection> sections{circleAt(0.0, 1.5), midSquareAt(2.0, 2.0), circleAt(4.0, 1.0, Vec3(0.5, 0, 0))};
        const Body tube = loftSheet(sections, false);
        const Body solid = loftSolid(sections, false);
        checkSheet(tube);
        checkSolid(solid);
        const double lateral = totalArea(solid) - kPi * 1.5 * 1.5 - kPi * 1.0 * 1.0;
        FK_CHECK_NEAR(totalArea(tube), lateral, 1e-9 * lateral);
    }
    // Liscio con G1 alle estremita': stessa corrispondenza e stesse condizioni del solido.
    {
        const std::vector<LoftSection> sections{circleAt(0.0, 1.5), circleAt(2.0, 1.0), circleAt(4.0, 1.5)};
        LoftOptions options;
        options.startContinuity = 1;
        options.endContinuity = 1;
        const Body tube = loftSheet(sections, options);
        const Body solid = loftSolid(sections, options);
        checkSheet(tube);
        checkSolid(solid);
        const double lateral = totalArea(solid) - 2.0 * kPi * 1.5 * 1.5;
        FK_CHECK_NEAR(totalArea(tube), lateral, 1e-9 * lateral);
    }
}

FK_TEST(LoftSheetMixedSectionsThrow) {
    LoftSection open;
    open.frame = Frame3(Vec3(0, 0, 2), Vec3(0, 0, 1), Vec3(1, 0, 0));
    open.loop.segments = {lineSegment(Vec2(-1, 0), Vec2(1, 0))};
    FK_CHECK_THROWS(loftSheet({circleAt(0.0, 1.0), open}, true));
    FK_CHECK_THROWS(loftSheet({open, circleAt(0.0, 1.0)}, false));
}

FK_TEST(RuledSurfaceFrustumOfCone) {
    // Cerchi coassiali su un asse obliquo, raggi e quote diversi, il secondo
    // orario e con un'altra partenza: tronco di cono, area pi (r1 + r2) apotema.
    const double r1 = 2.0, r2 = 1.0, h = 3.0;
    const Vec3 axis = normalized(Vec3(1, 2, 2)), x = normalized(cross(axis, Vec3(0, 0, 1))), y = cross(axis, x);
    const Vec3 origin(0.5, -1.0, 2.0);
    const auto c1 = std::make_shared<Circle<3>>(origin, x, y, r1);
    const auto c2 = std::make_shared<Circle<3>>(origin + h * axis, std::cos(1.0) * x - std::sin(1.0) * y, -std::sin(1.0) * x - std::cos(1.0) * y, r2);
    const Body body = ruledSurface({{c1, c1->domain()}}, {{c2, c2->domain()}});
    checkSheet(body);
    const double area = kPi * (r1 + r2) * std::hypot(h, r1 - r2);
    FK_CHECK_NEAR(totalArea(body), area, 1e-9 * area);
}

FK_TEST(RuledSurfaceHyperbolicParaboloid) {
    // Due segmenti sghembi: confronto con la faccia rigata di OCCT (BRepFill::Face).
    const Vec3 a0(0, 0, 0), a1(3, 0, 0.5), b0(0.2, 2, 1.5), b1(2.5, 2.5, -0.5);
    // Il secondo dato al contrario: la corrispondenza deve rigirarlo.
    const Body body = ruledSurface({segment3(a0, a1)}, {segment3(b1, b0)});
    checkSheet(body);
    const TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(toPnt(a0), toPnt(a1)).Edge();
    const TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(toPnt(b0), toPnt(b1)).Edge();
    const TopoDS_Face face = BRepFill::Face(e1, e2);
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props, 1e-12);
    const double reference = props.Mass();
    FK_CHECK_NEAR(totalArea(body), reference, 1e-7 * reference);
}

FK_TEST(RuledSurfaceHelicoid) {
    // Elica e il suo asse: elicoide retto, area esatta in forma chiusa
    // 2 pi [R sqrt(R^2 + c^2) / 2 + c^2 / 2 ln((R + sqrt(R^2 + c^2)) / c)], c = passo / (2 pi).
    HelixSpec spec;
    spec.frame = Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
    spec.radius = 1.0;
    spec.pitch = 2.0;
    spec.turns = 1.0;
    const auto helix = std::make_shared<HelixCurve>(spec);
    const Body body = ruledSurface({{helix, helix->domain()}}, {segment3(Vec3(0, 0, 0), Vec3(0, 0, spec.pitch))});
    checkSheet(body);
    const double R = spec.radius, c = spec.pitch / kTwoPi, s = std::sqrt(R * R + c * c);
    const double area = kTwoPi * (0.5 * R * s + 0.5 * c * c * std::log((R + s) / c));
    FK_CHECK_NEAR(totalArea(body), area, 1e-7 * area);
    // Anche con la B-spline dell'elica e un segmento qualsiasi.
    const auto spline = std::make_shared<BSplineCurve<3>>(helixBSpline(*helix));
    const Body other = ruledSurface({{spline, spline->domain()}}, {segment3(Vec3(3, 0, 0), Vec3(3, 1, 2))});
    checkSheet(other);
    FK_CHECK(totalArea(other) > 0.0);
}

FK_TEST(RuledSurfaceSquareToCircle) {
    // Catena chiusa di 4 segmenti e cerchio: stessa area del loft rigato sulle stesse sezioni.
    const double a = 2.0, r = 1.5;
    const std::vector<LoftSection> sections{cornerSquareAt(0.0, a), circleAt(3.0, r)};
    const Body ruled = ruledSurface(chainOf(sections[0]), chainOf(sections[1]));
    checkSheet(ruled);
    const Body tube = loftSheet(sections, true);
    const Body solid = loftSolid(sections, true);
    checkSolid(solid);
    const double lateral = totalArea(solid) - a * a - kPi * r * r;
    FK_CHECK_NEAR(totalArea(ruled), lateral, 1e-9 * lateral);
    FK_CHECK_NEAR(totalArea(tube), lateral, 1e-9 * lateral);
    // Tratti della catena dati in verso qualsiasi.
    std::vector<PathSegment> shuffled = chainOf(sections[0]);
    for (std::size_t k : {std::size_t(0), std::size_t(2)}) {
        PathSegment &p = shuffled[k];
        p = {reversedCurve(p.curve), {-p.range.hi, -p.range.lo}};
    }
    const Body again = ruledSurface(shuffled, chainOf(sections[1]));
    checkSheet(again);
    FK_CHECK_NEAR(totalArea(again), lateral, 1e-9 * lateral);
}

FK_TEST(RuledSurfaceMixedChainsThrow) {
    const auto circle = std::make_shared<Circle<3>>(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), 1.0);
    FK_CHECK_THROWS(ruledSurface({{circle, circle->domain()}}, {segment3(Vec3(-1, 0, 2), Vec3(1, 0, 2))}));
    FK_CHECK_THROWS(ruledSurface({segment3(Vec3(-1, 0, 2), Vec3(1, 0, 2))}, {{circle, circle->domain()}}));
}

// Catene chiuse di misura diversa o spostate: la partenza della seconda e' il
// vertice nella direzione della partenza della prima (come il loft rigato),
// non il punto piu' vicino in 3D, che cadeva a meta' di un lato e torceva la
// superficie (8 facce invece di 4).
FK_TEST(RuledSurfaceClosedChainsKeepCorners) {
    const auto square = [](double a, const Vec3 &center) {
        std::vector<PathSegment> chain;
        const double h = a / 2;
        const std::vector<Vec3> c{center + Vec3(h, h, 0), center + Vec3(-h, h, 0), center + Vec3(-h, -h, 0), center + Vec3(h, -h, 0)};
        for (std::size_t i = 0; i < 4; ++i) {
            const Vec3 p = c[i], q = c[(i + 1) % 4];
            chain.push_back({std::make_shared<Line<3>>(p, q - p), Interval{0.0, distance(p, q)}});
        }
        return chain;
    };
    const Body frustum = ruledSurface(square(2.0, Vec3(0, 0, 0)), square(4.0, Vec3(0, 0, 3)));
    checkSheet(frustum);
    FK_CHECK(frustum.faces().size() == 4);
    FK_CHECK_NEAR(totalArea(frustum), 12.0 * std::sqrt(10.0), 1e-9);
    const Body prism = ruledSurface(square(2.0, Vec3(0, 0, 0)), square(2.0, Vec3(1.5, 0, 3)));
    checkSheet(prism);
    FK_CHECK(prism.faces().size() == 4);
    // Due facce verticali 2 x 3 sghembe di 1.5 lungo x (area 2 * sqrt(9 + 2.25)) e due 2 x 3.
    FK_CHECK_NEAR(totalArea(prism), 2.0 * 2.0 * std::sqrt(9.0 + 2.25) + 2.0 * 2.0 * 3.0, 1e-9);
}
