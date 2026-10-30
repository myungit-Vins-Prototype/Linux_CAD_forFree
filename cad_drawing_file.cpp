#include "cad_drawing_file.h"
#include "cad_feature_build.h"
#include "cad_element_names.h"
#include "fk_cancel.h"
#include "cad_feature_dependencies.h"
#include "cad_model_history.h"
#include "cad_native_revisions.h"
#include "cad_workspace.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <QSaveFile>
#include <QSet>
#include <stdexcept>

namespace ForgeCad {
namespace {
void require(bool condition,const QString &message){if(!condition)throw std::runtime_error(message.toStdString());}
void ok(const QString &error){require(error.isEmpty(),error);}
QString text(const QUuid &id){return id.isNull()?QString():id.toString(QUuid::WithoutBraces);}
QUuid optionalUuid(const QJsonValue &value){
    require(value.isString(),QStringLiteral("Identita' della revisione della tavola assente."));
    const auto string=value.toString();if(string.isEmpty())return {};
    const QUuid id(string);require(!id.isNull(),QStringLiteral("Identita' della revisione della tavola non valida."));return id;
}
void validate(const DrawingFile &drawing){
    require(drawingSheetCount(drawing)<=64&&(drawing.sheetIds.isEmpty()?drawing.otherSheets.isEmpty():drawing.sheetIds.size()==drawingSheetCount(drawing)),QStringLiteral("Numero o identita' dei fogli non validi (massimo 64)."));
    QSet<QUuid> ids;
    for(const auto &id:drawing.sheetIds){require(!id.isNull()&&!ids.contains(id),QStringLiteral("Identita' del foglio nulla o duplicata."));ids.insert(id);}
    const auto same=[](const DocumentVersion &a,const DocumentVersion &b){return a.documentId==b.documentId&&a.checkinId==b.checkinId;};
    for(int i=0;i<drawingSheetCount(drawing);++i){const auto &sheet=drawingSheetAt(drawing,i);
        require(same(sheet.drawing.version,drawing.sheet.drawing.version)&&same(sheet.drawing.model,drawing.sheet.drawing.model),QStringLiteral("I fogli devono condividere revisione della tavola e modello."));
        const auto entity=[&](const QUuid &id){require(!ids.contains(id),QStringLiteral("Identita' ripetuta tra fogli o entita'."));ids.insert(id);};
        for(const auto &v:sheet.drawing.views)entity(v.id);
        for(const auto &d:sheet.drawing.dimensions)entity(d.id);
        for(const auto &a:sheet.axes)entity(a.id);
        for(const auto &n:sheet.annotations)entity(n.id);
    }
    require(!drawing.modelPath.trimmed().isEmpty()&&drawing.modelPath.size()<=4096&&!drawing.modelPath.contains(QChar(0)),QStringLiteral("Riferimento al modello non valido."));
    require(drawing.modelDefinitionHash.size()==32,QStringLiteral("Impronta della definizione del modello non valida."));
    require(drawing.modelArchivePath.size()<=4096&&!drawing.modelArchivePath.contains(QChar(0))
            &&drawing.modelArchiveId.isNull()==drawing.modelArchivePath.isEmpty(),QStringLiteral("Riferimento all'archivio del modello non valido."));
    require(drawing.sheet.drawing.version.checkinId.isNull()||drawing.sheet.drawing.version.checkinId!=drawing.baseCheckinId,
            QStringLiteral("Identita' del check-in della tavola incoerente."));
}
bool sameVersion(const DocumentVersion &a,const DocumentVersion &b){return a.documentId==b.documentId&&a.checkinId==b.checkinId;}
QString resolve(const QString &drawingPath,const QString &reference){return QDir(QFileInfo(drawingPath).absolutePath()).absoluteFilePath(reference);}
}
int drawingSheetCount(const DrawingFile &drawing){return 1+int(drawing.otherSheets.size());}
DrawingSheet &drawingSheetAt(DrawingFile &drawing,int index){return index==0?drawing.sheet:drawing.otherSheets[index-1];}
const DrawingSheet &drawingSheetAt(const DrawingFile &drawing,int index){return index==0?drawing.sheet:drawing.otherSheets.at(index-1);}
QUuid drawingSheetId(const DrawingFile &drawing,int index){return drawing.sheetIds.isEmpty()?drawing.sheet.drawing.version.documentId:drawing.sheetIds.at(index);}
void synchronizeDrawingFileVersions(DrawingFile &drawing){for(auto &sheet:drawing.otherSheets){sheet.drawing.version=drawing.sheet.drawing.version;sheet.drawing.model=drawing.sheet.drawing.model;}}
QString cloneDrawingSheet(const DrawingSheet &source,DrawingSheet &copy){
    QByteArray bytes;auto error=encodeDrawingSheet(source,bytes);if(!error.isEmpty())return error;
    QHash<QString,QString> ids;const auto add=[&](const QUuid &id){ids.insert(text(id),text(QUuid::createUuid()));};
    for(const auto &v:source.drawing.views)add(v.id);
        for(const auto &d:source.drawing.dimensions)add(d.id);
        for(const auto &a:source.axes)add(a.id);
        for(const auto &n:source.annotations)add(n.id);
    for(auto it=source.dimensionNumbering.assignments.cbegin();it!=source.dimensionNumbering.assignments.cend();++it)if(!ids.contains(text(it.key())))add(it.key());
    const QSet<QString> identityKeys{QStringLiteral("firstDimension"),QStringLiteral("id"),QStringLiteral("viewId"),QStringLiteral("parent"),QStringLiteral("axisId"),QStringLiteral("datums")};
    std::function<QJsonValue(QJsonValue,const QString &)> remap=[&](QJsonValue value,const QString &key)->QJsonValue{
        if(value.isString()&&identityKeys.contains(key)){const auto found=ids.constFind(value.toString());return found==ids.cend()?value:QJsonValue(*found);}
        if(value.isArray()){auto array=value.toArray();for(int i=0;i<array.size();++i)array[i]=remap(array[i],key);return array;}
        if(value.isObject()){auto object=value.toObject();for(auto it=object.begin();it!=object.end();++it)it.value()=remap(it.value(),it.key());return object;}return value;
    };
    return decodeDrawingSheet(QJsonDocument(remap(QJsonDocument::fromJson(bytes).object(),QString()).toObject()).toJson(QJsonDocument::Compact),copy);
}
QString encodeDrawingFile(const DrawingFile &drawing,QByteArray &bytes){
    try{
        validate(drawing);const bool multiple=!drawing.sheetIds.isEmpty();
        QJsonObject object{{"format","ForgeCAD.Drawing"},{"schema",multiple?2:1},
            {"modelPath",drawing.modelPath},{"modelDefinitionSha256",QString::fromLatin1(drawing.modelDefinitionHash.toHex())},
            {"baseCheckinId",text(drawing.baseCheckinId)},{"modelArchiveId",text(drawing.modelArchiveId)},{"modelArchivePath",drawing.modelArchivePath}};
        QJsonArray sheets;
        for(int i=0;i<drawingSheetCount(drawing);++i){QByteArray sheet;ok(encodeDrawingSheet(drawingSheetAt(drawing,i),sheet));const auto value=QJsonDocument::fromJson(sheet).object();
            if(multiple)sheets.append(QJsonObject{{"id",text(drawingSheetId(drawing,i))},{"sheet",value}});else object["sheet"]=value;}
        if(multiple)object["sheets"]=sheets;
        auto prepared=QJsonDocument(object).toJson(QJsonDocument::Compact);
        require(prepared.size()<=9*1024*1024,QStringLiteral("Tavola oltre i limiti del prototipo."));bytes=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString decodeDrawingFile(const QByteArray &bytes,DrawingFile &drawing){
    try{
        require(!bytes.isEmpty()&&bytes.size()<=9*1024*1024,QStringLiteral("Dimensione del file tavola non valida."));
        QJsonParseError parse;const auto json=QJsonDocument::fromJson(bytes,&parse);const auto object=json.object();const auto schema=object["schema"].toDouble(-1);
        require(parse.error==QJsonParseError::NoError&&json.isObject()&&object["format"].toString()==QLatin1String("ForgeCAD.Drawing")&&(schema==1||schema==2),QStringLiteral("Formato del file tavola non valido o non supportato."));
        require(object["modelPath"].isString()&&object["modelDefinitionSha256"].isString()&&object["modelArchivePath"].isString(),QStringLiteral("File tavola incompleto."));
        DrawingFile prepared;prepared.modelPath=object["modelPath"].toString();prepared.modelArchivePath=object["modelArchivePath"].toString();
        prepared.baseCheckinId=optionalUuid(object["baseCheckinId"]);prepared.modelArchiveId=optionalUuid(object["modelArchiveId"]);
        const auto digest=object["modelDefinitionSha256"].toString().toLatin1();prepared.modelDefinitionHash=QByteArray::fromHex(digest);
        require(digest==prepared.modelDefinitionHash.toHex(),QStringLiteral("Impronta del modello malformata."));
        if(schema==1){require(object["sheet"].isObject()&&!object.contains("sheets"),QStringLiteral("Foglio legacy assente o schema multifoglio declassato."));ok(decodeDrawingSheet(QJsonDocument(object["sheet"].toObject()).toJson(QJsonDocument::Compact),prepared.sheet));}
        else{require(!object.contains("sheet")&&object["sheets"].isArray(),QStringLiteral("Elenco dei fogli assente o ambiguo."));const auto sheets=object["sheets"].toArray();require(!sheets.isEmpty()&&sheets.size()<=64,QStringLiteral("La tavola deve contenere da 1 a 64 fogli."));
            for(int i=0;i<sheets.size();++i){require(sheets[i].isObject(),QStringLiteral("Voce del foglio non valida."));const auto entry=sheets[i].toObject();require(entry.size()==2&&entry["sheet"].isObject(),QStringLiteral("Dati del foglio incompleti o sconosciuti."));
                const auto id=optionalUuid(entry["id"]);require(!id.isNull(),QStringLiteral("Identita' del foglio assente."));prepared.sheetIds.append(id);DrawingSheet sheet;ok(decodeDrawingSheet(QJsonDocument(entry["sheet"].toObject()).toJson(QJsonDocument::Compact),sheet));if(i==0)prepared.sheet=std::move(sheet);else prepared.otherSheets.append(std::move(sheet));}}
        validate(prepared);drawing=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString loadDrawingFile(const QString &path,DrawingFile &drawing){
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>9*1024*1024)return QStringLiteral("Impossibile leggere il file tavola o file oltre i limiti.");
    return decodeDrawingFile(file.readAll(),drawing);
}
QString saveDrawingFile(const QString &path,const DrawingFile &drawing,const DocumentFileLock *lock){
    if(!drawing.sheet.drawing.version.checkinId.isNull())return QStringLiteral("Tavola archiviata: usare Salva con nome per una copia indipendente.");
    std::unique_ptr<DocumentFileLock> temporary;
    if(!lock){temporary=std::make_unique<DocumentFileLock>(path);const auto error=temporary->acquire();if(!error.isEmpty())return error;lock=temporary.get();}
    if(!lock->owns(path))return QStringLiteral("La tavola non ha piu' un blocco esclusivo valido.");
    QByteArray bytes;const auto error=encodeDrawingFile(drawing,bytes);if(!error.isEmpty())return error;
    QSaveFile file(lock->path());if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return QStringLiteral("Impossibile scrivere il file tavola.");
    WorkspacePaths workspace;const auto workspaceError=ensureDocumentWorkspace(lock->path(),workspace);if(!workspaceError.isEmpty())return workspaceError;
    if(!lock->owns(path))return QStringLiteral("Blocco della tavola perso durante il salvataggio.");
    if(!file.commit())return QStringLiteral("Pubblicazione del file tavola fallita.");
    return {};
}
QString drawingModelDefinitionHash(const DocumentState &document,QByteArray &hash){
    auto normalized=document;normalized.version.checkinId={};normalized.baseCheckinId={};normalized.orientation=AxesOrientation{};
    normalized.lengthUnit=LengthUnit::Millimeter;QByteArray definition;const auto error=encodeDocument(normalized,definition,false);if(!error.isEmpty())return error;
    hash=QCryptographicHash::hash(definition,QCryptographicHash::Sha256);return {};
}
QString drawingModelDefinitionMatches(const DocumentState &document,const QByteArray &expected,bool &matches){
    QByteArray current;const auto error=drawingModelDefinitionHash(document,current);if(!error.isEmpty())return error;
    if(current==expected){matches=true;return {};}
    // I vecchi hash non contenevano questi nomi. Un riferimento nuovo o
    // modificato deve restare una modifica, anche con uguali indici e punti.
    for(const auto &sketch:document.sketches){
        if(sketch.support&&!sketch.support->point.name.isEmpty()){matches=false;return {};}
        for(const auto &constraint:sketch.geometricConstraints)
            if(constraint.pierce&&!constraint.pierce->surface.point.name.isEmpty()){matches=false;return {};}
    }
    auto normalized=document;normalized.version.checkinId={};normalized.baseCheckinId={};normalized.orientation=AxesOrientation{};
    normalized.lengthUnit=LengthUnit::Millimeter;QByteArray legacy;
    const auto legacyError=encodeDocumentDefinition53ForHash(normalized,legacy);if(!legacyError.isEmpty())return legacyError;
    matches=QCryptographicHash::hash(legacy,QCryptographicHash::Sha256)==expected;return {};
}
QString prepareDrawingEditorModel(DocumentState &document,DrawingEditorModel &model){
    try{
        normalizeModelHistory(document);
        QSet<quint64> cached;
        for(int index=0;index<document.extrusions.size();++index){auto &feature=document.extrusions[index];
            if(feature.cachedGeometry&&feature.forgeBody)cached.insert(feature.featureId);
            if(!feature.forgeBody)buildGeometry(feature,index,document.sketches,document.extrusions);
            else registerFeatureGenealogy(feature,index,document.sketches,document.extrusions);}
        normalizeModelHistory(document);
        QHash<quint64,quint64> parents;
        for(int index=0;index<document.extrusions.size();++index){const auto &feature=document.extrusions[index];
            if(!feature.modelBodyId||feature.suppressed||!feature.error.isEmpty()||!feature.forgeBody)continue;
            for(int operand:hiddenOperands(feature,document.extrusions))if(operand>=0&&operand<index){const auto id=document.extrusions[operand].modelBodyId;
                if(id&&id!=feature.modelBodyId&&!parents.contains(id))parents[id]=feature.modelBodyId;}}
        const auto root=[&](quint64 id){QSet<quint64> seen;while(parents.contains(id)){require(!seen.contains(id),QStringLiteral("Dipendenze cicliche dei corpi."));seen.insert(id);id=parents.value(id);}return id;};
        DrawingEditorModel prepared;prepared.snapshot.version=document.version;
        const auto &o=document.orientation;
        prepared.frontFrame=Kernel::Frame3::fromAxes(Kernel::Vec3(),Kernel::Vec3(o.right[0],o.right[1],o.right[2]),Kernel::Vec3(o.up[0],o.up[1],o.up[2]),Kernel::Vec3(o.toward[0],o.toward[1],o.toward[2]));QVector<quint64> ids;QHash<quint64,quint64> tips;
        for(const auto &body:document.modelBodies){if(root(body.id)!=body.id)continue;const int index=modelBodyTipIndex(document,body.id);
            if(index<0||!isShapeBody(document.extrusions[index])||isExposedPatternAggregate(index,document.extrusions))continue;
            const auto &feature=document.extrusions[index];
            ids.append(feature.featureId);tips[body.id]=feature.featureId;
            prepared.choices.append({feature.featureId,body.name.isEmpty()?feature.name:body.name});}
        if(!ids.isEmpty())ok(drawingSnapshotOfDocument(document,ids,prepared.snapshot));
        for(auto &source:prepared.snapshot.bodies)for(const auto &feature:document.extrusions)if(feature.featureId==source.featureId){
            QColor color=feature.solid?QColor::fromRgbF(.25,.65,.90):QColor::fromRgbF(.20,.80,.95);
            for(const auto &body:document.modelBodies)if(body.id==feature.modelBodyId&&body.meshColor.isValid()){color=body.meshColor;break;}
            source.color=color.rgba();break;
        }
        for(const auto &feature:document.extrusions){const auto tip=tips.value(root(feature.modelBodyId),0);if(tip)prepared.replacements[feature.featureId]=tip;}
        for(const auto &source:prepared.snapshot.bodies)if(cached.contains(source.featureId))
            prepared.legacySnapshotNames.insert(source.featureId,legacyDrawingElementNames(*source.body));
        model=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
int upgradeDrawingFileReferences(DrawingFile &drawing,const DrawingEditorModel &model){
    if(!sameVersion(drawing.sheet.drawing.model,model.snapshot.version)||model.legacySnapshotNames.isEmpty())return 0;
    auto legacyModel=model.snapshot;
    for(auto &source:legacyModel.bodies){const auto names=model.legacySnapshotNames.value(source.featureId);
        if(names&&names->body==source.body.get())source.names=names;}
    const auto rename=[&](quint64 feature,Kernel::EntityKind kind,QString &name){
        if(name.isEmpty())return false;
        const auto legacy=model.legacySnapshotNames.value(feature);
        const auto source=std::find_if(model.snapshot.bodies.begin(),model.snapshot.bodies.end(),
            [&](const auto &body){return body.featureId==feature;});
        if(!legacy||source==model.snapshot.bodies.end()||legacy->body!=source->body.get())return false;
        if(!source->names->find(kind,name.toStdString()).empty())return false;
        const auto found=legacy->find(kind,name.toStdString());
        if(found.empty())return false;
        const auto replacement=source->names->primary(kind,found.front());
        if(replacement.empty()||!std::all_of(found.begin(),found.end(),[&](int index){
            return source->names->primary(kind,index)==replacement;}))return false;
        name=QString::fromStdString(replacement);return true;
    };
    int changed=0;
    for(int i=0;i<drawingSheetCount(drawing);++i){auto &sheet=drawingSheetAt(drawing,i);
        std::map<QUuid,std::vector<DrawingCurve>> curves;
        const auto attachment=[&](const QUuid &viewId,DrawingAttachment &ref){
            if(!ref.axisId.isNull()||drawingDatum(ref.role))return;
            auto candidate=ref;const auto feature=ref.curve.featureId;bool renamed=false;
            renamed|=rename(feature,Kernel::EntityKind::Face,candidate.curve.faceName);
            renamed|=rename(feature,Kernel::EntityKind::Edge,candidate.curve.edgeName);
            renamed|=rename(feature,Kernel::EntityKind::Face,candidate.supportFace);
            renamed|=rename(feature,Kernel::EntityKind::Edge,candidate.boundaryName);
            renamed|=rename(feature,Kernel::EntityKind::Vertex,candidate.vertexName);
            if(!renamed)return;
            const auto source=std::find_if(model.snapshot.bodies.begin(),model.snapshot.bodies.end(),
                [&](const auto &body){return body.featureId==feature;});
            // Il lato UV era definito su una faccia legacy univoca. Se il nome
            // genealogico e' condiviso, decide l'ancoraggio geometrico verificato.
            if(!candidate.supportFace.isEmpty()&&source->names->find(Kernel::EntityKind::Face,candidate.supportFace.toStdString()).size()!=1){
                candidate.supportFace.clear();candidate.curveSide=0;candidate.anchorSide=0;
            }
            const auto view=std::find_if(sheet.drawing.views.begin(),sheet.drawing.views.end(),[&](const auto &v){return v.id==viewId;});
            if(view==sheet.drawing.views.end())return;
            auto found=curves.find(viewId);
            if(found==curves.end()){
                std::vector<DrawingCurve> prepared;
                if(!drawingViewCurves(model.snapshot,*view,prepared).isEmpty())return;
                found=curves.emplace(viewId,std::move(prepared)).first;
            }
            const auto before=resolveDrawingAttachment(*view,legacyModel,found->second,ref);
            const auto same=[&](const DrawingResolvedAttachment &after){
                return before.status==DrawingDimensionStatus::Ready&&after.status==DrawingDimensionStatus::Ready
                    &&before.curve==after.curve&&before.isPoint==after.isPoint&&before.isLine==after.isLine
                    &&Kernel::distance(before.point,after.point)<=view->tolerance
                    &&Kernel::distance(before.first,after.first)<=view->tolerance
                    &&Kernel::distance(before.second,after.second)<=view->tolerance;
            };
            auto after=resolveDrawingAttachment(*view,model.snapshot,found->second,candidate);
            // Un bordo rinominato puo' identificare entrambi gli estremi. Il
            // creatore di agganci aggiunge anche il lato UV del bordo: usarlo
            // solo se ritrova l'esatto punto legacy, senza scegliere il piu' vicino.
            if(!same(after)&&before.status==DrawingDimensionStatus::Ready&&before.curve
                &&(ref.role==DrawingPointRole::Start||ref.role==DrawingPointRole::End)){
                for(auto role:{DrawingPointRole::Start,DrawingPointRole::End}){
                    try{
                        auto anchored=drawingAttachment(*view,*before.curve,model.snapshot,role);
                        auto resolved=resolveDrawingAttachment(*view,model.snapshot,found->second,anchored);
                        if(same(resolved)){candidate=std::move(anchored);after=resolved;break;}
                    }catch(const Kernel::OperationCancelled &){throw;}
                    catch(const std::bad_alloc &){throw;}
                    catch(...){ /* Aggancio ambiguo: resta il riferimento originale. */ }
                }
            }
            // Last resort for unchanged geometry: constrain a shared topological
            // endpoint by its exact saved position, never by proximity ranking.
            if(!same(after)&&before.status==DrawingDimensionStatus::Ready&&before.curve
                &&ref.curve.kind==DrawingCurveKind::Section&&!view->step&&!view->bend
                &&(ref.role==DrawingPointRole::Start||ref.role==DrawingPointRole::End)){
                candidate.endpointWitness=view->plane.toGlobal(Kernel::Vec3(before.point.x(),before.point.y(),0));
                after=resolveDrawingAttachment(*view,model.snapshot,found->second,candidate);
            }
            if(!same(after))return;
            ref=std::move(candidate);++changed;
        };
        for(auto &dimension:sheet.drawing.dimensions){
            if(dimension.references.empty()){
                DrawingAttachment ref;ref.curve=dimension.section;ref.role=DrawingPointRole::Curve;
                attachment(dimension.viewId,ref);dimension.section=ref.curve;
            }else{
                for(auto &ref:dimension.references)attachment(dimension.viewId,ref);
                const auto primary=std::find_if(dimension.references.begin(),dimension.references.end(),[](const auto &ref){return ref.axisId.isNull()&&!drawingDatum(ref.role);});
                if(primary!=dimension.references.end())dimension.section=primary->curve;
            }
        }
        for(auto &axis:sheet.axes)for(auto &ref:axis.references)attachment(axis.viewId,ref);
        for(auto &note:sheet.annotations)if(note.reference)attachment(note.viewId,*note.reference);
    }
    return changed;
}
QString loadDrawingModel(const QString &drawingPath,const DrawingFile &drawing,DocumentState &model,QString &resolvedPath,bool &definitionChanged,const QString &overridePath){
    try{
        const auto path=canonicalDocumentPath(overridePath.isEmpty()?resolve(drawingPath,drawing.modelPath):overridePath);
        DocumentState prepared;auto error=loadDocumentFile(path,prepared);
        const auto expected=drawing.sheet.drawing.model;
        if(expected.checkinId.isNull()){
            ok(error);require(prepared.version.documentId==expected.documentId&&prepared.version.checkinId.isNull(),QStringLiteral("Il file scelto non e' la copia di lavoro del modello richiesto dalla tavola."));
        }else if(!error.isEmpty()||!sameVersion(prepared.version,expected)){
            require(!drawing.modelArchiveId.isNull()&&!drawing.modelArchivePath.isEmpty(),QStringLiteral("La tavola richiede un check-in preciso del modello; indicare il relativo snapshot o ripristinare l'archivio."));
            RevisionStore store(resolve(drawingPath,drawing.modelArchivePath));QUuid archiveId;ok(store.archiveIdentity(archiveId));
            require(archiveId==drawing.modelArchiveId,QStringLiteral("Archivio del modello diverso da quello richiesto dalla tavola."));
            ok(readNativeRevision(store,expected,prepared));
        }
        bool matches=false;ok(drawingModelDefinitionMatches(prepared,drawing.modelDefinitionHash,matches));const bool changed=!matches;
        require(expected.checkinId.isNull()||!changed,QStringLiteral("Lo snapshot archiviato non corrisponde all'impronta della tavola."));
        model=std::move(prepared);resolvedPath=path;definitionChanged=changed;return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
void forkDrawingIdentity(DrawingFile &drawing){drawing.sheet.drawing.version={QUuid::createUuid(),{}};drawing.baseCheckinId={};synchronizeDrawingFileVersions(drawing);}
}
