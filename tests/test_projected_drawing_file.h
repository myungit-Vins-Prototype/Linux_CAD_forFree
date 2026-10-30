#pragma once
#include <QDockWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include "../cad_drawing_dimension.h"
#include "../cad_drawing_derived.h"
namespace {
void brokenNativeFileRevisionTests(){
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    QTemporaryDir temporary;const auto modelPath=temporary.filePath("interrotta.prt"),path=temporary.filePath("interrotta.drw");
    auto model=clientModel();clientOk(saveDocumentFile(modelPath,model));clientOk(checkoutWorkspaceDocument(modelPath,model,"qa"));RevisionRecord modelRecord;
    clientOk(checkinWorkspaceDocument(modelPath,model,"M-A","",modelRecord));const auto modelBytes=clientRead(modelPath);
    DrawingWindow window;clientOk(window.createFromModel(modelPath));DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=2;
    clientOk(window.editor()->insertView(view,{view.id,"Interrotta",{100,100},{model.extrusions[0].featureId}}));
    double low=std::numeric_limits<double>::infinity(),high=-low;DrawingDimension dimension;bool chosen=false;
    for(const auto &curve:window.editor()->curves(view.id)){
        for(double t:{curve.range.lo,curve.range.hi,(curve.range.lo+curve.range.hi)/2}){const auto p=view.plane.toLocal(curve.curve->point(t));low=std::min(low,p.x());high=std::max(high,p.x());}
        if(!chosen&&curve.kind==DrawingCurveKind::ProjectedEdge&&std::abs(arcLength(*curve.curve,curve.range)-10)<1e-7){dimension=dimensionOfCurve(view,curve,DrawingMeasure::EndpointDistance);chosen=true;}
    }
    require(chosen&&high-low>4,"geometria della vista interrotta nativa");clientOk(window.editor()->addDimension(dimension,{120,120}));
    DrawingViewBreak cut{false,low+(high-low)*.3,low+(high-low)*.7,1};clientOk(window.editor()->setViewBreak(view.id,cut));
    clientOk(window.savePath(path));clientOk(window.pinModelRevision(modelRecord.version));clientOk(window.savePath(path));
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"qa"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"Interruzione A"));
    auto first=window.document().sheet.drawing.version;first.checkinId=window.document().baseCheckinId;clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"qa"));
    cut.gap=2;clientOk(window.editor()->setViewBreak(view.id,cut));clientOk(window.savePath(path));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"Interruzione B"));
    DrawingFile loaded;clientOk(loadDrawingFile(path,loaded));require(loaded.sheet.views[0].interruption&&loaded.sheet.views[0].interruption->gap==2,"file drw conserva interruzione B");
    DrawingWindow archived;clientOk(archived.openRevision(path,first));require(archived.editor()->sheet().views[0].interruption&&archived.editor()->sheet().views[0].interruption->gap==1,"storico A conserva distacco originale");
    require(archived.editor()->dimensionResult(dimension.id).status==DrawingDimensionStatus::Ready&&std::abs(archived.editor()->dimensionResult(dimension.id).value-10)<1e-7,"quota storica completa invariata");
    require(archived.editor()->isReadOnly()&&!archived.editor()->findChild<QAction *>("drawingBreakViewAction")->isEnabled()&&!archived.editor()->setViewBreak(view.id,{}).isEmpty(),"storico protegge interruzione");
    DrawingPdfReport report;clientOk(archived.exportPdfPath(temporary.filePath("interrotta-a.pdf"),report));require(clientRead(modelPath)==modelBytes,"tavola e PDF non riscrivono il modello originale");
    auto *insert=window.findChild<QAction *>("drawingBreakViewAction");bool inMenu=false;for(auto *action:window.menuBar()->actions())if(action->text()=="Inserisci")inMenu=action->menu()->actions().contains(insert);require(inMenu,"vista interrotta nel menu nativo");
}
void auxiliaryNativeFileRevisionTests(){
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    QTemporaryDir temporary;const auto modelPath=temporary.filePath("ausiliaria.prt"),path=temporary.filePath("ausiliaria.drw");
    auto model=clientModel();clientOk(saveDocumentFile(modelPath,model));clientOk(checkoutWorkspaceDocument(modelPath,model,"qa"));RevisionRecord modelRecord;
    clientOk(checkinWorkspaceDocument(modelPath,model,"M-A","",modelRecord));DrawingWindow window;clientOk(window.createFromModel(modelPath));
    DrawingView parent;parent.kind=DrawingViewKind::ProjectedEdges;parent.scale=2;clientOk(window.editor()->insertView(parent,{parent.id,"Madre",{60,120},{model.extrusions[0].featureId}}));
    auto sheet=window.editor()->sheet();DrawingView child;DrawingViewLayout derived{child.id,"Ausiliaria A",{160,60},{model.extrusions[0].featureId}};
    derived.relation.kind=DrawingDerivedKind::Auxiliary;derived.relation.parentId=parent.id;derived.relation.auxiliaryAngle=45;
    sheet.drawing.views.push_back(child);sheet.views.append(derived);clientOk(synchronizeDrawingViews(sheet));clientOk(window.editor()->insertView(sheet.drawing.views.back(),sheet.views.back()));
    DrawingDimension dimension;bool chosen=false;
    for(const auto &curve:window.editor()->curves(child.id))if(curve.kind==DrawingCurveKind::ProjectedEdge&&arcLength(*curve.curve,curve.range)>1e-5){dimension=dimensionOfCurve(sheet.drawing.views.back(),curve);chosen=true;break;}
    require(chosen,"bordo dell'ausiliaria nativa da quotare");clientOk(window.editor()->addDimension(dimension,{200,80}));const auto nominal=window.editor()->dimensionResult(dimension.id).value;
    clientOk(window.savePath(path));clientOk(window.pinModelRevision(modelRecord.version));
    clientOk(window.savePath(path));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"qa"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"Ausiliaria A"));
    auto first=window.document().sheet.drawing.version;first.checkinId=window.document().baseCheckinId;
    require(!first.checkinId.isNull(),"check-in dell'ausiliaria A");clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"qa"));
    auto changed=window.editor()->sheet().views.back();changed.relation.auxiliaryAngle=75;clientOk(window.editor()->updateView(window.editor()->sheet().drawing.views.back(),changed));
    clientOk(window.savePath(path));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"Ausiliaria B"));
    DrawingFile loaded;clientOk(loadDrawingFile(path,loaded));require(loaded.sheet.views.back().relation.auxiliaryAngle==75&&loaded.sheet.views.back().relation.parentId==parent.id,"file .drw conserva direzione B e madre");
    DrawingWindow archived;clientOk(archived.openRevision(path,first));require(archived.document().sheet.views.back().relation.auxiliaryAngle==45,"storico A conserva la direzione senza futuro B");
    require(archived.editor()->dimensionResult(dimension.id).status==DrawingDimensionStatus::Ready&&std::abs(archived.editor()->dimensionResult(dimension.id).value-nominal)<1e-7,"quota storica ausiliaria invariata");
    require(archived.editor()->isReadOnly()&&!archived.editor()->findChild<QAction *>("drawingAuxiliaryViewAction")->isEnabled()&&!archived.editor()->beginAuxiliaryViewPlacement(parent.id,30).isEmpty(),"storico ausiliaria protegge comandi e dati");
    DrawingPdfReport report;clientOk(archived.exportPdfPath(temporary.filePath("ausiliaria-a.pdf"),report));
    auto *insert=window.findChild<QAction *>("drawingAuxiliaryViewAction");bool inMenu=false;
    for(auto *action:window.menuBar()->actions())if(action->text()=="Inserisci")inMenu=action->menu()->actions().contains(insert);
    require(inMenu,"vista ausiliaria raggiungibile dal menu della finestra nativa");
}
void portableDrawingReferencesTests(){
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    auto definition=clientModel();ExtrusionObject offset;
    offset.feature=BodyFeature::Scale;offset.featureId=81002;
    offset.modelBodyId=definition.extrusions[0].modelBodyId;offset.firstBody=0;offset.scaleFactor=1.2;
    definition.extrusions.append(offset);CadViewport viewport;viewport.loadDocument(definition);
    const auto original=viewport.currentDocument();const auto tip=original.extrusions.back().featureId;
    require(original.extrusions.back().forgeBody&&original.extrusions.back().error.isEmpty(),"catena portabile costruita");
    DrawingModelSnapshot built;clientOk(drawingSnapshotOfDocument(original,{tip},built));
    QByteArray cachedBytes,compactBytes;clientOk(encodeDocument(original,cachedBytes,true));clientOk(encodeDocument(original,compactBytes,false));
    DocumentState cached,compact;clientOk(decodeDocument(cachedBytes,cached));clientOk(decodeDocument(compactBytes,compact));
    DrawingModelSnapshot direct;clientOk(drawingSnapshotOfDocument(cached,{tip},direct));
    require(direct.bodies[0].names->faces==built.bodies[0].names->faces&&direct.bodies[0].names->edges==built.bodies[0].names->edges
        &&direct.bodies[0].names->vertices==built.bodies[0].names->vertices,"snapshot diretto registra tutta la genealogia");
    DrawingEditorModel savedModel,rebuilt;clientOk(prepareDrawingEditorModel(cached,savedModel));clientOk(prepareDrawingEditorModel(compact,rebuilt));
    require(savedModel.snapshot.bodies.size()==1&&rebuilt.snapshot.bodies.size()==1,"stesso corpo logico nei due percorsi");
    require(savedModel.snapshot.bodies[0].names->faces==rebuilt.snapshot.bodies[0].names->faces
        &&savedModel.snapshot.bodies[0].names->edges==rebuilt.snapshot.bodies[0].names->edges
        &&savedModel.snapshot.bodies[0].names->vertices==rebuilt.snapshot.bodies[0].names->vertices,"nomi identici con snapshot o rigenerazione");
    require(rebuilt.legacySnapshotNames.isEmpty(),"nessuna migrazione per indice sui corpi rigenerati");
    // Fixture legacy indipendente: riproduce la registrazione della sola punta.
    DocumentState legacy;clientOk(decodeDocument(cachedBytes,legacy));
    const int last=legacy.extrusions.size()-1;registerFeatureGenealogy(legacy.extrusions[last],last,legacy.sketches,legacy.extrusions);
    DrawingEditorModel old;old.snapshot.version=original.version;
    old.snapshot.bodies.push_back({tip,legacy.extrusions[last].forgeBody,elementNamesOf(*legacy.extrusions[last].forgeBody)});
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));
    std::vector<DrawingCurve> curves;clientOk(drawingViewCurves(old.snapshot,view,curves));
    const auto edge=std::find_if(curves.begin(),curves.end(),[](const auto &c){return c.kind==DrawingCurveKind::ProjectedEdge&&arcLength(*c.curve,c.range)>1;});
    require(edge!=curves.end(),"bordo legacy misurabile");
    const auto first=drawingAttachment(view,*edge,old.snapshot,DrawingPointRole::Start),end=drawingAttachment(view,*edge,old.snapshot,DrawingPointRole::End);
    auto dimension=drawingDimensionBetween(view,old.snapshot,curves,{first,end},DrawingMeasure::Distance,{20,20});
    DrawingFile file;file.modelPath="parte.prt";file.sheet.drawing.model=original.version;
    file.sheet.drawing.views.push_back(view);file.sheet.views.append({view.id,"Portabile",{60,90},{tip}});
    file.sheet.drawing.dimensions.push_back(dimension);file.sheet.dimensions.append({dimension.id,{80,110}});
    DrawingAxis axis;axis.viewId=view.id;axis.references={first,end};file.sheet.axes.append(axis);
    DrawingAnnotation note;note.viewId=view.id;note.text="Riferimento portabile";note.position={100,30};note.reference=first;file.sheet.annotations.append(note);
    clientOk(drawingModelDefinitionHash(original,file.modelDefinitionHash));
    // Anche un foglio non attivo deve essere migrato e salvato.
    DrawingSheet second;clientOk(cloneDrawingSheet(file.sheet,second));file.otherSheets.append(second);
    file.sheetIds={QUuid::createUuid(),QUuid::createUuid()};
    QTemporaryDir directory;const auto modelPath=directory.filePath("parte.prt"),drawingPath=directory.filePath("parte.drw");
    clientWrite(modelPath,cachedBytes);clientOk(saveDrawingFile(drawingPath,file));
    const auto before=file.sheet.drawing.dimensions[0].references;
    require(upgradeDrawingFileReferences(file,rebuilt)==0,"legacy senza snapshot non riassegnato");
    DrawingFile foreign=file;foreign.sheet.drawing.model.documentId=QUuid::createUuid();
    require(upgradeDrawingFileReferences(foreign,savedModel)==0,"nessuna migrazione fra documenti diversi");
    {
        DrawingWindow window;clientOk(window.openPath(drawingPath));
        require(window.isModified(),"migrazione legacy richiede salvataggio della tavola");
        require(window.editor()->dimensionResult(dimension.id).status==DrawingDimensionStatus::Ready,"quota legacy migrata all'apertura");
        require(window.document().sheet.drawing.dimensions[0].references[0].curve.edgeName!=before[0].curve.edgeName,"riferimento sostituito da nome genealogico");
        clientOk(window.savePath(drawingPath));
    }
    // Il modello viene spostato insieme alla tavola e perde deliberatamente la cache.
    clientWrite(modelPath,compactBytes);const auto moved=directory.filePath("trasferiti");require(QDir().mkpath(moved),"cartella destinazione portabile");
    require(QFile::rename(modelPath,moved+"/parte.prt")&&QFile::rename(drawingPath,moved+"/parte.drw"),"spostamento coppia CAD");
    DrawingWindow portable;clientOk(portable.openPath(moved+"/parte.drw"));
    const auto result=portable.editor()->dimensionResult(dimension.id);
    require(result.status==DrawingDimensionStatus::Ready&&std::abs(result.value-arcLength(*edge->curve,edge->range))<1e-7,"quota conservata dopo trasferimento e rigenerazione");
    require(portable.editor()->annotationResult(note.id).status==DrawingDimensionStatus::Ready,"annotazione conservata dopo rigenerazione");
    DrawingFile reopened;clientOk(loadDrawingFile(moved+"/parte.drw",reopened));
    for(int i=0;i<drawingSheetCount(reopened);++i){DrawingSheetEditor editor(drawingSheetAt(reopened,i),rebuilt);
        require(editor.dimensionResult(drawingSheetAt(reopened,i).drawing.dimensions.front().id).status==DrawingDimensionStatus::Ready,"riferimenti portabili anche sul foglio non attivo");}
    auto broken=reopened;broken.sheet.drawing.dimensions[0].references[0].curve.edgeName="bordo-eliminato";
    require(upgradeDrawingFileReferences(broken,savedModel)==0,"un riferimento realmente assente non viene indovinato");
}

