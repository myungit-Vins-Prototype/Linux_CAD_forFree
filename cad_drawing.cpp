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
        require(reference.kind==DrawingCurveKind::Section || edge,"Tipo di riferimento non supportato.");
        require(reference.featureId && isFinite(reference.witness),"Riferimento geometrico non valido.");
        require(edge ? (!reference.edgeName.isEmpty() && reference.edgeName.size()<=4096 && reference.faceName.isEmpty())
                     : (!reference.faceName.isEmpty() && reference.faceName.size()<=4096 && reference.edgeName.isEmpty()),"Nome del riferimento geometrico non valido.");
        require(edge ? view->kind==DrawingViewKind::ProjectedEdges : (view->kind==DrawingViewKind::Section || view->sectionReferences),"Riferimento estraneo al tipo di vista.");
        require(int(dimension.measure)>=0&&int(dimension.measure)<=9,"Tipo di quota non supportato.");
        const auto attachments=validateDrawingAttachments(dimension,*view);require(attachments.isEmpty(),qPrintable(attachments));
        for(const auto &attachment:dimension.references)if(!drawingDatum(attachment.role)){
            const auto &r=attachment.curve;require(r.kind==DrawingCurveKind::ProjectedEdge?view->kind==DrawingViewKind::ProjectedEdges:(view->kind==DrawingViewKind::Section||view->sectionReferences),"Aggancio estraneo al tipo di vista.");
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
    const auto exact=form(*(piece.sourceGeometry?piece.sourceGeometry:piece.curve));DrawingPrimitive out;
    const auto q=view.plane.toLocal(exact.center),a=view.plane.directionToLocal(exact.a),b=view.plane.directionToLocal(exact.b);
    out.center={q.x(),q.y()};out.firstAxis={a.x(),a.y()};out.secondAxis={b.x(),b.y()};
    if(exact.kind==1&&norm(out.firstAxis)>view.tolerance)out.kind=DrawingPrimitive::Kind::Line;
    else if(exact.kind==2&&norm(out.firstAxis)>view.tolerance&&norm(out.secondAxis)>view.tolerance){out.kind=DrawingPrimitive::Kind::Conic;
        const double x=norm(out.firstAxis),y=norm(out.secondAxis);out.circular=std::abs(x-y)<=1e-10*std::max({1.0,x,y})&&std::abs(dot(out.firstAxis,out.secondAxis))<=1e-10*x*y;}
    return out;
}

QString sectionDrawingView(const DrawingModelSnapshot &model, const DrawingView &view,
                           std::vector<DrawingSection> &sections) {
    try {
        validateView(view);
        require(!model.version.documentId.isNull(), "Modello senza identita'.");
        std::vector<DrawingSection> prepared;
        std::map<std::pair<quint64, std::string>, std::size_t> counts;
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
                prepared.push_back(std::move(curve));
                // Un nome ereditato puo' essere un alias secondario sulle
                // altre facce: anche queste appartengono ai candidati del ramo.
                for (const auto &alias : source.names->faces.at(piece.secondFace.index)) ++counts[{source.featureId, alias}];
            }
        }
        for (auto &piece : prepared) piece.multipleBranches = counts.at({piece.featureId, piece.faceName}) > 1;
        sections = std::move(prepared);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}

