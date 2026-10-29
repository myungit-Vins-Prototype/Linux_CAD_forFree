#pragma once
#include "fk_bspline.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"
#include "fk_bernstein.h"
#include <optional>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <limits>
namespace ForgeCad {
struct DrawingRecognizedCircle {Kernel::Vec3 center,first,second;};
// La circolarita' non dipende dai soli campioni: il residuo omogeneo
// |P-CW|²-R²W² e la planarita' sono limitati su ogni pezza Bezier.
// La curva originale e i suoi parametri restano gli agganci della quota.
inline std::optional<DrawingRecognizedCircle> drawingRecognizeCircle(const Kernel::BSplineCurve<3> &curve,double tolerance){
    using namespace Kernel;
    if(curve.degree()>16||curve.poleCount()<3)return {};
    const auto domain=curve.domain();const auto a=curve.point(domain.lo);
    const bool closed=distance(a,curve.point(domain.hi))<=tolerance;
    const auto b=curve.point(domain.lo+domain.length()*(closed?1./3:.5)),c=curve.point(domain.lo+domain.length()*(closed?2./3:1.));
    const auto u=b-a,v=c-a,n=cross(u,v);const double area=dot(n,n);
    if(area<1e-4*dot(u,u)*dot(v,v)||norm(u)<=tolerance||norm(v)<=tolerance)return {};
    const auto center=a+(dot(u,u)*cross(v,n)+dot(v,v)*cross(n,u))/(2*area);const double radius=distance(a,center);
    if(!isFinite(center)||!std::isfinite(radius)||radius<10*tolerance||radius>1e6)return {};
    const auto normal=normalized(n);
    for(int i=1;i<32;++i){const auto p=curve.point(domain.lo+domain.length()*i/32)-center;
        if(std::abs(norm(p)-radius)>tolerance||std::abs(dot(p,normal))>tolerance)return {};}
    const auto binomial=[](int n,int k){double r=1;for(int i=1;i<=k;++i)r*=double(n-k+i)/i;return r;};
    using Pole=std::array<double,4>;
    const auto certify=[&](const auto &self,const std::vector<Pole> &p,int depth)->bool{
        cancellationPoint();const int degree=int(p.size())-1;double minWeight=std::numeric_limits<double>::infinity(),scale=radius;
        for(const auto &q:p){if(q[3]<=0)return false;minWeight=std::min(minWeight,q[3]);scale=std::max(scale,norm(Vec3(q[0],q[1],q[2]))/q[3]);}
        const double limit=tolerance*(2*radius-tolerance)*minWeight*minWeight;
        const double roundoff=256*std::numeric_limits<double>::epsilon()*scale*scale;
        bool bounded=true;
        for(const auto &q:p)if(std::abs(dot(Vec3(q[0],q[1],q[2]),normal))+roundoff>tolerance*q[3])bounded=false;
        for(int k=0;k<=2*degree;++k){double coefficient=0;
            for(int i=std::max(0,k-degree);i<=std::min(degree,k);++i){const int j=k-i;const auto &x=p[i],&y=p[j];coefficient+=(x[0]*y[0]+x[1]*y[1]+x[2]*y[2]-radius*radius*x[3]*y[3])*binomial(degree,i)*binomial(degree,j)/binomial(2*degree,k);}
            if(std::abs(coefficient)+roundoff>limit)bounded=false;
        }
        if(bounded)return true;
        if(depth==12)return false;
        auto work=p;std::vector<Pole> left(p.size()),right(p.size());left[0]=p.front();right.back()=p.back();
        for(int row=1;row<=degree;++row){for(int i=0;i<=degree-row;++i)for(int k=0;k<4;++k)work[i][k]=(work[i][k]+work[i+1][k])*.5;left[row]=work[0];right[degree-row]=work[degree-row];}
        return self(self,left,depth+1)&&self(self,right,depth+1);
    };
    for(const auto &segment:*curve.cachedBezierSegments()){
        std::vector<Pole> poles;for(int i=0;i<segment.poleCount();++i){const double w=segment.weight(i);const auto p=(segment.poles()[i]-center)*w;poles.push_back(Pole{p.x(),p.y(),p.z(),w});}
        if(!certify(certify,poles,0))return {};
    }
    const auto first=radius*normalized(a-center);return DrawingRecognizedCircle{center,first,radius*normalized(cross(normal,first))};
}
// Cache locale al thread, con proprietario debole: nessun dato geometrico
// persistente e nessun riuso dopo il rilascio della curva sorgente.
inline std::optional<DrawingRecognizedCircle> drawingRecognizeCircle(const Kernel::BSplineCurve<3> &curve,double tolerance,const Kernel::CurvePtr<3> &owner){
    struct Entry {std::weak_ptr<const Kernel::Curve<3>> owner;double tolerance;std::optional<DrawingRecognizedCircle> circle;};
    thread_local std::unordered_map<const Kernel::BSplineCurve<3> *,Entry> cache;
    const auto found=cache.find(&curve);
    if(found!=cache.end()&&found->second.tolerance==tolerance&&found->second.owner.lock()==owner)return found->second.circle;
    const auto circle=drawingRecognizeCircle(curve,tolerance);if(cache.size()>=2048)cache.clear();cache[&curve]={owner,tolerance,circle};return circle;
}

// Una curva spaziale puo' proiettarsi su una retta pur non essendo un
// cerchio o una retta 3D. I poli certificano la collinearita'; le radici
// del numeratore della derivata razionale danno tutti gli estremi finiti.
inline std::optional<std::array<Kernel::Vec2,2>> drawingRecognizeProjectedLineUncached(const Kernel::CurvePtr<3> &curve,Kernel::Interval range,const Kernel::Frame3 &plane,double tolerance){
    using namespace Kernel;
    auto source=curve;while(source->type()==CurveType::Trimmed||source->type()==CurveType::Transformed){if(source->type()==CurveType::Trimmed)source=static_cast<const TrimmedCurve<3> &>(*source).basis();else source=static_cast<const TransformedCurve &>(*source).basis();}
    if(source->type()!=CurveType::BSpline)return {};
    const auto image=planarImage(*curve,range,plane);if(!image.curve)return {};
    auto basis=image.curve;while(basis->type()==CurveType::Trimmed)basis=static_cast<const TrimmedCurve<2> &>(*basis).basis();
    if(basis->type()!=CurveType::BSpline)return {};
    const auto &spline=static_cast<const BSplineCurve<2> &>(*basis);if(spline.degree()>16||spline.poles().empty())return {};
    const auto origin=spline.poles().front();Vec2 direction;double length=0;
    for(const auto &p:spline.poles())if(distance(p,origin)>length){direction=p-origin;length=norm(direction);}
    if(length<=tolerance)return {};
    direction/=length;
    for(int i=0;i<spline.poleCount();++i)if(spline.weight(i)<=0||std::abs((spline.poles()[i]-origin).x()*direction.y()-(spline.poles()[i]-origin).y()*direction.x())>tolerance)return {};
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(const auto &segment:*spline.cachedBezierSegments()){
        const auto domain=segment.domain();const double a=std::max(domain.lo,image.range.lo),b=std::min(domain.hi,image.range.hi);if(b<a)continue;
        std::vector<double> values,weights;for(int i=0;i<segment.poleCount();++i){weights.push_back(segment.weight(i));values.push_back(dot(segment.poles()[i]-origin,direction)*segment.weight(i));}
        const BernsteinPolynomial numerator(values),denominator(weights);const auto derivative=numerator.derivative()*denominator-numerator*denominator.derivative();
        std::vector<double> candidates{a,b};for(double t:derivative.roots()){const double parameter=domain.lo+t*domain.length();if(parameter>a&&parameter<b)candidates.push_back(parameter);}
        for(double t:candidates){const double value=dot(segment.point(t)-origin,direction);low=std::min(low,value);high=std::max(high,value);}
    }
    if(!std::isfinite(low)||high-low<=tolerance)return {};
    return std::array<Vec2,2>{origin+low*direction,origin+high*direction};
}

inline std::optional<std::array<Kernel::Vec2,2>> drawingRecognizeProjectedLine(const Kernel::CurvePtr<3> &curve,Kernel::Interval range,const Kernel::Frame3 &plane,double tolerance){
    using namespace Kernel;using Result=std::optional<std::array<Vec2,2>>;
    struct Entry {std::weak_ptr<const Curve<3>> owner;Interval range;Frame3 plane;double tolerance;Result result;};
    thread_local std::unordered_map<const Curve<3> *,std::vector<Entry>> cache;thread_local int count=0;
    const auto found=cache.find(curve.get());if(found!=cache.end())for(const auto &entry:found->second)
        if(entry.owner.lock()==curve&&entry.range.lo==range.lo&&entry.range.hi==range.hi&&entry.tolerance==tolerance&&distance(entry.plane.origin(),plane.origin())==0&&distance(entry.plane.xDir(),plane.xDir())==0&&distance(entry.plane.yDir(),plane.yDir())==0&&distance(entry.plane.zDir(),plane.zDir())==0)return entry.result;
    const auto result=drawingRecognizeProjectedLineUncached(curve,range,plane,tolerance);
    if(count>=2048){cache.clear();count=0;}cache[curve.get()].push_back({curve,range,plane,tolerance,result});++count;return result;
}

}
