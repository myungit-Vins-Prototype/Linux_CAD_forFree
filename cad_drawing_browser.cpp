#include "cad_drawing_browser.h"
#include "cad_drawing_compute.h"
#include "cad_workspace.h"
#include "cad_icons.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
namespace ForgeCad {
QString findLinkedDrawings(const QString &modelPath,const QUuid &modelId,QVector<LinkedDrawingEntry> &result){
    if(modelPath.isEmpty()||modelId.isNull())return QStringLiteral("Modello non salvato o privo di identita'.");
    WorkspacePaths workspace;const auto error=locateDocumentWorkspace(modelPath,workspace);if(!error.isEmpty())return error;
    QVector<LinkedDrawingEntry> found;QStringList pending{workspace.root};
    while(!pending.empty()){
        Kernel::cancellationPoint();QDir dir(pending.takeLast());
        for(const auto &info:dir.entryInfoList(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot,QDir::Name)){
            Kernel::cancellationPoint();if(info.fileName().startsWith('.')||info.isSymLink())continue;
            if(info.isDir()){pending.append(info.absoluteFilePath());continue;}
            if(info.suffix().compare("drw",Qt::CaseInsensitive)!=0)continue;
            DrawingFile file;if(loadDrawingFile(info.absoluteFilePath(),file).isEmpty()&&file.sheet.drawing.model.documentId==modelId&&file.sheet.drawing.version.checkinId.isNull())found.append({canonicalDocumentPath(info.absoluteFilePath()),std::move(file)});
        }
    }
    std::sort(found.begin(),found.end(),[](const auto &a,const auto &b){return a.path<b.path;});result=std::move(found);return {};
}
QString chooseLinkedDrawing(QWidget *owner,const QString &modelPath,const QUuid &modelId){
    QVector<LinkedDrawingEntry> entries;bool busy=false;
    if(!modelPath.isEmpty()&&!modelId.isNull()){
        const auto error=runDrawingComputation(owner,busy,QStringLiteral("Ricerca tavole collegate"),entries,[=](auto phase){QVector<LinkedDrawingEntry> found;const auto error=findLinkedDrawings(modelPath,modelId,found);if(!error.isEmpty())throw std::runtime_error(error.toStdString());phase(5);return found;},{QStringLiteral("Ricerca dei file .drw nel workspace"),QString(),QString(),QString(),QString(),QStringLiteral("Ricerca completata")});
        if(!error.isEmpty())return {};
    }
    const auto browse=[&]{return QFileDialog::getOpenFileName(owner,QStringLiteral("Apri tavola 2D"),QFileInfo(modelPath).absolutePath(),QStringLiteral("Tavola ForgeCAD (*.drw)"));};
    if(entries.empty())return browse();
    if(entries.size()==1)return entries[0].path;
    QDialog dialog(owner);dialog.setObjectName("drawingSelectionDialog");dialog.setWindowTitle(QStringLiteral("Tavole del modello — %1").arg(QFileInfo(modelPath).fileName()));dialog.resize(1050,650);
    auto *layout=new QVBoxLayout(&dialog);auto *split=new QSplitter(&dialog);auto *list=new QListWidget(split);list->setObjectName("linkedDrawingList");list->setIconSize({160,110});list->setMinimumWidth(300);
    auto *right=new QWidget(split);auto *rightLayout=new QVBoxLayout(right);auto *preview=new QLabel(right);preview->setObjectName("linkedDrawingPreview");preview->setAlignment(Qt::AlignCenter);preview->setMinimumSize(400,300);preview->setScaledContents(false);auto *details=new QLabel(right);details->setObjectName("linkedDrawingDetails");details->setTextFormat(Qt::PlainText);details->setWordWrap(true);rightLayout->addWidget(preview,1);rightLayout->addWidget(details);split->addWidget(list);split->addWidget(right);split->setStretchFactor(1,1);layout->addWidget(split,1);
    for(const auto &entry:entries){auto *item=new QListWidgetItem(commandIcon("drawingView"),QFileInfo(entry.path).fileName()+"\n"+entry.drawing.sheet.title,list);item->setToolTip(entry.path);}
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Open)->setText(QStringLiteral("Apri"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));auto *other=buttons->addButton(QStringLiteral("Sfoglia…"),QDialogButtonBox::ActionRole);layout->addWidget(buttons);QString selected;bool loading=false;QHash<int,QImage> images;
    QTimer timer;timer.setSingleShot(true);timer.setInterval(150);
    QObject::connect(list,&QListWidget::currentRowChanged,&dialog,[&](int row){if(row<0)return;preview->clear();preview->setText(QStringLiteral("Preparazione anteprima…"));const auto &entry=entries[row];details->setText(entry.path+QStringLiteral("\n%1 viste · %2 quote · %3").arg(entry.drawing.sheet.views.size()).arg(entry.drawing.sheet.drawing.dimensions.size()).arg(entry.drawing.sheet.drawing.model.checkinId.isNull()?QStringLiteral("Modello: copia di lavoro"):QStringLiteral("Modello: check-in %1").arg(entry.drawing.sheet.drawing.model.checkinId.toString(QUuid::WithoutBraces).left(8))));timer.start();});
    QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{
        const int row=list->currentRow();if(row<0||loading)return;
        if(images.contains(row)){preview->setPixmap(QPixmap::fromImage(images[row]).scaled(preview->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));return;}
        loading=true;list->setEnabled(false);buttons->setEnabled(false);
        try{
            const auto &entry=entries[row];DrawingEditorModel model;
            const auto error=runDrawingComputation(&dialog,busy,QStringLiteral("Anteprima: %1").arg(QFileInfo(entry.path).fileName()),model,[entry](auto phase){DocumentState document;QString resolved;bool changed=false;auto error=loadDrawingModel(entry.path,entry.drawing,document,resolved,changed);if(changed)throw std::runtime_error("Modello cambiato: aprire la tavola e aggiornarla per verificare la geometria.");if(!error.isEmpty())throw std::runtime_error(error.toStdString());DrawingEditorModel model;error=prepareDrawingEditorModel(document,model);if(!error.isEmpty())throw std::runtime_error(error.toStdString());phase(5);return model;},{QStringLiteral("Lettura del modello sorgente della tavola"),QString(),QString(),QString(),QString(),QStringLiteral("Modello disponibile")});
            if(!error.isEmpty())throw std::runtime_error(error.toStdString());
            DrawingSheetEditor editor(entry.drawing.sheet,std::move(model));editor.setReadOnly(true);QImage image(1000,750,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);editor.canvas()->scene()->render(&painter,QRectF(image.rect()),QRectF(QPointF(),entry.drawing.sheet.size),Qt::KeepAspectRatio);painter.end();if(images.size()>=16)images.erase(images.begin());images.insert(row,image);list->item(row)->setIcon(QIcon(QPixmap::fromImage(image.scaled(160,110,Qt::KeepAspectRatio,Qt::SmoothTransformation))));preview->setPixmap(QPixmap::fromImage(image).scaled(preview->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        }catch(const std::exception &error){preview->setText(QStringLiteral("Anteprima non disponibile\n%1").arg(QString::fromUtf8(error.what())));}
        loading=false;list->setEnabled(true);buttons->setEnabled(true);
    });
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(!loading&&list->currentRow()>=0){selected=entries[list->currentRow()].path;dialog.accept();}});
    QObject::connect(list,&QListWidget::itemDoubleClicked,&dialog,[&](QListWidgetItem *){if(!loading&&list->currentRow()>=0){selected=entries[list->currentRow()].path;dialog.accept();}});
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);QObject::connect(other,&QPushButton::clicked,&dialog,[&]{const auto path=browse();if(!path.isEmpty()){selected=path;dialog.accept();}});list->setCurrentRow(0);
    return dialog.exec()==QDialog::Accepted?selected:QString();
}
}
