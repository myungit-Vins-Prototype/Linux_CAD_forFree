#include "cad_drawing_release.h"
#include <stdexcept>
#include "fk_cancel.h"
namespace ForgeCad {
namespace {
void releaseOk(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
bool sameVersion(const DocumentVersion &a,const DocumentVersion &b){return a.documentId==b.documentId&&a.checkinId==b.checkinId;}
void validateReleasePayloads(RevisionStore &store,const RevisionReleaseManifest &manifest){
    DocumentState model;releaseOk(readNativeRevision(store,manifest.model.version,model));QByteArray definition;releaseOk(drawingModelDefinitionHash(model,definition));
    for(const auto &member:manifest.drawings){Kernel::cancellationPoint();QByteArray bytes;releaseOk(store.read(member.version,bytes));DrawingFile drawing;releaseOk(decodeDrawingFile(bytes,drawing));
        if(!sameVersion(drawing.sheet.drawing.version,member.version)||!sameVersion(drawing.sheet.drawing.model,manifest.model.version)||drawing.modelArchiveId!=manifest.archiveId||drawing.modelDefinitionHash!=definition)
            throw std::runtime_error("Il contenuto della tavola non corrisponde al modello e alle versioni del rilascio.");
    }
}
}
QString inspectDrawingRelease(RevisionStore &store,const RevisionReleaseRequest &request,RevisionReleaseManifest &manifest){
    try{RevisionReleaseManifest prepared;releaseOk(store.inspectRelease(request,prepared));validateReleasePayloads(store,prepared);manifest=std::move(prepared);return {};}catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString publishDrawingRelease(RevisionStore &store,const RevisionReleaseRequest &request,RevisionReleaseManifest &manifest){
    try{RevisionReleaseManifest prepared;releaseOk(inspectDrawingRelease(store,request,prepared));Kernel::cancellationPoint();return store.publishRelease(request,manifest);}catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString readDrawingRelease(RevisionStore &store,const QUuid &id,RevisionReleaseManifest &manifest){
    try{RevisionReleaseManifest prepared;releaseOk(store.readRelease(id,prepared));validateReleasePayloads(store,prepared);manifest=std::move(prepared);return {};}catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
}
