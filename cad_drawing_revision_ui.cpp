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
            QSettings().value("revisions/owner",fallback).toString(),&accepted).trimmed();
        if(!accepted||owner.isEmpty()||!save(false))return;
        const auto error=runRevisionAction(RevisionAction::Checkout,owner);if(error.isEmpty())QSettings().setValue("revisions/owner",owner);report(error);
    });
    auto *checkin=revisions->addAction(QStringLiteral("Esegui check-in..."));checkin->setObjectName("drawingCheckinAction");revisionMutationActions_.append(checkin);
    connect(checkin,&QAction::triggered,this,[this]{
        QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("Check-in tavola"));dialog.setObjectName("drawingCheckinDialog");auto *layout=new QFormLayout(&dialog);
        auto *revision=new QLineEdit(QSettings().value("revisions/lastDrawingLabel","A").toString(),&dialog);revision->setObjectName("drawingRevisionLabel");revision->setMaxLength(128);
        auto *comment=new QTextEdit(&dialog);comment->setAcceptRichText(false);comment->setObjectName("drawingRevisionComment");
        layout->addRow(QStringLiteral("Revisione tecnica:"),revision);layout->addRow(QStringLiteral("Commento:"),comment);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(revision,&QLineEdit::textChanged,&dialog,[buttons](const QString &value){buttons->button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());});
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!revision->text().trimmed().isEmpty());dialog.resize(460,260);
        if(dialog.exec()!=QDialog::Accepted||!save(false))return;
        const auto label=revision->text().trimmed();const auto error=runRevisionAction(RevisionAction::Checkin,label,comment->toPlainText());
        if(error.isEmpty())QSettings().setValue("revisions/lastDrawingLabel",label);
        report(error);
    });
    auto *complete=revisions->addAction(QStringLiteral("Completa check-in pendente"));complete->setObjectName("drawingCompleteCheckinAction");revisionMutationActions_.append(complete);
    connect(complete,&QAction::triggered,this,[this]{report(runRevisionAction(RevisionAction::CompletePending));});
    auto *cancel=revisions->addAction(QStringLiteral("Annulla check-out..."));cancel->setObjectName("drawingCancelCheckoutAction");revisionMutationActions_.append(cancel);
    connect(cancel,&QAction::triggered,this,[this]{
        if(QMessageBox::question(this,QStringLiteral("Annulla check-out tavola"),QStringLiteral("Liberare la prenotazione? Le modifiche locali vengono mantenute."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
            report(runRevisionAction(RevisionAction::CancelCheckout));
    });
    auto *history=revisions->addAction(QStringLiteral("Storico dei check-in..."));history->setObjectName("drawingHistoryAction");
    connect(history,&QAction::triggered,this,[this]{showDrawingHistory();});
    for(auto *menu:{model,revisions}){menu->setIcon(commandIcon("storyboard"));for(auto *action:menu->actions()){
        action->setIcon(commandIcon(action==history||action==pin?"storyboard":action==working||action==complete?"rebuild":action==cancel?"undo":action==checkin?"save":"open"));action->setIconVisibleInMenu(true);}}
}
QString DrawingWindow::runRevisionAction(RevisionAction action,const QString &first,const QString &second){
    if(!editor_||busy_)return QStringLiteral("Attendere la conclusione dell'operazione in corso.");
    if(path_.isEmpty())return QStringLiteral("Salvare prima la tavola nel workspace.");
    if(!drawing_.sheet.drawing.version.checkinId.isNull())return QStringLiteral("La tavola archiviata e' in sola lettura.");
    if(!lock_||!lock_->owns(path_))return QStringLiteral("La tavola non ha piu' un blocco esclusivo valido.");
    const auto oldBase=drawing_.baseCheckinId;busy_=true;refreshTitle();
    RevisionRecord record;bool matches=true;QString error;
    switch(action){
    case RevisionAction::Checkout:error=checkoutWorkspaceDrawing(path_,drawing_,first);break;
    case RevisionAction::Checkin:error=checkinWorkspaceDrawing(path_,drawing_,first,second,record,lock_.get());break;
    case RevisionAction::CompletePending:error=completeWorkspaceDrawingCheckin(path_,drawing_,record,matches,lock_.get());break;
    case RevisionAction::CancelCheckout:error=cancelWorkspaceDrawingCheckout(path_,drawing_);break;
    }
    busy_=false;
    if(!error.isEmpty()&&drawing_.baseCheckinId!=oldBase)modified_=true;
    if(error.isEmpty()&&(action==RevisionAction::Checkin||action==RevisionAction::CompletePending)){
        modified_=false;statusBar()->showMessage(matches?QStringLiteral("Check-in della tavola archiviato."):
            QStringLiteral("Check-in originale completato. Le modifiche successive sono conservate nella copia di lavoro."),10000);
    }
    refreshTitle();return error;
}
QString DrawingWindow::applySource(DrawingFile prepared,DocumentState source){
    DrawingEditorModel geometry;auto error=prepareDrawingEditorModel(source,geometry);if(!error.isEmpty())return error;
    const bool wasReadOnly=editor_->isReadOnly();editor_->setReadOnly(false);error=editor_->updateModel(geometry);
    if(!error.isEmpty()){editor_->setReadOnly(wasReadOnly);return error;}
    prepared.sheet=editor_->sheet();drawing_=std::move(prepared);model_=std::move(geometry);sourceReady_=true;modified_=true;
    sourceArchivePath_=drawing_.modelArchivePath.isEmpty()?QString():QDir(QFileInfo(path_).absolutePath()).absoluteFilePath(drawing_.modelArchivePath);
    const auto checkin=drawing_.sheet.drawing.model.checkinId;
    sourceStatus_->setText(checkin.isNull()?QStringLiteral("Sorgente: copia di lavoro salvata %1").arg(sourcePath_):QStringLiteral("Modello fissato al check-in %1").arg(checkin.toString(QUuid::WithoutBraces)));
    refreshTitle();return {};
}
QString DrawingWindow::pinModelRevision(const DocumentVersion &version){
    if(!editor_||!drawing_.sheet.drawing.version.checkinId.isNull())return QStringLiteral("La tavola non e' modificabile.");
    auto prepared=drawing_;auto error=pinWorkspaceDrawingModel(path_,prepared,version);if(!error.isEmpty())return error;
    WorkspacePaths paths;error=locateDocumentWorkspace(path_,paths);if(!error.isEmpty())return error;
    RevisionStore store(paths.revisions);DocumentState source;error=readNativeRevision(store,version,source);if(!error.isEmpty())return error;
    return applySource(std::move(prepared),std::move(source));
}
QString DrawingWindow::useWorkingModel(){
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
    WorkspacePaths paths;auto error=locateDocumentWorkspace(workingPath,paths);if(!error.isEmpty())return error;
    RevisionStore store(paths.revisions);DrawingFile archived;error=readDrawingRevision(store,version,archived);if(!error.isEmpty())return error;
    archived.modelArchivePath=QDir(QFileInfo(workingPath).absolutePath()).relativeFilePath(paths.revisions);
    loadContents(canonicalDocumentPath(workingPath),std::move(archived));return {};
}
void DrawingWindow::showDrawingHistory(){
    if(path_.isEmpty()){report(QStringLiteral("Salvare prima la tavola nel workspace."));return;}
    QVector<RevisionRecord> records;const auto error=workspaceDrawingHistory(path_,drawing_,records);if(!error.isEmpty()){report(error);return;}
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("Storico della tavola"));dialog.setObjectName("drawingHistoryDialog");auto *layout=new QVBoxLayout(&dialog);
    auto *table=revisionTable(dialog,records);layout->addWidget(table);
    if(records.isEmpty())layout->addWidget(new QLabel(QStringLiteral("Nessun check-in della tavola archiviato."),&dialog));
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Close,&dialog);buttons->button(QDialogButtonBox::Open)->setText(QStringLiteral("Apri copia storica"));
    buttons->button(QDialogButtonBox::Open)->setEnabled(!records.isEmpty());layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(table,&QTableWidget::cellDoubleClicked,&dialog,[&dialog](int,int){dialog.accept();});dialog.resize(800,360);
    if(dialog.exec()!=QDialog::Accepted||table->currentRow()<0)return;
    auto *historical=new DrawingWindow(parentWidget());const auto opened=historical->openRevision(path_,records[table->currentRow()].version);
    if(!opened.isEmpty()){delete historical;report(opened);return;}
    historical->setAttribute(Qt::WA_DeleteOnClose);historical->show();
}
}
