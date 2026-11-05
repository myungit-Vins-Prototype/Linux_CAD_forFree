#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_loft.h"
#include "fk_mass.h"
#include "fk_nurbs.h"
#include "fk_test.h"
#include "fk_tessellate.h"
#include "fk_step.h"
#include "fk_surface_algo.h"
#include <stdexcept>
#include <algorithm>
using namespace ForgeCad::Kernel;
namespace {
LoftSection lineAt(double z) {
    LoftSection s;
    s.path = {{std::make_shared<Line<3>>(Vec3(0, 0, z), Vec3(1, 0, 0)), {0, 10}}};
    return s;
}
LoftSection circleAt(double z) {
    LoftSection s;
    s.path = {
        {std::make_shared<Circle<3>>(makeCircle(Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5)), {0, kTwoPi}}};
    return s;
}
bool edgeConnects(const Body &body, Vec3 a, Vec3 b) {
    for (auto id : body.edges()) {
        const auto &edge = body.edge(id);
        auto p = edge.curve->point(edge.range.lo), q = edge.curve->point(edge.range.hi);
        if ((distance(a, p) < 1e-7 && distance(b, q) < 1e-7) || (distance(a, q) < 1e-7 && distance(b, p) < 1e-7))
            return true;
    }
    return false;
}
} // namespace
FK_TEST(LoftCorrespondenceOpenInterior) {
    std::vector<LoftSection> sections{lineAt(0), lineAt(10)};
    auto curve = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                   std::vector<Vec3>{{0, 0, 10}, {3, 4, 10}, {7, 4, 10}, {10, 0, 10}});
    sections[1].path = {{curve, {0, 1}}};
    const Vec3 top = loftSectionPoint(sections[1], .7);
    LoftOptions options;
    options.correspondencePoints = {{{0, 0, 0}, {2, 0, 0}, {10, 0, 0}}, {{0, 0, 10}, top, {10, 0, 10}}};
    const auto body = loftSheet(sections, options);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK(edgeConnects(body, {2, 0, 0}, top));
    // Stessi profili, diversa corrispondenza interna, nessuna guida artificiale.
    for (int r = 0; r < 2; ++r)
        for (int i = 0; i <= 20; ++i) {
            const Vec3 p = loftSectionPoint(sections[r], i / 20.);
            double nearest = 1e300;
            for (auto e : body.edges())
                nearest = std::min(nearest, projectPoint(*body.edge(e).curve, p, body.edge(e).range).distance);
            FK_CHECK(nearest < 1e-8);
        }
    auto invalid = options;
    std::swap(invalid.correspondencePoints[1][1], invalid.correspondencePoints[1][2]);
    bool rejected = false;
    try {
        loftSheet(sections, invalid);
    } catch (const std::domain_error &) {
        rejected = true;
    }
    FK_CHECK(rejected);
}
FK_TEST(LoftCorrespondenceClosedSeamAndGuides) {
    const std::vector<LoftSection> sections{circleAt(0), circleAt(10)};
    LoftOptions options;
    options.correspondencePoints = {{{0, 5, 0}}, {{0, 5, 10}}};
    auto body = loftSolid(sections, options);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK(edgeConnects(body, {0, 5, 0}, {0, 5, 10}));
    FK_CHECK(std::abs(massProperties(body).volume - 250 * kPi) < 1e-6);
    // Una guida reale diversa dalla cucitura deve restare vincolante.
    options.guides = {{{std::make_shared<Line<3>>(Vec3(5, 0, 0), Vec3(0, 0, 1)), {0, 10}}}};
    body = loftSheet(sections, options);
    FK_CHECK(checkBody(body).empty());
    FK_CHECK(edgeConnects(body, {5, 0, 0}, {5, 0, 10}));
    options.correspondencePoints[1][0] = {5, 0, 10};
    bool rejected = false;
    try {
        loftSheet(sections, options);
    } catch (const std::domain_error &) {
        rejected = true;
    }
    FK_CHECK(rejected);
}
FK_TEST(LoftCorrespondenceReverseAndFractions) {
    const std::vector<LoftSection> sections{lineAt(0), lineAt(10)};
    LoftOptions options;
    options.reverseSections = {false, true};
    std::vector<bool> reverse;
    const auto defaults = loftCorrespondenceDefaults(sections, options, &reverse);
    FK_CHECK(!reverse[0] && reverse[1]);
    FK_CHECK(distance(defaults[1][0], Vec3(10, 0, 10)) < 1e-9);
    options.correspondencePoints = defaults;
    const auto body = loftSheet(sections, options);
    FK_CHECK(edgeConnects(body, {0, 0, 0}, {10, 0, 10}));
    auto curve = std::make_shared<BSplineCurve<3>>(3, std::vector<double>{0, 0, 0, 0, 1, 1, 1, 1},
                                                   std::vector<Vec3>{{0, 0, 0}, {2, 3, 4}, {5, -2, 1}, {10, 1, 2}});
    LoftSection spatial;
    spatial.path = {{curve, {0, 1}}};
    for (double f : {0., 0.13, 0.5, 0.82, 1.})
        FK_CHECK(std::abs(loftSectionFraction(spatial, loftSectionPoint(spatial, f)) - f) < 1e-8);
    auto invalid = options;
    invalid.correspondencePoints.pop_back();
    bool rejected = false;
    try {
        loftSheet(sections, invalid);
    } catch (const std::domain_error &) {
        rejected = true;
    }
    FK_CHECK(rejected);
}

