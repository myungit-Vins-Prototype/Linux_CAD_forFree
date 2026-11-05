#include "fk_loft_merge.h"
#include "fk_bernstein.h"
#include "fk_bspline_surface.h"
#include "fk_cancel.h"
#include "fk_curve_ops.h"
#include "fk_nurbs.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace ForgeCad::Kernel::detail {
namespace {
using Poly=BernsteinPolynomial;
using PolynomialVector=std::array<Poly,3>;
PolynomialVector polynomial(const std::vector<Vec3> &poles) {
    PolynomialVector result;
    double scale=0;
    for(const auto &p:poles)scale=std::max(scale,norm(p));
    if(scale==0)scale=1;
    for(int d=0;d<3;++d){std::vector<double> c;for(const auto&p:poles)c.push_back(p[d]/scale);result[d]=Poly(std::move(c));}
    return result;
}
PolynomialVector crossProduct(const PolynomialVector&a,const PolynomialVector&b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
Poly dotProduct(const PolynomialVector&a,const PolynomialVector&b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool positive(const Poly&p,int depth=0) {
    const auto &c=p.coefficients();
    if(*std::min_element(c.begin(),c.end())>1e-12)return true;
    if(depth==12 || p.evaluate(0)<=1e-12 || p.evaluate(1)<=1e-12)return false;
    Poly a,b;p.split(.5,a,b);return positive(a,depth+1)&&positive(b,depth+1);
}
// Le loft normalizzano il peso di ogni bordo a 1; richiedere pesi costanti
// lungo V permette di certificare le derivate senza divisioni razionali.
bool boundary(const BSplineSurface&s,bool end,std::vector<Vec3>&points,std::vector<Vec3>&derivatives) {
    const int row=end?s.uPoleCount()-1:0,adjacent=end?row-1:1;
    const double w=s.weight(row,0),q=s.weight(adjacent,0);
    const auto &knots=s.uKnots();
    const double span=end?knots[s.uPoleCount()]-knots[s.uPoleCount()-1]:knots[s.uDegree()+1]-knots[s.uDegree()];
    if(!(span>0 && w>0 && q>0))return false;
    for(int j=0;j<s.vPoleCount();++j){
        if(std::fabs(s.weight(row,j)-w)>1e-13*w || std::fabs(s.weight(adjacent,j)-q)>1e-13*q)return false;
        points.push_back(s.pole(row,j));
        derivatives.push_back((end?s.pole(row,j)-s.pole(adjacent,j):s.pole(adjacent,j)-s.pole(row,j))*(s.uDegree()*q/(span*w)));
    }
    return true;
}
bool tangent(const BSplineSurface&a,const BSplineSurface&b) {
    if(a.vDegree()!=b.vDegree() || a.vKnots()!=b.vKnots())return false;
    std::vector<Vec3> pa,pb,da,db;
    if(!boundary(a,true,pa,da)||!boundary(b,false,pb,db))return false;
    for(std::size_t i=0;i<pa.size();++i)
        if(distance(pa[i],pb[i])>256*std::numeric_limits<double>::epsilon()*std::max({1.0,norm(pa[i]),norm(pb[i])}))return false;
    const auto aa=BSplineCurve<3>(a.vDegree(),a.vKnots(),da).bezierSegments();
    const auto bb=BSplineCurve<3>(a.vDegree(),a.vKnots(),db).bezierSegments();
    const auto cc=BSplineCurve<3>(a.vDegree(),a.vKnots(),pa).bezierSegments();
    for(std::size_t k=0;k<aa.size();++k){
        cancellationPoint();
        std::vector<Vec3> dv;const auto &p=cc[k].poles();
        for(std::size_t j=1;j<p.size();++j)dv.push_back(p[j]-p[j-1]);
        const auto u=polynomial(aa[k].poles()),v=polynomial(bb[k].poles()),t=polynomial(dv);
        const auto determinant=dotProduct(crossProduct(u,v),t);
        for(double value:determinant.coefficients())if(std::fabs(value)>1e-10)return false;
        if(!positive(dotProduct(crossProduct(u,t),crossProduct(v,t))))return false;
    }
    return true;
}
BSplineCurve<3> elevate(BSplineCurve<3> piece,int degree) {
    while(piece.degree()<degree){
        const int n=piece.degree();std::vector<Vec3> p(n+2);std::vector<double>w(n+2);
        p.front()=piece.poles().front();p.back()=piece.poles().back();w.front()=piece.weight(0);w.back()=piece.weight(n);
        for(int j=1;j<=n;++j){const double t=double(j)/(n+1);w[j]=t*piece.weight(j-1)+(1-t)*piece.weight(j);p[j]=(t*piece.weight(j-1)*piece.poles()[j-1]+(1-t)*piece.weight(j)*piece.poles()[j])/w[j];}
        std::vector<double> knots(n+2,piece.domain().lo);knots.insert(knots.end(),n+2,piece.domain().hi);
        piece=BSplineCurve<3>(n+1,knots,p,w);
    }
    return piece;
}
BSplineSurfacePtr concatenate(const BSplineSurface&a,const BSplineSurface&b) {
    const double shift=a.uDomain().hi-b.uDomain().lo;
    const int degree=std::max(a.uDegree(),b.uDegree()),nv=a.vPoleCount();
    const double factor=a.weight(a.uPoleCount()-1,0)/b.weight(0,0);
    std::vector<BSplineCurve<3>> rows;
    for(int j=0;j<nv;++j){
        cancellationPoint();std::vector<BSplineCurve<3>> pieces;std::vector<double> breaks{a.uDomain().lo};
        for(int side=0;side<2;++side){
            const auto&s=side?b:a;std::vector<Vec3> p;std::vector<double>w;
            for(int i=0;i<s.uPoleCount();++i){p.push_back(s.pole(i,j));w.push_back(s.weight(i,j)*(side?factor:1));}
            const BSplineCurve<3> row(s.uDegree(),s.uKnots(),p,w);
            for(const auto&piece:row.bezierSegments()){pieces.push_back(elevate(piece,degree));breaks.push_back(piece.domain().hi+(side?shift:0));}
        }
        rows.push_back(joinBezierPieces(pieces,breaks));
    }
    const int nu=rows[0].poleCount();std::vector<Vec3>p;std::vector<double>w;
    for(int i=0;i<nu;++i)for(int j=0;j<nv;++j){p.push_back(rows[j].poles()[i]);w.push_back(rows[j].weight(i));}
    return std::make_shared<BSplineSurface>(degree,a.vDegree(),rows[0].knots(),a.vKnots(),nu,nv,p,w);
}
bool boundaryEdge(const Body::BuildEdge&e,const BSplineSurface&s,bool end) {
    const double u=end?s.uDomain().hi:s.uDomain().lo;
    const double tolerance=256*std::numeric_limits<double>::epsilon()*std::max({1.0,norm(s.point(u,s.vDomain().lo)),norm(s.point(u,s.vDomain().hi))});
    return distance(e.curve->point(e.range.lo),s.point(u,s.vDomain().lo))<=tolerance
        && distance(e.curve->point(e.range.hi),s.point(u,s.vDomain().hi))<=tolerance;
}
}
void mergeLoftTangentFaces(std::vector<Vec3>&vertices,std::vector<Body::BuildEdge>&edges,std::vector<Body::BuildFace>&faces,FaceAliases*labels) {
    bool changed=true,any=false;
    while(changed){
        changed=false;
        for(std::size_t a=0;a<faces.size()&&!changed;++a)for(std::size_t b=0;b<faces.size()&&!changed;++b){
            if(a==b || faces[a].loops.size()!=1 || faces[b].loops.size()!=1 || faces[a].sense!=faces[b].sense)continue;
            const auto sa=std::dynamic_pointer_cast<const BSplineSurface>(faces[a].surface),sb=std::dynamic_pointer_cast<const BSplineSurface>(faces[b].surface);
            if(!sa||!sb)continue;
            auto &la=faces[a].loops[0],&lb=faces[b].loops[0];
            for(std::size_t i=0;!changed&&i<la.size();++i)for(std::size_t j=0;!changed&&j<lb.size();++j){
                if(la[i].edge!=lb[j].edge || !la[i].sense || lb[j].sense)continue;
                if(!boundaryEdge(edges[la[i].edge],*sa,true)||!boundaryEdge(edges[lb[j].edge],*sb,false)||!tangent(*sa,*sb))continue;
                // Una singola faccia chiusa conserva la cucitura parametrica:
                // le due fin dello stesso edge restano topologicamente necessarie.
                auto surface=concatenate(*sa,*sb);std::vector<Body::BuildFin> loop;
                const auto append=[&](Body::BuildFin f,const BSplineSurface &source,double shift) {
                    if(!f.pcurve) for(bool end : {false,true}) if(boundaryEdge(edges[f.edge],source,end)) {
                        f.pcurve=std::make_shared<Line<2>>(Vec2(end?source.uDomain().hi:source.uDomain().lo,0),Vec2(0,1));
                        break;
                    }
                    if(f.pcurve && shift!=0)f.pcurve=translatedCurve(f.pcurve,Vec2(shift,0));
                    loop.push_back(std::move(f));
                };
                for(std::size_t k=1;k<la.size();++k)append(la[(i+k)%la.size()],*sa,0);
                for(std::size_t k=1;k<lb.size();++k)append(lb[(j+k)%lb.size()],*sb,sa->uDomain().hi-sb->uDomain().lo);
                faces[a].surface=surface;faces[a].loops={std::move(loop)};
                if(labels){auto &into=(*labels)[a];into.insert(into.end(),(*labels)[b].begin(),(*labels)[b].end());labels->erase(labels->begin()+b);}
                faces.erase(faces.begin()+b);changed=any=true;
            }
        }
    }
    if(!any)return;
    // Il costruttore B-rep non deve ricevere edge o vertici ormai orfani.
    std::vector<int> edgeMap(edges.size(),-1),vertexMap(vertices.size(),-1);
    for(const auto&f:faces)for(const auto&l:f.loops)for(const auto&fin:l)edgeMap[fin.edge]=0;
    std::vector<Body::BuildEdge> compactEdges;std::vector<Vec3> compactVertices;
    for(std::size_t i=0;i<edges.size();++i)if(edgeMap[i]>=0){
        auto e=edges[i];for(int*v:{&e.start,&e.end}){if(vertexMap[*v]<0){vertexMap[*v]=int(compactVertices.size());compactVertices.push_back(vertices[*v]);}*v=vertexMap[*v];}
        edgeMap[i]=int(compactEdges.size());compactEdges.push_back(std::move(e));
    }
    for(auto&f:faces)for(auto&l:f.loops)for(auto&fin:l)fin.edge=edgeMap[fin.edge];
    edges=std::move(compactEdges);vertices=std::move(compactVertices);
}
}
