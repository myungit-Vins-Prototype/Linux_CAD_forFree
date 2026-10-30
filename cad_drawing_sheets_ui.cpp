#include "cad_drawing_window.h"
#include "cad_drawing_focus.h"
#include "cad_drawing_compute.h"
#include "cad_menu.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

namespace ForgeCad {
namespace {
int indexOfSheet(const DrawingFile &file,const QUuid &id){for(int i=0;i<drawingSheetCount(file);++i)if(drawingSheetId(file,i)==id)return i;return -1;}
void assignSheets(DrawingFile &file,const QVector<DrawingSheet> &sheets){file.sheet=sheets.front();file.otherSheets=sheets.mid(1);}
QVector<DrawingSheet> sheetsOf(const DrawingFile &file){QVector<DrawingSheet> sheets{file.sheet};sheets+=file.otherSheets;return sheets;}
void ensureIds(DrawingFile &file){if(file.sheetIds.isEmpty())file.sheetIds.append(file.sheet.drawing.version.documentId);}
}
void DrawingWindow::setupSheetActions(){
    auto *menu=addCadMenu(menuBar(),QStringLiteral("Tavola"));
    auto *add=menu->addAction(QStringLiteral("Nuovo foglio…"));add->setObjectName("drawingNewSheetAction");connect(add,&QAction::triggered,this,[this]{showNewSheetDialog();});
    auto *manage=menu->addAction(QStringLiteral("Gestisci fogli…"));manage->setObjectName("drawingManageSheetsAction");connect(manage,&QAction::triggered,this,[this]{showManageSheetsDialog();});
    if(auto *bar=findChild<QToolBar *>("drawingDocumentBar")){bar->addSeparator();bar->addAction(add);bar->addAction(manage);}
}
void DrawingWindow::installSheetTabs(QVBoxLayout *layout){
    auto *row=new QWidget(layout->parentWidget());auto *horizontal=new QHBoxLayout(row);horizontal->setContentsMargins(0,0,0,0);
    sheetTabs_=new QTabBar(row);sheetTabs_->setObjectName("drawingSheetTabs");sheetTabs_->setExpanding(false);sheetTabs_->setMovable(true);sheetTabs_->setUsesScrollButtons(true);sheetTabs_->setElideMode(Qt::ElideMiddle);horizontal->addWidget(sheetTabs_,1);
    newSheetButton_=new QPushButton(QStringLiteral("+ Foglio"),row);newSheetButton_->setObjectName("drawingNewSheetButton");horizontal->addWidget(newSheetButton_);layout->addWidget(row);
    connect(newSheetButton_,&QPushButton::clicked,this,[this]{showNewSheetDialog();});
    connect(sheetTabs_,&QTabBar::currentChanged,this,[this](int index){if(index>=0&&index<drawingSheetCount(drawing_)&&sheetTabs_->tabData(index).toUuid()==drawingSheetId(drawing_,index)){const auto error=setActiveSheet(index);if(!error.isEmpty()){refreshSheetTabs();report(error);}}});
    connect(sheetTabs_,&QTabBar::tabMoved,this,[this](int from,int to){const auto error=moveSheet(from,to);if(!error.isEmpty()){refreshSheetTabs();report(error);}});
}
void DrawingWindow::refreshSheetTabs(){
    if(!editor_||!sheetTabs_)return;
    const QSignalBlocker block(sheetTabs_);while(sheetTabs_->count()>drawingSheetCount(drawing_))sheetTabs_->removeTab(sheetTabs_->count()-1);
    for(int i=0;i<drawingSheetCount(drawing_);++i){const auto &sheet=drawingSheetAt(drawing_,i);const auto label=QStringLiteral("%1 · %2  [%3]").arg(i+1).arg(sheet.title,drawingPaperName(sheet.size));if(i>=sheetTabs_->count())sheetTabs_->addTab(label);else sheetTabs_->setTabText(i,label);sheetTabs_->setTabData(i,drawingSheetId(drawing_,i));sheetTabs_->setTabToolTip(i,sheet.title+QStringLiteral(" — %1 × %2 mm").arg(sheet.size.width()).arg(sheet.size.height()));}
    sheetTabs_->setCurrentIndex(activeSheet_);sheetTabs_->setMovable(!editor_->isReadOnly());
    newSheetButton_->setEnabled(!editor_->isReadOnly()&&!busy_&&!editor_->isComputing());
    auto context=editor_->pageContext();context.sheetNumber=activeSheet_+1;context.sheetCount=drawingSheetCount(drawing_);editor_->setPageContext(context);
    editor_->setDocumentHistory([this](bool redo){return restoreSheets(redo);},sheetHistoryIndex_>0,sheetHistoryIndex_+1<sheetHistory_.size(),sheetHistoryIndex_>0?sheetHistory_[sheetHistoryIndex_].label:QString(),sheetHistoryIndex_+1<sheetHistory_.size()?sheetHistory_[sheetHistoryIndex_+1].label:QString());
}
QString DrawingWindow::setActiveSheet(int index){
    if(!editor_||busy_||editor_->isComputing())return QStringLiteral("Attendere il calcolo in corso.");
    if(index<0||index>=drawingSheetCount(drawing_))return QStringLiteral("Foglio non trovato.");
    if(index!=activeSheet_){const auto context=editor_->pageContext();const auto error=editor_->loadSheet(drawingSheetAt(drawing_,index));if(!error.isEmpty())return error;activeSheet_=index;editor_->setPageContext(context);editor_->fitSheet();}
    refreshSheetTabs();return {};
}
QString DrawingWindow::sheetMutationError(){
    if(!editor_||busy_||editor_->isComputing())return QStringLiteral("Attendere il calcolo in corso.");
    const auto error=requireEditAccess();if(!error.isEmpty())return error;
    return editor_->isReadOnly()?QStringLiteral("La tavola e' in sola lettura o il modello non e' verificato."):QString();
}
void DrawingWindow::resetSheetsHistory(bool clean){
    QByteArray bytes;const auto error=encodeDrawingFile(drawing_,bytes);if(!error.isEmpty()){statusBar()->showMessage(error);return;}
    sheetHistory_={{bytes,drawingSheetId(drawing_,activeSheet_),{}}};sheetHistoryIndex_=0;if(clean)savedDrawing_=bytes;refreshSheetTabs();
}
void DrawingWindow::markSheetsSaved(){
    // Salva normalizza i percorsi del contenitore, senza creare un passo Undo.
    for(auto &entry:sheetHistory_){DrawingFile file;if(decodeDrawingFile(entry.bytes,file).isEmpty()){file.modelPath=drawing_.modelPath;file.modelArchivePath=drawing_.modelArchivePath;encodeDrawingFile(file,entry.bytes);}}
    encodeDrawingFile(drawing_,savedDrawing_);
}
void DrawingWindow::recordSheets(const QString &label){
    QByteArray bytes;const auto error=encodeDrawingFile(drawing_,bytes);if(!error.isEmpty()){statusBar()->showMessage(error);return;}
    if(sheetHistory_.isEmpty()){resetSheetsHistory(false);return;}
    if(bytes!=sheetHistory_[sheetHistoryIndex_].bytes){sheetHistory_.resize(sheetHistoryIndex_+1);sheetHistory_.append({bytes,drawingSheetId(drawing_,activeSheet_),label});++sheetHistoryIndex_;}
    qsizetype total=0;for(const auto &entry:sheetHistory_)total+=entry.bytes.size();while(sheetHistory_.size()>1&&(sheetHistory_.size()>101||total>32*1024*1024)){total-=sheetHistory_.front().bytes.size();sheetHistory_.removeFirst();--sheetHistoryIndex_;}
    modified_=bytes!=savedDrawing_;containerDirty_=modified_;refreshSheetTabs();refreshTitle();
}
void DrawingWindow::sheetChanged(const DrawingSheet &sheet){
    if(restoringSheets_)return;
    auto prepared=drawing_;drawingSheetAt(prepared,activeSheet_)=sheet;QByteArray validation;const auto error=encodeDrawingFile(prepared,validation);
    if(!error.isEmpty()){editor_->loadSheet(drawingSheetAt(drawing_,activeSheet_));statusBar()->showMessage(error,15000);return;}
    drawing_=std::move(prepared);recordSheets(QStringLiteral("Modifica foglio %1").arg(sheet.title));
}
QString DrawingWindow::commitSheets(DrawingFile prepared,const QUuid &active,const QString &label){
    const auto access=sheetMutationError();if(!access.isEmpty())return access;
    QByteArray bytes;const auto valid=encodeDrawingFile(prepared,bytes);if(!valid.isEmpty())return valid;
    int index=indexOfSheet(prepared,active);if(index<0)index=0;
    const bool fit=drawingSheetId(drawing_,activeSheet_)!=drawingSheetId(prepared,index)||editor_->sheet().size!=drawingSheetAt(prepared,index).size;
    const auto context=editor_->pageContext();const auto error=editor_->loadSheet(drawingSheetAt(prepared,index));if(!error.isEmpty())return error;
    drawing_=std::move(prepared);activeSheet_=index;editor_->setPageContext(context);recordSheets(label);if(fit)editor_->fitSheet();return {};
}
QString DrawingWindow::restoreSheets(bool redo){
    const auto access=sheetMutationError();if(!access.isEmpty())return access;
    const int target=sheetHistoryIndex_+(redo?1:-1);if(target<0||target>=sheetHistory_.size())return QStringLiteral("Nessuna modifica da ripristinare.");
    DrawingFile prepared;const auto decoded=decodeDrawingFile(sheetHistory_[target].bytes,prepared);if(!decoded.isEmpty())return decoded;
    const auto focus=sheetHistory_[redo?target:sheetHistoryIndex_].active;int index=indexOfSheet(prepared,focus);if(index<0)index=indexOfSheet(prepared,sheetHistory_[target].active);if(index<0)index=0;
    const auto &a=prepared.sheet.drawing.model;const auto &b=drawing_.sheet.drawing.model;
    if(a.documentId!=b.documentId||a.checkinId!=b.checkinId||prepared.modelDefinitionHash!=drawing_.modelDefinitionHash||prepared.modelArchiveId!=drawing_.modelArchiveId||prepared.modelArchivePath!=drawing_.modelArchivePath||prepared.modelPath!=drawing_.modelPath)
        return restoreDrawingState(std::move(prepared),index,{},target);
    const auto context=editor_->pageContext();const auto error=editor_->loadSheet(drawingSheetAt(prepared,index));if(!error.isEmpty())return error;
    drawing_=std::move(prepared);activeSheet_=index;sheetHistoryIndex_=target;modified_=sheetHistory_[target].bytes!=savedDrawing_;containerDirty_=modified_;editor_->setPageContext(context);refreshSheetTabs();refreshTitle();return {};
}
QString DrawingWindow::addSheet(const QString &name,const QSizeF &size){
    const auto access=sheetMutationError();if(!access.isEmpty())return access;
    auto prepared=drawing_;ensureIds(prepared);DrawingSheet sheet;sheet.drawing.version=drawing_.sheet.drawing.version;sheet.drawing.model=drawing_.sheet.drawing.model;sheet.title=name.trimmed();sheet.size=size;sheet.titleBlock=editor_->sheet().titleBlock;sheet.style=editor_->sheet().style;
    const auto id=QUuid::createUuid();prepared.otherSheets.append(std::move(sheet));prepared.sheetIds.append(id);return commitSheets(std::move(prepared),id,QStringLiteral("Nuovo foglio"));
}
QString DrawingWindow::duplicateSheet(int index){
    const auto access=sheetMutationError();if(!access.isEmpty())return access;
    if(index<0||index>=drawingSheetCount(drawing_))return QStringLiteral("Foglio non trovato.");
    DrawingSheet copy;const auto cloned=cloneDrawingSheet(drawingSheetAt(drawing_,index),copy);if(!cloned.isEmpty())return cloned;copy.title=copy.title.left(248)+QStringLiteral(" — copia");
    auto prepared=drawing_;ensureIds(prepared);auto sheets=sheetsOf(prepared);sheets.insert(index+1,std::move(copy));assignSheets(prepared,sheets);const auto id=QUuid::createUuid();prepared.sheetIds.insert(index+1,id);return commitSheets(std::move(prepared),id,QStringLiteral("Duplica foglio"));
}
QString DrawingWindow::renameSheet(int index,const QString &name){
    if(!editor_||index<0||index>=drawingSheetCount(drawing_))return QStringLiteral("Foglio non trovato.");
    auto prepared=drawing_;drawingSheetAt(prepared,index).title=name.trimmed();return commitSheets(std::move(prepared),drawingSheetId(drawing_,activeSheet_),QStringLiteral("Rinomina foglio"));
}
QString DrawingWindow::moveSheet(int from,int to){
    if(!editor_||from<0||to<0||from>=drawingSheetCount(drawing_)||to>=drawingSheetCount(drawing_))return QStringLiteral("Ordine dei fogli non valido.");
    const auto access=sheetMutationError();if(!access.isEmpty())return access;
    if(from==to)return {};
    auto prepared=drawing_;ensureIds(prepared);const auto active=drawingSheetId(drawing_,activeSheet_);auto sheets=sheetsOf(prepared);sheets.move(from,to);prepared.sheetIds.move(from,to);assignSheets(prepared,sheets);return commitSheets(std::move(prepared),active,QStringLiteral("Riordina fogli"));
}
QString DrawingWindow::removeSheet(int index){
    if(!editor_||index<0||index>=drawingSheetCount(drawing_))return QStringLiteral("Foglio non trovato.");
    if(drawingSheetCount(drawing_)==1)return QStringLiteral("La tavola deve conservare almeno un foglio.");
    auto prepared=drawing_;ensureIds(prepared);auto sheets=sheetsOf(prepared);sheets.removeAt(index);prepared.sheetIds.removeAt(index);const auto active=index==activeSheet_?prepared.sheetIds[std::min(index,int(sheets.size())-1)]:drawingSheetId(drawing_,activeSheet_);assignSheets(prepared,sheets);return commitSheets(std::move(prepared),active,QStringLiteral("Elimina foglio"));
}
QString DrawingWindow::updateSheetsModel(DrawingFile prepared,DrawingEditorModel model){
    const bool previousBusy=busy_;busy_=true;refreshTitle();
    const auto finish=[&](QString error){busy_=previousBusy;refreshEditAccess();refreshTitle();return error;};
    // Prepara dati e geometria una sola volta per foglio, senza editor temporanei.
    // Nessuna mutazione fino alla validazione di tutti i fogli e del contenitore.
    try{
        PreparedDrawingSheet active;
        for(int i=0;i<drawingSheetCount(drawing_);++i){
            auto sheet=retargetDrawingSheetModel(drawingSheetAt(prepared,i),model);
            PreparedDrawingSheet result;
            if(sheet.drawing.views.empty())result=prepareDrawingSheet(std::move(sheet),model,[](int){});
            else{
                bool computing=false;
                const auto error=runDrawingComputation(this,computing,QStringLiteral("Aggiorna foglio %1 di %2").arg(i+1).arg(drawingSheetCount(drawing_)),result,
                    [sheet=std::move(sheet),model](auto phase)mutable{return prepareDrawingSheet(std::move(sheet),model,phase);});
                if(!error.isEmpty())return finish(error);
            }
            drawingSheetAt(prepared,i)=result.sheet;
            if(i==activeSheet_)active=std::move(result);
        }
        synchronizeDrawingFileVersions(prepared);
        QByteArray bytes;const auto valid=encodeDrawingFile(prepared,bytes);if(!valid.isEmpty())return finish(valid);
        active.sheet=drawingSheetAt(prepared,activeSheet_);
        editor_->installPreparedModel(std::move(active));
        drawing_=std::move(prepared);model_=std::move(model);modified_=true;containerDirty_=true;
        resetSheetsHistory(false);return finish({});
    }catch(const std::exception &error){return finish(QString::fromUtf8(error.what()));}
}
void DrawingWindow::showNewSheetDialog(){
    if(!sheetMutationError().isEmpty())return;
    DrawingFocusReturn focus(this);QDialog dialog(this);dialog.setObjectName("drawingNewSheetDialog");dialog.setWindowTitle(QStringLiteral("Nuovo foglio"));auto *form=new QFormLayout(&dialog);
    auto *name=new QLineEdit(QStringLiteral("Foglio %1").arg(sheetCount()+1),&dialog);name->setObjectName("drawingNewSheetName");name->setMaxLength(128);form->addRow(QStringLiteral("Nome:"),name);
    auto *format=new QComboBox(&dialog);format->setObjectName("drawingNewSheetFormat");for(const auto &value:{"A3","A4","A2","A1","A0"})format->addItem(QString::fromLatin1(value));form->addRow(QStringLiteral("Formato:"),format);
    auto *orientation=new QComboBox(&dialog);orientation->setObjectName("drawingNewSheetOrientation");orientation->addItems({QStringLiteral("Orizzontale"),QStringLiteral("Verticale")});form->addRow(QStringLiteral("Orientamento:"),orientation);
    auto *error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Crea foglio"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));form->addRow(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{const QSizeF sizes[]={{297,420},{210,297},{420,594},{594,841},{841,1189}};auto size=sizes[format->currentIndex()];if(orientation->currentIndex()==0)size.transpose();const auto message=addSheet(name->text(),size);if(message.isEmpty())dialog.accept();else error->setText(message);});dialog.exec();
}
void DrawingWindow::showManageSheetsDialog(){
    if(!editor_||busy_||editor_->isComputing())return;
    DrawingFocusReturn focus(this);QDialog dialog(this);dialog.setObjectName("drawingManageSheetsDialog");dialog.setWindowTitle(QStringLiteral("Gestisci fogli"));dialog.resize(720,360);auto *layout=new QVBoxLayout(&dialog);
    auto *table=new QTableWidget(&dialog);table->setObjectName("drawingSheetsTable");table->setColumnCount(4);table->setHorizontalHeaderLabels({QStringLiteral("Foglio"),QStringLiteral("Nome"),QStringLiteral("Formato"),QStringLiteral("Ordine e operazioni")});table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);table->verticalHeader()->hide();table->setSelectionBehavior(QAbstractItemView::SelectRows);layout->addWidget(table);
    auto *message=new QLabel(&dialog);message->setWordWrap(true);layout->addWidget(message);bool filling=false;
    std::function<void()> populate=[&]{filling=true;table->setRowCount(sheetCount());for(int i=0;i<sheetCount();++i){const auto &sheet=drawingSheetAt(drawing_,i);auto *number=new QTableWidgetItem(QString::number(i+1));number->setFlags(number->flags()&~Qt::ItemIsEditable);table->setItem(i,0,number);auto *name=new QTableWidgetItem(sheet.title);if(editor_->isReadOnly())name->setFlags(name->flags()&~Qt::ItemIsEditable);table->setItem(i,1,name);auto *format=new QTableWidgetItem(drawingPaperName(sheet.size)+QStringLiteral(" %1×%2").arg(sheet.size.width()).arg(sheet.size.height()));format->setFlags(format->flags()&~Qt::ItemIsEditable);table->setItem(i,2,format);
        auto *tools=new QWidget(table);auto *row=new QHBoxLayout(tools);row->setContentsMargins(2,2,2,2);row->setSpacing(3);
        for(int action=0;action<4;++action){auto *button=new QPushButton(QStringList{QStringLiteral("Su"),QStringLiteral("Giù"),QStringLiteral("Duplica"),QStringLiteral("Elimina")}[action],tools);button->setEnabled(!editor_->isReadOnly()&&(action!=0||i>0)&&(action!=1||i+1<sheetCount())&&(action!=3||sheetCount()>1));row->addWidget(button);connect(button,&QPushButton::clicked,&dialog,[&,i,action]{QString error;if(action==0)error=moveSheet(i,i-1);else if(action==1)error=moveSheet(i,i+1);else if(action==2)error=duplicateSheet(i);else{if(QMessageBox::question(&dialog,QStringLiteral("Elimina foglio"),QStringLiteral("Eliminare «%1» con viste e annotazioni?").arg(drawingSheetAt(drawing_,i).title),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;error=removeSheet(i);}message->setText(error);populate();});}
        table->setCellWidget(i,3,tools);table->setRowHeight(i,38);}
        table->resizeColumnToContents(0);table->resizeColumnToContents(2);table->resizeColumnToContents(3);filling=false;};
    connect(table,&QTableWidget::itemChanged,&dialog,[&](QTableWidgetItem *item){if(filling||item->column()!=1)return;message->setText(renameSheet(item->row(),item->text()));});
    connect(table,&QTableWidget::cellDoubleClicked,&dialog,[&](int row,int column){if(column!=1){const auto error=setActiveSheet(row);if(error.isEmpty())dialog.accept();else message->setText(error);}});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Chiudi"));auto *add=buttons->addButton(QStringLiteral("Nuovo foglio…"),QDialogButtonBox::ActionRole);add->setEnabled(!editor_->isReadOnly());connect(add,&QPushButton::clicked,&dialog,[&]{showNewSheetDialog();populate();});connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);populate();dialog.exec();
}
void DrawingWindow::showPdfExportDialog(){
    if(!editor_)return;
    DrawingFocusReturn focus(this);bool all=true;
    if(sheetCount()>1){QDialog dialog(this);dialog.setObjectName("drawingPdfSheetsDialog");dialog.setWindowTitle(QStringLiteral("PDF della tavola"));auto *layout=new QVBoxLayout(&dialog);auto *every=new QRadioButton(QStringLiteral("Tutti i fogli (%1 pagine)").arg(sheetCount()),&dialog);every->setObjectName("drawingPdfAllSheets");every->setChecked(true);auto *current=new QRadioButton(QStringLiteral("Solo il foglio attivo: %1").arg(editor_->sheet().title),&dialog);current->setObjectName("drawingPdfCurrentSheet");layout->addWidget(every);layout->addWidget(current);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Continua"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;all=every->isChecked();}
    const QFileInfo source(path_.isEmpty()?sourcePath_:path_);auto path=QFileDialog::getSaveFileName(this,QStringLiteral("Esporta tavola in PDF"),source.absolutePath()+"/"+source.completeBaseName()+".pdf",QStringLiteral("Documento PDF (*.pdf)"));if(path.isEmpty())return;if(QFileInfo(path).suffix().isEmpty())path+=".pdf";DrawingPdfReport result;const auto error=exportPdfPath(path,result,all);if(!error.isEmpty()){report(error);return;}statusBar()->showMessage(QStringLiteral("PDF creato: %1").arg(path)+(result.warnings.isEmpty()?QString():QStringLiteral(" · ")+result.warnings.join(QStringLiteral(" "))),15000);
}
}
