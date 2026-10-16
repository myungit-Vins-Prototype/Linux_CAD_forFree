#include "cad_drawing_visibility.h"
#include "fk_classify.h"
#include <QSet>
#include <QLineF>
#include <QTransform>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
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
struct Triangle {Vec3 a,b,c;Bounds bounds;double area=0,bias=0;};
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
    static bool covered(const Triangle &t,Vec3 a,Vec3 b,double &lo,double &hi){lo=0;hi=1;const double sign=t.area>0?1:-1;
        const auto clip=[&](double fa,double fb){if(fa<0&&fb<0)return false;if(fa>=0&&fb>=0)return true;const double q=fa/(fa-fb);if(fa<0)lo=std::max(lo,q);else hi=std::min(hi,q);return hi>lo;};
        const Vec3 vertices[]={t.a,t.b,t.c};for(int i=0;i<3;++i){const auto p=vertices[i],edge=vertices[(i+1)%3]-p;if(!clip(sign*cross2(edge,a-p),sign*cross2(edge,b-p)))return false;}
        const auto depth=[&](Vec3 p){const double u=cross2(t.b-p,t.c-p)/t.area,v=cross2(t.c-p,t.a-p)/t.area;return u*t.a.z()+v*t.b.z()+(1-u-v)*t.c.z()-p.z()-t.bias;};
        return clip(depth(a),depth(b))&&hi-lo>1e-12;
    }
    void query(int id,const Bounds &box,Vec3 a,Vec3 b,std::vector<Interval> &hits)const{const auto &node=nodes_[id];if(!node.bounds.overlaps(box))return;if(node.left>=0){query(node.left,box,a,b,hits);query(node.right,box,a,b,hits);return;}
        for(auto i=node.start;i<node.end;++i){const auto &triangle=triangles_[order_[i]];if(!triangle.bounds.overlaps(box))continue;double lo,hi;if(covered(triangle,a,b,lo,hi))hits.push_back({lo,hi});}}
