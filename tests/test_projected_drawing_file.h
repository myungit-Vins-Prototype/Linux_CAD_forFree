#pragma once
#include "../cad_drawing_dimension.h"
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
    clientOk(window.pinModelRevision(modelA.version));clientOk(window.savePath(drawingPath));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"A","vista e sezione"));
    require(window.editor()->pageContext().drawingRevision=="Lavoro (base A)"&&window.editor()->pageContext().modelRevision=="M-A","cartiglio distingue copia di lavoro della tavola e revisione del modello");
    QVector<RevisionRecord> history;clientOk(workspaceDrawingHistory(drawingPath,window.document(),history));require(history.size()==1,"check-in della nuova vista");const auto first=history[0].version;
    working.extrusions[0].primitive.size[0]=14;working.extrusions[0].cachedGeometry=false;{CadViewport viewport;viewport.loadDocument(working);working=viewport.currentDocument();}
    clientOk(saveDocumentFile(sourcePath,working));clientOk(checkoutWorkspaceDocument(sourcePath,working,"modellatore"));RevisionRecord modelB;clientOk(checkinWorkspaceDocument(sourcePath,working,"M-B","",modelB));
    clientOk(window.pinModelRevision(modelB.version));require(std::abs(window.editor()->dimensionResult(dimension).value-14)<1e-7,"quota del bordo segue genealogia al modello B");
    require(std::abs(window.editor()->dimensionResult(snappedId).value-14)<1e-7,"entrambi gli snap seguono la genealogia al modello B");
    require(!window.editor()->canUndo()&&!window.editor()->canRedo(),"collegamento modello B separa la cronologia del layout");
    require(window.editor()->pageContext().modelRevision=="M-B","cartiglio aggiornato al modello esatto B");
    block.code="FC-B";block.author="Autore B";clientOk(window.editor()->setPageSettings(window.editor()->sheet().title,{420,297},block));
    clientOk(window.editor()->setHiddenLinesVisible(view.id,false));QApplication::processEvents();
    clientOk(window.editor()->moveView(view.id,window.editor()->sheet().views[0].position+QPointF(1,2)));clientOk(window.editor()->undo());
    require(window.isModified()&&std::abs(window.editor()->dimensionResult(dimension).value-14)<1e-7,"undo del layout non pulisce il cambio sorgente non salvato");
    clientOk(window.savePath(drawingPath));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkout,"disegnatore"));clientOk(window.runRevisionAction(DrawingWindow::RevisionAction::Checkin,"B","allungamento"));
    require(!window.document().sheet.views[0].showHiddenLines,"revisione B con tratti nascosti esclusi");
    DrawingPdfReport pdfReport;const auto output=qEnvironmentVariable("FORGECAD_PAGE_REVISION_OUTPUT");if(!output.isEmpty())require(QDir().mkpath(output),"cartella PDF revisioni");
    const auto pdfB=output.isEmpty()?temporary.filePath("revision-b.pdf"):output+"/revision-b.pdf";clientOk(window.exportPdfPath(pdfB,pdfReport));require(clientRead(pdfB).startsWith("%PDF-"),"PDF della copia di lavoro B");
    const auto modelBytes=clientRead(sourcePath);require(!window.exportPdfPath(sourcePath,pdfReport).isEmpty()&&clientRead(sourcePath)==modelBytes,"export non sovrascrive il modello");
    const auto copy=temporary.filePath("copia.drw");
    {DrawingWindow archived;clientOk(archived.openRevision(drawingPath,first));require(archived.editor()->isReadOnly()&&std::abs(archived.editor()->dimensionResult(dimension).value-10)<1e-7,"prima proiezione archiviata resta a 10 mm");
     require(archived.editor()->pageContext().drawingRevision=="A"&&archived.editor()->pageContext().modelRevision=="M-A"&&archived.document().sheet.titleBlock.code=="FC-A"&&archived.document().sheet.size==QSizeF(297,210),"cartiglio storico conserva revisioni A e metadati originali");
     require(archived.findChild<QAction *>("drawingExportPdfAction")->isEnabled()&&!archived.editor()->findChild<QAction *>("drawingPageSettingsAction")->isEnabled(),"storico esportabile ma pagina non modificabile");
     const auto pdfA=output.isEmpty()?temporary.filePath("revision-a.pdf"):output+"/revision-a.pdf";clientOk(archived.exportPdfPath(pdfA,pdfReport));require(clientRead(pdfA).startsWith("%PDF-"),"PDF della revisione storica A");
     require(archived.document().sheet.views[0].showHiddenLines&&!archived.editor()->findChild<QAction *>("drawingHiddenLinesAction")->isEnabled(),"revisione A conserva le linee nascoste mostrate in sola lettura");
     require(std::abs(archived.editor()->dimensionResult(snappedId).value-10)<1e-7&&!archived.editor()->findChild<QAction *>("drawingSmartDimensionAction")->isEnabled(),"snap storici sul modello A e quotatura bloccata");
     require(!archived.editor()->findChild<QAction *>("drawingEditViewAction")->isEnabled()&&archived.document().sheet.views[0].title=="Vista superiore modificata","modifica vista conservata nello storico in sola lettura");
     clientOk(archived.savePath(copy));require(archived.editor()->findChild<QAction *>("drawingEditViewAction")->isEnabled(),"salva copia reinstalla editor e azioni modificabili");}
    {DrawingWindow reopened;clientOk(reopened.openPath(copy));require(reopened.editor()->pageContext().drawingRevision=="Copia di lavoro"&&reopened.editor()->pageContext().modelRevision=="M-A"&&reopened.document().sheet.titleBlock.code=="FC-A","copia indipendente aggiorna solo la revisione della tavola nel cartiglio");require(reopened.document().sheet.views[0].showHiddenLines,"copia storica conserva la visibilita' della revisione A");require(reopened.document().sheet.drawing.views[0].sectionReferences&&std::abs(reopened.editor()->dimensionResult(dimension).value-10)<1e-7,"copia storica riaperta con sezioni e quote proiettate");require(reopened.document().sheet.drawing.dimensions[1].references.size()==2&&std::abs(reopened.editor()->dimensionResult(snappedId).value-10)<1e-7,"snap associativi riaperti dalla copia storica");}
}
}
