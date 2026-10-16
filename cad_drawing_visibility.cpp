#include "cad_drawing_visibility.h"
#include "fk_classify.h"
#include <QSet>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <numeric>
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
class Occlusion {
    std::vector<Triangle> triangles_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
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
    Occlusion(const DrawingModelSnapshot &model,const DrawingView &view,DrawingSilhouetteCache &cache,QStringList &warnings){QSet<quint64> ids;
        for(const auto &source:model.bodies){cancellationPoint();need(source.featureId&&!ids.contains(source.featureId)&&source.body&&source.names&&source.names->body==source.body.get(),"Sorgente della visibilita' incoerente.");ids.insert(source.featureId);
            const auto mesh=cache.seeds(source);if(mesh->failedFaces)warnings.append(QStringLiteral("Visibilita' incompleta: %1 facce non tassellate nella sorgente %2.").arg(mesh->failedFaces).arg(source.featureId));
            Box bodyBox;for(auto face:source.body->faces())bodyBox.add(faceBox(*source.body,face));const double deflection=std::clamp(bodyBox.diagonal()*.001,1e-6,1.0);
            for(const auto &face:mesh->faces){cancellationPoint();const bool planar=source.body->face(face.face).surface->type()==SurfaceType::Plane;
                for(const auto &indices:face.triangles){Triangle triangle;triangle.a=view.plane.toLocal(face.points.at(indices[0]));triangle.b=view.plane.toLocal(face.points.at(indices[1]));triangle.c=view.plane.toLocal(face.points.at(indices[2]));
                    need(isFinite(triangle.a)&&isFinite(triangle.b)&&isFinite(triangle.c),"Triangolo non finito nella visibilita'.");triangle.area=cross2(triangle.b-triangle.a,triangle.c-triangle.a);
                    const double side=std::max({norm(triangle.b-triangle.a),norm(triangle.c-triangle.a),norm(triangle.c-triangle.b)});if(std::abs(triangle.area)<=1e-12*side*side)continue;
                    triangle.bias=std::max(4*view.tolerance,planar?0.0:2*deflection);triangle.bounds.add(triangle.a);triangle.bounds.add(triangle.b);triangle.bounds.add(triangle.c);triangles_.push_back(triangle);
                    need(triangles_.size()<=2000000,"Troppi triangoli per la visibilita' della tavola.");}}
        }
        order_.resize(triangles_.size());std::iota(order_.begin(),order_.end(),0);if(!order_.empty())build(0,order_.size());
    }
    std::vector<Interval> hidden(Vec3 a,Vec3 b)const{std::vector<Interval> hits;if(nodes_.empty())return hits;Bounds box;box.add(a);box.add(b);query(0,box,a,b,hits);
        std::sort(hits.begin(),hits.end(),[](const auto &a,const auto &b){return a.lo<b.lo;});std::vector<Interval> merged;for(const auto &hit:hits){if(!merged.empty()&&hit.lo<=merged.back().hi+1e-10)merged.back().hi=std::max(merged.back().hi,hit.hi);else merged.push_back(hit);}return merged;}
};
void append(QPainterPath &path,QPointF a,QPointF b){if(QLineF(a,b).length()<1e-10)return;if(path.isEmpty()||QLineF(path.currentPosition(),a).length()>1e-8)path.moveTo(a);path.lineTo(b);}
DrawingVisiblePath display(const std::vector<Vec3> &points,const DrawingView &view,const Occlusion *index){DrawingVisiblePath result;
    const auto paper=[&](Vec3 p){const QPointF point(p.x()*view.scale,-p.y()*view.scale);need(std::isfinite(point.x())&&std::isfinite(point.y()),"Tratto non finito sul foglio.");return point;};
    for(std::size_t i=1;i<points.size();++i){cancellationPoint();const auto a=view.plane.toLocal(points[i-1]),b=view.plane.toLocal(points[i]);need(isFinite(a)&&isFinite(b),"Curva non finita nella visibilita'.");
        double end=0;const auto hidden=index?index->hidden(a,b):std::vector<Interval>{};for(const auto &part:hidden){append(result.visible,paper(a+end*(b-a)),paper(a+part.lo*(b-a)));append(result.hidden,paper(a+part.lo*(b-a)),paper(a+part.hi*(b-a)));end=part.hi;}
        append(result.visible,paper(a+end*(b-a)),paper(b));}return result;}
}
QString drawingViewVisibility(const DrawingModelSnapshot &model,const DrawingView &view,const std::vector<DrawingCurve> &curves,const DrawingSilhouetteResult &silhouettes,DrawingSilhouetteCache &cache,DrawingViewVisibility &result){
    try{need(!model.version.documentId.isNull()&&std::isfinite(view.scale)&&view.scale>0&&std::isfinite(view.tolerance)&&view.tolerance>0,"Vista non valida per la visibilita'.");DrawingViewVisibility next;
        std::unique_ptr<Occlusion> index;if(view.kind==DrawingViewKind::ProjectedEdges)index=std::make_unique<Occlusion>(model,view,cache,next.warnings);
        TessellationOptions options;options.deflection=std::clamp(.03/view.scale,1e-6,1.0);options.angle=.12;
        for(const auto &curve:curves){cancellationPoint();need(curve.viewId==view.id&&curve.curve,"Curva estranea alla vista.");CurvePtr<3> geometry=curve.curve;auto range=curve.range;
            if(curve.kind==DrawingCurveKind::ProjectedEdge){const auto source=std::find_if(model.bodies.begin(),model.bodies.end(),[&](const auto &body){return body.featureId==curve.featureId;});need(source!=model.bodies.end()&&source->body,"Sorgente del bordo assente.");
                const auto &edge=source->body->edge(curve.sourceEdge);geometry=edge.curve;range=edge.range;}
            const auto parameters=sampleCurve(*geometry,range,options);need(parameters.size()<=200000,"Troppi campioni per la visibilita'.");std::vector<Vec3> points;points.reserve(parameters.size());for(auto parameter:parameters)points.push_back(geometry->point(parameter));
            next.curves.push_back(display(points,view,curve.kind==DrawingCurveKind::ProjectedEdge?index.get():nullptr));}
        for(const auto &contour:silhouettes.paths)next.silhouettes.push_back(display(contour.points,view,index.get()));
        result=std::move(next);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
