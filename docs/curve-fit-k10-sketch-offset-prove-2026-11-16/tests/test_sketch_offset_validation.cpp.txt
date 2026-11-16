#include "cad_sketch_offset.h"
#include <QLineF>
#include "cad_curve_solver.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_nurbs.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ForgeCad;
using namespace ForgeCad::Kernel;
constexpr double tolerance=1e-7;
int passed=0,failed=0;
void require(bool condition,const std::string &message) { if(!condition) throw std::runtime_error(message); }
// Existing K10 null-sampling polynomial: z27 scaled to stay below the
// independent collapse guard (10*fit tolerance), while exceeding fit tolerance.
CurveObject hiddenCurve(bool rational=false,bool reverse=false) {
    static const double ordinates[]={0,0,0,0.0023926594471666389,-0.03158506535957388,0.22274247758415186,-1.0588644597919092,3.7068614333740251,-10.150702528319338,22.626599248232061,-42.105538437516088,66.431608164324672,-89.673893632054558,104.07526656293437,-104.07526656293437,89.673893632054558,-66.431608164324672,42.105538437516088,-22.626599248232061,10.150702528319338,-3.7068614333740251,1.0588644597919092,-0.22274247758415186,0.03158506535957388,-0.0023926594471666389,0,0,0};
    CurveObject c;c.tool=DrawingTool::Converted;c.degree=27;
    for(int i=0;i<28;++i) {
        int k=reverse?27-i:i;
        const double t=k/27.,w=rational?1.+.5*t:1.;
        const double numerator=rational?t+.5*k*(k-1)/(27.*26.):t;
        c.controlPoints.append(QPointF(numerator/w,.01*ordinates[k]/w));
        if(rational)c.weights.append(w);
    }
    for(int i=0;i<28;++i)c.knots.append(0.);
    for(int i=0;i<28;++i)c.knots.append(1.);
    return c;
}
CurveObject parabola() {
    CurveObject c;c.tool=DrawingTool::Converted;c.degree=2;
    c.controlPoints={{-2,4},{0,-4},{2,4}};c.knots={0,0,0,1,1,1};return c;
}
bool sameCurve(const CurveObject&a,const CurveObject&b) {
    return a.tool==b.tool && a.degree==b.degree && a.controlPoints==b.controlPoints
        && a.weights==b.weights && a.knots==b.knots && a.construction==b.construction;
}
void unchanged(const SketchObject&a,const SketchObject&b) {
    require(a.segments==b.segments && a.curves.size()==b.curves.size()
        && a.geometricConstraints.size()==b.geometricConstraints.size()
        && a.constructionSegments==b.constructionSegments,"failure mutated sketch structure");
    for(int i=0;i<a.curves.size();++i)require(sameCurve(a.curves[i],b.curves[i]),"failure mutated curve");
}
Vec2 exactOffset(const Curve<2>&source,double t,double signedDistance) {
    Vec2 d[2];if(t==source.domain().hi)source.evaluateLeft(t,1,d);else source.evaluate(t,1,d);
    return d[0]+signedDistance*Vec2(-d[1].y(),d[1].x())/norm(d[1]);
}
void checkOffset(const CurveObject&sourceObject,const CurveObject&copy,double signedDistance,bool sameParameter=true) {
    const auto source=curveGeometry(sourceObject).front().curve;
    const auto fitted=curveGeometry(copy).front().curve;
    double parameterError=0.,geometricError=0.;
    for(int i=0;i<=4096;++i) {
        const double t=source->domain().lo+source->domain().length()*i/4096.;
        const auto exact=exactOffset(*source,t,signedDistance);
        if(sameParameter)parameterError=std::max(parameterError,distance(fitted->point(t),exact));
        if(i%16==0)geometricError=std::max(geometricError,projectPoint(*fitted,exact,fitted->domain()).distance);
    }
    std::cout<<" max_parameter="<<parameterError<<" max_geometry="<<geometricError;
    require(!sameParameter || parameterError<=tolerance,"hidden offset exceeds parameter tolerance");
    require(geometricError<=tolerance,"hidden offset exceeds geometric tolerance");
}
void hidden(bool rational,bool reversedPoles,bool reverse,bool both) {
    SketchObject s;s.curves={hiddenCurve(rational,reversedPoles)};const auto source=s.curves[0];
    SketchOffset o;o.distance=.001;o.reverse=reverse;o.bothSides=both;
    QVector<SketchEntity> made;const auto result=offsetSketchEntities(s,{{1,0}},o,&made);
    require(result.error.isEmpty(),result.error.toStdString());
    require(made.size()==(both?2:1),"incorrect copy count");
    require(sameCurve(s.curves[0],source),"source modified");
    for(int i=0;i<made.size();++i)checkOffset(source,s.curves[made[i].index],o.distance*(reverse?-1.:1.)*(i==1?-1.:1.));
}
void chain(bool backwards,bool round) {
    SketchObject s;s.curves={hiddenCurve()};
    s.segments=backwards?QVector<SketchSegment>{{{2,0},{1,0}}}:QVector<SketchSegment>{{{1,0},{1,1}}};
    QVector<SketchEntity> selected=backwards?QVector<SketchEntity>{{0,0},{1,0}}:QVector<SketchEntity>{{1,0},{0,0}};
    SketchOffset o;o.distance=.001;o.roundCorners=round;QVector<SketchEntity> made;
    const auto r=offsetSketchEntities(s,selected,o,&made);require(r.error.isEmpty(),r.error.toStdString());
    int freeIndex=-1;
    for(const auto&e:made)if(e.kind==1 && s.curves[e.index].tool==DrawingTool::Converted)freeIndex=e.index;
    require(freeIndex>=0,"chain missing fitted copy");
    auto copy=curveGeometry(s.curves[freeIndex]).front();auto source=curveGeometry(s.curves[0]).front();
    double error=0.;for(int k=0;k<=500;++k){double t=copy.range.lo+copy.range.length()*k/500.;error=std::max(error,distance(copy.curve->point(t),exactOffset(*source.curve,t,backwards?-.001:.001)));}
    std::cout<<" max_trimmed_parameter="<<error;require(error<=tolerance,"chain fit exceeds tolerance");
    if(!backwards)require(copy.range.hi<source.range.hi,"concave join did not trim final range");
}
void associative() {
    SketchObject s;s.curves={hiddenCurve()};SketchOffset o;o.distance=.001;o.dimensioned=true;
    require(offsetSketchEntities(s,{{1,0}},o).error.isEmpty(),"initial associative creation");
    s.geometricConstraints.last().value=.0012;
    for(auto&p:s.curves[0].controlPoints)p+=QPointF(0,.03);
    require(refreshSketchOffsets(s).isEmpty(),"associative refresh failed");
    require(s.curves.size()==2 && s.geometricConstraints.size()==1,"refresh appended entities");
    checkOffset(s.curves[0],s.curves[1],.0012);
    s.geometricConstraints.last().value=-.001;const auto before=s;
    require(!refreshSketchOffsets(s).isEmpty(),"invalid associative distance accepted");unchanged(s,before);
}
void loops() {
    SketchObject s;s.curves={parabola()};SketchOffset o;o.distance=1.;o.dimensioned=true;
    require(offsetSketchEntities(s,{{1,0}},o).error.isEmpty(),"trimmed parabola rejected");
    for(double d:{1.,1.1}) {
        s.geometricConstraints.last().value=d;require(refreshSketchOffsets(s).isEmpty(),"trimmed associative refresh rejected");
        auto result=curveGeometry(s.curves[1]).front(),source=curveGeometry(s.curves[0]).front();double previous=-1e300,worst=0.;
        for(int k=0;k<=500;++k){auto p=result.curve->point(result.range.lo+result.range.length()*k/500.);require(p.x()>=previous-1e-8,"loop survived trimming");previous=p.x();require(p.y()>=d*d+.25-1e-6,"cusp loop retained");worst=std::max(worst,std::fabs(projectPoint(*source.curve,p,source.range).distance-d));}
        std::cout<<" trimmed_distance_error="<<worst;require(worst<2*tolerance,"post-join geometry exceeds tolerance");
    }
}
void atomicFailure() {
    SketchObject s;s.curves={hiddenCurve()};CurveObject c;c.tool=DrawingTool::Circle;c.controlPoints={{3,0},{3.0005,0}};s.curves.append(c);
    SketchOffset o;o.distance=.001;o.reverse=true;o.construction=true;o.dimensioned=true;
    QVector<SketchEntity> made={{8,99}};const auto before=s;
    require(!offsetSketchEntities(s,{{1,0},{1,1}},o,&made).error.isEmpty(),"collapsed circle accepted");
    unchanged(s,before);require(made.size()==1 && made[0].kind==8 && made[0].index==99,"failure mutated created sentinel");
}
void knotCorner(bool reverse) {
    CurveObject c;c.tool=DrawingTool::Nurbs;c.degree=2;c.controlPoints={{0,0},{.5,0},{1,0},{1,.5},{1,1}};
    c.knots={0,0,0,.5,.5,1,1,1};c.weights={1,2,1,3,1};
    SketchObject s;s.curves={c};SketchOffset o;o.distance=.001;o.reverse=reverse;
    QVector<SketchEntity> made;const auto result=offsetSketchEntities(s,{{1,0}},o,&made);
    require(result.error.isEmpty(),result.error.toStdString());
    require(made.size()==(reverse?3:2),"knot corner produced incorrect piece count");
    auto source=curveGeometry(c).front().curve;int freeCount=0;double worst=0.;
    for(const auto&e:made)if(e.kind==1 && s.curves[e.index].tool==DrawingTool::Converted) {
        ++freeCount;auto copy=curveGeometry(s.curves[e.index]).front();
        for(int k=0;k<=200;++k) {
            double t=copy.range.lo+copy.range.length()*k/200.;Vec2 d[2];
            if(k==200)source->evaluateLeft(t,1,d);else source->evaluate(t,1,d);
            auto exact=d[0]+(reverse?-.001:.001)*Vec2(-d[1].y(),d[1].x())/norm(d[1]);
            worst=std::max(worst,distance(copy.curve->point(t),exact));
        }
    }
    std::cout<<" max_one_sided="<<worst;
    require(freeCount==2 && worst<=tolerance,"one-sided knot offset exceeds tolerance");
}
void zeroSpeedFailure() {
    CurveObject c;c.tool=DrawingTool::Converted;c.degree=3;c.controlPoints={{0,0},{1,0},{0,0},{1,0}};
    c.knots={0,0,0,0,1,1,1,1};
    SketchObject s;s.curves={c};s.segments={{{3,0},{4,0}}};
    SketchOffset o;o.distance=.001;o.construction=true;o.dimensioned=true;
    QVector<SketchEntity> made={{7,88}};const auto before=s;
    require(!offsetSketchEntities(s,{{0,0},{1,0}},o,&made).error.isEmpty(),"stationary tangent accepted");
    unchanged(s,before);require(made.size()==1 && made[0].kind==7 && made[0].index==88,"unresolved source mutated output sentinel");
}
void controls() {
    for(bool reverse:{false,true}) {
        SketchObject s;s.segments={{{0,0},{2,0}}};CurveObject c;c.tool=DrawingTool::Circle;c.controlPoints={{5,0},{7,0}};s.curves={c};
        SketchOffset o;o.distance=.1;o.reverse=reverse;QVector<SketchEntity> made;
        require(offsetSketchEntities(s,{{0,0},{1,0}},o,&made).error.isEmpty(),"analytic controls failed");
        require(made.size()==2,"analytic copy count");require(std::fabs(s.segments[1].first.y()-(reverse?-.1:.1))<1e-14,"line changed");
        require(std::fabs(QLineF(s.curves[1].controlPoints[0],s.curves[1].controlPoints[1]).length()-(reverse?1.9:2.1))<1e-14,"circle changed");
    }
}
template<class F>void run(const std::string&name,F f){std::cout<<name;try{f();++passed;std::cout<<" PASS\n";}catch(const std::exception&e){++failed;std::cout<<" FAIL "<<e.what()<<'\n';}}
}
int main(){
    std::cout<<std::setprecision(17)<<std::unitbuf;
    for(bool rat:{false,true})for(bool poles:{false,true})for(bool rev:{false,true})for(bool both:{false,true})run("hidden rational="+std::to_string(rat)+" polesReverse="+std::to_string(poles)+" reverse="+std::to_string(rev)+" both="+std::to_string(both),[=]{hidden(rat,poles,rev,both);});
    for(bool back:{false,true})for(bool round:{false,true})run("chain back="+std::to_string(back)+" round="+std::to_string(round),[=]{chain(back,round);});
    for(bool rev:{false,true})run("knot_corner reverse="+std::to_string(rev),[=]{knotCorner(rev);});
    run("zero_speed_failure",zeroSpeedFailure);run("associative",associative);run("trimmed_loops",loops);run("atomic_failure",atomicFailure);run("analytic_controls",controls);
    std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
}
