#include "cad_drawing_derived.h"
#include "fk_intersect.h"
#include "fk_curve_ops.h"
#include <QLineF>
#include "fk_profile.h"
#include "fk_classify.h"
#include "fk_tessellate.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
namespace ForgeCad {
namespace {
using namespace Kernel;
void require(bool value,const char *why){if(!value)throw std::invalid_argument(why);}
const DrawingViewLayout &layoutFor(const DrawingSheet &s,const QUuid &id){for(const auto &l:s.views)if(l.viewId==id)return l;throw std::invalid_argument("Vista madre non disponibile.");}
const DrawingView &viewFor(const DrawingSheet &s,const QUuid &id){for(const auto &v:s.drawing.views)if(v.id==id)return v;throw std::invalid_argument("Definizione della vista madre assente.");}
void derive(const DrawingView &parent,const DrawingViewRelation &r,DrawingView &child,bool preserveLegacy=true){
    const auto x=parent.plane.xDir(),y=parent.plane.yDir(),z=parent.plane.zDir();
    if(r.kind==DrawingDerivedKind::Detail){
        const auto id=child.id;const double scale=child.scale;child=parent;child.id=id;child.scale=scale;return;
    }
    child.scale=parent.scale;child.tolerance=parent.tolerance;
    if(r.kind==DrawingDerivedKind::Orthographic){
        // Il lato e' la posizione della figlia sul foglio; il metodo determina il verso d'osservazione.
        const double sign=r.method==DrawingProjectionMethod::FirstAngle?-1:1;
        Vec3 normal,reference;
        switch(r.side){
        case DrawingProjectionSide::Left:normal=-sign*x;reference=sign*z;break;
        case DrawingProjectionSide::Right:normal=sign*x;reference=-sign*z;break;
        case DrawingProjectionSide::Above:normal=sign*y;reference=x;break;
        case DrawingProjectionSide::Below:normal=-sign*y;reference=x;break;
        }
        child.kind=DrawingViewKind::ProjectedEdges;child.sectionStyle=DrawingSectionStyle::Removed;child.plane=Frame3(parent.plane.origin(),normal,reference);
    }else if(r.kind==DrawingDerivedKind::Auxiliary){
        require(std::isfinite(r.auxiliaryAngle)&&std::abs(r.auxiliaryAngle)<=360,"Direzione della vista ausiliaria non valida.");
        const double angle=r.auxiliaryAngle*kPi/180;
        const auto normal=std::cos(angle)*x+std::sin(angle)*y;
        const auto tangent=-std::sin(angle)*x+std::cos(angle)*y;
        // Ribalta il piano madre di 90 gradi attorno alla retta perpendicolare
        // alla direzione scelta: conserva l'orientamento sul foglio, senza specchiare.
        const auto reference=-std::sin(angle)*tangent-std::cos(angle)*z;
        child.kind=DrawingViewKind::ProjectedEdges;child.sectionStyle=DrawingSectionStyle::Removed;
        child.plane=Frame3(parent.plane.origin(),normal,reference);
    }else{
        const auto delta=r.cutEnd-r.cutStart;const double length=std::hypot(delta.x(),delta.y());
        require(std::isfinite(length)&&length>1e-7,"La linea di taglio richiede due punti distinti.");
        const auto tangent=(delta.x()*x+delta.y()*y)/length;
        const double sign=r.reverse?-1:1;const auto normal=sign*cross(tangent,z);
        // I vecchi file conservano il taglio lungo X. Il frame salvato basta a
        // riconoscerli: aprire uno storico non deve cambiarne la presentazione.
        const bool legacy=preserveLegacy&&child.kind==DrawingViewKind::Section
            &&distance(child.plane.xDir(),tangent)<1e-10&&std::abs(dot(child.plane.yDir(),z))>1-1e-10;
        // Ribaltamento di 90 gradi attorno alla linea di taglio, applicato anche
        // agli assi della madre (Rodrigues). La linea mantiene la sua direzione
        // sul foglio: verticale resta verticale, obliqua resta obliqua.
        const auto foldedX=(delta.x()/length)*tangent-sign*(delta.y()/length)*z;
        child.kind=DrawingViewKind::Section;child.sectionReferences=false;
        child.plane=Frame3(parent.plane.toGlobal(Vec3(r.cutStart.x(),r.cutStart.y(),0)),normal,legacy?tangent:foldedX);
    }
}
// Collega soltanto estremi isolati che provengono dalle due facce dello stesso edge.
// Il punto edge/piano deve essere univoco e dentro entrambi i budget locali dichiarati.
// Sono raccordi del solo tratteggio, limitati inoltre a 0,02 mm sul foglio.
int connectTolerantSectionEnds(const Body &body,const DrawingView &view,const std::vector<DrawingCurve> &curves,std::vector<ProfileSegment> &segments){
    struct End { Vec2 point; FaceId face; };
    std::vector<End> ends;
    for(const auto &s:segments)for(const auto point:{s.start(),s.end()}){
        int degree=0;for(const auto &other:segments){degree+=distance(point,other.start())<=view.tolerance;degree+=distance(point,other.end())<=view.tolerance;}
        if(degree==1)ends.push_back({point,curves.at(std::size_t(s.source)).sourceFace});
    }
    if(ends.empty())return 0;
    struct Crossing {Vec2 point;double firstBudget,secondBudget;};
    std::map<std::pair<int,int>,std::vector<Crossing>> crossings;
    const Plane plane(view.plane);
    for(const auto edgeId:body.edges()){
        const auto &edge=body.edge(edgeId);if(!edge.forward.valid()||!edge.backward.valid())continue;
        const auto first=body.finFace(edge.forward),second=body.finFace(edge.backward);
        if(first==second||std::none_of(ends.begin(),ends.end(),[&](const auto &e){return e.face==first;})||std::none_of(ends.begin(),ends.end(),[&](const auto &e){return e.face==second;}))continue;
        const double budget=std::max({view.tolerance,kLinearResolution,edge.tolerance});
        if(budget<=view.tolerance&&body.fin(edge.forward).pcurveTolerance==0&&body.fin(edge.backward).pcurveTolerance==0)continue;
        CurveSurfaceIntersection cut;
        try{cut=intersectCurveSurface(*edge.curve,edge.range,plane,view.tolerance);}catch(const std::exception &){continue;}
        if(!cut.coincident.empty())continue;
        for(double parameter:cut.parameters){const auto point=view.plane.toLocal(edge.curve->point(parameter));if(std::abs(point.z())>view.tolerance)continue;
            const Vec2 local(point.x(),point.y());const double a=budget+body.fin(edge.forward).pcurveTolerance,b=budget+body.fin(edge.backward).pcurveTolerance;
            crossings[{first.index,second.index}].push_back({local,a,b});crossings[{second.index,first.index}].push_back({local,b,a});}
    }
    std::vector<std::vector<std::size_t>> choices(ends.size());
    for(std::size_t i=0;i<ends.size();++i)for(std::size_t j=i+1;j<ends.size();++j){
        if(distance(ends[i].point,ends[j].point)*view.scale>.02)continue;
        const auto found=crossings.find({ends[i].face.index,ends[j].face.index});if(found==crossings.end())continue;int matches=0;
        for(const auto &candidate:found->second)if(distance(ends[i].point,candidate.point)<=candidate.firstBudget&&distance(ends[j].point,candidate.point)<=candidate.secondBudget)++matches;
        if(matches==1){choices[i].push_back(j);choices[j].push_back(i);}
    }
    int count=0;
    for(std::size_t i=0;i<ends.size();++i)if(choices[i].size()==1){const auto j=choices[i].front();if(j<=i||choices[j].size()!=1||choices[j].front()!=i)continue;
        const auto delta=ends[j].point-ends[i].point;const double length=norm(delta);if(length<=view.tolerance)continue;
        segments.push_back({std::make_shared<Line<2>>(ends[i].point,delta/length),{0,length},-1});++count;
    }
    return count;
}
// Recupero grafico per tagli sui bordi condivisi. Riusa le sezioni campionate
// entro 0,001 mm sul foglio; nelle fasce con intersezioni mancanti (numero
// dispari) interseca le facce esatte. Classifica ogni componente nel solido,
// riusando il risultato solo fra eventi di estremita'/tangenza con la stessa
// sequenza ordinata di curve di confine. Il risultato resta solo grafico.
// Non si chiudono catene aperte per vicinanza e non si cambiano riferimenti di quota.
QPainterPath solidSectionMaterial(const Body &body,int region,const DrawingView &view,const std::vector<DrawingCurve> &curves,quint64 feature){
    struct Face {FaceId id;double low,high;};std::vector<Face> faces;Box bounds;
    const auto limits=[&](const Box &b){double low=1e100,high=-1e100;
        for(int mask=0;mask<8;++mask){const auto q=view.plane.toLocal(Vec3(mask&1?b.hi.x():b.lo.x(),mask&2?b.hi.y():b.lo.y(),mask&4?b.hi.z():b.lo.z()));low=std::min(low,q.y());high=std::max(high,q.y());}return std::pair<double,double>{low,high};};
    std::vector<double> levels;
    for(auto id:body.faces())if(body.shell(body.face(id).shell).region.index==region){const auto b=faceBox(body,id);const auto [low,high]=limits(b);faces.push_back({id,low,high});bounds.add(b);}
    if(faces.empty())return {};
    double left=1e100,right=-1e100;const auto [bottom,top]=limits(bounds);
    for(int mask=0;mask<8;++mask){const auto q=view.plane.toLocal(Vec3(mask&1?bounds.hi.x():bounds.lo.x(),mask&2?bounds.hi.y():bounds.lo.y(),mask&4?bounds.hi.z():bounds.lo.z()));left=std::min(left,q.x());right=std::max(right,q.x());}
    left-=1;right+=1;SolidClassifier classifier(body,view.tolerance);
    for(auto vertex:body.vertices())levels.push_back(view.plane.toLocal(body.vertex(vertex).point).y());
    for(const auto &curve:curves)if(curve.kind==DrawingCurveKind::Section&&curve.featureId==feature){
        const auto [low,high]=limits(curveBox(*curve.curve,curve.range));levels.push_back(low);levels.push_back(high);
        for(double t:{curve.range.lo,curve.range.hi})levels.push_back(view.plane.toLocal(curve.curve->point(t)).y());
    }
    const double precision=std::clamp(.005/view.scale,1e-6,1.0),step=std::max(.5/view.scale,4*precision);
    require((top-bottom)/step<20000,"Sezione troppo estesa per la ricostruzione del materiale.");
    for(double y=bottom;y<top;y+=step)levels.push_back(y);
    levels.push_back(top);
    std::sort(levels.begin(),levels.end());levels.erase(std::unique(levels.begin(),levels.end(),[&](double a,double b){return b-a<=view.tolerance;}),levels.end());
    struct CutSegment{Vec2 a,b;int curve;};std::vector<CutSegment> cutSegments;std::vector<double> critical;int curveId=0;
    TessellationOptions chord;chord.deflection=std::clamp(.001/view.scale,1e-7,1.0);chord.angle=.06;
    for(const auto &curve:curves)if(curve.kind==DrawingCurveKind::Section&&curve.featureId==feature){
        ++curveId;const auto samples=sampleCurve(*curve.curve,curve.range,chord);
        critical.push_back(view.plane.toLocal(curve.curve->point(samples.front())).y());critical.push_back(view.plane.toLocal(curve.curve->point(samples.back())).y());
        for(std::size_t i=1;i<samples.size();++i){const auto a=view.plane.toLocal(curve.curve->point(samples[i-1])),b=view.plane.toLocal(curve.curve->point(samples[i]));cutSegments.push_back({{a.x(),a.y()},{b.x(),b.y()},curveId});
            if(i+1<samples.size()){const auto c=view.plane.toLocal(curve.curve->point(samples[i+1]));if((b.y()-a.y())*(c.y()-b.y())<=0)critical.push_back(b.y());}}
    }
    for(auto vertex:body.vertices())critical.push_back(view.plane.toLocal(body.vertex(vertex).point).y());
    std::sort(critical.begin(),critical.end());
    std::map<std::tuple<int,std::vector<int>,int>,bool> classified;
    using Spans=std::vector<Interval>;std::map<double,Spans> scans;
    const auto scan=[&](double y)->const Spans &{
        const auto cached=scans.find(y);if(cached!=scans.end())return cached->second;
        cancellationPoint();require(scans.size()<50000,"Troppi campioni per ricostruire il materiale della sezione.");
        Line<3> line(view.plane.toGlobal(Vec3(left,y,0)),view.plane.xDir());std::vector<double> cuts;std::vector<std::pair<double,int>> crossings;
        for(const auto &s:cutSegments)if((s.a.y()<=y&&s.b.y()>y)||(s.b.y()<=y&&s.a.y()>y))crossings.push_back({s.a.x()+(s.b.x()-s.a.x())*(y-s.a.y())/(s.b.y()-s.a.y()),s.curve});
        std::sort(crossings.begin(),crossings.end());for(const auto &hit:crossings)if(cuts.empty()||hit.first-cuts.back()>4*view.tolerance)cuts.push_back(hit.first);
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[&](double a,double b){return b-a<=4*view.tolerance;}),cuts.end());
        const bool exact=cuts.size()%2;
        if(exact){cuts.clear();
        for(const auto &face:faces){
            if(y<face.low-view.tolerance||y>face.high+view.tolerance)continue;
            const auto hits=intersectCurveSurface(line,{0,right-left},*body.face(face.id).surface,view.tolerance);
            for(double t:hits.parameters)if(classifyPointOnFace(body,face.id,line.point(t),view.tolerance)!=PointLocation::Outside)cuts.push_back(left+t);
        }
        }
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[&](double a,double b){return b-a<=4*view.tolerance;}),cuts.end());Spans spans;
        std::vector<int> signature;for(const auto &hit:crossings)signature.push_back(hit.second);
        const int band=int(std::upper_bound(critical.begin(),critical.end(),y)-critical.begin());
        const auto inside=[&](std::size_t i){
            const auto classify=[&]{return classifier.classify(view.plane.toGlobal(Vec3(.5*(cuts[i]+cuts[i-1]),y,0)))==PointLocation::Inside;};
            const auto event=std::lower_bound(critical.begin(),critical.end(),y-view.tolerance);
            if(exact||(event!=critical.end()&&std::abs(*event-y)<=view.tolerance))return classify();
            const auto key=std::make_tuple(band,signature,int(i));const auto old=classified.find(key);if(old!=classified.end())return old->second;
            const bool value=classify();classified.emplace(key,value);return value;
        };
        for(std::size_t i=1;i<cuts.size();++i)if(cuts[i]-cuts[i-1]>4*view.tolerance&&inside(i)){
            if(!spans.empty()&&cuts[i-1]-spans.back().hi<=4*view.tolerance)spans.back().hi=cuts[i];else spans.push_back({cuts[i-1],cuts[i]});
        }
        return scans.emplace(y,std::move(spans)).first->second;
    };
    QPainterPath material;material.setFillRule(Qt::WindingFill);
    const auto point=[&](double x,double y){return QPointF(x*view.scale,-y*view.scale);};
    const auto band=[&](double low,double high,const Spans &a,const Spans &b){for(std::size_t i=0;i<a.size();++i){QPainterPath path;path.moveTo(point(a[i].lo,low));path.lineTo(point(a[i].hi,low));path.lineTo(point(b[i].hi,high));path.lineTo(point(b[i].lo,high));path.closeSubpath();material.addPath(path);}};
    std::function<void(double,double,int)> append=[&](double low,double high,int depth){
        const double mid=.5*(low+high);const auto &a=scan(low),&b=scan(high),&m=scan(mid);
        if(high-low<=precision||depth==24){band(low,high,m,m);return;}
        bool flat=a.size()==b.size()&&a.size()==m.size();
        if(flat)for(std::size_t i=0;i<a.size();++i)if(std::abs(m[i].lo-.5*(a[i].lo+b[i].lo))>precision||std::abs(m[i].hi-.5*(a[i].hi+b[i].hi))>precision)flat=false;
        if(flat){band(low,high,a,b);return;}
        append(low,mid,depth+1);append(mid,high,depth+1);
    };
    for(std::size_t i=1;i<levels.size();++i)if(levels[i]>levels[i-1])append(levels[i-1],levels[i],0);
    return material.simplified();
}

