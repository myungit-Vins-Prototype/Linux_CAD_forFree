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
void projectedDrawingFileTests(){
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
    for(std::size_t i=0;i<curves.size();++i)if(curves[i].kind==DrawingCurveKind::ProjectedEdge&&std::abs(arcLength(*curves[i].curve,curves[i].range)-10)<1e-7){chosen=int(i);break;}
    require(chosen>=0,"bordo proiettato da quotare su .prt");for(auto *item:window.editor()->canvas()->scene()->items())if(item->data(0).toString()=="projection"&&item->data(2).toInt()==chosen)item->setSelected(true);
    clientOk(window.editor()->addSelectedDimension(DrawingMeasure::HorizontalDistance));const auto dimension=window.document().sheet.drawing.dimensions[0].id;
    auto snapped=dimensionOfCurve(view,curves[std::size_t(chosen)]);snapped.measure=DrawingMeasure::Distance;
    DrawingModelSnapshot modelSnapshot;clientOk(drawingSnapshotOfDocument(working,{working.extrusions[0].featureId},modelSnapshot));
    snapped.references={drawingAttachment(view,curves[std::size_t(chosen)],modelSnapshot,DrawingPointRole::Start),
                        drawingAttachment(view,curves[std::size_t(chosen)],modelSnapshot,DrawingPointRole::End)};
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
    clientOk(window.savePath(drawingPath));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"A","vista e sezione"));
    require(window.editor()->pageContext().drawingRevision=="Lavoro (base A)"&&window.editor()->pageContext().modelRevision=="M-A","cartiglio distingue copia di lavoro della tavola e revisione del modello");
    QVector<RevisionRecord> history;clientOk(workspaceDrawingHistory(drawingPath,window.document(),history));require(history.size()==1,"check-in della nuova vista");const auto first=history[0].version;
    working.extrusions[0].primitive.size[0]=14;working.extrusions[0].cachedGeometry=false;{CadViewport viewport;viewport.loadDocument(working);working=viewport.currentDocument();}
    clientOk(saveDocumentFile(sourcePath,working));clientOk(checkoutWorkspaceDocument(sourcePath,working,"modellatore"));RevisionRecord modelB;clientOk(checkinWorkspaceDocument(sourcePath,working,"M-B","",modelB));
    clientOk(window.pinModelRevision(modelB.version));require(std::abs(window.editor()->dimensionResult(dimension).value-14)<1e-7,"quota del bordo segue genealogia al modello B");
    require(std::abs(window.editor()->dimensionResult(snappedId).value-14)<1e-7,"entrambi gli snap seguono la genealogia al modello B");
    require(!window.editor()->canUndo()&&!window.editor()->canRedo(),"collegamento modello B separa la cronologia del layout");
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
    clientOk(window.savePath(drawingPath));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));
    require(!window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"A","duplicato").isEmpty(),"etichetta tecnica duplicata rifiutata prima del pendente");WorkspaceRevisionStatus failedStatus;clientOk(workspaceDrawingRevisionStatus(drawingPath,window.document(),failedStatus));require(failedStatus.hasSession&&!failedStatus.hasPending,"duplicato non blocca la copia con un pendente non pubblicabile");
    clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,{},"allungamento"));QVector<RevisionRecord> numbered;clientOk(workspaceDrawingHistory(drawingPath,window.document(),numbered));require(numbered.size()==2&&numbered.back().technicalRevision=="2","nuovo check-in numerato automaticamente");
    require(window.findChild<QTableWidget *>("drawingRevisionHistory")->rowCount()==2,"pannello aggiornato dopo check-in automatico");
    require(!window.document().sheet.views[0].showHiddenLines,"revisione B con tratti nascosti esclusi");
    DrawingPdfReport pdfReport;const auto output=qEnvironmentVariable("FORGECAD_PAGE_REVISION_OUTPUT");if(!output.isEmpty())require(QDir().mkpath(output),"cartella PDF revisioni");
    const auto pdfB=output.isEmpty()?temporary.filePath("revision-b.pdf"):output+"/revision-b.pdf";clientOk(window.exportPdfPath(pdfB,pdfReport));require(clientRead(pdfB).startsWith("%PDF-"),"PDF della copia di lavoro B");
    if(!output.isEmpty()){window.resize(1500,950);window.show();QApplication::processEvents();window.editor()->fitSheet();require(window.grab().save(output+"/pannello-revisioni.png"),"anteprima finestra con pannello revisioni");}
    const auto modelBytes=clientRead(sourcePath);require(!window.exportPdfPath(sourcePath,pdfReport).isEmpty()&&clientRead(sourcePath)==modelBytes,"export non sovrascrive il modello");
    const auto copy=temporary.filePath("copia.drw");
    {DrawingWindow archived;clientOk(archived.openRevision(drawingPath,first));require(archived.editor()->isReadOnly()&&std::abs(archived.editor()->dimensionResult(dimension).value-10)<1e-7,"prima proiezione archiviata resta a 10 mm");
     require(archived.editor()->pageContext().projectRevisions.size()==1&&archived.document().sheet.revisionTable.notes.size()==1&&!archived.document().sheet.views[0].showMesh,"storico A conserva solo tabella/modello/visibilita' originali, senza futuro B");
     require(archived.document().sheet.views[1].viewId==derivedId&&archived.document().sheet.views[1].relation.parentId==view.id&&archived.document().sheet.views[2].viewId==technicalId,"relazioni madre/figlie persistite nello storico A");
     require(archived.document().sheet.drawing.views[2].sectionStyle==DrawingSectionStyle::Full&&archived.document().sheet.views[2].relation.hatch&&!archived.document().sheet.views[2].relation.reverse&&std::abs(archived.editor()->dimensionResult(technicalDimensionId).value-10)<1e-7,"sezione A mantiene verso, tratteggio e quota 10 contro B a 14");
     require(!archived.editor()->findChild<QAction *>("drawingTechnicalSectionAction")->isEnabled(),"creazione sezioni bloccata nello storico");
     require(archived.editor()->pageContext().drawingRevision=="A"&&archived.editor()->pageContext().modelRevision=="M-A"&&archived.document().sheet.titleBlock.code=="FC-A"&&archived.document().sheet.size==QSizeF(297,210),"cartiglio storico conserva revisioni A e metadati originali");
     require(archived.findChild<QAction *>("drawingExportPdfAction")->isEnabled()&&!archived.editor()->findChild<QAction *>("drawingPageSettingsAction")->isEnabled(),"storico esportabile ma pagina non modificabile");
     const auto pdfA=output.isEmpty()?temporary.filePath("revision-a.pdf"):output+"/revision-a.pdf";clientOk(archived.exportPdfPath(pdfA,pdfReport));require(clientRead(pdfA).startsWith("%PDF-"),"PDF della revisione storica A");
     require(archived.document().sheet.views[0].showHiddenLines&&!archived.editor()->findChild<QAction *>("drawingHiddenLinesAction")->isEnabled(),"revisione A conserva le linee nascoste mostrate in sola lettura");
     require(std::abs(archived.editor()->dimensionResult(snappedId).value-10)<1e-7&&!archived.editor()->findChild<QAction *>("drawingSmartDimensionAction")->isEnabled(),"snap storici sul modello A e quotatura bloccata");
     require(!archived.editor()->findChild<QAction *>("drawingEditViewAction")->isEnabled()&&archived.document().sheet.views[0].title=="Vista superiore modificata","modifica vista conservata nello storico in sola lettura");
     clientOk(archived.savePath(copy));require(archived.editor()->findChild<QAction *>("drawingEditViewAction")->isEnabled(),"salva copia reinstalla editor e azioni modificabili");}
    {DrawingWindow reopened;clientOk(reopened.openPath(copy));require(reopened.document().sheet.views[2].relation.parentId==view.id&&reopened.document().sheet.views[2].relation.hatch&&std::abs(reopened.editor()->dimensionResult(technicalDimensionId).value-10)<1e-7,"copia indipendente mantiene relazioni e sezione A");require(reopened.editor()->pageContext().drawingRevision=="Copia di lavoro"&&reopened.editor()->pageContext().modelRevision=="M-A"&&reopened.document().sheet.titleBlock.code=="FC-A","copia indipendente aggiorna solo la revisione della tavola nel cartiglio");require(reopened.document().sheet.views[0].showHiddenLines,"copia storica conserva la visibilita' della revisione A");require(reopened.document().sheet.drawing.views[0].sectionReferences&&std::abs(reopened.editor()->dimensionResult(dimension).value-10)<1e-7,"copia storica riaperta con sezioni e quote proiettate");require(reopened.document().sheet.drawing.dimensions[1].references.size()==2&&std::abs(reopened.editor()->dimensionResult(snappedId).value-10)<1e-7,"snap associativi riaperti dalla copia storica");}
    auto automatic=working;forkDocumentIdentity(automatic);const auto automaticPath=temporary.filePath("automatico.prt");clientOk(saveDocumentFile(automaticPath,automatic));RevisionRecord autoOne,autoTwo;clientOk(checkoutWorkspaceDocument(automaticPath,automatic,"modellatore"));clientOk(checkinWorkspaceDocument(automaticPath,automatic,{},"prima",autoOne));clientOk(checkoutWorkspaceDocument(automaticPath,automatic,"modellatore"));clientOk(checkinWorkspaceDocument(automaticPath,automatic,{},"seconda",autoTwo));require(autoOne.technicalRevision=="1"&&autoTwo.technicalRevision=="2","numerazione automatica reale anche del modello 3D");
}
}
