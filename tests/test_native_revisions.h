#pragma once
#include "../cad_native_revisions.h"
#include "../cad_history.h"
#include "../cad_workspace.h"
#include "fk_body_io.h"
#include "fk_curve_algo.h"

namespace {
QByteArray nativeFixture(const QByteArray &payload, quint16 version, const QByteArray &cache = {}) {
    const auto compressed = qCompress(payload,9);
    QByteArray result; QDataStream out(&result,QIODevice::WriteOnly);
    out.writeRawData("FCAD",4); out << version << quint8(1) << quint32(compressed.size());
    out.writeRawData(compressed.constData(),int(compressed.size()));
    out.writeRawData(cache.constData(),int(cache.size())); return result;
}
QByteArray nativeLegacy49Fixture(const QByteArray &current) {
    QDataStream header(current); header.skipRawData(7); quint32 size=0; header >> size;
    const auto payload = qUncompress(current.mid(11,size));
    require(payload.size() >= 48,"metadati nativi da rimuovere per fixture 49");
    const auto legacyPayload = payload.left(payload.size()-48);
    const auto cache = qUncompress(current.mid(11+size));
    QDataStream in(cache); QByteArray sourceHash,payloadHash; in >> sourceHash >> payloadHash;
    QByteArray restoredCache; QDataStream out(&restoredCache,QIODevice::WriteOnly);
    out << sourceHash << QCryptographicHash::hash(legacyPayload,QCryptographicHash::Sha256);
    const auto rest = cache.mid(in.device()->pos()); out.writeRawData(rest.constData(),int(rest.size()));
    return nativeFixture(legacyPayload,49,qCompress(restoredCache,9));
}
void nativeOk(const QString &error) { if (!error.isEmpty()) throw std::runtime_error(error.toStdString()); }
void nativeRevisionTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    QTemporaryDir temporary; require(temporary.isValid(),"directory revisioni native");
    DocumentState initial; ExtrusionObject root; root.feature=BodyFeature::Primitive; root.featureId=71001;
    root.primitive.kind=PrimitiveKind::Box;root.primitive.size[0]=10;root.primitive.size[1]=6;root.primitive.size[2]=4; initial.extrusions={root};
    CadViewport viewport; viewport.loadDocument(initial);
    auto working=viewport.currentDocument(); const auto identity=working.version.documentId;
    require(identity==initial.version.documentId && working.extrusions[0].forgeBody,"identita' e geometria del documento aperto");
    require(viewport.currentDocument().version.documentId==identity,"istantanee senza nuovi UUID");
    const auto geometry=writeBodyBinary(*working.extrusions[0].forgeBody);
    const auto originalNames=elementNamesOf(*working.extrusions[0].forgeBody);
    viewport.setHistoryPosition(0);viewport.setHistoryPosition(1);
    require(viewport.currentDocument().version.documentId==identity,"navigazione non cambia documento");
    QByteArray encoded;nativeOk(encodeDocument(working,encoded,true));
    QDataStream header(encoded);header.skipRawData(4);quint16 format=0;quint8 compression=0;quint32 payloadSize=0;
    header>>format>>compression>>payloadSize;
    require(format==50 && compression==1,"nuovo formato nativo 50");
    const auto cache=qUncompress(encoded.mid(11+payloadSize));QDataStream cacheIn(cache);
    QByteArray sourceHash,payloadHash;quint32 tag=0,cacheVersion=0;cacheIn>>sourceHash>>payloadHash>>tag>>cacheVersion;
    require(cacheVersion==7,"cache geometrica resta 7");
    const auto path=temporary.filePath("lavoro.prt");
    WorkspacePaths workspace;nativeOk(locateDocumentWorkspace(path,workspace));
    require(!QFileInfo::exists(workspace.metadata),"nessun workspace prima del primo salvataggio");
    nativeOk(saveDocumentFile(path,working,true));nativeOk(locateDocumentWorkspace(path,workspace));
    require(!workspace.workspaceId.isNull() && QFileInfo::exists(QDir(workspace.revisions).filePath("catalog.json")),"primo Salva prepara workspace nascosto");
    const auto workspaceMarker=QDir(workspace.metadata).filePath("workspace.json");QByteArray markerBytes,fileBytes;
    {QFile f(workspaceMarker);require(f.open(QIODevice::ReadOnly),"leggi identita workspace");markerBytes=f.readAll();}
    {QFile f(path);require(f.open(QIODevice::ReadOnly),"leggi file salvato");fileBytes=f.readAll();}
    {QFile f(workspaceMarker);require(f.open(QIODevice::WriteOnly),"corrompi marker fixture");f.write("{}");}
    auto rejectedSave=working;rejectedSave.lengthUnit=LengthUnit::Inch;
    require(!saveDocumentFile(path,rejectedSave,false).isEmpty(),"workspace non valido impedisce Salva");
    {QFile f(path);require(f.open(QIODevice::ReadOnly) && f.readAll()==fileBytes,"errore workspace conserva documento precedente");}
    {QFile f(workspaceMarker);require(f.open(QIODevice::WriteOnly)&&f.write(markerBytes)==markerBytes.size(),"ripristina marker fixture");}
    DocumentState loaded;nativeOk(loadDocumentFile(path,loaded));
    require(loaded.version.documentId==identity && loaded.version.checkinId.isNull() && loaded.baseCheckinId.isNull(),"identita' nativa dopo riapertura");
    require(loaded.extrusions[0].forgeBody && writeBodyBinary(*loaded.extrusions[0].forgeBody)==geometry,"B-rep e snapshot riaperti");
    DocumentState legacy;nativeOk(decodeDocument(nativeLegacy49Fixture(encoded),legacy));
    require(!legacy.version.documentId.isNull() && legacy.version.checkinId.isNull() && legacy.baseCheckinId.isNull(),"migrazione documento 49");
    require(legacy.extrusions[0].forgeBody && writeBodyBinary(*legacy.extrusions[0].forgeBody)==geometry,"cache 49 leggibile senza cambiare geometria");
    QByteArray upgraded;nativeOk(encodeDocument(legacy,upgraded));DocumentState upgradedRead;nativeOk(decodeDocument(upgraded,upgradedRead));
    require(upgradedRead.version.documentId==legacy.version.documentId,"identita' migrata conservata nel 50");
    auto invalid=working;invalid.version.documentId={};QByteArray retained("mantieni");
    require(!encodeDocument(invalid,retained).isEmpty() && retained=="mantieni","ID nullo rifiutato atomicamente");
    auto malformedPayload=qUncompress(encoded.mid(11,payloadSize));malformedPayload.replace(malformedPayload.size()-48,16,QByteArray(16,0));
    const auto retainedId=loaded.version.documentId;
    require(!decodeDocument(nativeFixture(malformedPayload,50),loaded).isEmpty() && loaded.version.documentId==retainedId,"identita' corrotta rifiutata senza stato parziale");
    const auto truncated=qUncompress(encoded.mid(11,payloadSize));
    require(!decodeDocument(nativeFixture(truncated.left(truncated.size()-1),50),loaded).isEmpty(),"metadati troncati rifiutati");
    History undo;undo.record(working);auto copied=working;forkDocumentIdentity(copied);copied.baseCheckinId=QUuid::createUuid();
    const auto copyId=copied.version.documentId,copyBase=copied.baseCheckinId;
    require(undo.undo(copied) && copied.version.documentId==copyId && copied.baseCheckinId==copyBase,"Undo preserva identita' e base correnti");
    require(undo.redo(copied) && copied.version.documentId==copyId && copied.baseCheckinId==copyBase,"Redo preserva identita' e base correnti");
    require(copied.extrusions[0].forgeBody==working.extrusions[0].forgeBody,"copia indipendente riusa B-rep immutabili");

