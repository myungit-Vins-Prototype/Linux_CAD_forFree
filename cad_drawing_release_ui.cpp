#include "cad_drawing_window.h"
#include "cad_drawing_release.h"
#include "cad_drawing_compute.h"
#include "cad_workspace.h"
#include "forgeCad2026_gui.h"
#include <QAction>
#include <QSet>
#include <algorithm>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <optional>
#include <stdexcept>
namespace ForgeCad {
namespace {
void releaseUiOk(const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());}
const QStringList releasePhases{QStringLiteral("Lettura archivio"),QStringLiteral("Verifica versioni e contenuti"),QStringLiteral("Verifica dipendenze"),QStringLiteral("Preparazione risultato"),QStringLiteral("Completamento operazione"),QStringLiteral("Operazione completata")};
bool sameVersion(const DocumentVersion &a,const DocumentVersion &b){return a.documentId==b.documentId&&a.checkinId==b.checkinId;}
QString releaseVersionLabel(const RevisionRecord &r){return QStringLiteral("%1 · check-in %2").arg(r.technicalRevision).arg(r.sequence);}
struct ReleaseChoices {QVector<RevisionRecord> models;QVector<RevisionDocumentInfo> drawings;QVector<QVector<RevisionRecord>> histories;QVector<RevisionReleaseManifest> releases;};
ReleaseChoices releaseChoices(RevisionStore store,const QUuid &model){
    ReleaseChoices result;releaseUiOk(store.history(model,result.models));releaseUiOk(store.releases(result.releases));QVector<RevisionDocumentInfo> documents;releaseUiOk(store.documents(documents));
    std::sort(documents.begin(),documents.end(),[](const auto &a,const auto &b){return a.name!=b.name?a.name<b.name:a.documentId<b.documentId;});
    for(const auto &d:documents)if(d.kind=="drawing"){Kernel::cancellationPoint();QVector<RevisionRecord> records;releaseUiOk(store.history(d.documentId,records));if(std::none_of(records.cbegin(),records.cend(),[&](const auto &record){return record.dependencies.size()==1&&record.dependencies[0].documentId==model;}))continue;result.drawings.append(d);result.histories.append(records);}
    return result;
}
QString showReleaseCreation(QWidget *owner,RevisionStore store,const ReleaseChoices &choices,const DocumentVersion &currentModel,const QUuid &currentDrawing){
    if(choices.models.isEmpty())return {};
    QDialog dialog(owner);dialog.setObjectName("drawingCreateReleaseDialog");dialog.setWindowTitle(QStringLiteral("Nuovo rilascio coordinato"));auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;layout->addLayout(form);
    auto *name=new QLineEdit(&dialog);name->setObjectName("drawingReleaseName");name->setMaxLength(128);int next=1;for(;;++next){bool used=false;for(const auto &m:choices.releases)if(m.model.version.documentId==currentModel.documentId&&m.name.compare(QStringLiteral("Rilascio %1").arg(next),Qt::CaseInsensitive)==0)used=true;if(!used)break;}name->setText(QStringLiteral("Rilascio %1").arg(next));form->addRow(QStringLiteral("Nome consegna:"),name);
    auto *author=new QLineEdit(QSettings().value("revisions/owner",qEnvironmentVariable("USER",qEnvironmentVariable("USERNAME"))).toString(),&dialog);author->setObjectName("drawingReleaseAuthor");author->setMaxLength(256);form->addRow(QStringLiteral("Autore dichiarato:"),author);
    auto *model=new QComboBox(&dialog);model->setObjectName("drawingReleaseModel");int wanted=choices.models.size()-1;for(int i=0;i<choices.models.size();++i){model->addItem(releaseVersionLabel(choices.models[i]));if(sameVersion(currentModel,choices.models[i].version))wanted=i;}model->setCurrentIndex(wanted);form->addRow(QStringLiteral("Revisione modello:"),model);
    auto *note=new QLabel(QStringLiteral("Seleziona le revisioni archiviate da consegnare. Le copie di lavoro e le modifiche non salvate non vengono incluse. Ogni tavola deve usare esattamente il modello scelto; il rilascio conserva tutti i suoi fogli."),&dialog);note->setWordWrap(true);layout->addWidget(note);
    auto *table=new QTableWidget(choices.drawings.size(),3,&dialog);table->setObjectName("drawingReleaseDrawings");table->setHorizontalHeaderLabels({QStringLiteral("Includi"),QStringLiteral("Tavola"),QStringLiteral("Revisione da consegnare")});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table,1);
    auto *comment=new QPlainTextEdit(&dialog);comment->setObjectName("drawingReleaseComment");comment->setPlaceholderText(QStringLiteral("Descrizione della consegna"));comment->setMaximumHeight(85);layout->addWidget(comment);
    auto *status=new QLabel(&dialog);status->setObjectName("drawingReleaseStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);auto *verify=buttons->addButton(QStringLiteral("Verifica insieme"),QDialogButtonBox::ActionRole),*publish=buttons->addButton(QStringLiteral("Pubblica rilascio"),QDialogButtonBox::AcceptRole);verify->setObjectName("drawingVerifyReleaseButton");publish->setObjectName("drawingPublishReleaseButton");layout->addWidget(buttons);
    QUuid operation=QUuid::createUuid();const auto changed=[&]{operation=QUuid::createUuid();status->clear();};bool first=true;
    const auto refresh=[&]{QSet<QUuid> selected;for(int row=0;row<table->rowCount();++row)if(table->item(row,0)&&table->item(row,0)->checkState()==Qt::Checked)selected.insert(choices.drawings[row].documentId);if(first){selected.insert(currentDrawing);first=false;}table->blockSignals(true);
        for(int row=0;row<table->rowCount();++row){auto *include=new QTableWidgetItem;include->setFlags(Qt::ItemIsUserCheckable|Qt::ItemIsEnabled|Qt::ItemIsSelectable);auto *versions=new QComboBox(table);versions->setObjectName(QStringLiteral("drawingReleaseVersion%1").arg(row));const auto &records=choices.histories[row];
            for(int i=0;i<records.size();++i)if(records[i].dependencies.size()==1&&sameVersion(records[i].dependencies[0],choices.models[model->currentIndex()].version))versions->addItem(releaseVersionLabel(records[i]),i);
            if(versions->count()){versions->setCurrentIndex(versions->count()-1);include->setCheckState(selected.contains(choices.drawings[row].documentId)?Qt::Checked:Qt::Unchecked);}else{versions->addItem(QStringLiteral("Nessuna revisione compatibile"));versions->setEnabled(false);include->setFlags(Qt::ItemIsSelectable);include->setCheckState(Qt::Unchecked);}
            if(auto *old=table->cellWidget(row,2))old->hide();
            table->setItem(row,0,include);table->setItem(row,1,new QTableWidgetItem(choices.drawings[row].name));table->setCellWidget(row,2,versions);QObject::connect(versions,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,changed);
        }table->blockSignals(false);table->resizeColumnsToContents();table->doItemsLayout();changed();};refresh();
    const auto request=[&]{RevisionReleaseRequest r{operation,name->text(),author->text(),comment->toPlainText(),choices.models[model->currentIndex()].version,{}};
        for(int row=0;row<table->rowCount();++row)if(table->item(row,0)->checkState()==Qt::Checked){auto *combo=qobject_cast<QComboBox *>(table->cellWidget(row,2));const int index=combo->currentData().toInt();r.drawings.append(choices.histories[row][index].version);}
        return r;};
    QObject::connect(name,&QLineEdit::textChanged,&dialog,changed);QObject::connect(author,&QLineEdit::textChanged,&dialog,changed);QObject::connect(comment,&QPlainTextEdit::textChanged,&dialog,changed);QObject::connect(table,&QTableWidget::itemChanged,&dialog,changed);QObject::connect(model,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,refresh);
    QString published;const auto execute=[&](bool commit){const auto r=request();RevisionReleaseManifest result;bool busy=false;const auto error=runDrawingComputation(&dialog,busy,commit?QStringLiteral("Pubblicazione rilascio"):QStringLiteral("Verifica consegna"),result,[store,r,commit](auto phase)mutable{RevisionReleaseManifest m;phase(1);releaseUiOk(commit?publishDrawingRelease(store,r,m):inspectDrawingRelease(store,r,m));phase(5);return m;},releasePhases);
        if(!error.isEmpty()){
            // Un annullamento arrivato dopo il commit non deve perdere la ricevuta.
            RevisionReleaseManifest recovered;const auto recovery=commit?readDrawingRelease(store,r.releaseId,recovered):QStringLiteral("Nessuna pubblicazione richiesta");
            if(!recovery.isEmpty()){status->setText(error+(commit?QStringLiteral("\nPubblicazione non confermata: puoi ritentare mantenendo gli stessi dati."):QString()));return;}
            result=std::move(recovered);
        }
        status->setText(QStringLiteral("Verificati modello %1 e %2 tavole. Tutti i contenuti e le dipendenze corrispondono.").arg(result.model.technicalRevision).arg(result.drawings.size()));
        if(commit){published=result.name;QSettings().setValue("revisions/owner",result.author);dialog.accept();}
    };
    QObject::connect(verify,&QPushButton::clicked,&dialog,[&]{execute(false);});QObject::connect(publish,&QPushButton::clicked,&dialog,[&]{execute(true);});QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.resize(880,650);dialog.exec();return published;
}
QString showPublishedReleases(DrawingWindow *owner,RevisionStore store,const WorkspacePaths &workspace){
    QVector<RevisionReleaseManifest> records;bool loading=false;const auto loadError=runDrawingComputation(owner,loading,QStringLiteral("Elenco rilasci"),records,[store](auto phase)mutable{QVector<RevisionReleaseManifest> result;releaseUiOk(store.releases(result));phase(5);return result;},releasePhases);if(!loadError.isEmpty())return loadError;QDialog dialog(owner);dialog.setObjectName("drawingReleasesDialog");dialog.setWindowTitle(QStringLiteral("Rilasci pubblicati"));auto *layout=new QVBoxLayout(&dialog);
    auto *table=new QTableWidget(records.size(),6,&dialog);table->setObjectName("drawingReleasesTable");table->setHorizontalHeaderLabels({QStringLiteral("Rilascio"),QStringLiteral("Modello"),QStringLiteral("Rev."),QStringLiteral("Tavole"),QStringLiteral("Autore"),QStringLiteral("Data")});table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table,1);
    for(int row=0;row<records.size();++row){const auto &r=records[row];const QStringList values{r.name,r.model.name,r.model.technicalRevision,QString::number(r.drawings.size()),r.author,r.createdUtc.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")};for(int c=0;c<values.size();++c)table->setItem(row,c,new QTableWidgetItem(values[c]));}table->resizeColumnsToContents();
    auto *details=new QTableWidget(0,5,&dialog);details->setObjectName("drawingReleaseMembers");details->setHorizontalHeaderLabels({QStringLiteral("Documento"),QStringLiteral("Tipo"),QStringLiteral("Revisione"),QStringLiteral("Check-in"),QStringLiteral("SHA256")});details->setSelectionBehavior(QAbstractItemView::SelectRows);details->setSelectionMode(QAbstractItemView::SingleSelection);details->setEditTriggers(QAbstractItemView::NoEditTriggers);details->horizontalHeader()->setStretchLastSection(true);layout->addWidget(details,1);
    auto *status=new QLabel(records.isEmpty()?QStringLiteral("Nessun rilascio pubblicato."):QStringLiteral("Seleziona un rilascio per verificare contenuti e dipendenze."),&dialog);status->setObjectName("drawingReleaseVerification");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);auto *open=buttons->addButton(QStringLiteral("Apri documento selezionato"),QDialogButtonBox::ActionRole);open->setObjectName("drawingOpenReleaseMember");open->setEnabled(false);layout->addWidget(buttons);std::optional<RevisionReleaseManifest> selected;
    QObject::connect(table,&QTableWidget::itemSelectionChanged,&dialog,[&]{const int row=table->currentRow();selected.reset();open->setEnabled(false);details->setRowCount(0);if(row<0)return;RevisionReleaseManifest verified;bool busy=false;
        const auto error=runDrawingComputation(&dialog,busy,QStringLiteral("Verifica rilascio"),verified,[store,id=records[row].releaseId](auto phase)mutable{RevisionReleaseManifest m;phase(1);releaseUiOk(readDrawingRelease(store,id,m));phase(5);return m;},releasePhases);if(!error.isEmpty()){status->setText(error);return;}selected=std::move(verified);QVector<RevisionReleaseMember> members{selected->model};members+=selected->drawings;details->setRowCount(members.size());
        for(int r=0;r<members.size();++r){const auto &m=members[r];const QStringList values{m.name,m.kind=="model"?QStringLiteral("Modello"):QStringLiteral("Tavola"),m.technicalRevision,m.version.checkinId.toString(QUuid::WithoutBraces).left(8),QString::fromLatin1(m.sha256.toHex()).left(16)+QStringLiteral("…")};for(int c=0;c<values.size();++c){auto *item=new QTableWidgetItem(values[c]);item->setToolTip(c==4?QString::fromLatin1(m.sha256.toHex()):m.version.checkinId.toString(QUuid::WithoutBraces));details->setItem(r,c,item);}}
        details->resizeColumnsToContents();details->selectRow(1);open->setEnabled(true);status->setText(QStringLiteral("Rilascio verificato: %1 · %2 tavole\n%3").arg(selected->name).arg(selected->drawings.size()).arg(selected->comment));
    });
    QObject::connect(open,&QPushButton::clicked,&dialog,[&]{if(!selected||details->currentRow()<0)return;RevisionReleaseManifest checked;bool busy=false;const auto error=runDrawingComputation(&dialog,busy,QStringLiteral("Verifica documento del rilascio"),checked,[store,id=selected->releaseId](auto phase)mutable{RevisionReleaseManifest result;phase(1);releaseUiOk(readDrawingRelease(store,id,result));phase(5);return result;},releasePhases);if(!error.isEmpty()){open->setEnabled(false);status->setText(error);return;}const int row=details->currentRow();const auto member=row==0?checked.model:checked.drawings[row-1];QString filename=QFileInfo(member.name).fileName();if(filename.isEmpty())filename=member.version.documentId.toString(QUuid::WithoutBraces);const QString extension=row==0?QStringLiteral("prt"):QStringLiteral("drw");if(QFileInfo(filename).suffix().compare(extension,Qt::CaseInsensitive)!=0)filename+="."+extension;const auto anchor=QDir(workspace.root).filePath(filename);
        if(row==0){auto *window=new PdfWindow;const auto result=window->openArchivedDocument(anchor,member.version);if(!result.isEmpty()){delete window;status->setText(result);return;}window->setWindowTitle(checked.name+QStringLiteral(" · ")+window->windowTitle());window->setAttribute(Qt::WA_DeleteOnClose);window->show();}
        else{auto *window=new DrawingWindow;const auto result=window->openRevision(anchor,member.version);if(!result.isEmpty()){delete window;status->setText(result);return;}window->setWindowTitle(checked.name+QStringLiteral(" · ")+window->windowTitle());window->setAttribute(Qt::WA_DeleteOnClose);window->show();QTimer::singleShot(0,window,[window]{window->editor()->fitSheet();});}dialog.accept();
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(!records.isEmpty())QTimer::singleShot(0,&dialog,[table,records]{table->selectRow(records.size()-1);});dialog.resize(1020,700);dialog.exec();return {};
}
}
void DrawingWindow::showNewReleaseDialog(){
    if(!editor_||busy_||editor_->isComputing())return;
    WorkspacePaths workspace;const auto located=locateDocumentWorkspace(path_,workspace);if(!located.isEmpty()){report(located);return;}
    QString error,published;{QScopedValueRollback<bool> guard(busy_,true);refreshTitle();ReleaseChoices choices;bool computing=false;const auto store=RevisionStore(workspace.revisions);const auto model=drawing_.sheet.drawing.model;
        error=runDrawingComputation(this,computing,QStringLiteral("Revisioni disponibili per il rilascio"),choices,[store,id=model.documentId](auto phase){auto result=releaseChoices(store,id);phase(5);return result;},releasePhases);if(error.isEmpty()&&choices.models.isEmpty())error=QStringLiteral("Archiviare prima un check-in del modello e delle tavole da consegnare.");
        if(error.isEmpty())published=showReleaseCreation(this,store,choices,model,drawing_.sheet.drawing.version.documentId);
    }refreshEditAccess();refreshTitle();if(!error.isEmpty())report(error);else if(!published.isEmpty())statusBar()->showMessage(QStringLiteral("Rilascio %1 pubblicato.").arg(published),15000);
}
void DrawingWindow::showReleasesDialog(){
    if(!editor_||busy_||editor_->isComputing())return;
    WorkspacePaths workspace;const auto located=locateDocumentWorkspace(path_,workspace);if(!located.isEmpty()){report(located);return;}QString error;
    try{QScopedValueRollback<bool> guard(busy_,true);refreshTitle();error=showPublishedReleases(this,RevisionStore(workspace.revisions),workspace);}catch(const std::exception &e){error=QString::fromUtf8(e.what());}refreshEditAccess();refreshTitle();report(error);
}
}
