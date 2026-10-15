#include "cad_drawing_window.h"
#include <QAction>
#include <QCloseEvent>
#include <QDir>
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
    auto *file=menuBar()->addMenu(QStringLiteral("File"));
    auto *saveAction=file->addAction(QStringLiteral("Salva tavola"));saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction,&QAction::triggered,this,[this]{save(false);});
    auto *saveAs=file->addAction(QStringLiteral("Salva tavola con nome..."));saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs,&QAction::triggered,this,[this]{save(true);});
    auto *source=addToolBar(QStringLiteral("Modello sorgente"));
    updateAction_=source->addAction(QStringLiteral("Aggiorna dal .prt salvato"));updateAction_->setObjectName("drawingReloadSourceAction");
    connect(updateAction_,&QAction::triggered,this,[this]{report(updateFromSource());});
    auto *locate=source->addAction(QStringLiteral("Individua modello..."));locate->setObjectName("drawingLocateSourceAction");
    connect(locate,&QAction::triggered,this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("Individua il modello della tavola"),sourcePath_,QStringLiteral("Modello ForgeCAD (*.prt)"));
        if(!path.isEmpty())report(updateFromSource(path));
    });
    sourceStatus_=new QLabel(this);sourceStatus_->setObjectName("drawingSourceStatus");sourceStatus_->setWordWrap(true);
    refreshTitle();
}
void DrawingWindow::report(const QString &error){if(!error.isEmpty())QMessageBox::warning(this,QStringLiteral("Tavola 2D"),error);}
void DrawingWindow::refreshTitle(){setWindowTitle((path_.isEmpty()?QStringLiteral("Nuova tavola"):QFileInfo(path_).fileName())+(modified_?QStringLiteral(" *"):QString())+QStringLiteral(" — ForgeCAD"));}
void DrawingWindow::installEditor(){
    auto *container=new QWidget(this);auto *layout=new QVBoxLayout(container);layout->setContentsMargins(4,4,4,4);
    sourceStatus_->setParent(container);layout->addWidget(sourceStatus_);
    DrawingSheetEditor *next=nullptr;
    try { next=new DrawingSheetEditor(drawing_.sheet,model_,container); }
    catch(const std::exception &error){
        sourceReady_=false;model_={};model_.snapshot.version=drawing_.sheet.drawing.model;
        sourceStatus_->setText(QStringLiteral("Impossibile generare le viste: %1").arg(QString::fromUtf8(error.what())));
        next=new DrawingSheetEditor(drawing_.sheet,model_,container);
    }
    next->setReadOnly(!sourceReady_);
    next->setChangedCallback([this](const DrawingSheet &sheet){drawing_.sheet=sheet;modified_=true;refreshTitle();});
    // Il comando del contenitore verifica percorso, identita' e impronta.
    next->findChild<QAction *>("drawingUpdateModelAction")->setVisible(false);
    layout->addWidget(next,1);auto *previous=takeCentralWidget();setCentralWidget(container);editor_=next;delete previous;
    updateAction_->setEnabled(drawing_.sheet.drawing.version.checkinId.isNull());
    if(sourceReady_)sourceStatus_->setText(QStringLiteral("Modello: %1. Le modifiche al 3D vengono lette solo dopo il salvataggio e l'aggiornamento esplicito della tavola.").arg(sourcePath_));
}
QString DrawingWindow::createFromModel(const QString &modelPath){
    if(editor_)return QStringLiteral("Questa finestra contiene gia' una tavola.");
    const auto resolved=canonicalDocumentPath(modelPath);DocumentState source;
    auto error=loadDocumentFile(resolved,source);if(!error.isEmpty())return error;
    DrawingFile prepared;prepared.sheet.title=QFileInfo(resolved).completeBaseName();prepared.sheet.drawing.model=source.version;prepared.modelPath=resolved;
    error=drawingModelDefinitionHash(source,prepared.modelDefinitionHash);if(!error.isEmpty())return error;
    DrawingEditorModel geometry;error=prepareDrawingEditorModel(source,geometry);if(!error.isEmpty())return error;
    if(geometry.choices.isEmpty())return QStringLiteral("Il modello non contiene corpi o superfici disponibili per una tavola.");
    drawing_=std::move(prepared);model_=std::move(geometry);sourcePath_=resolved;sourceReady_=true;modified_=true;installEditor();refreshTitle();return {};
}
QString DrawingWindow::openPath(const QString &path){
    if(editor_)return QStringLiteral("Questa finestra contiene gia' una tavola.");
    auto candidate=std::make_unique<DocumentFileLock>(path);auto error=candidate->acquire();if(!error.isEmpty())return error;
    DrawingFile prepared;error=loadDrawingFile(candidate->path(),prepared);if(!error.isEmpty())return error;
    DocumentState source;QString resolved;bool changed=false;
    error=loadDrawingModel(candidate->path(),prepared,source,resolved,changed);
    DrawingEditorModel geometry;geometry.snapshot.version=prepared.sheet.drawing.model;
    if(error.isEmpty()&&!changed)error=prepareDrawingEditorModel(source,geometry);
    const bool available=error.isEmpty()&&!changed;
    // Anche senza il 3D i metadati rimangono consultabili; nessun vecchio valore
    // numerico viene presentato come misura aggiornata.
    if(!available){geometry={};geometry.snapshot.version=prepared.sheet.drawing.model;}
    drawing_=std::move(prepared);model_=std::move(geometry);path_=candidate->path();lock_=std::move(candidate);
    sourcePath_=resolved.isEmpty()?QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelPath):resolved;
    sourceArchivePath_=drawing_.modelArchivePath.isEmpty()?QString():QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelArchivePath);
    sourceReady_=available;modified_=false;
    sourceStatus_->setText(changed?QStringLiteral("Il modello salvato e' cambiato: premere Aggiorna dal .prt salvato per ricalcolare viste e quote."):
        QStringLiteral("Modello non disponibile: %1. Individuare il modello per verificare le quote.").arg(error));
    installEditor();refreshTitle();return {};
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
    sourceReady_=true;modified_=true;
    sourceStatus_->setText(QStringLiteral("Modello aggiornato da: %1").arg(sourcePath_));refreshTitle();return {};
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
    drawing_=std::move(prepared);path_=resolved;modified_=false;
    if(different)installEditor();
    refreshTitle();return {};
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
    if(modified_){
        const auto choice=QMessageBox::warning(this,QStringLiteral("Tavola 2D"),QStringLiteral("Salvare le modifiche alla tavola?"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(choice==QMessageBox::Cancel||(choice==QMessageBox::Save&&!save(false))){event->ignore();return;}
    }
    lock_.reset();event->accept();
}
}