QString projectDrawingEdges(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &curves){
    try{
        validateView(view);require(!model.version.documentId.isNull(),"Modello senza identita'.");
        std::vector<DrawingCurve> prepared;QSet<quint64> ids;std::map<std::pair<quint64,std::string>,std::size_t> counts;
        for(const auto &source:model.bodies){
            cancellationPoint();require(source.featureId&&!ids.contains(source.featureId),"Feature sorgente nulla o duplicata.");ids.insert(source.featureId);
            require(source.body&&source.names&&source.names->body==source.body.get(),"Corpo o nomi sorgenti mancanti o incoerenti.");
            for(auto edgeId:source.body->edges()){
                cancellationPoint();const auto &edge=source.body->edge(edgeId);require(edge.curve&&edge.range.isFinite()&&edge.range.length()>0,"Bordo sorgente non valido.");
                CurvePtr<3> curve;Interval range=edge.range;
                if(genericProjection(*edge.curve)){
                    curve=std::make_shared<ProjectedCurve>(edge.curve,view.plane);
                    const double length=arcLength(*curve,range);require(std::isfinite(length),"Proiezione del bordo non finita.");if(length<=view.tolerance)continue;
                }else{
                    const auto image=planarImage(*edge.curve,edge.range,view.plane);if(!image.curve)continue;curve=embedCurve(image.curve,view.plane);range=image.range;
                    const auto extent=curveBox(*curve,range).diagonal();require(std::isfinite(extent),"Limiti del bordo proiettato non finiti.");if(extent<=view.tolerance)continue;
                }
                const auto name=source.names->primary(EntityKind::Edge,edgeId.index);require(!name.empty(),"Bordo senza nome topologico.");
                DrawingCurve result;result.viewId=view.id;result.featureId=source.featureId;result.kind=DrawingCurveKind::ProjectedEdge;
                result.sourceEdge=edgeId;result.edgeName=name;result.sourceGeometry=edge.curve;result.curve=curve;result.range=range;prepared.push_back(std::move(result));
                for(const auto &alias:source.names->edges.at(edgeId.index))++counts[{source.featureId,alias}];
            }
        }
        for(auto &curve:prepared)curve.multipleBranches=counts.at({curve.featureId,curve.edgeName})>1;
        curves=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString drawingViewCurves(const DrawingModelSnapshot &model,const DrawingView &view,std::vector<DrawingCurve> &curves){
    if(view.kind==DrawingViewKind::Section)return sectionDrawingView(model,view,curves);
    std::vector<DrawingCurve> prepared;auto error=projectDrawingEdges(model,view,prepared);if(!error.isEmpty())return error;
    if(view.sectionReferences){std::vector<DrawingCurve> sections;error=sectionDrawingView(model,view,sections);if(!error.isEmpty())return error;
        prepared.insert(prepared.end(),sections.begin(),sections.end());}
    curves=std::move(prepared);return {};
}
DrawingDimension dimensionOfCurve(const DrawingView &view,const DrawingCurve &curve,DrawingMeasure measure){
    validateView(view);const bool edge=curve.kind==DrawingCurveKind::ProjectedEdge;
    require(curve.kind==DrawingCurveKind::Section||edge,"Tipo di curva non supportato.");
    require(curve.viewId==view.id&&curve.featureId&&curve.curve&&curve.range.isFinite()&&curve.range.length()>0,"Curva della quota non valida.");
    require(edge?view.kind==DrawingViewKind::ProjectedEdges:(view.kind==DrawingViewKind::Section||view.sectionReferences),"Curva estranea alla vista.");
    require(edge?!curve.edgeName.empty():!curve.faceName.empty(),"Curva senza provenienza topologica.");
    require(measure==DrawingMeasure::SectionLength||measure==DrawingMeasure::EndpointDistance||measure==DrawingMeasure::HorizontalDistance||measure==DrawingMeasure::VerticalDistance,"Tipo di quota non supportato.");
    const auto point=view.plane.toLocal(curve.curve->point(0.5*(curve.range.lo+curve.range.hi)));
    require(std::fabs(point.z())<=view.tolerance,"La curva non appartiene al piano della vista.");
    DrawingDimension dimension;dimension.viewId=view.id;dimension.measure=measure;
    dimension.section.featureId=curve.featureId;dimension.section.witness=Vec2(point.x(),point.y());dimension.section.requiresWitness=curve.multipleBranches;
    dimension.section.kind=curve.kind;
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
            for(const auto &reference:dimension->references)if(!drawingDatum(reference.role))wanted.insert(reference.curve.featureId);
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

QString encodeDrawingDocument(const DrawingDocument &document, QByteArray &bytes) {
    try {
        validateDocument(document);
        const bool attachments=std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &d){return !d.references.empty()||d.decimals!=2;});
        const bool extended=attachments||std::any_of(document.views.begin(),document.views.end(),[](const auto &view){return view.kind!=DrawingViewKind::Section;})
            ||std::any_of(document.dimensions.begin(),document.dimensions.end(),[](const auto &dimension){return int(dimension.measure)>1;});
        QJsonArray views,dimensions;
        for(const auto &view:document.views){QJsonObject object{
            {"id",uuidText(view.id)},{"origin",vectorJson(view.plane.origin())},{"x",vectorJson(view.plane.xDir())},
            {"y",vectorJson(view.plane.yDir())},{"z",vectorJson(view.plane.zDir())},{"scale",view.scale},{"tolerance",view.tolerance}};
            if(extended){object["kind"]=int(view.kind);object["sectionReferences"]=view.sectionReferences;}views.append(object);}
        for(const auto &dimension:document.dimensions){QJsonObject object{
            {"id",uuidText(dimension.id)},{"viewId",uuidText(dimension.viewId)},{"featureId",QString::number(dimension.section.featureId)},
            {"faceName",dimension.section.faceName},{"witness",QJsonArray{dimension.section.witness.x(),dimension.section.witness.y()}},
            {"requiresWitness",dimension.section.requiresWitness},{"measure",int(dimension.measure)}};
            if(extended){object["referenceKind"]=int(dimension.section.kind);if(dimension.section.kind==DrawingCurveKind::ProjectedEdge)object["edgeName"]=dimension.section.edgeName;}
            if(attachments){QJsonArray references;for(const auto &r:dimension.references){references.append(QJsonObject{
                {"featureId",QString::number(r.curve.featureId)},{"faceName",r.curve.faceName},{"edgeName",r.curve.edgeName},{"kind",int(r.curve.kind)},
                {"witness",QJsonArray{r.curve.witness.x(),r.curve.witness.y()}},{"requiresWitness",r.curve.requiresWitness},
                {"role",int(r.role)},{"fraction",r.fraction},{"polar",r.polar},{"supportFace",r.supportFace},{"curveSide",r.curveSide},{"anchorSide",r.anchorSide},{"vertex",r.vertexName},{"boundary",r.boundaryName}});}
                object["references"]=references;object["angleSides"]=dimension.angleSides;object["decimals"]=dimension.decimals;}
            dimensions.append(object);}
        const QByteArray prepared=QJsonDocument(QJsonObject{{"format","ForgeCAD.DrawingPrototype"},{"schema",attachments?3:extended?2:1},
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
        require(root["format"].toString()==QLatin1String("ForgeCAD.DrawingPrototype")&&(schema==1||schema==2||schema==3),"Formato o schema della tavola non supportato.");
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
            require(measure==0||measure==1||(schema>=2&&(measure==2||measure==3))||(schema==3&&measure>=4&&measure<=9&&measure==std::floor(measure)),"Tipo di quota non supportato.");
            if(schema>=2){const double kind=d["referenceKind"].toDouble(-1);require(kind==0||kind==1,"Tipo di riferimento non supportato.");
                dimension.section.kind=DrawingCurveKind(int(kind));
                if(kind==1){require(d["edgeName"].isString(),"Nome del bordo proiettato assente.");dimension.section.edgeName=d["edgeName"].toString();}
                else require(!d.contains("edgeName"),"Riferimento di sezione con nome di bordo estraneo.");}
            else require(!d.contains("referenceKind")&&!d.contains("edgeName"),"Estensione della quota non prevista dallo schema 1.");
            if(schema==3){
                require(d["references"].isArray()&&d["references"].toArray().size()<=2,"Agganci della quota assenti o eccessivi.");
                const auto sides=d["angleSides"].toDouble(-1),decimals=d["decimals"].toDouble(-1);
                require(sides>=0&&sides<=3&&sides==std::floor(sides)&&decimals>=0&&decimals<=8&&decimals==std::floor(decimals),"Presentazione della quota non valida.");dimension.angleSides=int(sides);dimension.decimals=int(decimals);
                for(const auto &entry:d["references"].toArray()){
                    require(entry.isObject(),"Aggancio non valido.");const auto r=entry.toObject();DrawingAttachment a;
                    require(r["featureId"].isString()&&r["faceName"].isString()&&r["edgeName"].isString()&&r["vertex"].isString()&&r["boundary"].isString()&&r["supportFace"].isString(),"Origine dell'aggancio assente.");
                    bool valid=false;a.curve.featureId=r["featureId"].toString().toULongLong(&valid);require(valid,"ID della sorgente dell'aggancio non valido.");
                    a.curve.faceName=r["faceName"].toString();a.curve.edgeName=r["edgeName"].toString();a.vertexName=r["vertex"].toString();a.boundaryName=r["boundary"].toString();a.supportFace=r["supportFace"].toString();
                    const auto curveSide=r["curveSide"].toDouble(-1),anchorSide=r["anchorSide"].toDouble(-1);require(curveSide>=0&&curveSide<=15&&curveSide==std::floor(curveSide)&&anchorSide>=0&&anchorSide<=15&&anchorSide==std::floor(anchorSide),"Localizzatore UV non valido.");a.curveSide=int(curveSide);a.anchorSide=int(anchorSide);
                    const auto kind=r["kind"].toDouble(-1),role=r["role"].toDouble(-1);require((kind==0||kind==1)&&role>=0&&role<=12&&role==std::floor(role),"Tipo di aggancio non valido.");
                    a.curve.kind=DrawingCurveKind(int(kind));a.role=DrawingPointRole(int(role));
                    const auto witness=r["witness"].toArray();require(r["witness"].isArray()&&witness.size()==2&&witness[0].isDouble()&&witness[1].isDouble()&&r["requiresWitness"].isBool()&&r["fraction"].isDouble()&&r["polar"].isBool(),"Parametri dell'aggancio non validi.");
                    a.curve.witness=Vec2(witness[0].toDouble(),witness[1].toDouble());a.curve.requiresWitness=r["requiresWitness"].toBool();a.fraction=r["fraction"].toDouble();a.polar=r["polar"].toBool();dimension.references.push_back(std::move(a));
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
