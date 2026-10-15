#include "cad_drawing_file.h"
#include "cad_feature_build.h"
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
QString encodeDrawingFile(const DrawingFile &drawing,QByteArray &bytes){
    try{
        validate(drawing);QByteArray sheet;ok(encodeDrawingSheet(drawing.sheet,sheet));
        auto prepared=QJsonDocument(QJsonObject{{"format","ForgeCAD.Drawing"},{"schema",1},
            {"modelPath",drawing.modelPath},{"modelDefinitionSha256",QString::fromLatin1(drawing.modelDefinitionHash.toHex())},
            {"baseCheckinId",text(drawing.baseCheckinId)},{"modelArchiveId",text(drawing.modelArchiveId)},
            {"modelArchivePath",drawing.modelArchivePath},{"sheet",QJsonDocument::fromJson(sheet).object()}}).toJson(QJsonDocument::Compact);
        require(prepared.size()<=9*1024*1024,QStringLiteral("Tavola oltre i limiti del prototipo."));bytes=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString decodeDrawingFile(const QByteArray &bytes,DrawingFile &drawing){
    try{
        require(!bytes.isEmpty()&&bytes.size()<=9*1024*1024,QStringLiteral("Dimensione del file tavola non valida."));
        QJsonParseError parse;const auto json=QJsonDocument::fromJson(bytes,&parse);const auto object=json.object();
        require(parse.error==QJsonParseError::NoError&&json.isObject()&&object["format"].toString()==QLatin1String("ForgeCAD.Drawing")
                &&object["schema"].toInt(-1)==1,QStringLiteral("Formato del file tavola non valido o non supportato."));
        require(object["modelPath"].isString()&&object["modelDefinitionSha256"].isString()&&object["modelArchivePath"].isString()&&object["sheet"].isObject(),QStringLiteral("File tavola incompleto."));
        DrawingFile prepared;prepared.modelPath=object["modelPath"].toString();prepared.modelArchivePath=object["modelArchivePath"].toString();
        prepared.baseCheckinId=optionalUuid(object["baseCheckinId"]);prepared.modelArchiveId=optionalUuid(object["modelArchiveId"]);
        const auto digest=object["modelDefinitionSha256"].toString().toLatin1();
        prepared.modelDefinitionHash=QByteArray::fromHex(digest);
        require(digest==prepared.modelDefinitionHash.toHex(),QStringLiteral("Impronta del modello malformata."));
        ok(decodeDrawingSheet(QJsonDocument(object["sheet"].toObject()).toJson(QJsonDocument::Compact),prepared.sheet));validate(prepared);
        drawing=std::move(prepared);return {};
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
QString prepareDrawingEditorModel(DocumentState &document,DrawingEditorModel &model){
    try{
        normalizeModelHistory(document);
        for(int index=0;index<document.extrusions.size();++index){auto &feature=document.extrusions[index];
            if(!feature.forgeBody)buildGeometry(feature,index,document.sketches,document.extrusions);}
        normalizeModelHistory(document);
        QHash<quint64,quint64> parents;
        for(int index=0;index<document.extrusions.size();++index){const auto &feature=document.extrusions[index];
            if(!feature.modelBodyId||feature.suppressed||!feature.error.isEmpty()||!feature.forgeBody)continue;
            for(int operand:hiddenOperands(feature))if(operand>=0&&operand<index){const auto id=document.extrusions[operand].modelBodyId;
                if(id&&id!=feature.modelBodyId&&!parents.contains(id))parents[id]=feature.modelBodyId;}}
        const auto root=[&](quint64 id){QSet<quint64> seen;while(parents.contains(id)){require(!seen.contains(id),QStringLiteral("Dipendenze cicliche dei corpi."));seen.insert(id);id=parents.value(id);}return id;};
        DrawingEditorModel prepared;prepared.snapshot.version=document.version;QVector<quint64> ids;QHash<quint64,quint64> tips;
        for(const auto &body:document.modelBodies){if(root(body.id)!=body.id)continue;const int index=modelBodyTipIndex(document,body.id);
            if(index<0||!isShapeBody(document.extrusions[index]))continue;
            const auto &feature=document.extrusions[index];
            ids.append(feature.featureId);tips[body.id]=feature.featureId;
            prepared.choices.append({feature.featureId,body.name.isEmpty()?feature.name:body.name});}
        if(!ids.isEmpty())ok(drawingSnapshotOfDocument(document,ids,prepared.snapshot));
        for(const auto &feature:document.extrusions){const auto tip=tips.value(root(feature.modelBodyId),0);if(tip)prepared.replacements[feature.featureId]=tip;}
        model=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
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
        QByteArray digest;ok(drawingModelDefinitionHash(prepared,digest));const bool changed=digest!=drawing.modelDefinitionHash;
        require(expected.checkinId.isNull()||!changed,QStringLiteral("Lo snapshot archiviato non corrisponde all'impronta della tavola."));
        model=std::move(prepared);resolvedPath=path;definitionChanged=changed;return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
void forkDrawingIdentity(DrawingFile &drawing){drawing.sheet.drawing.version={QUuid::createUuid(),{}};drawing.baseCheckinId={};}
}
