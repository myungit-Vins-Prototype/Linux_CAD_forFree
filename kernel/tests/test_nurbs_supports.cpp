#include "fk_test.h"
#include "fk_blend.h"
#include "fk_blend_model.h"
#include "fk_body_check.h"
#include "fk_body_io.h"
#include "fk_curve_algo.h"
#include "fk_history.h"
#include "fk_nurbs_join.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_transform.h"
#include "fk_unify.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

using namespace ForgeCad::Kernel;
namespace {
BSplineSurface chart(bool curved, double crease=0.0, bool rational=false) {
    const std::vector<double> v{0,0,1,1};
    if (!curved) return BSplineSurface(1,1,{0,0,1,1},v,2,2,{{-1,0,0},{-1,1,0},{0,0,0},{0,1,0}});
    return BSplineSurface(2,1,{0,0,0,1,1,1},v,3,2,
        {{0,0,0},{0,1,0},{.5,0,crease},{.5,1,crease},{1,0,1},{1,1,1}},
        rational?std::vector<double>{1,1,.8,.8,1,1}:std::vector<double>{});
}
BSplineSurface reorient(const BSplineSurface&s,bool swap,bool ru,bool rv) {
    auto u=swap?s.vKnots():s.uKnots(),v=swap?s.uKnots():s.vKnots();
    const auto ud=swap?s.vDomain():s.uDomain(),vd=swap?s.uDomain():s.vDomain();
    if(ru){std::reverse(u.begin(),u.end());for(double&t:u)t=ud.lo+ud.hi-t;}
    if(rv){std::reverse(v.begin(),v.end());for(double&t:v)t=vd.lo+vd.hi-t;}
    const int nu=swap?s.vPoleCount():s.uPoleCount(),nv=swap?s.uPoleCount():s.vPoleCount();
    std::vector<Vec3>p;std::vector<double>w;
    for(int i=0;i<nu;++i)for(int j=0;j<nv;++j){int a=ru?nu-1-i:i,b=rv?nv-1-j:j;p.push_back(swap?s.pole(b,a):s.pole(a,b));w.push_back(swap?s.weight(b,a):s.weight(a,b));}
    return BSplineSurface(swap?s.vDegree():s.uDegree(),swap?s.uDegree():s.vDegree(),u,v,nu,nv,p,w);
}
Body adjacentCharts() {
    auto a=std::make_shared<BSplineSurface>(chart(false)),b=std::make_shared<BSplineSurface>(chart(true));
    const std::vector<Vec3>p{{-1,0,0},{0,0,0},{0,1,0},{-1,1,0},{1,0,1},{1,1,1}};
    std::vector<Body::BuildEdge> edges;
    const auto line=[&](int i,int j){edges.push_back({i,j,std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,1,1},std::vector<Vec3>{p[std::size_t(i)],p[std::size_t(j)]}),{0,1},0.0});};
    line(0,1);line(1,2);line(2,3);line(3,0);
    edges.push_back({1,4,std::make_shared<BSplineCurve<3>>(b->vIsoCurve(0)),{0,1},0.0});
    line(4,5);
    edges.push_back({2,5,std::make_shared<BSplineCurve<3>>(b->vIsoCurve(1)),{0,1},0.0});
    Body body=Body::buildSheet(p,edges,{{a,true,{{{0,true,{},0.0},{1,true,{},0.0},{2,true,{},0.0},{3,true,{},0.0}}}},
                                      {b,true,{{{4,true,{},0.0},{5,true,{},0.0},{6,false,{},0.0},{1,false,{},0.0}}}}});
    computePCurves(body);
    body.vertex(body.edgeStart(EdgeId(0))).tolerance=3e-8;
    return body;
}
Body sweepFixture() {
    std::ifstream in(std::string(FORGECAD_SOURCE_DIR)+"/kernel/tests/data/blend_tangent_sweep.body",std::ios::binary);
    if(!in)throw std::runtime_error("fixture supporti raccordo mancante");
    return readBodyBinary(std::string(std::istreambuf_iterator<char>(in),{}));
}
void checkCircularSections(const Body &body,const BodyHistory&history,double radius) {
    int generated=0;
    for(FaceId f:body.faces()) {
        bool blend=false;
        for(auto&link:history.origins(EntityKind::Face,f.index))blend=blend||(link.kind==EntityKind::Edge&&link.relation==HistoryRelation::Generated);
        if(!blend)continue;
        ++generated;
        const auto*s=dynamic_cast<const BSplineSurface*>(body.face(f).surface.get());
        FK_CHECK(s&&s->vDegree()==2);if(!s)continue;
        for(double fraction:{.2,.5,.8}) {
            const double u=s->uDomain().lo+fraction*s->uDomain().length();
            const Vec3 p=s->point(u,0),q=s->point(u,.5),r=s->point(u,1),a=q-p,b=r-p,n=cross(a,b);
            FK_CHECK(norm(n)>0);if(!(norm(n)>0))continue;
            const Vec3 center=p+(squaredNorm(a)*cross(b,n)+squaredNorm(b)*cross(n,a))/(2*squaredNorm(n));
            FK_CHECK_NEAR(distance(center,p),radius,1e-7*std::max(radius,1.0));
            for(double v:{.25,.75})FK_CHECK_NEAR(distance(center,s->point(u,v)),radius,1e-7*std::max(radius,1.0));
        }
    }
    FK_CHECK(generated>0);
}
}