public:
    Occlusion(const DrawingModelSnapshot &model,const DrawingView &view,DrawingSilhouetteCache &cache,QStringList &warnings,const QPainterPath *material){QSet<quint64> ids;
        if(view.sectionStyle!=DrawingSectionStyle::Removed){need(material,"Materiale della sezione non preparato per la visibilita'.");capBias_=4*view.tolerance;cap_=QTransform::fromScale(1/view.scale,-1/view.scale).map(*material);capLoops_=cap_.toSubpathPolygons();}
        for(const auto &source:model.bodies){cancellationPoint();need(source.featureId&&!ids.contains(source.featureId)&&source.body&&source.names&&source.names->body==source.body.get(),"Sorgente della visibilita' incoerente.");ids.insert(source.featureId);
            const auto mesh=cache.seeds(source);if(mesh->failedFaces)warnings.append(QStringLiteral("Visibilita' incompleta: %1 facce non tassellate nella sorgente %2.").arg(mesh->failedFaces).arg(source.featureId));
            Box bodyBox;for(auto face:source.body->faces())bodyBox.add(faceBox(*source.body,face));const double deflection=std::clamp(bodyBox.diagonal()*.001,1e-6,1.0);
            for(const auto &face:mesh->faces){cancellationPoint();const bool planar=source.body->face(face.face).surface->type()==SurfaceType::Plane;
                for(const auto &indices:face.triangles){const auto a=view.plane.toLocal(face.points.at(indices[0])),b=view.plane.toLocal(face.points.at(indices[1])),c=view.plane.toLocal(face.points.at(indices[2]));
                    need(isFinite(a)&&isFinite(b)&&isFinite(c),"Triangolo non finito nella visibilita'.");
                    for(const auto &polygon:retainedTriangles(a,b,c,view))for(std::size_t i=2;i<polygon.size();++i){Triangle triangle;triangle.a=polygon[0];triangle.b=polygon[i-1];triangle.c=polygon[i];triangle.area=cross2(triangle.b-triangle.a,triangle.c-triangle.a);
                        const double side=std::max({norm(triangle.b-triangle.a),norm(triangle.c-triangle.a),norm(triangle.c-triangle.b)});if(std::abs(triangle.area)<=1e-12*side*side)continue;
                        triangle.bias=std::max(4*view.tolerance,planar?0.0:2*deflection);triangle.bounds.add(triangle.a);triangle.bounds.add(triangle.b);triangle.bounds.add(triangle.c);triangles_.push_back(triangle);
                        need(triangles_.size()<=2000000,"Troppi triangoli per la visibilita' della tavola.");}}
            }
        }
        order_.resize(triangles_.size());std::iota(order_.begin(),order_.end(),0);if(!order_.empty())build(0,order_.size());
    }
    std::vector<Interval> hidden(Vec3 a,Vec3 b)const{std::vector<Interval> hits=capHidden(a,b);Bounds box;box.add(a);box.add(b);if(!nodes_.empty())query(0,box,a,b,hits);
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
DrawingVisiblePath display(const std::vector<Vec3> &points,const DrawingView &view,const Occlusion *index){DrawingVisiblePath result;
    const auto paper=[&](Vec3 p){const QPointF point(p.x()*view.scale,-p.y()*view.scale);need(std::isfinite(point.x())&&std::isfinite(point.y()),"Tratto non finito sul foglio.");return point;};
    for(std::size_t i=1;i<points.size();++i){cancellationPoint();const auto a=view.plane.toLocal(points[i-1]),b=view.plane.toLocal(points[i]);need(isFinite(a)&&isFinite(b),"Curva non finita nella visibilita'.");
        std::vector<double> cuts{0,1};if(index&&view.sectionStyle!=DrawingSectionStyle::Removed){for(int axis:{0,2})if(b[axis]!=a[axis]){const double t=-a[axis]/(b[axis]-a[axis]);if(t>0&&t<1)cuts.push_back(t);}std::sort(cuts.begin(),cuts.end());}
        for(std::size_t k=1;k<cuts.size();++k){if(index&&!drawingRetainsPoint(view,a+.5*(cuts[k-1]+cuts[k])*(b-a)))continue;
            const auto first=a+cuts[k-1]*(b-a),last=a+cuts[k]*(b-a);double end=0;const auto hidden=index?index->hidden(first,last):std::vector<Interval>{};
            for(const auto &part:hidden){append(result.visible,paper(first+end*(last-first)),paper(first+part.lo*(last-first)));append(result.hidden,paper(first+part.lo*(last-first)),paper(first+part.hi*(last-first)));end=part.hi;}
            append(result.visible,paper(first+end*(last-first)),paper(last));}}
    return result;}
}
QString drawingViewVisibility(const DrawingModelSnapshot &model,const DrawingView &view,const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &silhouettes,DrawingSilhouetteCache &cache,DrawingViewVisibility &result,const QPainterPath *sectionMaterial,bool includeMesh){
    try{need(!model.version.documentId.isNull()&&std::isfinite(view.scale)&&view.scale>0&&std::isfinite(view.tolerance)&&view.tolerance>0,"Vista non valida per la visibilita'.");DrawingViewVisibility next;
        std::unique_ptr<Occlusion> index;if(drawingHasProjection(view))index=std::make_unique<Occlusion>(model,view,cache,next.warnings,sectionMaterial);
        TessellationOptions options;options.deflection=std::clamp(.03/view.scale,1e-6,1.0);options.angle=.12;
        for(const auto &curve:curves){cancellationPoint();need(curve.viewId==view.id&&curve.curve,"Curva estranea alla vista.");CurvePtr<3> geometry=curve.curve;auto range=curve.range;
            if(curve.kind==DrawingCurveKind::ProjectedEdge){const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &body){return body.featureId==curve.featureId;});need(source!=model.bodies.end()&&source->body,"Sorgente del bordo assente.");
                const auto &edge=source->body->edge(curve.sourceEdge);geometry=edge.curve;range=curve.hasSourceRange?curve.sourceRange:edge.range;}
            const auto parameters=sampleCurve(*geometry,range,options);need(parameters.size()<=200000,"Troppi campioni per la visibilita'.");std::vector<Vec3> points;points.reserve(parameters.size());for(auto parameter:parameters)points.push_back(geometry->point(parameter));
            next.curves.push_back(display(points,view,curve.kind==DrawingCurveKind::ProjectedEdge?index.get():nullptr));}
        for(const auto &contour:silhouettes.paths)next.silhouettes.push_back(display(contour.points,view,index.get()));
        if(includeMesh&&drawingHasProjection(view)){renderShadedMesh(model,view,cache,next.shadedImage,next.shadedBounds);next.meshPrepared=true;}
        result=std::move(next);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
