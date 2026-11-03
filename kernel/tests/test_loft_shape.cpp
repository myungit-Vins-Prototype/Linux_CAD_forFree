#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_loft.h"
#include "fk_nurbs.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad::Kernel;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
BSplineCurvePtr<3> bezier(std::vector<Vec3> poles, std::vector<double> weights = {}) {
    const int degree = int(poles.size()) - 1;
    std::vector<double> knots(degree + 1, 0);
    knots.insert(knots.end(), degree + 1, 1);
    return std::make_shared<BSplineCurve<3>>(degree, knots, std::move(poles), std::move(weights));
}
LoftSection section(CurvePtr<3> curve) {
    LoftSection result;
    result.path = {{curve, curve->domain()}};
    return result;
}
const Surface &surface(const Body &body) {
    require(body.isSheet() && body.faces().size() == 1 && checkBody(body).empty(),
            "lamina B-rep valida con una faccia");
    return *body.face(body.faces().front()).surface;
}
double angle(Vec3 a, Vec3 b) {
    return std::atan2(norm(cross(a, b)), dot(a, b));
}
double uJump(const Surface &s) {
    double worst = 0;
    for (double u : s.uBreakpoints({0, 1}))
        if (u > 0 && u < 1)
            for (int j = 1; j < 60; ++j)
                worst = std::max(worst, angle(s.normal(u - 1e-9, j / 60.), s.normal(u + 1e-9, j / 60.)));
    return worst;
}
double vJump(const Surface &s) {
    double worst = 0;
    for (double v : s.vBreakpoints({0, 1}))
        if (v > 0 && v < 1)
            for (int j = 1; j < 60; ++j)
                worst = std::max(worst, angle(s.normal(j / 60., v - 1e-10), s.normal(j / 60., v + 1e-10)));
    return worst;
}
void regular(const Surface &s, double minimumSine) {
    for (int i = 1; i < 40; ++i)
        for (int j = 1; j < 40; ++j) {
            Vec3 q[4];
            s.evaluate(i / 40., j / 40., 1, q);
            require(norm(q[1]) > 1e-9 && norm(q[2]) > 1e-9, "jet regolari interni");
            require(norm(cross(q[1], q[2])) / (norm(q[1]) * norm(q[2])) > minimumSine,
                    "assenza di collassi nella griglia interna");
        }
}
void sameShape(const Surface &a, const Surface &b, double tolerance) {
    double worst = 0;
    for (int i = 0; i <= 30; ++i)
        for (int j = 0; j <= 30; ++j)
            worst = std::max(worst, distance(a.point(i / 30., j / 30.), b.point(i / 30., j / 30.)));
    std::cout << " maxDistance=" << worst;
    require(worst < tolerance, "invarianza geometrica rispetto alla rappresentazione");
}
LoftOptions guides(CurvePtr<3> a, CurvePtr<3> b, int count) {
    LoftOptions o;
    o.startContinuity = o.endContinuity = 1;
    if (count > 0) {
        o.guides.push_back({{a, a->domain()}});
        o.guideConditions.push_back({1, 1});
    }
    if (count > 1) {
        o.guides.push_back({{b, b->domain()}});
        o.guideConditions.push_back({1, 1});
    }
    return o;
}
void knotInvariance(bool rational) {
    auto a = bezier({{0, 0, 0}, {0, 1, 0}, {2, 2, 0}, {3, 2, 0}},
                    rational ? std::vector<double>{1, .6, 1.4, 1} : std::vector<double>{});
    auto b = bezier({{0, 0, 4}, {0, 2, 4}, {3, 3, 4}, {4, 3, 4}},
                    rational ? std::vector<double>{1, 1.3, .8, 1} : std::vector<double>{});
    auto ar = std::make_shared<BSplineCurve<3>>(a->insertKnot(.37, 2)),
         br = std::make_shared<BSplineCurve<3>>(b->insertKnot(.61, 1));
    auto g1 = bezier({{0, 0, 0}, {-1, 0, 1}, {-1, 0, 3}, {0, 0, 4}}),
         g2 = bezier({{3, 2, 0}, {5, 2, 1}, {5, 3, 3}, {4, 3, 4}});
    for (int count : {0, 1, 2}) {
        const auto o = guides(g1, g2, count);
        const auto base = loftSheet({section(a), section(b)}, o), refined = loftSheet({section(ar), section(br)}, o);
        std::cout << " knot rational=" << rational << " guides=" << count;
        const auto &s = surface(base), &r = surface(refined);
        sameShape(s, r, 4e-9);
        require(uJump(r) < 1e-6, "un nodo ridondante non genera una cresta U");
        regular(r, .1);
        std::cout << std::endl;
    }
}
CurvePtr<3> joinedSection(double z, bool second, bool rational = false) {
    auto a = bezier(second ? std::vector<Vec3>{{0, 0, z}, {.5, 0, z}, {1.5, 1, z}}
                           : std::vector<Vec3>{{0, 0, z}, {1, 0, z}, {2, 1, z}},
                    rational ? std::vector<double>{1, .8, 1} : std::vector<double>{});
    // Le tangenti nei due lati sono collineari ma le velocita' differenti.
    auto b = bezier(second ? std::vector<Vec3>{{1.5, 1, z}, {3.5, 3, z}, {3.5, 4, z}}
                           : std::vector<Vec3>{{2, 1, z}, {3, 2, z}, {3, 4, z}},
                    rational ? std::vector<double>{1, 1.2, 1} : std::vector<double>{});
    return std::make_shared<BSplineCurve<3>>(
        joinBezierPieces({*a, *b}, second ? std::vector<double>{0, 1, 2} : std::vector<double>{0, 1, 3}));
}
void nonuniformSections(bool rational) {
    const auto a = joinedSection(0, false, rational), b = joinedSection(4, true, rational);
    auto g1 = bezier({a->point(0), {-1, 0, 1}, {-1, 0, 3}, b->point(0)}),
         g2 = bezier({a->point(a->domain().hi), {4, 4, 1}, {4, 4, 3}, b->point(b->domain().hi)});
    for (int count : {0, 1, 2}) {
        const auto o = guides(g1, g2, count);
        const auto body = loftSheet({section(a), section(b)}, o);
        const auto &s = surface(body);
        const double jump = uJump(s);
        std::cout << " G1 rational=" << rational << " guides=" << count << " Ujump=" << jump << std::endl;
        require(jump < 1e-6, "giunzioni G1 con velocita' diverse non diventano creste U");
        regular(s, .05);
        for (const auto &c : {a, b})
            for (int k = 0; k <= 20; ++k) {
                const auto p = c->point(c->domain().hi * k / 20.);
                double nearest = 1e100;
                for (EdgeId id : body.edges()) {
                    const auto &e = body.edge(id);
                    nearest = std::min(nearest, projectPoint(*e.curve, p, e.range).distance);
                }
                require(nearest < 1e-9, "profili esatti preservati");
            }
    }
}
BSplineCurvePtr<3> nonuniformGuide(Vec3 at) {
    auto a = bezier({at, at + Vec3(-.5, 0, .5), at + Vec3(-1, 0, 1.5)});
    auto b = bezier({at + Vec3(-1, 0, 1.5), at + Vec3(-1.5, 0, 2.5), at + Vec3(0, 0, 4)});
    return std::make_shared<BSplineCurve<3>>(joinBezierPieces({*a, *b}, {0, 1, 5}));
}
void nonuniformGuides() {
    const auto a = bezier({{0, 0, 0}, {0, 1, 0}, {2, 2, 0}, {3, 2, 0}}),
               b = bezier({{0, 0, 4}, {0, 2, 4}, {3, 3, 4}, {4, 3, 4}});
    const auto g1 = nonuniformGuide({0, 0, 0});
    auto g2 = bezier({{3, 2, 0}, {5, 2, 1}, {5, 3, 3}, {4, 3, 4}});
    for (int count : {1, 2}) {
        const auto o = guides(g1, g2, count);
        const auto body = loftSheet({section(a), section(b)}, o);
        const auto &s = surface(body);
        const double jump = vJump(s);
        std::cout << " guide G1 count=" << count << " Vjump=" << jump << std::endl;
        require(jump < 1e-6, "guida G1 non genera una cresta V");
        regular(s, .1);
        if (count == 2) {
            auto swapped = o;
            std::reverse(swapped.guides.begin(), swapped.guides.end());
            std::reverse(swapped.guideConditions.begin(), swapped.guideConditions.end());
            const auto other = loftSheet({section(a), section(b)}, swapped);
            sameShape(s, surface(other), 4e-9);
            std::cout << std::endl;
        }
        for (int k = 0; k <= 20; ++k) {
            double nearest = 1e100;
            const auto p = g1->point(g1->domain().hi * k / 20.);
            for (EdgeId id : body.edges()) {
                const auto &e = body.edge(id);
                nearest = std::min(nearest, projectPoint(*e.curve, p, e.range).distance);
            }
            require(nearest < 4e-9, "guida esatta conservata entro budget loft");
        }
        const auto restored = readBodyBinary(writeBodyBinary(body));
        sameShape(s, surface(restored), 1e-12);
        std::cout << std::endl;
    }
}
void cornerPreserved() {
    auto a = joinedSection(0, false), b = joinedSection(4, true);
    auto p = rationalBezierPieces(*b, b->domain());
    auto poles = p.back().poles();
    poles[1] += Vec3(.1, -.1, 0);
    p.back() = *bezier(poles);
    b = std::make_shared<BSplineCurve<3>>(joinBezierPieces(p, {0, 1, 2}));
    Vec3 l[2], r[2];
    b->evaluateLeft(1, 1, l);
    b->evaluate(1, 1, r);
    const double wanted = angle(l[1], r[1]);
    require(wanted > .01, "fixture con corner reale");
    const auto body = loftSheet({section(a), section(b)}, LoftOptions{});
    const auto &s = surface(body);
    const auto iso = s.vIso(1);
    require(bool(iso), "profilo isoparametrico disponibile");
    double found = 0;
    for (double t : iso->breakpoints(iso->domain()))
        if (t > 0 && t < 1) {
            iso->evaluateLeft(t, 1, l);
            iso->evaluate(t, 1, r);
            found = std::max(found, angle(l[1], r[1]));
        }
    require(std::fabs(found - wanted) < 1e-10, "corner geometrico della sezione non arrotondato");
    std::cout << " corner=" << wanted << std::endl;
}
void threeGuideContinuity() {
    const auto line = [](Vec3 a, Vec3 b) -> PathSegment {
        return {std::make_shared<Line<3>>(a, normalized(b - a)), {0, distance(a, b)}};
    };
    LoftSection a, b;
    a.path = {line({0, 0, 0}, {10, 0, 0})};
    b.path = {line({0, 10, 0}, {10, 10, 0})};
    LoftOptions o;
    for (double x : {0., 5., 10.}) {
        auto c = bezier({{x, 0, 0}, {x, 3, x == 5 ? 6. : 2.}, {x, 7, x == 5 ? 6. : 2.}, {x, 10, 0}});
        o.guides.push_back({{c, c->domain()}});
        o.guideConditions.push_back({1, 1});
    }
    const auto body = loftSheet({a, b}, o);
    require(body.faces().size() == 2 && checkBody(body).empty(), "tre guide generano due facce valide");
    const auto &left = *body.face(body.faces()[0]).surface, &right = *body.face(body.faces()[1]).surface;
    for (int k = 1; k < 80; ++k) {
        double v = k / 80.;
        require(distance(left.point(1, v), right.point(0, v)) < 1e-10, "guida interna condivisa");
        require(angle(left.normal(1, v), right.normal(0, v)) < 1e-6,
                "nessuna cresta artificiale sulla guida intermedia");
    }
}
void perpendicularChord() {
    const auto a = bezier({{0, 0, 0}, {1, 0, 0}, {2, 1, 0}, {3, 1, 0}});
    const auto b = std::make_shared<TransformedCurve>(a, Transform3::translation({0, 10, 0}));
    LoftOptions o;
    o.startContinuity = o.endContinuity = 1;
    const auto body = loftSheet({section(a), section(b)}, o);
    const auto &s = surface(body);
    for (double u : {.1, .4, .8})
        for (double v : {0., 1.}) {
            Vec3 jet[4];
            s.evaluate(u, v, 1, jet);
            require(norm(jet[1]) > 1, "G1 conserva una derivata non nulla anche con corda perpendicolare");
            require(std::fabs(normalized(jet[1]).z()) > 1 - 1e-10, "G1 normale al piano della sezione");
            require(std::fabs(s.normal(u, v).z()) < 1e-9, "piano tangente corretto al bordo");
        }
}
void guideParameterInvariance() {
    const auto a = bezier({{0, 0, 0}, {0, 1, 0}, {2, 2, 0}, {3, 2, 0}}),
               b = bezier({{0, 0, 4}, {0, 2, 4}, {3, 3, 4}, {4, 3, 4}});
    const auto guide = nonuniformGuide({0, 0, 0});
    const auto pieces = rationalBezierPieces(*guide, guide->domain());
    const auto reparameterized = std::make_shared<BSplineCurve<3>>(joinBezierPieces(pieces, {0, 1, 2}));
    const auto o = guides(guide, guide, 1);
    const auto body = loftSheet({section(a), section(b)}, o);
    auto other = o;
    other.guides = {{{reparameterized, reparameterized->domain()}}};
    const auto changed = loftSheet({section(a), section(b)}, other);
    sameShape(surface(body), surface(changed), 4e-9);
    std::cout << std::endl;
    auto reversed = other;
    const auto c = reversedCurve<3>(reparameterized);
    reversed.guides = {{{c, c->domain()}}};
    const auto reverseBody = loftSheet({section(a), section(b)}, reversed);
    sameShape(surface(body), surface(reverseBody), 4e-9);
    std::cout << std::endl;
}