FK_TEST(NurbsSupportJoinUsesCoefficientsAndHandlesChartOrientations) {
    for(bool rational:{false,true})for(int mode=0;mode<8;++mode) {
        const auto a=chart(false),source=chart(true,0,rational);
        auto adjusted=source;
        if(mode%2) {
            auto knots=source.vKnots();for(double &t:knots)t=-3.0+7.0*t;
            std::vector<Vec3>p;std::vector<double>w;
            for(int i=0;i<source.uPoleCount();++i)for(int j=0;j<source.vPoleCount();++j){p.push_back(source.pole(i,j));w.push_back(source.weight(i,j));}
            adjusted=BSplineSurface(source.uDegree(),source.vDegree(),source.uKnots(),knots,source.uPoleCount(),source.vPoleCount(),p,w);
        }
        const auto b=reorient(adjusted,mode&1,mode&2,mode&4);
        const auto result=detail::joinTangentNurbs(a,b);
        FK_CHECK(bool(result));if(!result)continue;
        for(const BSplineSurface*s:{&a,&b})for(double u:{.0,.2,.7,1.})for(double v:{.0,.3,1.}) {
            const Vec3 p=s->point(s->uDomain().lo+u*s->uDomain().length(),s->vDomain().lo+v*s->vDomain().length());
            const auto projection=projectPoint(*result->surface,p);
            FK_CHECK(projection.distance<1e-12);
        }
        // Input poles and rational weights are unchanged.
        FK_CHECK_NEAR(source.weight(1,0),rational?.8:1.,0.0);
    }
    FK_CHECK(!detail::joinTangentNurbs(chart(false),chart(true,.002)));
    FK_CHECK(!detail::joinTangentNurbs(chart(false),chart(true,1e-8)));
    const auto into=std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,1,1},std::vector<Vec3>{{-1,0,0},{0,0,0}});
    const auto back=std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,1,1},std::vector<Vec3>{{0,0,0},{-1,0,0}});
    FK_CHECK(!detail::joinTangentSplineCurves(into,{0,1},true,back,{0,1},true,1e-9));
}

