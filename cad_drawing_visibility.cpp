#include "cad_drawing_visibility.h"
#include "cad_drawing_dimension.h"
#include "fk_classify.h"
#include "fk_surface_algo.h"
#include <QSet>
#include <QLineF>
#include <QTransform>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <set>
#include <stdexcept>
namespace ForgeCad {
namespace {
using namespace Kernel;
void need(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
struct Bounds {
    double x0=1e300,y0=1e300,x1=-1e300,y1=-1e300;
    void add(Vec3 p){x0=std::min(x0,p.x());x1=std::max(x1,p.x());y0=std::min(y0,p.y());y1=std::max(y1,p.y());}
    void add(const Bounds &b){x0=std::min(x0,b.x0);x1=std::max(x1,b.x1);y0=std::min(y0,b.y0);y1=std::max(y1,b.y1);}
    bool overlaps(const Bounds &b)const{return x0<=b.x1&&x1>=b.x0&&y0<=b.y1&&y1>=b.y0;}
};
double cross2(Vec3 a,Vec3 b){return a.x()*b.y()-a.y()*b.x();}
struct DisplaySource {quint64 featureId=0;std::vector<FaceId> faces;const Surface *surface=nullptr;double extraError=0;};
struct Triangle {Vec3 a,b,c;Bounds bounds;double area=0,bias=0,selfBias=0,depthScale=0;quint64 featureId=0;FaceId face;};
struct Node {Bounds bounds;std::size_t start=0,end=0;int left=-1,right=-1;};
std::vector<Vec3> clipPolygon(const std::vector<Vec3> &input,int axis,double sign){
    std::vector<Vec3> result;if(input.empty())return result;auto a=input.back();double fa=sign*a[axis];
    for(const auto b:input){const double fb=sign*b[axis];if((fa<0)!=(fb<0))result.push_back(a+(b-a)*(fa/(fa-fb)));if(fb>=0)result.push_back(b);a=b;fa=fb;}return result;
}
std::vector<std::vector<Vec3>> retainedTriangles(Vec3 a,Vec3 b,Vec3 c,const DrawingView &view){
    const std::vector<Vec3> polygon{a,b,c};if(view.sectionStyle==DrawingSectionStyle::Removed)return {polygon};
    if(view.sectionStyle==DrawingSectionStyle::Full)return {clipPolygon(polygon,2,-1)};
    const double side=view.sectionStyle==DrawingSectionStyle::HalfRight?1:-1;
    return {clipPolygon(polygon,0,-side),clipPolygon(clipPolygon(polygon,0,side),2,-1)};
}
class Occlusion {
    std::vector<Triangle> triangles_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
    double capBias_=0;
    QPainterPath cap_;
    QList<QPolygonF> capLoops_;
    std::vector<Interval> capHidden(Vec3 a,Vec3 b)const{
        std::vector<Interval> result;if(cap_.isEmpty())return result;const Vec3 d=b-a;std::vector<double> cuts{0,1};
        if(d.z()!=0){const double t=(-capBias_-a.z())/d.z();if(t>0&&t<1)cuts.push_back(t);}
        const QPointF start(a.x(),a.y()),direction(d.x(),d.y());const auto cross=[](QPointF p,QPointF q){return p.x()*q.y()-p.y()*q.x();};
        for(const auto &loop:capLoops_)for(int i=1;i<loop.size();++i){const auto p=loop[i-1],edge=loop[i]-p;const double det=cross(direction,edge);if(std::abs(det)<=1e-15)continue;
            const double t=cross(p-start,edge)/det,u=cross(p-start,direction)/det;if(t>0&&t<1&&u>=0&&u<=1)cuts.push_back(t);}
        std::sort(cuts.begin(),cuts.end());
        for(std::size_t i=1;i<cuts.size();++i){const auto q=a+(.5*(cuts[i-1]+cuts[i]))*d;if(q.z()<-capBias_&&cap_.contains(QPointF(q.x(),q.y())))result.push_back({cuts[i-1],cuts[i]});}
        return result;
    }
    int build(std::size_t start,std::size_t end){Node node;node.start=start;node.end=end;for(auto i=start;i<end;++i)node.bounds.add(triangles_[order_[i]].bounds);const int id=int(nodes_.size());nodes_.push_back(node);
        if(end-start>8){const int axis=node.bounds.x1-node.bounds.x0>=node.bounds.y1-node.bounds.y0?0:1;const auto mid=start+(end-start)/2;
            std::nth_element(order_.begin()+start,order_.begin()+mid,order_.begin()+end,[&](auto a,auto b){const auto &x=triangles_[a].bounds,&y=triangles_[b].bounds;return axis?x.y0+x.y1<y.y0+y.y1:x.x0+x.x1<y.x0+y.x1;});
            const int left=build(start,mid),right=build(mid,end);nodes_[id].left=left;nodes_[id].right=right;}return id;}
    // Intersezione esatta del segmento grafico con l'impronta del triangolo e
    // con il semispazio dietro il suo piano. Conserva anche occlusioni molto corte.
    static bool covered(const Triangle &t,Vec3 a,Vec3 b,const DisplaySource &source,double &lo,double &hi){lo=0;hi=1;const double sign=t.area>0?1:-1;
        const auto clip=[&](double fa,double fb){if(fa<0&&fb<0)return false;if(fa>=0&&fb>=0)return true;const double q=fa/(fa-fb);if(fa<0)lo=std::max(lo,q);else hi=std::min(hi,q);return hi>lo;};
        const Vec3 vertices[]={t.a,t.b,t.c};for(int i=0;i<3;++i){const auto p=vertices[i],edge=vertices[(i+1)%3]-p;if(!clip(sign*cross2(edge,a-p),sign*cross2(edge,b-p)))return false;}
        // La corda grafica di un contorno tangente puo' passare appena dentro
        // la sua superficie. L'errore normale si amplifica in profondita'
        // nelle viste radenti; questo margine riguarda solo le facce d'origine.
        const bool self=t.featureId==source.featureId&&std::find(source.faces.begin(),source.faces.end(),t.face)!=source.faces.end();
        const double bias=self?t.selfBias+source.extraError*t.depthScale:t.bias;
        const auto depth=[&](Vec3 p){const double u=cross2(t.b-p,t.c-p)/t.area,v=cross2(t.c-p,t.a-p)/t.area;return u*t.a.z()+v*t.b.z()+(1-u-v)*t.c.z()-p.z()-bias;};
        return clip(depth(a),depth(b))&&hi-lo>1e-12;
    }
    void query(int id,const Bounds &box,Vec3 a,Vec3 b,const DisplaySource &source,std::vector<Interval> &hits)const{const auto &node=nodes_[id];if(!node.bounds.overlaps(box))return;if(node.left>=0){query(node.left,box,a,b,source,hits);query(node.right,box,a,b,source,hits);return;}
        for(auto i=node.start;i<node.end;++i){const auto &triangle=triangles_[order_[i]];if(!triangle.bounds.overlaps(box))continue;double lo,hi;if(covered(triangle,a,b,source,lo,hi))hits.push_back({lo,hi});}}
public:
    Occlusion(const DrawingModelSnapshot &model,const DrawingView &view,DrawingSilhouetteCache &cache,QStringList &warnings,const QPainterPath *material){QSet<quint64> ids;
        if(view.sectionStyle!=DrawingSectionStyle::Removed){need(material,"Materiale della sezione non preparato per la visibilita'.");capBias_=4*view.tolerance;cap_=QTransform::fromScale(1/view.scale,-1/view.scale).map(*material);capLoops_=cap_.toSubpathPolygons();}
        for(const auto &source:model.bodies){cancellationPoint();need(source.featureId&&!ids.contains(source.featureId)&&source.body&&source.names&&source.names->body==source.body.get(),"Sorgente della visibilita' incoerente.");ids.insert(source.featureId);
            // Il margine di profondita' segue la precisione sul foglio, non il
            // box conservativo delle superfici (che puo' essere molto piu' grande del pezzo).
            const double deflection=std::clamp(.005/view.scale,1e-6,1.0);
            const auto mesh=cache.seeds(source,deflection);if(mesh->failedFaces)warnings.append(QStringLiteral("Visibilita' incompleta: %1 facce non tassellate nella sorgente %2.").arg(mesh->failedFaces).arg(source.featureId));
            for(const auto &face:mesh->faces){cancellationPoint();const bool planar=source.body->face(face.face).surface->type()==SurfaceType::Plane;
                for(const auto &indices:face.triangles){const auto a=view.plane.toLocal(face.points.at(indices[0])),b=view.plane.toLocal(face.points.at(indices[1])),c=view.plane.toLocal(face.points.at(indices[2]));
                    need(isFinite(a)&&isFinite(b)&&isFinite(c),"Triangolo non finito nella visibilita'.");
                    for(const auto &polygon:retainedTriangles(a,b,c,view))for(std::size_t i=2;i<polygon.size();++i){Triangle triangle;triangle.a=polygon[0];triangle.b=polygon[i-1];triangle.c=polygon[i];triangle.area=cross2(triangle.b-triangle.a,triangle.c-triangle.a);
                        const double side=std::max({norm(triangle.b-triangle.a),norm(triangle.c-triangle.a),norm(triangle.c-triangle.b)});if(std::abs(triangle.area)<=1e-12*side*side)continue;
                        triangle.bias=std::max(4*view.tolerance,planar?0.0:2*deflection);
                        const auto normal=normalized(cross(triangle.b-triangle.a,triangle.c-triangle.a));
                        const double cosine=std::abs(normal.z());double deviation=deflection;
                        if(!planar){
                            need(face.parameters.size()==face.points.size(),"Parametri della mesh assenti per la visibilita'.");
                            const auto u=face.parameters.at(indices[0]),v=face.parameters.at(indices[1]),w=face.parameters.at(indices[2]);
                            // Ai bordi tolleranti o ai poli lo scarto effettivo puo'
                            // superare quello richiesto al tassellatore: misuralo.
                            for(const auto uv:{(u+v+w)/3.0,(u+v)/2.0,(v+w)/2.0,(w+u)/2.0}){
                                const auto point=view.plane.toLocal(source.body->face(face.face).surface->point(uv.x(),uv.y()));
                                deviation=std::max(deviation,std::abs(dot(point-triangle.a,normal)));
                            }
                        }
                        triangle.depthScale=planar?0:1/std::max(cosine,1e-8);
                        triangle.selfBias=planar?triangle.bias:std::max(triangle.bias,(2*deviation+.03/view.scale)*triangle.depthScale);
                        triangle.featureId=source.featureId;triangle.face=face.face;triangle.bounds.add(triangle.a);triangle.bounds.add(triangle.b);triangle.bounds.add(triangle.c);triangles_.push_back(triangle);
                        need(triangles_.size()<=2000000,"Troppi triangoli per la visibilita' della tavola.");}}
            }
        }
        order_.resize(triangles_.size());std::iota(order_.begin(),order_.end(),0);if(!order_.empty())build(0,order_.size());
    }
    std::vector<Interval> hidden(Vec3 a,Vec3 b,const DisplaySource &source)const{std::vector<Interval> hits=capHidden(a,b);Bounds box;box.add(a);box.add(b);if(!nodes_.empty())query(0,box,a,b,source,hits);
        std::sort(hits.begin(),hits.end(),[](const auto &a,const auto &b){return a.lo<b.lo;});std::vector<Interval> merged;for(const auto &hit:hits){if(!merged.empty()&&hit.lo<=merged.back().hi+1e-10)merged.back().hi=std::max(merged.back().hi,hit.hi);else merged.push_back(hit);}return merged;}
};
void renderShadedMesh(const DrawingModelSnapshot &model,const DrawingView &view,DrawingSilhouetteCache &cache,QImage &image,QRectF &bounds){
    struct RenderTriangle{Vec3 p[3],n[3];QRgb color;};std::vector<RenderTriangle> triangles;QRectF box;bool first=true;
    for(const auto &source:model.bodies){const auto mesh=cache.seeds(source);
        for(const auto &face:mesh->faces)for(const auto &indices:face.triangles){cancellationPoint();RenderTriangle original;
            for(int i=0;i<3;++i){const auto id=indices[i];original.p[i]=view.plane.toLocal(face.points.at(id));const auto n=face.normals.at(id);original.n[i]={dot(n,view.plane.xDir()),dot(n,view.plane.yDir()),dot(n,view.plane.zDir())};}
            original.color=source.color;const double area=cross2(original.p[1]-original.p[0],original.p[2]-original.p[0]);if(std::abs(area)<1e-18)continue;
            for(const auto &polygon:retainedTriangles(original.p[0],original.p[1],original.p[2],view))for(std::size_t i=2;i<polygon.size();++i){RenderTriangle clipped;clipped.color=original.color;const Vec3 vertices[]={polygon[0],polygon[i-1],polygon[i]};
                for(int k=0;k<3;++k){const auto p=vertices[k];clipped.p[k]=p;const double a=cross2(original.p[1]-p,original.p[2]-p)/area,b=cross2(original.p[2]-p,original.p[0]-p)/area;clipped.n[k]=a*original.n[0]+b*original.n[1]+(1-a-b)*original.n[2];const QPointF q(p.x()*view.scale,-p.y()*view.scale);if(first){box={q,QSizeF(0,0)};first=false;}else box=QRectF(QPointF(std::min(box.left(),q.x()),std::min(box.top(),q.y())),QPointF(std::max(box.right(),q.x()),std::max(box.bottom(),q.y())));}
                triangles.push_back(clipped);need(triangles.size()<=2000000,"Mesh ombreggiata troppo complessa.");}
        }
    }
    if(first||box.width()<1e-12||box.height()<1e-12)return;
    box.adjust(-.05,-.05,.05,.05);const double density=std::min(8.0,3072/std::max(box.width(),box.height()));const int width=std::max(1,int(std::ceil(box.width()*density))),height=std::max(1,int(std::ceil(box.height()*density)));
    image=QImage(width,height,QImage::Format_ARGB32_Premultiplied);need(!image.isNull(),"Memoria insufficiente per la vista ombreggiata.");image.fill(Qt::transparent);std::vector<double> depth(std::size_t(width)*height,-std::numeric_limits<double>::infinity());
    const Vec3 key=normalized(Vec3(.5,.7,1)),fill=normalized(Vec3(-.6,.3,.5)),halfway=normalized(key+Vec3(0,0,1));
    for(const auto &triangle:triangles){cancellationPoint();Vec3 screen[3];for(int i=0;i<3;++i)screen[i]={((triangle.p[i].x()*view.scale)-box.left())*density,((-triangle.p[i].y()*view.scale)-box.top())*density,triangle.p[i].z()};const double area=cross2(screen[1]-screen[0],screen[2]-screen[0]);if(std::abs(area)<1e-12)continue;
        const int x0=std::clamp(int(std::floor(std::min({screen[0].x(),screen[1].x(),screen[2].x()}))),0,width-1),x1=std::clamp(int(std::ceil(std::max({screen[0].x(),screen[1].x(),screen[2].x()}))),0,width-1);
        const int y0=std::clamp(int(std::floor(std::min({screen[0].y(),screen[1].y(),screen[2].y()}))),0,height-1),y1=std::clamp(int(std::ceil(std::max({screen[0].y(),screen[1].y(),screen[2].y()}))),0,height-1);
        for(int y=y0;y<=y1;++y){cancellationPoint();auto *pixels=reinterpret_cast<QRgb *>(image.scanLine(y));for(int x=x0;x<=x1;++x){const Vec3 p(x+.5,y+.5,0);const double a=cross2(screen[1]-p,screen[2]-p)/area,b=cross2(screen[2]-p,screen[0]-p)/area,c=1-a-b;if(a<-1e-9||b<-1e-9||c<-1e-9)continue;const double z=a*screen[0].z()+b*screen[1].z()+c*screen[2].z();auto &old=depth[std::size_t(y)*width+x];if(z<old)continue;old=z;Vec3 n=a*triangle.n[0]+b*triangle.n[1]+c*triangle.n[2];if(norm(n)<1e-12)n={0,0,1};else n=normalized(n);if(n.z()<0)n=-n;
            const double light=.24+.55*std::max(0.,dot(n,key))+.21*std::max(0.,dot(n,fill)),specular=.12*std::pow(std::max(0.,dot(n,halfway)),24);const auto channel=[&](int color){return std::clamp(int(std::lround(color*light+255*specular)),0,255);};pixels[x]=qRgb(channel(qRed(triangle.color)),channel(qGreen(triangle.color)),channel(qBlue(triangle.color)));}}
    }
    bounds=QRectF(box.topLeft(),QSizeF(width/density,height/density));
}

void append(QPainterPath &path,QPointF a,QPointF b){if(QLineF(a,b).length()<1e-10)return;if(path.isEmpty()||QLineF(path.currentPosition(),a).length()>1e-8)path.moveTo(a);path.lineTo(b);}
DrawingVisiblePath display(const std::vector<Vec3> &points,const DrawingView &view,const Occlusion *index,const DisplaySource &source={},const std::function<bool(Vec3,Vec3)> &omit={}){DrawingVisiblePath result;auto displayed=source;
    const auto paper=[&](Vec3 p){const QPointF point(p.x()*view.scale,-p.y()*view.scale);need(std::isfinite(point.x())&&std::isfinite(point.y()),"Tratto non finito sul foglio.");return point;};
    for(std::size_t i=1;i<points.size();++i){cancellationPoint();if(omit&&omit(points[i-1],points[i]))continue;const auto a=view.plane.toLocal(points[i-1]),b=view.plane.toLocal(points[i]);need(isFinite(a)&&isFinite(b),"Curva non finita nella visibilita'.");
        // Misura anche la corda effettiva: un contorno con raffinamento
        // limitato puo' superare la precisione grafica richiesta.
        if(source.surface){const auto projection=projectPoint(*source.surface,.5*(points[i-1]+points[i]));
            need(std::isfinite(projection.distance),"Scarto del contorno non finito.");displayed.extraError=std::max(0.,2*projection.distance-.03/view.scale);}
        std::vector<double> cuts{0,1};if(index&&view.sectionStyle!=DrawingSectionStyle::Removed){for(int axis:{0,2})if(b[axis]!=a[axis]){const double t=-a[axis]/(b[axis]-a[axis]);if(t>0&&t<1)cuts.push_back(t);}std::sort(cuts.begin(),cuts.end());}
        for(std::size_t k=1;k<cuts.size();++k){if(index&&!drawingRetainsPoint(view,a+.5*(cuts[k-1]+cuts[k])*(b-a)))continue;
            const auto first=a+cuts[k-1]*(b-a),last=a+cuts[k]*(b-a);double end=0;const auto hidden=index?index->hidden(first,last,displayed):std::vector<Interval>{};
            for(const auto &part:hidden){append(result.visible,paper(first+end*(last-first)),paper(first+part.lo*(last-first)));append(result.hidden,paper(first+part.lo*(last-first)),paper(first+part.hi*(last-first)));end=part.hi;}
            append(result.visible,paper(first+end*(last-first)),paper(last));}}
    return result;}
// I bordi 3D restano disponibili per gli agganci salvati. Sul foglio,
// ogni intervallo rettilineo compare una sola volta, con precedenza al visibile.
void consolidateLinearPaths(const DrawingView &view,const std::vector<DrawingCurve> &curves,std::vector<DrawingVisiblePath> &paths){
    struct Entry{std::size_t index;double length;};struct Group{QPointF origin,direction;std::vector<Entry> entries;};std::vector<Group> groups;
    const double tolerance=.001;
    const auto paper=[&](Vec2 p){return QPointF(p.x()*view.scale,-p.y()*view.scale);};
    for(std::size_t i=0;i<curves.size();++i){const auto primitive=drawingPrimitive(view,curves[i]);if(primitive.kind!=DrawingPrimitive::Kind::Line)continue;
        const auto local=[&](double t){const auto p=view.plane.toLocal(curves[i].curve->point(t));return Vec2(p.x(),p.y());};
        const auto a=paper(primitive.linearProjection?primitive.lineFirst:local(curves[i].range.lo)),b=paper(primitive.linearProjection?primitive.lineSecond:local(curves[i].range.hi));const auto delta=b-a;const double length=std::hypot(delta.x(),delta.y());if(length<1e-10)continue;
        auto direction=delta/length;if(direction.x()<0||(std::abs(direction.x())<1e-12&&direction.y()<0))direction=-direction;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto &g){const auto offset=a-g.origin;return (!view.sectionReferences||(curves[g.entries.front().index].kind==DrawingCurveKind::Section)==(curves[i].kind==DrawingCurveKind::Section))&&std::abs(g.direction.x()*direction.y()-g.direction.y()*direction.x())<1e-8&&std::abs(offset.x()*direction.y()-offset.y()*direction.x())<=tolerance;});
        if(group==groups.end()){groups.push_back({a,direction,{}});group=std::prev(groups.end());}group->entries.push_back({i,length});
    }
    using Intervals=std::vector<Interval>;
    const auto unite=[&](Intervals spans){std::sort(spans.begin(),spans.end(),[](auto a,auto b){return a.lo<b.lo;});Intervals result;for(auto span:spans){if(!result.empty()&&span.lo<=result.back().hi+tolerance)result.back().hi=std::max(result.back().hi,span.hi);else result.push_back(span);}return result;};
    const auto subtract=[&](Intervals spans,const Intervals &covered){Intervals result;for(auto span:spans){double start=span.lo;for(auto old:covered){if(old.hi<=start+tolerance)continue;if(old.lo>=span.hi-tolerance)break;if(old.lo>start+tolerance)result.push_back({start,old.lo});start=std::max(start,old.hi);}if(start<span.hi-tolerance)result.push_back({start,span.hi});}return result;};
    for(auto &group:groups){
        std::stable_sort(group.entries.begin(),group.entries.end(),[&](auto a,auto b){if(curves[a.index].kind!=curves[b.index].kind){const auto rank=[](DrawingCurveKind k){return k==DrawingCurveKind::Section?0:k==DrawingCurveKind::ProjectedEdge?1:2;};return rank(curves[a.index].kind)<rank(curves[b.index].kind);}const double visibleA=paths[a.index].visible.length(),visibleB=paths[b.index].visible.length();if(std::abs(visibleA-visibleB)>1e-8)return visibleA>visibleB;return a.length>b.length;});
        const auto spans=[&](const QPainterPath &path){Intervals result;for(int i=1;i<path.elementCount();++i){const auto e=path.elementAt(i);if(e.type!=QPainterPath::LineToElement)continue;const auto previous=path.elementAt(i-1);const double a=QPointF::dotProduct(QPointF(previous.x,previous.y)-group.origin,group.direction),b=QPointF::dotProduct(QPointF(e.x,e.y)-group.origin,group.direction);result.push_back({std::min(a,b),std::max(a,b)});}return unite(std::move(result));};
        const auto path=[&](const Intervals &spans){QPainterPath result;for(auto span:spans){result.moveTo(group.origin+span.lo*group.direction);result.lineTo(group.origin+span.hi*group.direction);}return result;};
        Intervals covered;
        for(auto entry:group.entries){auto &visible=paths[entry.index].visible;auto intervals=spans(visible);visible=path(subtract(intervals,covered));covered.insert(covered.end(),intervals.begin(),intervals.end());covered=unite(std::move(covered));}
        for(auto entry:group.entries){auto &hidden=paths[entry.index].hidden;auto intervals=spans(hidden);hidden=path(subtract(intervals,covered));covered.insert(covered.end(),intervals.begin(),intervals.end());covered=unite(std::move(covered));}
    }
}
void consolidateConicPaths(const DrawingView &view,const std::vector<DrawingCurve> &curves,std::vector<DrawingVisiblePath> &paths){
    struct Group{DrawingPrimitive form;std::vector<std::size_t> entries;};std::vector<Group> groups;
    const auto shape=[](const DrawingPrimitive &p){return Vec3(p.firstAxis.x()*p.firstAxis.x()+p.secondAxis.x()*p.secondAxis.x(),p.firstAxis.x()*p.firstAxis.y()+p.secondAxis.x()*p.secondAxis.y(),p.firstAxis.y()*p.firstAxis.y()+p.secondAxis.y()*p.secondAxis.y());};
    for(std::size_t i=0;i<curves.size();++i){const auto form=drawingPrimitive(view,curves[i]);if(form.kind!=DrawingPrimitive::Kind::Conic)continue;auto group=std::find_if(groups.begin(),groups.end(),[&](const auto &g){return (!view.sectionReferences||(curves[g.entries.front()].kind==DrawingCurveKind::Section)==(curves[i].kind==DrawingCurveKind::Section))&&distance(g.form.center,form.center)*view.scale<1e-6&&distance(shape(g.form),shape(form))<1e-8*std::max(1.,norm(shape(form)));});if(group==groups.end()){groups.push_back({form,{}});group=std::prev(groups.end());}group->entries.push_back(i);}
    using Intervals=std::vector<Interval>;const double tolerance=1e-9;
    const auto unite=[&](Intervals spans){std::sort(spans.begin(),spans.end(),[](auto a,auto b){return a.lo<b.lo;});Intervals result;for(auto span:spans){if(!result.empty()&&span.lo<=result.back().hi+tolerance)result.back().hi=std::max(result.back().hi,span.hi);else result.push_back(span);}return result;};
    const auto subtract=[&](const Intervals &spans,const Intervals &covered){Intervals result;for(auto span:spans){double start=span.lo;for(auto old:covered){if(old.hi<=start+tolerance)continue;if(old.lo>=span.hi-tolerance)break;if(old.lo>start+tolerance)result.push_back({start,old.lo});start=std::max(start,old.hi);}if(start<span.hi-tolerance)result.push_back({start,span.hi});}return result;};
    for(auto &group:groups){const auto &f=group.form;const double determinant=f.firstAxis.x()*f.secondAxis.y()-f.firstAxis.y()*f.secondAxis.x();if(std::abs(determinant)<1e-20)continue;
        const auto angle=[&](QPointF p){const Vec2 d(p.x()/view.scale-f.center.x(),-p.y()/view.scale-f.center.y());double t=std::atan2((f.firstAxis.x()*d.y()-f.firstAxis.y()*d.x())/determinant,(d.x()*f.secondAxis.y()-d.y()*f.secondAxis.x())/determinant);if(t<0)t+=kTwoPi;return t;};
        const auto spans=[&](const QPainterPath &path){Intervals result;for(int i=1;i<path.elementCount();++i){const auto e=path.elementAt(i),previous=path.elementAt(i-1);if(e.type!=QPainterPath::LineToElement)continue;const double a=angle({previous.x,previous.y}),b=angle({e.x,e.y}),delta=std::remainder(b-a,kTwoPi);double low=std::min(a,a+delta),high=std::max(a,a+delta);if(low<0){result.push_back({low+kTwoPi,kTwoPi});low=0;}if(high>kTwoPi){result.push_back({0,high-kTwoPi});high=kTwoPi;}result.push_back({low,high});}return unite(std::move(result));};
        const double radius=std::max(norm(f.firstAxis),norm(f.secondAxis))*view.scale,step=std::clamp(std::sqrt(.008/std::max(radius,1e-12)),.001,.08);
        const auto path=[&](const Intervals &spans){QPainterPath result;for(auto span:spans){const int count=std::max(1,int(std::ceil(span.length()/step)));for(int i=0;i<=count;++i){const double t=span.lo+span.length()*i/count;const auto p=f.center+std::cos(t)*f.firstAxis+std::sin(t)*f.secondAxis;const QPointF q(p.x()*view.scale,-p.y()*view.scale);if(i)result.lineTo(q);else result.moveTo(q);}}return result;};
        std::stable_sort(group.entries.begin(),group.entries.end(),[&](auto a,auto b){return paths[a].visible.length()>paths[b].visible.length();});Intervals covered;
        for(auto i:group.entries){auto intervals=spans(paths[i].visible);paths[i].visible=path(subtract(intervals,covered));covered.insert(covered.end(),intervals.begin(),intervals.end());covered=unite(std::move(covered));}
        for(auto i:group.entries){auto intervals=spans(paths[i].hidden);paths[i].hidden=path(subtract(intervals,covered));covered.insert(covered.end(),intervals.begin(),intervals.end());covered=unite(std::move(covered));}
    }
}
}
QString drawingViewVisibility(const DrawingModelSnapshot &model,const DrawingView &view,const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &silhouettes,DrawingSilhouetteCache &cache,DrawingViewVisibility &result,const QPainterPath *sectionMaterial,bool includeMesh){
    try{need(!model.version.documentId.isNull()&&std::isfinite(view.scale)&&view.scale>0&&std::isfinite(view.tolerance)&&view.tolerance>0,"Vista non valida per la visibilita'.");DrawingViewVisibility next;
        std::unique_ptr<Occlusion> index;if(drawingHasProjection(view))index=std::make_unique<Occlusion>(model,view,cache,next.warnings,sectionMaterial);
        TessellationOptions options;options.deflection=std::clamp(.03/view.scale,1e-6,1.0);options.angle=.12;
        for(const auto &curve:curves){cancellationPoint();need(curve.viewId==view.id&&curve.curve,"Curva estranea alla vista.");CurvePtr<3> geometry=curve.curve;auto range=curve.range;DisplaySource origin;
            if(curve.kind==DrawingCurveKind::ProjectedEdge){const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &body){return body.featureId==curve.featureId;});need(source!=model.bodies.end()&&source->body,"Sorgente del bordo assente.");
                const auto &edge=source->body->edge(curve.sourceEdge);origin.featureId=source->featureId;
                if(edge.forward.valid())origin.faces.push_back(source->body->finFace(edge.forward));
                if(edge.backward.valid())origin.faces.push_back(source->body->finFace(edge.backward));
                geometry=edge.curve;range=curve.hasSourceRange?curve.sourceRange:edge.range;}
            if(curve.kind==DrawingCurveKind::ApparentLine||curve.kind==DrawingCurveKind::ApparentArc){geometry=curve.sourceGeometry;range=curve.sourceRange;origin.featureId=curve.featureId;origin.faces={curve.sourceFace};
                if(curve.kind==DrawingCurveKind::ApparentArc){const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &b){return b.featureId==curve.featureId;});need(source!=model.bodies.end(),"Sorgente del raccordo assente.");origin.surface=source->body->face(curve.sourceFace).surface.get();
                    // Stessi margini locali dei contorni tangenti: anche la
                    // faccia adiacente al raccordo puo' essere radente.
                    for(auto loop:source->body->face(curve.sourceFace).loops)for(auto fin:source->body->loopFins(loop)){const auto &edge=source->body->edge(source->body->fin(fin).edge);for(auto side:{edge.forward,edge.backward})if(side.valid()){const auto face=source->body->finFace(side);if(std::find(origin.faces.begin(),origin.faces.end(),face)==origin.faces.end())origin.faces.push_back(face);}}
                }
            }
            const auto parameters=sampleCurve(*geometry,range,options);need(parameters.size()<=200000,"Troppi campioni per la visibilita'.");std::vector<Vec3> points;points.reserve(parameters.size());for(auto parameter:parameters)points.push_back(geometry->point(parameter));
            next.curves.push_back(display(points,view,curve.kind!=DrawingCurveKind::Section?index.get():nullptr,origin));}
        if(drawingHasProjection(view)){consolidateLinearPaths(view,curves,next.curves);consolidateConicPaths(view,curves,next.curves);}
        std::vector<bool> eligible(curves.size(),true);next.allCenters=drawingCenterGeometry(view,curves,eligible);
        for(std::size_t i=0;i<curves.size();++i)eligible[i]=!next.curves[i].visible.isEmpty();
        next.visibleCenters=drawingCenterGeometry(view,curves,eligible);
        for(const auto &contour:silhouettes.paths){
            if(std::any_of(curves.begin(),curves.end(),[&](const auto &c){return c.kind==DrawingCurveKind::ApparentLine&&c.featureId==contour.featureId&&c.sourceFace==contour.sourceFace;})){next.silhouettes.emplace_back();continue;}
            DisplaySource origin{contour.featureId,{contour.sourceFace}};
            const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &b){return b.featureId==contour.featureId;});
            need(source!=model.bodies.end()&&source->body,"Sorgente del contorno assente.");
            origin.surface=source->body->face(contour.sourceFace).surface.get();
            // Anche il raccordo con una faccia adiacente puo' essere radente:
            // i margini normali restano locali alla faccia e ai suoi bordi.
            for(auto loop:source->body->face(contour.sourceFace).loops)for(auto fin:source->body->loopFins(loop)){
                const auto &edge=source->body->edge(source->body->fin(fin).edge);
                for(auto side:{edge.forward,edge.backward})if(side.valid()){
                    const auto face=source->body->finFace(side);if(std::find(origin.faces.begin(),origin.faces.end(),face)==origin.faces.end())origin.faces.push_back(face);
                }
            }
            // Elimina solo i segmenti gia' rappresentati dagli archi esatti.
            // Gli altri rami del contorno toroidale (ad es. anelli di tangenza
            // di un toro completo) conservano la propria visibilita'.
            std::vector<const DrawingCurve *> exact;
            for(const auto &c:curves)if(c.kind==DrawingCurveKind::ApparentArc&&c.featureId==contour.featureId&&c.sourceFace==contour.sourceFace)exact.push_back(&c);
            const auto coincident=[&](Vec3 a,Vec3 b){return std::any_of(exact.begin(),exact.end(),[&](const auto *c){return projectPoint(*c->sourceGeometry,a,c->sourceRange).distance<=8*view.tolerance&&projectPoint(*c->sourceGeometry,b,c->sourceRange).distance<=8*view.tolerance;});};
            next.silhouettes.push_back(display(contour.points,view,index.get(),origin,exact.empty()?std::function<bool(Vec3,Vec3)>{}:coincident));
        }
        std::set<std::pair<quint64,std::uint32_t>> visibleFaces;
        for(std::size_t i=0;i<curves.size();++i)if(!next.curves[i].visible.isEmpty()){
            const auto &curve=curves[i];if(curve.kind!=DrawingCurveKind::ProjectedEdge)visibleFaces.emplace(curve.featureId,curve.sourceFace.index);
            else{const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &s){return s.featureId==curve.featureId;});if(source==model.bodies.end())continue;
                const auto &edge=source->body->edge(curve.sourceEdge);for(auto fin:{edge.forward,edge.backward})if(fin.valid())visibleFaces.emplace(curve.featureId,source->body->finFace(fin).index);}
        }
        for(std::size_t i=0;i<silhouettes.paths.size();++i)if(!next.silhouettes[i].visible.isEmpty())visibleFaces.emplace(silhouettes.paths[i].featureId,silhouettes.paths[i].sourceFace.index);
        next.allCylinderAxes=drawingCylinderAxes(model,view,[](quint64,FaceId){return true;});
        next.visibleCylinderAxes=drawingCylinderAxes(model,view,[&](quint64 feature,FaceId face){return visibleFaces.count({feature,face.index});});
        if(includeMesh&&drawingHasProjection(view)){renderShadedMesh(model,view,cache,next.shadedImage,next.shadedBounds);next.meshPrepared=true;}
        result=std::move(next);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
