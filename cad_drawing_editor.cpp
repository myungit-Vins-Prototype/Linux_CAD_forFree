#include "cad_drawing_editor.h"
#include "cad_icons.h"
#include "cad_menu.h"
#include "cad_toolbar.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_tessellate.h"
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QSignalBlocker>
#include <QSet>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QFormLayout>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSpinBox>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace ForgeCad {
namespace {
using namespace Kernel;
constexpr int kindRole=0,viewRole=1,indexRole=2,idRole=3;
struct SheetCanvas final:QGraphicsView{
    using QGraphicsView::QGraphicsView;
    std::function<bool(QPointF,Qt::KeyboardModifiers,bool)> pointer;
    std::function<bool(int)> key;
    void mouseMoveEvent(QMouseEvent *event)override{if(pointer&&pointer(mapToScene(event->position().toPoint()),event->modifiers(),false)){event->accept();return;}QGraphicsView::mouseMoveEvent(event);}
    void mousePressEvent(QMouseEvent *event)override{if(event->button()==Qt::RightButton&&key&&key(Qt::Key_Escape)){event->accept();return;}
        if(event->button()==Qt::LeftButton&&pointer&&pointer(mapToScene(event->position().toPoint()),event->modifiers(),true)){event->accept();return;}QGraphicsView::mousePressEvent(event);}
    bool event(QEvent *event)override{if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent *>(event)->key()==Qt::Key_Tab&&key&&key(Qt::Key_Tab)){event->accept();return true;}return QGraphicsView::event(event);}
    void mouseReleaseEvent(QMouseEvent *event)override{if(pointer&&pointer(mapToScene(event->position().toPoint()),event->modifiers(),false)){event->accept();return;}QGraphicsView::mouseReleaseEvent(event);}
    void keyPressEvent(QKeyEvent *event)override{if(key&&key(event->key())){event->accept();return;}QGraphicsView::keyPressEvent(event);}
    void leaveEvent(QEvent *event)override{if(key)key(Qt::Key_Clear);QGraphicsView::leaveEvent(event);}
    void wheelEvent(QWheelEvent *event)override{
        const double factor=event->angleDelta().y()>0?1.15:1/1.15;
        if(transform().m11()*factor>=0.1&&transform().m11()*factor<=100)scale(factor,factor);
        event->accept();
    }
};
struct SectionItem final:QGraphicsPathItem{
    using QGraphicsPathItem::QGraphicsPathItem;
    bool sectionReference=false;
    QRectF boundingRect()const override{return path().boundingRect().adjusted(-0.7,-0.7,0.7,0.7);}
    QPainterPath shape()const override{QPainterPathStroker stroke;stroke.setWidth(1.2);return stroke.createStroke(path());}
    void paint(QPainter *painter,const QStyleOptionGraphicsItem *,QWidget *)override{
        painter->setPen(QPen(isSelected()?QColor("#1469c0"):(sectionReference?QColor("#187c59"):QColor("#222222")),isSelected()?0.65:sectionReference?0.25:0.3,
            sectionReference?Qt::DotLine:Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);painter->drawPath(path());
    }
};
struct MoveLabel final:QGraphicsSimpleTextItem{
    QPointF pressPosition;
    std::function<void(QPointF,QPointF)> moved;
    std::function<void()> activated;
    explicit MoveLabel(const QString &text):QGraphicsSimpleTextItem(text){setFlags(ItemIsMovable|ItemIsSelectable);setZValue(8);}
    void mousePressEvent(QGraphicsSceneMouseEvent *event)override{pressPosition=pos();QGraphicsSimpleTextItem::mousePressEvent(event);}
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)override{if(activated){activated();event->accept();}else QGraphicsSimpleTextItem::mouseDoubleClickEvent(event);}
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event)override{
        QGraphicsSimpleTextItem::mouseReleaseEvent(event);if(moved&&pos()!=pressPosition)moved(pressPosition,pos());
    }
};
QPointF paperPoint(const DrawingView &view,const DrawingViewLayout &layout,const Vec3 &point){
    const auto local=view.plane.toLocal(point);return layout.position+QPointF(local.x()*view.scale,-local.y()*view.scale);
}
QPainterPath sectionPath(const DrawingView &view,const DrawingViewLayout &layout,const DrawingSection &section){
    TessellationOptions options;options.deflection=std::clamp(0.03/view.scale,1e-6,1.0);options.angle=0.12;
    const auto samples=sampleCurve(*section.curve,section.range,options);
    if(samples.size()>200000)throw std::runtime_error("Curva troppo complessa per l'anteprima della tavola.");
    QPainterPath path;for(std::size_t i=0;i<samples.size();++i){const auto point=paperPoint(view,layout,section.curve->point(samples[i]));
        if(!std::isfinite(point.x())||!std::isfinite(point.y()))throw std::runtime_error("Curva della tavola non finita.");
        if(i)path.lineTo(point);else path.moveTo(point);}
    return path;
}
bool sameGeometry(const DrawingView &a,const DrawingView &b){
    return a.kind==b.kind&&a.sectionReferences==b.sectionReferences&&a.tolerance==b.tolerance
        &&distance(a.plane.origin(),b.plane.origin())==0&&distance(a.plane.xDir(),b.plane.xDir())==0
        &&distance(a.plane.yDir(),b.plane.yDir())==0&&distance(a.plane.zDir(),b.plane.zDir())==0;
}
bool sameDimension(const DrawingDimension &a,const DrawingDimension &b){
    if(a.angleSides!=b.angleSides||a.decimals!=b.decimals||a.references.size()!=b.references.size())return false;
    for(std::size_t i=0;i<a.references.size();++i){const auto &x=a.references[i],&y=b.references[i];
        if(x.role!=y.role||x.fraction!=y.fraction||x.polar!=y.polar||x.vertexName!=y.vertexName||x.boundaryName!=y.boundaryName||x.supportFace!=y.supportFace||x.curveSide!=y.curveSide||x.anchorSide!=y.anchorSide
            ||x.curve.featureId!=y.curve.featureId||x.curve.kind!=y.curve.kind||x.curve.faceName!=y.curve.faceName||x.curve.edgeName!=y.curve.edgeName||x.curve.requiresWitness!=y.curve.requiresWitness||distance(x.curve.witness,y.curve.witness)!=0)return false;}
    const auto &x=a.section,&y=b.section;
    return a.viewId==b.viewId&&a.measure==b.measure&&x.featureId==y.featureId&&x.kind==y.kind&&x.faceName==y.faceName&&x.edgeName==y.edgeName
        &&x.requiresWitness==y.requiresWitness&&distance(x.witness,y.witness)==0;
}
void fontFor(QGraphicsSimpleTextItem *item,double scale=0.25){QFont font("DejaVu Sans");font.setPixelSize(12);item->setFont(font);item->setScale(scale);}
}
struct DrawingSheetEditor::Impl{
    DrawingSheetEditor *owner;
    DrawingSheet state;
    DrawingEditorModel model;
    SheetCanvas *view=nullptr;
    QGraphicsScene *scene=nullptr;
    QLabel *message=nullptr;
    QAction *updateAction=nullptr;
    QTreeWidget *tree=nullptr;
    bool syncingSelection=false;
    struct HistoryEntry{QByteArray sheet;QString label;};
    std::vector<HistoryEntry> history;
    std::size_t historyIndex=0;
    QByteArray savedSheet;
    QAction *undoAction=nullptr,*redoAction=nullptr;
    std::map<QUuid,std::vector<DrawingSection>> curves;
    std::map<QUuid,std::vector<DrawingSection>> sectionCurves;
    std::map<QUuid,DrawingDimensionResult> results;
    void cacheSections(){sectionCurves.clear();for(const auto &[id,pieces]:curves)for(const auto &piece:pieces)
        if(piece.kind==DrawingCurveKind::Section)sectionCurves[id].push_back(piece);}
    std::function<void(const DrawingSheet &)> changed;
    std::function<DrawingEditorModel()> provider;
    bool rebuilding=false;
    bool readOnly=false;
    QAction *quoteAction=nullptr;
    QComboBox *quoteType=nullptr;
    struct SnapPick{QUuid view;std::size_t index=0;DrawingPointRole role=DrawingPointRole::Curve;double parameter=std::numeric_limits<double>::quiet_NaN();QPointF paper;double gap=0;};
    std::vector<SnapPick> notable,snapChoices;
    std::map<QUuid,std::vector<QPainterPath>> paintedPaths;
    std::vector<DrawingAttachment> quoteReferences;
    QUuid quoteView;
    QPointF quoteCursor,lastSnapCursor;
    Qt::KeyboardModifiers quoteModifiers{};
    std::size_t snapCycle=0;
    std::vector<QGraphicsItem *> overlay;
    Impl(DrawingSheetEditor *widget,DrawingSheet sheet,DrawingEditorModel source):owner(widget),state(std::move(sheet)),model(std::move(source)){}
    void say(const QString &text){message->setText(text);message->setToolTip(text);}
    void report(const QString &error){if(!error.isEmpty())QMessageBox::warning(owner,QStringLiteral("Tavola 2D"),error);}
    void notify(){if(changed)changed(state);}
    QByteArray encodedState()const{QByteArray bytes;const auto error=encodeDrawingSheet(state,bytes);if(!error.isEmpty())throw std::runtime_error(error.toStdString());return bytes;}
    void refreshHistoryActions(){
        const bool previous=historyIndex>0,next=historyIndex+1<history.size();
        undoAction->setEnabled(!readOnly&&previous);redoAction->setEnabled(!readOnly&&next);
        undoAction->setText(previous?QStringLiteral("Annulla: %1").arg(history[historyIndex].label):QStringLiteral("Annulla"));
        redoAction->setText(next?QStringLiteral("Ripristina: %1").arg(history[historyIndex+1].label):QStringLiteral("Ripristina"));
        undoAction->setToolTip(undoAction->text());redoAction->setToolTip(redoAction->text());
    }
    void resetHistory(){history={{encodedState(),{}}};historyIndex=0;refreshHistoryActions();}
    void record(const QString &label,QByteArray bytes={}){
        if(bytes.isEmpty())bytes=encodedState();
        if(!history.empty()&&bytes==history[historyIndex].sheet)return;
        history.erase(history.begin()+std::ptrdiff_t(historyIndex+1),history.end());history.push_back({bytes,label});historyIndex=history.size()-1;
        qsizetype size=0;for(const auto &entry:history)size+=entry.sheet.size();
        // Massimo 100 passi e 16 MiB di JSON, senza geometria o snapshot del modello.
        while(history.size()>1&&(history.size()>101||size>16*1024*1024)){size-=history.front().sheet.size();history.erase(history.begin());--historyIndex;}
        refreshHistoryActions();notify();
    }
    QString restore(std::size_t index){
        try{
            DrawingSheet next;const auto error=decodeDrawingSheet(history[index].sheet,next);if(!error.isEmpty())return error;
            std::map<QUuid,std::vector<DrawingCurve>> prepared;QSet<QUuid> changedViews;
            for(const auto &definition:next.drawing.views){const auto layout=std::find_if(next.views.begin(),next.views.end(),[&](const auto &l){return l.viewId==definition.id;});
                const auto *old=viewFor(definition.id);const auto *oldLayout=layoutFor(definition.id);const auto cached=curves.find(definition.id);
                if(old&&oldLayout&&sameGeometry(*old,definition)&&oldLayout->features==layout->features&&cached!=curves.end())prepared[definition.id]=cached->second;
                else{std::vector<DrawingCurve> pieces;const auto generated=drawingViewCurves(sourceFor(*layout),definition,pieces);if(!generated.isEmpty())return generated;
                    prepared[definition.id]=std::move(pieces);changedViews.insert(definition.id);}
                for(const auto &piece:prepared[definition.id])sectionPath(definition,*layout,piece);
            }
            std::map<QUuid,DrawingDimensionResult> measured;
            for(const auto &dimension:next.drawing.dimensions){const auto found=results.find(dimension.id);
                const auto old=std::find_if(state.drawing.dimensions.begin(),state.drawing.dimensions.end(),[&](const auto &d){return d.id==dimension.id;});
                measured[dimension.id]=!changedViews.contains(dimension.viewId)&&old!=state.drawing.dimensions.end()&&sameDimension(*old,dimension)&&found!=results.end()
                    ?found->second:this->measured(next,dimension,prepared.at(dimension.viewId));}
            state=std::move(next);curves=std::move(prepared);results=std::move(measured);cacheSections();historyIndex=index;refreshHistoryActions();notify();redraw();return {};
        }catch(const std::exception &error){return QString::fromUtf8(error.what());}
    }
    QString applyLayout(DrawingSheet next,const QString &label){
        if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        if(bytes==history[historyIndex].sheet)return {};
        state=std::move(next);record(label,std::move(bytes));queueRedraw();return {};
    }
    QString moveViewTo(const QUuid &id,const QPointF &position,const QString &label){
        const auto *layout=layoutFor(id);if(!layout)return QStringLiteral("Vista non trovata.");const auto delta=position-layout->position;
        auto next=state;for(auto &p:next.views)if(p.viewId==id)p.position=position;
        for(auto &p:next.dimensions){const auto dimension=std::find_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return d.id==p.dimensionId;});
            if(dimension!=next.drawing.dimensions.end()&&dimension->viewId==id)p.position+=delta;}
        return applyLayout(std::move(next),label);
    }
    bool centerFor(const QUuid &id,QPointF &center)const{
        const auto *definition=viewFor(id);const auto *layout=layoutFor(id);const auto found=curves.find(id);if(!definition||!layout||found==curves.end()||found->second.empty())return false;
        QRectF bounds;bool first=true;for(const auto &piece:found->second){const auto box=sectionPath(*definition,*layout,piece).boundingRect();bounds=first?box:bounds.united(box);first=false;}
        center=bounds.center();return true;
    }
    void alignDialog(){
        if(readOnly)return;
        const auto id=selectedView();if(id.isNull()){say(QStringLiteral("Seleziona la vista da allineare."));return;}
        QDialog dialog(owner);dialog.setObjectName("drawingAlignViewsDialog");dialog.setWindowTitle(QStringLiteral("Allinea vista"));auto *form=new QFormLayout(&dialog);
        auto *reference=new QComboBox(&dialog);reference->setObjectName("drawingAlignmentReference");for(const auto &layout:state.views)if(layout.viewId!=id)reference->addItem(layout.title,layout.viewId);
        if(!reference->count()){say(QStringLiteral("Inserisci almeno due viste per allinearle."));return;}
        auto *direction=new QComboBox(&dialog);direction->setObjectName("drawingAlignmentDirection");direction->addItems({QStringLiteral("Centri sulla stessa riga"),QStringLiteral("Centri sulla stessa colonna")});
        form->addRow(QStringLiteral("Vista di riferimento:"),reference);form->addRow(QStringLiteral("Allineamento:"),direction);
        auto *note=new QLabel(QStringLiteral("Posiziona i centri sul foglio. Dopo l'allineamento le viste si possono spostare liberamente."),&dialog);note->setWordWrap(true);form->addRow(note);
        auto *error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
            const auto result=owner->alignView(id,reference->currentData().toUuid(),direction->currentIndex()==0?ViewAlignment::SameRow:ViewAlignment::SameColumn);
            error->setText(result);if(result.isEmpty())dialog.accept();});dialog.exec();
    }
    DrawingModelSnapshot sourceFor(const DrawingViewLayout &layout)const{
        DrawingModelSnapshot source;source.version=model.snapshot.version;
        for(const auto &body:model.snapshot.bodies)if(layout.features.contains(body.featureId))source.bodies.push_back(body);
        return source;
    }
    DrawingDimensionResult measured(const DrawingSheet &sheet,const DrawingDimension &d,const std::vector<DrawingCurve> &pieces)const{
        if(d.references.empty())return evaluateDrawingDimension(sheet.drawing,model.snapshot,d.id);
        const auto definition=std::find_if(sheet.drawing.views.begin(),sheet.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});
        if(definition==sheet.drawing.views.end()||sheet.drawing.model.documentId!=model.snapshot.version.documentId||sheet.drawing.model.checkinId!=model.snapshot.version.checkinId)
            return {DrawingDimensionStatus::VersionMismatch,std::numeric_limits<double>::quiet_NaN(),QStringLiteral("Versione della vista non disponibile.")};
        return evaluateDrawingAttachments(d,*definition,model.snapshot,pieces);
    }
    const DrawingViewLayout *layoutFor(const QUuid &id)const{
        for(const auto &layout:state.views)if(layout.viewId==id)return &layout;
        return nullptr;
    }
    const DrawingView *viewFor(const QUuid &id)const{
        for(const auto &definition:state.drawing.views)if(definition.id==id)return &definition;
        return nullptr;
    }
    QUuid selectedView()const{
        const auto selected=scene->selectedItems();QUuid id;
        for(auto *item:selected){auto current=item->data(viewRole).toUuid();
            if(current.isNull()&&item->data(kindRole).toString()==QLatin1String("dimension")){
                for(const auto &d:state.drawing.dimensions)if(d.id==item->data(idRole).toUuid())current=d.viewId;}
            if(!current.isNull()){if(!id.isNull()&&current!=id)return {};id=current;}}
        return id;
    }
    void selectTreeItem(const QString &kind,const QUuid &id){
        QSignalBlocker block(tree);tree->clearSelection();tree->setCurrentItem(nullptr);
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==kind&&(*it)->data(0,Qt::UserRole+1).toUuid()==id){
            tree->setCurrentItem(*it);tree->scrollToItem(*it);break;}
    }
    void selectionFromScene(){
        if(rebuilding||syncingSelection)return;
        const auto selected=scene->selectedItems();
        if(selected.size()==1&&selected.front()->data(kindRole).toString()==QLatin1String("dimension")){
            const auto id=selected.front()->data(idRole).toUuid();selectTreeItem("dimension",id);
            const auto result=results.find(id);if(result!=results.end()&&result->second.status!=DrawingDimensionStatus::Ready)say(result->second.message);
        }else selectTreeItem("view",selectedView());
    }
    void selectionFromTree(){
        if(rebuilding||syncingSelection)return;
        syncingSelection=true;scene->clearSelection();const auto *current=tree->currentItem();
        if(current){const auto kind=current->data(0,Qt::UserRole).toString();const auto id=current->data(0,Qt::UserRole+1).toUuid();
            for(auto *item:scene->items())if((kind==QLatin1String("view")&&item->data(kindRole).toString()==QLatin1String("view")&&item->data(viewRole).toUuid()==id)
                ||(kind==QLatin1String("dimension")&&item->data(kindRole).toString()==kind&&item->data(idRole).toUuid()==id)){
                item->setSelected(true);view->ensureVisible(item,40,40);}}
        syncingSelection=false;selectionFromScene();
    }
    void refreshTree(){
        const auto *current=tree->currentItem();const auto kind=current?current->data(0,Qt::UserRole).toString():QString();
        const auto selected=current?current->data(0,Qt::UserRole+1).toUuid():QUuid();QSignalBlocker block(tree);tree->clear();
        auto *root=new QTreeWidgetItem(tree,{state.title});root->setIcon(0,commandIcon("new"));root->setExpanded(true);
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);if(!layout)continue;
            auto *item=new QTreeWidgetItem(root,{layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4)});
            item->setIcon(0,commandIcon(definition.kind==DrawingViewKind::Section?"section":"viewFront"));
            item->setData(0,Qt::UserRole,"view");item->setData(0,Qt::UserRole+1,definition.id);item->setExpanded(true);
            item->setToolTip(0,QStringLiteral("Doppio clic per modificare piano, scala e sorgenti della vista."));
            for(const auto &dimension:state.drawing.dimensions)if(dimension.viewId==definition.id){const auto found=results.find(dimension.id);
                const bool ready=found!=results.end()&&found->second.status==DrawingDimensionStatus::Ready;
                const auto measure=drawingDimensionName(dimension.measure);
                auto *child=new QTreeWidgetItem(item,{measure+QStringLiteral(" · ")+(ready?drawingDimensionText(dimension,found->second.value):QStringLiteral("Da verificare"))});
                child->setIcon(0,commandIcon("dimension"));child->setData(0,Qt::UserRole,"dimension");child->setData(0,Qt::UserRole+1,dimension.id);
                if(!ready){child->setForeground(0,QColor("#d34b4b"));if(found!=results.end())child->setToolTip(0,found->second.message);}}
        }
        selectTreeItem(kind,selected);
    }
    void editSelectedView(){const auto id=selectedView();if(id.isNull()){say(QStringLiteral("Seleziona una vista o una sua quota nell'albero o sul foglio."));return;}
        const auto *definition=viewFor(id);if(definition)addViewDialog(definition->kind,id);}
    void calculate(){
        std::map<QUuid,std::vector<DrawingSection>> prepared;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);if(!layout)continue;
            std::vector<DrawingSection> sections;const auto error=drawingViewCurves(sourceFor(*layout),definition,sections);
            if(!error.isEmpty())throw std::runtime_error(error.toStdString());
            for(const auto &piece:sections)sectionPath(definition,*layout,piece);
            prepared.emplace(definition.id,std::move(sections));}
        std::map<QUuid,DrawingDimensionResult> measured;
        for(const auto &dimension:state.drawing.dimensions)measured[dimension.id]=this->measured(state,dimension,prepared.at(dimension.viewId));
        curves=std::move(prepared);results=std::move(measured);cacheSections();
    }
    void queueRedraw(){QTimer::singleShot(0,owner,[this]{redraw();});}
    const DrawingSection *resolved(const DrawingDimension &dimension)const{
        const auto *definition=viewFor(dimension.viewId);const auto it=curves.find(dimension.viewId);if(!definition||it==curves.end())return nullptr;
        const auto source=std::find_if(model.snapshot.bodies.begin(),model.snapshot.bodies.end(),[&](const auto &body){return body.featureId==dimension.section.featureId;});
        if(source==model.snapshot.bodies.end()||!source->names)return nullptr;
        const bool edge=dimension.section.kind==DrawingCurveKind::ProjectedEdge;
        const auto entities=source->names->find(edge?EntityKind::Edge:EntityKind::Face,(edge?dimension.section.edgeName:dimension.section.faceName).toStdString());std::vector<const DrawingSection *> candidates;
        for(const auto &piece:it->second)if(piece.kind==dimension.section.kind&&piece.featureId==dimension.section.featureId
            &&std::find(entities.begin(),entities.end(),edge?piece.sourceEdge.index:piece.sourceFace.index)!=entities.end())candidates.push_back(&piece);
        if(candidates.empty())return nullptr;
        if(candidates.size()==1&&!dimension.section.requiresWitness)return candidates.front();
        const auto witness=definition->plane.toGlobal(Vec3(dimension.section.witness.x(),dimension.section.witness.y(),0));const DrawingSection *chosen=nullptr;
        for(const auto *piece:candidates)if(projectPoint(*piece->curve,witness,piece->range).distance<=definition->tolerance){if(chosen)return nullptr;chosen=piece;}
        return chosen;
    }
    void redraw(){
        resetQuote();rebuilding=true;scene->clear();paintedPaths.clear();
        scene->setSceneRect(-8,-8,state.size.width()+16,state.size.height()+16);
        scene->addRect(QRectF(QPointF(0,0),state.size),QPen(QColor("#b5bdc4"),0.25),QBrush(Qt::white))->setZValue(-20);
        scene->addRect(10,10,state.size.width()-20,state.size.height()-20,QPen(QColor("#c3c9cf"),0.18))->setZValue(-19);
        auto *title=scene->addSimpleText(state.title+QStringLiteral("  |  Quote in mm"));fontFor(title,0.28);title->setPos(14,state.size.height()-18);
        int missing=0;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);const auto found=curves.find(definition.id);
            if(!layout||found==curves.end())continue;
            QRectF bounds;
            for(std::size_t i=0;i<found->second.size();++i){const auto path=sectionPath(definition,*layout,found->second[i]);paintedPaths[definition.id].push_back(path);auto *item=new SectionItem(path);
                const auto &piece=found->second[i];const bool projected=piece.kind==DrawingCurveKind::ProjectedEdge;
                item->sectionReference=!projected&&definition.kind==DrawingViewKind::ProjectedEdges;
                item->setFlags(QGraphicsItem::ItemIsSelectable);item->setData(kindRole,projected?"projection":"section");item->setData(viewRole,definition.id);item->setData(indexRole,qulonglong(i));
                item->setToolTip(projected?QStringLiteral("Bordo proiettato. Le quote misurano la sua immagine nel piano della vista."):
                    QStringLiteral("Curva di sezione fra la faccia e il piano della vista."));scene->addItem(item);
                bounds=bounds.isNull()?item->path().boundingRect():bounds.united(item->path().boundingRect());}
            auto *label=new MoveLabel(layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4));fontFor(label);label->setBrush(QColor("#35495c"));
            label->setPos(bounds.isNull()?layout->position:bounds.bottomLeft()+QPointF(0,6));label->setData(kindRole,"view");label->setData(viewRole,definition.id);
            label->setFlag(QGraphicsItem::ItemIsMovable,!readOnly);
            label->setToolTip(QStringLiteral("Trascina il titolo per spostare la vista. Doppio clic per modificarla."));
            label->activated=[this,id=definition.id]{QTimer::singleShot(0,owner,[this,id]{const auto *v=viewFor(id);if(v)addViewDialog(v->kind,id);});};
            const auto id=definition.id;label->moved=[this,id](QPointF before,QPointF after){const auto *layout=layoutFor(id);if(!layout)return;
                const auto error=owner->moveView(id,layout->position+after-before);if(!error.isEmpty()){say(error);queueRedraw();}};scene->addItem(label);
        }
        for(const auto &dimension:state.drawing.dimensions){
            const auto placement=std::find_if(state.dimensions.begin(),state.dimensions.end(),[&](const auto &p){return p.dimensionId==dimension.id;});
            if(placement==state.dimensions.end())continue;
            const auto result=results.find(dimension.id);
            const bool ready=result!=results.end()&&result->second.status==DrawingDimensionStatus::Ready;
            const QString text=ready?drawingDimensionText(dimension,result->second.value):QStringLiteral("Riferimento da verificare");
            auto *label=new MoveLabel(text);fontFor(label);label->setPos(placement->position);label->setBrush(ready?QColor("#27445e"):QColor("#c42b32"));
            label->setData(kindRole,"dimension");label->setData(idRole,dimension.id);label->setFlag(QGraphicsItem::ItemIsMovable,!readOnly);
            if(!ready){++missing;if(result!=results.end())label->setToolTip(result->second.message);}
            label->activated=[this,id=dimension.id]{QTimer::singleShot(0,owner,[this,id]{editDimensionDialog(id);});};
            const auto id=dimension.id;label->moved=[this,id](QPointF,QPointF position){const auto error=owner->moveDimension(id,position);if(!error.isEmpty()){say(error);queueRedraw();}};scene->addItem(label);
            scene->addRect(label->sceneBoundingRect().adjusted(-0.6,-0.3,0.6,0.3),QPen(Qt::NoPen),QBrush(Qt::white))->setZValue(6);
            if(!ready)continue;
            if(!dimension.references.empty()){paintAttached(dimension,result->second,label->sceneBoundingRect().center());continue;}
            const auto *piece=resolved(dimension);const auto *definition=viewFor(dimension.viewId);const auto *layout=layoutFor(dimension.viewId);
            if(!piece||!definition||!layout)continue;
            const auto point=[&](double parameter){const auto p=definition->plane.toLocal(piece->curve->point(parameter));return Vec2(p.x(),p.y());};
            auto rendered=result->second;rendered.geometry=DrawingDimensionResult::Geometry::Linear;
            rendered.first=point(piece->range.lo);rendered.second=point(piece->range.hi);
            if(dimension.measure==DrawingMeasure::SectionLength)rendered.first=point(.5*(piece->range.lo+piece->range.hi));
            paintAttached(dimension,rendered,label->sceneBoundingRect().center());
        }
        refreshTree();rebuilding=false;selectionFromTree();refreshNotable();
        const bool projected=std::any_of(state.drawing.views.begin(),state.drawing.views.end(),[](const auto &v){return v.kind==DrawingViewKind::ProjectedEdges;});
        say(missing?QStringLiteral("%1 quote richiedono verifica. Seleziona il testo per leggere il motivo.").arg(missing):projected?
            QStringLiteral("Proiezioni di tutti i bordi, inclusi quelli posteriori. Verde: sezioni del piano. Le quote misurano nel piano della vista."):
            QStringLiteral("Seleziona una curva e inserisci una quota. Trascina titoli e testi per posizionarli."));
    }
    QPointF paperLocal(const QUuid &id,Vec2 p)const{const auto *v=viewFor(id);const auto *l=layoutFor(id);return l->position+QPointF(v->scale*p.x(),-v->scale*p.y());}
    Vec2 localPaper(const QUuid &id,QPointF p)const{const auto *v=viewFor(id);const auto *l=layoutFor(id);return {(p.x()-l->position.x())/v->scale,(l->position.y()-p.y())/v->scale};}
    void clearOverlay(){for(auto *item:overlay)delete item;overlay.clear();}
    void resetQuote(){clearOverlay();quoteReferences.clear();quoteView={};snapChoices.clear();snapCycle=0;}
    void paintAttached(const DrawingDimension &dimension,const DrawingDimensionResult &result,QPointF text,bool preview=false){
        const auto id=dimension.viewId;const QColor color=preview?QColor("#1469c0"):QColor("#27445e");
        const auto stroke=[&](QPointF a,QPointF b){auto *item=scene->addLine(QLineF(a,b),QPen(color,.22));item->setZValue(preview?20:4);if(preview)overlay.push_back(item);};
        const auto tip=[&](QPointF a,QPointF toward){auto d=toward-a;const double length=std::hypot(d.x(),d.y());if(length<1e-10)return;d/=length;const QPointF n(-d.y(),d.x());stroke(a,a+2*d+.65*n);stroke(a,a+2*d-.65*n);};
        const auto a=paperLocal(id,result.first),b=paperLocal(id,result.second);
        const auto dimensionLine=[&](QPointF p,QPointF q){
            stroke(p,q);const auto delta=q-p;const double squared=QPointF::dotProduct(delta,delta);
            if(squared<1e-16){stroke(p,text);return;}
            tip(p,q);tip(q,p);const double position=QPointF::dotProduct(text-p,delta);
            if(position<0)stroke(text,p);else if(position>squared)stroke(q,text);
        };
        using G=DrawingDimensionResult::Geometry;
        if(result.geometry==G::Angle){const auto vertex=paperLocal(id,result.vertex);const double radius=std::max(5.0,std::hypot(text.x()-vertex.x(),text.y()-vertex.y()));
            const double start=std::atan2(-result.firstDirection.y(),result.firstDirection.x()),sweep=-result.sweep;QPainterPath arc;
            for(int i=0;i<=48;++i){const double t=start+sweep*i/48;const auto p=vertex+radius*QPointF(std::cos(t),std::sin(t));if(i)arc.lineTo(p);else arc.moveTo(p);}
            auto *item=scene->addPath(arc,QPen(color,.22));item->setZValue(preview?20:4);if(preview)overlay.push_back(item);
            const auto p=arc.pointAtPercent(0),q=arc.pointAtPercent(1);stroke(vertex,p);stroke(vertex,q);tip(p,arc.pointAtPercent(.06));tip(q,arc.pointAtPercent(.94));
            const QPointF radial=text-vertex;const double angle=std::atan2(radial.y(),radial.x());
            double delta=std::remainder(angle-start,2*kPi);const double t=std::clamp(sweep==0?0:delta/sweep,0.0,1.0);
            const auto anchor=arc.pointAtPercent(t);if(QLineF(anchor,text).length()>2)stroke(anchor,text);return;
        }
        if(result.geometry==G::Radius||result.geometry==G::Diameter){
            if(result.geometry==G::Radius){stroke(a,b);tip(b,a);}else{const auto opposite=2*a-b;stroke(opposite,b);tip(b,opposite);tip(opposite,b);}
            stroke(b,text);return;
        }
        if(dimension.measure==DrawingMeasure::SectionLength){stroke(a,text);tip(a,text);return;}
        const auto difference=b-a;const double length=std::hypot(difference.x(),difference.y());
        if(dimension.measure==DrawingMeasure::HorizontalDistance){const QPointF p(a.x(),text.y()),q(b.x(),text.y());stroke(a,p);stroke(b,q);dimensionLine(p,q);}
        else if(dimension.measure==DrawingMeasure::VerticalDistance){const QPointF p(text.x(),a.y()),q(text.x(),b.y());stroke(a,p);stroke(b,q);dimensionLine(p,q);}
        else if(length>1e-8){const QPointF tangent=difference/length,normal(-tangent.y(),tangent.x());const double offset=QPointF::dotProduct(text-(a+b)/2,normal);
            const auto p=a+offset*normal,q=b+offset*normal;stroke(a,p);stroke(b,q);dimensionLine(p,q);}
        else stroke(a,text);
    }
    QString snapName(DrawingPointRole role)const{switch(role){case DrawingPointRole::Start:case DrawingPointRole::End:return QStringLiteral("Estremo");case DrawingPointRole::Midpoint:return QStringLiteral("Punto medio");case DrawingPointRole::Center:return QStringLiteral("Centro");case DrawingPointRole::Nearest:return QStringLiteral("Punto piu' vicino");case DrawingPointRole::Origin:return QStringLiteral("Origine");case DrawingPointRole::AxisX:return QStringLiteral("Asse X");case DrawingPointRole::AxisY:return QStringLiteral("Asse Y");case DrawingPointRole::Curve:return QStringLiteral("Curva");default:return QStringLiteral("Quadrante");}}
    void refreshNotable(){
        notable.clear();if(!quoteAction||!quoteAction->isChecked())return;
        for(const auto &[id,pieces]:curves){const auto *definition=viewFor(id);const auto *layout=layoutFor(id);if(!definition||!layout)continue;
            for(std::size_t i=0;i<pieces.size();++i){const auto &piece=pieces[i];const auto a=piece.curve->point(piece.range.lo),b=piece.curve->point(piece.range.hi);const auto primitive=drawingPrimitive(*definition,piece);
                const auto add=[&](DrawingPointRole role,Vec3 point){notable.push_back({id,i,role,std::numeric_limits<double>::quiet_NaN(),paperPoint(*definition,*layout,point),0});};
                if(distance(a,b)>definition->tolerance){add(DrawingPointRole::Start,a);add(DrawingPointRole::End,b);}
                if(primitive.kind==DrawingPrimitive::Kind::Line)add(DrawingPointRole::Midpoint,.5*(a+b));
                if(primitive.kind==DrawingPrimitive::Kind::Conic){const auto center=definition->plane.toGlobal(Vec3(primitive.center.x(),primitive.center.y(),0));add(DrawingPointRole::Center,center);
                    if(primitive.circular)for(int k=0;k<4;++k){const auto q=primitive.center+norm(primitive.firstAxis)*Vec2(k==0?1:k==2?-1:0,k==1?1:k==3?-1:0);const auto point=definition->plane.toGlobal(Vec3(q.x(),q.y(),0));
                        if(projectPoint(*piece.curve,point,piece.range).distance<=definition->tolerance)add(DrawingPointRole(int(DrawingPointRole::Quadrant0)+k),point);}}
            }
        }
    }
    DrawingDimension quoted(QPointF cursor)const{return drawingDimensionBetween(*viewFor(quoteView),model.snapshot,curves.at(quoteView),quoteReferences,
        DrawingMeasure(quoteType->currentData().toInt()<0?int(DrawingMeasure::Distance):quoteType->currentData().toInt()),localPaper(quoteView,cursor),quoteType->currentData().toInt()<0);}
    void quoteOverlay(){
        clearOverlay();if(!quoteAction->isChecked())return;
        const auto dot=[&](QPointF p,QColor color,const QString &kind){auto *marker=scene->addEllipse(-4,-4,8,8,QPen(color,1),QBrush(Qt::white));marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);marker->setPos(p);marker->setZValue(40);marker->setData(kindRole,kind);overlay.push_back(marker);};
        for(const auto &ref:quoteReferences){const auto found=resolveDrawingAttachment(*viewFor(quoteView),model.snapshot,curves.at(quoteView),ref);if(found.status==DrawingDimensionStatus::Ready)dot(paperLocal(quoteView,found.isPoint?found.point:.5*(found.first+found.second)),QColor("#bd7800"),"dimensionPicked");}
        QString hint=QStringLiteral("Quota: clic sui riferimenti, poi posiziona nello spazio libero. Maiusc: curva; Alt: punto piu' vicino; Tab: cambia entita'; Esc: annulla.");
        if(!snapChoices.empty()){
            const auto &pick=snapChoices[snapCycle%snapChoices.size()];const auto *layout=layoutFor(pick.view);const auto &piece=curves.at(pick.view)[pick.index];
            dot(pick.paper,QColor("#1469c0"),"dimensionSnap");auto *highlight=scene->addPath(paintedPaths.at(pick.view).at(pick.index),QPen(QColor("#1469c0"),.5));highlight->setZValue(25);overlay.push_back(highlight);
            const auto source=std::find_if(model.choices.begin(),model.choices.end(),[&](const auto &c){return c.id==piece.featureId;});
            hint=snapName(pick.role)+QStringLiteral(" · ")+layout->title+(source==model.choices.end()?QString():QStringLiteral(" · ")+source->name)
                +QStringLiteral(" (%1/%2) · Tab cambia entita'; Maiusc curva; Alt punto.").arg(snapCycle%snapChoices.size()+1).arg(snapChoices.size());
        }
        if(!quoteReferences.empty())try{
            auto dimension=quoted(quoteCursor);const auto value=evaluateDrawingAttachments(dimension,*viewFor(quoteView),model.snapshot,curves.at(quoteView));
            if(value.status==DrawingDimensionStatus::Ready){auto *text=scene->addSimpleText(drawingDimensionText(dimension,value.value));fontFor(text);text->setBrush(QColor("#1469c0"));text->setZValue(30);text->setPos(quoteCursor+QPointF(3,-4));text->setData(kindRole,"dimensionPreview");overlay.push_back(text);paintAttached(dimension,value,text->sceneBoundingRect().center(),true);}
        }catch(const std::exception &){if(snapChoices.empty())hint=QStringLiteral("Seleziona un secondo riferimento compatibile; Esc cancella la scelta.");}
        say(hint);
    }
    bool quotePointer(QPointF cursor,Qt::KeyboardModifiers modifiers,bool click){
        if(readOnly||!quoteAction->isChecked())return false;
        const double zoom=std::abs(view->transform().m11()),tolerance=9/std::max(.1,zoom);
        if(QLineF(cursor,lastSnapCursor).length()*zoom>3||modifiers!=quoteModifiers)snapCycle=0;
        lastSnapCursor=cursor;quoteCursor=cursor;quoteModifiers=modifiers;snapChoices.clear();
        const auto measure=DrawingMeasure(quoteType->currentData().toInt());
        const bool wholeCurve=measure==DrawingMeasure::Radius||measure==DrawingMeasure::Diameter||measure==DrawingMeasure::Angle||measure==DrawingMeasure::SectionLength;
        if(!wholeCurve&&!(modifiers&(Qt::ShiftModifier|Qt::AltModifier)))for(auto pick:notable){if(!quoteView.isNull()&&pick.view!=quoteView)continue;pick.gap=QLineF(cursor,pick.paper).length()*zoom;if(pick.gap<=9)snapChoices.push_back(pick);}
        if(snapChoices.empty()){
            const auto candidates=scene->items(QRectF(cursor-QPointF(tolerance,tolerance),QSizeF(2*tolerance,2*tolerance)),Qt::IntersectsItemBoundingRect);
            for(auto *item:candidates){const auto kind=item->data(kindRole).toString();if(kind!=QLatin1String("projection")&&kind!=QLatin1String("section"))continue;
                const auto id=item->data(viewRole).toUuid();if(!quoteView.isNull()&&id!=quoteView)continue;const auto index=item->data(indexRole).toULongLong();const auto *definition=viewFor(id);const auto *layout=layoutFor(id);const auto &piece=curves.at(id).at(index);
                const auto p=localPaper(id,cursor);const auto found=projectPoint(*piece.curve,definition->plane.toGlobal(Vec3(p.x(),p.y(),0)),piece.range);const auto paper=paperPoint(*definition,*layout,found.point);const double gap=QLineF(cursor,paper).length()*zoom;
                if(gap<=9)snapChoices.push_back({id,std::size_t(index),modifiers&Qt::AltModifier?DrawingPointRole::Nearest:DrawingPointRole::Curve,found.parameter,paper,gap});
            }
        }
        std::stable_sort(snapChoices.begin(),snapChoices.end(),[](const auto &a,const auto &b){if(std::abs(a.gap-b.gap)>1e-6)return a.gap<b.gap;if(a.view!=b.view)return a.view<b.view;if(a.index!=b.index)return a.index<b.index;return int(a.role)<int(b.role);});
        if(click){
            if(!quoteReferences.empty()&&(quoteReferences.size()==2||snapChoices.empty())){
                try{auto dimension=quoted(cursor);QFont font("DejaVu Sans");font.setPixelSize(12);const auto measured=evaluateDrawingAttachments(dimension,*viewFor(quoteView),model.snapshot,curves.at(quoteView));
                    const double width=.25*QFontMetricsF(font).horizontalAdvance(drawingDimensionText(dimension,measured.value));const auto error=owner->addDimension(dimension,cursor-QPointF(width/2,1.5));
                    if(!error.isEmpty()){say(error);return true;}resetQuote();return true;
                }catch(const std::exception &e){say(QString::fromUtf8(e.what()));return true;}
            }
            if(snapChoices.empty()){say(QStringLiteral("Avvicina il puntatore a un punto o a una curva della vista."));return true;}
            const auto pick=snapChoices[snapCycle%snapChoices.size()];try{
                auto ref=drawingAttachment(*viewFor(pick.view),curves.at(pick.view)[pick.index],model.snapshot,pick.role,pick.parameter);
                if(!quoteReferences.empty()){const auto a=resolveDrawingAttachment(*viewFor(pick.view),model.snapshot,curves.at(pick.view),quoteReferences[0]),b=resolveDrawingAttachment(*viewFor(pick.view),model.snapshot,curves.at(pick.view),ref);
                    if(a.isPoint&&b.isPoint&&distance(a.point,b.point)<=viewFor(pick.view)->tolerance){say(QStringLiteral("Seleziona due punti distinti."));return true;}}
                quoteView=pick.view;quoteReferences.push_back(std::move(ref));snapChoices.clear();snapCycle=0;
            }catch(const std::exception &e){say(QString::fromUtf8(e.what()));return true;}
        }
        quoteOverlay();return true;
    }
    bool quoteKey(int key){
        if(!quoteAction||!quoteAction->isChecked())return false;
        if(key==Qt::Key_Tab&&!snapChoices.empty()){++snapCycle;quoteOverlay();return true;}
        if(key==Qt::Key_Escape){if(quoteReferences.empty())quoteAction->setChecked(false);else{resetQuote();quoteOverlay();}return true;}
        if(key==Qt::Key_Clear){clearOverlay();return true;}return false;
    }
    void addDatum(DrawingPointRole role){
        if(readOnly)return;
        const auto selected=quoteView.isNull()?selectedView():quoteView;if(selected.isNull()){say(QStringLiteral("Seleziona una vista o scegli prima un punto della vista."));return;}
        if(!quoteAction->isChecked())quoteAction->setChecked(true);
        if(quoteReferences.size()>=2){say(QStringLiteral("Posiziona la quota o premi Esc prima di scegliere altri riferimenti."));return;}
        DrawingAttachment ref;ref.role=role;quoteView=selected;quoteReferences.push_back(ref);snapChoices.clear();quoteOverlay();
    }
    void editSelectedDimension(){const auto selected=scene->selectedItems();if(selected.size()!=1||selected[0]->data(kindRole).toString()!=QLatin1String("dimension")){say(QStringLiteral("Seleziona il testo della quota da modificare."));return;}editDimensionDialog(selected[0]->data(idRole).toUuid());}
    void editDimensionDialog(const QUuid &id){
        if(readOnly)return;
        const auto found=std::find_if(state.drawing.dimensions.begin(),state.drawing.dimensions.end(),[&](const auto &d){return d.id==id;});if(found==state.drawing.dimensions.end())return;
        const auto original=*found;const auto *definition=viewFor(original.viewId);const auto placement=std::find_if(state.dimensions.begin(),state.dimensions.end(),[&](const auto &p){return p.dimensionId==id;});
        const auto cursor=localPaper(original.viewId,placement->position);auto refs=original.references;if(refs.empty()){DrawingAttachment ref;ref.curve=original.section;refs.push_back(ref);}
        QDialog dialog(owner);dialog.setObjectName("drawingDimensionPropertiesDialog");dialog.setWindowTitle(QStringLiteral("Proprieta' quota"));auto *form=new QFormLayout(&dialog);
        auto *type=new QComboBox(&dialog);type->setObjectName("drawingDimensionMeasure");std::vector<DrawingDimension> options;
        for(int m=0;m<=9;++m){if(m==1)continue;try{auto d=drawingDimensionBetween(*definition,model.snapshot,curves.at(original.viewId),refs,DrawingMeasure(m),cursor);d.id=original.id;
                if(d.measure==DrawingMeasure::Angle)d.angleSides=original.measure==DrawingMeasure::Angle?original.angleSides:d.angleSides;
                options.push_back(d);type->addItem(drawingDimensionName(d.measure));}catch(const std::exception &) {}}
        if(options.empty()){options.push_back(original);type->addItem(drawingDimensionName(original.measure));}
        const auto wanted=original.measure==DrawingMeasure::EndpointDistance?DrawingMeasure::Distance:original.measure;
        for(std::size_t i=0;i<options.size();++i)if(options[i].measure==wanted)type->setCurrentIndex(int(i));
        auto *decimals=new QSpinBox(&dialog);decimals->setObjectName("drawingDimensionDecimals");decimals->setRange(0,8);decimals->setValue(original.decimals);
        auto *sector=new QComboBox(&dialog);sector->setObjectName("drawingDimensionSector");sector->addItems({"Direzioni originali","Prima direzione invertita","Seconda direzione invertita","Entrambe invertite"});sector->setCurrentIndex(original.angleSides);
        auto *value=new QLabel(&dialog);value->setObjectName("drawingDimensionValue");auto *error=new QLabel(&dialog);error->setWordWrap(true);
        const auto candidate=[&]{auto d=options.at(std::size_t(type->currentIndex()));d.decimals=decimals->value();d.angleSides=d.measure==DrawingMeasure::Angle?sector->currentIndex():0;return d;};
        const auto refresh=[&]{const auto d=candidate();const auto result=measured(state,d,curves.at(d.viewId));value->setText(result.status==DrawingDimensionStatus::Ready?drawingDimensionText(d,result.value):QStringLiteral("Riferimento da verificare"));sector->setEnabled(d.measure==DrawingMeasure::Angle);error->setText(result.message);};
        form->addRow(QStringLiteral("Tipo:"),type);form->addRow(QStringLiteral("Decimali:"),decimals);form->addRow(QStringLiteral("Settore angolare:"),sector);form->addRow(QStringLiteral("Valore misurato:"),value);form->addRow(error);
        auto *note=new QLabel(QStringLiteral("Quota di riferimento associata al modello salvato. Le dimensioni del modello si modificano nello schizzo 3D."),&dialog);note->setWordWrap(true);form->addRow(note);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{const auto result=owner->updateDimension(candidate());error->setText(result);if(result.isEmpty())dialog.accept();});
        connect(type,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});connect(decimals,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){refresh();});connect(sector,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});refresh();dialog.exec();
    }
    void addViewDialog(DrawingViewKind kind=DrawingViewKind::Section,const QUuid &existing={}){
        if(readOnly)return;
        const auto *original=viewFor(existing);const auto *placement=layoutFor(existing);
        const bool editing=original&&placement;
        const bool projected=kind==DrawingViewKind::ProjectedEdges;
        QDialog dialog(owner);dialog.setWindowTitle(projected?QStringLiteral("Inserisci proiezione dei bordi"):QStringLiteral("Inserisci sezione"));dialog.setObjectName(QStringLiteral("drawingViewDialog"));auto *form=new QFormLayout(&dialog);
        auto *title=new QLineEdit((projected?QStringLiteral("Vista %1"):QStringLiteral("Sezione %1")).arg(state.views.size()+1),&dialog);title->setObjectName("drawingViewTitle");title->setMaxLength(256);
        auto *plane=new QComboBox(&dialog);plane->setObjectName("drawingViewPlane");
        if(projected){plane->addItems({"Superiore (XY)","Frontale (XZ)","Destra (YZ)","Inferiore","Posteriore","Sinistra","Isometrica"});plane->setCurrentIndex(1);}
        else plane->addItems({"XY","XZ","YZ"});
        auto *references=new QCheckBox(QStringLiteral("Mostra sezioni del piano come riferimenti di quota"),&dialog);references->setObjectName("drawingSectionReferences");references->setVisible(projected);
        auto *depth=new QDoubleSpinBox(&dialog);depth->setRange(-1e8,1e8);depth->setDecimals(6);depth->setSuffix(" mm");depth->setObjectName("drawingViewDepth");
        auto *scale=new QDoubleSpinBox(&dialog);scale->setDecimals(4);scale->setRange(0.001,1000);scale->setValue(1);scale->setObjectName("drawingViewScale");
        auto *bodies=new QListWidget(&dialog);bodies->setObjectName("drawingViewBodies");bodies->setMinimumHeight(100);
        for(const auto &choice:model.choices){auto *item=new QListWidgetItem(choice.name,bodies);item->setData(Qt::UserRole,QVariant::fromValue(choice.id));item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);}
        Box bounds;for(const auto &body:model.snapshot.bodies)for(auto face:body.body->faces())bounds.add(faceBox(*body.body,face));
        const auto normalFor=[](int index){switch(index){case 0:return Vec3(0,0,1);case 1:return Vec3(0,-1,0);case 2:return Vec3(1,0,0);
            case 3:return Vec3(0,0,-1);case 4:return Vec3(0,1,0);case 5:return Vec3(-1,0,0);default:return normalized(Vec3(1,-1,1));}};
        const auto xFor=[](int index){return index==2?Vec3(0,1,0):index==4?Vec3(-1,0,0):index==5?Vec3(0,-1,0):index==6?normalized(Vec3(1,1,0)):Vec3(1,0,0);};
        const Vec3 center=bounds.isEmpty()?Vec3():0.5*(bounds.lo+bounds.hi);
        depth->setValue(dot(center,normalFor(plane->currentIndex())));depth->setEnabled(!projected);
        connect(references,&QCheckBox::toggled,depth,&QDoubleSpinBox::setEnabled);
        form->addRow(QStringLiteral("Titolo:"),title);form->addRow(QStringLiteral("Piano:"),plane);form->addRow(QStringLiteral("Posizione lungo la normale:"),depth);
        if(projected){form->addRow(references);auto *note=new QLabel(QStringLiteral("Tutti i bordi proiettati, compresi quelli posteriori. Le silhouette delle superfici curve non sono ancora incluse."),&dialog);note->setWordWrap(true);form->addRow(note);}
        form->addRow(QStringLiteral("Scala foglio/modello:"),scale);form->addRow(QStringLiteral("Corpi e superfici:"),bodies);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        // Mantiene il frame originale anche per piani arbitrari importati: nessun arrotondamento silenzioso.
        int originalPlane=-1;
        if(editing){
            dialog.setWindowTitle(QStringLiteral("Modifica vista"));title->setText(placement->title);scale->setValue(original->scale);
            for(int i=0;i<plane->count();++i)if(distance(normalFor(i),original->plane.zDir())<1e-10&&distance(xFor(i),original->plane.xDir())<1e-10){originalPlane=i;break;}
            if(originalPlane<0){plane->addItem(QStringLiteral("Piano corrente"));originalPlane=plane->count()-1;}
            plane->setCurrentIndex(originalPlane);depth->setValue(dot(original->plane.origin(),original->plane.zDir()));
            references->setChecked(original->sectionReferences);depth->setEnabled(!projected||references->isChecked());
            for(int row=0;row<bodies->count();++row)bodies->item(row)->setCheckState(placement->features.contains(bodies->item(row)->data(Qt::UserRole).toULongLong())?Qt::Checked:Qt::Unchecked);
            // Conserva anche sorgenti mancanti, per non cancellare i riferimenti durante una rinomina.
            for(auto feature:placement->features)if(std::none_of(model.choices.begin(),model.choices.end(),[&](const auto &choice){return choice.id==feature;})){
                auto *item=new QListWidgetItem(QStringLiteral("Sorgente %1 (non disponibile)").arg(feature),bodies);item->setData(Qt::UserRole,QVariant::fromValue(feature));item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);}
        }
        const auto initialDepth=depth->value(),initialScale=scale->value();
        connect(plane,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int index){
            depth->setValue(editing&&index==originalPlane?initialDepth:dot(center,normalFor(index)));});
        auto *errorLabel=new QLabel(&dialog);errorLabel->setWordWrap(true);errorLabel->setObjectName("drawingViewError");form->insertRow(form->rowCount()-1,errorLabel);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
            DrawingView definition=editing?*original:DrawingView{};
            const auto normal=editing&&plane->currentIndex()==originalPlane?original->plane.zDir():normalFor(plane->currentIndex());
            if(editing&&plane->currentIndex()==originalPlane){const double oldDepth=dot(original->plane.origin(),normal);
                if(depth->value()!=initialDepth)definition.plane=Frame3::fromAxes(original->plane.origin()+(depth->value()-oldDepth)*normal,original->plane.xDir(),original->plane.yDir(),normal);
            }else definition.plane=Frame3(depth->value()*normal,normal,xFor(plane->currentIndex()));
            definition.scale=editing&&scale->value()==initialScale?original->scale:scale->value();definition.kind=kind;definition.sectionReferences=projected&&references->isChecked();
            DrawingViewLayout layout=editing?*placement:DrawingViewLayout{};layout.viewId=definition.id;layout.title=title->text().trimmed();layout.features.clear();
            for(int row=0;row<bodies->count();++row)if(bodies->item(row)->checkState()==Qt::Checked)layout.features.append(bodies->item(row)->data(Qt::UserRole).toULongLong());
            if(!editing){const auto local=definition.plane.toLocal(center);layout.position=QPointF(state.size.width()/2,state.size.height()/2)-QPointF(local.x()*definition.scale,-local.y()*definition.scale);}
            const auto error=editing?owner->updateView(definition,layout):owner->insertView(definition,layout);
            errorLabel->setText(error);if(error.isEmpty())dialog.accept();
        });
        dialog.exec();
    }
    void removeSelection(){
        if(readOnly)return;
        const auto selected=scene->selectedItems();if(selected.empty())return;
        QSet<QUuid> dimensions;for(auto *item:selected)if(item->data(kindRole).toString()==QLatin1String("dimension"))dimensions.insert(item->data(idRole).toUuid());
        if(dimensions.size()==selected.size()){
            auto next=state;auto &definitions=next.drawing.dimensions;definitions.erase(std::remove_if(definitions.begin(),definitions.end(),[&](const auto &d){return dimensions.contains(d.id);}),definitions.end());
            next.dimensions.erase(std::remove_if(next.dimensions.begin(),next.dimensions.end(),[&](const auto &p){return dimensions.contains(p.dimensionId);}),next.dimensions.end());
            for(auto id:dimensions)results.erase(id);
            state=std::move(next);record(QStringLiteral("Elimina quote"));redraw();return;
        }
        if(selected.size()==1&&selected[0]->data(kindRole).toString()==QLatin1String("view")){
            const auto id=selected[0]->data(viewRole).toUuid();const auto count=std::count_if(state.drawing.dimensions.begin(),state.drawing.dimensions.end(),[&](const auto &d){return d.viewId==id;});
            if(count){QMessageBox confirm(QMessageBox::Question,QStringLiteral("Elimina vista"),QStringLiteral("Eliminare la vista e le sue %1 quote? L'operazione puo' essere annullata.").arg(count),QMessageBox::Yes|QMessageBox::No,owner);
                confirm.setObjectName("drawingDeleteViewDialog");confirm.setDefaultButton(QMessageBox::No);if(confirm.exec()!=QMessageBox::Yes)return;}
            report(owner->removeView(id));return;
        }
        say(QStringLiteral("Seleziona il titolo della vista oppure i testi delle quote da eliminare."));
    }
};
DrawingSheetEditor::DrawingSheetEditor(DrawingSheet sheet,DrawingEditorModel model,QWidget *parent):QWidget(parent),impl_(std::make_unique<Impl>(this,std::move(sheet),std::move(model))){
    setObjectName("drawingSheetEditor");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto *workspace=new QMainWindow(this);workspace->setWindowFlags(Qt::Widget);workspace->setObjectName("drawingWorkspace");layout->addWidget(workspace);
    auto *toolbar=workspace->addToolBar(QStringLiteral("Tavola"));toolbar->setObjectName("drawingCommandBar");configureCommandBar(toolbar);
    const auto action=[&](const QString &name,const char *object,std::function<void()> callback){auto *a=toolbar->addAction(name);a->setObjectName(object);a->setToolTip(name);connect(a,&QAction::triggered,this,std::move(callback));return a;};
    impl_->undoAction=action(QStringLiteral("Annulla"),"drawingUndoAction",[this]{impl_->report(undo());});
    impl_->redoAction=action(QStringLiteral("Ripristina"),"drawingRedoAction",[this]{impl_->report(redo());});toolbar->addSeparator();
    action(QStringLiteral("Inserisci vista..."),"drawingAddProjectionAction",[this]{impl_->addViewDialog(DrawingViewKind::ProjectedEdges);});
    action(QStringLiteral("Inserisci sezione..."),"drawingAddViewAction",[this]{impl_->addViewDialog();});
    action(QStringLiteral("Modifica vista..."),"drawingEditViewAction",[this]{impl_->editSelectedView();});
    action(QStringLiteral("Allinea vista..."),"drawingAlignViewAction",[this]{impl_->alignDialog();});toolbar->addSeparator();
    impl_->quoteAction=action(QStringLiteral("Quota intelligente"),"drawingSmartDimensionAction",[]{});impl_->quoteAction->setCheckable(true);
    impl_->quoteType=new QComboBox(toolbar);impl_->quoteType->setObjectName("drawingDimensionType");impl_->quoteType->setToolTip(QStringLiteral("Tipo della quota interattiva"));
    impl_->quoteType->addItem(QStringLiteral("Automatica"),-1);
    for(auto measure:{DrawingMeasure::Distance,DrawingMeasure::HorizontalDistance,DrawingMeasure::VerticalDistance,DrawingMeasure::Radius,DrawingMeasure::Diameter,DrawingMeasure::Angle,DrawingMeasure::AxisRadius,DrawingMeasure::AxisDiameter,DrawingMeasure::SectionLength})impl_->quoteType->addItem(drawingDimensionName(measure),int(measure));
    toolbar->addWidget(impl_->quoteType);
    action(QStringLiteral("Proprieta' quota..."),"drawingEditDimensionAction",[this]{impl_->editSelectedDimension();});
    action(QStringLiteral("Quota estremi"),"drawingEndpointDimensionAction",[this]{impl_->report(addSelectedDimension(DrawingMeasure::EndpointDistance));});
    action(QStringLiteral("Quota orizzontale"),"drawingHorizontalDimensionAction",[this]{impl_->report(addSelectedDimension(DrawingMeasure::HorizontalDistance));});
    action(QStringLiteral("Quota verticale"),"drawingVerticalDimensionAction",[this]{impl_->report(addSelectedDimension(DrawingMeasure::VerticalDistance));});
    action(QStringLiteral("Lunghezza curva"),"drawingLengthDimensionAction",[this]{impl_->report(addSelectedDimension(DrawingMeasure::SectionLength));});
    toolbar->addSeparator();action(QStringLiteral("Elimina selezione"),"drawingDeleteSelectionAction",[this]{impl_->removeSelection();});
    impl_->updateAction=action(QStringLiteral("Aggiorna dal modello"),"drawingUpdateModelAction",[this]{if(impl_->provider){try{impl_->report(updateModel(impl_->provider()));}catch(const std::exception &error){impl_->report(QString::fromUtf8(error.what()));}}});
    impl_->updateAction->setEnabled(false);action(QStringLiteral("Adatta foglio"),"drawingFitAction",[this]{fitSheet();});
    impl_->scene=new QGraphicsScene(this);impl_->view=new SheetCanvas(impl_->scene,this);impl_->view->setObjectName("drawingCanvas");impl_->view->setRenderHint(QPainter::Antialiasing);impl_->view->setBackgroundRole(QPalette::Dark);impl_->view->setMouseTracking(true);impl_->view->viewport()->setMouseTracking(true);
    impl_->view->pointer=[this](QPointF point,Qt::KeyboardModifiers modifiers,bool click){try{return impl_->quotePointer(point,modifiers,click);}catch(const std::exception &e){impl_->say(QString::fromUtf8(e.what()));return true;}};
    impl_->view->key=[this](int key){return impl_->quoteKey(key);};
    connect(impl_->quoteAction,&QAction::toggled,this,[this](bool checked){impl_->resetQuote();impl_->view->setCursor(checked?Qt::CrossCursor:Qt::ArrowCursor);impl_->refreshNotable();if(checked)impl_->quoteOverlay();});
    connect(impl_->quoteType,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){if(!impl_->quoteAction->isChecked())impl_->quoteAction->setChecked(true);impl_->quoteOverlay();});
    for(const auto &entry:{qMakePair("drawingOriginReferenceAction",DrawingPointRole::Origin),qMakePair("drawingAxisXReferenceAction",DrawingPointRole::AxisX),qMakePair("drawingAxisYReferenceAction",DrawingPointRole::AxisY)}){
        auto *a=new QAction(commandIcon("viewAxes"),impl_->snapName(entry.second),this);a->setObjectName(entry.first);connect(a,&QAction::triggered,this,[this,role=entry.second]{impl_->addDatum(role);});}
    impl_->view->setDragMode(QGraphicsView::RubberBandDrag);impl_->view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);workspace->setCentralWidget(impl_->view);
    auto *dock=new QDockWidget(QStringLiteral("Albero tavola"),workspace);dock->setObjectName("drawingTreeDock");
    impl_->tree=new QTreeWidget(dock);impl_->tree->setObjectName("drawingTree");impl_->tree->setHeaderLabel(QStringLiteral("Oggetti tavola"));impl_->tree->setMinimumWidth(220);
    impl_->tree->setSelectionMode(QAbstractItemView::SingleSelection);impl_->tree->setContextMenuPolicy(Qt::CustomContextMenu);dock->setWidget(impl_->tree);workspace->addDockWidget(Qt::LeftDockWidgetArea,dock);
    connect(impl_->tree,&QTreeWidget::itemSelectionChanged,this,[this]{impl_->selectionFromTree();});
    connect(impl_->scene,&QGraphicsScene::selectionChanged,this,[this]{impl_->selectionFromScene();});
    connect(impl_->tree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int){if(item->data(0,Qt::UserRole).toString()==QLatin1String("dimension"))impl_->editSelectedDimension();else impl_->editSelectedView();});
    connect(impl_->tree,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){
        auto *item=impl_->tree->itemAt(point);if(!item)return;impl_->tree->setCurrentItem(item);CadMenu menu(this);
        if(item->data(0,Qt::UserRole).toString()==QLatin1String("dimension"))menu.addAction(findChild<QAction *>("drawingEditDimensionAction"));
        else{menu.addAction(findChild<QAction *>("drawingEditViewAction"));menu.addAction(findChild<QAction *>("drawingAlignViewAction"));}
        menu.addSeparator();menu.addAction(findChild<QAction *>("drawingDeleteSelectionAction"));
        menu.exec(impl_->tree->viewport()->mapToGlobal(point));});
    impl_->message=new QLabel(this);impl_->message->setWordWrap(false);impl_->message->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);impl_->message->setMinimumWidth(0);workspace->statusBar()->addWidget(impl_->message,1);workspace->statusBar()->setSizeGripEnabled(false);
    const QPair<const char *,const char *> icons[]={{"drawingSmartDimensionAction","dimension"},{"drawingEditDimensionAction","edit"},{"drawingUndoAction","undo"},{"drawingRedoAction","redo"},{"drawingAlignViewAction","move"},{"drawingAddProjectionAction","viewFront"},{"drawingAddViewAction","section"},{"drawingEditViewAction","edit"},
        {"drawingEndpointDimensionAction","dimensionAligned"},{"drawingHorizontalDimensionAction","dimension"},{"drawingVerticalDimensionAction","dimensionVertical"},{"drawingLengthDimensionAction","measure"},
        {"drawingDeleteSelectionAction","delete"},{"drawingUpdateModelAction","rebuild"},{"drawingFitAction","zoomAll"}};
    for(const auto &entry:icons){auto *a=findChild<QAction *>(entry.first);a->setIcon(commandIcon(entry.second));a->setIconVisibleInMenu(true);}
    const QPair<QAction *,QKeySequence> shortcuts[]={{impl_->undoAction,QKeySequence::Undo},{impl_->redoAction,QKeySequence::Redo},{findChild<QAction *>("drawingDeleteSelectionAction"),QKeySequence::Delete}};
    for(const auto &entry:shortcuts){entry.first->setShortcut(entry.second);entry.first->setShortcutContext(Qt::WidgetWithChildrenShortcut);impl_->view->addAction(entry.first);impl_->tree->addAction(entry.first);}
    impl_->calculate();impl_->resetHistory();impl_->savedSheet=impl_->history[0].sheet;impl_->redraw();setReadOnly(!impl_->state.drawing.version.checkinId.isNull());QTimer::singleShot(0,this,[this]{fitSheet();});
}
DrawingSheetEditor::~DrawingSheetEditor(){
    // La scena emette selectionChanged anche mentre distrugge le etichette.
    // Impl deve essere ancora vivo quando si scollegano questi callback.
    disconnect(impl_->scene,nullptr,this,nullptr);disconnect(impl_->tree,nullptr,this,nullptr);
    impl_->view->pointer={};impl_->view->key={};
}
const DrawingSheet &DrawingSheetEditor::sheet()const{return impl_->state;}
QGraphicsView *DrawingSheetEditor::canvas()const{return impl_->view;}
const std::vector<DrawingCurve> &DrawingSheetEditor::curves(const QUuid &viewId)const{static const std::vector<DrawingCurve> empty;const auto found=impl_->curves.find(viewId);return found==impl_->curves.end()?empty:found->second;}
const std::vector<DrawingSection> &DrawingSheetEditor::sections(const QUuid &viewId)const{static const std::vector<DrawingSection> empty;const auto found=impl_->sectionCurves.find(viewId);return found==impl_->sectionCurves.end()?empty:found->second;}
DrawingDimensionResult DrawingSheetEditor::dimensionResult(const QUuid &id)const{const auto found=impl_->results.find(id);return found==impl_->results.end()?DrawingDimensionResult{}:found->second;}
QString DrawingSheetEditor::insertView(DrawingView definition,DrawingViewLayout layout){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{
        auto next=impl_->state;next.drawing.views.push_back(definition);next.views.append(layout);QByteArray validation;const auto valid=encodeDrawingSheet(next,validation);if(!valid.isEmpty())return valid;
        std::vector<DrawingSection> pieces;const auto error=drawingViewCurves(impl_->sourceFor(layout),definition,pieces);if(!error.isEmpty())return error;
        if(pieces.empty())return definition.kind==DrawingViewKind::Section?QStringLiteral("Il piano non interseca i corpi selezionati. Modificare posizione o piano."):QStringLiteral("Nessun bordo proiettabile nei corpi selezionati.");
        for(const auto &piece:pieces)sectionPath(definition,layout,piece);
        impl_->state=std::move(next);impl_->curves[definition.id]=std::move(pieces);impl_->cacheSections();impl_->record(QStringLiteral("Inserisci vista"),std::move(validation));impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::updateView(DrawingView definition,DrawingViewLayout layout){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *old=impl_->viewFor(definition.id);if(!old||layout.viewId!=definition.id)return QStringLiteral("Vista da modificare non trovata.");
    if(definition.kind!=old->kind)return QStringLiteral("Conservare il tipo della vista per mantenerne i riferimenti.");
    for(const auto &dimension:impl_->state.drawing.dimensions)if(dimension.viewId==definition.id){
        std::vector<DrawingSectionReference> sources{dimension.section};for(const auto &r:dimension.references)if(!drawingDatum(r.role))sources.push_back(r.curve);
        for(const auto &source:sources){
            if(!layout.features.contains(source.featureId))return QStringLiteral("La sorgente contiene quote associate: eliminare prima le quote per rimuoverla dalla vista.");
            if(source.kind==DrawingCurveKind::Section&&definition.kind==DrawingViewKind::ProjectedEdges&&!definition.sectionReferences)
                return QStringLiteral("Sono presenti quote di sezione: conservare le sezioni del piano o eliminare prima quelle quote.");}
    }
    try{
        auto next=impl_->state;for(auto &v:next.drawing.views)if(v.id==definition.id)v=definition;
        for(auto &v:next.views)if(v.viewId==definition.id)v=layout;
        QByteArray validation;const auto valid=encodeDrawingSheet(next,validation);if(!valid.isEmpty())return valid;
        const auto *oldLayout=impl_->layoutFor(definition.id);
        const bool geometryChanged=!sameGeometry(*old,definition)||oldLayout->features!=layout.features;
        std::vector<DrawingCurve> pieces;
        if(geometryChanged){const auto error=drawingViewCurves(impl_->sourceFor(layout),definition,pieces);if(!error.isEmpty())return error;}
        else pieces=impl_->curves.at(definition.id);
        // Una sezione spostata puo' non intersecare piu' il corpo: le sue quote restano, marcate mancanti.
        for(const auto &piece:pieces)sectionPath(definition,layout,piece);
        auto results=impl_->results;if(geometryChanged)for(const auto &d:next.drawing.dimensions)if(d.viewId==definition.id)results[d.id]=impl_->measured(next,d,pieces);
        impl_->state=std::move(next);impl_->curves[definition.id]=std::move(pieces);impl_->results=std::move(results);impl_->cacheSections();impl_->record(QStringLiteral("Modifica vista"),std::move(validation));impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::moveView(const QUuid &id,const QPointF &position){return impl_->moveViewTo(id,position,QStringLiteral("Sposta vista"));}
QString DrawingSheetEditor::moveDimension(const QUuid &id,const QPointF &position){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    auto next=impl_->state;const auto found=std::find_if(next.dimensions.begin(),next.dimensions.end(),[&](const auto &p){return p.dimensionId==id;});
    if(found==next.dimensions.end())return QStringLiteral("Quota non trovata.");
    found->position=position;auto dimension=std::find_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return d.id==id;});
    auto value=impl_->results.at(id);
    if(dimension->measure==DrawingMeasure::Angle&&!dimension->references.empty()){
        auto probe=*dimension;probe.angleSides=0;const auto base=impl_->measured(next,probe,impl_->curves.at(probe.viewId));
        if(base.status==DrawingDimensionStatus::Ready){const auto *v=impl_->viewFor(probe.viewId);const auto *layout=impl_->layoutFor(probe.viewId);
            dimension->angleSides=drawingAngleSector(base,Vec2((position.x()-layout->position.x())/v->scale,-(position.y()-layout->position.y())/v->scale));}
        value=impl_->measured(next,*dimension,impl_->curves.at(dimension->viewId));
    }
    const auto error=impl_->applyLayout(std::move(next),QStringLiteral("Sposta quota"));if(error.isEmpty())impl_->results[id]=value;return error;
}
QString DrawingSheetEditor::alignView(const QUuid &id,const QUuid &reference,ViewAlignment alignment){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(id==reference||!impl_->viewFor(id)||!impl_->viewFor(reference))return QStringLiteral("Scegliere due viste distinte della tavola.");
    if(alignment!=ViewAlignment::SameRow&&alignment!=ViewAlignment::SameColumn)return QStringLiteral("Direzione di allineamento non valida.");
    try{QPointF center,target;if(!impl_->centerFor(id,center)||!impl_->centerFor(reference,target))return QStringLiteral("Le due viste devono avere curve visibili per allinearne i centri.");
        auto position=impl_->layoutFor(id)->position;if(alignment==ViewAlignment::SameRow)position.ry()+=target.y()-center.y();else position.rx()+=target.x()-center.x();
        return impl_->moveViewTo(id,position,QStringLiteral("Allinea viste"));
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::removeView(const QUuid &id){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(!impl_->viewFor(id))return QStringLiteral("Vista non trovata.");
    auto next=impl_->state;QSet<QUuid> dimensions;for(const auto &d:next.drawing.dimensions)if(d.viewId==id)dimensions.insert(d.id);
    next.drawing.views.erase(std::remove_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==id;}),next.drawing.views.end());
    next.views.erase(std::remove_if(next.views.begin(),next.views.end(),[&](const auto &p){return p.viewId==id;}),next.views.end());
    next.drawing.dimensions.erase(std::remove_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return dimensions.contains(d.id);}),next.drawing.dimensions.end());
    next.dimensions.erase(std::remove_if(next.dimensions.begin(),next.dimensions.end(),[&](const auto &p){return dimensions.contains(p.dimensionId);}),next.dimensions.end());
    QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
    impl_->state=std::move(next);impl_->curves.erase(id);impl_->cacheSections();for(auto dimension:dimensions)impl_->results.erase(dimension);
    impl_->record(QStringLiteral("Elimina vista e quote"),std::move(bytes));impl_->redraw();return {};
}
bool DrawingSheetEditor::canUndo()const{return !impl_->readOnly&&impl_->historyIndex>0;}
bool DrawingSheetEditor::canRedo()const{return !impl_->readOnly&&impl_->historyIndex+1<impl_->history.size();}
QString DrawingSheetEditor::undo(){return canUndo()?impl_->restore(impl_->historyIndex-1):QStringLiteral("Nessuna modifica annullabile.");}
QString DrawingSheetEditor::redo(){return canRedo()?impl_->restore(impl_->historyIndex+1):QStringLiteral("Nessuna modifica da ripristinare.");}
void DrawingSheetEditor::markSaved(){impl_->savedSheet=impl_->encodedState();}
bool DrawingSheetEditor::isModified()const{return impl_->history[impl_->historyIndex].sheet!=impl_->savedSheet;}
QString DrawingSheetEditor::addDimension(DrawingDimension dimension,const QPointF &position){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{auto next=impl_->state;next.drawing.dimensions.push_back(dimension);next.dimensions.append({dimension.id,position});
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        const auto value=impl_->measured(next,dimension,impl_->curves.at(dimension.viewId));if(value.status!=DrawingDimensionStatus::Ready)return value.message;
        impl_->state=std::move(next);impl_->results[dimension.id]=value;impl_->record(QStringLiteral("Inserisci quota"),std::move(bytes));impl_->redraw();return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString DrawingSheetEditor::updateDimension(DrawingDimension dimension){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{auto next=impl_->state;auto found=std::find_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return d.id==dimension.id;});
        if(found==next.drawing.dimensions.end()||found->viewId!=dimension.viewId)return QStringLiteral("Quota non trovata nella vista.");
        *found=dimension;
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        const auto value=impl_->measured(next,dimension,impl_->curves.at(dimension.viewId));if(value.status==DrawingDimensionStatus::Invalid)return value.message;
        impl_->state=std::move(next);impl_->results[dimension.id]=value;impl_->record(QStringLiteral("Modifica quota"),std::move(bytes));impl_->queueRedraw();return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString DrawingSheetEditor::addSelectedDimension(DrawingMeasure measure){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{
        QGraphicsItem *selected=nullptr;for(auto *item:impl_->scene->selectedItems())if(item->data(kindRole).toString()==QLatin1String("section")||item->data(kindRole).toString()==QLatin1String("projection")){if(selected)return QStringLiteral("Seleziona una sola curva da quotare.");selected=item;}
        if(!selected)return QStringLiteral("Seleziona una curva della vista sul foglio.");
        const auto id=selected->data(viewRole).toUuid();const auto index=selected->data(indexRole).toULongLong();const auto *definition=impl_->viewFor(id);const auto *layout=impl_->layoutFor(id);const auto found=impl_->curves.find(id);
        if(!definition||!layout||found==impl_->curves.end()||index>=found->second.size())return QStringLiteral("Selezione della vista non piu' valida.");
        const auto &piece=found->second[index];auto dimension=dimensionOfCurve(*definition,piece,measure);auto next=impl_->state;next.drawing.dimensions.push_back(dimension);
        auto anchor=paperPoint(*definition,*layout,piece.curve->point(0.5*(piece.range.lo+piece.range.hi)))+(measure==DrawingMeasure::VerticalDistance?QPointF(10,0):QPointF(0,-10));
        next.dimensions.append({dimension.id,anchor});const auto value=evaluateDrawingDimension(next.drawing,impl_->model.snapshot,dimension.id);
        if(value.status!=DrawingDimensionStatus::Ready)return value.message;
        if((measure==DrawingMeasure::HorizontalDistance||measure==DrawingMeasure::VerticalDistance)&&value.value<=definition->tolerance)
            return QStringLiteral("La distanza degli estremi lungo questa direzione e' nulla. Scegliere un'altra curva o un altro tipo di quota.");
        QFont font("DejaVu Sans");font.setPixelSize(12);
        const auto label=(measure==DrawingMeasure::SectionLength?QStringLiteral("L = %1 mm"):QStringLiteral("%1 mm")).arg(value.value,0,'f',2);
        next.dimensions.back().position.rx()-=0.125*QFontMetricsF(font).horizontalAdvance(label);
        QByteArray validation;const auto valid=encodeDrawingSheet(next,validation);if(!valid.isEmpty())return valid;
        impl_->state=std::move(next);impl_->results[dimension.id]=value;impl_->record(QStringLiteral("Inserisci quota"),std::move(validation));impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::updateModel(DrawingEditorModel model){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(model.snapshot.version.documentId!=impl_->state.drawing.model.documentId)return QStringLiteral("La tavola appartiene a un altro modello.");
    auto oldState=impl_->state;auto oldModel=impl_->model;
    try{
        const auto error=retargetDrawingModel(impl_->state.drawing,model.snapshot.version);if(!error.isEmpty())return error;
        for(auto &layout:impl_->state.views){QVector<quint64> features;for(auto id:layout.features){const auto current=model.replacements.value(id,id);if(!features.contains(current))features.append(current);}layout.features=features;}
        for(auto &dimension:impl_->state.drawing.dimensions){dimension.section.featureId=model.replacements.value(dimension.section.featureId,dimension.section.featureId);
            for(auto &ref:dimension.references)if(!drawingDatum(ref.role))ref.curve.featureId=model.replacements.value(ref.curve.featureId,ref.curve.featureId);}
        impl_->model=std::move(model);impl_->calculate();impl_->resetHistory();impl_->notify();impl_->redraw();return {};
    }catch(const std::exception &error){impl_->state=std::move(oldState);impl_->model=std::move(oldModel);return QString::fromUtf8(error.what());}
}
void DrawingSheetEditor::setChangedCallback(std::function<void(const DrawingSheet &)> callback){impl_->changed=std::move(callback);}
void DrawingSheetEditor::setModelProvider(std::function<DrawingEditorModel()> provider){impl_->provider=std::move(provider);impl_->updateAction->setEnabled(bool(impl_->provider)&&!impl_->readOnly);}
bool DrawingSheetEditor::isReadOnly()const{return impl_->readOnly;}
void DrawingSheetEditor::setReadOnly(bool readOnly){
    impl_->readOnly=readOnly||!impl_->state.drawing.version.checkinId.isNull();
    if(impl_->readOnly)impl_->quoteAction->setChecked(false);
    impl_->quoteType->setEnabled(!impl_->readOnly);
    for(const auto &name:{"drawingSmartDimensionAction","drawingEditDimensionAction","drawingOriginReferenceAction","drawingAxisXReferenceAction","drawingAxisYReferenceAction","drawingAddProjectionAction","drawingAddViewAction","drawingEditViewAction","drawingAlignViewAction","drawingEndpointDimensionAction","drawingHorizontalDimensionAction","drawingVerticalDimensionAction","drawingLengthDimensionAction","drawingDeleteSelectionAction"})
        findChild<QAction *>(QString::fromLatin1(name))->setEnabled(!impl_->readOnly);
    impl_->refreshHistoryActions();impl_->updateAction->setEnabled(!impl_->readOnly&&bool(impl_->provider));
    for(auto *item:impl_->scene->items())if(dynamic_cast<MoveLabel *>(item))item->setFlag(QGraphicsItem::ItemIsMovable,!impl_->readOnly);
}
void DrawingSheetEditor::fitSheet(){impl_->view->fitInView(QRectF(QPointF(-5,-5),impl_->state.size+QSizeF(10,10)),Qt::KeepAspectRatio);}
}
