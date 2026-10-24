#include "cad_drawing.h"
#include "cad_drawing_dimension.h"
#include "fk_bspline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_sheet.h"
#include "fk_transform.h"
#include "fk_surface_algo.h"

namespace ForgeCad {
namespace {
using namespace Kernel;
constexpr qsizetype kMaxDrawingBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxRecords = 10000;

void require(bool ok, const char *message) {
    if (!ok) throw std::invalid_argument(message);
}

// Mantiene parametro e derivate per curve generiche/isoparametriche del kernel.
class ProjectedCurve final:public Curve<3>{
    CurvePtr<3> source_;Frame3 plane_;
    void project(int order,Vec3 *values)const{
        const auto normal=plane_.zDir();values[0]-=normal*dot(values[0]-plane_.origin(),normal);
        for(int i=1;i<=order;++i)values[i]-=normal*dot(values[i],normal);
    }
public:
    ProjectedCurve(CurvePtr<3> source,Frame3 plane):source_(std::move(source)),plane_(std::move(plane)){}
    CurveType type()const override{return CurveType::Other;}
    Interval domain()const override{return source_->domain();}
    bool isPeriodic()const override{return source_->isPeriodic();}
    double period()const override{return source_->period();}
    std::vector<double> breakpoints(const Interval &range)const override{return source_->breakpoints(range);}
    void evaluate(double t,int order,Vec3 *values)const override{source_->evaluate(t,order,values);project(order,values);}
    void evaluateLeft(double t,int order,Vec3 *values)const override{source_->evaluateLeft(t,order,values);project(order,values);}
};

bool genericProjection(const Curve<3> &curve){
    switch(curve.type()){
    case CurveType::Line:case CurveType::Circle:case CurveType::Ellipse:case CurveType::BSpline:return false;
    case CurveType::Trimmed:return genericProjection(*static_cast<const TrimmedCurve<3> &>(curve).basis());
    default:return true;
    }
}

void validateView(const DrawingView &view) {
    require(!view.id.isNull(), "Vista senza identita'.");
    require(view.kind==DrawingViewKind::Section || view.kind==DrawingViewKind::ProjectedEdges,"Tipo di vista non supportato.");
    if(view.bend)require(!view.step&&view.kind==DrawingViewKind::Section&&view.sectionStyle==DrawingSectionStyle::Removed&&!view.sectionReferences
        &&std::isfinite(view.bend->at)&&view.bend->at>1e-7&&view.bend->at<=1e7&&std::isfinite(view.bend->angle)
        &&std::abs(view.bend->angle)>=.1&&std::abs(view.bend->angle)<=170,
        "La sezione allineata richiede una cerniera positiva e un angolo tra 0,1 e 170 gradi, senza gradini paralleli.");
    if(view.step){
        require(view.kind==DrawingViewKind::Section&&view.sectionStyle==DrawingSectionStyle::Removed&&!view.sectionReferences,
                "I gradini richiedono una sezione rimossa.");
        require(view.step->additional.size()<32,"Sono ammessi al massimo 32 gradini.");
        double previousAt=0,previousOffset=0;
        for(const auto &step:view.step->transitions()){
            require(std::isfinite(step.at)&&std::isfinite(step.offset)&&step.at>previousAt+1e-7&&step.at<=1e7
                    &&std::abs(step.offset-previousOffset)>view.tolerance&&std::abs(step.offset)<=1e7,
                    "Ordina le distanze dei gradini e usa scostamenti finiti diversi dal piano precedente.");
            previousAt=step.at;previousOffset=step.offset;
        }
    }
    require(int(view.sectionStyle)>=0&&int(view.sectionStyle)<=3&&(view.kind==DrawingViewKind::Section||view.sectionStyle==DrawingSectionStyle::Removed),"Presentazione della sezione incoerente con la vista.");
    require(view.kind==DrawingViewKind::ProjectedEdges || !view.sectionReferences,"Riferimenti di sezione incoerenti con il tipo di vista.");
    require(std::isfinite(view.scale) && view.scale > 0, "Scala della vista non valida.");
    require(std::isfinite(view.tolerance) && view.tolerance > 0, "Tolleranza della vista non valida.");
    const auto &p = view.plane;
    require(isFinite(p.origin()) && isFinite(p.xDir()) && isFinite(p.yDir()) && isFinite(p.zDir()),
            "Piano della vista non finito.");
    require(std::fabs(norm(p.xDir()) - 1) <= kAngularResolution && std::fabs(norm(p.yDir()) - 1) <= kAngularResolution
            && std::fabs(norm(p.zDir()) - 1) <= kAngularResolution
            && distance(cross(p.xDir(), p.yDir()), p.zDir()) <= kAngularResolution
            && std::fabs(dot(p.xDir(), p.yDir())) <= kAngularResolution, "Assi del piano non ortonormali e destrorsi.");
}

void validateDocument(const DrawingDocument &document) {
    require(!document.version.documentId.isNull() && !document.model.documentId.isNull()
            && document.version.documentId != document.model.documentId, "Identita' dei documenti non valide.");
    require(document.views.size() <= kMaxRecords && document.dimensions.size() <= kMaxRecords, "Troppi elementi nella tavola.");
    QSet<QUuid> ids, viewIds;
    for (const auto &view : document.views) {
        validateView(view);
        require(!ids.contains(view.id), "Identita' duplicata nella tavola.");
        ids.insert(view.id); viewIds.insert(view.id);
    }
    for (const auto &dimension : document.dimensions) {
        require(!dimension.id.isNull() && !ids.contains(dimension.id), "Identita' della quota non valida o duplicata.");
        ids.insert(dimension.id);
        require(viewIds.contains(dimension.viewId), "La quota riferisce una vista assente.");
        const auto view=std::find_if(document.views.begin(),document.views.end(),[&](const auto &v){return v.id==dimension.viewId;});
        const auto &reference=dimension.section;
        const bool edge=reference.kind==DrawingCurveKind::ProjectedEdge;
        require(reference.kind==DrawingCurveKind::Section || reference.kind==DrawingCurveKind::ApparentLine || reference.kind==DrawingCurveKind::ApparentArc || edge,"Tipo di riferimento non supportato.");
        require(reference.kind==DrawingCurveKind::ApparentArc?std::abs(reference.contourSide)==1:reference.contourSide==0,"Lato del riferimento geometrico non valido.");
        const bool axisOnly=!dimension.references.empty()&&std::all_of(dimension.references.begin(),dimension.references.end(),[](const auto &r){return !r.axisId.isNull()||drawingDatum(r.role);});
        if(!axisOnly){
        require(reference.featureId && isFinite(reference.witness),"Riferimento geometrico non valido.");
        require(edge ? (!reference.edgeName.isEmpty() && reference.edgeName.size()<=4096 && reference.faceName.isEmpty())
                     : (!reference.faceName.isEmpty() && reference.faceName.size()<=4096 && reference.edgeName.isEmpty()),"Nome del riferimento geometrico non valido.");
        require(edge||reference.kind==DrawingCurveKind::ApparentLine||reference.kind==DrawingCurveKind::ApparentArc ? drawingHasProjection(*view) : (view->kind==DrawingViewKind::Section || view->sectionReferences),"Riferimento estraneo al tipo di vista.");
        }
        require(int(dimension.measure)>=0&&int(dimension.measure)<=11,"Tipo di quota non supportato.");
        const auto attachments=validateDrawingAttachments(dimension,*view);require(attachments.isEmpty(),qPrintable(attachments));
        for(const auto &attachment:dimension.references)if(attachment.axisId.isNull()&&!drawingDatum(attachment.role)){
            const auto &r=attachment.curve;require((r.kind==DrawingCurveKind::ProjectedEdge||r.kind==DrawingCurveKind::ApparentLine||r.kind==DrawingCurveKind::ApparentArc)?drawingHasProjection(*view):(view->kind==DrawingViewKind::Section||view->sectionReferences),"Aggancio estraneo al tipo di vista.");
        }
    }
}

QJsonArray vectorJson(const Vec3 &v) { return {v[0], v[1], v[2]}; }
Vec3 readVector(const QJsonValue &value) {
    require(value.isArray(), "Vettore della tavola non valido.");
    const auto a = value.toArray();
    require(a.size() == 3, "Vettore della tavola incompleto.");
    for (const auto &v : a) require(v.isDouble() && std::isfinite(v.toDouble()), "Coordinata non valida.");
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
}
QString uuidText(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
QUuid readUuid(const QJsonValue &value, bool optional = false) {
    require(value.isString(), "Identita' della tavola non valida.");
    const QString text = value.toString();
    if (optional && text.isEmpty()) return {};
    const QUuid id(text);
    require(!id.isNull(), "Identita' della tavola nulla o malformata.");
    return id;
}
QJsonObject versionJson(const DrawingDocumentVersion &version) {
    return {{"documentId", uuidText(version.documentId)}, {"checkinId", uuidText(version.checkinId)}};
}
DrawingDocumentVersion readVersion(const QJsonValue &value) {
    require(value.isObject(), "Versione del documento assente.");
    const auto o = value.toObject();
    return {readUuid(o["documentId"]), readUuid(o["checkinId"], true)};
}
DrawingDimensionResult failure(DrawingDimensionStatus status, const QString &message) {
    return {status, std::numeric_limits<double>::quiet_NaN(), message};
}
}

bool drawingRetainsPoint(const DrawingView &view,const Vec3 &local){
    if(view.kind!=DrawingViewKind::Section||view.sectionStyle==DrawingSectionStyle::Removed)return true;
    if(view.sectionStyle==DrawingSectionStyle::Full)return local.z()<=view.tolerance;
    const double side=view.sectionStyle==DrawingSectionStyle::HalfRight?1:-1;
    return side*local.x()<=view.tolerance||local.z()<=view.tolerance;
}
std::vector<Interval> drawingRetainedCurveRanges(const DrawingView &view,const Curve<3> &curve,Interval range,bool section){
    if(view.sectionStyle==DrawingSectionStyle::Removed||(section&&!drawingHalfSection(view)))return {range};
    std::vector<double> cuts{range.lo,range.hi};
    const auto split=[&](const Frame3 &plane){for(double t:intersectCurveSurface(curve,range,Plane(plane),view.tolerance).parameters)if(t>range.lo&&t<range.hi)cuts.push_back(t);};
    if(!section)split(view.plane);
    if(drawingHalfSection(view))split(Frame3(view.plane.origin(),view.plane.xDir(),view.plane.yDir()));
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());std::vector<Interval> result;
    for(std::size_t i=1;i<cuts.size();++i){const auto local=view.plane.toLocal(curve.point(cuts[i-1]+.5*(cuts[i]-cuts[i-1])));
        const bool keep=section?(view.sectionStyle==DrawingSectionStyle::HalfRight?local.x()>=-view.tolerance:local.x()<=view.tolerance):drawingRetainsPoint(view,local);
        if(!keep)continue;
        if(!result.empty()&&result.back().hi==cuts[i-1])result.back().hi=cuts[i];else result.push_back({cuts[i-1],cuts[i]});
    }
    if(result.size()>1&&(curve.type()==CurveType::Circle||curve.type()==CurveType::Ellipse)&&std::abs(range.length()-curve.period())<=1e-10*curve.period()&&result.front().lo==range.lo&&result.back().hi==range.hi){
        const Interval wrapped{result.back().lo,result.front().hi+curve.period()};result.erase(result.begin());result.pop_back();result.push_back(wrapped);
    }
    return result;
}

DrawingPrimitive drawingPrimitive(const DrawingView &view,const DrawingCurve &piece){
    struct Form{int kind=0;Vec3 center{},a{},b{};};
    const std::function<Form(const Curve<3> &)> form=[&](const Curve<3> &curve)->Form{
        switch(curve.type()){
        case CurveType::Trimmed:return form(*static_cast<const TrimmedCurve<3> &>(curve).basis());
        case CurveType::Transformed:{const auto &c=static_cast<const TransformedCurve &>(curve);auto f=form(*c.basis());f.center=c.transform().applyToPoint(f.center);f.a=c.transform().applyToVector(f.a);f.b=c.transform().applyToVector(f.b);return f;}
        case CurveType::Line:{const auto &c=static_cast<const Line<3> &>(curve);return {1,c.origin(),c.direction(),{}};}
        case CurveType::Circle:{const auto &c=static_cast<const Circle<3> &>(curve);return {2,c.center(),c.radius()*c.xAxis(),c.radius()*c.yAxis()};}
        case CurveType::Ellipse:{const auto &c=static_cast<const Ellipse<3> &>(curve);return {2,c.center(),c.xRadius()*c.xAxis(),c.yRadius()*c.yAxis()};}
        case CurveType::BSpline:{const auto &c=static_cast<const BSplineCurve<3> &>(curve);if(c.poles().size()<2)return {};const auto a=c.poles().front(),d=c.poles().back()-a;
            if(norm(d)<=view.tolerance)return {};
            for(const auto &point:c.poles())if(norm(cross(point-a,normalized(d)))>view.tolerance)return {};
            return {1,a,d,{}};}
        default:return {};
        }
    };
    // A rigidly unfolded section is measured in its common plane. Original
    // geometry remains the topology witness, not an oblique projection.
    const auto exact=form(*(piece.unfoldedToSource?piece.curve:piece.sourceGeometry?piece.sourceGeometry:piece.curve));DrawingPrimitive out;
    const auto q=view.plane.toLocal(exact.center),a=view.plane.directionToLocal(exact.a),b=view.plane.directionToLocal(exact.b);
    out.center={q.x(),q.y()};out.firstAxis={a.x(),a.y()};out.secondAxis={b.x(),b.y()};
    if(exact.kind==1&&norm(out.firstAxis)>view.tolerance)out.kind=DrawingPrimitive::Kind::Line;
    else if(exact.kind==2&&std::max(norm(out.firstAxis),norm(out.secondAxis))>view.tolerance&&std::abs(out.firstAxis.x()*out.secondAxis.y()-out.firstAxis.y()*out.secondAxis.x())<=view.tolerance*std::max(norm(out.firstAxis),norm(out.secondAxis))){
        // Un cerchio visto di taglio e' un segmento finito, anche se i due
        // estremi parametrici del bordo chiuso coincidono.
        const auto u=normalized(norm(out.firstAxis)>norm(out.secondAxis)?out.firstAxis:out.secondAxis);
        const double x=dot(out.firstAxis,u),y=dot(out.secondAxis,u);const auto range=piece.hasSourceRange?piece.sourceRange:piece.range;
        const auto value=[&](double t){return x*std::cos(t)+y*std::sin(t);};
        double low=std::min(value(range.lo),value(range.hi)),high=std::max(value(range.lo),value(range.hi));
        const double phase=std::atan2(y,x);for(double t=phase+kPi*std::ceil((range.lo-phase)/kPi);t<=range.hi+1e-12;t+=kPi){low=std::min(low,value(t));high=std::max(high,value(t));}
        out.kind=DrawingPrimitive::Kind::Line;out.linearProjection=true;out.lineFirst=out.center+low*u;out.lineSecond=out.center+high*u;
    }
    else if(exact.kind==2&&norm(out.firstAxis)>view.tolerance&&norm(out.secondAxis)>view.tolerance){out.kind=DrawingPrimitive::Kind::Conic;
        const double x=norm(out.firstAxis),y=norm(out.secondAxis);out.circular=std::abs(x-y)<=1e-10*std::max({1.0,x,y})&&std::abs(dot(out.firstAxis,out.secondAxis))<=1e-10*x*y;}
    return out;
}

Transform3 drawingSectionUnfoldTransform(const DrawingView &view){
    validateView(view);require(view.bend.has_value(),"Sezione allineata assente.");
    return Transform3::rotation(view.plane.toGlobal(Vec3(view.bend->at,0,0)),view.plane.yDir(),view.bend->angle*kPi/180);
}
DrawingView drawingSectionBentPlane(const DrawingView &view){
    const auto rotate=drawingSectionUnfoldTransform(view).inverted();auto part=view;part.bend.reset();
    part.plane=Frame3::fromAxes(rotate.applyToPoint(view.plane.origin()),rotate.applyToVector(view.plane.xDir()),rotate.applyToVector(view.plane.yDir()),rotate.applyToVector(view.plane.zDir()));return part;
}
QString sectionDrawingView(const DrawingModelSnapshot &model, const DrawingView &view,
                           std::vector<DrawingSection> &sections) {
    try {
        validateView(view);
        require(!model.version.documentId.isNull(), "Modello senza identita'.");
        std::vector<DrawingSection> prepared;
        if(view.bend){
            const auto unfold=drawingSectionUnfoldTransform(view);
            for(int side=0;side<2;++side){
                auto part=side?drawingSectionBentPlane(view):view;part.bend.reset();
                const Plane divider(Frame3(part.plane.toGlobal(Vec3(view.bend->at,0,0)),part.plane.xDir(),part.plane.yDir()));
                std::vector<DrawingCurve> pieces;const auto error=sectionDrawingView(model,part,pieces);if(!error.isEmpty())return error;
                for(const auto &piece:pieces){cancellationPoint();std::vector<double> cuts{piece.range.lo,piece.range.hi};
                    const auto split=intersectCurveSurface(*piece.curve,piece.range,divider,view.tolerance);
                    for(double t:split.parameters)if(t>piece.range.lo&&t<piece.range.hi)cuts.push_back(t);
                    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
                    for(std::size_t i=1;i<cuts.size();++i){const double x=part.plane.toLocal(piece.curve->point(cuts[i-1]+.5*(cuts[i]-cuts[i-1]))).x();
                        if(side?x<view.bend->at:x>view.bend->at)continue;
                        auto clipped=piece;clipped.range={cuts[i-1],cuts[i]};clipped.sourceGeometry=piece.curve;clipped.sourceRange=clipped.range;clipped.hasSourceRange=true;clipped.multipleBranches=true;
                        if(side){clipped.curve=transformCurve(piece.curve,unfold);clipped.unfoldedToSource=unfold.inverted();}
                        prepared.push_back(std::move(clipped));
                    }
                }
            }
            sections=std::move(prepared);return {};
        }
        if(view.step){
            const auto steps=view.step->transitions();
            for(std::size_t band=0;band<=steps.size();++band){
                const double left=band?steps[band-1].at:-std::numeric_limits<double>::infinity();
                const double right=band<steps.size()?steps[band].at:std::numeric_limits<double>::infinity();
                auto part=view;part.step.reset();const Vec3 shift=(band?steps[band-1].offset:0)*view.plane.zDir();
                part.plane=Frame3::fromAxes(view.plane.origin()+shift,view.plane.xDir(),view.plane.yDir(),view.plane.zDir());
                std::vector<DrawingCurve> pieces;const auto error=sectionDrawingView(model,part,pieces);if(!error.isEmpty())return error;
                for(const auto &piece:pieces){
                    cancellationPoint();std::vector<double> cuts{piece.range.lo,piece.range.hi};
                    // Si ritagliano solo curve esistenti, senza creare bordi sui raccordi.
                    for(double boundary:{left,right})if(std::isfinite(boundary)){
                        const Plane divider(Frame3(view.plane.toGlobal(Vec3(boundary,0,0)),view.plane.xDir(),view.plane.yDir()));
                        const auto split=intersectCurveSurface(*piece.curve,piece.range,divider,view.tolerance);
                        for(double t:split.parameters)if(t>piece.range.lo&&t<piece.range.hi)cuts.push_back(t);
                    }
                    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
                    for(std::size_t i=1;i<cuts.size();++i){
                        const double x=view.plane.toLocal(piece.curve->point(cuts[i-1]+.5*(cuts[i]-cuts[i-1]))).x();
                        if(x<left||x>right)continue;
                        auto clipped=piece;clipped.range={cuts[i-1],cuts[i]};
                        clipped.sourceGeometry=piece.curve;clipped.sourceRange=clipped.range;clipped.hasSourceRange=true;
                        clipped.curve=translatedCurve(piece.curve,-shift);clipped.multipleBranches=true;
                        prepared.push_back(std::move(clipped));
                    }
                }
            }
            sections=std::move(prepared);return {};
        }
        std::map<std::pair<quint64, std::string>, std::size_t> counts,originalCounts;
        QSet<quint64> ids;
        for (const auto &source : model.bodies) {
            cancellationPoint();
            require(source.featureId && !ids.contains(source.featureId), "Feature sorgente nulla o duplicata.");
            ids.insert(source.featureId);
            require(source.body && source.names && source.names->body == source.body.get(), "Corpo o nomi sorgenti mancanti o incoerenti.");
            if (source.body->faces().empty()) continue;
            Box bounds;
            for (FaceId f : source.body->faces()) bounds.add(faceBox(*source.body, f));
            const double half = 2 * std::max(1.0, bounds.diagonal()) + distance(view.plane.origin(), 0.5 * (bounds.lo + bounds.hi));
            require(std::isfinite(half), "Limiti geometrici della vista non validi.");
            const Body plane = makePlaneSheet(view.plane, half);
            BooleanOptions options; options.tolerance = view.tolerance;
            for (const auto &piece : bodySectionCurvesWithOrigins(plane, *source.body, options)) {
                require(piece.secondFace.valid(), "Sezione senza provenienza.");
                const auto name = source.names->primary(EntityKind::Face, piece.secondFace.index);
                require(!name.empty(), "Faccia di sezione senza nome topologico.");
                DrawingCurve curve;curve.viewId=view.id;curve.featureId=source.featureId;curve.sourceFace=piece.secondFace;curve.faceName=name;
                curve.curve=piece.curve;curve.range=piece.range;curve.coincident=piece.coincident;curve.tangent=piece.tangent;
                // Anche i rami esclusi dalla mezza sezione restano parte dell'origine topologica.
                for(const auto &alias:source.names->faces.at(piece.secondFace.index))++originalCounts[{source.featureId,alias}];
                for(const auto &range:drawingRetainedCurveRanges(view,*curve.curve,curve.range,true)){
                    auto clipped=curve;clipped.range=range;prepared.push_back(std::move(clipped));
                    // Tutti gli alias partecipano alla disambiguazione anche quando un ramo viene diviso.
                    for(const auto &alias:source.names->faces.at(piece.secondFace.index))++counts[{source.featureId,alias}];
                }
            }
        }
        for (auto &piece : prepared) piece.multipleBranches = counts.at({piece.featureId, piece.faceName}) > 1 || originalCounts.at({piece.featureId,piece.faceName}) > 1;
        sections = std::move(prepared);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString projectDrawingEdges(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &curves){
    try{
        validateView(view);require(!model.version.documentId.isNull(),"Modello senza identita'.");
        std::vector<DrawingCurve> prepared;QSet<quint64> ids;std::map<std::pair<quint64,std::string>,std::size_t> counts,originalCounts;
        for(const auto &source:model.bodies){
            cancellationPoint();require(source.featureId&&!ids.contains(source.featureId),"Feature sorgente nulla o duplicata.");ids.insert(source.featureId);
            require(source.body&&source.names&&source.names->body==source.body.get(),"Corpo o nomi sorgenti mancanti o incoerenti.");
            for(auto edgeId:source.body->edges()){
                cancellationPoint();const auto &edge=source.body->edge(edgeId);require(edge.curve&&edge.range.isFinite()&&edge.range.length()>0,"Bordo sorgente non valido.");
                for(const auto &alias:source.names->edges.at(edgeId.index))++originalCounts[{source.featureId,alias}];
                for(const auto retained:drawingRetainedCurveRanges(view,*edge.curve,edge.range)){
                CurvePtr<3> curve;Interval range=retained;
                if(view.sectionStyle!=DrawingSectionStyle::Removed||genericProjection(*edge.curve)){
                    curve=std::make_shared<ProjectedCurve>(edge.curve,view.plane);
                    const double length=arcLength(*curve,range);require(std::isfinite(length),"Proiezione del bordo non finita.");if(length<=view.tolerance)continue;
                }else{
                    const auto image=planarImage(*edge.curve,retained,view.plane);if(!image.curve)continue;curve=embedCurve(image.curve,view.plane);range=image.range;
                    const auto extent=curveBox(*curve,range).diagonal();require(std::isfinite(extent),"Limiti del bordo proiettato non finiti.");if(extent<=view.tolerance)continue;
                }
                const auto name=source.names->primary(EntityKind::Edge,edgeId.index);require(!name.empty(),"Bordo senza nome topologico.");
                DrawingCurve result;result.viewId=view.id;result.featureId=source.featureId;result.kind=DrawingCurveKind::ProjectedEdge;
                result.sourceEdge=edgeId;result.edgeName=name;result.sourceGeometry=edge.curve;result.sourceRange=retained;result.hasSourceRange=true;result.curve=curve;result.range=range;prepared.push_back(std::move(result));
                for(const auto &alias:source.names->edges.at(edgeId.index))++counts[{source.featureId,alias}];
                }
            }
        }
        for(auto &curve:prepared)curve.multipleBranches=counts.at({curve.featureId,curve.edgeName})>1||(view.sectionStyle!=DrawingSectionStyle::Removed&&originalCounts.at({curve.featureId,curve.edgeName})>1);
        curves=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
namespace {
// Generatrici esatte del cilindro tangenti alla direzione di vista. Il
// ritaglio usa i bordi B-rep della faccia e la classificazione sulla faccia;
// nessun punto della mesh entra nei riferimenti delle quote.
void appendApparentLines(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &out){
    if(view.kind!=DrawingViewKind::ProjectedEdges)return;
    for(const auto &source:model.bodies)for(auto faceId:source.body->faces()){
        cancellationPoint();const auto &face=source.body->face(faceId);if(face.surface->type()!=SurfaceType::Cylinder)continue;
        const auto &cylinder=static_cast<const CylindricalSurface &>(*face.surface);const auto axis=cylinder.frame().zDir(),direction=view.plane.zDir();
        // Riferimenti analitici per la vista laterale ortogonale al cilindro.
        if(std::abs(dot(axis,direction))>1e-10)continue;
        const auto radial=normalized(cross(axis,direction));const auto origin=cylinder.frame().origin();
        const auto bounds=faceBox(*source.body,faceId);double low=1e100,high=-1e100;
        for(int mask=0;mask<8;++mask){const double v=dot(Vec3(mask&1?bounds.hi.x():bounds.lo.x(),mask&2?bounds.hi.y():bounds.lo.y(),mask&4?bounds.hi.z():bounds.lo.z())-origin,axis);low=std::min(low,v);high=std::max(high,v);}
        const Plane meridian(Frame3(origin,cross(axis,radial),axis));std::vector<double> cuts{low,high};
        for(auto loop:face.loops)for(auto fin:source.body->loopFins(loop)){const auto &edge=source.body->edge(source.body->fin(fin).edge);const auto hits=intersectCurveSurface(*edge.curve,edge.range,meridian,view.tolerance);
            for(double t:hits.parameters)cuts.push_back(dot(edge.curve->point(t)-origin,axis));
            for(const auto &range:hits.coincident)for(double t:{range.lo,range.hi})cuts.push_back(dot(edge.curve->point(t)-origin,axis));}
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[&](double a,double b){return b-a<=view.tolerance;}),cuts.end());
        for(double side:{-1.,1.}){auto geometry=std::make_shared<Line<3>>(origin+side*cylinder.radius()*radial,axis);std::vector<Interval> retained;
            for(std::size_t i=1;i<cuts.size();++i){const Interval range{cuts[i-1],cuts[i]};if(range.length()<=view.tolerance||classifyPointOnFace(*source.body,faceId,geometry->point(.5*(range.lo+range.hi)),view.tolerance)==PointLocation::Outside)continue;
                if(!retained.empty()&&std::abs(retained.back().hi-range.lo)<=view.tolerance)retained.back().hi=range.hi;else retained.push_back(range);
            }
            for(const auto range:retained){
                const auto image=planarImage(*geometry,range,view.plane);if(!image.curve)continue;DrawingCurve curve;curve.viewId=view.id;curve.featureId=source.featureId;curve.kind=DrawingCurveKind::ApparentLine;curve.sourceFace=faceId;
                curve.faceName=source.names->primary(EntityKind::Face,faceId.index);require(!curve.faceName.empty(),"Contorno cilindrico senza nome della faccia.");curve.curve=embedCurve(image.curve,view.plane);curve.range=image.range;curve.sourceGeometry=geometry;curve.sourceRange=range;curve.hasSourceRange=true;curve.multipleBranches=true;out.push_back(std::move(curve));
            }
        }
    }
}
// Raccordi toroidali nella vista laterale: i due meridiani tangenti
// sono archi circolari esatti con raggio pari al raggio minore del toro.
void appendApparentArcs(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &out){
    if(view.kind!=DrawingViewKind::ProjectedEdges)return;
    for(const auto &source:model.bodies)for(auto faceId:source.body->faces()){
        cancellationPoint();const auto &face=source.body->face(faceId);if(face.surface->type()!=SurfaceType::Torus)continue;
        const auto &torus=static_cast<const ToroidalSurface &>(*face.surface);const auto origin=torus.frame().origin(),axis=torus.frame().zDir(),direction=view.plane.zDir();
        if(std::abs(dot(axis,direction))>1e-10)continue;
        const auto radial=normalized(cross(axis,direction));const Plane meridian(Frame3(origin,cross(axis,radial),axis));std::vector<Vec3> boundary;
        for(auto loop:face.loops)for(auto fin:source.body->loopFins(loop)){const auto &edge=source.body->edge(source.body->fin(fin).edge);const auto hits=intersectCurveSurface(*edge.curve,edge.range,meridian,view.tolerance);
            for(double t:hits.parameters)boundary.push_back(edge.curve->point(t));
            for(const auto &range:hits.coincident)for(double t:{range.lo,range.hi})boundary.push_back(edge.curve->point(t));}
        for(int side:{-1,1}){const auto x=double(side)*radial,center=origin+torus.majorRadius()*x;auto geometry=std::make_shared<Circle<3>>(center,x,axis,torus.minorRadius());std::vector<double> cuts{0,kTwoPi};
            for(const auto &point:boundary){const auto d=point-center;double angle=std::atan2(dot(d,axis),dot(d,x));if(angle<0)angle+=kTwoPi;const double seam=view.tolerance/std::max(torus.minorRadius(),view.tolerance);if(angle<seam||kTwoPi-angle<seam)angle=0;
                if(distance(geometry->point(angle),point)<=4*view.tolerance)cuts.push_back(angle);}
            std::sort(cuts.begin(),cuts.end());const double angular=view.tolerance/std::max(torus.minorRadius(),view.tolerance);cuts.erase(std::unique(cuts.begin(),cuts.end(),[&](double a,double b){return b-a<=angular;}),cuts.end());std::vector<Interval> retained;
            for(std::size_t i=1;i<cuts.size();++i){const Interval range{cuts[i-1],cuts[i]};if(range.length()*torus.minorRadius()<=view.tolerance||classifyPointOnFace(*source.body,faceId,geometry->point(.5*(range.lo+range.hi)),view.tolerance)==PointLocation::Outside)continue;
                if(!retained.empty()&&retained.back().hi==range.lo)retained.back().hi=range.hi;else retained.push_back(range);}
            if(retained.size()>1&&retained.front().lo==0&&retained.back().hi==kTwoPi){const Interval wrapped{retained.back().lo,retained.front().hi+kTwoPi};retained.erase(retained.begin());retained.pop_back();retained.push_back(wrapped);}
            for(const auto range:retained){const auto image=planarImage(*geometry,range,view.plane);if(!image.curve)continue;DrawingCurve curve;curve.viewId=view.id;curve.featureId=source.featureId;curve.kind=DrawingCurveKind::ApparentArc;curve.sourceFace=faceId;
                curve.faceName=source.names->primary(EntityKind::Face,faceId.index);require(!curve.faceName.empty(),"Raccordo toroidale senza nome della faccia.");curve.curve=embedCurve(image.curve,view.plane);curve.range=image.range;curve.sourceGeometry=geometry;curve.sourceRange=range;curve.hasSourceRange=true;curve.multipleBranches=true;curve.contourSide=side;out.push_back(std::move(curve));}
        }
    }
}
}
QString drawingViewCurves(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &curves){
    if(view.kind==DrawingViewKind::Section&&!drawingHasProjection(view))return sectionDrawingView(model,view,curves);
    std::vector<DrawingCurve> prepared;auto error=projectDrawingEdges(model,view,prepared);if(!error.isEmpty())return error;
    if(view.kind==DrawingViewKind::Section||view.sectionReferences){std::vector<DrawingCurve> sections;error=sectionDrawingView(model,view,sections);if(!error.isEmpty())return error;
        prepared.insert(prepared.end(),sections.begin(),sections.end());}
    appendApparentLines(model,view,prepared);appendApparentArcs(model,view,prepared);
    curves=std::move(prepared);return {};
}
DrawingDimension dimensionOfCurve(const DrawingView &view,const DrawingCurve &curve,DrawingMeasure measure){
    validateView(view);const bool edge=curve.kind==DrawingCurveKind::ProjectedEdge;
    require(curve.kind==DrawingCurveKind::Section||curve.kind==DrawingCurveKind::ApparentLine||curve.kind==DrawingCurveKind::ApparentArc||edge,"Tipo di curva non supportato.");
    require(curve.viewId==view.id&&curve.featureId&&curve.curve&&curve.range.isFinite()&&curve.range.length()>0,"Curva della quota non valida.");
    require((edge||curve.kind==DrawingCurveKind::ApparentLine||curve.kind==DrawingCurveKind::ApparentArc)?drawingHasProjection(view):(view.kind==DrawingViewKind::Section||view.sectionReferences),"Curva estranea alla vista.");
    require(edge?!curve.edgeName.empty():!curve.faceName.empty(),"Curva senza provenienza topologica.");
    require(measure==DrawingMeasure::SectionLength||measure==DrawingMeasure::EndpointDistance||measure==DrawingMeasure::HorizontalDistance||measure==DrawingMeasure::VerticalDistance,"Tipo di quota non supportato.");
    const auto point=view.plane.toLocal(curve.curve->point(0.5*(curve.range.lo+curve.range.hi)));
    require(std::fabs(point.z())<=view.tolerance,"La curva non appartiene al piano della vista.");
    DrawingDimension dimension;dimension.viewId=view.id;dimension.measure=measure;
    dimension.section.featureId=curve.featureId;dimension.section.witness=Vec2(point.x(),point.y());dimension.section.requiresWitness=curve.multipleBranches;
    dimension.section.kind=curve.kind;dimension.section.contourSide=curve.contourSide;
    if(edge)dimension.section.edgeName=QString::fromStdString(curve.edgeName);else dimension.section.faceName=QString::fromStdString(curve.faceName);
    return dimension;
}
DrawingDimension dimensionOfSection(const DrawingView &view,const DrawingSection &section,DrawingMeasure measure){
    require(section.kind==DrawingCurveKind::Section,"La curva non e' una sezione.");return dimensionOfCurve(view,section,measure);
}

DrawingDimensionResult evaluateDrawingDimension(const DrawingDocument &document, const DrawingModelSnapshot &model,
                                               const QUuid &dimensionId) {
    try {
        validateDocument(document);
        if (document.model.documentId != model.version.documentId || document.model.checkinId != model.version.checkinId)
            return failure(DrawingDimensionStatus::VersionMismatch, QStringLiteral("La tavola richiede un'altra versione del modello."));
        const auto dimension = std::find_if(document.dimensions.begin(), document.dimensions.end(),
                                          [&](const auto &d) { return d.id == dimensionId; });
        if (dimension == document.dimensions.end()) return failure(DrawingDimensionStatus::Missing, QStringLiteral("Quota assente."));
        const auto view = std::find_if(document.views.begin(), document.views.end(), [&](const auto &v) { return v.id == dimension->viewId; });
        if(!dimension->references.empty()){
            DrawingModelSnapshot selected;selected.version=model.version;QSet<quint64> wanted;
            for(const auto &reference:dimension->references)if(reference.axisId.isNull()&&!drawingDatum(reference.role))wanted.insert(reference.curve.featureId);
            for(const auto &s:model.bodies)if(wanted.contains(s.featureId))selected.bodies.push_back(s);
            std::vector<DrawingCurve> curves;const auto generated=drawingViewCurves(selected,*view,curves);
            if(!generated.isEmpty())return failure(DrawingDimensionStatus::Invalid,generated);
            return evaluateDrawingAttachments(*dimension,*view,model,curves);
        }
        const auto source = std::find_if(model.bodies.begin(), model.bodies.end(), [&](const auto &s) { return s.featureId == dimension->section.featureId; });
        if (source == model.bodies.end()) return failure(DrawingDimensionStatus::Missing, QStringLiteral("Feature sorgente assente."));
        if (std::count_if(model.bodies.begin(), model.bodies.end(), [&](const auto &s) {
                return s.featureId == dimension->section.featureId;
            }) != 1) return failure(DrawingDimensionStatus::Invalid, QStringLiteral("Identita' della feature duplicata nel modello."));
        // Calcola solo il corpo richiesto. Il chiamante puo' condividere la sezione
        // di vista tra piu' quote in una futura cache esterna a questo prototipo.
        std::vector<DrawingSection> pieces;
        const QString error = drawingViewCurves({model.version, {*source}}, *view, pieces);
        if (!error.isEmpty()) return failure(DrawingDimensionStatus::Invalid, error);
        const bool edge=dimension->section.kind==DrawingCurveKind::ProjectedEdge;
        const auto entities=source->names->find(edge?EntityKind::Edge:EntityKind::Face,(edge?dimension->section.edgeName:dimension->section.faceName).toStdString());
        std::vector<const DrawingSection *> candidates;
        for (const auto &piece : pieces)
            if(piece.kind==dimension->section.kind && std::find(entities.begin(),entities.end(),edge?piece.sourceEdge.index:piece.sourceFace.index)!=entities.end())candidates.push_back(&piece);
        if (candidates.empty()) return failure(DrawingDimensionStatus::Missing, QStringLiteral("Entita' o ramo geometrico non ritrovato."));
        const DrawingSection *chosen = candidates.front();
        if (candidates.size() > 1 || dimension->section.requiresWitness) {
            chosen = nullptr;
            const auto &p = dimension->section.witness;
            const Vec3 witness = view->plane.toGlobal(Vec3(p.x(), p.y(), 0));
            for (const auto *candidate : candidates)
                if (projectPoint(*candidate->curve, witness, candidate->range).distance <= view->tolerance) {
                    if (chosen) return failure(DrawingDimensionStatus::Ambiguous, QStringLiteral("Piu' rami contengono l'ancoraggio della quota."));
                    chosen = candidate;
                }
            if (!chosen) return failure(candidates.size() > 1 ? DrawingDimensionStatus::Ambiguous : DrawingDimensionStatus::Missing,
                                       QStringLiteral("L'ancoraggio non identifica il ramo di sezione originario."));
        }
        const auto a=view->plane.toLocal(chosen->curve->point(chosen->range.lo)),b=view->plane.toLocal(chosen->curve->point(chosen->range.hi));
        const double value=dimension->measure==DrawingMeasure::SectionLength?arcLength(*chosen->curve,chosen->range)
            :dimension->measure==DrawingMeasure::HorizontalDistance?std::abs(b.x()-a.x())
            :dimension->measure==DrawingMeasure::VerticalDistance?std::abs(b.y()-a.y()):distance(a,b);
        require(std::isfinite(value) && value >= 0, "Misura di sezione non valida.");
        return {DrawingDimensionStatus::Ready, value, {}};
    } catch (const std::exception &error) { return failure(DrawingDimensionStatus::Invalid, QString::fromUtf8(error.what())); }
}

QString retargetDrawingModel(DrawingDocument &document, const DrawingDocumentVersion &version) {
    if (version.documentId.isNull() || document.model.documentId != version.documentId)
        return QStringLiteral("L'aggiornamento deve riferire lo stesso documento modello.");
    if (!document.version.checkinId.isNull()) return QStringLiteral("Una tavola archiviata va prima aperta come copia di lavoro.");
    document.model = version;
    return {};
}

bool drawingNeedsAxisSchema(const DrawingDocument &document){
    for(const auto &d:document.dimensions)for(const auto &r:d.references)if(!r.axisId.isNull())return true;
    return false;
}
bool drawingNeedsSectionSchema(const DrawingDocument &document){
    if(std::any_of(document.views.begin(),document.views.end(),[](const auto &v){return v.sectionStyle!=DrawingSectionStyle::Removed;}))return true;
    for(const auto &d:document.dimensions)for(const auto &r:d.references)if(r.clipBoundary)return true;
    return false;
}
QString encodeDrawingDocument(const DrawingDocument &document, QByteArray &bytes) {
    try {
        validateDocument(document);
        const bool axisReferences=drawingNeedsAxisSchema(document);
        const bool apparent=std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &d){return d.section.kind==DrawingCurveKind::ApparentLine||std::any_of(d.references.begin(),d.references.end(),[](const auto &r){return r.curve.kind==DrawingCurveKind::ApparentLine;});});
        const bool apparentArc=std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &d){return d.section.kind==DrawingCurveKind::ApparentArc||std::any_of(d.references.begin(),d.references.end(),[](const auto &r){return r.curve.kind==DrawingCurveKind::ApparentArc;});});
        const bool stepped=std::any_of(document.views.begin(),document.views.end(),[](const auto &v){return v.step.has_value();});
        const bool multiple=std::any_of(document.views.begin(),document.views.end(),[](const auto &v){return v.step&&!v.step->additional.empty();});
        const bool bent=std::any_of(document.views.begin(),document.views.end(),[](const auto &v){return v.bend.has_value();});
        const bool ordinate=std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &d){return drawingOrdinate(d.measure);});
        const bool technical=ordinate||bent||stepped||apparentArc||apparent||axisReferences||drawingNeedsSectionSchema(document);
        const bool attachments=technical||std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &d){return !d.references.empty()||d.decimals!=2;});
        const bool extended=attachments||std::any_of(document.views.begin(),document.views.end(),[](const auto &view){return view.kind!=DrawingViewKind::Section;})
            ||std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &dimension){return int(dimension.measure)>1;});
        QJsonArray views,dimensions;
        for(const auto &view:document.views){QJsonObject object{
            {"id",uuidText(view.id)},{"origin",vectorJson(view.plane.origin())},{"x",vectorJson(view.plane.xDir())},
            {"y",vectorJson(view.plane.yDir())},{"z",vectorJson(view.plane.zDir())},{"scale",view.scale},{"tolerance",view.tolerance}};
            if(extended){object["kind"]=int(view.kind);object["sectionReferences"]=view.sectionReferences;}if(technical)object["sectionStyle"]=int(view.sectionStyle);if(view.step)object["step"]=QJsonArray{view.step->at,view.step->offset};if(view.step&&!view.step->additional.empty()){QJsonArray extra;for(const auto &step:view.step->additional)extra.append(QJsonArray{step.at,step.offset});object["additionalSteps"]=extra;}if(view.bend)object["bend"]=QJsonArray{view.bend->at,view.bend->angle};views.append(object);}
        for(const auto &dimension:document.dimensions){QJsonObject object{
            {"id",uuidText(dimension.id)},{"viewId",uuidText(dimension.viewId)},{"featureId",QString::number(dimension.section.featureId)},
            {"faceName",dimension.section.faceName},{"witness",QJsonArray{dimension.section.witness.x(),dimension.section.witness.y()}},
            {"requiresWitness",dimension.section.requiresWitness},{"measure",int(dimension.measure)}};
            if(dimension.section.contourSide)object["contourSide"]=dimension.section.contourSide;
            if(extended){object["referenceKind"]=int(dimension.section.kind);if(dimension.section.kind==DrawingCurveKind::ProjectedEdge)object["edgeName"]=dimension.section.edgeName;}
            if(attachments){QJsonArray references;for(const auto &r:dimension.references){references.append(QJsonObject{
                {"featureId",QString::number(r.curve.featureId)},{"faceName",r.curve.faceName},{"edgeName",r.curve.edgeName},{"kind",int(r.curve.kind)},
                {"witness",QJsonArray{r.curve.witness.x(),r.curve.witness.y()}},{"requiresWitness",r.curve.requiresWitness},
                {"role",int(r.role)},{"fraction",r.fraction},{"polar",r.polar},{"supportFace",r.supportFace},{"curveSide",r.curveSide},{"anchorSide",r.anchorSide},{"vertex",r.vertexName},{"boundary",r.boundaryName}});if(technical){auto object=references.last().toObject();if(r.curve.contourSide)object["contourSide"]=r.curve.contourSide;object["clipBoundary"]=r.clipBoundary;if(!r.axisId.isNull())object["axisId"]=uuidText(r.axisId);references[references.size()-1]=object;}}
                object["references"]=references;object["angleSides"]=dimension.angleSides;object["decimals"]=dimension.decimals;}
            dimensions.append(object);}
        const QByteArray prepared=QJsonDocument(QJsonObject{{"format","ForgeCAD.DrawingPrototype"},{"schema",ordinate?11:bent?10:multiple?9:stepped?8:apparentArc?7:apparent?6:axisReferences?5:technical?4:attachments?3:extended?2:1},
            {"version",versionJson(document.version)},{"model",versionJson(document.model)},{"views",views},{"dimensions",dimensions}}).toJson(QJsonDocument::Compact);
        require(prepared.size() <= kMaxDrawingBytes, "Tavola troppo grande per il codec del prototipo.");
        bytes = prepared;
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString decodeDrawingDocument(const QByteArray &bytes, DrawingDocument &document) {
    try {
        require(!bytes.isEmpty() && bytes.size() <= kMaxDrawingBytes, "Dimensione del documento tavola non valida.");
        QJsonParseError parse;
        const auto json = QJsonDocument::fromJson(bytes, &parse);
        require(parse.error == QJsonParseError::NoError && json.isObject(), "Documento tavola JSON non valido.");
        const auto root = json.object();
        const double schema=root["schema"].toDouble(-1);
        require(root["format"].toString()==QLatin1String("ForgeCAD.DrawingPrototype")&&(schema==1||schema==2||schema==3||schema==4||schema==5||schema==6||schema==7||schema==8||schema==9||schema==10||schema==11),"Formato o schema della tavola non supportato.");
        require(root["views"].isArray() && root["dimensions"].isArray(), "Elenchi della tavola assenti.");
        const auto views = root["views"].toArray(), dimensions = root["dimensions"].toArray();
        require(views.size() <= qsizetype(kMaxRecords) && dimensions.size() <= qsizetype(kMaxRecords), "Troppi elementi nella tavola.");
        DrawingDocument prepared;
        prepared.version = readVersion(root["version"]); prepared.model = readVersion(root["model"]);
        for (const auto &entry : views) {
            require(entry.isObject(), "Vista non valida.");
            const auto v = entry.toObject(); DrawingView view;
            view.id = readUuid(v["id"]);
            view.plane = Frame3::fromAxes(readVector(v["origin"]), readVector(v["x"]), readVector(v["y"]), readVector(v["z"]));
            view.scale = v["scale"].toDouble(-1); view.tolerance = v["tolerance"].toDouble(-1);
            if(schema>=2){const double kind=v["kind"].toDouble(-1);require((kind==0||kind==1)&&v["sectionReferences"].isBool(),"Tipo o riferimenti della vista non validi.");
                view.kind=DrawingViewKind(int(kind));view.sectionReferences=v["sectionReferences"].toBool();}
            else require(!v.contains("kind")&&!v.contains("sectionReferences"),"Estensione della vista non prevista dallo schema 1.");
            if(schema>=4){const auto style=v["sectionStyle"].toDouble(-1);require(style>=0&&style<=3&&style==std::floor(style),"Presentazione della sezione non valida.");view.sectionStyle=DrawingSectionStyle(int(style));}
            else require(!v.contains("sectionStyle"),"Presentazione della sezione estranea allo schema dichiarato.");
            if(v.contains("step")){require(schema>=8&&v["step"].isArray(),"Gradino estraneo allo schema dichiarato.");
                const auto a=v["step"].toArray();require(a.size()==2&&a[0].isDouble()&&a[1].isDouble(),"Gradino incompleto.");view.step=DrawingSectionStep{a[0].toDouble(),a[1].toDouble()};}
            if(v.contains("additionalSteps")){require(schema>=9&&view.step&&v["additionalSteps"].isArray(),"Gradini multipli estranei allo schema dichiarato.");const auto extra=v["additionalSteps"].toArray();require(!extra.isEmpty()&&extra.size()<32,"Numero di gradini non valido.");for(const auto &entry:extra){require(entry.isArray(),"Gradino non valido.");const auto a=entry.toArray();require(a.size()==2&&a[0].isDouble()&&a[1].isDouble(),"Gradino incompleto.");view.step->additional.push_back({a[0].toDouble(),a[1].toDouble()});}}
            if(v.contains("bend")){require(schema>=10&&v["bend"].isArray(),"Sezione angolare estranea allo schema dichiarato.");const auto a=v["bend"].toArray();require(a.size()==2&&a[0].isDouble()&&a[1].isDouble(),"Sezione angolare incompleta.");view.bend=DrawingSectionBend{a[0].toDouble(),a[1].toDouble()};}
            prepared.views.push_back(std::move(view));
        }
        for (const auto &entry : dimensions) {
            require(entry.isObject(), "Quota non valida.");
            const auto d = entry.toObject(); DrawingDimension dimension;
            dimension.id = readUuid(d["id"]); dimension.viewId = readUuid(d["viewId"]);
            require(d["featureId"].isString() && d["faceName"].isString(), "Sorgente della quota non valida.");
            bool ok = false; dimension.section.featureId = d["featureId"].toString().toULongLong(&ok);
            require(ok, "Identita' della feature non valida.");
            dimension.section.faceName = d["faceName"].toString();
            require(d["witness"].isArray(), "Ancoraggio della quota assente.");
            const auto witness = d["witness"].toArray();
            require(witness.size() == 2 && witness[0].isDouble() && witness[1].isDouble(), "Ancoraggio della quota non valido.");
            dimension.section.witness = Vec2(witness[0].toDouble(), witness[1].toDouble());
            require(d["requiresWitness"].isBool(), "Regola di associazione del ramo assente.");
            dimension.section.requiresWitness = d["requiresWitness"].toBool();
            const double measure = d["measure"].toDouble(-1);
            require(measure==0||measure==1||(schema>=2&&(measure==2||measure==3))||(schema>=3&&measure>=4&&measure<=9&&measure==std::floor(measure))||(schema>=11&&(measure==10||measure==11)),"Tipo di quota non supportato.");
            if(schema>=2){const double kind=d["referenceKind"].toDouble(-1);require(kind==0||kind==1||(schema>=6&&kind==2)||(schema>=7&&kind==3),"Tipo di riferimento non supportato.");
                dimension.section.kind=DrawingCurveKind(int(kind));
                if(kind==1){require(d["edgeName"].isString(),"Nome del bordo proiettato assente.");dimension.section.edgeName=d["edgeName"].toString();}
                else require(!d.contains("edgeName"),"Riferimento di sezione con nome di bordo estraneo.");}
            else require(!d.contains("referenceKind")&&!d.contains("edgeName"),"Estensione della quota non prevista dallo schema 1.");
            if(d.contains("contourSide")){const double side=d["contourSide"].toDouble(0);require(schema>=7&&(side==-1||side==1)&&dimension.section.kind==DrawingCurveKind::ApparentArc,"Lato del raccordo apparente non valido.");dimension.section.contourSide=int(side);}
            if(schema>=3){
                require(d["references"].isArray()&&d["references"].toArray().size()<=2,"Agganci della quota assenti o eccessivi.");
                const auto sides=d["angleSides"].toDouble(-1),decimals=d["decimals"].toDouble(-1);
                require(sides>=0&&sides<=3&&sides==std::floor(sides)&&decimals>=0&&decimals<=8&&decimals==std::floor(decimals),"Presentazione della quota non valida.");dimension.angleSides=int(sides);dimension.decimals=int(decimals);
                for(const auto &entry:d["references"].toArray()){
                    require(entry.isObject(),"Aggancio non valido.");const auto r=entry.toObject();DrawingAttachment a;
                    require(r["featureId"].isString()&&r["faceName"].isString()&&r["edgeName"].isString()&&r["vertex"].isString()&&r["boundary"].isString()&&r["supportFace"].isString(),"Origine dell'aggancio assente.");
                    bool valid=false;a.curve.featureId=r["featureId"].toString().toULongLong(&valid);require(valid,"ID della sorgente dell'aggancio non valido.");
                    a.curve.faceName=r["faceName"].toString();a.curve.edgeName=r["edgeName"].toString();a.vertexName=r["vertex"].toString();a.boundaryName=r["boundary"].toString();a.supportFace=r["supportFace"].toString();
                    const auto curveSide=r["curveSide"].toDouble(-1),anchorSide=r["anchorSide"].toDouble(-1);require(curveSide>=0&&curveSide<=15&&curveSide==std::floor(curveSide)&&anchorSide>=0&&anchorSide<=15&&anchorSide==std::floor(anchorSide),"Localizzatore UV non valido.");a.curveSide=int(curveSide);a.anchorSide=int(anchorSide);
                    const auto kind=r["kind"].toDouble(-1),role=r["role"].toDouble(-1);require((kind==0||kind==1||(schema>=6&&kind==2)||(schema>=7&&kind==3))&&role>=0&&role<=12&&role==std::floor(role),"Tipo di aggancio non valido.");
                    a.curve.kind=DrawingCurveKind(int(kind));
                    if(r.contains("contourSide")){const double side=r["contourSide"].toDouble(0);require(schema>=7&&(side==-1||side==1)&&a.curve.kind==DrawingCurveKind::ApparentArc,"Lato dell'aggancio sul raccordo non valido.");a.curve.contourSide=int(side);}
                    a.role=DrawingPointRole(int(role));
                    const auto witness=r["witness"].toArray();require(r["witness"].isArray()&&witness.size()==2&&witness[0].isDouble()&&witness[1].isDouble()&&r["requiresWitness"].isBool()&&r["fraction"].isDouble()&&r["polar"].isBool(),"Parametri dell'aggancio non validi.");
                    a.curve.witness=Vec2(witness[0].toDouble(),witness[1].toDouble());a.curve.requiresWitness=r["requiresWitness"].toBool();
                    if(schema>=4){const auto boundary=r["clipBoundary"].toDouble(-1);require(boundary>=0&&(boundary<=3||(schema>=8&&boundary==4)||(schema>=10&&boundary==5))&&boundary==std::floor(boundary),"Origine di taglio dell'aggancio non valida.");a.clipBoundary=int(boundary);}else require(!r.contains("clipBoundary"),"Origine di taglio estranea allo schema dichiarato.");
                    if(r.contains("axisId")){require(schema>=5,"Asse estraneo allo schema degli agganci.");a.axisId=readUuid(r["axisId"]);}
                    a.fraction=r["fraction"].toDouble();a.polar=r["polar"].toBool();dimension.references.push_back(std::move(a));
                }
            }else require(!d.contains("references")&&!d.contains("angleSides")&&!d.contains("decimals"),"Agganci estesi non previsti dallo schema dichiarato.");
            dimension.measure = DrawingMeasure(int(measure));
            prepared.dimensions.push_back(std::move(dimension));
        }
        validateDocument(prepared);
        document = std::move(prepared);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}
}