void reverseProfileNormals() {
    const auto a=bezier({{0,0,0},{2,2,0},{4,0,0}}), b=bezier({{2,0,5},{4,2,5},{6,0,5}});
    LoftOptions options;options.startContinuity=options.endContinuity=1;
    const auto baseline=loftSheet({section(a),section(b)},options);
    for(int continuity:{1,2})for(bool reverseStart:{false,true})for(bool reverseEnd:{false,true}) {
        options.startContinuity=options.endContinuity=continuity;
        options.startReverse=reverseStart;options.endReverse=reverseEnd;
        const auto body=loftSheet({section(a),section(b)},options);
        const auto &s=surface(body), &base=surface(baseline);
        for(double u:{.1,.4,.8})for(double v:{0.,1.}) {
            Vec3 q[9],original[4];s.evaluate(u,v,2,q);base.evaluate(u,v,1,original);
            require(distance(q[0],original[0])<1e-9,"inversione mantiene il profilo esatto");
            const bool reverse=v==0?reverseStart:reverseEnd;
            require(dot(normalized(q[Surface::derivativeIndex(0,1,2)]), normalized(original[Surface::derivativeIndex(0,1,1)]))*(reverse?-1:1)>.999999,
                    "direzione G1/G2 invertita soltanto al profilo scelto");
        }
        const auto ruled=ruledSurface({{a,a->domain()}},{{b,b->domain()}},options);
        require(checkBody(ruled).empty(),"inversione disponibile anche sulla superficie tra due profili");
        options.startContinuity=options.endContinuity=0;
        const auto free=loftSheet({section(a),section(b)},options);
        options.startReverse=options.endReverse=false;
        const auto unchanged=loftSheet({section(a),section(b)},options);
        sameShape(surface(free),surface(unchanged),1e-10);
    }
}

} // namespace
int main() {
    int passed = 0, failed = 0;
    std::cout.precision(12);
    const auto run = [&](const char *name, auto test) {
        std::cout << "CASE " << name << std::endl;
        try {
            test();
            ++passed;
        } catch (const std::exception &e) {
            std::cerr << "FAIL " << name << ": " << e.what() << std::endl;
            ++failed;
        }
    };
    run("polynomial-knot-invariance", [] { knotInvariance(false); });
    run("rational-knot-invariance", [] { knotInvariance(true); });
    run("polynomial-nonuniform-G1-sections", [] { nonuniformSections(false); });
    run("rational-nonuniform-G1-sections", [] { nonuniformSections(true); });
    run("nonuniform-G1-guides", [] { nonuniformGuides(); });
    run("source-corner", [] { cornerPreserved(); });
    run("three-guide-continuity", [] { threeGuideContinuity(); });
    run("perpendicular-G1-chord", [] { perpendicularChord(); });
    run("reverse-profile-normals", [] { reverseProfileNormals(); });
    run("guide-parameter-invariance", [] { guideParameterInvariance(); });
    std::cout << passed << "/" << (passed + failed) << " gruppi di regressione superati" << std::endl;
    return failed ? 1 : 0;
}
