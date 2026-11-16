#include "fk_project.h"
#include "fk_sheet.h"
#include "fk_curve_algo.h"
#include "fk_curve.h"
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>
#include <utility>
#include <optional>
const std::vector<double> z27={0,0,0,0.0023926594471666389,-0.03158506535957388,0.22274247758415186,-1.0588644597919092,3.7068614333740251,-10.150702528319338,22.626599248232061,-42.105538437516088,66.431608164324672,-89.673893632054558,104.07526656293437,-104.07526656293437,89.673893632054558,-66.431608164324672,42.105538437516088,-22.626599248232061,10.150702528319338,-3.7068614333740251,1.0588644597919092,-0.22274247758415186,0.03158506535957388,-0.0023926594471666389,0,0,0};

using namespace ForgeCad::Kernel;
namespace {
constexpr double fitTolerance=1e-7;
int passed=0,failed=0;
void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
CurvePtr<3> hidden(bool rational=false,double lo=0.,double hi=1.,double scale=1.,const Frame3 &frame=Frame3(),double elevation=1.,double xOffset=0.) {
    std::vector<Vec3> poles;std::vector<double> weights,knots(28,lo);knots.insert(knots.end(),28,hi);
    for(int i=0;i<28;++i) {
        const double t=i/27.,w=rational?1.+.5*t:1.;
        const double numerator=rational?t+.5*i*(i-1)/(27.*26.):t;
        poles.push_back(frame.toGlobal(Vec3(scale*(xOffset+numerator/w),scale*z27[i]/w,scale*elevation)));
        if(rational)weights.push_back(w);
    }
    return std::make_shared<BSplineCurve<3>>(27,knots,poles,weights);
}
ProjectionOptions options(int threads=1){ProjectionOptions o;o.tolerance=fitTolerance;o.threads=threads;return o;}
Vec3 exactProjection(const Vec3 &point,const Frame3 &frame,const Vec3 &direction) {
    const Vec3 n=cross(frame.xDir(),frame.yDir());
    return point-dot(point-frame.origin(),n)/dot(direction,n)*direction;
}
void checkProjection(const Curve<3>&source,const std::vector<ProjectedPiece>&pieces,const Frame3 &frame,const Vec3 &d,std::optional<Interval> expectedRange=std::nullopt) {
    const Interval expected=expectedRange.value_or(source.domain());
    require(!pieces.empty(),"valid planar projection returned no piece");double worst=0.;
    require(std::fabs(pieces.front().range.lo-expected.lo)<1e-12 && std::fabs(pieces.back().range.hi-expected.hi)<1e-12,"projection omitted source range");
    double end=expected.lo;
    for(const auto&p:pieces) {
        double localWorst=0.;
        require(std::fabs(p.range.lo-end)<1e-12,"projection left an unreported range gap");end=p.range.hi;
        require(p.curve && p.curve->degree()==3 && !p.curve->isRational(),"projection output must be a nonrational cubic");
        require(p.deviation<=fitTolerance && std::isfinite(p.deviation),"invalid reported deviation");
        for(int k=0;k<=4096;++k) {
            double t=p.range.lo+p.range.length()*k/4096.;
            localWorst=std::max(localWorst,distance(p.curve->point(t),exactProjection(source.point(t),frame,d)));
        }
        worst=std::max(worst,localWorst);
        require(localWorst<=p.deviation+1e-12,"reported piece deviation underestimates measured error");
    }
    std::cout<<" max_error="<<worst;require(worst<=fitTolerance,"planar projection exceeds tolerance");
}
void standard(bool rational,bool upward,int wrapper) {
    const Frame3 frame;auto source=hidden(rational,0,1,1,frame,upward?-1.:1.);
    if(wrapper==1)source=std::make_shared<TrimmedCurve<3>>(source,.05,.95);
    if(wrapper==2)source=std::make_shared<TransformedCurve>(source,Transform3::translation(Vec3(.3,.2,0)));
    const Vec3 d(0,0,upward?1.:-1.);auto p=projectCurve(makePlaneSheet(frame,100),*source,source->domain(),d,options());
    checkProjection(*source,p,frame,d);
    // A witness outside the GLOBAL control hull is a geometric failure,
    // independent of parameter matching or a nearest-point root finder.
    if(!rational && wrapper==0){double low=1e300,high=-1e300;for(const auto&piece:p)for(const auto&q:piece.curve->poles()){low=std::min(low,q.y());high=std::max(high,q.y());}
        const auto exact=exactProjection(source->point(.8793),frame,d);double lower=std::max(low-exact.y(),exact.y()-high);std::cout<<" global_hull_lower="<<lower;require(lower<=fitTolerance,"projection misses true geometry outside its global hull");}
}
void placed(bool rational,bool shiftedParameter,double scale) {
    const Frame3 frame(Vec3(3,-4,2),normalized(Vec3(1,2,3)),Vec3(2,-1,0));
    auto source=hidden(rational,shiftedParameter?-3.:0.,shiftedParameter?5.:1.,scale,frame);
    Vec3 d=-frame.zDir()+.2*frame.xDir();auto p=projectCurve(makePlaneSheet(frame,std::max(10.,100*scale)),*source,source->domain(),d,options());
    checkProjection(*source,p,frame,d);
}
void knotCorner() {
    auto source=std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,.5,1,1},std::vector<Vec3>{{0,0,1},{.5,0,1},{1,.3,1}});
    auto p=projectCurve(makePlaneSheet(Frame3(),10),*source,{0,1},Vec3(0,0,-1),options());checkProjection(*source,p,Frame3(),Vec3(0,0,-1));
}
struct Other final:Curve<3>{CurveType type()const override{return CurveType::Other;}Interval domain()const override{return {0,1};}void evaluate(double t,int n,Vec3*out)const override{out[0]=Vec3(t,.1*t,1);if(n>=1)out[1]=Vec3(1,.1,0);for(int k=2;k<=n;++k)out[k]=Vec3();}};
void batch(int threads) {
    std::vector<CurveSpan> curves;for(bool rat:{false,true})curves.push_back({hidden(rat),{0,1}});
    curves.push_back({std::make_shared<Line<3>>(Vec3(0,.2,1),Vec3(1,0,0)),{0,1}});curves.push_back({std::make_shared<Other>(),{0,1}});
    auto result=projectCurves(makePlaneSheet(Frame3(),100),curves,Vec3(0,0,-1),options(threads));require(result.size()==curves.size(),"batch ordering/count changed");
    for(std::size_t k=0;k<result.size();++k)checkProjection(*curves[k].curve,result[k],Frame3(),Vec3(0,0,-1),curves[k].range);
}
std::vector<ProjectedPiece> lifetimePieces(double xOffset=0.,bool rational=false) {
    const Body body=makePlaneSheet(Frame3(),100);auto source=hidden(rational,0,1,1,Frame3(),1.,xOffset);
    return projectCurve(body,*source,{0,1},Vec3(0,0,-1),options());
}
void append(std::vector<ProjectedPiece>&dst,const std::vector<ProjectedPiece>&src){dst.insert(dst.end(),src.begin(),src.end());}
template<class T,class=void>struct HasDeviation:std::false_type{};
template<class T>struct HasDeviation<T,std::void_t<decltype(std::declval<T>().deviation)>>:std::true_type{};
template<class T>void checkChain(const T&chain,const std::vector<double>&offsets) {
    const auto &curve=*chain.curve;
    double error=0.;for(std::size_t i=0;i<offsets.size();++i){auto source=hidden(false,0,1,1,Frame3(),1,offsets[i]);for(int k=0;k<=1024;++k){double t=double(k)/1024.;auto exact=source->point(t);exact[2]=0.;error=std::max(error,distance(curve.point(i+t),exact));}}
    std::cout<<" joined_error="<<error;require(error<=fitTolerance,"joined final poles exceed source projection tolerance");
    if constexpr(HasDeviation<T>::value)require(std::isfinite(chain.deviation) && chain.deviation<=fitTolerance && error<=chain.deviation+1e-12,"joined reported deviation is not a valid final bound");
    else require(false,"joined chain does not publish final deviation");
}
void joinSeveral() {
    std::vector<ProjectedPiece> pieces;for(double x:{0.,1.,2.})append(pieces,lifetimePieces(x));
    auto chains=joinProjectedPieces(pieces);require(chains.size()==1 && !chains[0].closed,"contiguous pieces failed to form one open chain");checkChain(chains[0],{0,1,2});
}
void firstLastMerge() {
    std::vector<ProjectedPiece> pieces;for(double x:{0.,4.,-1.})append(pieces,lifetimePieces(x));
    auto chains=joinProjectedPieces(pieces);require(chains.size()==2,"first/last chains not merged");checkChain(chains[0],{-1,0});checkChain(chains[1],{4});
}
void unsafeJoin(bool firstLast) {
    // Both projectCurve calls are individually exact near the endpoint.
    // join's historical averaging would move each endpoint by 2.5e-7,
    // exceeding the 1e-7 source certificate while within join tolerance.
    std::vector<ProjectedPiece> pieces=lifetimePieces(firstLast?1.+5e-7:0.);
    if(firstLast)append(pieces,lifetimePieces(4.));
    append(pieces,lifetimePieces(firstLast?0.:1.+5e-7));
    bool threw=false;try{(void)joinProjectedPieces(pieces);}catch(const std::domain_error&){threw=true;}
    require(threw,"join accepted final endpoint outside its source tolerance");
}
void alteredCertifiedPiece() {
    auto pieces=lifetimePieces();require(pieces.size()==1,"altered-piece fixture requires one piece");
    const auto original=pieces[0].curve;auto poles=original->poles();poles.front()[1]+=.001;
    pieces[0].curve=std::make_shared<BSplineCurve<3>>(original->degree(),original->knots(),poles,original->weights());
    bool threw=false;try{(void)joinProjectedPieces(pieces);}catch(const std::domain_error&){threw=true;}
    require(threw,"modified curve retained a stale source certificate");
}
void invalidInputs() {
    const auto source=hidden();const Body plane=makePlaneSheet(Frame3(),100);bool threw=false;
    try{(void)projectCurve(plane,*source,{0,1},Vec3(),options());}catch(const std::invalid_argument&){threw=true;}require(threw,"zero direction accepted");
    threw=false;try{(void)projectCurves(plane,{{source,{0,1}},{nullptr,{0,1}}},Vec3(0,0,-1),options(4));}catch(const std::invalid_argument&){threw=true;}require(threw,"invalid mixed batch accepted");
    require(projectCurve(plane,*source,{0,1},Vec3(0,0,1),options()).empty(),"opposite ray incorrectly projected behind origin");
}
void run(const std::string&name,const std::function<void()>&test){std::cout<<name;try{test();++passed;std::cout<<" PASS\n";}catch(const std::exception&e){++failed;std::cout<<" FAIL "<<e.what()<<'\n';}}
}
int main(){std::cout<<std::setprecision(17)<<std::unitbuf;
    for(bool r:{false,true})for(bool up:{false,true})for(int w:{0,1,2})run("hidden rational="+std::to_string(r)+" upward="+std::to_string(up)+" wrapper="+std::to_string(w),[=]{standard(r,up,w);});
    for(bool r:{false,true})for(bool parameter:{false,true})for(double scale:{.001,1.,1000.})run("placed rational="+std::to_string(r)+" parameterShift="+std::to_string(parameter)+" scale="+std::to_string(scale),[=]{placed(r,parameter,scale);});
    run("knot_corner",knotCorner);for(int t:{1,4})run("mixed_batch threads="+std::to_string(t),[=]{batch(t);});
    run("join_lifetime",joinSeveral);run("join_first_last",firstLastMerge);run("unsafe_join",[]{unsafeJoin(false);});run("unsafe_first_last",[]{unsafeJoin(true);});run("altered_certified_piece",alteredCertifiedPiece);run("invalid_inputs",invalidInputs);
    std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;}
