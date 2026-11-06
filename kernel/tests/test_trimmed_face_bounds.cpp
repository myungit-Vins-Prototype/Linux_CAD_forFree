#include "fk_classify.h"
#include "fk_bspline_surface.h"
#include "fk_curve_ops.h"
#include "fk_pcurve.h"
#include "fk_transform.h"
#include "fk_surface_algo.h"
#include "fk_curve_surface.h"
#include "fk_cycle_point_index.h"
#include "fk_boolean.h"
#include "fk_primitives.h"
#include "fk_solid_trim.h"
#include "fk_body_check.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
using namespace ForgeCad::Kernel;
void require(bool b,const char *m){if(!b)throw std::runtime_error(m);}
class GenericPCurve final : public Curve<2> {
public:
    explicit GenericPCurve(CurvePtr<2> basis) : basis_(std::move(basis)) {}
    CurveType type() const override { return CurveType::Other; }
    Interval domain() const override { return basis_->domain(); }
    void evaluate(double t, int order, Vec2 *out) const override { basis_->evaluate(t, order, out); }
private:
    CurvePtr<2> basis_;
};
Body rectangle(const SurfacePtr &surface,Interval u,Interval v) {
    const std::vector<Vec3> points{surface->point(u.lo,v.lo),surface->point(u.hi,v.lo),surface->point(u.hi,v.hi),surface->point(u.lo,v.hi)};
    std::vector<Body::BuildEdge> edges{{0,1,surface->vIso(v.lo),u,0},{1,2,surface->uIso(u.hi),v,0},
        {3,2,surface->vIso(v.hi),u,0},{0,3,surface->uIso(u.lo),v,0}};
    Body::BuildFace face;face.surface=surface;face.loops={{{0,true,nullptr,0},{1,true,nullptr,0},{2,false,nullptr,0},{3,false,nullptr,0}}};
    auto result=Body::buildSheet(points,edges,{face});require(computePCurves(result)==0,"SP-curve rettangolo");return result;
}
int main(){try{
    // Una singola pezza Bezier molto piu' grande della faccia: anche la
    // suddivisione del box sul ritaglio deve conservare tutto il materiale.
    const std::vector<double> knots{0,0,0,1,1,1};std::vector<Vec3> poles;std::vector<double> weights;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){poles.emplace_back(50*i,50*j,i==1&&j==1?20.:0.);weights.push_back(1.+.1*i+.05*j);}
    for(bool rational:{false,true}) {
        auto surface=std::make_shared<BSplineSurface>(2,2,knots,knots,3,3,poles,rational?weights:std::vector<double>{});
        const Interval u{.4,.43},v{.2,.22};const auto face=rectangle(surface,u,v);const auto box=faceBox(face,face.faces()[0]);
        require(box.hi.x()-box.lo.x()<10&&box.hi.y()-box.lo.y()<10,"box locale invece del carrier intero");
        for(int i=0;i<=80;++i)for(int j=0;j<=80;++j){const auto p=surface->point(u.lo+u.length()*i/80.,v.lo+v.length()*j/80.);for(int axis=0;axis<3;++axis)require(p[axis]>=box.lo[axis]&&p[axis]<=box.hi[axis],"punto interno escluso dal box");}
        const auto whole=rectangle(surface,{0,1},{0,1});const auto all=faceBox(whole,whole.faces()[0]);require(all.lo.x()<=0&&all.hi.x()>=100,"carrier completo conservato quando richiesto");
    }
    // Ritagli periodici locali: coordinate delle fin in periodi diversi,
    // rotazione del carrier e verso della faccia invertito. Il box include
    // tutta la pezza senza includere il lato opposto del carrier.
    std::vector<SurfacePtr> periodic{
        std::make_shared<ToroidalSurface>(Frame3(),20.,3.),
        std::make_shared<RevolutionSurface>(std::make_shared<Circle<3>>(
            Vec3(-195,0,50),Vec3(1,0,0),Vec3(0,0,1),250.),Vec3(),Vec3(0,0,1))};
    for(const auto &surface:periodic) {
        const Interval u{-.08,.08},v{-.02,.02};auto patch=rectangle(surface,u,v);
        int n=0;
        for(auto fin:patch.fins()) {
            const Vec2 shift((n%2)*surface->uPeriod(),(n%3)*surface->vPeriod());
            patch.fin(fin).pcurve=translatedCurve(patch.fin(fin).pcurve,shift);++n;
        }
        const auto box=faceBox(patch,patch.faces()[0]);
        require(box.hi.x()-box.lo.x()<2&&box.hi.y()-box.lo.y()<10,"ritaglio periodico non limitato");
        for(int i=0;i<=80;++i)for(int j=0;j<=80;++j) {
            const auto p=surface->point(u.lo+u.length()*i/80.,v.lo+v.length()*j/80.);
            for(int k=0;k<3;++k)require(p[k]>=box.lo[k]&&p[k]<=box.hi[k],"punto periodico perso");
        }
        const auto rotated=transformBody(patch,Transform3::rotation(Vec3(),Vec3(0,0,1),kPi));
        require(!box.overlaps(faceBox(rotated,rotated.faces()[0])),"copie opposte falsamente sovrapposte");
        // Un foro sul carrier completo ha orientamento opposto: restringere
        // al foro perderebbe la maggior parte della faccia.
        auto complement=patch;complement.face(complement.faces()[0]).sense=false;
        const auto all=faceBox(complement,complement.faces()[0]);
        require(all.lo.x()<0&&all.hi.x()>0,"complemento periodico ristretto al foro");
        auto missing=patch;missing.fin(missing.fins()[0]).pcurve.reset();
        const auto conservative=faceBox(missing,missing.faces()[0]);
        require(conservative.lo.x()<0,"seam senza SP-curve deve restare conservativo");
    }
    // Una fascia che avvolge il toro non e' una pezza locale.
    auto belt=rectangle(periodic.front(),{0.,kTwoPi},{-.1,.1});
    const auto beltBox=faceBox(belt,belt.faces()[0]);
    require(beltBox.lo.x()<-20&&beltBox.hi.x()>20,"fascia periodica troncata");
    // Riproduzione sintetica HoneyRepeat: tredici solidi chiusi da due
    // rivoluzioni concentriche; due istanze circolari non si intersecano.
    // Il carrier esterno ha un meridiano periodico che attraversa l'asse:
    // avviare il marching sull'intero carrier rende questa prova lentissima.
    std::vector<Body> blocks;
    for(int i=0;i<13;++i)blocks.push_back(makeBox(Frame3({40,-3,2.+7*i},{0,0,1},{1,0,0}),20,6,5));
    std::vector<const Body*> operands;for(const auto &b:blocks)operands.push_back(&b);
    const auto source=Body::collect(operands);
    const auto inner=rectangle(periodic.back(),{-.2,.2},{-.21,.21});
    const auto outerSurface=std::make_shared<RevolutionSurface>(std::make_shared<Circle<3>>(
        Vec3(-195,0,50),Vec3(1,0,0),Vec3(0,0,1),251.),Vec3(),Vec3(0,0,1));
    const auto outer=rectangle(outerSurface,{-.2,.2},{-.21,.21});
    const auto trimmed=trimSolidBetweenFaces(source,inner,outer);
    require(trimmed.shells().size()==13&&checkBody(trimmed,{true,true}).empty(),"13 solidi sintetici tagliati");
    const auto opposite=transformBody(trimmed,Transform3::rotation(Vec3(),Vec3(0,0,1),kPi));
    BooleanOptions options;options.threads=1;
    const auto copies=booleanOperation(trimmed,opposite,BooleanOperation::Unite,options);
    require(copies.shells().size()==26&&checkBody(copies,{true,true}).empty(),"26 solidi nella ripetizione sintetica");
    // Picco di una SP-curve fra due campioni uniformi: il box deve usare
    // l'inviluppo della curva, non perdere il picco stretto del ritaglio.
    auto plane=std::make_shared<BSplineSurface>(1,1,std::vector<double>{0,0,1,1},std::vector<double>{0,0,1,1},2,2,
        std::vector<Vec3>{{0,0,0},{0,100,0},{100,0,0},{100,100,0}});
    for (const auto &point : std::vector<Vec3>{{41,21,0},{60,21,10},{41,60,10},{60,60,10},{-10,-10,2}}) {
        const auto projection = projectPoint(*plane, point, {.4,.43}, {.2,.22});
        const Vec3 expected(std::clamp(point.x(),40.,43.),std::clamp(point.y(),20.,22.),0.);
        require(distance(projection.point,expected)<1e-9,"minimo interno, su bordo o angolo del dominio limitato");
    }
    std::vector<Vec3> vertices{{40,20,0},{43,20,0},{43,22,0},{40,22,0}};std::vector<Body::BuildEdge> edges;std::vector<CurvePtr<2>> pcs;
    const auto addLine=[&](int a,int b){const auto d=vertices[b]-vertices[a];edges.push_back({a,b,std::make_shared<Line<3>>(vertices[a],normalized(d)),{0,norm(d)},0});pcs.push_back(std::make_shared<BSplineCurve<2>>(1,std::vector<double>{0,0,norm(d),norm(d)},std::vector<Vec2>{{vertices[a].x()/100,vertices[a].y()/100},{vertices[b].x()/100,vertices[b].y()/100}}));};
    addLine(0,1);addLine(1,2);
    const std::vector<double> spikeKnots{0,0,.5036,.5037,.5038,1,1};
    const std::vector<Vec2> spike{{.43,.22},{.41501,.22},{.415,.8},{.41499,.22},{.4,.22}};std::vector<Vec3> mapped;for(const auto &p:spike)mapped.emplace_back(100*p.x(),100*p.y(),0);
    edges.push_back({2,3,std::make_shared<BSplineCurve<3>>(1,spikeKnots,mapped),{0,1},0});pcs.push_back(std::make_shared<BSplineCurve<2>>(1,spikeKnots,spike));addLine(3,0);
    Body::BuildFace f;f.surface=plane;f.loops={{{0,true,pcs[0],0},{1,true,pcs[1],0},{2,true,pcs[2],0},{3,true,pcs[3],0}}};
    auto sharp=Body::buildSheet(vertices,edges,{f});const auto box=faceBox(sharp,sharp.faces()[0]);require(box.hi.y()>=80&&box.hi.x()-box.lo.x()<10,"picco UV non perso e box stretto");
    // Una curva UV generica o assente non autorizza una finestra ridotta:
    // mantieni il carrier completo anche se gli altri tre bordi sono noti.
    f.loops[0][2].pcurve = std::make_shared<GenericPCurve>(pcs[2]);
    auto generic = Body::buildSheet(vertices,edges,{f});
    require(faceBox(generic,generic.faces()[0]).hi.y()>=100,"carrier completo con SP-curve generica");
    f.loops[0][2].pcurve.reset();
    auto missing = Body::buildSheet(vertices,edges,{f});
    require(faceBox(missing,missing.faces()[0]).hi.y()>=100,"carrier completo con SP-curve assente");
    // Gli estremi della bisezione devono appartenere al tratto accettato:
    // la media fra l'ultimo punto interno e quello esterno puo' arrotondarsi
    // sul lato sbagliato e bloccare il successivo trasferimento SP-curve.
    const BSplineSurface unitPlane(1,1,{0,0,1,1},{0,0,1,1},2,2,
        {{0,0,0},{0,1,0},{1,0,0},{1,1,0}});
    constexpr double tolerance=1e-6;
    for(double root:{.12,.25,.33,.43,.51,.61,.76,.93})for(double slope:{1e-5,1.7e-5,3e-5}) {
        const BSplineCurve<3> crossing(1,{0,0,1,1},{{0,.5,-slope*root},{1,.5,slope*(1-root)}});
        const auto ranges=curveOnSurface(crossing,{0,1},unitPlane,tolerance);
        require(ranges.size()==1,"tratto vicino al piano non trovato");
        for(const auto &range:ranges)for(double t:{range.lo,range.hi})
            require(std::fabs(crossing.point(t).z())<=tolerance,"estremo del tratto oltre la tolleranza originale");
    }
    // L'indice deve restituire lo stesso minimo del confronto completo,
    // anche escludendo un ciclo e con le traslazioni delle superfici periodiche.
    std::vector<detail::CyclePointIndex::Point> points;
    for(int c=0;c<80;++c)for(int i=0;i<24;++i)
        points.push_back({Vec2(.03*c+.007*std::cos(i),.02*(c%7)+.009*std::sin(i)),c});
    const detail::CyclePointIndex index(points);
    for(int k=0;k<120;++k)for(const Vec2 shift:std::vector<Vec2>{{0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1}}) {
        const Vec2 query(.019*k,.017*(k%11));const int excluded=k%80;double brute=1e300;
        for(const auto &p:points)if(p.cycle!=excluded)brute=std::min(brute,distance(query,p.uv+shift));
        require(index.nearest(query,excluded,shift)==brute,"minimo indicizzato diverso dalla ricerca completa");
    }
    double allNearest=1e300;
    for(const auto &p:points)allNearest=std::min(allNearest,distance(Vec2(.4,.08),p.uv));
    require(index.nearest({.4,.08},-1)==allNearest,"ricerca senza esclusioni");
    require(detail::CyclePointIndex({{{0,0},3}}).nearest({0,0},3)==1e300,"ciclo unico escluso");
    require(detail::CyclePointIndex({}).nearest({0,0},0)==1e300,"indice vuoto");
    std::cout<<"PASS box NURBS, estremi coincidenti e 840 minimi UV identici al confronto completo\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