    DrawingModelSnapshot snapshot;nativeOk(drawingSnapshotOfDocument(working,{root.featureId},snapshot));
    require(snapshot.version.documentId==identity && snapshot.bodies[0].body==working.extrusions[0].forgeBody,"ponte tavole senza ricostruire B-rep");
    DrawingView view;view.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));
    std::vector<DrawingSection> pieces;nativeOk(sectionDrawingView(snapshot,view,pieces));
    const auto chosen=std::find_if(pieces.begin(),pieces.end(),[](const auto &p){return std::fabs(arcLength(*p.curve,p.range)-10)<1e-7;});
    require(chosen!=pieces.end(),"sezione da quotare nel documento reale");
    DrawingDocument drawing;drawing.model=snapshot.version;drawing.views={view};drawing.dimensions={dimensionOfSection(view,*chosen)};
    const auto dimensionId=drawing.dimensions[0].id;
    auto value=evaluateDrawingDimension(drawing,snapshot,dimensionId);
    require(value.status==DrawingDimensionStatus::Ready && std::fabs(value.value-10)<1e-7,"quota da genealogia applicativa");

    RevisionStore store(temporary.filePath("archivio"));nativeOk(store.initialize());
    NativeRevisionSession session;nativeOk(beginNativeCheckout(store,working,"Particolare","progettista",session));
    require(session.checkout.documentId==identity,"archivio adotta UUID nativo");
    nativeOk(resumeNativeCheckout(store,loaded,session));
    RevisionCheckinRequest request;nativeOk(prepareNativeCheckin(working,session,"A","prima versione",request));
    QByteArray pending;nativeOk(encodeNativeRevisionOperation(session,request,pending));
    const auto pendingPath=temporary.filePath("operazione.pendente");
    {QFile f(pendingPath);require(f.open(QIODevice::WriteOnly)&&f.write(pending)==pending.size(),"operazione pendente su disco");}
    NativeRevisionSession recovered;RevisionCheckinRequest retry;
    {QFile f(pendingPath);require(f.open(QIODevice::ReadOnly),"operazione riletta");nativeOk(decodeNativeRevisionOperation(f.readAll(),recovered,retry));}
    require(retry.payload==request.payload && retry.checkinId==request.checkinId && recovered.checkout.token==session.checkout.token,
            "byte originali e identita' dell'operazione dopo riapertura");
    RevisionRecord first;nativeOk(store.checkin(request,first)); // pubblicato, risposta al client persa
    nativeOk(publishNativeCheckin(store,retry,loaded,recovered,first));
    require(loaded.baseCheckinId==first.version.checkinId && loaded.version.checkinId.isNull() && recovered.checkout.token.isNull(),
            "ricevuta ritentata aggiorna base e rilascia prenotazione client");
    QVector<RevisionRecord> records;nativeOk(store.history(identity,records));require(records.size()==1,"nessun doppio check-in nativo");
    DocumentState archived;nativeOk(readNativeRevision(store,first.version,archived));
    require(archived.version.checkinId==first.version.checkinId && archived.version.documentId==identity
            && archived.extrusions[0].forgeBody && writeBodyBinary(*archived.extrusions[0].forgeBody)==geometry,"snapshot archiviato con identita' coerente");
    require(!saveDocumentFile(temporary.filePath("archiviato.prt"),archived).isEmpty()
            && !QFileInfo::exists(temporary.filePath("archiviato.prt")),"normale Salva non riscrive snapshot archiviati");
    auto independent=archived;forkDocumentIdentity(independent);
    nativeOk(saveDocumentFile(temporary.filePath("indipendente.prt"),independent));
    require(independent.version.documentId!=identity && independent.version.checkinId.isNull() && independent.baseCheckinId.isNull(),
            "copia indipendente scollegata dall'archivio originale");
    DrawingModelSnapshot frozen;nativeOk(drawingSnapshotOfDocument(archived,{root.featureId},frozen));
    require(frozen.version.checkinId==first.version.checkinId && frozen.bodies[0].names->faces==originalNames->faces,
            "nomi genealogici dello snapshot dopo lettura archivio");
    drawing.model=frozen.version;value=evaluateDrawingDimension(drawing,frozen,dimensionId);
    require(value.status==DrawingDimensionStatus::Ready && std::fabs(value.value-10)<1e-7,"quota sulla versione archiviata");

    DocumentState next;NativeRevisionSession nextSession;
    nativeOk(checkoutNativeRevision(store,first.version,"progettista",next,nextSession));
    require(next.version.documentId==identity && next.version.checkinId.isNull() && next.baseCheckinId==first.version.checkinId,
            "copia di lavoro conserva ID e base del modello");
    // Ritentare una ricevuta gia' applicata non deve annullare il nuovo check-out.
    nativeOk(publishNativeCheckin(store,retry,next,nextSession,first));
    require(!nextSession.checkout.token.isNull(),"ricevuta vecchia non rilascia check-out successivo");
    next.extrusions[0].primitive.size[0]=14;next.extrusions[0].cachedGeometry=false;
    viewport.loadDocument(next);next=viewport.currentDocument();
    NativeRevisionSession resumed=nextSession;nativeOk(resumeNativeCheckout(store,next,resumed));
    RevisionCheckinRequest nextRequest;nativeOk(prepareNativeCheckin(next,nextSession,"B","allungamento",nextRequest));
    auto editedAfterPrepare=next;editedAfterPrepare.lengthUnit=LengthUnit::Inch;
    RevisionRecord second;
    require(!publishNativeCheckin(store,nextRequest,editedAfterPrepare,nextSession,second).isEmpty(),"modifica dopo preparazione rifiutata");
    nativeOk(publishNativeCheckin(store,nextRequest,next,nextSession,second));
    require(second.previousCheckinId==first.version.checkinId && second.sequence==2 && next.baseCheckinId==second.version.checkinId,
            "seconda revisione nativa discende dalla prima");
    DrawingModelSnapshot newSnapshot;DocumentState secondArchived;
    nativeOk(readNativeRevision(store,second.version,secondArchived));nativeOk(drawingSnapshotOfDocument(secondArchived,{root.featureId},newSnapshot));
    require(evaluateDrawingDimension(drawing,newSnapshot,dimensionId).status==DrawingDimensionStatus::VersionMismatch,
            "tavola storica non passa automaticamente al modello nuovo");
    nativeOk(retargetDrawingModel(drawing,newSnapshot.version));
    value=evaluateDrawingDimension(drawing,newSnapshot,dimensionId);
    require(value.status==DrawingDimensionStatus::Ready && std::fabs(value.value-14)<1e-7,"quota segue variazione parametrica e genealogia reali");
    DocumentState firstAgain;nativeOk(readNativeRevision(store,first.version,firstAgain));
    require(writeBodyBinary(*firstAgain.extrusions[0].forgeBody)==geometry,"prima copia geometrica resta identica");
    NativeRevisionSession staleSession;DocumentState staleWorking;
    require(!checkoutNativeRevision(store,first.version,"altro",staleWorking,staleSession).isEmpty(),"check-out vecchia testa rifiutato senza prenotazione");

    // Tavola e modello archiviati con identita' e dipendenze coerenti.
    drawing.model=first.version;nativeOk(store.registerDocument(drawing.version.documentId,"Tavola","drawing"));
    RevisionCheckinRequest drawingRequest;nativeOk(store.checkout(drawing.version.documentId,"disegnatore",drawingRequest.checkout));
    drawingRequest.checkinId=QUuid::createUuid();drawing.version.checkinId=drawingRequest.checkinId;
    drawingRequest.technicalRevision="A";drawingRequest.dependencies={first.version};
    nativeOk(encodeDrawingDocument(drawing,drawingRequest.payload));RevisionRecord drawingRecord;nativeOk(store.checkin(drawingRequest,drawingRecord));
    QByteArray drawingBytes;nativeOk(store.read(drawingRecord.version,drawingBytes));DrawingDocument drawingAgain;nativeOk(decodeDrawingDocument(drawingBytes,drawingAgain));
    require(drawingAgain.model.checkinId==first.version.checkinId,"tavola conserva il riferimento al vecchio modello");

    // Operazioni troncate/corrotte non producono richieste o sessioni parziali.
    const auto retainedToken=recovered.checkout.token;const auto retainedRequest=retry.checkinId;
    auto damaged=pending;damaged[damaged.size()-1]=char(damaged.back()^1);
    require(!decodeNativeRevisionOperation(damaged,recovered,retry).isEmpty() && recovered.checkout.token==retainedToken
            && retry.checkinId==retainedRequest,"operazione corrotta rifiutata atomicamente");
    require(!decodeNativeRevisionOperation(pending.left(pending.size()-1),recovered,retry).isEmpty(),"operazione troncata rifiutata");
    require(!decodeNativeRevisionOperation(pending+"x",recovered,retry).isEmpty(),"byte extra dell'operazione rifiutati");
    auto noGeometry=next;noGeometry.extrusions[0].forgeBody.reset();
    NativeRevisionSession incomplete;nativeOk(beginNativeCheckout(store,noGeometry,"Particolare","progettista",incomplete));
    require(!prepareNativeCheckin(noGeometry,incomplete,"C","incompleto",request).isEmpty(),"check-in nativo richiede snapshot geometrico disponibile");
    nativeOk(store.cancelCheckout(incomplete.checkout));
    auto invalidSelection=newSnapshot;
    require(!drawingSnapshotOfDocument(secondArchived,{root.featureId,root.featureId},invalidSelection).isEmpty(),"selezione duplicata della tavola rifiutata");
}
}