bool finite(QPointF p){return std::isfinite(p.x())&&std::isfinite(p.y())&&std::abs(p.x())<=1e7&&std::abs(p.y())<=1e7;}
}
QPointF constrainDrawingViewPosition(const DrawingSheet &sheet,const DrawingViewLayout &layout,QPointF p){
    const auto &r=layout.relation;if(r.parentId.isNull()||!r.aligned)return p;
    const auto &parent=layoutFor(sheet,r.parentId);
    if(r.kind==DrawingDerivedKind::Auxiliary){
        const double angle=r.auxiliaryAngle*kPi/180;const QPointF direction(std::cos(angle),-std::sin(angle));
        return parent.position+QPointF::dotProduct(p-parent.position,direction)*direction;
    }
    if(r.kind!=DrawingDerivedKind::Orthographic)return p;
    if(r.side==DrawingProjectionSide::Left||r.side==DrawingProjectionSide::Right)p.setY(parent.position.y());else p.setX(parent.position.x());
    return p;
}
QString synchronizeDrawingViews(DrawingSheet &sheet){
    try{
        QSet<QUuid> visiting,done;
        std::function<void(const QUuid &)> visit=[&](const QUuid &id){
            if(done.contains(id))return;
            require(!visiting.contains(id),"Ciclo nelle dipendenze delle viste.");
            require(visiting.size()<256,"Gerarchia delle viste troppo profonda.");visiting.insert(id);
            auto &layout=const_cast<DrawingViewLayout &>(layoutFor(sheet,id));const auto &r=layout.relation;
            if(!r.parentId.isNull()){
                require(r.kind==DrawingDerivedKind::Orthographic||r.kind==DrawingDerivedKind::Section||r.kind==DrawingDerivedKind::Detail||r.kind==DrawingDerivedKind::Auxiliary,"Tipo di vista derivata non valido.");
                visit(r.parentId);const auto &parent=viewFor(sheet,r.parentId);
                require(r.kind==DrawingDerivedKind::Detail||parent.kind==DrawingViewKind::ProjectedEdges,"La vista madre deve essere una proiezione.");
                require(layoutFor(sheet,r.parentId).relation.kind!=DrawingDerivedKind::Detail,"Non sono supportati dettagli di altri dettagli.");
                auto &child=const_cast<DrawingView &>(viewFor(sheet,id));derive(parent,r,child);
                layout.features=layoutFor(sheet,r.parentId).features;layout.position=constrainDrawingViewPosition(sheet,layout,layout.position);
            }
            visiting.remove(id);done.insert(id);
        };
        for(const auto &v:sheet.views)visit(v.viewId);
        return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString alignDrawingSectionOrientation(DrawingSheet &sheet,const QUuid &id){
    try{
        const auto &layout=layoutFor(sheet,id);require(layout.relation.kind==DrawingDerivedKind::Section&&!layout.relation.parentId.isNull(),"Seleziona una sezione da linea di taglio.");
        const auto &parent=viewFor(sheet,layout.relation.parentId);require(parent.kind==DrawingViewKind::ProjectedEdges,"La vista madre deve essere una proiezione.");
        auto prepared=viewFor(sheet,id);derive(parent,layout.relation,prepared,false);const_cast<DrawingView &>(viewFor(sheet,id))=std::move(prepared);return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString validateDrawingRelations(const DrawingSheet &sheet){
    try{
        QSet<QString> labels;
        for(const auto &l:sheet.views){const auto &r=l.relation;
            if(r.parentId.isNull()){require(r.kind==DrawingDerivedKind::None,"Relazione della vista senza madre.");continue;}
            require(r.kind==DrawingDerivedKind::Orthographic||r.kind==DrawingDerivedKind::Section||r.kind==DrawingDerivedKind::Detail||r.kind==DrawingDerivedKind::Auxiliary,"Tipo di relazione non valido.");
            if(r.kind==DrawingDerivedKind::Auxiliary)require(std::isfinite(r.auxiliaryAngle)&&std::abs(r.auxiliaryAngle)<=360,"Direzione della vista ausiliaria non valida.");
            require(r.side>=DrawingProjectionSide::Left&&r.side<=DrawingProjectionSide::Below,"Lato della vista derivata non valido.");
            require(r.method==DrawingProjectionMethod::FirstAngle||r.method==DrawingProjectionMethod::ThirdAngle,"Metodo di proiezione non valido.");
            if(r.kind==DrawingDerivedKind::Detail){
                require(finite(r.detailCenter)&&std::isfinite(r.detailRadius)&&r.detailRadius>1e-7&&r.detailRadius<=1e6,"Ritaglio del dettaglio non valido.");
                require(!r.aligned,"Il dettaglio ha posizione indipendente.");
            }
            if(r.kind==DrawingDerivedKind::Section||r.kind==DrawingDerivedKind::Detail){
                if(r.kind==DrawingDerivedKind::Section)require(finite(r.cutStart)&&finite(r.cutEnd)&&QLineF(r.cutStart,r.cutEnd).length()>1e-7,"Linea di taglio non valida.");
                require(!r.label.isEmpty()&&r.label.size()<=8,"Identificativo della sezione non valido.");
                for(auto c:r.label)require(c.isLetterOrNumber(),"L'identificativo della sezione deve essere alfanumerico.");
                const auto label=r.label.toUpper();require(!labels.contains(label),"Identificativo della sezione duplicato.");labels.insert(label);
            }
        }
        auto expected=sheet;const auto error=synchronizeDrawingViews(expected);if(!error.isEmpty())return error;
        for(const auto &l:sheet.views)if(!l.relation.parentId.isNull()){
            const auto &a=viewFor(sheet,l.viewId),&b=viewFor(expected,l.viewId);const auto &p=layoutFor(expected,l.viewId);
            require(a.kind==b.kind&&a.scale==b.scale&&a.tolerance==b.tolerance&&l.features==p.features
                &&distance(a.plane.origin(),b.plane.origin())<1e-9&&distance(a.plane.xDir(),b.plane.xDir())<1e-10
                &&distance(a.plane.yDir(),b.plane.yDir())<1e-10&&distance(a.plane.zDir(),b.plane.zDir())<1e-10
                &&QLineF(l.position,p.position).length()<1e-8,"Vista derivata incoerente con la madre.");
        }
        return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString drawingSectionHatch(const DrawingModelSnapshot &model,const DrawingView &view,const std::vector<DrawingCurve> &curves,DrawingSectionHatch &result){
    try{
        DrawingSectionHatch prepared;if(view.kind!=DrawingViewKind::Section){result=std::move(prepared);return {};}
        if(drawingHalfSection(view)){
            auto full=view;full.sectionStyle=DrawingSectionStyle::Removed;std::vector<DrawingCurve> closed;const auto cut=sectionDrawingView(model,full,closed);if(!cut.isEmpty())return cut;
            full.sectionStyle=DrawingSectionStyle::Full;const auto error=drawingSectionHatch(model,full,closed,prepared);if(!error.isEmpty())return error;
            const auto bounds=prepared.material.boundingRect();QPainterPath half;
            if(view.sectionStyle==DrawingSectionStyle::HalfRight)half.addRect(QRectF(0,bounds.top()-1,std::max(1.0,bounds.right()+1),bounds.height()+2));
            else half.addRect(QRectF(std::min(-1.0,bounds.left()-1),bounds.top()-1,std::max(1.0,-bounds.left()+1),bounds.height()+2));
            prepared.material=prepared.material.intersected(half);QPainterPath lines;
            for(int i=0;i+1<prepared.lines.elementCount();i+=2){const auto x=prepared.lines.elementAt(i),y=prepared.lines.elementAt(i+1);QPointF a(x.x,x.y),b(y.x,y.y);const double sign=view.sectionStyle==DrawingSectionStyle::HalfRight?1:-1;
                const double fa=sign*a.x(),fb=sign*b.x();if(fa<0&&fb<0)continue;if(fa<0||fb<0){const auto point=a+(b-a)*(fa/(fa-fb));if(fa<0)a=point;else b=point;}lines.moveTo(a);lines.lineTo(b);}
            prepared.lines=std::move(lines);result=std::move(prepared);return {};
        }
        std::map<std::pair<quint64,int>,std::vector<ProfileSegment>> groups;QSet<quint64> uncertain;
        for(const auto &piece:curves){
            if(piece.kind!=DrawingCurveKind::Section)continue;
            const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &b){return b.featureId==piece.featureId;});
            require(source!=model.bodies.end()&&source->body&&piece.sourceFace.valid(),"Sorgente della sezione non disponibile.");
            const auto &body=*source->body;const auto region=body.shell(body.face(piece.sourceFace).shell).region;
            if(!body.region(region).solid)continue; // anche un cilindro di sola superficie puo' produrre un contorno chiuso
            if(piece.coincident||piece.tangent){uncertain.insert(piece.featureId);continue;}
            const auto image=planarImage(*piece.curve,piece.range,view.plane);
            if(!image.curve){uncertain.insert(piece.featureId);continue;}
            groups[{piece.featureId,region.index}].push_back({image.curve,image.range,int(&piece-curves.data())});
        }
        int index=0;
        for(const auto &[key,original]:groups){
            if(uncertain.contains(key.first))continue;
            auto segments=original;const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &b){return b.featureId==key.first;});
            const int connections=connectTolerantSectionEnds(*source->body,view,curves,segments);
            // Nodi con piu' di due rami rendono la scelta dei loop ambigua: nessun riempimento arbitrario.
            bool ambiguous=false;
            for(const auto &s:segments)for(const auto point:{s.start(),s.end()}){
                int degree=0;for(const auto &other:segments){degree+=distance(point,other.start())<=view.tolerance;degree+=distance(point,other.end())<=view.tolerance;}
                if(degree!=2)ambiguous=true;
            }
            const auto profile=buildProfile(segments,view.tolerance);
            QPainterPath material;material.setFillRule(Qt::OddEvenFill);
            if(ambiguous||profile.openChains){
                // Recupero ammesso solo per bordi esatti condivisi da due facce,
                // non per una generica catena aperta o per una superficie.
                bool shared=false;
                for(std::size_t i=0;i<original.size()&&!shared;++i)for(std::size_t j=0;j<i;++j){
                    const auto &a=curves.at(original[i].source),&b=curves.at(original[j].source);
                    if(a.sourceFace!=b.sourceFace&&a.curve==b.curve&&a.range.lo==b.range.lo&&a.range.hi==b.range.hi){
                        for(auto edgeId:source->body->edges()){
                            const auto &edge=source->body->edge(edgeId);if(edge.curve!=a.curve||!edge.forward.valid()||!edge.backward.valid())continue;
                            const auto first=source->body->finFace(edge.forward),second=source->body->finFace(edge.backward);
                            if((first==a.sourceFace&&second==b.sourceFace)||(first==b.sourceFace&&second==a.sourceFace))shared=true;
                        }
                    }
                }
                if(shared)material=solidSectionMaterial(*source->body,key.second,view,curves,key.first);
                if(material.isEmpty()){prepared.warnings.append(QStringLiteral("Tratteggio omesso: contorno aperto o ambiguo nel corpo %1.").arg(key.first));continue;}
                prepared.warnings.append(QStringLiteral("Campitura grafica ricostruita dal solido sui bordi condivisi (corpo %1).").arg(key.first));
            }
            if(connections)prepared.warnings.append(QStringLiteral("Tratteggio con %1 connessioni grafiche entro le tolleranze locali del B-rep (corpo %2).").arg(connections).arg(key.first));
            const auto loopPath=[&](const ProfileLoop &loop){QPainterPath path;bool first=true;
                for(const auto &segment:loop.segments){const auto world=embedCurve(segment.curve,view.plane);TessellationOptions options;options.deflection=std::clamp(.02/view.scale,1e-6,1.0);options.angle=.1;const auto points=sampleCurve(*world,segment.range,options);
                    for(const auto parameter:points){const auto q=view.plane.toLocal(world->point(parameter));const QPointF point(q.x()*view.scale,-q.y()*view.scale);if(first){path.moveTo(point);first=false;}else path.lineTo(point);}}
                path.closeSubpath();return path;};
            if(material.isEmpty())for(const auto &region:profile.regions){material.addPath(loopPath(region.outer));for(const auto &hole:region.holes)material.addPath(loopPath(hole));}
            prepared.material=prepared.material.united(material);
            // Intersezione delle righe con i poligoni campionati; parita' conserva i fori. Passo fisso sul foglio.
            const bool alternate=(index++%2)!=0;const auto polygons=material.toSubpathPolygons();const auto bounds=material.boundingRect();
            const auto ordinate=[&](QPointF p){return p.y()+(alternate?p.x():-p.x());};
            double lo=1e100,hi=-1e100;for(auto p:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight()}){lo=std::min(lo,ordinate(p));hi=std::max(hi,ordinate(p));}
            require((hi-lo)/2.5<100000,"Sezione troppo estesa per il tratteggio.");
            for(double level=std::ceil(lo/2.5)*2.5;level<hi;level+=2.5){std::vector<double> cuts;
                for(const auto &poly:polygons)for(int i=1;i<poly.size();++i){const auto a=poly[i-1],b=poly[i];const double u=ordinate(a),v=ordinate(b);if((u<=level&&v>level)||(v<=level&&u>level))cuts.push_back(a.x()+(b.x()-a.x())*(level-u)/(v-u));}
                std::sort(cuts.begin(),cuts.end());for(std::size_t i=1;i<cuts.size();i+=2){const double a=cuts[i-1],b=cuts[i];if(b-a<1e-8)continue;prepared.lines.moveTo(a,level+(alternate?-a:a));prepared.lines.lineTo(b,level+(alternate?-b:b));}
            }
        }
        prepared.material.setFillRule(Qt::OddEvenFill);
        for(auto feature:uncertain)prepared.warnings.append(QStringLiteral("Tratteggio omesso: taglio tangente o coincidente nel corpo %1.").arg(feature));
        result=std::move(prepared);return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
}