FK_TEST(NurbsSupportUnionPreservesTrimGeometryTolerancesAndDeclaredHistory) {
    const Body source=adjacentCharts();const auto before=writeBodyBinary(source);
    BodyHistory history;const auto result=unifyTangentNurbsSupports(source,&history);
    FK_CHECK(bool(result));if(!result)return;
    FK_CHECK(result->faces().size()==1);
    FK_CHECK(result->edges().size()==4);
    FK_CHECK(checkBody(*result,{true,true}).empty());
    FK_CHECK(history.descendants(EntityKind::Face,0,0)==history.descendants(EntityKind::Face,0,1));
    FK_CHECK(history.descendants(EntityKind::Edge,0,0)==history.descendants(EntityKind::Edge,0,4));
    FK_CHECK(history.descendants(EntityKind::Edge,0,1).empty());
    for(auto e:result->edges())FK_CHECK(history.isDeclared(EntityKind::Edge,e.index));
    for(auto f:result->faces())FK_CHECK(history.isDeclared(EntityKind::Face,f.index));
    for(auto v:result->vertices())FK_CHECK(history.isDeclared(EntityKind::Vertex,v.index));
    const auto kept=history.descendants(EntityKind::Vertex,0,source.edgeStart(EdgeId(0)).index);
    FK_CHECK(kept.size()==1);if(!kept.empty())FK_CHECK_NEAR(result->vertex(VertexId(kept.front())).tolerance,3e-8,0.0);
    for(auto e:source.edges())if(e!=EdgeId(1)) {
        const auto descendants=history.descendants(EntityKind::Edge,0,e.index);FK_CHECK(descendants.size()==1);if(descendants.empty())continue;
        const auto&old=source.edge(e);const auto&now=result->edge(EdgeId(descendants.front()));
        for(int i=0;i<=64;++i)FK_CHECK(projectPoint(*now.curve,old.curve->point(old.range.lo+old.range.length()*i/64.),now.range).distance<=1e-9);
    }
    FK_CHECK(writeBodyBinary(source)==before);
    FK_CHECK(!unifyTangentNurbsSupports(*result));
}

FK_TEST(BlendTangentSweepSurvivesRadiiPosesScaleAndFaceRenumbering) {
    const Body source=sweepFixture();
    const int seeds[]={8,20,10,26,14,33,16,39,44,47,49,52};
    for(int mode=0;mode<4;++mode) {
        detail::BlendModel model(source,&source);
        if(mode>=2) {
            std::reverse(model.faces.begin(),model.faces.end());
            for(auto &[sourceFace,index]:model.faceIndex) { (void)sourceFace; index=int(model.faces.size())-1-index; }
        }
        BodyHistory order;
        Body body=model.build(&order);
        const double factor=mode==3?2.5:1.;
        if(mode)body=transformBody(body,Transform3::translation(Vec3(20,-30,15))
            *Transform3::rotation(Vec3(),normalized(Vec3(1,2,-1)),.63)
            *Transform3::scaling(Vec3(),factor));
        const auto before=writeBodyBinary(body);
        std::vector<EdgeId>selected;
        for(int e:seeds) {
            const auto descendants=order.descendants(EntityKind::Edge,0,e);
            FK_CHECK(descendants.size()==1);if(descendants.size()==1)selected.emplace_back(descendants.front());
        }
        if(mode==1) {
            const auto seam=order.descendants(EntityKind::Edge,0,83);
            FK_CHECK(seam.size()==1);if(seam.size()==1)selected.emplace_back(seam.front());
        }
        if(mode%2)std::reverse(selected.begin(),selected.end());
        for(double r:mode==0?std::vector<double>{.05,.1,.15}:std::vector<double>{.15}) {
            BodyHistory history;
            const Body result=[&] {
                try { return blendEdges(body,selected,r*factor,false,&history); }
                catch(const std::exception &error) { throw std::runtime_error("mode="+std::to_string(mode)+" R="+std::to_string(r*factor)+": "+error.what()); }
            }();
            FK_CHECK(checkBody(result,{true,true}).empty());
            for(EdgeId e:result.edges())FK_CHECK(!result.isLaminar(e));
            for(FinId f:result.fins())FK_CHECK(bool(result.fin(f).pcurve));
            for(FaceId f:result.faces())FK_CHECK(history.isDeclared(EntityKind::Face,f.index));
            checkCircularSections(result,history,r*factor);
            FK_CHECK(tessellate(result,{}).failedFaces==0);
            FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)),{true,true}).empty());
        }
        FK_CHECK(writeBodyBinary(body)==before);
    }
}

