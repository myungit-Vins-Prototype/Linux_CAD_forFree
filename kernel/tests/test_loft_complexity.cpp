#include "fk_test.h"
#include "fk_loft.h"
#include "fk_bspline_surface.h"
#include "fk_curve_algo.h"
#include "fk_body_check.h"
#include <algorithm>
using namespace ForgeCad::Kernel;

FK_TEST(LoftMixedProfilesPreserveSectionsGuidesAndSmoothKnots) {
    // A quintic section with unequal speeds at its join and a rational conic:
    // parameter normalization is needed. Smooth guide knots must remain C2,
    // preserving both the rational sections and the full guide trajectories.
    const std::vector<double> knots{0,0,0,0,0,0,.5,.5,.5,.5,.5,1,1,1,1,1,1};
    auto first = std::make_shared<BSplineCurve<3>>(5, knots, std::vector<Vec3>{
        {-5,0,0},{-4,0,0},{-3,.5,0},{-2,1,0},{-1,1,0},{0,1,0},
        {2,1,0},{3,.5,0},{4,.1,0},{4.5,0,0},{5,0,0}});
    auto last = std::make_shared<Circle<3>>(Vec3(0,0,10),Vec3(1,0,0),Vec3(0,1,0),5);
    LoftSection a,b; a.path={{first,{0,1}}}; b.path={{last,{kPi,kTwoPi}}};
    for (int continuity : {1,2}) {
        LoftOptions options; options.guideContinuity=continuity;
        for (double x : {-5.,5.}) {
            auto guide=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
                std::vector<Vec3>{{x,0,0},{x,2,3},{x,1,7},{x,0,10}});
            options.guides.push_back({{guide,{0,1}}});
        }
        const Body body=loftSheet({a,b},options);
        FK_CHECK(checkBody(body).empty());
        FK_CHECK(body.faces().size()==1);
        const auto &s=static_cast<const BSplineSurface&>(*body.face(body.faces().front()).surface);
        bool smooth=false;
        for (double v:s.vBreakpoints(s.vDomain())) {
            if(v==s.vDomain().lo||v==s.vDomain().hi)continue;
            const int mult=int(std::count(s.vKnots().begin(),s.vKnots().end(),v));
            FK_CHECK(mult<=5); smooth=smooth||mult==3;
        }
        FK_CHECK(smooth);
        for (int k=0;k<=80;++k) {
            const double u=k/80.;
            FK_CHECK(projectPoint(*first,s.point(u,0),first->domain()).distance<1e-9);
            FK_CHECK(projectPoint(*last,s.point(u,1),{kPi,kTwoPi}).distance<1e-9);
            for(int side=0;side<2;++side) {
                const auto &guide=options.guides[side].front();
                FK_CHECK(projectPoint(*guide.curve,s.point(side,u),guide.range).distance<4e-9);
            }
        }
    }
}

FK_TEST(LoftGridReductionPreservesActualGuideCorners) {
    const auto line=[](Vec3 a,Vec3 b)->PathSegment {
        return {std::make_shared<Line<3>>(a,normalized(b-a)),{0,distance(a,b)}};
    };
    LoftSection a,b;
    a.path={line({0,0,0},{10,0,0})}; b.path={line({0,0,10},{10,0,10})};
    LoftOptions options;
    const Vec3 corner(0,2,5);
    options.guides.push_back({line({0,0,0},corner),line(corner,{0,0,10})});
    auto smooth=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
        std::vector<Vec3>{{10,0,0},{10,1,3},{10,1,7},{10,0,10}});
    options.guides.push_back({{smooth,{0,1}}});
    const auto body=loftSheet({a,b},options);
    FK_CHECK(checkBody(body).empty());
    const auto &s=static_cast<const BSplineSurface&>(*body.face(body.faces().front()).surface);
    const auto boundary=s.uIsoCurve(0);
    bool found=false;
    for(double v:boundary.breakpoints(boundary.domain())) {
        if(distance(boundary.point(v),corner)>1e-9)continue;
        Vec3 left[2],right[2];boundary.evaluateLeft(v,1,left);boundary.evaluate(v,1,right);
        FK_CHECK(norm(cross(normalized(left[1]),normalized(right[1])))>.1);
        found=true;
    }
    FK_CHECK(found);
}