void projectedDrawingFileTests(){
    portableDrawingReferencesTests();
    brokenNativeFileRevisionTests();
    auxiliaryNativeFileRevisionTests();
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    QTemporaryDir temporary;const auto sourcePath=temporary.filePath("modello.prt"),drawingPath=temporary.filePath("proiezioni.drw");
    auto working=clientModel();clientOk(saveDocumentFile(sourcePath,working));clientOk(checkoutWorkspaceDocument(sourcePath,working,"modellatore"));RevisionRecord modelA;
    clientOk(checkinWorkspaceDocument(sourcePath,working,"M-A","",modelA));DrawingWindow window;clientOk(window.createFromModel(sourcePath));
    auto block=window.editor()->sheet().titleBlock;block.visible=true;block.code="FC-A";block.author="Autore A";block.date="2026-10-16";clientOk(window.editor()->setPageSettings(window.editor()->sheet().title,{297,210},block));
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.sectionReferences=true;view.plane=Frame3(Vec3(0,0,2),Vec3(0,0,1),Vec3(1,0,0));view.scale=4;
    clientOk(window.editor()->insertView(view,{view.id,"Vista superiore",QPointF(140,100),{working.extrusions[0].featureId}}));
    // La fixture quota anche bordi coperti: renderli disponibili nella vista.
    clientOk(window.editor()->setHiddenLinesVisible(view.id,true));QApplication::processEvents();
    const auto &curves=window.editor()->curves(view.id);int chosen=-1;
    // I bordi coincidenti sono consolidati: scegliere il proprietario presente sul foglio.
    for(auto *item:window.editor()->canvas()->scene()->items())if(item->data(0).toString()=="projection"){
        const auto i=item->data(2).toULongLong();
        if(i<curves.size()&&curves[i].kind==DrawingCurveKind::ProjectedEdge&&std::abs(arcLength(*curves[i].curve,curves[i].range)-10)<1e-7){chosen=int(i);break;}
    }
    require(chosen>=0,"bordo proiettato da quotare su .prt");for(auto *item:window.editor()->canvas()->scene()->items())if(item->data(0).toString()=="projection"&&item->data(2).toInt()==chosen)item->setSelected(true);
    clientOk(window.editor()->addSelectedDimension(DrawingMeasure::HorizontalDistance));const auto dimension=window.document().sheet.drawing.dimensions[0].id;
    auto snapped=dimensionOfCurve(view,curves[std::size_t(chosen)]);snapped.measure=DrawingMeasure::Distance;
    DrawingModelSnapshot modelSnapshot;clientOk(drawingSnapshotOfDocument(working,{working.extrusions[0].featureId},modelSnapshot));
    // Il proprietario consolidato può avere verso opposto: il richiamo deve seguire l'estremo X crescente.
    const auto &selectedCurve=curves[std::size_t(chosen)];
    const bool forward=selectedCurve.curve->point(selectedCurve.range.hi).x()>selectedCurve.curve->point(selectedCurve.range.lo).x();
    snapped.references={drawingAttachment(view,selectedCurve,modelSnapshot,forward?DrawingPointRole::Start:DrawingPointRole::End),
                        drawingAttachment(view,selectedCurve,modelSnapshot,forward?DrawingPointRole::End:DrawingPointRole::Start)};
    snapped.section=snapped.references[0].curve;snapped.decimals=3;const auto snappedId=snapped.id;
    clientOk(window.editor()->addDimension(snapped,QPointF(105,70)));
    auto changedView=view;changedView.scale=3;auto changedLayout=window.editor()->sheet().views[0];changedLayout.title="Vista superiore modificata";
    clientOk(window.editor()->updateView(changedView,changedLayout));require(window.isModified(),"modifica vista rende sporca la tavola");
    auto *editAction=window.editor()->findChild<QAction *>("drawingEditViewAction");bool sharedAction=false;
    for(auto *top:window.menuBar()->actions()){require(dynamic_cast<CadMenu *>(top->menu()),"menu comuni alla GUI 3D");
        if(top->text()=="Modifica")sharedAction=top->menu()->actions().contains(editAction);}
    require(sharedAction,"menu e toolbar usano la stessa azione di modifica");
    const auto preview=qEnvironmentVariable("FORGECAD_DRAWING_WINDOW_PREVIEW");if(!preview.isEmpty()){
        PdfWindow themeHost;window.resize(1200,820);window.show();QApplication::processEvents();window.editor()->fitSheet();require(window.grab().save(preview),"anteprima finestra tavola completa");window.hide();}
    clientOk(window.savePath(drawingPath));DrawingFile read;clientOk(loadDrawingFile(drawingPath,read));
    require(read.sheet.drawing.views[0].kind==DrawingViewKind::ProjectedEdges&&read.sheet.drawing.dimensions[0].section.kind==DrawingCurveKind::ProjectedEdge,".drw conserva tipo di vista e riferimento al bordo");
    require(read.sheet.views[0].showHiddenLines,".drw conserva la visibilita' per vista");
    require(read.sheet.views[0].title=="Vista superiore modificata"&&read.sheet.drawing.views[0].scale==3,"modifica vista persistita nel .drw");
    require(read.sheet.titleBlock.visible&&read.sheet.titleBlock.code=="FC-A"&&read.sheet.titleBlock.author=="Autore A",".drw conserva i campi del cartiglio");
    require(read.sheet.drawing.dimensions.size()==2&&read.sheet.drawing.dimensions[1].references.size()==2&&read.sheet.drawing.dimensions[1].decimals==3,".drw conserva entrambi gli snap e la precisione");
    const auto savedPosition=window.editor()->sheet().views[0].position;
    clientOk(window.editor()->moveView(view.id,savedPosition+QPointF(12,-7)));require(window.isModified(),"layout modificato rende sporca la finestra");
    clientOk(window.editor()->undo());require(!window.isModified(),"undo al salvataggio pulisce la finestra");
    clientOk(window.editor()->redo());require(window.isModified(),"redo dopo salvataggio rende sporca la finestra");
    clientOk(window.savePath(drawingPath));clientOk(window.editor()->undo());require(window.isModified(),"undo oltre nuovo salvataggio rende sporca la finestra");
    clientOk(window.editor()->redo());require(!window.isModified(),"redo al nuovo salvataggio pulisce la finestra");
    clientOk(window.editor()->removeView(view.id));require(window.document().sheet.drawing.views.empty(),"cancellazione aggiorna documento contenitore");
    clientOk(window.editor()->undo());require(!window.isModified()&&window.document().sheet.drawing.dimensions[0].id==dimension,"ripristino vista e quota al checkpoint");
    const auto bytes=clientRead(drawingPath);require(bytes.size()<8192,"proiezioni persistite senza cache 3D duplicata");
    // Schema 2: relazioni e tagli entrano nei medesimi snapshot, senza una seconda copia del modello.
    auto derivedSheet=window.editor()->sheet();DrawingView derived;DrawingViewLayout derivedLayout;derivedLayout.viewId=derived.id;derivedLayout.title="Derivata destra";derivedLayout.position={220,100};derivedLayout.relation.parentId=view.id;derivedLayout.relation.kind=DrawingDerivedKind::Orthographic;
    derivedSheet.drawing.views.push_back(derived);derivedSheet.views.append(derivedLayout);clientOk(synchronizeDrawingViews(derivedSheet));clientOk(window.editor()->insertView(derivedSheet.drawing.views.back(),derivedSheet.views.back()));const auto derivedId=derived.id;
    auto sectionSheet=window.editor()->sheet();DrawingView technical;technical.sectionStyle=DrawingSectionStyle::Full;DrawingViewLayout technicalLayout;technicalLayout.viewId=technical.id;technicalLayout.title="Sezione completa";technicalLayout.position={140,114};technicalLayout.relation.parentId=view.id;technicalLayout.relation.kind=DrawingDerivedKind::Section;technicalLayout.relation.cutStart={-2,3};technicalLayout.relation.cutEnd={12,3};technicalLayout.relation.label="A";technicalLayout.relation.aligned=false;
    sectionSheet.drawing.views.push_back(technical);sectionSheet.views.append(technicalLayout);clientOk(synchronizeDrawingViews(sectionSheet));technical=sectionSheet.drawing.views.back();clientOk(window.editor()->insertView(technical,sectionSheet.views.back()));const auto technicalId=technical.id;
    const auto &sectionPieces=window.editor()->sections(technicalId);const auto sectionEdge=std::find_if(sectionPieces.begin(),sectionPieces.end(),[](const auto &c){return std::abs(arcLength(*c.curve,c.range)-10)<1e-7;});require(sectionEdge!=sectionPieces.end(),"taglio tecnico sul solido reale del documento");auto technicalDimension=dimensionOfCurve(technical,*sectionEdge,DrawingMeasure::SectionLength);clientOk(window.editor()->addDimension(technicalDimension,{210,116}));const auto technicalDimensionId=technicalDimension.id;
    clientOk(window.pinModelRevision(modelA.version));
    require(window.editor()->pageContext().projectRevisions.size()==1&&window.editor()->pageContext().projectRevisions[0].version.checkinId==modelA.version.checkinId,"tabella alimentata dal modello A reale");
    DrawingRevisionTable revisionNotes;revisionNotes.visible=true;revisionNotes.notes={{modelA.version.checkinId,"Prima emissione del particolare","Verificatore A"}};clientOk(window.editor()->setRevisionTable(revisionNotes));
    DrawingAxis modelAxis;modelAxis.viewId=view.id;modelAxis.name="Asse del bordo";modelAxis.references=snapped.references;clientOk(window.editor()->addAxis(modelAxis));
    DrawingDimension axisLength;axisLength.viewId=view.id;axisLength.measure=DrawingMeasure::Distance;DrawingAttachment axisFirst;axisFirst.axisId=modelAxis.id;axisFirst.role=DrawingPointRole::Start;auto axisLast=axisFirst;axisLast.role=DrawingPointRole::End;axisLength.references={axisFirst,axisLast};const auto axisLengthId=axisLength.id;
    clientOk(window.editor()->addDimension(axisLength,{180,65}));require(std::abs(window.editor()->dimensionResult(axisLengthId).value-10)<1e-7,"quota asse associato sul modello A");
    DrawingDimensionPresentation toleranceA;toleranceA.mode=DrawingToleranceMode::Limits;toleranceA.upper=.1;toleranceA.lower=-.02;toleranceA.suffix=" TOL-A";clientOk(window.editor()->updateDimension(axisLength,toleranceA));
    const auto presentationOf=[&](const DrawingSheet &sheet){const auto it=std::find_if(sheet.dimensions.begin(),sheet.dimensions.end(),[&](const auto &p){return p.dimensionId==axisLengthId;});require(it!=sheet.dimensions.end(),"presentazione quota presente");return it->presentation;};
    const auto formattedQuota=[&](DrawingSheetEditor *editor){return drawingDimensionLabel(axisLength,editor->dimensionResult(axisLengthId).value,presentationOf(editor->sheet())).summary;};
    DrawingAnnotation noteA;noteA.text="NOTA TECNICA A";noteA.position={210,25};noteA.width=60;clientOk(window.editor()->addAnnotation(noteA));
    DrawingAnnotation leaderA;leaderA.text="Richiamo A al modello archiviato";leaderA.position={225,60};leaderA.width=60;leaderA.viewId=view.id;leaderA.reference=axisLast;clientOk(window.editor()->addAnnotation(leaderA));
    const auto anchorA=window.editor()->annotationResult(leaderA.id).point;
    DrawingStyle styleA;styleA.fontFamily="DejaVu Serif";styleA.dimensionHeight=3.5;styleA.arrow=DrawingArrowStyle::Filled;clientOk(window.editor()->setDrawingStyle(styleA));const QPointF titleA(0,8);clientOk(window.editor()->moveViewTitle(view.id,window.editor()->sheet().views[0].position+titleA));
    clientOk(window.editor()->setCenterMarksVisible(view.id,true));
    clientOk(window.savePath(drawingPath));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"A","vista e sezione"));
    require(window.editor()->pageContext().drawingRevision=="Lavoro (base A)"&&window.editor()->pageContext().modelRevision=="M-A","cartiglio distingue copia di lavoro della tavola e revisione del modello");
    QVector<RevisionRecord> history;clientOk(workspaceDrawingHistory(drawingPath,window.document(),history));require(history.size()==1,"check-in della nuova vista");const auto first=history[0].version;
    working.extrusions[0].primitive.size[0]=14;working.extrusions[0].cachedGeometry=false;{CadViewport viewport;viewport.loadDocument(working);working=viewport.currentDocument();}
    clientOk(saveDocumentFile(sourcePath,working));clientOk(checkoutWorkspaceDocument(sourcePath,working,"modellatore"));RevisionRecord modelB;clientOk(checkinWorkspaceDocument(sourcePath,working,"M-B","",modelB));
    require(window.editor()->isReadOnly()&&!window.pinModelRevision(modelB.version).isEmpty(),"check-in blocca sostituzione della sorgente prima del check-out");
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
    clientOk(window.pinModelRevision(modelB.version));require(std::abs(window.editor()->dimensionResult(dimension).value-14)<1e-7,"quota del bordo segue genealogia al modello B");
    require(std::abs(window.editor()->dimensionResult(snappedId).value-14)<1e-7,"entrambi gli snap seguono la genealogia al modello B");
    require(std::abs(window.editor()->dimensionResult(axisLengthId).value-14)<1e-7,"quota dell’asse segue il modello B");
    require(!window.editor()->canUndo()&&!window.editor()->canRedo(),"collegamento modello B separa la cronologia del layout");
    require(formattedQuota(window.editor()).contains("[14.100 / 13.980]")&&presentationOf(window.editor()->sheet())==toleranceA,"tolleranza A segue geometria B prima del nuovo check-in");
    auto toleranceB=toleranceA;toleranceB.mode=DrawingToleranceMode::Deviations;toleranceB.upper=.2;toleranceB.lower=-.05;toleranceB.suffix=" TOL-B";clientOk(window.editor()->updateDimension(axisLength,toleranceB));
    require(window.editor()->pageContext().modelRevision=="M-B","cartiglio aggiornato al modello esatto B");
    require(std::abs(window.editor()->dimensionResult(technicalDimensionId).value-14)<1e-7,"quota sulla sezione derivata segue il modello B");
    require(window.editor()->sheet().views[1].relation.parentId==view.id&&window.editor()->sheet().views[1].position.y()==window.editor()->sheet().views[0].position.y(),"legame e allineamento conservati dopo aggiornamento del modello");
    clientOk(window.editor()->setSectionHatch(technicalId,false));clientOk(window.editor()->reverseSection(technicalId));
    clientOk(window.editor()->setSectionLine(technicalId,{7,3},{16,3}));clientOk(window.editor()->setSectionStyle(technicalId,DrawingSectionStyle::HalfRight));require(std::abs(window.editor()->dimensionResult(technicalDimensionId).value-7)<1e-7,"B conserva una mezza sezione con quota del solo tratto rimasto");auto bView=window.editor()->sheet().drawing.views[2];auto bLayout=window.editor()->sheet().views[2];bLayout.title="Mezza sezione destra";clientOk(window.editor()->updateView(bView,bLayout));
    block.code="FC-B";block.author="Autore B";clientOk(window.editor()->setPageSettings(window.editor()->sheet().title,{420,297},block));
    clientOk(window.editor()->setHiddenLinesVisible(view.id,false));QApplication::processEvents();
    clientOk(window.editor()->moveView(view.id,window.editor()->sheet().views[0].position+QPointF(1,2)));clientOk(window.editor()->undo());
    require(window.isModified()&&std::abs(window.editor()->dimensionResult(dimension).value-14)<1e-7,"undo del layout non pulisce il cambio sorgente non salvato");
    require(window.findChild<QDockWidget *>("drawingRevisionDock")&&window.findChild<QTableWidget *>("drawingProjectHistory")->rowCount()==2,"pannello mostra lo storico reale del modello");
    bool tableEdited=false;QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog *>("drawingRevisionTableDialog");require(dialog,"dialogo tabella separato");auto *rows=dialog->findChild<QTableWidget *>("drawingRevisionNotes");require(rows&&rows->rowCount()==2&&rows->item(1,0)->text()==modelB.technicalRevision&&rows->item(1,1)->text()==modelB.createdUtc.toLocalTime().toString("yyyy-MM-dd"),"numero/data dal modello B reale");require(!(rows->item(1,0)->flags()&Qt::ItemIsEditable)&&!(rows->item(1,1)->flags()&Qt::ItemIsEditable),"numero e data non sono liberi");rows->item(1,2)->setText("Allungamento del particolare con verifica delle superfici e delle quote associative.");rows->item(1,4)->setText("Verificatore B");tableEdited=true;dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});window.findChild<QAction *>("drawingRevisionTableAction")->trigger();require(tableEdited,"compilazione della tabella dal dialogo reale");
    clientOk(window.editor()->setShadedFacesVisible(view.id,true));
    auto styleB=styleA;styleB.fontFamily="DejaVu Sans";styleB.dimensionHeight=4;styleB.arrow=DrawingArrowStyle::Tick;clientOk(window.editor()->setDrawingStyle(styleB));const QPointF titleB(5,12);clientOk(window.editor()->moveViewTitle(view.id,window.editor()->sheet().views[0].position+titleB));
    auto noteB=window.editor()->sheet().annotations[0];noteB.text="NOTA TECNICA B";noteB.width=70;clientOk(window.editor()->updateAnnotation(noteB));
    auto leaderB=window.editor()->sheet().annotations[1];leaderB.text="Richiamo B al modello aggiornato";clientOk(window.editor()->updateAnnotation(leaderB));
    require(std::abs(distance(window.editor()->annotationResult(leaderA.id).point,anchorA)-4)<1e-7,"punta del richiamo asse aggiornata al modello B");
    auto axisB=window.editor()->sheet().axes[0];axisB.extension=6;axisB.visible=false;clientOk(window.editor()->updateAxis(axisB));
    clientOk(window.editor()->setCenterMarksVisible(view.id,false));clientOk(window.editor()->setCenterLinesVisible(view.id,true));
    clientOk(window.savePath(drawingPath));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
    require(!window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"A","duplicato").isEmpty(),"etichetta tecnica duplicata rifiutata prima del pendente");WorkspaceRevisionStatus failedStatus;clientOk(workspaceDrawingRevisionStatus(drawingPath,window.document(),failedStatus));require(failedStatus.hasSession&&!failedStatus.hasPending,"duplicato non blocca la copia con un pendente non pubblicabile");
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"allungamento"));QVector<RevisionRecord> numbered;clientOk(workspaceDrawingHistory(drawingPath,window.document(),numbered));require(numbered.size()==2&&numbered.back().technicalRevision=="2","nuovo check-in numerato automaticamente");
    require(window.findChild<QTableWidget *>("drawingRevisionHistory")->rowCount()==2,"pannello aggiornato dopo check-in automatico");
    require(!window.document().sheet.views[0].showHiddenLines,"revisione B con tratti nascosti esclusi");
    require(!window.document().sheet.views[0].showCenterMarks&&window.document().sheet.views[0].showCenterLines,"revisione B conserva proprie opzioni di assi e centri");
    require(window.document().sheet.style==styleB&&window.document().sheet.views[0].titleOffset==titleB,"revisione B conserva stile e nome propri");
    DrawingPdfReport pdfReport;const auto output=qEnvironmentVariable("FORGECAD_PAGE_REVISION_OUTPUT");if(!output.isEmpty())require(QDir().mkpath(output),"cartella PDF revisioni");
    require(presentationOf(window.editor()->sheet())==toleranceB&&formattedQuota(window.editor()).contains("[+0.200 / -0.050]"),"revisione B conserva propria tolleranza");
    const auto pdfB=output.isEmpty()?temporary.filePath("revision-b.pdf"):output+"/revision-b.pdf";clientOk(window.exportPdfPath(pdfB,pdfReport));require(clientRead(pdfB).startsWith("%PDF-"),"PDF della copia di lavoro B");
    if(!output.isEmpty()){window.resize(1500,950);window.show();QApplication::processEvents();window.editor()->fitSheet();require(window.grab().save(output+"/pannello-revisioni.png"),"anteprima finestra con pannello revisioni");}
    const auto modelBytes=clientRead(sourcePath);require(!window.exportPdfPath(sourcePath,pdfReport).isEmpty()&&clientRead(sourcePath)==modelBytes,"export non sovrascrive il modello");
    const auto copy=temporary.filePath("copia.drw");
    {DrawingWindow archived;clientOk(archived.openRevision(drawingPath,first));require(archived.document().sheet.style==styleA&&archived.document().sheet.views[0].titleOffset==titleA,"storico A mantiene font/frecce/nome senza futuro B");require(!archived.editor()->setDrawingStyle(styleB).isEmpty()&&!archived.editor()->moveViewTitle(view.id,{1,1}).isEmpty(),"storico protegge stile e posizione nome");require(presentationOf(archived.editor()->sheet())==toleranceA&&formattedQuota(archived.editor()).contains("[10.100 / 9.980]"),"snapshot A conserva tolleranza e nominale A dopo B");require(!archived.editor()->updateDimension(axisLength,toleranceB).isEmpty(),"check-in protegge anche tolleranze");require(std::abs(archived.editor()->dimensionResult(axisLengthId).value-10)<1e-7,"snapshot A conserva quota e UUID dell’asse dopo B");require(archived.editor()->isReadOnly()&&std::abs(archived.editor()->dimensionResult(dimension).value-10)<1e-7,"prima proiezione archiviata resta a 10 mm");
     require(archived.document().sheet.annotations.size()==2&&archived.document().sheet.annotations[0].text=="NOTA TECNICA A"&&archived.document().sheet.annotations[0].width==60&&archived.document().sheet.annotations[1].text==leaderA.text,"snapshot A conserva testo e impaginazione senza futuro B");
     require(distance(archived.editor()->annotationResult(leaderA.id).point,anchorA)<1e-7,"richiamo storico risolto sul modello A esatto");
     require(!archived.editor()->findChild<QAction *>("drawingAddNoteAction")->isEnabled()&&!archived.editor()->findChild<QAction *>("drawingAddLeaderAction")->isEnabled()&&!archived.editor()->updateAnnotation(noteB).isEmpty(),"storico protegge annotazioni");
     require(archived.document().sheet.axes.size()==1&&archived.document().sheet.axes[0].visible&&archived.document().sheet.axes[0].extension==2,"storico A conserva asse e proprieta originali senza futuro B");
     require(archived.editor()->axisResult(modelAxis.id).status==DrawingDimensionStatus::Ready&&std::abs(Kernel::distance(archived.editor()->axisResult(modelAxis.id).first,archived.editor()->axisResult(modelAxis.id).second)-10)<1e-7,"asse storico A conserva geometria del modello A");
     require(archived.document().sheet.views[0].showCenterMarks&&!archived.document().sheet.views[0].showCenterLines,"storico A conserva centri senza assi futuri B");
     require(archived.editor()->pageContext().projectRevisions.size()==1&&archived.document().sheet.revisionTable.notes.size()==1&&!archived.document().sheet.views[0].showMesh,"storico A conserva solo tabella/modello/visibilita' originali, senza futuro B");
     require(archived.document().sheet.views[1].viewId==derivedId&&archived.document().sheet.views[1].relation.parentId==view.id&&archived.document().sheet.views[2].viewId==technicalId,"relazioni madre/figlie persistite nello storico A");
     require(archived.document().sheet.drawing.views[2].sectionStyle==DrawingSectionStyle::Full&&archived.document().sheet.views[2].relation.hatch&&!archived.document().sheet.views[2].relation.reverse&&std::abs(archived.editor()->dimensionResult(technicalDimensionId).value-10)<1e-7,"sezione A mantiene verso, tratteggio e quota 10 contro B a 14");
     require(!archived.editor()->findChild<QAction *>("drawingTechnicalSectionAction")->isEnabled(),"creazione sezioni bloccata nello storico");
     require(!archived.editor()->findChild<QAction *>("drawingCenterMarksAction")->isEnabled()&&!archived.editor()->findChild<QAction *>("drawingCenterLinesAction")->isEnabled(),"assi e centri non modificabili nella copia storica");
     require(archived.editor()->pageContext().drawingRevision=="A"&&archived.editor()->pageContext().modelRevision=="M-A"&&archived.document().sheet.titleBlock.code=="FC-A"&&archived.document().sheet.size==QSizeF(297,210),"cartiglio storico conserva revisioni A e metadati originali");
     require(archived.findChild<QAction *>("drawingExportPdfAction")->isEnabled()&&!archived.editor()->findChild<QAction *>("drawingPageSettingsAction")->isEnabled(),"storico esportabile ma pagina non modificabile");
     const auto pdfA=output.isEmpty()?temporary.filePath("revision-a.pdf"):output+"/revision-a.pdf";clientOk(archived.exportPdfPath(pdfA,pdfReport));require(clientRead(pdfA).startsWith("%PDF-"),"PDF della revisione storica A");
     require(archived.document().sheet.views[0].showHiddenLines&&!archived.editor()->findChild<QAction *>("drawingHiddenLinesAction")->isEnabled(),"revisione A conserva le linee nascoste mostrate in sola lettura");
     require(std::abs(archived.editor()->dimensionResult(snappedId).value-10)<1e-7&&!archived.editor()->findChild<QAction *>("drawingSmartDimensionAction")->isEnabled(),"snap storici sul modello A e quotatura bloccata");
     require(!archived.editor()->findChild<QAction *>("drawingEditViewAction")->isEnabled()&&archived.document().sheet.views[0].title=="Vista superiore modificata","modifica vista conservata nello storico in sola lettura");
     clientOk(archived.savePath(copy));require(archived.editor()->findChild<QAction *>("drawingEditViewAction")->isEnabled(),"salva copia reinstalla editor e azioni modificabili");}
    {DrawingWindow reopened;clientOk(reopened.openPath(copy));require(reopened.document().sheet.style==styleA&&reopened.document().sheet.views[0].titleOffset==titleA,"copia indipendente conserva stile e nome A");require(presentationOf(reopened.editor()->sheet())==toleranceA&&formattedQuota(reopened.editor()).contains("[10.100 / 9.980]"),"copia indipendente conserva tolleranze e modello A");require(reopened.document().sheet.annotations.size()==2&&reopened.document().sheet.annotations[0].text=="NOTA TECNICA A"&&distance(reopened.editor()->annotationResult(leaderA.id).point,anchorA)<1e-7,"copia indipendente conserva note e modello A");require(reopened.document().sheet.views[2].relation.parentId==view.id&&reopened.document().sheet.views[2].relation.hatch&&std::abs(reopened.editor()->dimensionResult(technicalDimensionId).value-10)<1e-7,"copia indipendente mantiene relazioni e sezione A");require(reopened.editor()->pageContext().drawingRevision=="Copia di lavoro"&&reopened.editor()->pageContext().modelRevision=="M-A"&&reopened.document().sheet.titleBlock.code=="FC-A","copia indipendente aggiorna solo la revisione della tavola nel cartiglio");require(reopened.document().sheet.views[0].showHiddenLines,"copia storica conserva la visibilita' della revisione A");require(reopened.document().sheet.views[0].showCenterMarks&&!reopened.document().sheet.views[0].showCenterLines,"copia indipendente conserva assi/centri della revisione A");require(reopened.document().sheet.drawing.views[0].sectionReferences&&std::abs(reopened.editor()->dimensionResult(dimension).value-10)<1e-7,"copia storica riaperta con sezioni e quote proiettate");require(reopened.document().sheet.drawing.dimensions[1].references.size()==2&&std::abs(reopened.editor()->dimensionResult(snappedId).value-10)<1e-7,"snap associativi riaperti dalla copia storica");}
    auto automatic=working;forkDocumentIdentity(automatic);const auto automaticPath=temporary.filePath("automatico.prt");clientOk(saveDocumentFile(automaticPath,automatic));RevisionRecord autoOne,autoTwo;clientOk(checkoutWorkspaceDocument(automaticPath,automatic,"modellatore"));clientOk(checkinWorkspaceDocument(automaticPath,automatic,{},"prima",autoOne));clientOk(checkoutWorkspaceDocument(automaticPath,automatic,"modellatore"));clientOk(checkinWorkspaceDocument(automaticPath,automatic,{},"seconda",autoTwo));require(autoOne.technicalRevision=="1"&&autoTwo.technicalRevision=="2","numerazione automatica reale anche del modello 3D");
}
}
