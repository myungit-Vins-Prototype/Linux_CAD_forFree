#include "fk_blend_contact_breaks.h"
#include "fk_bspline_surface.h"
#include "fk_test.h"
#include "fk_blend.h"
#include "fk_body_io.h"
#include "fk_body_check.h"
#include "fk_history.h"
#include "fk_offset.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace ForgeCad::Kernel;

namespace {
BSplineSurface contactSupport(bool crease = false, bool swap = false) {
    const std::vector<double> u{0,0,0,0,0.5,0.5,0.5,1,1,1,1}, v{0,0,1,1};
    const double x[]{0,1,2,3,6,9,12}, z[]{0,0,1,2,5,7,10};
    std::vector<Vec3> poles;
    for (int i = 0; i < 7; ++i)
        for (double y : {0.,10.}) poles.emplace_back(x[i], y, z[i] + (crease && i > 3 ? 2.0 : 0.0));
    if (!swap) return BSplineSurface(3,1,u,v,7,2,poles);
    std::vector<Vec3> transposed;
    for (int j = 0; j < 2; ++j) for (int i = 0; i < 7; ++i) transposed.push_back(poles[2*i+j]);
    return BSplineSurface(1,3,v,u,2,7,transposed);
}
}

FK_TEST(BlendContactKnotCrossingsUseContactParameters) {
    const Plane plane(Frame3{});
    for (bool reverse : {false,true}) for (bool swap : {false,true}) for (int side : {0,1}) {
        const auto support = contactSupport(false, swap);
        const auto sample = [&](double t) {
            detail::BlendContactSample at{{0.3,0.3,0.3,0.3},{0,0,0,0}};
            const int coordinate = 2*side + int(swap);
            at.uv[coordinate] = reverse ? 0.8-0.7*t : 0.1+0.7*t;
            at.derivative[coordinate] = reverse ? -0.7 : 0.7;
            return at;
        };
        const auto roots = detail::blendContactBreaks(side ? plane : static_cast<const Surface &>(support),
            side ? static_cast<const Surface &>(support) : plane, {0,1}, {0,0.25,1}, sample);
        FK_CHECK(roots.size() == 1);
        if (!roots.empty()) FK_CHECK_NEAR(roots[0], reverse ? 3./7. : 4./7., 1e-13);
    }
}

// Un evento sul limite del fit non aggiunge una campata, ma serve a usare
// la derivata dal lato interno: quella del tratto opposto crea micro-pezze.
FK_TEST(BlendContactKnotAtFitEndpoints) {
    const auto surface = contactSupport();
    const Plane plane(Frame3{});
    for (bool atStart : {false, true}) for (bool reverse : {false, true}) {
        const double endpoint = atStart ? 0.0 : 1.0;
        const double direction = reverse ? -0.3 : 0.3;
        const auto sample = [&](double t) {
            return detail::BlendContactSample{{0.5 + direction * (t-endpoint),0.3,0.3,0.3},
                                               {direction,0,0,0}};
        };
        const auto roots = detail::blendContactBreaks(surface,plane,{0,1},{0,1},sample);
        FK_CHECK(roots.size()==1);
        if (!roots.empty()) FK_CHECK_NEAR(roots[0],endpoint,1e-14);
    }
}

FK_TEST(BlendContactKnotAtExistingEdgeBreakUsesOneParameter) {
    const auto surface = contactSupport();
    const Plane plane(Frame3{});
    const auto sample=[](double t){return detail::BlendContactSample{{t,0.3,0.3,0.3},{1,0,0,0}};};
    for(double direction:{0.,1.}) {
        const double node=std::nextafter(.5,direction);
        const auto roots=detail::blendContactBreaks(surface,plane,{0,1},{0,node,1},sample);
        FK_CHECK(roots.size()==1);
        if(!roots.empty()) FK_CHECK(roots[0]==node);
    }
    const auto distinct=detail::blendContactBreaks(surface,plane,{0,1},{0,.5+1e-10,1},sample);
    FK_CHECK(distinct.size()==1);
    if(!distinct.empty()) FK_CHECK_NEAR(distinct[0],.5,1e-14);
}

FK_TEST(BlendContactKnotOnProbeAndRealNormalJump) {
    const Plane plane(Frame3{});
    const auto sample = [](double t) { return detail::BlendContactSample{{t,0.3,0.3,0.3},{1,0,0,0}}; };
    const auto smooth = contactSupport();
    const auto roots = detail::blendContactBreaks(smooth, plane, {0,1}, {0,1}, sample);
    FK_CHECK(roots.size() == 1);
    if (!roots.empty()) FK_CHECK_NEAR(roots[0], 0.5, 1e-14);
    FK_CHECK(detail::blendContactBreaks(smooth, plane, {0,1}, {0,1}, [](double) {
        return detail::BlendContactSample{{0.5,0.3,0.3,0.3},{0,0,0,0}};
    }).empty());
    const auto sharp = contactSupport(true);
    FK_CHECK_THROWS(detail::blendContactBreaks(sharp, plane, {0,1}, {0,1}, sample));
    int calls = 0;
    FK_CHECK(detail::blendContactBreaks(plane, plane, {0,1}, {0,1}, [&](double t) {
        ++calls; return sample(t);
    }).empty());
    FK_CHECK(calls == 0);
}

