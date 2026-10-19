#include "cad_drawing_sheet.h"
#include "cad_drawing_annotations.h"
#include "cad_drawing_derived.h"
#include "cad_drawing_dimension.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QHash>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace ForgeCad {
namespace {
void require(bool condition,const char *message){if(!condition)throw std::invalid_argument(message);}
void ok(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
bool finite(const QPointF &point){return std::isfinite(point.x())&&std::isfinite(point.y())&&std::abs(point.x())<=1e7&&std::abs(point.y())<=1e7;}
QJsonArray pointJson(const QPointF &point){return {point.x(),point.y()};}
QPointF readPoint(const QJsonValue &value){
    require(value.isArray(),"Posizione del foglio assente.");const auto array=value.toArray();
    require(array.size()==2&&array[0].isDouble()&&array[1].isDouble(),"Posizione del foglio non valida.");
    const QPointF point(array[0].toDouble(),array[1].toDouble());require(finite(point),"Posizione del foglio non finita.");return point;
}
QUuid readId(const QJsonValue &value){require(value.isString(),"Identita' del foglio assente.");const QUuid id(value.toString());require(!id.isNull(),"Identita' del foglio non valida.");return id;}
DrawingDocument axisSupportDocument(const DrawingSheet &sheet){
    auto document=sheet.drawing;document.dimensions.clear();
    for(const auto &axis:sheet.axes)document.dimensions.push_back(drawingAxisSupport(axis));
    return document;
}
DrawingDocument annotationSupportDocument(const DrawingSheet &sheet){
    auto document=sheet.drawing;document.dimensions.clear();
    for(const auto &note:sheet.annotations)if(note.reference)document.dimensions.push_back(drawingAnnotationSupport(note));
    return document;
}
void validate(const DrawingSheet &sheet){
    require(!sheet.title.trimmed().isEmpty()&&sheet.title.size()<=256,"Titolo della tavola non valido.");
    require(std::isfinite(sheet.size.width())&&std::isfinite(sheet.size.height())&&sheet.size.width()>=50&&sheet.size.height()>=50
            &&sheet.size.width()<=2000&&sheet.size.height()<=2000,"Dimensioni del foglio non valide.");
    require(sheet.views.size()==qsizetype(sheet.drawing.views.size())&&sheet.dimensions.size()==qsizetype(sheet.drawing.dimensions.size()),"Presentazione del foglio incompleta.");
    ok(validateDrawingStyle(sheet.style));
    const auto &block=sheet.titleBlock;
    require(block.code.size()<=128&&block.organization.size()<=128&&block.author.size()<=128&&block.date.size()<=32,"Campi del cartiglio troppo lunghi.");
    for(const auto &field:{block.code,block.organization,block.author,block.date})for(const QChar c:field)require(c.isPrint(),"Il cartiglio contiene caratteri non stampabili.");
    require(!block.visible||(sheet.size.width()>=195&&sheet.size.height()>=80),"Foglio troppo piccolo per il cartiglio: minimo 195 x 80 mm.");
    require(sheet.revisionTable.notes.size()<=1000,"Troppe annotazioni nella tabella revisioni.");QSet<QUuid> revisions;
    for(const auto &row:sheet.revisionTable.notes){
        require(!row.checkinId.isNull()&&!revisions.contains(row.checkinId)&&row.description.size()<=8192&&row.verifiedBy.size()<=256,"Riga della tabella revisioni non valida.");revisions.insert(row.checkinId);
        for(const auto &field:{row.description,row.verifiedBy})for(const auto c:field)require(c.isPrint()||c=='\n'||c=='\t',"Caratteri non stampabili nella tabella revisioni.");
    }
    require(!sheet.revisionTable.visible||sheet.size.width()>=195,"Foglio troppo stretto per la tabella revisioni: minimo 195 mm.");
    ok(validateDrawingRelations(sheet));
    require(sheet.axes.size()<=10000,"Troppi assi nella tavola.");QSet<QUuid> axisIds;
    for(const auto &v:sheet.drawing.views)axisIds.insert(v.id);
    for(const auto &d:sheet.drawing.dimensions)axisIds.insert(d.id);
    require(sheet.annotations.size()<=10000,"Troppe annotazioni nella tavola.");
    for(const auto &note:sheet.annotations){
        require(!note.id.isNull()&&!axisIds.contains(note.id),"Identita' della nota nulla o duplicata.");axisIds.insert(note.id);
    }
    const auto documentIds=axisIds;QHash<QUuid,QUuid> axisViews;
    for(const auto &axis:sheet.axes){
        require(!axis.id.isNull()&&!axisIds.contains(axis.id)&&!axis.name.trimmed().isEmpty()&&axis.name.size()<=128,"Identita' o nome dell'asse non validi.");axisIds.insert(axis.id);axisViews.insert(axis.id,axis.viewId);
        for(const auto c:axis.name)require(c.isPrint(),"Nome dell'asse non stampabile.");
        require(std::isfinite(axis.extension)&&axis.extension>=0&&axis.extension<=1000&&(axis.kind==DrawingAxisKind::ThroughPoints||axis.kind==DrawingAxisKind::BetweenLines)&&axis.references.size()==2,"Definizione dell'asse non valida.");
        const auto layout=std::find_if(sheet.views.cbegin(),sheet.views.cend(),[&](const auto &l){return l.viewId==axis.viewId;});require(layout!=sheet.views.cend(),"Vista dell'asse assente.");
        for(const auto &ref:axis.references){
            require(ref.axisId.isNull(),"Dipendenza ricorsiva fra assi non supportata.");
            if(!drawingDatum(ref.role))require(layout->features.contains(ref.curve.featureId),"Asse collegato a un corpo estraneo alla vista.");
            require(axis.kind==DrawingAxisKind::BetweenLines?ref.role==DrawingPointRole::Curve:(ref.role!=DrawingPointRole::Curve&&ref.role!=DrawingPointRole::AxisX&&ref.role!=DrawingPointRole::AxisY),"Tipo di aggancio incompatibile con l'asse.");
        }
    }
    if(!sheet.axes.isEmpty()){QByteArray checked;ok(encodeDrawingDocument(axisSupportDocument(sheet),checked));}
    for(const auto &note:sheet.annotations){
        require(!note.text.trimmed().isEmpty()&&note.text.size()<=8192,"Testo della nota vuoto o troppo lungo.");
        for(const auto c:note.text)require(c.isPrint()||c=='\n'||c=='\t',"Caratteri non stampabili nella nota.");
        require(finite(note.position)&&std::isfinite(note.width)&&note.width>=5&&note.width<=1000&&std::isfinite(note.height)&&note.height>=1&&note.height<=20,"Impaginazione della nota non valida.");
        require(note.reference.has_value()!=note.viewId.isNull(),"Vista e aggancio del richiamo incoerenti.");
        if(!note.reference)continue;
        const auto &ref=*note.reference;require(!drawingDatum(ref.role)&&ref.role!=DrawingPointRole::Curve,"Il richiamo richiede un punto geometrico.");
        const auto layout=std::find_if(sheet.views.cbegin(),sheet.views.cend(),[&](const auto &l){return l.viewId==note.viewId;});require(layout!=sheet.views.cend(),"Vista del richiamo assente.");
        if(ref.axisId.isNull())require(layout->features.contains(ref.curve.featureId),"Richiamo collegato a un corpo estraneo alla vista.");
        else {require(!documentIds.contains(ref.axisId),"Identita' asse del richiamo confusa con altri elementi.");if(axisViews.contains(ref.axisId))require(axisViews.value(ref.axisId)==note.viewId,"Asse del richiamo appartenente a un'altra vista.");}
    }
    if(!sheet.annotations.isEmpty()){QByteArray checked;ok(encodeDrawingDocument(annotationSupportDocument(sheet),checked));}
    QSet<QUuid> viewIds,dimensionIds;
    for(const auto &view:sheet.drawing.views)viewIds.insert(view.id);
    for(const auto &dimension:sheet.drawing.dimensions)dimensionIds.insert(dimension.id);
    for(const auto &layout:sheet.views){
        require(viewIds.remove(layout.viewId)&&finite(layout.position)&&!layout.title.trimmed().isEmpty()&&layout.title.size()<=256
                &&!layout.features.isEmpty()&&layout.features.size()<=10000,"Presentazione della vista non valida.");
        require(!layout.titleOffset||finite(*layout.titleOffset),"Posizione del titolo non valida.");
        QSet<quint64> features;for(auto id:layout.features){require(id&&!features.contains(id),"Sorgente della vista nulla o duplicata.");features.insert(id);}
    }
    for(const auto &layout:sheet.dimensions){require(dimensionIds.remove(layout.dimensionId)&&finite(layout.position),"Presentazione della quota non valida.");ok(validateDrawingDimensionPresentation(layout.presentation));}
    for(const auto &dimension:sheet.drawing.dimensions){
        const auto layout=std::find_if(sheet.views.cbegin(),sheet.views.cend(),[&](const auto &view){return view.viewId==dimension.viewId;});
        require(layout!=sheet.views.cend()&&(!dimension.section.featureId||layout->features.contains(dimension.section.featureId)),"La quota riferisce un corpo estraneo alla vista.");
        for(const auto &ref:dimension.references)if(ref.axisId.isNull()){if(!drawingDatum(ref.role))require(layout->features.contains(ref.curve.featureId),"Un aggancio della quota riferisce un corpo estraneo alla vista.");}
            else {if(axisViews.contains(ref.axisId))require(axisViews.value(ref.axisId)==dimension.viewId,"Asse della quota appartenente a un’altra vista.");require(!documentIds.contains(ref.axisId),"Identita’ dell’asse confusa con vista o quota.");}
    }
}
}
DrawingAttachmentResolver drawingSheetAttachmentResolver(const DrawingSheet &sheet,const DrawingView &view,
    const DrawingModelSnapshot &model,const std::vector<DrawingCurve> &curves){
    return [&sheet,&view,&model,&curves](const DrawingAttachment &ref){
        DrawingResolvedAttachment out;out.status=DrawingDimensionStatus::Missing;out.message=QStringLiteral("Asse associato non trovato.");
        const DrawingAxis *axis=nullptr;for(const auto &candidate:sheet.axes)if(candidate.id==ref.axisId){axis=&candidate;break;}
        if(!axis||axis->viewId!=view.id)return out;
        const auto result=evaluateDrawingAxis(*axis,view,model,curves);out.status=result.status;out.message=result.message;
        if(out.status!=DrawingDimensionStatus::Ready)return out;
        out.first=result.first;out.second=result.second;out.isLine=ref.role==DrawingPointRole::Curve;out.isPoint=!out.isLine;
        const double fraction=ref.role==DrawingPointRole::End?1:ref.role==DrawingPointRole::Midpoint?.5:ref.role==DrawingPointRole::Nearest?ref.fraction:0;
        out.point=out.first+fraction*(out.second-out.first);return out;
    };
}
QString encodeDrawingSheet(const DrawingSheet &sheet,QByteArray &bytes){
    try{
        validate(sheet);QByteArray document;ok(encodeDrawingDocument(sheet.drawing,document));QJsonArray views,dimensions;
        for(const auto &view:sheet.views){QJsonArray features;for(auto id:view.features)features.append(QString::number(id));
            QJsonObject object{{"id",view.viewId.toString(QUuid::WithoutBraces)},{"title",view.title},{"position",pointJson(view.position)},{"features",features}};
            if(view.titleOffset)object.insert("titleOffset",pointJson(*view.titleOffset));
            if(view.showHiddenLines)object.insert("showHiddenLines",true);
            if(view.showMesh)object.insert("showMesh",true);
            if(view.showCylinderAxes)object.insert("showCylinderAxes",true);
            if(view.showCenterMarks)object.insert("showCenterMarks",true);
            if(view.showCenterLines)object.insert("showCenterLines",true);
            if(view.relation.parentId.isNull()&&!view.relation.hatch)object.insert("sectionHatch",false);
            const auto &r=view.relation;if(!r.parentId.isNull())object.insert("relation",QJsonObject{
                {"parent",r.parentId.toString(QUuid::WithoutBraces)},{"kind",int(r.kind)},{"side",int(r.side)},{"method",int(r.method)},
                {"aligned",r.aligned},{"cutStart",pointJson(r.cutStart)},{"cutEnd",pointJson(r.cutEnd)},{"reverse",r.reverse},{"hatch",r.hatch},{"label",r.label}});
            views.append(object);}
        bool styled=false;
        for(const auto &dimension:sheet.dimensions){QJsonObject item{{"id",dimension.dimensionId.toString(QUuid::WithoutBraces)},{"position",pointJson(dimension.position)}};
            const auto &p=dimension.presentation;if(p.active()){styled=true;item.insert("presentation",QJsonObject{{"mode",int(p.mode)},{"upper",p.upper},{"lower",p.lower},{"decimals",p.decimals},{"prefix",p.prefix},{"suffix",p.suffix}});}dimensions.append(item);}
        QJsonObject object{{"format","ForgeCAD.DrawingSheet"},{"schema",(sheet.style.extensionGap!=0||sheet.style.dimensionOffset!=0)?9:(sheet.style.active()||std::any_of(sheet.views.cbegin(),sheet.views.cend(),[](const auto &v){return v.titleOffset.has_value();}))?8:styled?7:!sheet.annotations.isEmpty()?6:drawingNeedsAxisSchema(sheet.drawing)?5:!sheet.axes.isEmpty()?4:(drawingNeedsSectionSchema(sheet.drawing)||std::any_of(sheet.views.cbegin(),sheet.views.cend(),[](const auto &v){return v.relation.parentId.isNull()&&!v.relation.hatch;}))?3:std::any_of(sheet.views.cbegin(),sheet.views.cend(),[](const auto &v){return !v.relation.parentId.isNull();})?2:1},{"title",sheet.title},
            {"size",QJsonArray{sheet.size.width(),sheet.size.height()}},{"drawing",QJsonDocument::fromJson(document).object()},
            {"views",views},{"dimensions",dimensions}};
        if(sheet.style.active()){const auto &s=sheet.style;object.insert("style",QJsonObject{{"fontFamily",s.fontFamily},{"dimensionHeight",s.dimensionHeight},{"viewTitleHeight",s.viewTitleHeight},{"arrowLength",s.arrowLength},{"arrow",int(s.arrow)},{"titleGap",s.titleGap},{"titleSnaps",s.titleSnaps}});if(s.extensionGap!=0||s.dimensionOffset!=0)object.insert("dimensionSpacing",QJsonObject{{"extensionGap",s.extensionGap},{"dimensionOffset",s.dimensionOffset}});}
        if(!sheet.axes.isEmpty()){
            QByteArray encoded;ok(encodeDrawingDocument(axisSupportDocument(sheet),encoded));const auto support=QJsonDocument::fromJson(encoded).object();const auto definitions=support["dimensions"].toArray();QJsonArray axes;
            for(qsizetype i=0;i<sheet.axes.size();++i){const auto &a=sheet.axes[i];axes.append(QJsonObject{{"definition",definitions[i]},{"kind",int(a.kind)},{"name",a.name},{"extension",a.extension},{"visible",a.visible}});}
            object.insert("axes",axes);object.insert("axisReferenceSchema",support["schema"]);
        }
        if(!sheet.annotations.isEmpty()){
            QByteArray encoded;ok(encodeDrawingDocument(annotationSupportDocument(sheet),encoded));const auto support=QJsonDocument::fromJson(encoded).object();const auto definitions=support["dimensions"].toArray();QJsonArray notes;qsizetype index=0;
            for(const auto &n:sheet.annotations){QJsonObject entry{{"id",n.id.toString(QUuid::WithoutBraces)},{"text",n.text},{"position",pointJson(n.position)},{"width",n.width},{"height",n.height}};if(n.reference)entry.insert("reference",definitions[index++]);notes.append(entry);}
            object.insert("annotations",notes);if(index)object.insert("annotationReferenceSchema",support["schema"]);
        }
        const auto &block=sheet.titleBlock;if(block.visible||!block.code.isEmpty()||!block.organization.isEmpty()||!block.author.isEmpty()||!block.date.isEmpty())object.insert("titleBlock",QJsonObject{{"visible",block.visible},{"code",block.code},{"organization",block.organization},{"author",block.author},{"date",block.date}});
        if(sheet.revisionTable.visible||!sheet.revisionTable.notes.isEmpty()){
            QJsonArray notes;for(const auto &r:sheet.revisionTable.notes)notes.append(QJsonObject{{"checkinId",r.checkinId.toString(QUuid::WithoutBraces)},{"description",r.description},{"verifiedBy",r.verifiedBy}});
            object.insert("revisionTable",QJsonObject{{"visible",sheet.revisionTable.visible},{"notes",notes}});
        }
        auto prepared=QJsonDocument(object).toJson(QJsonDocument::Compact);
        require(prepared.size()<=8*1024*1024,"Foglio oltre i limiti del prototipo.");bytes=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString decodeDrawingSheet(const QByteArray &bytes,DrawingSheet &sheet){
    try{
        require(!bytes.isEmpty()&&bytes.size()<=8*1024*1024,"Dimensione del foglio non valida.");
        const auto json=QJsonDocument::fromJson(bytes);const auto object=json.object();
        require(json.isObject()&&object["format"].toString()==QLatin1String("ForgeCAD.DrawingSheet")&&(object["schema"].toInt(-1)==1||object["schema"].toInt(-1)==2||object["schema"].toInt(-1)==3||object["schema"].toInt(-1)==4||object["schema"].toInt(-1)==5||object["schema"].toInt(-1)==6||object["schema"].toInt(-1)==7||object["schema"].toInt(-1)==8||object["schema"].toInt(-1)==9),"Formato del foglio non supportato.");
        const int schema=object["schema"].toInt();
        require(schema>=6||(!object.contains("annotations")&&!object.contains("annotationReferenceSchema")),"Annotazioni estranee allo schema dichiarato.");
        require(schema!=6||object.contains("annotations"),"Annotazioni del foglio assenti.");
        require(object.contains("axes")==object.contains("axisReferenceSchema"),"Metadati assi incompleti.");
        require(schema>=4||(!object.contains("axes")&&!object.contains("axisReferenceSchema")),"Assi estranei allo schema del foglio dichiarato.");
        require(object["title"].isString()&&object["drawing"].isObject()&&object["views"].isArray()&&object["dimensions"].isArray(),"Dati del foglio incompleti.");
        require(schema>=8||!object.contains("style"),"Stile estraneo allo schema del foglio.");
        require(schema>=9||!object.contains("dimensionSpacing"),"Distanze quote estranee allo schema del foglio.");
        require(schema!=9||object.contains("dimensionSpacing"),"Distanze quote assenti.");
        DrawingSheet prepared;
        if(object.contains("style")){
            require(object["style"].isObject(),"Stile del foglio non valido.");const auto s=object["style"].toObject();
            require(s.size()==7&&s["fontFamily"].isString()&&s["titleSnaps"].isBool()&&s["arrow"].isDouble()&&s["arrow"].toDouble()==s["arrow"].toInt(-1),"Stile incompleto.");
            for(const auto key:{"dimensionHeight","viewTitleHeight","arrowLength","titleGap"})require(s[key].isDouble(),"Dimensioni dello stile assenti.");
            prepared.style={s["fontFamily"].toString(),s["dimensionHeight"].toDouble(),s["viewTitleHeight"].toDouble(),s["arrowLength"].toDouble(),s["titleGap"].toDouble(),DrawingArrowStyle(s["arrow"].toInt()),s["titleSnaps"].toBool()};
        }
        if(object.contains("dimensionSpacing")){
            require(object["dimensionSpacing"].isObject(),"Distanze quote non valide.");const auto spacing=object["dimensionSpacing"].toObject();
            require(spacing.size()==2&&spacing["extensionGap"].isDouble()&&spacing["dimensionOffset"].isDouble(),"Distanze quote incomplete.");
            prepared.style.extensionGap=spacing["extensionGap"].toDouble();prepared.style.dimensionOffset=spacing["dimensionOffset"].toDouble();
        }
        prepared.title=object["title"].toString();const auto size=readPoint(object["size"]);prepared.size=QSizeF(size.x(),size.y());
        if(object.contains("titleBlock")){require(object["titleBlock"].isObject(),"Cartiglio non valido.");const auto block=object["titleBlock"].toObject();require(block["visible"].isBool(),"Visibilita' del cartiglio non valida.");
            for(const auto &key:{"code","organization","author","date"})require(block[key].isString(),"Dati del cartiglio incompleti.");
            prepared.titleBlock={block["visible"].toBool(),block["code"].toString(),block["organization"].toString(),block["author"].toString(),block["date"].toString()};}
        if(object.contains("revisionTable")){
            require(object["revisionTable"].isObject(),"Tabella revisioni non valida.");const auto table=object["revisionTable"].toObject();require(table["visible"].isBool()&&table["notes"].isArray()&&table["notes"].toArray().size()<=1000,"Dati della tabella revisioni non validi.");prepared.revisionTable.visible=table["visible"].toBool();
            for(const auto &value:table["notes"].toArray()){require(value.isObject(),"Riga revisioni non valida.");const auto row=value.toObject();require(row["description"].isString()&&row["verifiedBy"].isString(),"Testo della revisione non valido.");prepared.revisionTable.notes.append({readId(row["checkinId"]),row["description"].toString(),row["verifiedBy"].toString()});}
        }
        ok(decodeDrawingDocument(QJsonDocument(object["drawing"].toObject()).toJson(QJsonDocument::Compact),prepared.drawing));
        require(!drawingNeedsAxisSchema(prepared.drawing)||object["schema"].toInt()>=5,"Quote sugli assi estranee allo schema del foglio.");
        if(schema==4||object.contains("axes")){
            require(object["axes"].isArray()&&object["axes"].toArray().size()<=10000,"Elenco assi non valido.");
            const auto refSchema=object["axisReferenceSchema"].toDouble(-1);require(refSchema==3||refSchema==4||refSchema==6||refSchema==7,"Schema degli agganci degli assi non valido.");
            auto support=object["drawing"].toObject();support["schema"]=refSchema;QJsonArray views,definitions;
            const auto originalViews=support["views"].toArray();
            for(qsizetype i=0;i<originalViews.size();++i){auto v=originalViews[i].toObject();v["kind"]=int(prepared.drawing.views[std::size_t(i)].kind);v["sectionReferences"]=prepared.drawing.views[std::size_t(i)].sectionReferences;
                if(refSchema>=4)v["sectionStyle"]=int(prepared.drawing.views[std::size_t(i)].sectionStyle);else v.remove("sectionStyle");views.append(v);}
            for(const auto &value:object["axes"].toArray()){
                require(value.isObject(),"Asse non valido.");const auto a=value.toObject();const double kind=a["kind"].toDouble(-1);require((kind==0||kind==1)&&a["name"].isString()&&a["extension"].isDouble()&&a["visible"].isBool()&&a["definition"].isObject(),"Dati dell'asse incompleti.");
                DrawingAxis axis;axis.kind=DrawingAxisKind(int(kind));axis.name=a["name"].toString();axis.extension=a["extension"].toDouble();axis.visible=a["visible"].toBool();prepared.axes.append(axis);definitions.append(a["definition"]);
            }
            support["views"]=views;support["dimensions"]=definitions;DrawingDocument references;ok(decodeDrawingDocument(QJsonDocument(support).toJson(QJsonDocument::Compact),references));
            for(qsizetype i=0;i<prepared.axes.size();++i){const auto &d=references.dimensions[std::size_t(i)];require(d.measure==DrawingMeasure::Distance&&d.angleSides==0&&d.decimals==2,"Record degli agganci dell'asse incoerente.");auto &axis=prepared.axes[i];axis.id=d.id;axis.viewId=d.viewId;axis.references=d.references;}
        }
        if(object.contains("annotations")){
            require(object["annotations"].isArray()&&object["annotations"].toArray().size()<=10000,"Elenco note non valido.");
            QJsonArray definitions;QVector<qsizetype> attached;
            for(const auto &value:object["annotations"].toArray()){
                require(value.isObject(),"Nota non valida.");const auto n=value.toObject();require(n["text"].isString()&&n["width"].isDouble()&&n["height"].isDouble(),"Dati della nota incompleti.");
                DrawingAnnotation note;note.id=readId(n["id"]);note.text=n["text"].toString();note.position=readPoint(n["position"]);note.width=n["width"].toDouble();note.height=n["height"].toDouble();
                if(n.contains("reference")){require(n["reference"].isObject(),"Aggancio del richiamo non valido.");definitions.append(n["reference"]);attached.append(prepared.annotations.size());}prepared.annotations.append(note);
            }
            require(!definitions.isEmpty()==object.contains("annotationReferenceSchema"),"Schema degli agganci delle note incoerente.");
            if(!definitions.isEmpty()){
                const double rs=object["annotationReferenceSchema"].toDouble(-1);require(rs==3||rs==4||rs==5||rs==6||rs==7,"Schema degli agganci delle note non supportato.");
                auto support=object["drawing"].toObject();support["schema"]=rs;support["dimensions"]=definitions;
                QJsonArray supportViews;const auto sourceViews=support["views"].toArray();
                for(qsizetype i=0;i<sourceViews.size();++i){auto v=sourceViews[i].toObject();const auto &original=prepared.drawing.views[std::size_t(i)];v["kind"]=int(original.kind);v["sectionReferences"]=original.sectionReferences;
                    require(rs>=4||original.sectionStyle==DrawingSectionStyle::Removed,"Schema degli agganci incompatibile con la sezione.");if(rs>=4)v["sectionStyle"]=int(original.sectionStyle);else v.remove("sectionStyle");supportViews.append(v);}
                support["views"]=supportViews;
                DrawingDocument refs;ok(decodeDrawingDocument(QJsonDocument(support).toJson(QJsonDocument::Compact),refs));
                for(qsizetype i=0;i<attached.size();++i){auto &n=prepared.annotations[attached[i]];const auto &d=refs.dimensions[std::size_t(i)];require(d.id==n.id&&d.measure==DrawingMeasure::SectionLength&&d.references.size()==1&&d.decimals==2&&d.angleSides==0,"Record degli agganci della nota incoerente.");n.viewId=d.viewId;n.reference=d.references[0];}
            }
        }
        require(object["views"].toArray().size()<=10000&&object["dimensions"].toArray().size()<=10000,"Troppi elementi nel foglio.");
        for(const auto &value:object["views"].toArray()){
            require(value.isObject(),"Vista del foglio non valida.");const auto view=value.toObject();DrawingViewLayout layout;
            layout.viewId=readId(view["id"]);require(view["title"].isString()&&view["features"].isArray(),"Vista senza titolo o sorgenti.");
            layout.title=view["title"].toString();layout.position=readPoint(view["position"]);
            if(view.contains("titleOffset")){require(schema>=8,"Posizione del titolo estranea allo schema dichiarato.");layout.titleOffset=readPoint(view["titleOffset"]);}
            require(!view.contains("showHiddenLines")||view["showHiddenLines"].isBool(),"Visibilita' delle linee nascoste non valida.");layout.showHiddenLines=view["showHiddenLines"].toBool(false);
            require(!view.contains("showMesh")||view["showMesh"].isBool(),"Visibilita' della mesh non valida.");layout.showMesh=view["showMesh"].toBool(false);
            for(const auto key:{"showCenterMarks","showCenterLines","showCylinderAxes"})require(!view.contains(key)||view[key].isBool(),"Visibilita' di assi/centri non valida.");
            layout.showCylinderAxes=view["showCylinderAxes"].toBool(false);
            layout.showCenterMarks=view["showCenterMarks"].toBool(false);layout.showCenterLines=view["showCenterLines"].toBool(false);
            const auto features=view["features"].toArray();require(features.size()<=10000,"Troppe sorgenti nella vista.");
            for(const auto &feature:features){require(feature.isString(),"ID sorgente non valido.");bool valid=false;const auto id=feature.toString().toULongLong(&valid);require(valid,"ID sorgente non valido.");layout.features.append(id);}
            if(view.contains("relation")){
                require(object["schema"].toInt()>=2&&view["relation"].isObject(),"Relazione della vista non supportata dal formato.");
                const auto r=view["relation"].toObject();layout.relation.parentId=readId(r["parent"]);
                const auto integer=[&](const char *key,int lo,int hi){const auto value=r[key];const double number=value.toDouble(-100);require(value.isDouble()&&number==std::floor(number)&&number>=lo&&number<=hi,"Enumerazione della relazione non valida.");return int(number);};
                layout.relation.kind=DrawingDerivedKind(integer("kind",1,2));layout.relation.side=DrawingProjectionSide(integer("side",0,3));layout.relation.method=DrawingProjectionMethod(integer("method",0,1));
                for(const auto key:{"aligned","reverse","hatch"})require(r[key].isBool(),"Opzione della relazione non valida.");
                require(r["label"].isString(),"Identificativo della sezione assente.");
                layout.relation.aligned=r["aligned"].toBool();layout.relation.reverse=r["reverse"].toBool();layout.relation.hatch=r["hatch"].toBool();layout.relation.label=r["label"].toString();
                layout.relation.cutStart=readPoint(r["cutStart"]);layout.relation.cutEnd=readPoint(r["cutEnd"]);
            }
            if(view.contains("sectionHatch")){require(object["schema"].toInt()>=3&&view["sectionHatch"].isBool()&&layout.relation.parentId.isNull(),"Tratteggio indipendente non valido.");layout.relation.hatch=view["sectionHatch"].toBool();}
            prepared.views.append(std::move(layout));
        }
        for(const auto &value:object["dimensions"].toArray()){require(value.isObject(),"Quota del foglio non valida.");const auto dimension=value.toObject();
            DrawingDimensionLayout layout{readId(dimension["id"]),readPoint(dimension["position"])};
            if(dimension.contains("presentation")){
                require(schema>=7&&dimension["presentation"].isObject(),"Presentazione della quota estranea allo schema dichiarato.");const auto p=dimension["presentation"].toObject();
                require(p.size()==6&&p["mode"].isDouble()&&p["mode"].toDouble()==p["mode"].toInt(-1)&&p["decimals"].isDouble()&&p["decimals"].toDouble()==p["decimals"].toInt(-1)&&p["upper"].isDouble()&&p["lower"].isDouble()&&p["prefix"].isString()&&p["suffix"].isString(),"Presentazione della quota incompleta.");
                layout.presentation={DrawingToleranceMode(p["mode"].toInt()),p["upper"].toDouble(),p["lower"].toDouble(),p["decimals"].toInt(),p["prefix"].toString(),p["suffix"].toString()};
            }prepared.dimensions.append(layout);}
        require(object["schema"].toInt()>=3||!drawingNeedsSectionSchema(prepared.drawing),"Sezione estesa non prevista dallo schema del foglio.");
        validate(prepared);sheet=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