// La composizione esatta include anche i fianchi lineari tangenti: le
// vecchie pezze d'angolo artificiali scompaiono. Il consumo di una frontiera
// reale resta coperto da BlendConsumedTerminalReachesTangentSupport.
FK_TEST(BlendCertifiedNurbsAtlasEliminatesArtificialTerminalCorners) {
    std::ifstream in(std::string(FORGECAD_SOURCE_DIR)+"/kernel/tests/data/blend_terminal_fourth_face.body",std::ios::binary);
    FK_CHECK(bool(in));if(!in)return;
    const Body source=readBodyBinary(std::string(std::istreambuf_iterator<char>(in),{}));
    for(int mode=0;mode<4;++mode) {
        detail::BlendModel model(source,&source);
        if(mode>=2) {
            std::reverse(model.faces.begin(),model.faces.end());
            for(auto &[face,index]:model.faceIndex) { (void)face; index=int(model.faces.size())-1-index; }
        }
        BodyHistory order;Body body=model.build(&order);
        const double factor=mode==3?2.5:1.;
        if(mode)body=transformBody(body,Transform3::translation(Vec3(-21,13,32))
            *Transform3::rotation(Vec3(),normalized(Vec3(2,-1,3)),.71)*Transform3::scaling(Vec3(),factor));
        std::vector<EdgeId> selected;
        for(int id:{7,20,11,28,14,32,18,40}) {
            const auto d=order.descendants(EntityKind::Edge,0,id);
            FK_CHECK(d.size()==1);if(d.size()==1)selected.emplace_back(d.front());
        }
        if(mode%2)std::reverse(selected.begin(),selected.end());
        std::set<int> tipEdges;
        for(int id:{44,47,49,52})for(int e:order.descendants(EntityKind::Edge,0,id))tipEdges.insert(e);
        const auto before=writeBodyBinary(body);
        for(double r:mode==0?std::vector<double>{.05,.1}:std::vector<double>{.1}) {
            BodyHistory history;
            const Body result=[&] {
                try{return blendEdges(body,selected,r*factor,false,&history);}
                catch(const std::exception&e){throw std::runtime_error("terminale mode="+std::to_string(mode)+" R="+std::to_string(r*factor)+": "+e.what());}
            }();
            FK_CHECK(checkBody(result,{true,true}).empty());
            for(auto e:result.edges()) { FK_CHECK(!result.isLaminar(e));FK_CHECK(history.isDeclared(EntityKind::Edge,e.index)); }
            for(auto v:result.vertices())FK_CHECK(history.isDeclared(EntityKind::Vertex,v.index));
            for(auto f:result.fins())FK_CHECK(bool(result.fin(f).pcurve));
            int corners=0;
            for(auto f:result.faces()) {
                FK_CHECK(history.isDeclared(EntityKind::Face,f.index));
                for(const auto &origin:history.origins(EntityKind::Face,f.index)) {
                    if(origin.relation!=HistoryRelation::Generated)continue;
                    if(origin.kind==EntityKind::Vertex)++corners;
                    if(origin.kind==EntityKind::Edge)FK_CHECK(!tipEdges.count(origin.index));
                }
            }
            FK_CHECK(corners==0);
            checkCircularSections(result,history,r*factor);
            FK_CHECK(tessellate(result,{}).failedFaces==0);
            FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)),{true,true}).empty());
        }
        FK_CHECK(writeBodyBinary(body)==before);
    }
}

