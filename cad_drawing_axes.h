#pragma once
#include "cad_drawing_dimension.h"
#include "fk_cancel.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include <QPainterPath>
#include <QPointF>
#include <functional>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>

namespace ForgeCad {
enum class DrawingAxisKind { ThroughPoints, BetweenLines };
struct DrawingAxis {
    QUuid id=QUuid::createUuid(),viewId;
    QString name=QStringLiteral("Asse");
    DrawingAxisKind kind=DrawingAxisKind::ThroughPoints;
    std::vector<DrawingAttachment> references;
    double extension=2; // mm sul foglio, per lato
    bool visible=true;
};
struct DrawingAxisResult {
    DrawingDimensionStatus status=DrawingDimensionStatus::Invalid;
    QString message;
    Kernel::Vec2 first{},second{};
};
// Riutilizza il codec degli agganci esistenti, senza introdurre quote nel documento.
inline DrawingDimension drawingAxisSupport(const DrawingAxis &axis){
    DrawingDimension d;d.id=axis.id;d.viewId=axis.viewId;d.measure=DrawingMeasure::Distance;d.references=axis.references;
    for(const auto &r:axis.references)if(!drawingDatum(r.role)){d.section=r.curve;break;}
    return d;
}
inline DrawingAxisResult evaluateDrawingAxis(const DrawingAxis &axis,const DrawingView &view,
    const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves){
    using namespace Kernel;
    if(axis.viewId!=view.id||axis.references.size()!=2)return {DrawingDimensionStatus::Invalid,QStringLiteral("L'asse richiede due riferimenti della stessa vista.")};
    const auto a=resolveDrawingAttachment(view,model,curves,axis.references[0]),b=resolveDrawingAttachment(view,model,curves,axis.references[1]);
    for(const auto *ref:{&a,&b})if(ref->status!=DrawingDimensionStatus::Ready)return {ref->status,ref->message};
    DrawingAxisResult result;result.status=DrawingDimensionStatus::Ready;
    if(axis.kind==DrawingAxisKind::ThroughPoints){
        if(!a.isPoint||!b.isPoint)return {DrawingDimensionStatus::Invalid,QStringLiteral("Seleziona due punti geometrici: estremi, centri, punti medi o quadranti.")};
        result.first=a.point;result.second=b.point;
    }else if(axis.kind==DrawingAxisKind::BetweenLines){
        if(!a.isLine||!b.isLine||a.isPoint||b.isPoint)return {DrawingDimensionStatus::Invalid,QStringLiteral("La mediana richiede due rette parallele.")};
        const auto da=a.second-a.first,db=b.second-b.first;
        if(norm(da)<=view.tolerance||norm(db)<=view.tolerance)return {DrawingDimensionStatus::Invalid,QStringLiteral("Retta degenere per la mediana.")};
        auto u=normalized(da);const auto v=normalized(db);
        if(std::abs(u.x()*v.y()-u.y()*v.x())>1e-9)return {DrawingDimensionStatus::Invalid,QStringLiteral("Le rette selezionate non sono parallele.")};
        if(u.x()<-1e-12||(std::abs(u.x())<=1e-12&&u.y()<0))u=-u;
        const auto offset=b.first-a.first-u*dot(b.first-a.first,u);
        if(norm(offset)<=view.tolerance)return {DrawingDimensionStatus::Invalid,QStringLiteral("Le rette selezionate sono coincidenti.")};
        const auto origin=a.first+.5*offset;
        const auto span=[&](const DrawingResolvedAttachment &r){const double p=dot(r.first-origin,u),q=dot(r.second-origin,u);return Interval{std::min(p,q),std::max(p,q)};};
        const auto x=span(a),y=span(b);const double lo=std::max(x.lo,y.lo),hi=std::min(x.hi,y.hi);
        if(hi-lo<=view.tolerance)return {DrawingDimensionStatus::Invalid,QStringLiteral("I tratti paralleli non hanno una porzione longitudinale comune.")};
        result.first=origin+lo*u;result.second=origin+hi*u;
    }else return {DrawingDimensionStatus::Invalid,QStringLiteral("Tipo di asse non supportato.")};
    if(!isFinite(result.first)||!isFinite(result.second)||distance(result.first,result.second)<=view.tolerance)
        return {DrawingDimensionStatus::Invalid,QStringLiteral("L'asse richiede due punti distinti.")};
    return result;
}
inline QPainterPath drawingAxisPath(const DrawingAxisResult &result,const DrawingView &view,double extension){
    if(result.status!=DrawingDimensionStatus::Ready)return {};
    const QPointF a(result.first.x()*view.scale,-result.first.y()*view.scale),b(result.second.x()*view.scale,-result.second.y()*view.scale);
    auto d=b-a;const double length=std::hypot(d.x(),d.y());if(length<=1e-12)return {};d*=extension/length;
    QPainterPath path;path.moveTo(a-d);path.lineTo(b+d);return path;
}
// Un asse cilindrico deriva dalla superficie analitica e dal suo intervallo UV
// rifilato. La visibilita' decide quali facce ammettere, non la posizione dell'asse.
inline QPainterPath drawingCylinderAxes(const DrawingModelSnapshot &model,const DrawingView &view,
    const std::function<bool(quint64,Kernel::FaceId)> &eligible){
    using namespace Kernel;
    if(!drawingHasProjection(view))return {};
    struct Segment{double lo,hi;QPointF direction,normal;double offset;};
    using Key=std::pair<qint64,qint64>;
    std::map<Key,std::vector<Segment>> groups;
    for(const auto &source:model.bodies)if(source.body)for(auto faceId:source.body->faces()){
        cancellationPoint();if(!eligible(source.featureId,faceId))continue;
        const auto *surface=dynamic_cast<const CylindricalSurface *>(source.body->face(faceId).surface.get());if(!surface)continue;
        const auto &frame=surface->frame();const auto z=view.plane.directionToLocal(frame.zDir());
        const double projected=std::hypot(z.x(),z.y());if(projected<=1e-10)continue;
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(auto loop:source.body->face(faceId).loops)for(auto fin:source.body->loopFins(loop)){
            const auto &f=source.body->fin(fin);if(!f.pcurve)continue;
            const auto limits=curveBox(*embedCurve(f.pcurve,Frame3()),source.body->edge(f.edge).range);lo=std::min(lo,limits.lo.y());hi=std::max(hi,limits.hi.y());
        }
        if(!std::isfinite(lo)||!std::isfinite(hi)||hi-lo<=view.tolerance)continue;
        Line<3> axis(frame.origin(),frame.zDir());
        for(auto range:drawingRetainedCurveRanges(view,axis,{lo,hi})){
            const auto a=view.plane.toLocal(axis.point(range.lo)),b=view.plane.toLocal(axis.point(range.hi));
            QPointF p(a.x()*view.scale,-a.y()*view.scale),q(b.x()*view.scale,-b.y()*view.scale),d=q-p;
            const double length=std::hypot(d.x(),d.y());if(!std::isfinite(length)||length<1e-8)continue;d/=length;
            if(d.x()<-1e-12||(std::abs(d.x())<=1e-12&&d.y()<0))d=-d;
            const QPointF n(-d.y(),d.x());const double offset=QPointF::dotProduct(p,n);
            if(!std::isfinite(offset)||std::abs(offset)>1e8)continue;
            if(drawingHalfSection(view)&&std::abs(d.x())<1e-8&&std::abs(p.x())<1e-7)continue;
            const double x=QPointF::dotProduct(p,d),y=QPointF::dotProduct(q,d);
            const Key key{std::llround(std::atan2(d.y(),d.x())*1e8),std::llround(offset*1e7)};
            groups[key].push_back({std::min(x,y),std::max(x,y),d,n,offset});
        }
    }
    QPainterPath path;
    for(auto &[key,segments]:groups){(void)key;std::sort(segments.begin(),segments.end(),[](const auto &a,const auto &b){return a.lo<b.lo;});
        auto active=segments.front();const auto append=[&](const Segment &s){const auto base=s.offset*s.normal;path.moveTo(base+(s.lo-2)*s.direction);path.lineTo(base+(s.hi+2)*s.direction);};
        for(std::size_t i=1;i<segments.size();++i){if(segments[i].lo<=active.hi+1e-7)active.hi=std::max(active.hi,segments[i].hi);else{append(active);active=segments[i];}}append(active);
    }
    return path;
}
}
