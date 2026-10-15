#include "cad_drawing.h"

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
        require(dimension.measure == DrawingMeasure::SectionLength || dimension.measure == DrawingMeasure::EndpointDistance
                || dimension.measure == DrawingMeasure::HorizontalDistance || dimension.measure == DrawingMeasure::VerticalDistance,"Tipo di quota non supportato.");
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
                result.sourceEdge=edgeId;result.edgeName=name;result.curve=curve;result.range=range;prepared.push_back(std::move(result));
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
        const bool extended=std::any_of(document.views.begin(),document.views.end(),[](const auto &view){return view.kind!=DrawingViewKind::Section;})
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
            dimensions.append(object);}
        const QByteArray prepared=QJsonDocument(QJsonObject{{"format","ForgeCAD.DrawingPrototype"},{"schema",extended?2:1},
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
        require(root["format"].toString()==QLatin1String("ForgeCAD.DrawingPrototype")&&(schema==1||schema==2),"Formato o schema della tavola non supportato.");
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
            if(schema==2){const double kind=v["kind"].toDouble(-1);require((kind==0||kind==1)&&v["sectionReferences"].isBool(),"Tipo o riferimenti della vista non validi.");
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
            require(measure==0||measure==1||(schema==2&&(measure==2||measure==3)),"Tipo di quota non supportato.");
            if(schema==2){const double kind=d["referenceKind"].toDouble(-1);require(kind==0||kind==1,"Tipo di riferimento non supportato.");
                dimension.section.kind=DrawingCurveKind(int(kind));
                if(kind==1){require(d["edgeName"].isString(),"Nome del bordo proiettato assente.");dimension.section.edgeName=d["edgeName"].toString();}
                else require(!d.contains("edgeName"),"Riferimento di sezione con nome di bordo estraneo.");}
            else require(!d.contains("referenceKind")&&!d.contains("edgeName"),"Estensione della quota non prevista dallo schema 1.");
            dimension.measure = DrawingMeasure(int(measure));
            prepared.dimensions.push_back(std::move(dimension));
        }
        validateDocument(prepared);
        document = std::move(prepared);
        return {};
    } catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
}
}
