#include "cad_drawing_window.h"
#include "cad_icons.h"
#include "cad_menu.h"
#include "cad_toolbar.h"
#include "cad_workspace.h"
#include <QDate>
#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QStatusBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <stdexcept>

namespace ForgeCad {
DrawingWindow::DrawingWindow(QWidget *parent):QMainWindow(parent,Qt::Window){
    setObjectName("drawingWindow");resize(1100,780);
    auto *file=addCadMenu(menuBar(),QStringLiteral("File"));
    auto *saveAction=file->addAction(QStringLiteral("Salva tavola"));saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction,&QAction::triggered,this,[this]{save(false);});
    auto *saveAs=file->addAction(QStringLiteral("Salva tavola con nome..."));saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs,&QAction::triggered,this,[this]{save(true);});
    auto *exportPdf=file->addAction(QStringLiteral("Esporta PDF..."));exportPdf->setObjectName("drawingExportPdfAction");exportPdf->setIcon(commandIcon("drawingPdf"));exportPdf->setToolTip(QStringLiteral("Esporta il foglio in PDF vettoriale alla scala indicata, senza selezioni e anteprime."));
    connect(exportPdf,&QAction::triggered,this,[this]{const QFileInfo source(path_.isEmpty()?sourcePath_:path_);auto path=QFileDialog::getSaveFileName(this,QStringLiteral("Esporta tavola in PDF"),source.absolutePath()+"/"+source.completeBaseName()+".pdf",QStringLiteral("Documento PDF (*.pdf)"));if(path.isEmpty())return;if(QFileInfo(path).suffix().isEmpty())path+=".pdf";DrawingPdfReport result;const auto error=exportPdfPath(path,result);if(!error.isEmpty()){report(error);return;}statusBar()->showMessage(QStringLiteral("PDF creato: %1").arg(path)+(result.warnings.isEmpty()?QString():QStringLiteral(" · ")+result.warnings.join(QStringLiteral(" "))),15000);});
    file->addSeparator();addRevisionMenus(file);
    editMenu_=addCadMenu(menuBar(),QStringLiteral("Modifica"));insertMenu_=addCadMenu(menuBar(),QStringLiteral("Inserisci"));
    dimensionMenu_=addCadMenu(menuBar(),QStringLiteral("Quote"));viewMenu_=addCadMenu(menuBar(),QStringLiteral("Visualizza"));
    auto *source=addToolBar(QStringLiteral("Documento e modello"));source->setObjectName("drawingDocumentBar");configureCommandBar(source);
    saveAction->setObjectName("drawingSaveAction");saveAction->setIcon(commandIcon("save"));saveAs->setIcon(commandIcon("save"));
    source->addAction(saveAction);source->addAction(exportPdf);source->addSeparator();
    updateAction_=source->addAction(QStringLiteral("Aggiorna dal .prt salvato"));updateAction_->setObjectName("drawingReloadSourceAction");
    connect(updateAction_,&QAction::triggered,this,[this]{report(updateFromSource());});
    auto *locate=source->addAction(QStringLiteral("Individua modello..."));locate->setObjectName("drawingLocateSourceAction");
    connect(locate,&QAction::triggered,this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("Individua il modello della tavola"),sourcePath_,QStringLiteral("Modello ForgeCAD (*.prt)"));
        if(!path.isEmpty())report(updateFromSource(path));
    });
    updateAction_->setIcon(commandIcon("rebuild"));updateAction_->setToolTip(QStringLiteral("Rilegge il .prt salvato e ricalcola viste e quote della tavola."));locate->setIcon(commandIcon("open"));
    auto *modelMenu=findChild<QMenu *>("drawingModelMenu");modelMenu->addSeparator();modelMenu->addAction(updateAction_);modelMenu->addAction(locate);
    sourceStatus_=new QLabel(this);sourceStatus_->setObjectName("drawingSourceStatus");sourceStatus_->setWordWrap(false);
    sourceStatus_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);sourceStatus_->setMinimumWidth(0);statusBar()->addWidget(sourceStatus_,1);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("  ForgeCAD / Tavola 2D  "),this));
    refreshTitle();
}
void DrawingWindow::report(const QString &error){if(!error.isEmpty())QMessageBox::warning(this,QStringLiteral("Tavola 2D"),error);}
void DrawingWindow::refreshTitle(){
    sourceStatus_->setToolTip(sourceStatus_->text());
    const auto checkin=drawing_.sheet.drawing.version.checkinId;
    setWindowTitle((path_.isEmpty()?QStringLiteral("Nuova tavola"):QFileInfo(path_).fileName())+(modified_?QStringLiteral(" *"):QString())
        +(checkin.isNull()?QString():QStringLiteral(" [check-in %1]").arg(checkin.toString(QUuid::WithoutBraces).left(8)))+QStringLiteral(" — ForgeCAD"));
    findChild<QAction *>("drawingExportPdfAction")->setEnabled(editor_&&sourceReady_&&!busy_);
    findChild<QAction *>("drawingSaveAction")->setEnabled(editor_&&checkin.isNull()&&!busy_);
    findChild<QAction *>("drawingLocateSourceAction")->setEnabled(editor_&&checkin.isNull()&&!busy_);
    for(auto *action:revisionMutationActions_)action->setEnabled(editor_&&checkin.isNull()&&!busy_);
}
void DrawingWindow::installEditor(){
    auto *container=new QWidget(this);auto *layout=new QVBoxLayout(container);layout->setContentsMargins(4,4,4,4);
    DrawingSheetEditor *next=nullptr;
    try { next=new DrawingSheetEditor(drawing_.sheet,model_,container); }
    catch(const std::exception &error){
        sourceReady_=false;model_={};model_.snapshot.version=drawing_.sheet.drawing.model;
        sourceStatus_->setText(QStringLiteral("Impossibile generare le viste: %1").arg(QString::fromUtf8(error.what())));
        next=new DrawingSheetEditor(drawing_.sheet,model_,container);
    }
    next->setReadOnly(!sourceReady_);
    next->setChangedCallback([this](const DrawingSheet &sheet){drawing_.sheet=sheet;modified_=containerDirty_||editor_->isModified();refreshTitle();});
    // Il comando del contenitore verifica percorso, identita' e impronta.
    next->findChild<QAction *>("drawingUpdateModelAction")->setVisible(false);
    layout->addWidget(next,1);auto *previous=takeCentralWidget();setCentralWidget(container);editor_=next;delete previous;
    const auto populate=[next](QMenu *menu,std::initializer_list<const char *> names){menu->clear();for(auto name:names)menu->addAction(next->findChild<QAction *>(name));};
    populate(editMenu_,{"drawingPageSettingsAction","drawingUndoAction","drawingRedoAction","drawingEditViewAction","drawingAlignViewAction","drawingDeleteSelectionAction"});
    populate(insertMenu_,{"drawingAddProjectionAction","drawingAddViewAction"});
    populate(dimensionMenu_,{"drawingSmartDimensionAction","drawingEditDimensionAction","drawingEndpointDimensionAction","drawingHorizontalDimensionAction","drawingVerticalDimensionAction","drawingLengthDimensionAction"});
    auto *references=addCadMenu(dimensionMenu_,QStringLiteral("Riferimenti del piano"));
    for(auto name:{"drawingOriginReferenceAction","drawingAxisXReferenceAction","drawingAxisYReferenceAction"})references->addAction(next->findChild<QAction *>(name));
    populate(viewMenu_,{"drawingHiddenLinesAction","drawingFitAction"});viewMenu_->addSeparator();viewMenu_->addAction(next->findChild<QDockWidget *>("drawingTreeDock")->toggleViewAction());
    viewMenu_->addAction(next->findChild<QToolBar *>("drawingCommandBar")->toggleViewAction());viewMenu_->addAction(findChild<QToolBar *>("drawingDocumentBar")->toggleViewAction());
    refreshPageContext();
    updateAction_->setEnabled(drawing_.sheet.drawing.version.checkinId.isNull());
    if(sourceReady_){
        const auto pinned=drawing_.sheet.drawing.model.checkinId;
        sourceStatus_->setText(pinned.isNull()?QStringLiteral("Modello: %1. Le modifiche al 3D vengono lette solo dopo il salvataggio e l'aggiornamento esplicito della tavola.").arg(sourcePath_)
            :QStringLiteral("Modello fissato al check-in %1. Il comando Modello consente di scegliere un'altra revisione o tornare alla copia di lavoro.").arg(pinned.toString(QUuid::WithoutBraces)));
    }
}
QString DrawingWindow::createFromModel(const QString &modelPath){
    if(editor_)return QStringLiteral("Questa finestra contiene gia' una tavola.");
    const auto resolved=canonicalDocumentPath(modelPath);DocumentState source;
    auto error=loadDocumentFile(resolved,source);if(!error.isEmpty())return error;
    DrawingFile prepared;prepared.sheet.title=QFileInfo(resolved).completeBaseName();prepared.sheet.drawing.model=source.version;prepared.modelPath=resolved;prepared.sheet.titleBlock.visible=true;prepared.sheet.titleBlock.date=QDate::currentDate().toString(Qt::ISODate);
    error=drawingModelDefinitionHash(source,prepared.modelDefinitionHash);if(!error.isEmpty())return error;
    DrawingEditorModel geometry;error=prepareDrawingEditorModel(source,geometry);if(!error.isEmpty())return error;
    if(geometry.choices.isEmpty())return QStringLiteral("Il modello non contiene corpi o superfici disponibili per una tavola.");
    drawing_=std::move(prepared);model_=std::move(geometry);sourcePath_=resolved;sourceReady_=true;containerDirty_=true;modified_=true;installEditor();refreshTitle();return {};
}
QString DrawingWindow::openPath(const QString &path){
    if(editor_)return QStringLiteral("Questa finestra contiene gia' una tavola.");
    auto candidate=std::make_unique<DocumentFileLock>(path);auto error=candidate->acquire();if(!error.isEmpty())return error;
    DrawingFile prepared;error=loadDrawingFile(candidate->path(),prepared);if(!error.isEmpty())return error;
    const auto resolved=candidate->path();lock_=std::move(candidate);loadContents(resolved,std::move(prepared));return {};
}
void DrawingWindow::loadContents(const QString &path,DrawingFile prepared){
    DocumentState source;QString resolved;bool changed=false;
    auto error=loadDrawingModel(path,prepared,source,resolved,changed);
    DrawingEditorModel geometry;geometry.snapshot.version=prepared.sheet.drawing.model;
    if(error.isEmpty()&&!changed)error=prepareDrawingEditorModel(source,geometry);
    const bool available=error.isEmpty()&&!changed;
    if(!available){geometry={};geometry.snapshot.version=prepared.sheet.drawing.model;}
    drawing_=std::move(prepared);model_=std::move(geometry);path_=path;
    sourcePath_=resolved.isEmpty()?QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelPath):resolved;
    sourceArchivePath_=drawing_.modelArchivePath.isEmpty()?QString():QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelArchivePath);
    sourceReady_=available;containerDirty_=false;modified_=false;
    sourceStatus_->setText(changed?QStringLiteral("Il modello salvato e' cambiato: premere Aggiorna dal .prt salvato per ricalcolare viste e quote."):
        QStringLiteral("Modello non disponibile: %1. Individuare il modello per verificare le quote.").arg(error));
    installEditor();refreshTitle();
}
QString DrawingWindow::updateFromSource(const QString &overridePath){
    if(!editor_||!drawing_.sheet.drawing.version.checkinId.isNull())return QStringLiteral("La tavola archiviata e' in sola lettura: creare prima una copia indipendente.");
    DocumentState source;QString resolved;bool changed=false;
    // Per una tavola nuova il riferimento e' assoluto; per quella salvata e' relativo.
    const auto context=path_.isEmpty()?sourcePath_:path_;
    auto error=loadDrawingModel(context,drawing_,source,resolved,changed,overridePath);if(!error.isEmpty())return error;
    QByteArray digest;error=drawingModelDefinitionHash(source,digest);if(!error.isEmpty())return error;
    DrawingEditorModel geometry;error=prepareDrawingEditorModel(source,geometry);if(!error.isEmpty())return error;
    const bool wasReadOnly=editor_->isReadOnly();editor_->setReadOnly(false);error=editor_->updateModel(geometry);
    if(!error.isEmpty()){editor_->setReadOnly(wasReadOnly);return error;}
    model_=std::move(geometry);drawing_.modelDefinitionHash=digest;sourcePath_=resolved;
    drawing_.modelPath=path_.isEmpty()?resolved:QDir(QFileInfo(path_).absolutePath()).relativeFilePath(resolved);
    sourceReady_=true;containerDirty_=true;modified_=true;
    sourceStatus_->setText(QStringLiteral("Modello aggiornato da: %1").arg(sourcePath_));refreshPageContext();refreshTitle();return {};
}
QString DrawingWindow::savePath(const QString &path){
    if(!editor_)return QStringLiteral("Nessuna tavola da salvare.");
    const auto resolved=canonicalDocumentPath(path);
    if(resolved==canonicalDocumentPath(sourcePath_)||QFileInfo(resolved).suffix().compare("drw",Qt::CaseInsensitive)!=0)
        return QStringLiteral("La destinazione deve essere un file .drw distinto dal modello 3D.");
    const bool different=!path_.isEmpty()&&resolved!=path_;
    auto prepared=drawing_;
    if(different)forkDrawingIdentity(prepared);
    if(!prepared.sheet.drawing.version.checkinId.isNull())return QStringLiteral("Usare Salva con nome per una copia indipendente della tavola archiviata.");
    prepared.modelPath=QDir(QFileInfo(resolved).absolutePath()).relativeFilePath(sourcePath_);
    if(!sourceArchivePath_.isEmpty())prepared.modelArchivePath=QDir(QFileInfo(resolved).absolutePath()).relativeFilePath(sourceArchivePath_);
    std::unique_ptr<DocumentFileLock> candidate;auto *lock=lock_.get();
    if(!lock||lock->path()!=resolved){candidate=std::make_unique<DocumentFileLock>(resolved);const auto error=candidate->acquire();if(!error.isEmpty())return error;lock=candidate.get();}
    const auto error=saveDrawingFile(resolved,prepared,lock);if(!error.isEmpty())return error;
    if(candidate)lock_=std::move(candidate);
    drawing_=std::move(prepared);path_=resolved;containerDirty_=false;modified_=false;
    if(different)installEditor();
    editor_->markSaved();refreshPageContext();refreshTitle();return {};
}
void DrawingWindow::refreshPageContext(){
    if(!editor_)return;
    auto context=defaultDrawingPageContext(drawing_.sheet);context.modelName=QFileInfo(drawing_.modelPath).fileName();WorkspacePaths workspace;const auto located=locateDocumentWorkspace(path_.isEmpty()?sourcePath_:path_,workspace);
    const auto label=[&](const DocumentVersion &version,const QString &directory,const QUuid &expectedArchive){
        const auto fallback=QStringLiteral("Check-in %1").arg(version.checkinId.toString(QUuid::WithoutBraces));if(directory.isEmpty()||version.checkinId.isNull())return fallback;
        const QString key=directory+"/"+version.documentId.toString()+"/"+version.checkinId.toString();if(revisionLabels_.contains(key))return revisionLabels_.value(key);RevisionStore store(directory);
        if(!expectedArchive.isNull()){QUuid actual;if(!store.archiveIdentity(actual).isEmpty()||actual!=expectedArchive)return fallback;}
        QVector<RevisionRecord> records;if(!store.history(version.documentId,records).isEmpty())return fallback;
        for(const auto &record:records)if(record.version.checkinId==version.checkinId){revisionLabels_[key]=record.technicalRevision;return record.technicalRevision;}
        return fallback;
    };
    const auto archive=located.isEmpty()?workspace.revisions:QString();
    if(!context.drawingVersion.checkinId.isNull())context.drawingRevision=label(context.drawingVersion,archive,{});
    else if(!drawing_.baseCheckinId.isNull())context.drawingRevision=QStringLiteral("Lavoro (base %1)").arg(label({context.drawingVersion.documentId,drawing_.baseCheckinId},archive,{}));
    if(!context.modelVersion.checkinId.isNull())context.modelRevision=label(context.modelVersion,sourceArchivePath_.isEmpty()?archive:sourceArchivePath_,drawing_.modelArchiveId);
    editor_->setPageContext(std::move(context));
}
QString DrawingWindow::exportPdfPath(const QString &path,DrawingPdfReport &report)const{
    if(!editor_||busy_||!sourceReady_)return QStringLiteral("Modello non verificato: aggiornare la tavola prima di esportare.");
    const auto destination=canonicalDocumentPath(path);
    if(destination==canonicalDocumentPath(path_)||destination==canonicalDocumentPath(sourcePath_))return QStringLiteral("Il PDF deve essere distinto dalla tavola e dal modello sorgente.");
    return editor_->exportPdf(path,report);
}
bool DrawingWindow::save(bool askPath){
    auto path=path_;
    if(askPath||path.isEmpty()){
        const auto initial=path.isEmpty()?QFileInfo(sourcePath_).absolutePath()+"/"+drawing_.sheet.title+".drw":path;
        path=QFileDialog::getSaveFileName(this,QStringLiteral("Salva tavola"),initial,QStringLiteral("Tavola ForgeCAD (*.drw)"));
        if(path.isEmpty())return false;
        if(QFileInfo(path).suffix().compare("drw",Qt::CaseInsensitive)!=0)path+=".drw";
    }
    const auto error=savePath(path);report(error);return error.isEmpty();
}
void DrawingWindow::closeEvent(QCloseEvent *event){
    if(busy_){event->ignore();return;}
    if(modified_){
        const auto choice=QMessageBox::warning(this,QStringLiteral("Tavola 2D"),QStringLiteral("Salvare le modifiche alla tavola?"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(choice==QMessageBox::Cancel||(choice==QMessageBox::Save&&!save(false))){event->ignore();return;}
    }
    lock_.reset();event->accept();
}
}