FK_TEST(LoftCorrespondencePeriodicNearOriginalSeam) {
    std::vector<LoftSection> sections{circleAt(0), circleAt(10), circleAt(20)};
    LoftOptions options;
    for (int i = 0; i < 3; ++i) {
        const double start = i == 1 ? .25 : i == 2 ? 4.88623e-9 : 0;
        options.correspondencePoints.push_back(
            {loftSectionPoint(sections[i], start), loftSectionPoint(sections[i], start + .5)});
    }
    const auto body = loftSheet(sections, options);
    FK_CHECK(checkBody(body).empty());
}

FK_TEST(LoftCorrespondenceDuplicateGuidesRejected) {
    const std::vector<LoftSection> sections{lineAt(0),lineAt(10)};
    const std::vector<PathSegment> guide{{std::make_shared<Line<3>>(Vec3(0,0,0),Vec3(0,0,1)),{0,10}}};
    LoftOptions options; options.guides={guide,guide};
    bool rejected=false;try{loftSheet(sections,options);}catch(const std::domain_error&){rejected=true;}FK_CHECK(rejected);
    options.guides={guide};options.correspondencePoints=loftCorrespondenceDefaults(sections);
    FK_CHECK(checkBody(loftSheet(sections,options)).empty());
    options.correspondencePoints[0].push_back(options.correspondencePoints[0].front());
    options.correspondencePoints[1].push_back(options.correspondencePoints[1].front());
    rejected=false;try{loftSheet(sections,options);}catch(const std::domain_error&){rejected=true;}FK_CHECK(rejected);
}

FK_TEST(LoftCorrespondenceClosedNurbsNearOriginalSeam) {
    std::vector<LoftSection> sections;
    LoftOptions options;
    for(int i=0;i<3;++i) {
        const auto circle=makeCircle(Frame3(Vec3(0,0,10*i),Vec3(0,0,1),Vec3(1,0,0)),5+i);
        auto curve=std::make_shared<BSplineCurve<3>>(joinBezierPieces(rationalBezierPieces(circle,{0,kTwoPi})));
        LoftSection section;section.path={{curve,curve->domain()}};sections.push_back(section);
        const double start=i==1?.25:i==2?4.88623e-9:0;
        options.correspondencePoints.push_back({loftSectionPoint(section,start),loftSectionPoint(section,start+.5)});
    }
    const auto body=loftSheet(sections,options);FK_CHECK(checkBody(body).empty());
    // Il cambio di cucitura non muove i profili razionali agli estremi.
    for(int r:{0,2})for(int j=0;j<=40;++j){
        const Vec3 p=loftSectionPoint(sections[r],j/40.);double nearest=1e300;
        for(auto id:body.edges()){const auto &e=body.edge(id);nearest=std::min(nearest,projectPoint(*e.curve,p,e.range).distance);}
        FK_CHECK(nearest<1e-9);
    }
}