// La cucitura cono/calotta non coincide con il vecchio vertice del trim.
// I raggi intermedi non possono saltare per una radice sul prolungamento
// polinomiale delle pezze di raccordo.
FK_TEST(BlendRefinesTangentSupportPassagesAndBoundsMitreIntersections) {
    std::ifstream in(std::string(FORGECAD_SOURCE_DIR)+"/kernel/tests/data/blend_terminal_fourth_face.body",std::ios::binary);
    FK_CHECK(bool(in));if(!in)return;
    const Body source=readBodyBinary(std::string(std::istreambuf_iterator<char>(in),{}));
    for(int mode=0;mode<4;++mode) {
        detail::BlendModel model(source,&source);
        if(mode>=2) {
            std::reverse(model.faces.begin(),model.faces.end());
            for(auto &[face,index]:model.faceIndex) { (void)face; index=int(model.faces.size())-1-index; }
        }
        BodyHistory order;Body body=model.build(&order);
        const double factor=mode==3?2.5:1.;
        if(mode)body=transformBody(body,Transform3::translation(Vec3(-17,23,41))
            *Transform3::rotation(Vec3(),normalized(Vec3(-1,3,2)),.58)*Transform3::scaling(Vec3(),factor));
        std::vector<EdgeId> selected;
        for(int id:{63,27,47,48,64,62,60,50,49,37,36,28,29,35,34,30,33,32,15,14,31,11,10,12}) {
            const auto d=order.descendants(EntityKind::Edge,0,id);
            FK_CHECK(d.size()==1);if(d.size()==1)selected.emplace_back(d.front());
        }
        if(mode%2)std::reverse(selected.begin(),selected.end());
        const auto before=writeBodyBinary(body);
        for(double r:mode==0?std::vector<double>{.15,.155,.16,.17,.18,.2,.3}:std::vector<double>{.16,.3}) {
            BodyHistory history;
            const Body result=[&] {
                try{return blendEdges(body,selected,r*factor,false,&history);}
                catch(const std::exception&e){throw std::runtime_error("passaggio mode="+std::to_string(mode)+" R="+std::to_string(r*factor)+": "+e.what());}
            }();
            FK_CHECK(checkBody(result,{true,true}).empty());
            for(auto e:result.edges()){FK_CHECK(!result.isLaminar(e));FK_CHECK(history.isDeclared(EntityKind::Edge,e.index));}
            for(auto v:result.vertices())FK_CHECK(history.isDeclared(EntityKind::Vertex,v.index));
            for(auto f:result.faces())FK_CHECK(history.isDeclared(EntityKind::Face,f.index));
            for(auto f:result.fins())FK_CHECK(bool(result.fin(f).pcurve));
            checkCircularSections(result,history,r*factor);
            FK_CHECK(tessellate(result,{}).failedFaces==0);
            FK_CHECK(checkBody(readBodyBinary(writeBodyBinary(result)),{true,true}).empty());
        }
        FK_CHECK(writeBodyBinary(body)==before);
    }
}

// Lo stesso roundoff dei poli va propagato attraverso grado/campata.
// Una scala UV grande non deve invece nascondere una piega geometrica.
FK_TEST(NurbsSupportJetCertificatePreservesCreasesAcrossParameterScales) {
    const auto rescale=[](const BSplineSurface&s,double scale) {
        auto u=s.uKnots();for(double&t:u)t*=scale;
        std::vector<Vec3>p;std::vector<double>w;
        for(int i=0;i<s.uPoleCount();++i)for(int j=0;j<s.vPoleCount();++j){p.push_back(s.pole(i,j));w.push_back(s.weight(i,j));}
        return BSplineSurface(s.uDegree(),s.vDegree(),u,s.vKnots(),s.uPoleCount(),s.vPoleCount(),p,w);
    };
    for(double scale:{1e-8,1.,1e8})for(bool reverse:{false,true}) {
        const auto a=rescale(chart(false),scale),b=rescale(chart(true),scale),crease=rescale(chart(true,1e-8),scale);
        FK_CHECK(bool(reverse?detail::joinTangentNurbs(b,a):detail::joinTangentNurbs(a,b)));
        FK_CHECK(!(reverse?detail::joinTangentNurbs(crease,a):detail::joinTangentNurbs(a,crease)));
    }
}
