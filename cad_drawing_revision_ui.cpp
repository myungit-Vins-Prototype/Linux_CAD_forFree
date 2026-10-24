#include "cad_drawing_window.h"
#include "cad_workspace.h"
#include "cad_icons.h"
#include "cad_menu.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QDockWidget>
#include <QTabWidget>
#include <QTimer>
#include <QCheckBox>
#include <algorithm>

namespace ForgeCad {
namespace {
QTableWidget *revisionTable(QDialog &dialog,const QVector<RevisionRecord> &records){
    auto *table=new QTableWidget(records.size(),5,&dialog);table->setObjectName("drawingRevisionTable");
    table->setHorizontalHeaderLabels({QStringLiteral("Check-in"),QStringLiteral("Revisione"),QStringLiteral("Autore"),QStringLiteral("Data"),QStringLiteral("Commento")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
    for(int row=0;row<records.size();++row){const auto &record=records[row];
        const QStringList values{QString::number(record.sequence),record.technicalRevision,record.author,record.createdUtc.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"),record.comment};
        for(int column=0;column<values.size();++column)table->setItem(row,column,new QTableWidgetItem(values[column]));
    }
    table->resizeColumnsToContents();table->horizontalHeader()->setStretchLastSection(true);
    if(!records.isEmpty())table->selectRow(records.size()-1);
    return table;
}
}
void DrawingWindow::addRevisionMenus(QMenu *file){
    auto *model=addCadMenu(file,QStringLiteral("Modello"));model->setObjectName("drawingModelMenu");
    auto *pin=model->addAction(QStringLiteral("Collega revisione del modello..."));pin->setObjectName("drawingPinModelAction");
    connect(pin,&QAction::triggered,this,[this]{showModelRevisions();});revisionMutationActions_.append(pin);
    auto *working=model->addAction(QStringLiteral("Usa copia di lavoro del modello"));working->setObjectName("drawingWorkingModelAction");
    connect(working,&QAction::triggered,this,[this]{report(useWorkingModel());});revisionMutationActions_.append(working);
    auto *revisions=addCadMenu(file,QStringLiteral("Revisioni tavola"));
    auto *checkout=revisions->addAction(QStringLiteral("Esegui check-out..."));checkout->setObjectName("drawingCheckoutAction");revisionMutationActions_.append(checkout);
    connect(checkout,&QAction::triggered,this,[this]{
        bool accepted=false;const auto fallback=qEnvironmentVariable("USER",qEnvironmentVariable("USERNAME"));
        const auto owner=QInputDialog::getText(this,QStringLiteral("Check-out tavola"),QStringLiteral("Proprietario della copia di lavoro:"),QLineEdit::Normal,
            revisionStatusKnown_&&revisionStatusCache_.hasSession?revisionStatusCache_.session.checkout.owner:QSettings().value("revisions/owner",fallback).toString(),&accepted).trimmed();
        if(!accepted||owner.isEmpty()||((path_.isEmpty()||(modified_&&!editAccess_.readOnly))&&!save(false)))return;
        const auto error=runRevisionAction(RevisionAction::Checkout,owner);if(error.isEmpty())QSettings().setValue("revisions/owner",owner);report(error);
    });
    auto *checkin=revisions->addAction(QStringLiteral("Esegui check-in..."));checkin->setObjectName("drawingCheckinAction");revisionMutationActions_.append(checkin);
    connect(checkin,&QAction::triggered,this,[this]{
        QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("Check-in tavola"));dialog.setObjectName("drawingCheckinDialog");auto *layout=new QFormLayout(&dialog);
        QString proposed;const auto proposalError=nextWorkspaceRevisionLabel(path_,drawing_.sheet.drawing.version.documentId,proposed);
        if(!proposalError.isEmpty()){report(proposalError);return;}
        auto *revision=new QLineEdit(proposed,&dialog);revision->setObjectName("drawingRevisionLabel");revision->setReadOnly(true);
        auto *comment=new QTextEdit(&dialog);comment->setAcceptRichText(false);comment->setObjectName("drawingRevisionComment");
        layout->addRow(QStringLiteral("Revisione tecnica:"),revision);layout->addRow(QStringLiteral("Commento:"),comment);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(revision,&QLineEdit::textChanged,&dialog,[buttons](const QString &value){buttons->button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());});
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!revision->text().trimmed().isEmpty());dialog.resize(460,260);
        if(dialog.exec()!=QDialog::Accepted||!save(false))return;
        const auto error=runRevisionAction(RevisionAction::Checkin,{},comment->toPlainText());
        report(error);
    });
    auto *complete=revisions->addAction(QStringLiteral("Completa check-in pendente"));complete->setObjectName("drawingCompleteCheckinAction");revisionMutationActions_.append(complete);
    connect(complete,&QAction::triggered,this,[this]{report(runRevisionAction(RevisionAction::CompletePending));});
    auto *cancel=revisions->addAction(QStringLiteral("Annulla check-out..."));cancel->setObjectName("drawingCancelCheckoutAction");revisionMutationActions_.append(cancel);
    connect(cancel,&QAction::triggered,this,[this]{
        if(QMessageBox::question(this,QStringLiteral("Annulla check-out tavola"),QStringLiteral("Liberare la prenotazione? Le modifiche locali vengono mantenute."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
            report(runRevisionAction(RevisionAction::CancelCheckout));
    });
    auto *restore=revisions->addAction(commandIcon("undo"),QStringLiteral("Ripristina revisione…"));restore->setObjectName("drawingRestoreRevisionAction");revisionMutationActions_.append(restore);
    restore->setToolTip(QStringLiteral("Recupera tutti i fogli e il modello di una revisione nella copia di lavoro, con Undo e nuovo check-in successivo."));connect(restore,&QAction::triggered,this,[this]{showDrawingHistory(true);});
    auto *createRelease=revisions->addAction(QStringLiteral("Nuovo rilascio coordinato…"));createRelease->setObjectName("drawingCreateReleaseAction");connect(createRelease,&QAction::triggered,this,[this]{showNewReleaseDialog();});
    auto *releases=revisions->addAction(QStringLiteral("Rilasci pubblicati…"));releases->setObjectName("drawingReleasesAction");connect(releases,&QAction::triggered,this,[this]{showReleasesDialog();});
    auto *history=revisions->addAction(QStringLiteral("Storico dei check-in..."));history->setObjectName("drawingHistoryAction");
    connect(history,&QAction::triggered,this,[this]{showDrawingHistory();});
    for(auto *menu:{model,revisions}){menu->setIcon(commandIcon("storyboard"));for(auto *action:menu->actions()){
        action->setIcon(commandIcon(action==history||action==pin?"storyboard":action==working||action==complete?"rebuild":action==cancel?"undo":action==checkin?"save":"open"));action->setIconVisibleInMenu(true);}}
}
QString DrawingWindow::runRevisionAction(RevisionAction action,const QString &first,const QString &second){
    if(!editor_||busy_||(editor_&&editor_->isComputing()))return QStringLiteral("Attendere la conclusione dell'operazione in corso.");
    if(path_.isEmpty())return QStringLiteral("Salvare prima la tavola nel workspace.");
    if(!drawing_.sheet.drawing.version.checkinId.isNull())return QStringLiteral("La tavola archiviata e' in sola lettura.");
    const bool owned=lock_&&lock_->owns(path_);
    if(!owned&&action==RevisionAction::Checkin)return QStringLiteral("Riprendere il check-out prima di pubblicare le modifiche.");
    const auto lockError=acquireWriteAccess();if(!lockError.isEmpty()){refreshEditAccess();refreshTitle();return lockError;}
    const auto oldBase=drawing_.baseCheckinId;busy_=true;refreshTitle();
    RevisionRecord record;bool matches=true;QString error;
    switch(action){
    case RevisionAction::Checkout:error=checkoutWorkspaceDrawing(path_,drawing_,first);break;
    case RevisionAction::Checkin:error=checkinWorkspaceDrawing(path_,drawing_,first,second,record,lock_.get());break;
    case RevisionAction::CompletePending:error=completeWorkspaceDrawingCheckin(path_,drawing_,record,matches,lock_.get());break;
    case RevisionAction::CancelCheckout:error=cancelWorkspaceDrawingCheckout(path_,drawing_);break;
    }
    busy_=false;
    if(!error.isEmpty()&&drawing_.baseCheckinId!=oldBase){containerDirty_=true;modified_=true;}
    if(error.isEmpty()&&(action==RevisionAction::Checkin||action==RevisionAction::CompletePending)){
        containerDirty_=false;modified_=false;editor_->markSaved();markSheetsSaved();statusBar()->showMessage(matches?QStringLiteral("Check-in della tavola archiviato."):
            QStringLiteral("Check-in originale completato. Le modifiche successive sono conservate nella copia di lavoro."),10000);
    }
    if(error.isEmpty()){diskFingerprint_.clear();documentFileFingerprint(path_,diskFingerprint_);}
    if(!owned&&!error.isEmpty())lock_.reset();
    if(error.isEmpty()&&(drawing_.baseCheckinId!=oldBase||action==RevisionAction::Checkin||action==RevisionAction::CompletePending))resetSheetsHistory(!modified_);
    refreshPageContext();refreshTitle();return error;
}
QString DrawingWindow::applySource(DrawingFile prepared,DocumentState source){
    DrawingEditorModel geometry;auto error=prepareDrawingEditorModel(source,geometry);if(!error.isEmpty())return error;
    error=updateSheetsModel(std::move(prepared),geometry);if(!error.isEmpty())return error;
    modelBaseCheckinId_=source.baseCheckinId;model_=std::move(geometry);sourceReady_=true;containerDirty_=true;modified_=true;
    sourceArchivePath_=drawing_.modelArchivePath.isEmpty()?QString():QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelArchivePath);
    const auto checkin=drawing_.sheet.drawing.model.checkinId;
    sourceStatus_->setText(checkin.isNull()?QStringLiteral("Sorgente: copia di lavoro salvata %1").arg(sourcePath_):QStringLiteral("Modello fissato al check-in %1").arg(checkin.toString(QUuid::WithoutBraces)));
    refreshPageContext();refreshTitle();return {};
}
QString DrawingWindow::pinModelRevision(const DocumentVersion &version){
    const auto accessError=requireEditAccess();if(!accessError.isEmpty())return accessError;
    if(busy_||(editor_&&editor_->isComputing()))return QStringLiteral("Attendere il calcolo in corso.");
    if(!editor_||!drawing_.sheet.drawing.version.checkinId.isNull())return QStringLiteral("La tavola non e' modificabile.");
    auto prepared=drawing_;auto error=pinWorkspaceDrawingModel(path_,prepared,version);if(!error.isEmpty())return error;
    WorkspacePaths paths;error=locateDocumentWorkspace(path_,paths);if(!error.isEmpty())return error;
    RevisionStore store(paths.revisions);DocumentState source;error=readNativeRevision(store,version,source);if(!error.isEmpty())return error;
    return applySource(std::move(prepared),std::move(source));
}
QString DrawingWindow::useWorkingModel(){
    const auto accessError=requireEditAccess();if(!accessError.isEmpty())return accessError;
    if(busy_||(editor_&&editor_->isComputing()))return QStringLiteral("Attendere il calcolo in corso.");
    if(!editor_||!drawing_.sheet.drawing.version.checkinId.isNull())return QStringLiteral("La tavola non e' modificabile.");
    DocumentState source;auto error=loadDocumentFile(sourcePath_,source);if(!error.isEmpty())return error;
    if(source.version.documentId!=drawing_.sheet.drawing.model.documentId||!source.version.checkinId.isNull())return QStringLiteral("Il file sorgente non e' la copia di lavoro del modello della tavola.");
    auto prepared=drawing_;prepared.sheet.drawing.model=source.version;prepared.modelArchiveId={};prepared.modelArchivePath.clear();
    error=drawingModelDefinitionHash(source,prepared.modelDefinitionHash);if(!error.isEmpty())return error;
    return applySource(std::move(prepared),std::move(source));
}
void DrawingWindow::showModelRevisions(){
    if(!editor_||!save(false))return;
    WorkspacePaths paths;auto error=locateDocumentWorkspace(path_,paths);if(!error.isEmpty()){report(error);return;}
    RevisionStore store(paths.revisions);QVector<RevisionRecord> records;error=store.history(drawing_.sheet.drawing.model.documentId,records);
    if(!error.isEmpty()){report(QStringLiteral("Archiviare prima il modello con File -> Revisioni nello stesso workspace. %1").arg(error));return;}
    if(records.isEmpty()){report(QStringLiteral("Nessun check-in del modello disponibile nello stesso workspace."));return;}
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("Revisione del modello sorgente"));dialog.setObjectName("drawingModelRevisionsDialog");auto *layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Scegli la versione del modello a cui fissare viste e quote."),&dialog));auto *table=revisionTable(dialog,records);layout->addWidget(table);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(table,&QTableWidget::cellDoubleClicked,&dialog,[&dialog](int,int){dialog.accept();});dialog.resize(800,360);
    if(dialog.exec()==QDialog::Accepted&&table->currentRow()>=0)report(pinModelRevision(records[table->currentRow()].version));
}
QString DrawingWindow::openRevision(const QString &workingPath,const DocumentVersion &version){
    if(editor_)return QStringLiteral("Questa finestra contiene gia' una tavola.");
    // Stessa base canonica per il riferimento relativo e la sua successiva risoluzione
    // (su macOS /var puo' essere un collegamento a /private/var).
    const auto canonical=canonicalDocumentPath(workingPath);
    WorkspacePaths paths;auto error=locateDocumentWorkspace(canonical,paths);if(!error.isEmpty())return error;
    RevisionStore store(paths.revisions);DrawingFile archived;error=readDrawingRevision(store,version,archived);if(!error.isEmpty())return error;
    archived.modelArchivePath=QDir(QFileInfo(canonical).absolutePath()).relativeFilePath(paths.revisions);
    loadContents(canonical,std::move(archived));return {};
}
void DrawingWindow::showDrawingHistory(bool restoreWorking){
    if(path_.isEmpty()){report(QStringLiteral("Salvare prima la tavola nel workspace."));return;}
    QVector<RevisionRecord> records;const auto error=workspaceDrawingHistory(path_,drawing_,records);if(!error.isEmpty()){report(error);return;}
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("Storico della tavola"));dialog.setObjectName("drawingHistoryDialog");auto *layout=new QVBoxLayout(&dialog);
    auto *table=revisionTable(dialog,records);layout->addWidget(table);
    if(records.isEmpty())layout->addWidget(new QLabel(QStringLiteral("Nessun check-in della tavola archiviato."),&dialog));
    auto *note=new QLabel(QStringLiteral("Il ripristino sostituisce tutti i fogli e usa il modello della revisione scelta. È annullabile con Undo; Salva e un nuovo check-in ne archiviano il risultato come discendente della base corrente."),&dialog);note->setWordWrap(true);layout->addWidget(note);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Close,&dialog);buttons->button(QDialogButtonBox::Open)->setText(restoreWorking?QStringLiteral("Ripristina copia di lavoro"):QStringLiteral("Apri copia storica"));
    const bool canRestore=findChild<QAction *>("drawingRestoreRevisionAction")->isEnabled();
    buttons->button(QDialogButtonBox::Open)->setEnabled(!records.isEmpty()&&(!restoreWorking||canRestore));layout->addWidget(buttons);
    if(!restoreWorking){auto *restore=buttons->addButton(QStringLiteral("Ripristina copia di lavoro"),QDialogButtonBox::ActionRole);restore->setObjectName("drawingRestoreHistoryButton");restore->setEnabled(canRestore&&!records.isEmpty());connect(restore,&QPushButton::clicked,&dialog,[&dialog]{dialog.done(2);});}
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&dialog,restoreWorking]{dialog.done(restoreWorking?2:QDialog::Accepted);});connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(table,&QTableWidget::cellDoubleClicked,&dialog,[&dialog,restoreWorking,canRestore](int,int){if(!restoreWorking||canRestore)dialog.done(restoreWorking?2:QDialog::Accepted);});dialog.resize(820,420);
    const int choice=dialog.exec();if((choice!=QDialog::Accepted&&choice!=2)||table->currentRow()<0)return;
    if(choice==2){report(restoreRevision(records[table->currentRow()].version));return;}
    auto *historical=new DrawingWindow(parentWidget());const auto opened=historical->openRevision(path_,records[table->currentRow()].version);
    if(!opened.isEmpty()){delete historical;report(opened);return;}
    historical->setAttribute(Qt::WA_DeleteOnClose);historical->show();
}
void DrawingWindow::installRevisionPanel(){
    revisionDock_=new QDockWidget(QStringLiteral("Revisioni"),this);revisionDock_->setObjectName("drawingRevisionDock");revisionDock_->setMinimumWidth(300);auto *body=new QWidget(revisionDock_);auto *layout=new QVBoxLayout(body);
    revisionStatus_=new QLabel(body);revisionStatus_->setObjectName("drawingRevisionStatus");revisionStatus_->setWordWrap(true);revisionStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(revisionStatus_);
    auto *commands=new QHBoxLayout;
    for(const auto name:{"drawingCheckoutAction","drawingCheckinAction","drawingCompleteCheckinAction","drawingCancelCheckoutAction"}){auto *button=new QToolButton(body);button->setDefaultAction(findChild<QAction *>(name));commands->addWidget(button);}layout->addLayout(commands);
    auto *tabs=new QTabWidget(body);revisionHistory_=new QTableWidget(tabs);revisionHistory_->setObjectName("drawingRevisionHistory");projectHistory_=new QTableWidget(tabs);projectHistory_->setObjectName("drawingProjectHistory");
    for(auto *table:{revisionHistory_,projectHistory_}){table->setColumnCount(5);table->setHorizontalHeaderLabels({QStringLiteral("N."),QStringLiteral("Rev."),QStringLiteral("Autore"),QStringLiteral("Data"),QStringLiteral("Commento")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->horizontalHeader()->setStretchLastSection(true);table->setWordWrap(true);}
    tabs->addTab(revisionHistory_,QStringLiteral("Tavola"));tabs->addTab(projectHistory_,QStringLiteral("Modello"));layout->addWidget(tabs,1);
    auto *refresh=new QPushButton(QStringLiteral("Aggiorna stato"),body);refresh->setObjectName("drawingRefreshRevisions");layout->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{if(!busy_&&editor_&&!editor_->isComputing()){refreshPageContext();refreshTitle();}});
    auto *edit=new QAction(commandIcon("drawingRevisionTable"),QStringLiteral("Tabella revisioni..."),this);edit->setObjectName("drawingRevisionTableAction");connect(edit,&QAction::triggered,this,[this]{showRevisionTableDialog();});auto *tableButton=new QToolButton(body);tableButton->setDefaultAction(edit);tableButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);layout->addWidget(tableButton);
    auto *openRevisionButton=new QPushButton(commandIcon("open"),QStringLiteral("Apri revisione selezionata"),body);
    openRevisionButton->setObjectName("drawingOpenSelectedRevision");openRevisionButton->setToolTip(QStringLiteral("Apre la revisione della tavola selezionata in una finestra separata, in sola lettura."));
    layout->insertWidget(layout->indexOf(tabs)+1,openRevisionButton);openRevisionButton->setEnabled(false);
    const auto updateOpenButton=[this,tabs,openRevisionButton]{openRevisionButton->setVisible(tabs->currentWidget()==revisionHistory_);openRevisionButton->setEnabled(revisionHistory_->currentRow()>=0);};
    connect(revisionHistory_,&QTableWidget::itemSelectionChanged,this,updateOpenButton);
    connect(tabs,&QTabWidget::currentChanged,this,updateOpenButton);
    const auto openSelectedRevision=[this](int row){if(busy_||!editor_||editor_->isComputing()||row<0)return;auto *item=revisionHistory_->item(row,0);if(!item)return;DocumentVersion version{item->data(Qt::UserRole+1).toUuid(),item->data(Qt::UserRole).toUuid()};auto *window=new DrawingWindow(parentWidget());const auto error=window->openRevision(path_,version);if(!error.isEmpty()){delete window;report(error);return;}window->setAttribute(Qt::WA_DeleteOnClose);window->show();};
    connect(openRevisionButton,&QPushButton::clicked,this,[this,openSelectedRevision]{openSelectedRevision(revisionHistory_->currentRow());});
    connect(revisionHistory_,&QTableWidget::cellDoubleClicked,this,[openSelectedRevision](int row,int){openSelectedRevision(row);});
    auto *restoreSelected=new QPushButton(commandIcon("undo"),QStringLiteral("Ripristina revisione selezionata"),body);restoreSelected->setObjectName("drawingRestoreSelectedRevision");restoreSelected->setToolTip(QStringLiteral("Sostituisce la copia di lavoro con i fogli e il modello selezionati; annullabile con Undo."));layout->insertWidget(layout->indexOf(tabs)+2,restoreSelected);
    auto *restoreAction=findChild<QAction *>("drawingRestoreRevisionAction");const auto updateRestore=[this,tabs,restoreSelected,restoreAction]{restoreSelected->setVisible(tabs->currentWidget()==revisionHistory_);restoreSelected->setEnabled(restoreAction->isEnabled()&&revisionHistory_->currentRow()>=0);};
    connect(restoreAction,&QAction::changed,restoreSelected,updateRestore);connect(revisionHistory_,&QTableWidget::itemSelectionChanged,restoreSelected,updateRestore);connect(tabs,&QTabWidget::currentChanged,restoreSelected,updateRestore);updateRestore();
    connect(restoreSelected,&QPushButton::clicked,this,[this]{const int row=revisionHistory_->currentRow();if(row<0)return;const auto *item=revisionHistory_->item(row,0);if(item)report(restoreRevision({item->data(Qt::UserRole+1).toUuid(),item->data(Qt::UserRole).toUuid()}));});
    revisionHistory_->setToolTip(QStringLiteral("Seleziona una revisione e premi Apri revisione selezionata; puoi anche fare doppio clic sulla riga."));
    revisionDock_->setWidget(body);addDockWidget(Qt::RightDockWidgetArea,revisionDock_);
    auto *timer=new QTimer(this);timer->setInterval(5000);connect(timer,&QTimer::timeout,this,[this,last=QString()]()mutable{
        if(!editor_||busy_||editor_->isComputing())return;
        refreshEditAccess();refreshTitle();
        WorkspacePaths paths;if(!locateDocumentWorkspace(path_.isEmpty()?sourcePath_:path_,paths).isEmpty())return;
        QString stamp;for(const auto &directory:{paths.revisions,sourceArchivePath_}){if(directory.isEmpty())continue;const QFileInfo catalog(QDir(directory).filePath("catalog.json"));stamp+=directory+QString::number(catalog.lastModified().toMSecsSinceEpoch())+QString::number(catalog.size());}
        if(stamp!=last){last=stamp;refreshPageContext();refreshTitle();}
    });timer->start();
    revisionStateText_=QStringLiteral("Nessuna tavola aperta.");
}
void DrawingWindow::refreshRevisionPanel(){
    if(!revisionDock_||!editor_||busy_||editor_->isComputing())return;
    const bool historical=!drawing_.sheet.drawing.version.checkinId.isNull();revisionStatusKnown_=false;revisionStatusCache_={};
    QVector<RevisionRecord> history;QString error;
    if(path_.isEmpty())revisionStateText_=QStringLiteral("Nuova tavola: salvare nel workspace per gestire i check-in.");
    else{
        error=workspaceDrawingHistory(path_,drawing_,history);
        if(historical)revisionStateText_=QStringLiteral("Copia storica in sola lettura.\nRevisione tavola: %1").arg(editor_->pageContext().drawingRevision);
        else{
            const auto statusError=workspaceDrawingRevisionStatus(path_,drawing_,revisionStatusCache_);revisionStatusKnown_=statusError.isEmpty();
            if(!statusError.isEmpty())revisionStateText_=QStringLiteral("Stato revisioni non verificato: %1").arg(statusError);
            else if(revisionStatusCache_.hasPending)revisionStateText_=QStringLiteral("Check-in pendente: completare l'operazione prima di crearne un'altra.");
            else if(revisionStatusCache_.hasSession)revisionStateText_=QStringLiteral("Check-out: %1\n%2").arg(revisionStatusCache_.session.checkout.owner,revisionStatusCache_.phase==WorkspaceCheckoutPhase::Active?QStringLiteral("Copia di lavoro prenotata"):QStringLiteral("Prenotazione da completare"));
            else{
                WorkspacePaths paths;QVector<RevisionDocumentInfo> documents;bool found=false;
                if(locateDocumentWorkspace(path_,paths).isEmpty()&&RevisionStore(paths.revisions).documents(documents).isEmpty())for(const auto &doc:documents)if(doc.documentId==drawing_.sheet.drawing.version.documentId){found=true;revisionStateText_=doc.checkedOut?QStringLiteral("Check-out assegnato a: %1").arg(doc.checkoutOwner):QStringLiteral("Nessun check-out attivo.");break;}
                if(!found)revisionStateText_=QStringLiteral("Tavola non ancora registrata: eseguire il primo check-out.");
            }
            QString next;if(nextWorkspaceRevisionLabel(path_,drawing_.sheet.drawing.version.documentId,next).isEmpty())revisionStateText_+=QStringLiteral("\nProssima revisione automatica: %1").arg(next);
        }
    }
    if(editAccess_.readOnly&&!historical)revisionStateText_+=QStringLiteral("\n")+editAccess_.reason;
    revisionStateText_+=QStringLiteral("\nModello: %1").arg(editor_->pageContext().modelRevision);
    if(!history.isEmpty()&&drawing_.baseCheckinId!=history.back().version.checkinId&&!historical)revisionStateText_+=QStringLiteral("\nAttenzione: la base locale non e' l'ultimo check-in.");
    const auto fill=[](QTableWidget *table,const QVector<RevisionRecord> &records){const auto selected=table->currentRow()>=0&&table->item(table->currentRow(),0)?table->item(table->currentRow(),0)->data(Qt::UserRole).toUuid():QUuid();table->setRowCount(records.size());int selection=records.size()-1;
        for(int row=0;row<records.size();++row){const auto &r=records[row];const QStringList values{QString::number(r.sequence),r.technicalRevision,r.author,r.createdUtc.toLocalTime().toString("yyyy-MM-dd HH:mm"),r.comment};for(int column=0;column<values.size();++column)table->setItem(row,column,new QTableWidgetItem(values[column]));table->item(row,0)->setData(Qt::UserRole,r.version.checkinId);table->item(row,0)->setData(Qt::UserRole+1,r.version.documentId);if(r.version.checkinId==selected)selection=row;}
        table->resizeColumnsToContents();table->setColumnWidth(4,200);table->resizeRowsToContents();if(selection>=0)table->selectRow(selection);
    };
    fill(revisionHistory_,history);fill(projectHistory_,projectRecords_);revisionStatus_->setToolTip(error);revisionStatus_->setText(revisionStateText_+(modified_?QStringLiteral("\nModifiche locali da salvare."):QString()));
}
void DrawingWindow::showRevisionTableDialog(){
    if(!editor_||busy_||editor_->isComputing())return;
    refreshPageContext();const auto context=editor_->pageContext();const auto original=editor_->sheet().revisionTable;const bool readOnly=editor_->isReadOnly();
    QDialog dialog(this);dialog.setObjectName("drawingRevisionTableDialog");dialog.setWindowTitle(QStringLiteral("Tabella revisioni del modello"));auto *layout=new QVBoxLayout(&dialog);
    auto *visible=new QCheckBox(QStringLiteral("Mostra la tabella sul foglio e nel PDF"),&dialog);visible->setObjectName("drawingRevisionTableVisible");visible->setChecked(original.visible);visible->setEnabled(!readOnly);layout->addWidget(visible);
    auto *note=new QLabel(QStringLiteral("Numero, data e autore provengono dai veri check-in del modello, fino alla versione usata dalla tavola. Compila descrizione e verifica; testo a capo e altezza delle righe sono automatici."),&dialog);note->setWordWrap(true);layout->addWidget(note);
    auto *table=new QTableWidget(context.projectRevisions.size(),5,&dialog);table->setObjectName("drawingRevisionNotes");table->setHorizontalHeaderLabels({QStringLiteral("Revisione"),QStringLiteral("Data"),QStringLiteral("Descrizione"),QStringLiteral("Autore"),QStringLiteral("Verifica")});table->setWordWrap(true);table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);table->setColumnWidth(0,80);table->setColumnWidth(1,110);table->setColumnWidth(3,100);table->setColumnWidth(4,110);
    for(int row=0;row<context.projectRevisions.size();++row){const auto &r=context.projectRevisions[row];QString description=r.comment,verified;for(const auto &entry:original.notes)if(entry.checkinId==r.version.checkinId){description=entry.description;verified=entry.verifiedBy;break;}const QStringList cells{r.label,r.createdUtc.toLocalTime().toString("yyyy-MM-dd"),description,r.author,verified};for(int column=0;column<cells.size();++column){auto *item=new QTableWidgetItem(cells[column]);if(readOnly||(column!=2&&column!=4))item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(row,column,item);}}
    connect(table,&QTableWidget::cellChanged,&dialog,[table](int,int){table->resizeRowsToContents();});table->resizeRowsToContents();layout->addWidget(table,1);
    if(context.projectRevisions.isEmpty())layout->addWidget(new QLabel(QStringLiteral("Nessun check-in modello verificato. Archiviare il modello e aggiornare o collegare la tavola alla revisione esatta."),&dialog));
    auto *error=new QLabel(&dialog);error->setWordWrap(true);layout->addWidget(error);auto *buttons=new QDialogButtonBox(readOnly?QDialogButtonBox::Close:QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{auto next=original;next.visible=visible->isChecked();for(int row=0;row<context.projectRevisions.size();++row){const auto id=context.projectRevisions[row].version.checkinId;auto found=std::find_if(next.notes.begin(),next.notes.end(),[&](const auto &entry){return entry.checkinId==id;});DrawingRevisionNote entry{id,table->item(row,2)->text(),table->item(row,4)->text()};if(found==next.notes.end())next.notes.append(entry);else *found=entry;}const auto message=editor_->setRevisionTable(next);error->setText(message);if(message.isEmpty())dialog.accept();});dialog.resize(900,450);dialog.exec();
}

}
