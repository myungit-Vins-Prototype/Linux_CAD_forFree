#include "fk_surface_mapping.h"
#include "fk_bspline_surface.h"
#include "fk_body_check.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"
#include "fk_offset.h"
#include "fk_pcurve.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include "fk_classify.h"
#include "fk_curve_surface.h"
#include "fk_sew.h"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
namespace ForgeCad::Kernel {
namespace {
void require(bool value, const char *message) { if (!value) throw std::domain_error(message); }
void checked(const Body &body) {
    const auto issues = checkBody(body, {true, true});
    if (!issues.empty()) throw std::domain_error("Mappatura: " + describe(issues.front().code) + ": " + issues.front().message);
}
CurvePtr<2> basis2(CurvePtr<2> curve) {
    while (curve && curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<2> &>(*curve).basis();
    return curve;
}
struct FlatEdge { CurvePtr<3> curve; Interval range; bool reverse = false; };
FlatEdge lineImage(const Vec3 &a, const Vec3 &b) {
    const double length = distance(a, b);
    require(length > kLinearResolution, "Sviluppo: bordo degenere o vertice del cono non gestito.");
    return {std::make_shared<Line<3>>(a, (b - a) / length), {0, length}, false};
}
CurvePtr<3> affineImage(const CurvePtr<2> &input, const Interval &range, const Frame3 &frame, double scaleX) {
    const auto curve = basis2(input);
    const auto point = [&](const Vec2 &p) { return frame.toGlobal(Vec3(scaleX * p.x(), p.y(), 0)); };
    if (curve->type() == CurveType::Line) {
        return std::make_shared<BSplineCurve<3>>(1, std::vector<double>{range.lo,range.lo,range.hi,range.hi},
            std::vector<Vec3>{point(curve->point(range.lo)),point(curve->point(range.hi))});
    }
    std::shared_ptr<BSplineCurve<2>> converted;
    const BSplineCurve<2> *spline = nullptr;
    if (curve->type() == CurveType::BSpline) spline = &static_cast<const BSplineCurve<2> &>(*curve);
    else if (curve->type() == CurveType::Circle) converted = std::make_shared<BSplineCurve<2>>(toBSpline(static_cast<const Circle<2> &>(*curve),range.lo,range.hi));
    else if (curve->type() == CurveType::Ellipse) converted = std::make_shared<BSplineCurve<2>>(toBSpline(static_cast<const Ellipse<2> &>(*curve),range.lo,range.hi));
    if (converted) spline = converted.get();
    require(spline, "Sviluppo: il bordo parametrico non ha una forma esatta supportata.");
    std::vector<Vec3> poles; for (const auto &p : spline->poles()) poles.push_back(point(p));
    return std::make_shared<BSplineCurve<3>>(spline->degree(),spline->knots(),poles,spline->weights());
}
BodyHistory mappedHistory(const Body &source, const Body &result) {
    auto h = makeHistory({&source}, result);
    const auto assign = [&](EntityKind kind, const auto &from, const auto &to) {
        require(from.size()==to.size(),"Mappatura: numero di entita' non conservato.");
        for (std::size_t i=0;i<to.size();++i) h.declareOrigins(kind,to[i].index,{{0,kind,from[i].index,HistoryRelation::Modified}});
    };
    assign(EntityKind::Face,source.faces(),result.faces()); assign(EntityKind::Edge,source.edges(),result.edges());
    assign(EntityKind::Vertex,source.vertices(),result.vertices()); return h;
}
std::pair<Interval,Interval> faceWindow(const Body &body, FaceId f) {
    Box box;
    const Frame3 xy;
    for (auto loop:body.face(f).loops) for (auto fin:body.loopFins(loop)) {
        const auto &edge=body.edge(body.fin(fin).edge); const auto &pc=body.fin(fin).pcurve;
        require(bool(pc),"Mappatura: SP-curve sorgente mancante.");
        box.add(curveBox(*embedCurve(pc,xy),edge.range));
    }
    require(!box.isEmpty() && box.hi.x()>box.lo.x() && box.hi.y()>box.lo.y(),"Mappatura: dominio di faccia non finito o degenere.");
    return {{box.lo.x(),box.hi.x()},{box.lo.y(),box.hi.y()}};
}
using Cell=std::array<std::array<Vec3,4>,4>;
Vec3 cubic(Vec3 a,Vec3 b,Vec3 c,Vec3 d,double t) {
    a=(1-t)*a+t*b;b=(1-t)*b+t*c;c=(1-t)*c+t*d;a=(1-t)*a+t*b;b=(1-t)*b+t*c;return (1-t)*a+t*b;
}
Vec3 cellPoint(const Cell &p,double u,double v) {
    Vec3 rows[4];for(int i=0;i<4;++i)rows[i]=cubic(p[i][0],p[i][1],p[i][2],p[i][3],v);
    return cubic(rows[0],rows[1],rows[2],rows[3],u);
}
// Fit per celle bicubiche Hermite; i nodi sorgenti restano espliciti.
// La griglia e' raffinata in base allo scarto misurato nello stesso dominio UV.
SurfacePtr fitMappedSurface(const Surface &source, Interval uw, Interval vw,
                           const std::function<Vec3(const Vec3 &)> &map, double tolerance, double &deviation) {
    std::vector<double> us=source.uBreakpoints(uw),vs=source.vBreakpoints(vw);
    require(us.size()>=2&&vs.size()>=2,"Deformazione: dominio senza campate.");
    const auto f=[&](double u,double v){return map(source.point(u,v));};
    struct Jet {Vec3 p,pu,pv,puv;};
    using Key=std::tuple<double,double,int,int>;
    std::map<Key,Jet> jets;
    const auto originalU=us,originalV=vs;
    struct Stencil {std::array<double,5> positions,weights;};
    const auto stencil=[](double t,int side,const std::vector<double> &grid) {
        const auto it=std::lower_bound(grid.begin(),grid.end(),t);
        require(it!=grid.end()&&*it==t,"Deformazione: nodo del fit assente.");
        const auto k=std::size_t(it-grid.begin());
        const double left=k? t-grid[k-1]:0.,right=k+1<grid.size()?grid[k+1]-t:0.;
        const double width=side<0?left:side>0?right:std::min(left,right);
        const double h=.001*width;
        require(h>0&&t+h!=t&&t-h!=t,"Deformazione: campata numericamente degenere.");
        Stencil out;
        if(side) {
            const double c[5]={-25,48,-36,16,-3};
            for(int i=0;i<5;++i){out.positions[i]=t+side*i*h;out.weights[i]=side*c[i]/(12*h);}
        } else {
            const double c[5]={1,-8,0,8,-1};
            for(int i=0;i<5;++i){out.positions[i]=t+(i-2)*h;out.weights[i]=c[i]/(12*h);}
        }
        return out;
    };
    const auto jet=[&](double u,double v,int su,int sv) {
        if(!std::binary_search(originalU.begin(),originalU.end(),u))su=0;
        if(!std::binary_search(originalV.begin(),originalV.end(),v))sv=0;
        const Key key{u,v,su,sv};const auto found=jets.find(key);if(found!=jets.end())return found->second;
        const auto a=stencil(u,su,us),b=stencil(v,sv,vs);Jet result;result.p=f(u,v);
        long double pu[3]={},pv[3]={},puv[3]={};
        std::array<Vec3,5> x,y;
        for(int i=0;i<5;++i){x[i]=f(a.positions[i],v);y[i]=f(u,b.positions[i]);}
        for(int i=0;i<5;++i) {
            for(int k=0;k<3;++k){pu[k]+=static_cast<long double>(a.weights[i])*(x[i][k]-result.p[k]);pv[k]+=static_cast<long double>(b.weights[i])*(y[i][k]-result.p[k]);}
            for(int j=0;j<5;++j){const Vec3 q=f(a.positions[i],b.positions[j]);for(int k=0;k<3;++k)puv[k]+=static_cast<long double>(a.weights[i])*b.weights[j]*
                ((static_cast<long double>(q[k])-x[i][k])-(static_cast<long double>(y[j][k])-result.p[k]));}
        }
        for(int k=0;k<3;++k){result.pu[k]=double(pu[k]);result.pv[k]=double(pv[k]);result.puv[k]=double(puv[k]);}
        require(isFinite(result.pu)&&isFinite(result.pv)&&isFinite(result.puv),"Deformazione: derivate non finite.");
        jets.emplace(key,result);return result;
    };
    for(int round=0;round<24;++round) {
        cancellationPoint();
        // Il passo delle derivate segue il raffinamento: un passo fisso sul
        // dominio iniziale attraversava dettagli NURBS anche nelle celle fini.
        jets.clear();
        const std::size_t nu=us.size()-1,nv=vs.size()-1;
        require(nu<=65536/nv && (3*nu+1)<=200000/(3*nv+1),"Deformazione: griglia NURBS oltre il budget.");
        std::vector<Cell> cells(nu*nv);std::vector<bool> splitU(nu,false),splitV(nv,false);
        bool done=true;double worst=0;
        for(std::size_t i=0;i<nu;++i)for(std::size_t j=0;j<nv;++j) {
            cancellationPoint();
            Cell &cell=cells[i*nv+j];const double hu=us[i+1]-us[i],hv=vs[j+1]-vs[j];
            require(hu>0&&hv>0,"Deformazione: campata nulla.");
            // Hermite: le celle lisce condividono punti e derivate, evitando
            // pieghe artificiali sulle linee della griglia del fit.
            for(int cu=0;cu<2;++cu)for(int cv=0;cv<2;++cv) {
                const double u=cu?us[i+1]:us[i],v=cv?vs[j+1]:vs[j];
                const int su=cu?-1:1,sv=cv?-1:1;
                const auto data=jet(u,v,su,sv);
                const int a=cu?3:0,b=cv?3:0,a2=cu?2:1,b2=cv?2:1;
                cell[a][b]=data.p;
                cell[a2][b]=data.p+(su*hu/3.)*data.pu;
                cell[a][b2]=data.p+(sv*hv/3.)*data.pv;
                cell[a2][b2]=data.p+(su*hu/3.)*data.pu+(sv*hv/3.)*data.pv+(su*sv*hu*hv/9.)*data.puv;
            }
            double alongU=0,alongV=0,inside=0;
            for(double q:{.125,.25,.5,.75,.875}) {
                for(double r:{0.,1.}) {
                    alongU=std::max(alongU,distance(cellPoint(cell,q,r),f(us[i]+hu*q,vs[j]+hv*r)));
                    alongV=std::max(alongV,distance(cellPoint(cell,r,q),f(us[i]+hu*r,vs[j]+hv*q)));
                }
                for(double r:{.125,.25,.5,.75,.875})inside=std::max(inside,distance(cellPoint(cell,q,r),f(us[i]+hu*q,vs[j]+hv*r)));
            }
            worst=std::max({worst,alongU,alongV,inside});
            if(std::max({alongU,alongV,inside})>tolerance) {
                done=false;bool u=alongU>.5*tolerance,v=alongV>.5*tolerance;
                if(!u&&!v)u=v=true;
                splitU[i]=splitU[i]||u;splitV[j]=splitV[j]||v;
            }
        }
        if(done) {
            const auto knots=[](const std::vector<double> &grid){std::vector<double> k{grid.front()};for(double x:grid)for(int r=0;r<3;++r)k.push_back(x);k.push_back(grid.back());return k;};
            const int npu=int(3*nu+1),npv=int(3*nv+1);std::vector<Vec3> poles(std::size_t(npu)*npv);
            for(std::size_t i=0;i<nu;++i)for(std::size_t j=0;j<nv;++j)for(int a=0;a<4;++a)for(int b=0;b<4;++b)
                poles[(3*i+a)*npv+3*j+b]=cells[i*nv+j][a][b];
            deviation=std::max(deviation,worst);
            return std::make_shared<BSplineSurface>(3,3,knots(us),knots(vs),npu,npv,poles);
        }
        const auto refine=[](std::vector<double> &grid,const std::vector<bool> &split){std::vector<double> next;for(std::size_t i=0;i<split.size();++i){next.push_back(grid[i]);if(split[i])next.push_back(.5*grid[i]+.5*grid[i+1]);}next.push_back(grid.back());grid=std::move(next);};
        refine(us,splitU);refine(vs,splitV);
    }
    throw std::domain_error("Deformazione: fit della superficie fuori tolleranza.");
}
Body mapBody(const Body &source,const std::function<Vec3(const Vec3 &)> &map,double tolerance,
             BodyHistory *history,double *deviation) {
    // Guardie locali sul volume sorgente; le degenerazioni non diventano
    // corpi validi soltanto perche' il fit riesce.
    Box box;for(auto e:source.edges())box.add(curveBox(*source.edge(e).curve,source.edge(e).range));
    require(!box.isEmpty(),"Deformazione: corpo vuoto.");
    const double h=1e-5*std::max(1.,box.diagonal());
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k) {
        Vec3 p(box.lo.x()+(box.hi.x()-box.lo.x())*i/2.,box.lo.y()+(box.hi.y()-box.lo.y())*j/2.,box.lo.z()+(box.hi.z()-box.lo.z())*k/2.);
        Vec3 axes[3];for(int a=0;a<3;++a){Vec3 q=p;const double step=p[a]+h<=box.hi[a]||box.hi[a]==box.lo[a]?h:-h;q[a]+=step;axes[a]=(map(q)-map(p))/step;}
        require(dot(cross(axes[0],axes[1]),axes[2])>1e-8,"Deformazione: inversione o collasso del corpo; ridurre spessore o estensione.");
    }
    std::vector<Vec3> vertices;std::vector<Body::BuildEdge> edges;std::vector<Body::BuildFace> faces;
    // Il fit deve rispettare anche la connessione edge/superficie del B-rep,
    // che usa kLinearResolution quando l'edge non e' tollerante. Il budget
    // dell'operazione non autorizza un edge lontano dalle facce adiacenti.
    const double connectionBudget=std::min(tolerance,kLinearResolution);
    const double fitBudget=.2*connectionBudget;
    std::map<int,int> vertexOf,edgeOf;double worst=0;
    for(auto v:source.vertices()){vertexOf[v.index]=int(vertices.size());vertices.push_back(map(source.vertex(v).point));}
    for(auto id:source.edges()) {
        cancellationPoint();const auto &edge=source.edge(id);double error=0;
        auto curve=fitCurve([&](double t){return map(edge.curve->point(t));},edge.range,edge.curve->breakpoints(edge.range),fitBudget,&error);
        worst=std::max(worst,error);edgeOf[id.index]=int(edges.size());
        const int a=vertexOf.at(source.edgeStart(id).index),b=vertexOf.at(source.edgeEnd(id).index);
        const double gap=std::max(distance(curve->point(edge.range.lo),vertices[a]),distance(curve->point(edge.range.hi),vertices[b]));
        require(gap<=fitBudget,"Deformazione: bordo sorgente non collegato ai vertici.");
        edges.push_back({a,b,curve,edge.range,gap>kLinearResolution?gap*1.01:0.});
    }
    for(auto id:source.faces()) {
        const auto &face=source.face(id);const auto window=faceWindow(source,id);
        Body::BuildFace mapped;mapped.sense=face.sense;
        mapped.surface=fitMappedSurface(*face.surface,window.first,window.second,map,fitBudget,worst);
        for(auto loop:face.loops){std::vector<Body::BuildFin> fins;for(auto use:source.loopFins(loop)){const auto &fin=source.fin(use);const auto &edge=edges[edgeOf.at(fin.edge.index)];const double error=pcurveDeviation(*mapped.surface,*edge.curve,*fin.pcurve,edge.range,32);require(error<=connectionBudget*.8,"Deformazione: SP-curve fuori tolleranza.");worst=std::max(worst,error);fins.push_back({edgeOf.at(fin.edge.index),fin.sense,fin.pcurve,std::max(error*1.01,1e-14)});}mapped.loops.push_back(std::move(fins));}
        faces.push_back(std::move(mapped));
    }
    Body result=source.isSheet()?Body::buildSheet(vertices,edges,faces):Body::build(vertices,edges,faces);checked(result);
    if(history)*history=mappedHistory(source,result);
    if(deviation)*deviation=worst;
    return result;
}

}
Body unrollFace(const Body &source, FaceId id, const Frame3 &plane, BodyHistory *history) {
    require(source.contains(id),"Sviluppo: faccia sorgente non valida.");
    const auto &face=source.face(id);const auto type=face.surface->type();
    require(type==SurfaceType::Plane||type==SurfaceType::Cylinder||type==SurfaceType::Cone,
            "Sviluppo esatto disponibile per piani, cilindri e coni; il carrier selezionato non e' supportato.");
    const double radius=type==SurfaceType::Cylinder?static_cast<const CylindricalSurface&>(*face.surface).radius():1.;
    double sine=1,reference=0;
    if(type==SurfaceType::Cone){const auto&cone=static_cast<const ConicalSurface&>(*face.surface);sine=std::sin(cone.semiAngle());reference=cone.referenceRadius();}
    struct Use {CurvePtr<2> curve;Interval range;bool sense=true;int edge=-1;double budget=0;};
    std::vector<std::vector<Use>> loops;bool winding=false;
    for(auto loop:face.loops) {
        std::vector<Use> uses;
        for(auto id:source.loopFins(loop)) {
            const auto &fin=source.fin(id);const auto &edge=source.edge(fin.edge);
            require(bool(fin.pcurve),"Sviluppo esatto: SP-curve sorgente assente.");
            // Isometria esatta della curva di trim gia' rappresentata in UV.
            // Nessun nuovo fit: si trasporta il budget del B-rep sorgente,
            // incluse le connessioni tolleranti ai vertici del taglio.
            const double budget=std::max({kLinearResolution,edge.tolerance,
                source.vertex(source.edgeStart(fin.edge)).tolerance,source.vertex(source.edgeEnd(fin.edge)).tolerance})+fin.pcurveTolerance;
            uses.push_back({fin.pcurve,edge.range,fin.sense,fin.edge.index,budget});
            const Vec2 a=fin.pcurve->point(edge.range.lo),b=fin.pcurve->point(edge.range.hi);
            winding=winding||(type!=SurfaceType::Plane&&distance(edge.curve->point(edge.range.lo),edge.curve->point(edge.range.hi))<=kLinearResolution&&std::fabs(b.x()-a.x())>kPi);
        }
        loops.push_back(std::move(uses));
    }
    if(winding) {
        // Cilindro/cono completo senza cucitura topologica: i due anelli
        // diventano due lati e vengono collegati da due bordi del taglio.
        require(loops.size()==2&&loops[0].size()==1&&loops[1].size()==1,
                "Sviluppo: faccia periodica con fori o contorni complessi; dividere prima lungo una generatrice.");
        double heights[2],u0=0,u1=kTwoPi;
        for(int i=0;i<2;++i) {
            const auto &use=loops[i][0];const auto pc=basis2(use.curve);
            require(pc->type()==CurveType::Line,"Sviluppo periodico: contorno non isoparametrico.");
            const Vec2 a=pc->point(use.range.lo),b=pc->point(use.range.hi);
            require(std::fabs(a.y()-b.y())<1e-12&&std::fabs(std::fabs(a.x()-b.x())-kTwoPi)<1e-12,
                    "Sviluppo periodico: contorno non completo o non isoparametrico.");
            heights[i]=a.y();if(i==0){u0=std::min(a.x(),b.x());u1=u0+kTwoPi;}
        }
        const int bottom=heights[0]<heights[1]?0:1,top=1-bottom;
        std::vector<Vec2> corners={{u0,heights[bottom]},{u1,heights[bottom]},{u1,heights[top]},{u0,heights[top]}};
        std::vector<int> origins={loops[bottom][0].edge,-1,loops[top][0].edge,-1};
        if(!face.sense){std::reverse(corners.begin(),corners.end());origins={loops[top][0].edge,-1,loops[bottom][0].edge,-1};}
        std::vector<Use> opened;
        for(int i=0;i<4;++i){const Vec2 a=corners[i],b=corners[(i+1)%4];const double length=distance(a,b);require(length>0,"Sviluppo: altezza nulla.");opened.push_back({std::make_shared<Line<2>>(a,(b-a)/length),{0,length},true,origins[i]});}
        loops={opened};
    }
    std::vector<Vec3> points;std::vector<Body::BuildEdge> edges;Body::BuildFace flat;
    flat.surface=std::make_shared<Plane>(plane);flat.sense=face.sense;std::vector<int> edgeOrigins;std::vector<double> vertexBudgets;
    for(const auto &uses:loops) {
        require(!uses.empty(),"Sviluppo: contorno vuoto.");std::vector<Body::BuildFin> fins;const int first=int(points.size());
        for(std::size_t k=0;k<uses.size();++k) {
            cancellationPoint();const auto &use=uses[k];FlatEdge mapped;
            if(type!=SurfaceType::Cone){mapped.curve=affineImage(use.curve,use.range,plane,radius);mapped.range=use.range;}
            else {
                const auto pc=basis2(use.curve);require(pc->type()==CurveType::Line,"Sviluppo conico esatto: scegliere contorni isoparametrici.");
                const Vec2 a=pc->point(use.range.lo),b=pc->point(use.range.hi);
                const auto image=[&](Vec2 uv){const double r=reference/sine+uv.y(),angle=sine*uv.x();require(r*sine>kLinearResolution,"Sviluppo: il contorno attraversa il vertice del cono.");return plane.toGlobal(Vec3(r*std::cos(angle),-r*std::sin(angle),0));};
                // Solo rumore entro gli arrotondamenti delle inversioni UV;
                // il criterio dipende dall'intero intervallo, non dalla pendenza.
                const double roundU=16*kMachineEpsilon*std::max({1.,std::fabs(a.x()),std::fabs(b.x())});
                const double roundV=16*kMachineEpsilon*std::max({1.,std::fabs(a.y()),std::fabs(b.y())});
                if(std::fabs(a.x()-b.x())<=roundU)mapped=lineImage(image(a),image(b));
                else {
                    require(std::fabs(a.y()-b.y())<=roundV,"Sviluppo conico esatto: bordo obliquo non isoparametrico.");
                    const double r=reference/sine+a.y(),sign=r>0?1.:-1.;
                    require(r*sine>kLinearResolution,"Sviluppo: bordo sul vertice del cono.");
                    mapped.curve=std::make_shared<Circle<3>>(plane.origin(),sign*plane.xDir(),-sign*plane.yDir(),std::fabs(r));
                    const double lo=sine*a.x(),hi=sine*b.x();mapped.range={std::min(lo,hi),std::max(lo,hi)};mapped.reverse=hi<lo;
                }
            }
            const bool sense=use.sense!=mapped.reverse;const Vec3 start=mapped.curve->point(sense?mapped.range.lo:mapped.range.hi);
            const int vertex=int(points.size());points.push_back(start);
            const double budget=use.budget+uses[(k+1)%uses.size()].budget;
            vertexBudgets.push_back(use.budget+uses[(k+uses.size()-1)%uses.size()].budget);
            const int end=k+1==uses.size()?first:vertex+1;
            edges.push_back({sense?vertex:end,sense?end:vertex,mapped.curve,mapped.range,budget});edgeOrigins.push_back(use.edge);
            fins.push_back({int(edges.size()-1),sense,nullptr,0});
        }
        flat.loops.push_back(std::move(fins));
    }
    Body result=Body::buildSheet(points,edges,{flat});
    const auto vertices=result.vertices();for(std::size_t i=0;i<vertices.size();++i)result.vertex(vertices[i]).tolerance=vertexBudgets[i];
    require(computePCurves(result)==0,"Sviluppo: SP-curve piana non calcolabile.");checked(result);
    if(history) {
        *history=makeHistory({&source},result);
        history->declareOrigins(EntityKind::Face,result.faces().front().index,{{0,EntityKind::Face,id.index,HistoryRelation::Modified}});
        for(auto vertex:result.vertices())history->declareOrigins(EntityKind::Vertex,vertex.index,{{0,EntityKind::Face,id.index,HistoryRelation::Generated}});
        const auto es=result.edges();for(std::size_t i=0;i<es.size();++i)history->declareOrigins(EntityKind::Edge,es[i].index,
            edgeOrigins[i]>=0?std::vector<HistoryLink>{{0,EntityKind::Edge,edgeOrigins[i],HistoryRelation::Modified}}:std::vector<HistoryLink>{{0,EntityKind::Face,id.index,HistoryRelation::Generated}});
    }
    return result;
}
Body wrapBodyOnSurface(const Body &source, const Surface &target, const SurfaceWrapOptions &options, BodyHistory *history,double *deviation) {
    require(options.tolerance>=1e-9&&options.tolerance<=.01&&std::isfinite(options.tolerance),"Deformazione: tolleranza non valida.");
    Vec3 d[4];target.evaluate(options.anchor.x(),options.anchor.y(),1,d);
    const double scaleU=norm(d[2]),scaleV=norm(d[1]);require(scaleU>1e-12&&scaleV>1e-12&&norm(cross(d[2],d[1]))>1e-12*scaleU*scaleV,"Deformazione: punto di destinazione singolare.");
    const double c=std::cos(options.angle),s=std::sin(options.angle);
    const auto map=[&](const Vec3 &point) {
        cancellationPoint();const Vec3 p=options.sourcePlane.toLocal(point);
        const double x=c*p.x()-s*p.y()+options.shift.x(),y=s*p.x()+c*p.y()+options.shift.y();
        const double u=options.anchor.x()+x/scaleU,v=options.anchor.y()+y/scaleV;
        require((target.isUPeriodic()||target.uDomain().contains(u,1e-12))&&(target.isVPeriodic()||target.vDomain().contains(v,1e-12)),"Deformazione: corpo oltre il dominio della superficie destinazione.");
        return target.point(u,v)+(p.z()+options.normalOffset)*target.normal(u,v);
    };
    return mapBody(source,map,options.tolerance,history,deviation);
}
Body wrapBodyOnFaces(const Body &source,const std::vector<SurfaceTargetFace> &targets,
                     const SurfaceWrapOptions &options,BodyHistory *history,double *deviation) {
    require(!targets.empty()&&targets.size()<=255,"Deformazione: scegliere da 1 a 255 facce destinazione.");
    require(options.tolerance>=1e-9&&options.tolerance<=.01&&std::isfinite(options.tolerance),"Deformazione: tolleranza non valida.");
    require(isFinite(options.anchor)&&isFinite(options.shift)&&std::isfinite(options.angle)&&
            std::isfinite(options.normalOffset)&&isFinite(options.sourcePlane.origin()),
            "Deformazione: parametri di appoggio non finiti.");
    const double precision=std::min(kLinearResolution,.02*options.tolerance);
    struct Patch {
        const Body *body;FaceId face;Box bounds;FaceClassifier classifier;Vec2 seed;
    };
    std::vector<Patch> patches;
    for(const auto &target:targets) {
        require(target.body&&target.body->contains(target.face),"Deformazione: faccia destinazione assente.");
        const auto &surface=*target.body->face(target.face).surface;
        const auto u=surface.uDomain(),v=surface.vDomain();
        patches.push_back({target.body,target.face,faceBox(*target.body,target.face),
            FaceClassifier(*target.body,target.face,precision),Vec2(u.isFinite()?.5*u.lo+.5*u.hi:0.,v.isFinite()?.5*v.lo+.5*v.hi:0.)});
    }
    const auto &primary=patches.front();const auto &first=primary.body->face(primary.face);
    Vec3 d[4];first.surface->evaluate(options.anchor.x(),options.anchor.y(),1,d);
    require(norm(d[2])>1e-12&&norm(d[1])>1e-12,"Deformazione: appoggio singolare.");
    const double orientation=first.sense?1.:-1.;
    const auto axisU=normalized(d[2]),axisV=orientation*normalized(d[1]);
    const auto direction=orientation*first.surface->normal(options.anchor.x(),options.anchor.y());
    require(dot(cross(axisU,axisV),direction)>1e-8,"Deformazione: appoggio degenere.");
    struct Contact {Vec3 point,normal;};
    std::map<std::pair<double,double>,Contact> contacts;
    const auto contact=[&](double x,double y) {
        const auto key=std::make_pair(x,y);const auto found=contacts.find(key);
        if(found!=contacts.end())return found->second;
        require(contacts.size()<200000,"Deformazione: troppe interrogazioni sull'appoggio multiplo.");
        const auto base=d[0]+x*axisU+y*axisV;
        bool have=false;Contact selected;
        for(auto &patch:patches) {
            cancellationPoint();Interval range;
            if(!clipLineToBox(base,direction,patch.bounds.padded(precision),range))continue;
            const auto &face=patch.body->face(patch.face);const auto &surface=*face.surface;
            std::vector<double> hits;
            if(surface.type()==SurfaceType::BSpline) {
                const double margin=std::max(100*precision,.001*patch.bounds.diagonal());
                range={range.lo-margin,range.hi+margin};
                hits=numericCurveSurface(Line<3>(base,direction),range,surface,precision).parameters;
            } else {
                bool grazing=false;hits=intersectLineSurface(base,direction,surface,grazing,precision);
                require(!grazing,"Deformazione: appoggio tangente alla direzione di proiezione.");
            }
            for(double t:hits) {
                const auto point=base+t*direction;
                if(patch.classifier.classify(point)==PointLocation::Outside)continue;
                auto uv=patch.seed;
                if(!invertPoint(surface,point,uv,precision,std::max(1.,patch.bounds.diagonal())))continue;
                patch.seed=uv;
                const auto normal=(face.sense?1.:-1.)*surface.normal(uv.x(),uv.y());
                // Una falda rivolta dalla parte opposta non appartiene al
                // grafico regolare dell'appoggio scelto dalla prima faccia.
                if(dot(normal,direction)<=1e-6)continue;
                const Contact current{surface.point(uv.x(),uv.y()),normal};
                if(have) {
                    require(distance(current.point,selected.point)<=2*precision,
                            "Deformazione: superfici destinazione sovrapposte lungo la normale all'appoggio.");
                    require(distance(current.normal,selected.normal)<=1e-4,
                            "Deformazione: le superfici destinazione hanno una giunzione non tangente.");
                } else {selected=current;have=true;}
            }
        }
        require(have,"Deformazione: un punto non incontra le facce destinazione; controllare vuoti, estensione e verso dell'appoggio.");
        contacts.emplace(key,selected);return selected;
    };
    const double cosine=std::cos(options.angle),sine=std::sin(options.angle);
    const auto map=[&](const Vec3 &point) {
        cancellationPoint();const auto p=options.sourcePlane.toLocal(point);
        const auto hit=contact(cosine*p.x()-sine*p.y()+options.shift.x(),
                               sine*p.x()+cosine*p.y()+options.shift.y());
        return hit.point+(p.z()+options.normalOffset)*hit.normal;
    };
    return mapBody(source,map,options.tolerance,history,deviation);
}

}