// Prisma inclinato z'=z+0,1x, profilo cubico a due tratti G1 con velocita'
// diverse al nodo 0,5. Il contatto R2 attraversa il nodo a un parametro del
// bordo diverso da 0,5. Prima del fix il fit generava una curvatura 11,14/mm
// (raggio spurio 0,09 mm) e l'offset interno di 1 mm diventava impossibile.
FK_TEST(BlendContactKnotKeepsRadiusAndAllowsOneMillimeterOffset) {
    std::ifstream file(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/blend_contact_knot.body", std::ios::binary);
    FK_CHECK(bool(file));
    std::stringstream bytes; bytes << file.rdbuf();
    for (double perturbation : {0.,1e-7}) {
    Body input = readBodyBinary(bytes.str());
    if (perturbation) for (auto f : input.faces()) if (input.face(f).surface->type()==SurfaceType::BSpline) {
        const auto &s=static_cast<const BSplineSurface &>(*input.face(f).surface);
        std::vector<Vec3> poles;
        for(int u=0;u<s.uPoleCount();++u)for(int v=0;v<s.vPoleCount();++v)
            poles.push_back(s.pole(u,v)+(u==4?Vec3(0,0,perturbation):Vec3()));
        input.face(f).surface=std::make_shared<BSplineSurface>(s.uDegree(),s.vDegree(),s.uKnots(),s.vKnots(),s.uPoleCount(),s.vPoleCount(),poles);
    }
    const auto original = writeBodyBinary(input);
    FK_CHECK(checkBody(input, {true,true}).empty());
    std::vector<EdgeId> chosen;
    for (FaceId f : input.faces()) {
        const auto &face = input.face(f);
        if (face.surface->type() != SurfaceType::Plane
            || projectPoint(*face.surface, Vec3(15,10,26.5)).distance > 1e-9) continue;
        for (LoopId l : face.loops) for (FinId fin : input.loopFins(l)) chosen.push_back(input.fin(fin).edge);
    }
    FK_CHECK(chosen.size() == 4);
    BodyHistory history;
    const Body result = blendEdges(input, chosen, 2.0, false, &history);
    FK_CHECK(checkBody(result, {true,true}).empty());
    int freeformFillets = 0;
    for (FaceId f : result.faces()) {
        const auto &face = result.face(f);
        if (face.surface->type() != SurfaceType::BSpline) continue;
        bool generated = false;
        for (const auto &link : history.origins(EntityKind::Face, f.index))
            generated = generated || (link.kind == EntityKind::Edge && link.relation == HistoryRelation::Generated);
        if (!generated) continue;
        ++freeformFillets;
        const auto &s = *face.surface;
        const auto us = s.uBreakpoints(s.uDomain());
        double worst = 0.0;
        for (std::size_t k = 1; k < us.size(); ++k)
            for (double fraction : {0.125,0.5,0.875}) for (double v : {0.125,0.5,0.875}) {
                const double u = us[k-1] + fraction*(us[k]-us[k-1]);
                Vec3 jet[9]; s.evaluate(u,v,2,jet);
                const auto at = [&](int a, int b) { return jet[Surface::derivativeIndex(a,b,2)]; };
                const auto su = at(1,0), sv = at(0,1), n = normalized(cross(su,sv));
                const double E = dot(su,su), F = dot(su,sv), G = dot(sv,sv);
                const double L = dot(n,at(2,0)), M = dot(n,at(1,1)), N = dot(n,at(0,2));
                const double area = E*G-F*F;
                const double mean = (L*G-2*M*F+N*E)/(2*area), gauss = (L*N-M*M)/area;
                const double root = std::sqrt(std::max(0.,mean*mean-gauss));
                const double direction = face.sense ? -1. : 1.;
                worst = std::max({worst,direction*(mean+root),direction*(mean-root)});
            }
        FK_CHECK_NEAR(worst, 0.5, 1e-7);
        const double d = face.sense ? -1. : 1.;
        const auto offset = offsetSurface(s,d,s.uDomain(),s.vDomain(),1e-7);
        for (double fraction : {0.1,0.4,0.7,0.9}) {
            const double u = s.uDomain().lo + fraction*s.uDomain().length();
            FK_CHECK_NEAR(distance(offset->point(u,0.5),s.point(u,0.5)+d*s.normal(u,0.5)),0.,2e-7);
        }
    }
    FK_CHECK(freeformFillets >= 1);
    FK_CHECK(writeBodyBinary(input) == original);
    }
}
