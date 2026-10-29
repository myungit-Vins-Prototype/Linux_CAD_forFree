#include "cad_drawing_dimension.h"
#include "fk_curve_algo.h"
#include "fk_bspline_surface.h"
#include "fk_surface_algo.h"
#include "cad_drawing_curve_recognition.h"
#include "fk_classify.h"
#include "fk_curve_ops.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ForgeCad {
namespace {
using namespace Kernel;
void need(bool ok,const char *why){if(!ok)throw std::invalid_argument(why);}
struct ReferenceError:std::runtime_error{DrawingDimensionStatus status;QString message;ReferenceError(DrawingDimensionStatus s,QString m):std::runtime_error(m.toStdString()),status(s),message(std::move(m)) {}};
void missing(const QString &why){throw ReferenceError{DrawingDimensionStatus::Missing,why};}
Vec2 local(const DrawingView &v,const Vec3 &p){const auto q=v.plane.toLocal(p);return {q.x(),q.y()};}
double cross2(Vec2 a,Vec2 b){return a.x()*b.y()-a.y()*b.x();}
Vec2 conicPoint(const DrawingPrimitive &primitive,double fraction){
    need(primitive.kind==DrawingPrimitive::Kind::Conic,"L'aggancio angolare richiede una conica nel piano.");
    const Vec2 direction(std::cos(fraction*kTwoPi),std::sin(fraction*kTwoPi));
    const double determinant=cross2(primitive.firstAxis,primitive.secondAxis);
    need(std::abs(determinant)>1e-14*norm(primitive.firstAxis)*norm(primitive.secondAxis),"Conica proiettata degenere.");
    const Vec2 coefficients(cross2(direction,primitive.secondAxis)/determinant,cross2(primitive.firstAxis,direction)/determinant);
    return primitive.center+direction/norm(coefficients);
}
bool same(const DrawingSectionReference &a,const DrawingSectionReference &b){return a.featureId==b.featureId&&a.faceName==b.faceName&&a.edgeName==b.edgeName&&a.kind==b.kind&&a.contourSide==b.contourSide&&a.requiresWitness==b.requiresWitness&&distance(a.witness,b.witness)==0;}
const DrawingSourceBody &sourceFor(const DrawingModelSnapshot &model,quint64 id){
    const DrawingSourceBody *found=nullptr;for(const auto &s:model.bodies)if(s.featureId==id){need(!found,"Identita' della feature duplicata.");found=&s;}
    if(!found)missing(QStringLiteral("Feature sorgente assente."));
    need(found->body&&found->names&&found->names->body==found->body.get(),"Nomi topologici incoerenti con il corpo.");return *found;
}
struct FaceBounds{Vec2 lo,hi;bool valid=false;};
FaceBounds uvBounds(const CurvePtr<2> &curve,const Interval &range){const auto box=curveBox(*embedCurve(curve,Frame3()),range);return {{box.lo.x(),box.lo.y()},{box.hi.x(),box.hi.y()},true};}
FaceBounds faceBounds(const DrawingSourceBody &s,FaceId face){
    const auto &f=s.body->face(face);if(f.surface->isUPeriodic()||f.surface->isVPeriodic())return {};
    FaceBounds out;bool first=true;for(auto loop:f.loops)for(auto fin:s.body->loopFins(loop)){
        const auto &f=s.body->fin(fin);if(!f.pcurve)return {};const auto box=uvBounds(f.pcurve,s.body->edge(f.edge).range);
        if(first){out.lo=box.lo;out.hi=box.hi;first=false;}else for(int axis=0;axis<2;++axis){out.lo[axis]=std::min(out.lo[axis],box.lo[axis]);out.hi[axis]=std::max(out.hi[axis],box.hi[axis]);}}
    out.valid=!first&&norm(out.hi-out.lo)>1e-12;return out;
}
int pointSide(const FaceBounds &box,Vec2 uv){if(!box.valid)return 0;const double tolerance=1e-9*std::max(1.0,norm(box.hi-box.lo));int side=0;
    if(std::abs(uv.x()-box.lo.x())<=tolerance)side|=1;
    if(std::abs(uv.x()-box.hi.x())<=tolerance)side|=2;
    if(std::abs(uv.y()-box.lo.y())<=tolerance)side|=4;
    if(std::abs(uv.y()-box.hi.y())<=tolerance)side|=8;
    return side;
}
int edgeSide(const DrawingSourceBody &s,FaceId face,EdgeId edge){const auto box=faceBounds(s,face);if(!box.valid)return 0;
    for(auto loop:s.body->face(face).loops)for(auto fin:s.body->loopFins(loop))if(s.body->fin(fin).edge==edge){const auto &f=s.body->fin(fin);const auto limits=uvBounds(f.pcurve,s.body->edge(edge).range);
        return pointSide(box,limits.lo)&pointSide(box,limits.hi);}
    return 0;
}
int vertexSide(const DrawingSourceBody &s,FaceId face,VertexId vertex){const auto box=faceBounds(s,face);if(!box.valid)return 0;
    for(auto loop:s.body->face(face).loops)for(auto fin:s.body->loopFins(loop)){
        const auto &f=s.body->fin(fin);if(vertex!=s.body->edgeStart(f.edge)&&vertex!=s.body->edgeEnd(f.edge))continue;
        const auto &edge=s.body->edge(f.edge);return pointSide(box,f.pcurve->point(vertex==s.body->edgeStart(f.edge)?edge.range.lo:edge.range.hi));}
    return 0;
}
std::vector<FaceId> supportFaces(const DrawingSourceBody &s,const QString &name){std::vector<FaceId> faces;const auto names=s.names->find(EntityKind::Face,name.toStdString());for(auto face:s.body->faces())if(std::find(names.begin(),names.end(),face.index)!=names.end())faces.push_back(face);return faces;}
bool onSupport(const DrawingSourceBody &s,const DrawingCurve &curve,const DrawingAttachment &ref,int side,bool vertex=false,VertexId point={}){
    const auto faces=supportFaces(s,ref.supportFace);if(faces.size()!=1)return false;
    if(curve.kind==DrawingCurveKind::Section&&curve.sourceFace!=faces[0])return false;
    return vertex?vertexSide(s,faces[0],point)==side:edgeSide(s,faces[0],curve.sourceEdge)==side;
}
void setCurveSupport(const DrawingSourceBody &s,const DrawingCurve &curve,DrawingAttachment &ref){
    if(curve.kind!=DrawingCurveKind::ProjectedEdge)return;
    const auto &e=s.body->edge(curve.sourceEdge);for(auto fin:{e.forward,e.backward})if(fin.valid()){
        const auto face=s.body->finFace(fin);const auto name=QString::fromStdString(s.names->primary(EntityKind::Face,face.index));const auto side=edgeSide(s,face,curve.sourceEdge);
        if(side&&supportFaces(s,name).size()==1){ref.supportFace=name;ref.curveSide=side;return;}}
}
const DrawingCurve &curveFor(const DrawingView &view,const DrawingSourceBody &source,const std::vector<DrawingCurve> &curves,const DrawingAttachment &attachment){
    const auto &ref=attachment.curve;const bool edge=ref.kind==DrawingCurveKind::ProjectedEdge;
    const auto entities=source.names->find(edge?EntityKind::Edge:EntityKind::Face,(edge?ref.edgeName:ref.faceName).toStdString());std::vector<const DrawingCurve *> candidates;
    for(const auto &c:curves)if(c.viewId==view.id&&c.featureId==ref.featureId&&c.kind==ref.kind&&std::find(entities.begin(),entities.end(),edge?c.sourceEdge.index:c.sourceFace.index)!=entities.end()){
        if(attachment.curveSide&&!onSupport(source,c,attachment,attachment.curveSide))continue;
        candidates.push_back(&c);}
    if(ref.kind==DrawingCurveKind::ApparentArc){
        const DrawingCurve *chosen=nullptr;
        for(const auto *c:candidates){if(c->contourSide!=ref.contourSide)continue;
            const auto primitive=drawingPrimitive(view,*c);if(!primitive.circular)continue;
            if(attachment.polar){const auto point=conicPoint(primitive,attachment.fraction);if(projectPoint(*c->curve,view.plane.toGlobal(Vec3(point.x(),point.y(),0)),c->range).distance>view.tolerance)continue;}
            if(chosen)throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Piu' archi del raccordo appartengono allo stesso lato e aggancio.")};
            chosen=c;
        }
        if(!chosen)missing(QStringLiteral("L'arco associato del raccordo non e' piu' disponibile."));
        return *chosen;
    }
    if(ref.kind==DrawingCurveKind::ApparentLine){
        // Il lato della generatrice rispetto all'asse resta associato anche
        // quando cambia il raggio. Nessuna scelta del ramo piu' vicino.
        const DrawingCurve *chosen=nullptr;
        for(const auto *c:candidates){const auto &face=source.body->face(c->sourceFace);if(face.surface->type()!=SurfaceType::Cylinder&&face.surface->type()!=SurfaceType::Cone)continue;
            const auto frame=face.surface->type()==SurfaceType::Cone?static_cast<const ConicalSurface &>(*face.surface).frame():static_cast<const CylindricalSurface &>(*face.surface).frame();
            const auto center=local(view,frame.origin());const auto axis3=view.plane.directionToLocal(frame.zDir());const Vec2 axis(axis3.x(),axis3.y());
            const double wanted=cross2(axis,ref.witness-center),side=cross2(axis,local(view,c->curve->point(.5*(c->range.lo+c->range.hi)))-center);
            if(wanted*side<=0)continue;
            // Facce rifilate con piu' intervalli sullo stesso lato richiedono
            // anche l'appartenenza assiale del testimone.
            const double position=dot(ref.witness-center,axis),a=dot(local(view,c->curve->point(c->range.lo))-center,axis),b=dot(local(view,c->curve->point(c->range.hi))-center,axis);
            if(position<std::min(a,b)-view.tolerance||position>std::max(a,b)+view.tolerance)continue;
            if(chosen)throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Contorno cilindrico con piu' rami compatibili.")};
            chosen=c;
        }
        if(!chosen)missing(QStringLiteral("Il lato del contorno cilindrico non e' piu' disponibile."));
        return *chosen;
    }
    if(candidates.empty())missing(QStringLiteral("Entita' o ramo geometrico non ritrovato."));
    if(candidates.size()==1&&(!ref.requiresWitness||(attachment.curveSide&&view.sectionStyle==DrawingSectionStyle::Removed)))return *candidates[0];
    const auto witness=view.plane.toGlobal(Vec3(ref.witness.x(),ref.witness.y(),0));const DrawingCurve *chosen=nullptr;
    for(const auto *c:candidates)if(projectPoint(*c->curve,witness,c->range).distance<=view.tolerance){if(chosen)throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Piu' rami contengono l'ancoraggio della quota.")};chosen=c;}
    if(!chosen)throw ReferenceError{candidates.size()>1?DrawingDimensionStatus::Ambiguous:DrawingDimensionStatus::Missing,QStringLiteral("L'ancoraggio non identifica il ramo originario.")};
    return *chosen;
}
// L'ancoraggio segue un vertice o il bordo che termina la sezione, mai l'indice/verso del parametro.
bool anchorAt(const DrawingView &view,const DrawingSourceBody &source,const DrawingCurve &curve,const DrawingAttachment &ref,const Vec3 &point){
    if(curve.kind==DrawingCurveKind::ApparentLine&&curve.sourceGeometry&&curve.hasSourceRange){
        const bool first=distance(point,curve.curve->point(curve.range.lo))<=view.tolerance;
        auto original=curve;original.curve=curve.sourceGeometry;original.range=curve.sourceRange;original.sourceGeometry.reset();
        return anchorAt(view,source,original,ref,original.curve->point(first?original.range.lo:original.range.hi));
    }
    if(view.step||view.bend){
        if(ref.clipBoundary==5)return view.bend&&std::abs(view.plane.toLocal(point).x()-view.bend->at)<=view.tolerance;
        if(ref.clipBoundary==4&&view.step){for(const auto &step:view.step->transitions())if(std::abs(view.plane.toLocal(point).x()-step.at)<=view.tolerance)return true;return false;}
        if(!curve.sourceGeometry)return false;
        const auto shift=curve.sourceGeometry->point(curve.range.lo)-curve.curve->point(curve.range.lo);
        auto original=curve;original.curve=curve.sourceGeometry;auto plain=view;plain.step.reset();plain.bend.reset();
        return anchorAt(plain,source,original,ref,curve.unfoldedToSource?curve.unfoldedToSource->applyToPoint(point):point+shift);
    }
    if(ref.clipBoundary){
        if(view.kind!=DrawingViewKind::Section||view.sectionStyle==DrawingSectionStyle::Removed)return false;
        if((ref.clipBoundary&2)&&!drawingHalfSection(view))return false;
        auto p=point;
        if(curve.kind==DrawingCurveKind::ProjectedEdge){if(!curve.hasSourceRange||!curve.sourceGeometry)return false;
            const bool first=distance(point,curve.curve->point(curve.range.lo))<=view.tolerance;p=curve.sourceGeometry->point(first?curve.sourceRange.lo:curve.sourceRange.hi);}
        const auto q=view.plane.toLocal(p);
        return (!(ref.clipBoundary&1)||std::abs(q.z())<=view.tolerance)&&(!(ref.clipBoundary&2)||std::abs(q.x())<=view.tolerance);
    }
    if(!ref.vertexName.isEmpty()){
        const auto vertices=source.names->find(EntityKind::Vertex,ref.vertexName.toStdString());
        for(auto vertex:source.body->vertices())if(std::find(vertices.begin(),vertices.end(),vertex.index)!=vertices.end()){
            if(curve.kind==DrawingCurveKind::ProjectedEdge&&vertex!=source.body->edgeStart(curve.sourceEdge)&&vertex!=source.body->edgeEnd(curve.sourceEdge))continue;
            const auto expected=curve.kind==DrawingCurveKind::ProjectedEdge?view.plane.toGlobal(Vec3(local(view,source.body->vertex(vertex).point).x(),local(view,source.body->vertex(vertex).point).y(),0)):source.body->vertex(vertex).point;
            if(ref.anchorSide&&!onSupport(source,curve,ref,ref.anchorSide,true,vertex))continue;
            if(distance(expected,point)<=view.tolerance)return true;
        }
    }else if(!ref.boundaryName.isEmpty()){
        const auto edges=source.names->find(EntityKind::Edge,ref.boundaryName.toStdString());
        for(auto loop:source.body->face(curve.sourceFace).loops)for(auto fin:source.body->loopFins(loop)){
            const auto edge=source.body->fin(fin).edge;if(std::find(edges.begin(),edges.end(),edge.index)==edges.end())continue;
            if(ref.anchorSide){const auto faces=supportFaces(source,ref.supportFace);if(faces.size()!=1||faces[0]!=curve.sourceFace||edgeSide(source,faces[0],edge)!=ref.anchorSide)continue;}
            const auto &e=source.body->edge(edge);if(projectPoint(*e.curve,point,e.range).distance<=view.tolerance)return true;
        }
    }
    return false;
}
void setAnchor(const DrawingView &view,const DrawingSourceBody &source,const DrawingCurve &curve,const Vec3 &point,DrawingAttachment &ref){
    if((view.step||view.bend)&&curve.sourceGeometry){
        const auto shift=curve.sourceGeometry->point(curve.range.lo)-curve.curve->point(curve.range.lo);
        auto original=curve;original.curve=curve.sourceGeometry;auto plain=view;plain.step.reset();plain.bend.reset();
        setAnchor(plain,source,original,curve.unfoldedToSource?curve.unfoldedToSource->applyToPoint(point):point+shift,ref);
        if(ref.vertexName.isEmpty()&&ref.boundaryName.isEmpty()&&view.bend&&std::abs(view.plane.toLocal(point).x()-view.bend->at)<=view.tolerance)ref.clipBoundary=5;
        if(ref.vertexName.isEmpty()&&ref.boundaryName.isEmpty()&&view.step)for(const auto &step:view.step->transitions())if(std::abs(view.plane.toLocal(point).x()-step.at)<=view.tolerance)ref.clipBoundary=4;
        return;
    }
    for(auto vertex:source.body->vertices()){
        if(curve.kind==DrawingCurveKind::ProjectedEdge&&vertex!=source.body->edgeStart(curve.sourceEdge)&&vertex!=source.body->edgeEnd(curve.sourceEdge))continue;
        const auto q=local(view,source.body->vertex(vertex).point);const auto expected=curve.kind==DrawingCurveKind::ProjectedEdge?view.plane.toGlobal(Vec3(q.x(),q.y(),0)):source.body->vertex(vertex).point;
        if((curve.kind==DrawingCurveKind::Section||curve.kind==DrawingCurveKind::ApparentLine)){bool incident=false;for(auto loop:source.body->face(curve.sourceFace).loops)for(auto fin:source.body->loopFins(loop))if(source.body->finStart(fin)==vertex||source.body->finEnd(fin)==vertex)incident=true;if(!incident)continue;}
        if(distance(expected,point)<=view.tolerance){const auto name=QString::fromStdString(source.names->primary(EntityKind::Vertex,vertex.index));if(!ref.vertexName.isEmpty()&&ref.vertexName!=name)throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Vertici sovrapposti nell'ancoraggio.")};ref.vertexName=name;
            if(curve.kind==DrawingCurveKind::Section){ref.supportFace=QString::fromStdString(source.names->primary(EntityKind::Face,curve.sourceFace.index));ref.anchorSide=vertexSide(source,curve.sourceFace,vertex);}
            else if(!ref.supportFace.isEmpty()){const auto faces=supportFaces(source,ref.supportFace);if(faces.size()==1)ref.anchorSide=vertexSide(source,faces[0],vertex);}
        }
    }
    if(!ref.curveSide&&!ref.anchorSide)ref.supportFace.clear();
    if(ref.vertexName.isEmpty()&&(curve.kind==DrawingCurveKind::Section||curve.kind==DrawingCurveKind::ApparentLine)){
    for(auto loop:source.body->face(curve.sourceFace).loops)for(auto fin:source.body->loopFins(loop)){
        const auto edge=source.body->fin(fin).edge;const auto &e=source.body->edge(edge);
        if(projectPoint(*e.curve,point,e.range).distance<=view.tolerance){const auto name=QString::fromStdString(source.names->primary(EntityKind::Edge,edge.index));
            if(!ref.boundaryName.isEmpty()&&ref.boundaryName!=name)throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Piu' bordi terminano la sezione nello stesso punto.")};
            ref.boundaryName=name;const auto side=edgeSide(source,curve.sourceFace,edge);
            if(side){ref.supportFace=QString::fromStdString(source.names->primary(EntityKind::Face,curve.sourceFace.index));ref.anchorSide=side;}
        }
    }
    }
    if(ref.vertexName.isEmpty()&&ref.boundaryName.isEmpty()&&view.sectionStyle!=DrawingSectionStyle::Removed){
        auto p=point;if(curve.kind==DrawingCurveKind::ProjectedEdge&&curve.hasSourceRange&&curve.sourceGeometry){const bool first=distance(point,curve.curve->point(curve.range.lo))<=view.tolerance;p=curve.sourceGeometry->point(first?curve.sourceRange.lo:curve.sourceRange.hi);}
        const auto q=view.plane.toLocal(p);if(curve.kind==DrawingCurveKind::ProjectedEdge&&std::abs(q.z())<=view.tolerance)ref.clipBoundary|=1;
        if(drawingHalfSection(view)&&std::abs(q.x())<=view.tolerance)ref.clipBoundary|=2;
    }
}
double fractionParameter(const DrawingCurve &curve,double fraction){
    if(fraction<=0)return curve.range.lo;
    if(fraction>=1)return curve.range.hi;
    const auto type=curve.curve->type();if(type==CurveType::Line||type==CurveType::Circle)return curve.range.lo+fraction*curve.range.length();
    const double total=arcLength(*curve.curve,curve.range);double lo=curve.range.lo,hi=curve.range.hi;
    for(int i=0;i<42;++i){const double mid=.5*(lo+hi);if(arcLength(*curve.curve,{curve.range.lo,mid})<total*fraction)lo=mid;else hi=mid;}
    return .5*(lo+hi);
}
DrawingResolvedAttachment resolve(const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const DrawingAttachment &ref){
    DrawingResolvedAttachment out;out.status=DrawingDimensionStatus::Ready;
    if(!ref.axisId.isNull())missing(QStringLiteral("Asse associato non disponibile senza il foglio della tavola."));
    if(drawingDatum(ref.role)){out.isPoint=ref.role==DrawingPointRole::Origin;out.isLine=!out.isPoint;out.first={};out.second=ref.role==DrawingPointRole::AxisY?Vec2(0,1):Vec2(1,0);return out;}
    const auto &source=sourceFor(model,ref.curve.featureId);const auto &curve=curveFor(view,source,curves,ref);out.curve=&curve;
    out.first=local(view,curve.curve->point(curve.range.lo));out.second=local(view,curve.curve->point(curve.range.hi));
    const auto primitive=drawingPrimitive(view,curve);out.center=primitive.center;out.radius=norm(primitive.firstAxis);out.isCircle=primitive.circular;out.hasCenter=primitive.kind==DrawingPrimitive::Kind::Conic;out.isLine=primitive.kind==DrawingPrimitive::Kind::Line;
    if(primitive.linearProjection){out.first=primitive.lineFirst;out.second=primitive.lineSecond;}
    if(ref.role==DrawingPointRole::Curve){
        if(out.isLine&&!primitive.linearProjection&&(!ref.vertexName.isEmpty()||!ref.boundaryName.isEmpty())){
            const bool first=anchorAt(view,source,curve,ref,curve.curve->point(curve.range.lo)),last=anchorAt(view,source,curve,ref,curve.curve->point(curve.range.hi));
            if(first&&last)throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Orientamento della retta ambiguo.")};
            if(!first&&!last)missing(QStringLiteral("Orientamento della retta non ritrovato."));
            if(!first&&last)std::swap(out.first,out.second);
        }
        out.point=ref.polar?conicPoint(primitive,ref.fraction):local(view,curve.curve->point(fractionParameter(curve,ref.fraction)));return out;
    }
    out.isPoint=true;out.isLine=false;
    if(ref.role==DrawingPointRole::Center){need(primitive.kind==DrawingPrimitive::Kind::Conic,"La curva non ha un centro geometrico.");out.point=primitive.center;}
    else if(int(ref.role)>=int(DrawingPointRole::Quadrant0)){need(primitive.circular,"I quadranti richiedono un cerchio nel piano della vista.");const int k=int(ref.role)-int(DrawingPointRole::Quadrant0);out.point=primitive.center+out.radius*Vec2(k==0?1:k==2?-1:0,k==1?1:k==3?-1:0);}
    else if(ref.role==DrawingPointRole::Midpoint)out.point=local(view,curve.curve->point(fractionParameter(curve,.5)));
    else if(ref.polar){need(primitive.kind==DrawingPrimitive::Kind::Conic,"L'aggancio angolare richiede una conica nel piano.");out.point=conicPoint(primitive,ref.fraction);
        const auto point=view.plane.toGlobal(Vec3(out.point.x(),out.point.y(),0));if(projectPoint(*curve.curve,point,curve.range).distance>view.tolerance)missing(QStringLiteral("Il punto angolare non appartiene piu' all'arco."));}
    else{
        const bool first=anchorAt(view,source,curve,ref,curve.curve->point(curve.range.lo)),last=anchorAt(view,source,curve,ref,curve.curve->point(curve.range.hi));
        if(!first&&!last)missing(QStringLiteral("Estremo topologico dell'aggancio non ritrovato."));
        if(first&&last&&(distance(out.first,out.second)>view.tolerance||ref.role==DrawingPointRole::Nearest))throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("L'ancoraggio identifica entrambi gli estremi.")};
        if(ref.role==DrawingPointRole::Start||ref.role==DrawingPointRole::End)out.point=first?out.first:out.second;
        else out.point=local(view,curve.curve->point(fractionParameter(curve,first?ref.fraction:1-ref.fraction)));
    }
    if(int(ref.role)>=int(DrawingPointRole::Quadrant0)){const auto p=view.plane.toGlobal(Vec3(out.point.x(),out.point.y(),0));if(projectPoint(*curve.curve,p,curve.range).distance>view.tolerance)missing(QStringLiteral("Il quadrante non appartiene piu' all'arco."));}
    return out;
}
}
std::optional<DrawingProfileRadius> drawingProfileRadius(const DrawingView &view,const DrawingCurve &curve,const DrawingModelSnapshot &model,double parameter){
    if(curve.kind!=DrawingCurveKind::Section||!curve.sourceFace.valid())return {};
    const auto &source=sourceFor(model,curve.featureId);const auto &face=source.body->face(curve.sourceFace);
    if(face.surface->type()!=SurfaceType::BSpline)return {};
    const auto &surface=static_cast<const BSplineSurface &>(*face.surface);
    // Le pezze del raccordo hanno un profilo razionale quadratico trasversale.
    // Una generica spline di sezione non acquisisce un raggio per approssimazione.
    if(!surface.isRational()||(surface.uDegree()==2)==(surface.vDegree()==2))return {};
    const auto visiblePoint=curve.curve->point(parameter);
    const auto original=curve.unfoldedToSource?curve.unfoldedToSource->applyToPoint(visiblePoint):curve.sourceGeometry?curve.sourceGeometry->point(parameter):visiblePoint;
    const auto uv=projectPoint(surface,original);if(uv.distance>view.tolerance)return {};
    const auto iso=surface.vDegree()==2?surface.uIsoCurve(uv.u):surface.vIsoCurve(uv.v);
    const auto circle=drawingRecognizeCircle(iso,view.tolerance);if(!circle)return {};
    auto center=circle->center;if(curve.unfoldedToSource)center=curve.unfoldedToSource->inverted().applyToPoint(center);
    else if(curve.sourceGeometry)center+=visiblePoint-original;
    const auto local=view.plane.toLocal(center);return DrawingProfileRadius{norm(circle->first),{local.x(),local.y()}};
}
bool drawingDatum(DrawingPointRole role){return role==DrawingPointRole::Origin||role==DrawingPointRole::AxisX||role==DrawingPointRole::AxisY;}
QString validateDrawingAttachments(const DrawingDimension &d,const DrawingView &view){
    try{
        need(int(d.measure)>=0&&int(d.measure)<=12,"Tipo di quota non supportato.");
        need(d.decimals>=0&&d.decimals<=8&&d.angleSides>=0&&d.angleSides<=3,"Presentazione della quota non valida.");
        need(d.measure==DrawingMeasure::Angle||d.angleSides==0,"Settore angolare su una quota lineare.");
        if(d.references.empty()){need(int(d.measure)<=3,"Quota senza riferimenti di aggancio.");return {};}
        need(d.references.size()<=2,"Troppi riferimenti della quota.");const DrawingAttachment *primary=nullptr;
        for(const auto &r:d.references){need(r.clipBoundary>=0&&(r.clipBoundary<=3||(r.clipBoundary==4&&view.step)||(r.clipBoundary==5&&view.bend))&&(!r.clipBoundary||(!drawingDatum(r.role)&&r.vertexName.isEmpty()&&r.boundaryName.isEmpty()&&view.kind==DrawingViewKind::Section)),"Origine dell'aggancio di taglio non valida.");need(int(r.role)>=0&&int(r.role)<=12&&std::isfinite(r.fraction)&&r.fraction>=0&&r.fraction<=1,"Aggancio della quota non valido.");
            need(r.supportFace.size()<=4096&&r.curveSide>=0&&r.curveSide<=15&&r.anchorSide>=0&&r.anchorSide<=15,"Localizzatore della faccia non valido.");
            need((r.curveSide||r.anchorSide)?!r.supportFace.isEmpty():r.supportFace.isEmpty(),"Faccia di supporto incoerente.");
            need((r.curveSide&3)!=3&&(r.curveSide&12)!=12&&(r.anchorSide&3)!=3&&(r.anchorSide&12)!=12,"Lati UV incompatibili.");
            need(r.vertexName.size()<=4096&&r.boundaryName.size()<=4096&&(r.vertexName.isEmpty()||r.boundaryName.isEmpty()),"Origine dell'aggancio non valida.");
            need(r.curve.kind==DrawingCurveKind::ApparentArc?std::abs(r.curve.contourSide)==1:r.curve.contourSide==0,"Lato dell'aggancio geometrico non valido.");
            if(!r.axisId.isNull()){
                need(r.role==DrawingPointRole::Curve||r.role==DrawingPointRole::Start||r.role==DrawingPointRole::End||r.role==DrawingPointRole::Midpoint||r.role==DrawingPointRole::Nearest,"Tipo di aggancio all’asse non supportato.");
                need(!r.curve.featureId&&r.curve.kind==DrawingCurveKind::Section&&r.curve.faceName.isEmpty()&&r.curve.edgeName.isEmpty()&&!r.curve.requiresWitness&&norm(r.curve.witness)==0&&r.supportFace.isEmpty()&&!r.curveSide&&!r.anchorSide&&!r.polar&&r.vertexName.isEmpty()&&r.boundaryName.isEmpty()&&!r.clipBoundary,"Aggancio all’asse con dati topologici estranei.");
            }else if(drawingDatum(r.role))need(r.supportFace.isEmpty()&&!r.curveSide&&!r.anchorSide&&!r.polar&&r.curve.kind==DrawingCurveKind::Section&&!r.curve.requiresWitness&&norm(r.curve.witness)==0&&!r.curve.featureId&&r.curve.faceName.isEmpty()&&r.curve.edgeName.isEmpty()&&r.vertexName.isEmpty()&&r.boundaryName.isEmpty()&&r.fraction==0,"Riferimento del piano con dati topologici estranei.");
            else{if(!primary)primary=&r;need(r.curve.featureId&&isFinite(r.curve.witness),"Sorgente dell'aggancio non valida.");
                need(((r.curve.kind==DrawingCurveKind::Section||r.curve.kind==DrawingCurveKind::ApparentLine||r.curve.kind==DrawingCurveKind::ApparentArc)&&!r.curve.faceName.isEmpty()&&r.curve.edgeName.isEmpty())||(r.curve.kind==DrawingCurveKind::ProjectedEdge&&!r.curve.edgeName.isEmpty()&&r.curve.faceName.isEmpty()),"Origine geometrica dell'aggancio non valida.");
                need(r.curve.faceName.size()<=4096&&r.curve.edgeName.size()<=4096,"Nome geometrico troppo lungo.");
                need(!r.polar||r.role==DrawingPointRole::Nearest||r.role==DrawingPointRole::Curve,"Aggancio angolare incompatibile con il tipo di punto.");
                if(r.role==DrawingPointRole::Start||r.role==DrawingPointRole::End||(r.role==DrawingPointRole::Nearest&&!r.polar))need(!r.vertexName.isEmpty()||!r.boundaryName.isEmpty()||r.clipBoundary,"Punto privo di ancoraggio topologico.");
                need(r.boundaryName.isEmpty()||r.curve.kind==DrawingCurveKind::Section||r.curve.kind==DrawingCurveKind::ApparentLine,"Bordo di sezione su una proiezione.");}
        }
        const bool hasAxis=std::any_of(d.references.begin(),d.references.end(),[](const auto &r){return !r.axisId.isNull();});
        need(primary?same(primary->curve,d.section):(hasAxis&&!d.section.featureId&&d.section.faceName.isEmpty()&&d.section.edgeName.isEmpty()&&d.section.kind==DrawingCurveKind::Section&&!d.section.requiresWitness&&norm(d.section.witness)==0),"Il riferimento principale non corrisponde agli agganci.");
        const bool single=d.measure==DrawingMeasure::ProfileRadius||d.measure==DrawingMeasure::Radius||d.measure==DrawingMeasure::Diameter||d.measure==DrawingMeasure::SectionLength;
        need(d.references.size()==(single?1u:2u),"Numero di riferimenti incompatibile con il tipo di quota.");return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
DrawingAttachment drawingAttachment(const DrawingView &view,const DrawingCurve &curve,const DrawingModelSnapshot &model,DrawingPointRole role,double parameter){
    DrawingAttachment ref;ref.curve=dimensionOfCurve(view,curve).section;ref.role=role;need(!drawingDatum(role),"Usare un riferimento del piano senza curva.");
    if(std::isfinite(parameter))need(parameter>=curve.range.lo&&parameter<=curve.range.hi,"Parametro dell'aggancio fuori dalla curva.");
    const auto &source=sourceFor(model,curve.featureId);setCurveSupport(source,curve,ref);const auto primitive=drawingPrimitive(view,curve);
    if(curve.kind==DrawingCurveKind::ApparentLine){
        if(role==DrawingPointRole::Curve||role==DrawingPointRole::Midpoint)return ref;
        need(role==DrawingPointRole::Start||role==DrawingPointRole::End,"Sulla generatrice scegliere retta, punto medio o estremo.");
        need(curve.sourceGeometry&&curve.hasSourceRange,"Generatrice senza geometria sorgente.");
        setAnchor(view,source,curve,curve.sourceGeometry->point(role==DrawingPointRole::End?curve.sourceRange.hi:curve.sourceRange.lo),ref);
        need(!ref.vertexName.isEmpty()||!ref.boundaryName.isEmpty(),"Estremo della generatrice senza bordo topologico stabile.");return ref;
    }
    if(primitive.linearProjection&&role==DrawingPointRole::Curve)return ref;
    if((role==DrawingPointRole::Curve||role==DrawingPointRole::Nearest)&&primitive.kind==DrawingPrimitive::Kind::Conic){
        const auto q=local(view,curve.curve->point(std::isfinite(parameter)?parameter:curve.range.lo))-primitive.center;
        ref.polar=true;ref.fraction=std::atan2(q.y(),q.x())/kTwoPi;if(ref.fraction<0)ref.fraction+=1;return ref;
    }
    if(role==DrawingPointRole::Nearest&&distance(curve.curve->point(curve.range.lo),curve.curve->point(curve.range.hi))<=view.tolerance)
        throw ReferenceError{DrawingDimensionStatus::Ambiguous,QStringLiteral("Il verso della curva chiusa non e' identificabile: scegliere un riferimento geometrico univoco.")};
    if(role==DrawingPointRole::Start||role==DrawingPointRole::End||role==DrawingPointRole::Nearest||role==DrawingPointRole::Curve){
        setAnchor(view,source,curve,curve.curve->point(role==DrawingPointRole::End?curve.range.hi:curve.range.lo),ref);
        if(role!=DrawingPointRole::Curve)need(!ref.vertexName.isEmpty()||!ref.boundaryName.isEmpty()||ref.clipBoundary,"L'estremo non dispone di un'origine topologica stabile.");
    }
    if(std::isfinite(parameter)){need(parameter>=curve.range.lo&&parameter<=curve.range.hi,"Parametro dell'aggancio fuori dalla curva.");
        const double length=arcLength(*curve.curve,curve.range);ref.fraction=parameter<=curve.range.lo?0:parameter>=curve.range.hi?1:arcLength(*curve.curve,{curve.range.lo,parameter})/length;}
    return ref;
}
DrawingResolvedAttachment resolveDrawingAttachment(const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const DrawingAttachment &ref,const DrawingAttachmentResolver &axes){
    try{return !ref.axisId.isNull()&&axes?axes(ref):resolve(view,model,curves,ref);}catch(const ReferenceError &e){DrawingResolvedAttachment out;out.status=e.status;out.message=e.message;return out;}catch(const std::exception &e){DrawingResolvedAttachment out;out.message=QString::fromUtf8(e.what());return out;}
}
namespace {
DrawingResolvedAttachment resolved(const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const DrawingAttachment &ref,const DrawingAttachmentResolver &axes){
    auto result=resolveDrawingAttachment(view,model,curves,ref,axes);
    if(result.status!=DrawingDimensionStatus::Ready)throw ReferenceError{result.status,result.message};
    return result;
}
}
DrawingDimensionResult evaluateDrawingAttachments(const DrawingDimension &d,const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const DrawingAttachmentResolver &axes){
    try{
        const auto error=validateDrawingAttachments(d,view);need(error.isEmpty(),qPrintable(error));need(!d.references.empty(),"Quota legacy senza agganci.");
        auto a=resolved(view,model,curves,d.references[0],axes);DrawingResolvedAttachment b;if(d.references.size()==2)b=resolved(view,model,curves,d.references[1],axes);
        DrawingDimensionResult out;out.status=DrawingDimensionStatus::Ready;out.geometry=DrawingDimensionResult::Geometry::Linear;
        using M=DrawingMeasure;
        if(d.measure==M::SectionLength){need(a.curve,"La lunghezza richiede una curva.");out.value=arcLength(*a.curve->curve,a.curve->range);out.first=a.point;out.second=a.point;}
        else if(d.measure==M::Diameter&&a.curve&&drawingPrimitive(view,*a.curve).linearProjection){
            const auto primitive=drawingPrimitive(view,*a.curve);
            need(primitive.sourceCircular&&std::abs(distance(primitive.lineFirst,primitive.lineSecond)-2*primitive.sourceRadius)<=view.tolerance,"Il bordo proiettato non rappresenta un diametro completo.");
            out.value=2*primitive.sourceRadius;out.first=primitive.lineFirst;out.second=primitive.lineSecond;out.diametral=true;
        }
        else if(d.measure==M::ProfileRadius){need(a.curve&&!a.isPoint,"Il raggio del profilo richiede una curva del raccordo.");
            const auto profile=drawingProfileRadius(view,*a.curve,model,fractionParameter(*a.curve,d.references.front().fraction));need(profile.has_value(),"Profilo circolare del raccordo non disponibile: verificare la superficie sorgente.");
            out.value=profile->radius;out.first=profile->center;out.second=a.point;out.geometry=DrawingDimensionResult::Geometry::Radius;
        }
        else if(d.measure==M::Radius||d.measure==M::Diameter){need(a.isCircle&&!a.isPoint,"Raggio e diametro richiedono un cerchio o arco circolare nel piano della vista.");out.value=a.radius*(d.measure==M::Diameter?2:1);out.first=a.center;out.second=a.point;
            out.geometry=d.measure==M::Diameter?DrawingDimensionResult::Geometry::Diameter:DrawingDimensionResult::Geometry::Radius;}
        else if(drawingOrdinate(d.measure)){
            need(a.isPoint&&b.isPoint,"Le ordinate richiedono origine e punto associativi.");
            out.first=a.point;out.second=b.point;out.value=d.measure==M::OrdinateX?b.point.x()-a.point.x():b.point.y()-a.point.y();out.geometry=DrawingDimensionResult::Geometry::Ordinate;
        }
        else if(d.measure==M::Angle){need(a.isLine&&b.isLine,"La quota angolare richiede due rette.");auto u=normalized(a.second-a.first),v=normalized(b.second-b.first);const double determinant=cross2(u,v);
            need(std::abs(determinant)>1e-10,"Le rette parallele non definiscono un angolo di quota.");out.vertex=a.first+u*(cross2(b.first-a.first,v)/determinant);
            if(d.angleSides&1)u=-u;
            if(d.angleSides&2)v=-v;
            out.firstDirection=u;out.secondDirection=v;out.sweep=std::atan2(cross2(u,v),dot(u,v));out.value=std::abs(out.sweep)*180/kPi;out.geometry=DrawingDimensionResult::Geometry::Angle;}
        else{
            if(d.measure==M::AxisRadius||d.measure==M::AxisDiameter){need((!d.references[1].axisId.isNull()&&b.isLine)||d.references[1].role==DrawingPointRole::AxisX||d.references[1].role==DrawingPointRole::AxisY,"Scegliere un asse come secondo riferimento.");}
            if(a.isPoint&&b.isPoint){out.first=a.point;out.second=b.point;}
            else if(a.isPoint!=b.isPoint){const auto &point=a.isPoint?a:b,&other=a.isPoint?b:a;out.first=point.point;
                if(other.isLine){const auto u=normalized(other.second-other.first);out.second=other.first+u*dot(point.point-other.first,u);}
                else{need(other.curve,"Il secondo riferimento non e' una curva.");const auto p=view.plane.toGlobal(Vec3(point.point.x(),point.point.y(),0));out.second=local(view,projectPoint(*other.curve->curve,p,other.curve->range).point);}}
            else{need(a.isLine&&b.isLine,"Selezionare due punti, punto e retta, oppure rette parallele.");const auto u=normalized(a.second-a.first),v=normalized(b.second-b.first);need(std::abs(cross2(u,v))<1e-9,"Per rette non parallele usare una quota angolare.");out.first=b.first;out.second=a.first+u*dot(b.first-a.first,u);}
            if(d.measure==M::HorizontalDistance||d.measure==M::VerticalDistance){
                const auto aligned=[&](const DrawingResolvedAttachment &r){return r.isPoint||(r.isLine&&std::abs(d.measure==M::HorizontalDistance?(r.second-r.first).x():(r.second-r.first).y())<=view.tolerance);};
                need(aligned(a)&&aligned(b),"Scegliere due punti oppure rette perpendicolari alla direzione di quota.");
            }
            const auto delta=out.second-out.first;out.value=d.measure==M::HorizontalDistance?std::abs(delta.x()):d.measure==M::VerticalDistance?std::abs(delta.y()):norm(delta);
            if(d.measure==M::AxisDiameter){out.second=2*out.second-out.first;out.value*=2;}
        }
        if(out.geometry==DrawingDimensionResult::Geometry::Linear&&a.curve&&b.curve&&d.references.size()==2){
            if(a.isPoint&&b.isPoint&&a.curve->featureId==b.curve->featureId&&a.curve->edgeName==b.curve->edgeName&&a.curve->kind==DrawingCurveKind::ProjectedEdge){
                const auto p=drawingPrimitive(view,*a.curve);
                if(p.circular&&distance(.5*(a.point+b.point),p.center)<=view.tolerance&&std::abs(out.value-2*norm(p.firstAxis))<=view.tolerance)out.diametral=true;
            }
            if(!a.isPoint&&!b.isPoint&&a.curve->kind==DrawingCurveKind::ApparentLine&&b.curve->kind==DrawingCurveKind::ApparentLine&&a.curve->featureId==b.curve->featureId&&a.curve->sourceFace==b.curve->sourceFace){
                const auto &surface=sourceFor(model,a.curve->featureId).body->face(a.curve->sourceFace).surface;
                if(surface->type()==SurfaceType::Cylinder&&std::abs(out.value-2*static_cast<const CylindricalSurface &>(*surface).radius())<=view.tolerance)out.diametral=true;
            }
            const auto rotational=[](DrawingCurveKind k){return k==DrawingCurveKind::ApparentLine||k==DrawingCurveKind::Section;};
            if(a.isPoint&&b.isPoint&&rotational(a.curve->kind)&&rotational(b.curve->kind)&&a.curve->featureId==b.curve->featureId&&a.curve->sourceFace==b.curve->sourceFace){
                const auto &surface=sourceFor(model,a.curve->featureId).body->face(a.curve->sourceFace).surface;const bool cone=surface->type()==SurfaceType::Cone;
                if(cone||surface->type()==SurfaceType::Cylinder){
                    const auto frame=cone?static_cast<const ConicalSurface &>(*surface).frame():static_cast<const CylindricalSurface &>(*surface).frame();
                    const auto axis3=view.plane.directionToLocal(frame.zDir());const Vec2 axis(axis3.x(),axis3.y());const auto center=local(view,frame.origin()),middle=.5*(a.point+b.point);const double h=dot(middle-center,axis);
                    const double radius=cone?std::abs(static_cast<const ConicalSurface &>(*surface).referenceRadius()+h*std::tan(static_cast<const ConicalSurface &>(*surface).semiAngle())):static_cast<const CylindricalSurface &>(*surface).radius();
                    if(std::abs(axis3.z())<1e-10&&std::abs(dot(b.point-a.point,axis))<=view.tolerance&&distance(middle,center+h*axis)<=view.tolerance&&std::abs(out.value-2*radius)<=view.tolerance)out.diametral=true;
                }
            }
        }
        need(std::isfinite(out.value)&&(out.value>=0||drawingOrdinate(d.measure)),"Misura non finita.");return out;
    }catch(const ReferenceError &e){return {e.status,std::numeric_limits<double>::quiet_NaN(),e.message};}catch(const std::exception &e){return {DrawingDimensionStatus::Invalid,std::numeric_limits<double>::quiet_NaN(),QString::fromUtf8(e.what())};}
}
int drawingAngleSector(const DrawingDimensionResult &base,const Vec2 &cursor){
    const auto w=cursor-base.vertex;for(int side=0;side<4;++side){const auto u=side&1?-base.firstDirection:base.firstDirection,v=side&2?-base.secondDirection:base.secondDirection;
        const double sweep=std::atan2(cross2(u,v),dot(u,v)),angle=std::atan2(cross2(u,w),dot(u,w));if(sweep*angle>=0&&std::abs(angle)<=std::abs(sweep))return side;}return 0;
}
DrawingDimension drawingDimensionBetween(const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,std::vector<DrawingAttachment> refs,DrawingMeasure measure,const Vec2 &cursor,bool automatic,const DrawingAttachmentResolver &axes){
    need(!refs.empty()&&refs.size()<=2,"Selezionare uno o due riferimenti.");using M=DrawingMeasure;auto a=resolved(view,model,curves,refs[0],axes);
    if(refs.size()==1&&a.isLine&&a.curve&&!drawingPrimitive(view,*a.curve).linearProjection&&(automatic||measure==M::Distance||measure==M::EndpointDistance||measure==M::HorizontalDistance||measure==M::VerticalDistance)){
        refs={drawingAttachment(view,*a.curve,model,DrawingPointRole::Start),drawingAttachment(view,*a.curve,model,DrawingPointRole::End)};a=resolved(view,model,curves,refs[0],axes);}
    if(refs.size()==2&&automatic){for(auto &ref:refs){auto r=resolved(view,model,curves,ref,axes);if(!r.isPoint&&r.hasCenter){ref.role=DrawingPointRole::Center;ref.polar=false;ref.fraction=0;ref.vertexName.clear();ref.boundaryName.clear();}}}
    a=resolved(view,model,curves,refs[0],axes);DrawingResolvedAttachment b;if(refs.size()==2)b=resolved(view,model,curves,refs[1],axes);
    if(automatic&&refs.size()==1&&a.curve&&drawingPrimitive(view,*a.curve).linearProjection){measure=M::Diameter;automatic=false;}
    if(automatic){
        if(refs.size()==1){need(a.isCircle&&!a.isPoint,"Aggiungere un secondo riferimento.");measure=distance(a.first,a.second)<=view.tolerance?M::Diameter:M::Radius;}
        else if(a.isLine&&b.isLine)measure=std::abs(cross2(normalized(a.second-a.first),normalized(b.second-b.first)))<1e-9?M::Distance:M::Angle;
        else if(a.isPoint&&b.isPoint){const bool insideX=cursor.x()>=std::min(a.point.x(),b.point.x())&&cursor.x()<=std::max(a.point.x(),b.point.x());
            const bool insideY=cursor.y()>=std::min(a.point.y(),b.point.y())&&cursor.y()<=std::max(a.point.y(),b.point.y());measure=insideX&&!insideY&&std::abs(a.point.x()-b.point.x())>view.tolerance?M::HorizontalDistance:
                insideY&&!insideX&&std::abs(a.point.y()-b.point.y())>view.tolerance?M::VerticalDistance:M::Distance;}
        else measure=M::Distance;
    }
    if(refs.size()==1&&measure==M::Radius&&!a.isCircle&&a.curve&&drawingProfileRadius(view,*a.curve,model,fractionParameter(*a.curve,refs.front().fraction)))measure=M::ProfileRadius;
    DrawingDimension d;d.viewId=view.id;d.measure=measure;d.references=std::move(refs);for(const auto &r:d.references)if(r.axisId.isNull()&&!drawingDatum(r.role)){d.section=r.curve;break;}
    auto result=evaluateDrawingAttachments(d,view,model,curves,axes);need(result.status==DrawingDimensionStatus::Ready,qPrintable(result.message));
    if(measure==M::Angle)d.angleSides=drawingAngleSector(result,cursor);
    return d;
}
std::vector<DrawingDimension> drawingDimensionSeries(const DrawingView &view,const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves,const std::vector<DrawingAttachment> &points,DrawingDimensionSeries kind,const DrawingAttachmentResolver &axes){
    need(int(kind)>=0&&int(kind)<=3&&points.size()>=2&&points.size()<=64,"Scegliere da 2 a 64 punti della stessa vista.");
    const bool horizontal=kind==DrawingDimensionSeries::ChainHorizontal||kind==DrawingDimensionSeries::OrdinateX;
    const bool ordinate=kind==DrawingDimensionSeries::OrdinateX||kind==DrawingDimensionSeries::OrdinateY;
    std::vector<Vec2> positions;for(const auto &ref:points){const auto p=resolved(view,model,curves,ref,axes);need(p.isPoint,"La serie richiede punti, centri o estremi associativi.");
        if(ordinate)for(const auto &prior:positions)need(std::abs(horizontal?p.point.x()-prior.x():p.point.y()-prior.y())>view.tolerance,"Le ordinate devono avere coordinate distinte dall’origine e dagli altri punti.");
        if(!positions.empty())need(std::abs(horizontal?p.point.x()-positions.back().x():p.point.y()-positions.back().y())>view.tolerance,"Due punti consecutivi hanno la stessa coordinata di quota.");
        positions.push_back(p.point);
    }
    std::vector<DrawingDimension> result;for(std::size_t i=1;i<points.size();++i){const auto measure=ordinate?(horizontal?DrawingMeasure::OrdinateX:DrawingMeasure::OrdinateY):(horizontal?DrawingMeasure::HorizontalDistance:DrawingMeasure::VerticalDistance);
        result.push_back(drawingDimensionBetween(view,model,curves,{points[ordinate?0:i-1],points[i]},measure,positions[i],false,axes));}
    return result;
}
QString drawingDimensionName(DrawingMeasure m){switch(m){case DrawingMeasure::ProfileRadius:return QStringLiteral("Raggio profilo raccordo");case DrawingMeasure::OrdinateX:return QStringLiteral("Ordinata X");case DrawingMeasure::OrdinateY:return QStringLiteral("Ordinata Y");case DrawingMeasure::SectionLength:return QStringLiteral("Lunghezza");case DrawingMeasure::HorizontalDistance:return QStringLiteral("Orizzontale");case DrawingMeasure::VerticalDistance:return QStringLiteral("Verticale");case DrawingMeasure::Radius:return QStringLiteral("Raggio");case DrawingMeasure::Diameter:return QStringLiteral("Diametro");case DrawingMeasure::AxisRadius:return QStringLiteral("Raggio rispetto asse");case DrawingMeasure::AxisDiameter:return QStringLiteral("Diametro rispetto asse");case DrawingMeasure::Angle:return QStringLiteral("Angolo");default:return QStringLiteral("Distanza");}}
QString drawingDimensionText(const DrawingDimension &d,double value){const auto number=QString::number(std::abs(value)<.5*std::pow(10.,-d.decimals)?0:value,'f',d.decimals);if(d.measure==DrawingMeasure::Angle)return number+QStringLiteral("°");
    const auto prefix=d.measure==DrawingMeasure::ProfileRadius?QStringLiteral("R profilo "):d.measure==DrawingMeasure::OrdinateX?QStringLiteral("X = "):d.measure==DrawingMeasure::OrdinateY?QStringLiteral("Y = "):d.measure==DrawingMeasure::Radius||d.measure==DrawingMeasure::AxisRadius?QStringLiteral("R "):d.measure==DrawingMeasure::Diameter||d.measure==DrawingMeasure::AxisDiameter?QStringLiteral("⌀ "):d.measure==DrawingMeasure::SectionLength?QStringLiteral("L = "):QString();return prefix+number+QStringLiteral(" mm");}
}