FK_TEST(LoftCorrespondenceEndpointsMustMatchDirection) {
    const std::vector<LoftSection> sections{lineAt(0),lineAt(10)};
    LoftOptions options;options.correspondencePoints=loftCorrespondenceDefaults(sections);
    std::swap(options.correspondencePoints[1][0],options.correspondencePoints[1][1]);
    bool rejected=false;try{loftSheet(sections,options);}catch(const std::domain_error&){rejected=true;}FK_CHECK(rejected);
    options.reverseSections={false,true};FK_CHECK(checkBody(loftSheet(sections,options)).empty());
    const std::vector<LoftSection> closed{circleAt(0),circleAt(10)};
    options={};options.correspondencePoints={{{5,0,0},{5,0,0}},{{5,0,10},{5,0,10}}};
    rejected=false;try{loftSheet(closed,options);}catch(const std::domain_error&){rejected=true;}FK_CHECK(rejected);
}

FK_TEST(LoftGuidedReturningPathOrientation) {
    // Tre sezioni su un gancio: l'ultima campata torna oltre mezzo giro.
    // La corda fra centri ha verso opposto alla tangente locale della guida.
    std::vector<LoftSection> sections;
    for(double angle : {0.0, kPi/3, 5*kPi/3}) {
        const Vec3 radial(std::cos(angle),0,std::sin(angle));
        const Frame3 frame(10*radial,Vec3(-std::sin(angle),0,std::cos(angle)),radial);
        LoftSection section;
        auto circle=std::make_shared<Circle<3>>(makeCircle(frame,2));
        for(int k=0;k<4;++k) section.path.push_back({circle,{k*kPi/2,(k+1)*kPi/2}});
        sections.push_back(section);
    }
    LoftOptions options;
    for(double radius : {12.0,8.0})
        options.guides.push_back({{std::make_shared<Circle<3>>(makeCircle(Frame3(Vec3(),Vec3(0,-1,0),Vec3(1,0,0)),radius)),{0,5*kPi/3}}});
    for(bool backwards : {false,true}) {
        if(backwards) std::reverse(sections.begin(),sections.end());
        std::vector<bool> reversed;
        options.correspondencePoints.clear();
        auto defaults=loftCorrespondenceDefaults(sections,options,&reversed);
        for(bool value : reversed) FK_CHECK(value==backwards);
        // Anche i controlli inizializzati dall'editor devono usare lo stesso verso.
        options.correspondencePoints=defaults;
        const auto body=loftSheet(sections,options);
        FK_CHECK(checkBody(body).empty());
        for(auto id : body.edges()) {
            const auto &edge=body.edge(id);
            if(std::fabs(std::fabs(edge.curve->point(edge.range.lo).y())-2)>1e-7
               || std::fabs(std::fabs(edge.curve->point(edge.range.hi).y())-2)>1e-7) continue;
            for(int j=1;j<40;++j) {
                const auto p=edge.curve->point(edge.range.lo+edge.range.length()*j/40.);
                FK_CHECK(std::hypot(p.x(),p.z())>7.9); // nessun bordo taglia l'interno del gancio
            }
        }
        for(auto id : body.faces()) {
            const auto &surface=*body.face(id).surface;
            const auto u=surface.uDomain(),v=surface.vDomain();
            const double middle=(u.lo+u.hi)/2;
            const double side=surface.point(middle,v.lo).y();
            if(std::fabs(side)<1e-8) continue;
            for(int j=1;j<=40;++j)
                FK_CHECK(surface.point(middle,v.lo+v.length()*j/40.).y()*side>0);
            // La superficie non deve ripiegarsi fra i tagli intermedi del
            // profilo: controllare anche il verso, non soltanto i bordi.
            for(int i=1;i<16;++i)for(int j=0;j<=40;++j) {
                const double atU=u.lo+u.length()*i/16.,atV=v.lo+v.length()*j/40.;
                const Vec3 p=surface.point(atU,atV),normal=normalAt(surface,atU,atV);
                FK_CHECK(normal.y()*p.y()>0);
            }
        }
    }
}

