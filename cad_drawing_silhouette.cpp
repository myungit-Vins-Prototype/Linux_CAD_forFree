#include "cad_drawing_silhouette.h"
#include "fk_classify.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
namespace ForgeCad {
namespace {
using namespace Kernel;
void need(bool yes,const char *why){if(!yes)throw std::runtime_error(why);}
struct NormalJet {double value=0,length=0,du=0,dv=0;};
NormalJet normalJet(const Surface &s,Vec2 uv,Vec3 direction,bool derivatives=false){
    Vec3 jet[9];const int order=derivatives?2:1;s.evaluate(uv.x(),uv.y(),order,jet);
    const auto at=[&](int u,int v){return jet[Surface::derivativeIndex(u,v,order)];};
    const auto su=at(1,0),sv=at(0,1),normal=cross(su,sv);NormalJet out;out.value=dot(normal,direction);out.length=norm(normal);
    if(derivatives){out.du=dot(cross(at(2,0),sv)+cross(su,at(1,1)),direction);out.dv=dot(cross(at(1,1),sv)+cross(su,at(0,2)),direction);}return out;
}
double cosine(const Surface &s,Vec2 uv,Vec3 d){const auto jet=normalJet(s,uv,d);return jet.length>1e-25?jet.value/jet.length:0;}
Vec2 root(const Surface &s,Vec2 a,Vec2 b,double fa,double fb,Vec3 direction){
    if(std::abs(fa)<1e-10)return a;
    if(std::abs(fb)<1e-10)return b;
    for(int i=0;i<44;++i){const auto mid=.5*(a+b);const double value=cosine(s,mid,direction);if(std::abs(value)<1e-11)return mid;
        if((value<0)==(fa<0)){a=mid;fa=value;}else{b=mid;fb=value;}}
    return .5*(a+b);
}
bool correctMid(const Surface &s,Vec2 a,Vec2 b,Vec2 span,Vec2 lo,Vec2 hi,Vec3 direction,Vec2 &mid){
    // Ai poli sferici la longitudine e' arbitraria: il punto medio si ottiene
    // sul cerchio massimo tangente alla vista, senza Newton nelle coordinate UV.
    if(s.type()==SurfaceType::Sphere){
        const auto &sphere=static_cast<const SphericalSurface &>(s);const auto &frame=sphere.frame();
        const auto pa=s.point(a.x(),a.y()),pb=s.point(b.x(),b.y());const auto radialA=pa-frame.origin(),radialB=pb-frame.origin();
        const auto sum=frame.toLocal(pa)+frame.toLocal(pb);
        if(norm(sum)>sphere.radius()*1e-12&&std::abs(dot(radialA,direction))<=1e-8*sphere.radius()&&std::abs(dot(radialB,direction))<=1e-8*sphere.radius()){
            const auto n=normalized(sum);double u=std::atan2(n.y(),n.x());u+=kTwoPi*std::round((.5*(a.x()+b.x())-u)/kTwoPi);mid=Vec2(u,std::atan2(n.z(),std::hypot(n.x(),n.y())));
            if(mid.x()>=lo.x()-1e-9*span.x()&&mid.x()<=hi.x()+1e-9*span.x()&&mid.y()>=lo.y()-1e-9*span.y()&&mid.y()<=hi.y()+1e-9*span.y())return true;
        }
    }
    const Vec2 chord((b.x()-a.x())/span.x(),(b.y()-a.y())/span.y());const double length=norm(chord);if(length<1e-14)return false;
    const Vec2 transverse(-chord.y()/length,chord.x()/length);mid=.5*(a+b);
    for(int i=0;i<12;++i){const auto jet=normalJet(s,mid,direction,true);if(jet.length<=1e-25)return false;if(std::abs(jet.value)<=1e-10*jet.length)return true;
        const double derivative=jet.du*transverse.x()*span.x()+jet.dv*transverse.y()*span.y();if(std::abs(derivative)<=1e-20)return false;
        const double step=jet.value/derivative;if(std::abs(step)>2*length)return false;
        mid-=Vec2(step*transverse.x()*span.x(),step*transverse.y()*span.y());
        if(mid.x()<lo.x()-1e-9*span.x()||mid.x()>hi.x()+1e-9*span.x()||mid.y()<lo.y()-1e-9*span.y()||mid.y()>hi.y()+1e-9*span.y())return false;
    }
    return false;
}
double chordError(Vec3 a,Vec3 b,Vec3 point){const auto d=b-a;const double squared=squaredNorm(d);return distance(point,a+(squared>0?std::clamp(dot(point-a,d)/squared,0.0,1.0):0)*d);}
void faceContours(const DrawingSourceBody &source,const FaceMesh &mesh,const DrawingView &view,double deflection,std::vector<DrawingSilhouette> &out){
    const auto &face=source.body->face(mesh.face);if(face.surface->type()==SurfaceType::Plane)return;
    const auto &surface=*face.surface;need(mesh.parameters.size()==mesh.points.size(),"Parametri della superficie assenti per il contorno apparente.");if(mesh.points.empty())return;
    const auto direction=view.plane.zDir();std::vector<double> values;values.reserve(mesh.points.size());Vec2 lo=mesh.parameters[0],hi=lo;double maximum=0;
    for(std::size_t i=0;i<mesh.points.size();++i){const auto uv=mesh.parameters[i];const auto jet=normalJet(surface,uv,direction);
        const double value=jet.length>1e-25?jet.value/jet.length:i<mesh.normals.size()?dot(mesh.normals[i],direction)*(face.sense?1:-1):0;
        need(std::isfinite(value),"Normale non finita nel contorno apparente.");values.push_back(std::abs(value)<1e-10?0:value);maximum=std::max(maximum,std::abs(value));
        for(int k=0;k<2;++k){lo[k]=std::min(lo[k],uv[k]);hi[k]=std::max(hi[k],uv[k]);}}
    if(maximum<1e-9)return; // faccia interamente vista di taglio: sono gia' presenti i bordi
    const Vec2 span(std::max(hi.x()-lo.x(),1e-12),std::max(hi.y()-lo.y(),1e-12));
    std::map<std::pair<int,int>,int> roots;std::vector<Vec2> uvPoints;std::vector<std::pair<int,int>> segments;
    const auto crossing=[&](int a,int b){std::pair<int,int> key;if(values[a]==0)key={a,a};else if(values[b]==0)key={b,b};else key=std::minmax(a,b);
        const auto found=roots.find(key);if(found!=roots.end())return found->second;
        const auto uv=key.first==key.second?mesh.parameters[key.first]:root(surface,mesh.parameters[a],mesh.parameters[b],values[a],values[b],direction);
        const int index=int(uvPoints.size());uvPoints.push_back(uv);roots.emplace(key,index);return index;};
    for(const auto &triangle:mesh.triangles){cancellationPoint();if(values[triangle[0]]==0&&values[triangle[1]]==0&&values[triangle[2]]==0)continue;std::vector<int> hits;
        for(int k=0;k<3;++k){const int a=triangle[k],b=triangle[(k+1)%3];if(values[a]==0||values[b]==0||(values[a]<0)!=(values[b]<0)){const auto index=crossing(a,b);if(std::find(hits.begin(),hits.end(),index)==hits.end())hits.push_back(index);}}
        if(hits.size()==2&&hits[0]!=hits[1])segments.emplace_back(hits[0],hits[1]);}
    for(auto &segment:segments)if(segment.first>segment.second)std::swap(segment.first,segment.second);
    std::sort(segments.begin(),segments.end());segments.erase(std::unique(segments.begin(),segments.end()),segments.end());
    std::vector<std::vector<int>> adjacent(uvPoints.size());for(std::size_t i=0;i<segments.size();++i){adjacent[segments[i].first].push_back(int(i));adjacent[segments[i].second].push_back(int(i));}
    std::vector<bool> used(segments.size());
    const auto walk=[&](int start,int edge){DrawingSilhouette path;path.featureId=source.featureId;path.sourceFace=mesh.face;const auto initial=surface.point(uvPoints[start].x(),uvPoints[start].y());need(isFinite(initial),"Punto non finito nel contorno apparente.");path.points.push_back(initial);
        std::function<void(Vec2,Vec2,int)> append=[&](Vec2 a,Vec2 b,int depth){cancellationPoint();const auto pa=surface.point(a.x(),a.y()),pb=surface.point(b.x(),b.y());Vec2 mid;
            if(correctMid(surface,a,b,span,lo,hi,direction,mid)){const auto point=surface.point(mid.x(),mid.y());if(chordError(pa,pb,point)>deflection){if(depth<14){append(a,mid,depth+1);append(mid,b,depth+1);return;}path.refinementLimited=true;}}
            else path.refinementLimited=true;
            need(path.points.size()<200000,"Contorno apparente troppo complesso.");need(isFinite(pb),"Punto non finito del contorno apparente.");path.points.push_back(pb);};
        int current=start;
        while(edge>=0&&!used[edge]){used[edge]=true;const auto segment=segments[edge];const int next=segment.first==current?segment.second:segment.first;append(uvPoints[current],uvPoints[next],0);current=next;
            if(current==start||adjacent[current].size()!=2)break;
            edge=used[adjacent[current][0]]?adjacent[current][1]:adjacent[current][0];}
        double length=0;for(std::size_t i=1;i<path.points.size();++i)length+=distance(path.points[i-1],path.points[i]);
        if(path.points.size()>1&&length>view.tolerance)out.push_back(std::move(path));};
    for(std::size_t i=0;i<adjacent.size();++i)if(adjacent[i].size()!=2)for(int edge:adjacent[i])if(!used[edge])walk(int(i),edge);
    for(std::size_t i=0;i<segments.size();++i)if(!used[i])walk(segments[i].first,int(i));
}
}
std::shared_ptr<const Kernel::Tessellation> DrawingSilhouetteCache::seeds(const DrawingSourceBody &source,double maximumDeflection){
    Kernel::Box bounds;for(auto face:source.body->faces())bounds.add(Kernel::faceBox(*source.body,face));
    Kernel::TessellationOptions options;options.deflection=std::min(std::clamp(bounds.diagonal()*.001,1e-6,1.0),maximumDeflection);options.angle=.18;
    need(std::isfinite(options.deflection)&&options.deflection>0,"Precisione della mesh non valida.");
    const auto found=entries_.find(source.body.get());if(found!=entries_.end()&&found->second.deflection<=options.deflection)return found->second.mesh;
    auto mesh=std::make_shared<const Kernel::Tessellation>(Kernel::tessellate(*source.body,options));entries_[source.body.get()]={source.body,mesh,options.deflection};return mesh;
}
QString drawingViewSilhouettes(const DrawingModelSnapshot &model,const DrawingView &view,DrawingSilhouetteResult &contours,DrawingSilhouetteCache &cache){
    try{if(!drawingHasProjection(view)){contours={};return {};}
        need(!model.version.documentId.isNull()&&isFinite(view.plane.zDir())&&std::abs(norm(view.plane.zDir())-1)<1e-10,"Direzione o modello non valido per il contorno apparente.");
        need(drawingHasProjection(view)&&std::isfinite(view.scale)&&view.scale>0,"Vista non valida per i contorni apparenti.");QSet<quint64> ids;DrawingSilhouetteResult prepared;
        const double deflection=std::clamp(.02/view.scale,1e-6,1.0);
        for(const auto &source:model.bodies){cancellationPoint();need(source.featureId&&!ids.contains(source.featureId)&&source.body&&source.names&&source.names->body==source.body.get(),"Sorgente dei contorni apparenti incoerente.");ids.insert(source.featureId);
            const auto sourceFaces=source.body->faces();if(std::all_of(sourceFaces.begin(),sourceFaces.end(),[&](auto face){return source.body->face(face).surface->type()==SurfaceType::Plane;}))continue;
            const auto mesh=cache.seeds(source);if(mesh->failedFaces)prepared.warnings.append(QStringLiteral("%1 facce senza contorno apparente nella sorgente %2.").arg(mesh->failedFaces).arg(source.featureId));
            for(const auto &face:mesh->faces){cancellationPoint();faceContours(source,face,view,deflection,prepared.paths);}}
        const auto limited=std::count_if(prepared.paths.begin(),prepared.paths.end(),[](const auto &path){return path.refinementLimited;});
        if(limited)prepared.warnings.append(QStringLiteral("Precisione grafica non verificata su %1 contorni apparenti; bordi e sezioni restano quotabili.").arg(limited));
        contours=std::move(prepared);return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
}