FK_TEST(LoftMergeTangentFacesPreservesShapeAndLabels) {
    std::vector<LoftSection> sections;
    for(int i=0;i<2;++i){
        LoftSection s;auto c=std::make_shared<Circle<3>>(makeCircle(Frame3(Vec3(0,0,10*i),Vec3(0,0,1),Vec3(1,0,0)),5+i));
        for(int k=0;k<4;++k)s.path.push_back({c,{k*kPi/2,(k+1)*kPi/2}});
        sections.push_back(s);
    }
    LoftOptions options;
    auto original=loftSheet(sections,options);
    options.mergeTangentFaces=true;
    FaceAliases labels;const auto merged=loftSheet(sections,options,&labels);
    FK_CHECK(original.faces().size()==4);
    FK_CHECK(merged.faces().size()==1);
    FK_CHECK(checkBody(merged).empty());
    FK_CHECK(labels.size()==1 && labels[0].size()==8);
    const auto &joined=*merged.face(merged.faces()[0]).surface;
    for(int k=0;k<4;++k){const auto &surface=*original.face(original.faces()[k]).surface;
        for(int i=0;i<=8;++i)for(int j=0;j<=8;++j)
            FK_CHECK(distance(surface.point(i/8.,j/8.),joined.point(k+i/8.,j/8.))<1e-10);
    }
    const auto mesh=tessellate(merged,{});
    FK_CHECK(mesh.failedFaces==0 && mesh.faces.size()==1 && !mesh.faces[0].triangles.empty());
    // La faccia unica resta leggibile nello STEP, inclusa la cucitura di parametro.
    ExchangeBody item;item.body=merged;
    const auto step=writeStep({item});
    const auto read=readStep(step);
    FK_CHECK(read.bodies.size()==1);
    const auto &reopened=read.bodies.at(0).body;
    FK_CHECK(reopened.faces().size()==1 && checkBody(reopened).empty());
    FK_CHECK(tessellate(reopened,{}).failedFaces==0);
    const auto solid=loftSolid(sections,options);
    FK_CHECK(solid.faces().size()==3 && checkBody(solid).empty());
    options.mergeTangentFaces=false;
    const auto oldSolid=loftSolid(sections,options);
    FK_CHECK(std::fabs(massProperties(solid).volume-massProperties(oldSolid).volume)<1e-7);
}

FK_TEST(LoftMergePreservesRealCorners) {
    std::vector<LoftSection> sections;
    for(int i=0;i<2;++i){
        LoftSection section;
        const Frame3 frame(Vec3(0,0,10*i),Vec3(0,0,1),Vec3(1,0,0));
        const Frame3 second(Vec3(5,5,10*i),Vec3(0,0,1),Vec3(1,0,0));
        section.path={{std::make_shared<Circle<3>>(makeCircle(frame,5)),{0,kPi/2}},
                      {std::make_shared<Circle<3>>(makeCircle(second,5)),{kPi,5*kPi/4}}};
        sections.push_back(section);
    }
    LoftOptions options;options.mergeTangentFaces=true;
    const auto body=loftSheet(sections,options);
    FK_CHECK(body.faces().size()==2 && checkBody(body).empty());
}

FK_TEST(LoftIncompatibleContactIdentifiesGuideAndSection) {
    std::vector<LoftSection> sections{circleAt(0),circleAt(10)};
    auto top=std::make_shared<Circle<3>>(makeCircle(Frame3(Vec3(0,0,10),Vec3(0,0,1),Vec3(1,0,0)),6));
    sections[1].path={{top,{0,kTwoPi}}};
    auto beyond=sections[1];beyond.path={{std::make_shared<Circle<3>>(makeCircle(Frame3(Vec3(0,0,20),Vec3(0,0,1),Vec3(1,0,0)),6)),{0,kTwoPi}}};
    auto support=std::make_shared<Body>(loftSheet({sections[1],beyond}));
    LoftOptions options;options.endContinuity=1;
    for(auto face:support->faces())options.endFaces.push_back({support,face});
    options.guides={{{std::make_shared<Line<3>>(Vec3(5,0,0),Vec3(1,0,10)),{0,std::sqrt(101.)}}}};
    bool rejected=false;try{loftSheet(sections,options);}catch(const std::domain_error&e){const std::string text=e.what();rejected=text.find("loft-guide=0")!=std::string::npos&&text.find("loft-section=1")!=std::string::npos&&text.find("5.710593")!=std::string::npos;}
    FK_CHECK(rejected);
}
