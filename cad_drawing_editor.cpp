#include "cad_drawing_editor.h"
#include "cad_drawing_silhouette.h"
#include "cad_drawing_visibility.h"
#include "cad_drawing_derived.h"
#include "cad_drawing_compute.h"
#include <QGraphicsPixmapItem>
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
#include <QGraphicsItemGroup>
#include <QCursor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSpinBox>
#include <QPdfWriter>
#include <QPageSize>
#include <QSaveFile>
#include <QFileInfo>
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
        if(pointer)pointer(mapToScene(event->position().toPoint()),event->modifiers(),false);
        event->accept();
    }
};
struct SectionItem final:QGraphicsPathItem{
    using QGraphicsPathItem::QGraphicsPathItem;
    bool sectionReference=false,hidden=false;
    QRectF boundingRect()const override{return path().boundingRect().adjusted(-0.7,-0.7,0.7,0.7);}
    QPainterPath shape()const override{QPainterPathStroker stroke;stroke.setWidth(1.2);return stroke.createStroke(path());}
    void paint(QPainter *painter,const QStyleOptionGraphicsItem *,QWidget *)override{
        painter->setPen(QPen(isSelected()?QColor("#1469c0"):(sectionReference?QColor("#187c59"):hidden?QColor("#80858a"):QColor("#222222")),isSelected()?0.65:sectionReference?0.25:hidden?0.18:0.3,
            sectionReference?Qt::DotLine:hidden?Qt::DashLine:Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);painter->drawPath(path());
    }
};
struct DrawingPageItem final:QGraphicsItem{
    DrawingSheet sheet;DrawingPageContext context;
    DrawingPageItem(DrawingSheet page,DrawingPageContext value):sheet(std::move(page)),context(std::move(value)){setZValue(-19);}
    QRectF boundingRect()const override{return QRectF(QPointF(0,0),sheet.size);}
    void paint(QPainter *painter,const QStyleOptionGraphicsItem *,QWidget *)override{paintDrawingPage(*painter,sheet,context);}
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
// addPath conserva i tratti aperti: operator+ esegue invece unione di aree.
QPainterPath joinedPaths(const QPainterPath &a,const QPainterPath &b){auto result=a;result.addPath(b);return result;}
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
QPainterPath silhouettePath(const DrawingView &view,const DrawingViewLayout &layout,const DrawingSilhouette &contour){
    QPainterPath path;bool first=true;for(const auto &point:contour.points){const auto p=paperPoint(view,layout,point);if(!std::isfinite(p.x())||!std::isfinite(p.y()))throw std::runtime_error("Contorno apparente non finito sul foglio.");if(first){path.moveTo(p);first=false;}else path.lineTo(p);}return path;
}
bool sameGeometry(const DrawingView &a,const DrawingView &b){
    return a.kind==b.kind&&a.sectionStyle==b.sectionStyle&&a.sectionReferences==b.sectionReferences&&a.tolerance==b.tolerance
        &&distance(a.plane.origin(),b.plane.origin())==0&&distance(a.plane.xDir(),b.plane.xDir())==0
        &&distance(a.plane.yDir(),b.plane.yDir())==0&&distance(a.plane.zDir(),b.plane.zDir())==0;
}
bool sameDimension(const DrawingDimension &a,const DrawingDimension &b){
    if(a.angleSides!=b.angleSides||a.decimals!=b.decimals||a.references.size()!=b.references.size())return false;
    for(std::size_t i=0;i<a.references.size();++i){const auto &x=a.references[i],&y=b.references[i];
        if(x.clipBoundary!=y.clipBoundary||x.role!=y.role||x.fraction!=y.fraction||x.polar!=y.polar||x.vertexName!=y.vertexName||x.boundaryName!=y.boundaryName||x.supportFace!=y.supportFace||x.curveSide!=y.curveSide||x.anchorSide!=y.anchorSide
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
    std::map<QUuid,DrawingSilhouetteResult> silhouettes;
    DrawingSilhouetteCache silhouetteCache;
    DrawingPageContext pageContext;
    std::map<QUuid,std::shared_ptr<const DrawingViewVisibility>> visibility;
    QAction *hiddenAction=nullptr,*shadedAction=nullptr,*centersAction=nullptr,*centerLinesAction=nullptr;
    bool computing=false;
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
    struct ViewPlacement {
        DrawingView definition;
        DrawingViewLayout layout;
        std::vector<DrawingCurve> curves;
        DrawingSilhouetteResult silhouettes;
        QPointF center;
        std::shared_ptr<const DrawingViewVisibility> visibility;
        QGraphicsItemGroup *item=nullptr;
    };
    std::unique_ptr<ViewPlacement> placement;
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
    QString replaceState(DrawingSheet next){
        try{
            std::map<QUuid,std::vector<DrawingCurve>> prepared;std::map<QUuid,DrawingSilhouetteResult> preparedSilhouettes;std::map<QUuid,std::shared_ptr<const DrawingViewVisibility>> preparedVisibility;QSet<QUuid> changedViews;
            auto seeds=silhouetteCache;
            for(const auto &definition:next.drawing.views){const auto layout=std::find_if(next.views.begin(),next.views.end(),[&](const auto &l){return l.viewId==definition.id;});
                const auto *old=viewFor(definition.id);const auto *oldLayout=layoutFor(definition.id);const auto cached=curves.find(definition.id);const auto contour=silhouettes.find(definition.id);const auto display=visibility.find(definition.id);
                const bool same=old&&oldLayout&&sameGeometry(*old,definition)&&oldLayout->features==layout->features&&cached!=curves.end();const bool contourSame=same&&old->scale==definition.scale&&contour!=silhouettes.end();
                if(contourSame&&display!=visibility.end()&&(!layout->showMesh||display->second->meshPrepared)){prepared[definition.id]=cached->second;preparedSilhouettes[definition.id]=contour->second;preparedVisibility[definition.id]=display->second;continue;}
                ComputedView result;const auto error=computeView(sourceFor(*layout),definition,*layout,seeds,result,same?&cached->second:nullptr,contourSame?&contour->second:nullptr);if(!error.isEmpty())return error;
                if(!same)changedViews.insert(definition.id);
                prepared[definition.id]=std::move(result.curves);preparedSilhouettes[definition.id]=std::move(result.contours);preparedVisibility[definition.id]=std::move(result.display);
            }
            std::map<QUuid,DrawingDimensionResult> measured;
            for(const auto &dimension:next.drawing.dimensions){const auto found=results.find(dimension.id);
                const auto old=std::find_if(state.drawing.dimensions.begin(),state.drawing.dimensions.end(),[&](const auto &d){return d.id==dimension.id;});
                measured[dimension.id]=!changedViews.contains(dimension.viewId)&&old!=state.drawing.dimensions.end()&&sameDimension(*old,dimension)&&found!=results.end()
                    ?found->second:this->measured(next,dimension,prepared.at(dimension.viewId));}
            silhouetteCache=std::move(seeds);state=std::move(next);curves=std::move(prepared);silhouettes=std::move(preparedSilhouettes);visibility=std::move(preparedVisibility);results=std::move(measured);cacheSections();return {};
        }catch(const std::exception &error){return QString::fromUtf8(error.what());}
    }
    QString restore(std::size_t index){
        DrawingSheet next;const auto error=decodeDrawingSheet(history[index].sheet,next);if(!error.isEmpty())return error;
        const auto applied=replaceState(std::move(next));if(!applied.isEmpty())return applied;
        historyIndex=index;refreshHistoryActions();notify();redraw();return {};
    }
    QString propagateLayout(DrawingSheet &next)const{
        QSet<QUuid> done,visiting;
        std::function<void(QUuid)> visit=[&](QUuid id){
            if(done.contains(id))return;
            if(visiting.contains(id))throw std::invalid_argument("Ciclo nelle viste derivate.");
            if(visiting.size()>=256)throw std::invalid_argument("Gerarchia delle viste troppo profonda.");
            visiting.insert(id);
            auto found=std::find_if(next.views.begin(),next.views.end(),[&](const auto &l){return l.viewId==id;});if(found==next.views.end())throw std::invalid_argument("Vista madre assente.");
            if(!found->relation.parentId.isNull()){
                const auto parent=found->relation.parentId;visit(parent);const auto *old=layoutFor(id),*oldParent=layoutFor(parent);
                const auto newParent=std::find_if(next.views.begin(),next.views.end(),[&](const auto &l){return l.viewId==parent;});
                if(old&&oldParent&&old->position==found->position)found->position+=newParent->position-oldParent->position;
                found->position=constrainDrawingViewPosition(next,*found,found->position);
            }
            visiting.remove(id);done.insert(id);
        };
        try{for(const auto &l:next.views)visit(l.viewId);}catch(const std::exception &e){return QString::fromUtf8(e.what());}
        const auto error=synchronizeDrawingViews(next);if(!error.isEmpty())return error;
        for(auto &p:next.dimensions){const auto d=std::find_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return d.id==p.dimensionId;});
            if(d==next.drawing.dimensions.end())continue;
            const auto *old=layoutFor(d->viewId);if(!old)continue;
            const auto now=std::find_if(next.views.begin(),next.views.end(),[&](const auto &l){return l.viewId==d->viewId;});if(now!=next.views.end())p.position+=now->position-old->position;
        }
        return {};
    }
    QString changeViews(DrawingSheet next,const QString &label){
        if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
        const auto sync=propagateLayout(next);if(!sync.isEmpty())return sync;
        for(auto &d:next.drawing.dimensions){const auto *old=viewFor(d.viewId);const auto current=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});
            if(!old||current==next.drawing.views.end()||sameGeometry(*old,*current))continue;
            const auto transform=[&](DrawingSectionReference &ref){if(!ref.requiresWitness)return;const auto world=old->plane.toGlobal(Vec3(ref.witness.x(),ref.witness.y(),0));const auto local=current->plane.toLocal(world);ref.witness=Vec2(local.x(),local.y());};
            transform(d.section);for(auto &ref:d.references)if(!drawingDatum(ref.role))transform(ref.curve);
        }
        for(const auto &d:next.drawing.dimensions){const auto v=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});
            if(v==next.drawing.views.end()||v->kind!=DrawingViewKind::ProjectedEdges||v->sectionReferences)continue;
            if(d.section.kind==DrawingCurveKind::Section)return QStringLiteral("Sono presenti quote di sezione: conservare i riferimenti del piano.");
            for(const auto &ref:d.references)if(!drawingDatum(ref.role)&&ref.curve.kind==DrawingCurveKind::Section)return QStringLiteral("Sono presenti quote di sezione: conservare i riferimenti del piano.");
        }
        QByteArray bytes;const auto valid=encodeDrawingSheet(next,bytes);if(!valid.isEmpty())return valid;
        const auto applied=replaceState(std::move(next));if(!applied.isEmpty())return applied;
        record(label,std::move(bytes));redraw();return {};
    }
    QString applyLayout(DrawingSheet next,const QString &label){
        if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
        const auto sync=propagateLayout(next);if(!sync.isEmpty())return sync;
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        if(bytes==history[historyIndex].sheet)return {};
        state=std::move(next);record(label,std::move(bytes));queueRedraw();return {};
    }
    QString moveViewTo(const QUuid &id,const QPointF &position,const QString &label){
        const auto *layout=layoutFor(id);if(!layout)return QStringLiteral("Vista non trovata.");
        auto next=state;for(auto &p:next.views)if(p.viewId==id)p.position=position;
        return applyLayout(std::move(next),label);
    }
    bool centerFor(const QUuid &id,QPointF &center)const{
        const auto *layout=layoutFor(id);const auto found=visibility.find(id);if(!layout||found==visibility.end())return false;QRectF bounds;bool first=true;
        const auto add=[&](const DrawingVisiblePath &path){const auto box=joinedPaths(path.visible,path.hidden).boundingRect();bounds=first?box:bounds.united(box);first=false;};
        for(const auto &path:found->second->curves)add(path);
        for(const auto &path:found->second->silhouettes)add(path);
        if(first)return false;
        center=layout->position+bounds.center();return true;
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
    void refreshHiddenAction(){
        if(!hiddenAction)return;
        const auto id=selectedView();const auto *definition=viewFor(id);const auto *layout=layoutFor(id);QSignalBlocker blocker(hiddenAction);
        hiddenAction->setEnabled(!readOnly&&definition&&drawingHasProjection(*definition));hiddenAction->setChecked(layout&&layout->showHiddenLines);
        for(auto *action:{centersAction,centerLinesAction})if(action){QSignalBlocker lock(action);action->setEnabled(!readOnly&&definition&&layout);action->setChecked(layout&&(action==centersAction?layout->showCenterMarks:layout->showCenterLines));}
        if(shadedAction){QSignalBlocker lock(shadedAction);shadedAction->setEnabled(!readOnly&&definition&&drawingHasProjection(*definition));shadedAction->setChecked(layout&&layout->showMesh);}
        if(auto *a=owner->findChild<QAction *>("drawingDetachViewAction"))a->setEnabled(!readOnly&&layout&&layout->relation.kind==DrawingDerivedKind::Orthographic);
        if(auto *a=owner->findChild<QAction *>("drawingReleaseAlignmentAction")){a->setEnabled(!readOnly&&layout&&layout->relation.kind==DrawingDerivedKind::Orthographic);a->setText(layout&&layout->relation.aligned?QStringLiteral("Libera allineamento"):QStringLiteral("Ripristina allineamento"));}
        for(auto name:{"drawingSectionHatchAction","drawingEditSectionLineAction","drawingReverseSectionAction"})if(auto *a=owner->findChild<QAction *>(name))a->setEnabled(!readOnly&&layout&&layout->relation.kind==DrawingDerivedKind::Section);
        if(auto *a=owner->findChild<QAction *>("drawingSectionHatchAction"))a->setEnabled(!readOnly&&definition&&layout&&definition->kind==DrawingViewKind::Section&&(layout->relation.kind==DrawingDerivedKind::Section||definition->sectionStyle!=DrawingSectionStyle::Removed));
        if(auto *a=owner->findChild<QAction *>("drawingSectionHatchAction"))a->setText(layout&&layout->relation.hatch?QStringLiteral("Nascondi tratteggio"):QStringLiteral("Mostra tratteggio"));
    }
    bool pointShown(const QUuid &id,std::size_t index,QPointF point,bool center=false)const{
        const auto *layout=layoutFor(id);if(!layout)return false;if(layout->showHiddenLines)return true;const auto found=visibility.find(id);if(found==visibility.end()||index>=found->second->curves.size())return false;
        const auto &path=found->second->curves[index].visible;if(path.isEmpty())return false;if(center)return true;QPainterPathStroker stroke;stroke.setWidth(.14);return stroke.createStroke(path).contains(point-layout->position);
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
        refreshHiddenAction();
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
        auto *root=new QTreeWidgetItem(tree,{state.title});root->setIcon(0,commandIcon("new"));root->setExpanded(true);QHash<QUuid,QTreeWidgetItem *> nodes;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);if(!layout)continue;
            auto *item=new QTreeWidgetItem(root,{layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4)});
            item->setIcon(0,commandIcon(definition.kind==DrawingViewKind::Section?"section":"viewFront"));
            item->setData(0,Qt::UserRole,"view");item->setData(0,Qt::UserRole+1,definition.id);item->setExpanded(true);nodes.insert(definition.id,item);
            item->setToolTip(0,QStringLiteral("Doppio clic per modificare la vista."));
            if(!layout->relation.parentId.isNull())item->setToolTip(0,layout->relation.kind==DrawingDerivedKind::Section?QStringLiteral("Sezione associata alla linea di taglio della madre; piano, scala e sorgenti seguono la madre."):QStringLiteral("Scala, orientamento e sorgenti ereditati dalla madre. Sgancia per renderla indipendente."));
            const auto contour=silhouettes.find(definition.id);if(contour!=silhouettes.end()&&!contour->second.warnings.empty()){item->setForeground(0,QColor("#ad6d00"));item->setToolTip(0,item->toolTip(0)+QStringLiteral("\n")+contour->second.warnings.join(QStringLiteral("\n")));}
            for(const auto &dimension:state.drawing.dimensions)if(dimension.viewId==definition.id){const auto found=results.find(dimension.id);
                const bool ready=found!=results.end()&&found->second.status==DrawingDimensionStatus::Ready;
                const auto measure=drawingDimensionName(dimension.measure);
                auto *child=new QTreeWidgetItem(item,{measure+QStringLiteral(" · ")+(ready?drawingDimensionText(dimension,found->second.value):QStringLiteral("Da verificare"))});
                child->setIcon(0,commandIcon("dimension"));child->setData(0,Qt::UserRole,"dimension");child->setData(0,Qt::UserRole+1,dimension.id);
                if(!ready){child->setForeground(0,QColor("#d34b4b"));if(found!=results.end())child->setToolTip(0,found->second.message);}}
        }
        for(const auto &l:state.views)if(!l.relation.parentId.isNull()&&nodes.contains(l.viewId)&&nodes.contains(l.relation.parentId)){
            auto *item=nodes.value(l.viewId);auto *oldParent=item->parent();oldParent->takeChild(oldParent->indexOfChild(item));nodes.value(l.relation.parentId)->addChild(item);
        }
        selectTreeItem(kind,selected);
    }
    void editSelectedView(){if(readOnly)return;const auto id=selectedView();if(id.isNull()){say(QStringLiteral("Seleziona una vista o una sua quota nell'albero o sul foglio."));return;}
        const auto *definition=viewFor(id);if(definition){if(!layoutFor(id)->relation.parentId.isNull())derivedProperties(id);else addViewDialog(definition->kind,id);}}
    struct ComputedView {std::vector<DrawingCurve> curves;DrawingSilhouetteResult contours;std::shared_ptr<DrawingViewVisibility> display;DrawingSilhouetteCache cache;};
    QString computeView(const DrawingModelSnapshot &source,const DrawingView &definition,const DrawingViewLayout &layout,
                        DrawingSilhouetteCache &cache,ComputedView &output,const std::vector<DrawingCurve> *reuseCurves=nullptr,const DrawingSilhouetteResult *reuseContours=nullptr){
        auto oldCurves=reuseCurves?*reuseCurves:std::vector<DrawingCurve>{};auto oldContours=reuseContours?*reuseContours:DrawingSilhouetteResult{};const bool keepCurves=reuseCurves,keepContours=reuseContours;
        const auto error=runDrawingComputation(owner,computing,QStringLiteral("Calcolo vista: %1").arg(layout.title),output,
            [source,definition,layout,seeds=cache,oldCurves=std::move(oldCurves),oldContours=std::move(oldContours),keepCurves,keepContours](auto phase)mutable{
                ComputedView result;result.cache=std::move(seeds);const auto ok=[](const QString &message){if(!message.isEmpty())throw std::runtime_error(message.toStdString());};
                if(keepCurves)result.curves=std::move(oldCurves);else ok(drawingViewCurves(source,definition,result.curves));phase(1);
                if(keepContours)result.contours=std::move(oldContours);else ok(drawingViewSilhouettes(source,definition,result.contours,result.cache));phase(2);
                DrawingSectionHatch hatch;ok(drawingSectionHatch(source,definition,result.curves,hatch));phase(3);
                result.display=std::make_shared<DrawingViewVisibility>();ok(drawingViewVisibility(source,definition,result.curves,result.contours,result.cache,*result.display,&hatch.material,layout.showMesh));
                result.display->sectionHatch=std::move(hatch.lines);result.display->sectionMaterial=std::move(hatch.material);result.display->warnings.append(hatch.warnings);phase(4);phase(5);return result;
            });
        if(error.isEmpty())cache=output.cache;
        return error;
    }
    void calculate(DrawingSheet nextState,DrawingEditorModel nextModel){
        std::map<QUuid,std::vector<DrawingSection>> prepared;std::map<QUuid,DrawingSilhouetteResult> preparedSilhouettes;std::map<QUuid,std::shared_ptr<const DrawingViewVisibility>> preparedVisibility;DrawingSilhouetteCache seeds;
        for(const auto &definition:nextState.drawing.views){const auto layout=std::find_if(nextState.views.begin(),nextState.views.end(),[&](const auto &l){return l.viewId==definition.id;});if(layout==nextState.views.end())continue;
            DrawingModelSnapshot source;source.version=nextModel.snapshot.version;for(const auto &body:nextModel.snapshot.bodies)if(layout->features.contains(body.featureId))source.bodies.push_back(body);
            ComputedView result;const auto error=computeView(source,definition,*layout,seeds,result);if(!error.isEmpty())throw std::runtime_error(error.toStdString());
            for(const auto &piece:result.curves)sectionPath(definition,*layout,piece);
            for(const auto &contour:result.contours.paths)silhouettePath(definition,*layout,contour);
            prepared[definition.id]=std::move(result.curves);preparedSilhouettes[definition.id]=std::move(result.contours);preparedVisibility[definition.id]=std::move(result.display);
        }
        std::map<QUuid,DrawingDimensionResult> measured;
        for(const auto &d:nextState.drawing.dimensions){const auto v=std::find_if(nextState.drawing.views.begin(),nextState.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});measured[d.id]=d.references.empty()?evaluateDrawingDimension(nextState.drawing,nextModel.snapshot,d.id):evaluateDrawingAttachments(d,*v,nextModel.snapshot,prepared.at(d.viewId));}
        state=std::move(nextState);model=std::move(nextModel);silhouetteCache=std::move(seeds);curves=std::move(prepared);silhouettes=std::move(preparedSilhouettes);visibility=std::move(preparedVisibility);results=std::move(measured);cacheSections();
    }
    QString insertionState(const DrawingView &definition,const DrawingViewLayout &layout,DrawingSheet &next,QByteArray &bytes)const{
        next=state;next.drawing.views.push_back(definition);next.views.append(layout);return encodeDrawingSheet(next,bytes);
    }
    QString insertPrepared(DrawingView definition,DrawingViewLayout layout,std::vector<DrawingCurve> pieces,DrawingSilhouetteResult contours,std::shared_ptr<const DrawingViewVisibility> display={}){
        DrawingSheet next;QByteArray bytes;const auto error=insertionState(definition,layout,next,bytes);if(!error.isEmpty())return error;
        if(pieces.empty()&&contours.paths.empty())return definition.kind==DrawingViewKind::Section?QStringLiteral("Il piano non interseca i corpi selezionati. Modificare posizione o piano."):QStringLiteral("Nessun bordo proiettabile nei corpi selezionati.");
        for(const auto &piece:pieces)sectionPath(definition,layout,piece);
        for(const auto &contour:contours.paths)silhouettePath(definition,layout,contour);
        if(!display){ComputedView result;const auto error=computeView(sourceFor(layout),definition,layout,silhouetteCache,result,&pieces,&contours);if(!error.isEmpty())return error;display=std::move(result.display);}
        state=std::move(next);curves[definition.id]=std::move(pieces);silhouettes[definition.id]=std::move(contours);visibility[definition.id]=std::move(display);cacheSections();record(QStringLiteral("Inserisci vista"),std::move(bytes));redraw();return {};
    }
    void cancelPlacement(){
        cancelCut();
        if(!placement)return;
        delete placement->item;placement.reset();view->setCursor(quoteAction->isChecked()?Qt::CrossCursor:Qt::ArrowCursor);
    }
    QString startPlacement(DrawingView definition,DrawingViewLayout layout){
        DrawingSheet next;QByteArray bytes;const auto valid=insertionState(definition,layout,next,bytes);if(!valid.isEmpty())return valid;
        ComputedView result;const auto generated=computeView(sourceFor(layout),definition,layout,silhouetteCache,result);if(!generated.isEmpty())return generated;
        auto pieces=std::move(result.curves);auto contours=std::move(result.contours);auto display=std::move(result.display);
        if(pieces.empty()&&contours.paths.empty())return definition.kind==DrawingViewKind::Section?QStringLiteral("Il piano non interseca i corpi selezionati. Modificare posizione o piano."):QStringLiteral("Nessuna geometria proiettabile nei corpi selezionati.");
        std::vector<QPainterPath> paths;QRectF bounds;bool first=true;
        const auto add=[&](const DrawingVisiblePath &path){const auto all=joinedPaths(path.visible,path.hidden);bounds=first?all.boundingRect():bounds.united(all.boundingRect());first=false;paths.push_back(joinedPaths(path.visible,layout.showHiddenLines?path.hidden:QPainterPath()));};
        for(const auto &path:display->curves)add(path);
        const auto edgePaths=paths.size();for(const auto &path:display->silhouettes)add(path);
        if((layout.relation.kind==DrawingDerivedKind::Section||definition.sectionStyle!=DrawingSectionStyle::Removed)&&layout.relation.hatch)paths.push_back(display->sectionHatch);
        auto prepared=std::make_unique<ViewPlacement>();prepared->definition=definition;prepared->layout=layout;prepared->curves=std::move(pieces);prepared->silhouettes=std::move(contours);prepared->center=bounds.center();prepared->visibility=std::move(display);
        // La scena possiede il gruppo e i figli; rimuove prima la vecchia anteprima.
        quoteAction->setChecked(false);cancelPlacement();resetQuote();
        auto *group=new QGraphicsItemGroup;scene->addItem(group);group->setData(kindRole,"viewPreview");group->setZValue(50);group->setOpacity(.75);group->setAcceptedMouseButtons(Qt::NoButton);prepared->item=group;
        if(layout.showMesh&&!prepared->visibility->shadedImage.isNull()){
            const auto &image=prepared->visibility->shadedImage;const auto &box=prepared->visibility->shadedBounds;auto *item=new QGraphicsPixmapItem(QPixmap::fromImage(image),group);item->setTransform(QTransform::fromScale(box.width()/image.width(),box.height()/image.height()));item->setPos(box.topLeft()-prepared->center);item->setZValue(-3);item->setAcceptedMouseButtons(Qt::NoButton);
            if(!prepared->visibility->sectionMaterial.isEmpty()){auto path=prepared->visibility->sectionMaterial.translated(-prepared->center);auto *cap=new QGraphicsPathItem(path,group);cap->setPen(Qt::NoPen);cap->setBrush(Qt::white);cap->setZValue(-2);cap->setAcceptedMouseButtons(Qt::NoButton);}
        }
        for(std::size_t i=0;i<paths.size();++i){paths[i].translate(-prepared->center);auto *item=new QGraphicsPathItem(paths[i],group);
            item->setPen(QPen(i<edgePaths&&prepared->curves[i].kind==DrawingCurveKind::Section?QColor("#187c59"):QColor("#1469c0"),.3,Qt::DashLine));item->setAcceptedMouseButtons(Qt::NoButton);}
        if(layout.showCenterMarks||layout.showCenterLines){const auto &centers=layout.showHiddenLines?prepared->visibility->allCenters:prepared->visibility->visibleCenters;
            if(layout.showCenterLines){auto *item=new QGraphicsPathItem(centers.axes.translated(-prepared->center),group);QPen pen(QColor("#1469c0"),.18);pen.setDashPattern({32,5,3,5});item->setPen(pen);item->setAcceptedMouseButtons(Qt::NoButton);}
            auto *item=new QGraphicsPathItem(centers.crosses.translated(-prepared->center),group);item->setPen(QPen(QColor("#1469c0"),.18));item->setAcceptedMouseButtons(Qt::NoButton);}
        auto *label=new QGraphicsSimpleTextItem(layout.title,group);fontFor(label);label->setBrush(QColor("#1469c0"));label->setPos(bounds.bottomLeft()-prepared->center+QPointF(0,6));label->setAcceptedMouseButtons(Qt::NoButton);
        const auto mouse=view->viewport()->mapFromGlobal(QCursor::pos());const auto initial=view->viewport()->rect().contains(mouse)?view->mapToScene(mouse):QPointF(state.size.width()/2,state.size.height()/2);
        group->setPos(initial);placement=std::move(prepared);placementPointer(initial,false);view->setCursor(Qt::CrossCursor);view->setFocus();say(QStringLiteral("Sposta l'anteprima e fai clic sul foglio per inserire la vista. Esc o tasto destro annulla."));return {};
    }
    bool placementPointer(QPointF point,bool click){
        if(!placement)return false;
        auto proposed=point-placement->center;auto layout=placement->layout;layout.position=proposed;
        proposed=constrainDrawingViewPosition(state,layout,proposed);point=proposed+placement->center;
        placement->item->setPos(point);placement->item->setVisible(true);
        if(click){if(!QRectF(QPointF(0,0),state.size).contains(point)){say(QStringLiteral("Scegli un punto all'interno del foglio. Esc annulla."));return true;}
            auto layout=placement->layout;layout.position=point-placement->center;
            const auto error=insertPrepared(placement->definition,layout,placement->curves,placement->silhouettes,placement->visibility);if(!error.isEmpty())say(error);
        }
        return true;
    }
    bool placementKey(int key){
        if(!placement)return false;
        if(key==Qt::Key_Escape){cancelPlacement();say(QStringLiteral("Inserimento annullato."));return true;}
        if(key==Qt::Key_Clear){placement->item->setVisible(false);return true;}return false;
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
    int drawSheet(QGraphicsScene *target,bool interactive){
        auto *scene=target;
        scene->setSceneRect(-8,-8,state.size.width()+16,state.size.height()+16);
        auto *paper=scene->addRect(QRectF(QPointF(0,0),state.size),interactive?QPen(QColor("#b5bdc4"),0.25):QPen(Qt::NoPen),QBrush(Qt::white));paper->setZValue(-20);paper->setData(kindRole,"paper");
        auto *page=new DrawingPageItem(state,pageContext);page->setData(kindRole,"page");scene->addItem(page);
        int missing=0;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);const auto found=curves.find(definition.id);
            if(!layout||found==curves.end())continue;
            QRectF bounds;const auto display=visibility.at(definition.id);
            if(layout->showMesh&&!display->shadedImage.isNull()){
                auto *image=scene->addPixmap(QPixmap::fromImage(display->shadedImage));image->setTransform(QTransform::fromScale(display->shadedBounds.width()/display->shadedImage.width(),display->shadedBounds.height()/display->shadedImage.height()));image->setPos(layout->position+display->shadedBounds.topLeft());image->setZValue(-3);image->setData(kindRole,"viewShading");image->setData(viewRole,definition.id);image->setAcceptedMouseButtons(Qt::NoButton);image->setTransformationMode(Qt::SmoothTransformation);
                if(!display->sectionMaterial.isEmpty()){auto *cap=scene->addPath(display->sectionMaterial.translated(layout->position),QPen(Qt::NoPen),Qt::white);cap->setZValue(-2);cap->setData(kindRole,"sectionMaterial");cap->setAcceptedMouseButtons(Qt::NoButton);}
            }
            if((layout->relation.kind==DrawingDerivedKind::Section||definition.sectionStyle!=DrawingSectionStyle::Removed)&&layout->relation.hatch&&!display->sectionHatch.isEmpty()){
                auto *hatch=scene->addPath(display->sectionHatch.translated(layout->position),QPen(QColor("#555555"),.13));hatch->setZValue(-1);hatch->setData(kindRole,"sectionHatch");hatch->setData(viewRole,definition.id);hatch->setAcceptedMouseButtons(Qt::NoButton);
            }
            drawCutLines(scene,definition,*layout);
            for(std::size_t i=0;i<found->second.size();++i){const auto &piece=found->second[i];const bool projected=piece.kind==DrawingCurveKind::ProjectedEdge;const auto &paths=display->curves.at(i);
                auto visible=paths.visible.translated(layout->position),hidden=paths.hidden.translated(layout->position);if(interactive)paintedPaths[definition.id].push_back(joinedPaths(visible,layout->showHiddenLines?hidden:QPainterPath()));
                const auto add=[&](const QPainterPath &path,bool obscured){if(path.isEmpty())return;auto *item=new SectionItem(path);item->hidden=obscured;item->sectionReference=!projected&&definition.kind==DrawingViewKind::ProjectedEdges;
                    item->setFlags(QGraphicsItem::ItemIsSelectable);item->setData(kindRole,projected?"projection":"section");item->setData(viewRole,definition.id);item->setData(indexRole,qulonglong(i));item->setData(4,obscured);item->setZValue(obscured?0:1);
                    item->setToolTip(projected?(obscured?QStringLiteral("Bordo nascosto. Le quote usano la curva esatta proiettata."):QStringLiteral("Bordo visibile. Le quote misurano nel piano della vista.")):QStringLiteral("Curva esatta di sezione fra la faccia e il piano della vista."));scene->addItem(item);};
                add(visible,false);if(layout->showHiddenLines)add(hidden,true);const auto box=joinedPaths(visible,hidden).boundingRect();bounds=bounds.isNull()?box:bounds.united(box);}
            const auto contour=silhouettes.find(definition.id);if(contour!=silhouettes.end())for(std::size_t i=0;i<contour->second.paths.size();++i){const auto &piece=contour->second.paths[i];const auto &paths=display->silhouettes.at(i);
                const auto add=[&](QPainterPath path,bool hidden){if(path.isEmpty())return;path.translate(layout->position);auto *item=scene->addPath(path,QPen(hidden?QColor("#80858a"):QColor("#222222"),hidden?.18:.25,hidden?Qt::DashLine:Qt::SolidLine));item->setData(kindRole,"silhouette");item->setData(viewRole,definition.id);item->setData(4,hidden);item->setZValue(hidden?0:1);item->setAcceptedMouseButtons(Qt::NoButton);
                    item->setToolTip(QStringLiteral("Contorno apparente approssimato, non quotabile. Per le quote usare bordi o sezioni del piano.")+(piece.refinementLimited?QStringLiteral(" Raffinamento limitato su questo tratto."):QString()));};
                add(paths.visible,false);if(layout->showHiddenLines)add(paths.hidden,true);const auto box=joinedPaths(paths.visible,paths.hidden).translated(layout->position).boundingRect();bounds=bounds.isNull()?box:bounds.united(box);}
            if(layout->showCenterMarks||layout->showCenterLines){
                const auto &centers=layout->showHiddenLines?display->allCenters:display->visibleCenters;
                const auto addCenter=[&](const QPainterPath &path,const QPen &pen,const char *kind){if(path.isEmpty())return;auto *item=scene->addPath(path,pen);item->setPos(layout->position);item->setData(kindRole,kind);item->setData(viewRole,definition.id);item->setAcceptedMouseButtons(Qt::NoButton);item->setZValue(3);};
                if(layout->showCenterLines){QPen pen(QColor("#444444"),.18);pen.setDashPattern({32,5,3,5});addCenter(centers.axes,pen,"centerLines");}
                addCenter(centers.crosses,QPen(QColor("#444444"),.18),"centerMarks");
            }
            if(drawingHalfSection(definition)&&!bounds.isNull()){
                QPen pen(QColor("#555555"),.18);pen.setDashPattern({10,2,2,2});auto *axis=scene->addLine(layout->position.x(),bounds.top()-3,layout->position.x(),bounds.bottom()+3,pen);axis->setData(kindRole,"sectionAxis");axis->setData(viewRole,definition.id);axis->setAcceptedMouseButtons(Qt::NoButton);axis->setZValue(3);
            }
            auto *label=new MoveLabel((layout->relation.kind==DrawingDerivedKind::Section?layout->relation.label+QStringLiteral("–")+layout->relation.label+QStringLiteral(" · "):QString())+layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4));fontFor(label);label->setBrush(QColor("#35495c"));
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
            if(!dimension.references.empty()){paintAttached(dimension,result->second,label->sceneBoundingRect().center(),false,target);continue;}
            const auto *piece=resolved(dimension);const auto *definition=viewFor(dimension.viewId);const auto *layout=layoutFor(dimension.viewId);
            if(!piece||!definition||!layout)continue;
            const auto point=[&](double parameter){const auto p=definition->plane.toLocal(piece->curve->point(parameter));return Vec2(p.x(),p.y());};
            auto rendered=result->second;rendered.geometry=DrawingDimensionResult::Geometry::Linear;
            rendered.first=point(piece->range.lo);rendered.second=point(piece->range.hi);
            if(dimension.measure==DrawingMeasure::SectionLength)rendered.first=point(.5*(piece->range.lo+piece->range.hi));
            paintAttached(dimension,rendered,label->sceneBoundingRect().center(),false,target);
        }
        return missing;
    }
    void redraw(){
        cancelPlacement();resetQuote();rebuilding=true;scene->clear();paintedPaths.clear();const int missing=drawSheet(scene,true);
        refreshTree();rebuilding=false;selectionFromTree();refreshNotable();
        const bool projected=std::any_of(state.drawing.views.begin(),state.drawing.views.end(),[](const auto &v){return v.kind==DrawingViewKind::ProjectedEdges;});
        say(missing?QStringLiteral("%1 quote richiedono verifica. Seleziona il testo per leggere il motivo.").arg(missing):projected?
            QStringLiteral("Mostra linee nascoste agisce sulla vista selezionata. Bordi nascosti tratteggiati quotabili; contorni approssimati esclusi dalle quote."):
            QStringLiteral("Seleziona una curva e inserisci una quota. Trascina titoli e testi per posizionarli."));
        QStringList warnings;for(const auto &[id,contour]:silhouettes){(void)id;warnings.append(contour.warnings);}for(const auto &[id,display]:visibility){(void)id;warnings.append(display->warnings);}if(!warnings.empty())say(message->text()+QStringLiteral(" · Attenzione: ")+warnings.join(QStringLiteral(" ")));
    }
    QPointF paperLocal(const QUuid &id,Vec2 p)const{const auto *v=viewFor(id);const auto *l=layoutFor(id);return l->position+QPointF(v->scale*p.x(),-v->scale*p.y());}
    Vec2 localPaper(const QUuid &id,QPointF p)const{const auto *v=viewFor(id);const auto *l=layoutFor(id);return {(p.x()-l->position.x())/v->scale,(l->position.y()-p.y())/v->scale};}
    void clearOverlay(){for(auto *item:overlay)delete item;overlay.clear();}
    void resetQuote(){clearOverlay();quoteReferences.clear();quoteView={};snapChoices.clear();snapCycle=0;}
    void paintAttached(const DrawingDimension &dimension,const DrawingDimensionResult &result,QPointF text,bool preview=false,QGraphicsScene *target=nullptr){
        auto *scene=target?target:this->scene;
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
                const auto add=[&](DrawingPointRole role,Vec3 point){const auto paper=paperPoint(*definition,*layout,point);if(pointShown(id,i,paper,role==DrawingPointRole::Center))notable.push_back({id,i,role,std::numeric_limits<double>::quiet_NaN(),paper,0});};
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
                if(gap<=9&&pointShown(id,std::size_t(index),paper))snapChoices.push_back({id,std::size_t(index),modifiers&Qt::AltModifier?DrawingPointRole::Nearest:DrawingPointRole::Curve,found.parameter,paper,gap});
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
        if(projected){form->addRow(references);auto *note=new QLabel(QStringLiteral("Linee nascoste escluse; il pulsante Mostra linee nascoste le rende disponibili nella vista selezionata. I contorni apparenti sono approssimati e non quotabili. Usare bordi o sezioni del piano per le quote."),&dialog);note->setWordWrap(true);form->addRow(note);}
        auto *sectionStyle=new QComboBox(&dialog);sectionStyle->setObjectName("drawingSectionStyle");sectionStyle->addItems({"Sezione rimossa","Sezione completa","Mezza sezione destra","Mezza sezione sinistra"});sectionStyle->setCurrentIndex(editing?int(original->sectionStyle):0);
        if(!projected){form->addRow(QStringLiteral("Tipo di sezione:"),sectionStyle);auto *note=new QLabel(QStringLiteral("La mezza sezione usa l’asse Y del piano corrente (X=0): il lato scelto viene tagliato, l’altro mantiene l’esterno."),&dialog);note->setWordWrap(true);form->addRow(note);}else sectionStyle->hide();
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
            definition.scale=editing&&scale->value()==initialScale?original->scale:scale->value();definition.kind=kind;definition.sectionStyle=projected?DrawingSectionStyle::Removed:DrawingSectionStyle(sectionStyle->currentIndex());definition.sectionReferences=projected&&references->isChecked();
            DrawingViewLayout layout=editing?*placement:DrawingViewLayout{};layout.viewId=definition.id;layout.title=title->text().trimmed();layout.features.clear();
            for(int row=0;row<bodies->count();++row)if(bodies->item(row)->checkState()==Qt::Checked)layout.features.append(bodies->item(row)->data(Qt::UserRole).toULongLong());
            if(!editing){const auto local=definition.plane.toLocal(center);layout.position=QPointF(state.size.width()/2,state.size.height()/2)-QPointF(local.x()*definition.scale,-local.y()*definition.scale);}
            const auto error=editing?owner->updateView(definition,layout):owner->beginViewPlacement(definition,layout);
            errorLabel->setText(error);if(error.isEmpty())dialog.accept();
        });
        dialog.exec();if(placement)view->setFocus();
    }
    struct CutPlacement {
        QUuid parent,existing;QString label;DrawingSectionStyle style=DrawingSectionStyle::Removed;bool reverse=false,started=false;QPointF first,cursor;
        QGraphicsPathItem *item=nullptr;
    };
    std::unique_ptr<CutPlacement> cut;
    void cancelCut(){if(cut){delete cut->item;cut.reset();view->setCursor(Qt::ArrowCursor);}}
    void cutOverlay(){
        if(!cut)return;
        QPainterPath path;const auto a=cut->started?cut->first:cut->cursor,b=cut->cursor;path.moveTo(a);path.lineTo(b);
        path.moveTo(b+QPointF(-2,0));path.lineTo(b+QPointF(2,0));path.moveTo(b+QPointF(0,-2));path.lineTo(b+QPointF(0,2));
        if(cut->started&&QLineF(a,b).length()>1e-6){const auto t=(b-a)/QLineF(a,b).length();const auto n=(cut->reverse?-1:1)*QPointF(t.y(),-t.x());for(auto p:((cut->style==DrawingSectionStyle::HalfRight||cut->style==DrawingSectionStyle::HalfLeft)?QVector<QPointF>{b}:QVector<QPointF>{a,b})){path.moveTo(p-n*7);path.lineTo(p);path.moveTo(p-n*2+t);path.lineTo(p);path.lineTo(p-n*2-t);}}
        cut->item->setPath(path);cut->item->setVisible(true);
    }
    bool cutPointer(QPointF cursor,Qt::KeyboardModifiers modifiers,bool click){
        if(!cut)return false;
        const double zoom=std::abs(view->transform().m11());double gap=9;QPointF snapped=cursor;
        if(!(modifiers&Qt::AltModifier))for(const auto &pick:notable)if(pick.view==cut->parent){const double current=QLineF(pick.paper,cursor).length()*zoom;if(current<gap){gap=current;snapped=pick.paper;}}
        cursor=snapped;
        if(cut->started&&(modifiers&Qt::ShiftModifier)){const auto d=cursor-cut->first;if(std::abs(d.x())>=std::abs(d.y()))cursor.setY(cut->first.y());else cursor.setX(cut->first.x());}
        cut->cursor=cursor;cutOverlay();
        if(!click)return true;
        if(!QRectF(QPointF(),state.size).contains(cursor)){say(QStringLiteral("Scegli un punto sul foglio."));return true;}
        if(!cut->started){cut->first=cursor;cut->started=true;say(QStringLiteral("Secondo punto del taglio. Maiusc: orizzontale/verticale; R: inverti frecce; Alt: senza snap; Esc: annulla."));return true;}
        if(QLineF(cut->first,cursor).length()<1){say(QStringLiteral("Scegli un secondo punto distinto, distante almeno 1 mm sul foglio."));return true;}
        const auto parent=cut->parent,existing=cut->existing;const auto label=cut->label;const auto style=cut->style;const bool reverse=cut->reverse;const auto a=localPaper(parent,cut->first),b=localPaper(parent,cursor);
        // beginSectionPlacement prepara tutto prima di sostituire lo strumento attivo.
        const auto error=existing.isNull()?owner->beginSectionPlacement(parent,QPointF(a.x(),a.y()),QPointF(b.x(),b.y()),label,reverse,style):owner->setSectionLine(existing,QPointF(a.x(),a.y()),QPointF(b.x(),b.y()));if(!error.isEmpty())say(error);
        return true;
    }
    bool cutKey(int key){
        if(!cut)return false;
        if(key==Qt::Key_Escape){cancelCut();say(QStringLiteral("Taglio annullato."));return true;}
        if(key==Qt::Key_R){cut->reverse=!cut->reverse;cutOverlay();return true;}
        if(key==Qt::Key_Clear){cut->item->setVisible(false);return true;}
        return true;
    }
    void drawCutLines(QGraphicsScene *target,const DrawingView &parent,const DrawingViewLayout &layout){
        for(const auto &section:state.views){const auto &r=section.relation;if(r.kind!=DrawingDerivedKind::Section||r.parentId!=parent.id)continue;
            const auto point=[&](QPointF p){return layout.position+QPointF(p.x()*parent.scale,-p.y()*parent.scale);};
            auto a=point(r.cutStart),b=point(r.cutEnd);const auto t=(b-a)/QLineF(a,b).length();const auto n=(r.reverse?-1:1)*QPointF(t.y(),-t.x());const auto *sectionView=viewFor(section.viewId);const bool half=sectionView&&drawingHalfSection(*sectionView);if(!half)a-=t*4;b+=t*4;
            const auto add=[&](QPainterPath path,QPen pen){auto *item=target->addPath(path,pen);item->setZValue(5);item->setData(kindRole,"cuttingLine");item->setData(viewRole,parent.id);item->setData(idRole,section.viewId);item->setAcceptedMouseButtons(Qt::NoButton);};
            QPainterPath center;center.moveTo(a);center.lineTo(b);QPen chain(QColor("#222222"),.2);chain.setDashPattern({12,2,2,2});add(center,chain);
            for(const auto p:(half?QVector<QPointF>{b}:QVector<QPointF>{a,b})){QPainterPath arrow;arrow.moveTo(p-n*8);arrow.lineTo(p);arrow.moveTo(p-n*2.5+t);arrow.lineTo(p);arrow.lineTo(p-n*2.5-t);add(arrow,QPen(QColor("#222222"),.4));
                auto *label=target->addSimpleText(r.label);fontFor(label,.3);label->setBrush(QColor("#222222"));label->setPos(p-n*10+QPointF(1,-2));label->setZValue(6);label->setData(kindRole,"cuttingLabel");label->setData(idRole,section.viewId);label->setAcceptedMouseButtons(Qt::NoButton);}
        }
    }
    void derivedDialog(bool section){
        if(readOnly)return;
        QDialog dialog(owner);dialog.setObjectName(section?"drawingTechnicalSectionDialog":"drawingDerivedViewDialog");dialog.setWindowTitle(section?QStringLiteral("Sezione da linea di taglio"):QStringLiteral("Vista derivata"));auto *form=new QFormLayout(&dialog);
        auto *parent=new QComboBox(&dialog);parent->setObjectName("drawingParentView");for(const auto &l:state.views)if(viewFor(l.viewId)->kind==DrawingViewKind::ProjectedEdges)parent->addItem(l.title,l.viewId);
        if(!parent->count()){say(QStringLiteral("Inserisci prima una vista proiettata."));return;}const auto chosen=parent->findData(selectedView());if(chosen>=0)parent->setCurrentIndex(chosen);form->addRow(QStringLiteral("Vista madre:"),parent);
        auto *side=new QComboBox(&dialog);side->setObjectName("drawingProjectionSide");side->addItems({"A sinistra","A destra","Sopra","Sotto"});side->setCurrentIndex(1);
        auto *method=new QComboBox(&dialog);method->setObjectName("drawingProjectionMethod");method->addItems({"Primo diedro (europeo)","Terzo diedro (americano)"});
        auto *label=new QLineEdit(&dialog);label->setObjectName("drawingSectionIdentifier");label->setMaxLength(8);int number=0;QString identifier;do{identifier=number<26?QString(QChar('A'+number)):QStringLiteral("S%1").arg(number+1);++number;}while(std::any_of(state.views.cbegin(),state.views.cend(),[&](const auto &l){return l.relation.kind==DrawingDerivedKind::Section&&l.relation.label.compare(identifier,Qt::CaseInsensitive)==0;}));label->setText(identifier);
        auto *reverse=new QCheckBox(QStringLiteral("Inverti verso delle frecce"),&dialog);reverse->setObjectName("drawingSectionReverse");
        auto *style=new QComboBox(&dialog);style->setObjectName("drawingSectionStyle");style->addItems({"Sezione rimossa","Sezione completa","Mezza sezione destra","Mezza sezione sinistra"});style->setCurrentIndex(1);
        if(section){form->addRow(QStringLiteral("Tipo:"),style);form->addRow(QStringLiteral("Identificativo:"),label);form->addRow(reverse);}else{style->hide();form->addRow(QStringLiteral("Posizione sul foglio:"),side);form->addRow(QStringLiteral("Metodo:"),method);}
        auto *note=new QLabel(section?QStringLiteral("Indica due punti sulla madre, poi posiziona l’anteprima. Completa: mostra il corpo rimasto dietro al taglio. Nella mezza sezione il primo punto e’ sull’asse, il secondo lungo il taglio: destra/sinistra si riferisce alla sezione risultante. Tratteggio solo sul materiale solido; le superfici restano quotabili."):QStringLiteral("Scala e sorgenti sono ereditate. L'allineamento segue l'origine geometrica della madre; trascina la figlia lungo l'asse libero. Il primo diedro colloca la vista sinistra a destra e la superiore sotto."),&dialog);note->setWordWrap(true);form->addRow(note);
        auto *error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{const auto result=section?owner->beginSectionLine(parent->currentData().toUuid(),label->text().trimmed().toUpper(),reverse->isChecked(),{},DrawingSectionStyle(style->currentIndex())):owner->beginDerivedViewPlacement(parent->currentData().toUuid(),DrawingProjectionSide(side->currentIndex()),DrawingProjectionMethod(method->currentIndex()));error->setText(result);if(result.isEmpty())dialog.accept();});dialog.resize(490,280);dialog.exec();if(placement||cut)view->setFocus();
    }
    void derivedProperties(QUuid id){
        const auto *v=viewFor(id);const auto *l=layoutFor(id);if(!v||!l)return;
        auto layout=*l;const bool section=layout.relation.kind==DrawingDerivedKind::Section;
        QDialog dialog(owner);dialog.setObjectName("drawingDerivedPropertiesDialog");dialog.setWindowTitle(QStringLiteral("Proprieta' vista derivata"));auto *form=new QFormLayout(&dialog);
        auto *title=new QLineEdit(layout.title,&dialog);title->setObjectName("drawingDerivedTitle");title->setMaxLength(256);form->addRow(QStringLiteral("Titolo:"),title);
        auto *label=new QLineEdit(layout.relation.label,&dialog);label->setMaxLength(8);label->setObjectName("drawingSectionIdentifier");
        auto *flag=new QCheckBox(section?QStringLiteral("Mostra tratteggio"):QStringLiteral("Mantieni allineamento"),&dialog);flag->setChecked(section?layout.relation.hatch:layout.relation.aligned);flag->setObjectName("drawingDerivedFlag");
        auto *reverse=new QCheckBox(QStringLiteral("Inverti verso delle frecce"),&dialog);reverse->setChecked(layout.relation.reverse);reverse->setObjectName("drawingSectionReverse");
        auto *style=new QComboBox(&dialog);style->setObjectName("drawingSectionStyle");style->addItems({"Sezione rimossa","Sezione completa","Mezza sezione destra","Mezza sezione sinistra"});style->setCurrentIndex(int(v->sectionStyle));
        if(section){form->addRow(QStringLiteral("Tipo:"),style);form->addRow(QStringLiteral("Identificativo:"),label);form->addRow(reverse);}else style->hide();form->addRow(flag);
        auto *references=new QCheckBox(QStringLiteral("Sezioni del piano come riferimenti di quota"),&dialog);references->setObjectName("drawingDerivedSectionReferences");references->setChecked(v->sectionReferences);if(!section)form->addRow(references);else references->hide();
        auto *note=new QLabel(section?QStringLiteral("Scala, piano e corpi seguono la madre. Ridefinisci linea di taglio conserva quote e identificativo. Per una mezza sezione indica prima un punto sull’asse: il cambio tipo non riposiziona l’asse automaticamente."):QStringLiteral("Scala, piano e corpi seguono la vista madre. Sgancia vista conserva l'orientamento corrente e permette modifiche indipendenti."),&dialog);note->setWordWrap(true);form->addRow(note);
        auto *error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{auto next=state;for(auto &p:next.views)if(p.viewId==id){p.title=title->text().trimmed();if(section){p.relation.label=label->text().trimmed().toUpper();p.relation.hatch=flag->isChecked();p.relation.reverse=reverse->isChecked();}else p.relation.aligned=flag->isChecked();}for(auto &v:next.drawing.views)if(v.id==id){if(section)v.sectionStyle=DrawingSectionStyle(style->currentIndex());else v.sectionReferences=references->isChecked();}const auto result=changeViews(std::move(next),QStringLiteral("Proprieta' vista derivata"));error->setText(result);if(result.isEmpty())dialog.accept();});dialog.exec();
    }
    void pageDialog(){
        if(readOnly)return;
        QDialog dialog(owner);dialog.setObjectName("drawingPageDialog");dialog.setWindowTitle(QStringLiteral("Formato foglio e cartiglio"));auto *form=new QFormLayout(&dialog);
        auto *title=new QLineEdit(state.title,&dialog);title->setMaxLength(256);title->setObjectName("drawingPageTitle");form->addRow(QStringLiteral("Titolo:"),title);
        auto *format=new QComboBox(&dialog);format->setObjectName("drawingPaperFormat");format->addItems({"A4","A3","A2","A1","A0","Personalizzato"});format->setCurrentText(drawingPaperName(state.size));
        auto *orientation=new QComboBox(&dialog);orientation->setObjectName("drawingPaperOrientation");orientation->addItems({"Orizzontale","Verticale"});orientation->setCurrentIndex(state.size.width()>=state.size.height()?0:1);
        auto *width=new QDoubleSpinBox(&dialog),*height=new QDoubleSpinBox(&dialog);width->setObjectName("drawingPaperWidth");height->setObjectName("drawingPaperHeight");for(auto *spin:{width,height}){spin->setRange(50,2000);spin->setDecimals(2);spin->setSuffix(" mm");}width->setValue(state.size.width());height->setValue(state.size.height());
        form->addRow(QStringLiteral("Formato:"),format);form->addRow(QStringLiteral("Orientamento:"),orientation);form->addRow(QStringLiteral("Larghezza:"),width);form->addRow(QStringLiteral("Altezza:"),height);
        const auto sizes=QVector<QSizeF>{{210,297},{297,420},{420,594},{594,841},{841,1189}};
        const auto update=[=]{const bool custom=format->currentIndex()==5;width->setEnabled(custom);height->setEnabled(custom);orientation->setEnabled(!custom);if(!custom){auto size=sizes[format->currentIndex()];if(orientation->currentIndex()==0)size.transpose();width->setValue(size.width());height->setValue(size.height());}};
        connect(format,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[=](int){update();});connect(orientation,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[=](int){update();});update();
        auto *visible=new QCheckBox(QStringLiteral("Mostra cartiglio"),&dialog);visible->setObjectName("drawingTitleBlockVisible");visible->setChecked(state.titleBlock.visible);form->addRow(visible);
        const auto field=[&](const char *name,const QString &label,const QString &value,int maximum){auto *edit=new QLineEdit(value,&dialog);edit->setObjectName(name);edit->setMaxLength(maximum);form->addRow(label,edit);return edit;};
        auto *code=field("drawingTitleBlockCode",QStringLiteral("Codice tavola:"),state.titleBlock.code,128),*organization=field("drawingTitleBlockOrganization",QStringLiteral("Organizzazione:"),state.titleBlock.organization,128),*author=field("drawingTitleBlockAuthor",QStringLiteral("Disegnato da:"),state.titleBlock.author,128),*date=field("drawingTitleBlockDate",QStringLiteral("Data:"),state.titleBlock.date,32);
        auto *note=new QLabel(QStringLiteral("Le revisioni di tavola e modello sono ricavate dai rispettivi check-in. Il cambio formato conserva scala e posizione delle viste."),&dialog);note->setWordWrap(true);form->addRow(note);
        auto *error=new QLabel(&dialog);error->setObjectName("drawingPageError");error->setWordWrap(true);form->addRow(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{const auto message=owner->setPageSettings(title->text().trimmed(),QSizeF(width->value(),height->value()),{visible->isChecked(),code->text(),organization->text(),author->text(),date->text()});error->setText(message);if(message.isEmpty())dialog.accept();});dialog.resize(510,540);dialog.exec();
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
    action(QStringLiteral("Vista derivata..."),"drawingDerivedViewAction",[this]{impl_->derivedDialog(false);});
    action(QStringLiteral("Sezione da linea di taglio..."),"drawingTechnicalSectionAction",[this]{impl_->derivedDialog(true);});
    action(QStringLiteral("Sgancia vista"),"drawingDetachViewAction",[this]{impl_->report(detachView(impl_->selectedView()));});
    action(QStringLiteral("Libera/ripristina allineamento"),"drawingReleaseAlignmentAction",[this]{const auto *l=impl_->layoutFor(impl_->selectedView());impl_->report(setViewAlignment(impl_->selectedView(),l?!l->relation.aligned:false));});
    action(QStringLiteral("Mostra/nascondi tratteggio"),"drawingSectionHatchAction",[this]{const auto *l=impl_->layoutFor(impl_->selectedView());impl_->report(setSectionHatch(impl_->selectedView(),l?!l->relation.hatch:false));});
    action(QStringLiteral("Ridefinisci linea di taglio..."),"drawingEditSectionLineAction",[this]{const auto id=impl_->selectedView();const auto *l=impl_->layoutFor(id);if(l)impl_->report(beginSectionLine(l->relation.parentId,l->relation.label,l->relation.reverse,id));});
    action(QStringLiteral("Inverti verso sezione"),"drawingReverseSectionAction",[this]{impl_->report(reverseSection(impl_->selectedView()));});
    action(QStringLiteral("Formato foglio e cartiglio..."),"drawingPageSettingsAction",[this]{impl_->pageDialog();});
    action(QStringLiteral("Modifica vista..."),"drawingEditViewAction",[this]{impl_->editSelectedView();});
    action(QStringLiteral("Allinea vista..."),"drawingAlignViewAction",[this]{impl_->alignDialog();});
    impl_->hiddenAction=action(QStringLiteral("Mostra linee nascoste"),"drawingHiddenLinesAction",[this]{impl_->report(setHiddenLinesVisible(impl_->selectedView(),impl_->hiddenAction->isChecked()));});impl_->hiddenAction->setCheckable(true);impl_->hiddenAction->setToolTip(QStringLiteral("Mostra/nasconde i tratti coperti nella sola vista selezionata, anche per quotarli."));toolbar->addSeparator();
    impl_->centersAction=action(QStringLiteral("Mostra centri"),"drawingCenterMarksAction",[this]{const auto error=setCenterMarksVisible(impl_->selectedView(),impl_->centersAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});
    impl_->centersAction->setCheckable(true);impl_->centersAction->setIcon(commandIcon("drawingCenterMark"));impl_->centersAction->setIconVisibleInMenu(true);impl_->centersAction->setToolTip(QStringLiteral("Croci sui centri esatti di cerchi, archi ed ellissi della vista selezionata. Le quote usano gli snap geometrici esistenti."));
    impl_->centerLinesAction=action(QStringLiteral("Mostra assi delle coniche"),"drawingCenterLinesAction",[this]{const auto error=setCenterLinesVisible(impl_->selectedView(),impl_->centerLinesAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});
    impl_->centerLinesAction->setCheckable(true);impl_->centerLinesAction->setIcon(commandIcon("drawingCenterLines"));impl_->centerLinesAction->setIconVisibleInMenu(true);impl_->centerLinesAction->setToolTip(QStringLiteral("Assi a tratto-punto di cerchi ed ellissi, con prolungamento di 2 mm sul foglio; segue la geometria e la scala della vista."));
    impl_->shadedAction=action(QStringLiteral("Mostra facce ombreggiate"),"drawingShadedFacesAction",[this]{const auto error=setShadedFacesVisible(impl_->selectedView(),impl_->shadedAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});impl_->shadedAction->setCheckable(true);impl_->shadedAction->setIcon(commandIcon("viewFront"));impl_->shadedAction->setToolTip(QStringLiteral("Facce e colori del modello nella vista selezionata; bordi e quote restano esatti."));
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
    impl_->view->pointer=[this](QPointF point,Qt::KeyboardModifiers modifiers,bool click){try{return impl_->cut?impl_->cutPointer(point,modifiers,click):impl_->placement?impl_->placementPointer(point,click):impl_->quotePointer(point,modifiers,click);}catch(const std::exception &e){impl_->say(QString::fromUtf8(e.what()));return true;}};
    impl_->view->key=[this](int key){return impl_->cut?impl_->cutKey(key):impl_->placement?impl_->placementKey(key):impl_->quoteKey(key);};
    connect(impl_->quoteAction,&QAction::toggled,this,[this](bool checked){if(checked)impl_->cancelPlacement();impl_->resetQuote();impl_->view->setCursor(checked?Qt::CrossCursor:Qt::ArrowCursor);impl_->refreshNotable();if(checked)impl_->quoteOverlay();});
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
        else{menu.addAction(findChild<QAction *>("drawingEditViewAction"));menu.addAction(findChild<QAction *>("drawingAlignViewAction"));menu.addAction(impl_->hiddenAction);menu.addAction(impl_->shadedAction);menu.addAction(impl_->centersAction);menu.addAction(impl_->centerLinesAction);for(auto name:{"drawingDerivedViewAction","drawingTechnicalSectionAction","drawingDetachViewAction","drawingReleaseAlignmentAction","drawingSectionHatchAction","drawingEditSectionLineAction","drawingReverseSectionAction"})menu.addAction(findChild<QAction *>(name));}
        menu.addSeparator();menu.addAction(findChild<QAction *>("drawingDeleteSelectionAction"));
        menu.exec(impl_->tree->viewport()->mapToGlobal(point));});
    for(auto name:{"drawingDetachViewAction","drawingReleaseAlignmentAction","drawingSectionHatchAction","drawingEditSectionLineAction","drawingReverseSectionAction"})toolbar->removeAction(findChild<QAction *>(name));
    impl_->message=new QLabel(this);impl_->message->setWordWrap(false);impl_->message->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);impl_->message->setMinimumWidth(0);workspace->statusBar()->addWidget(impl_->message,1);workspace->statusBar()->setSizeGripEnabled(false);
    const QPair<const char *,const char *> icons[]={{"drawingDerivedViewAction","viewFront"},{"drawingTechnicalSectionAction","section"},{"drawingDetachViewAction","copyFaces"},{"drawingReleaseAlignmentAction","move"},{"drawingSectionHatchAction","section"},{"drawingEditSectionLineAction","section"},{"drawingReverseSectionAction","rebuild"},{"drawingSmartDimensionAction","dimension"},{"drawingEditDimensionAction","edit"},{"drawingUndoAction","undo"},{"drawingRedoAction","redo"},{"drawingAlignViewAction","move"},{"drawingAddProjectionAction","viewFront"},{"drawingAddViewAction","section"},{"drawingEditViewAction","edit"},
        {"drawingEndpointDimensionAction","dimensionAligned"},{"drawingHorizontalDimensionAction","dimension"},{"drawingVerticalDimensionAction","dimensionVertical"},{"drawingLengthDimensionAction","measure"},
        {"drawingPageSettingsAction","drawingPage"},{"drawingHiddenLinesAction","visibility"},{"drawingDeleteSelectionAction","delete"},{"drawingUpdateModelAction","rebuild"},{"drawingFitAction","zoomAll"}};
    for(const auto &entry:icons){auto *a=findChild<QAction *>(entry.first);a->setIcon(commandIcon(entry.second));a->setIconVisibleInMenu(true);}
    const QPair<QAction *,QKeySequence> shortcuts[]={{impl_->undoAction,QKeySequence::Undo},{impl_->redoAction,QKeySequence::Redo},{findChild<QAction *>("drawingDeleteSelectionAction"),QKeySequence::Delete}};
    for(const auto &entry:shortcuts){entry.first->setShortcut(entry.second);entry.first->setShortcutContext(Qt::WidgetWithChildrenShortcut);impl_->view->addAction(entry.first);impl_->tree->addAction(entry.first);}
    impl_->pageContext=defaultDrawingPageContext(impl_->state);impl_->calculate(impl_->state,impl_->model);impl_->resetHistory();impl_->savedSheet=impl_->history[0].sheet;impl_->redraw();setReadOnly(!impl_->state.drawing.version.checkinId.isNull());QTimer::singleShot(0,this,[this]{fitSheet();});
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
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{DrawingSheet next;QByteArray bytes;const auto valid=impl_->insertionState(definition,layout,next,bytes);if(!valid.isEmpty())return valid;
        Impl::ComputedView result;const auto error=impl_->computeView(impl_->sourceFor(layout),definition,layout,impl_->silhouetteCache,result);if(!error.isEmpty())return error;
        return impl_->insertPrepared(std::move(definition),std::move(layout),std::move(result.curves),std::move(result.contours),std::move(result.display));
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::beginViewPlacement(DrawingView definition,DrawingViewLayout layout){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{return impl_->startPlacement(std::move(definition),std::move(layout));}catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::beginDerivedViewPlacement(const QUuid &parent,DrawingProjectionSide side,DrawingProjectionMethod method){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *base=impl_->viewFor(parent);const auto *source=impl_->layoutFor(parent);if(!base||!source||base->kind!=DrawingViewKind::ProjectedEdges)return QStringLiteral("Scegli una vista madre proiettata.");
    DrawingView child;child.sectionReferences=base->sectionReferences;
    DrawingViewLayout layout;layout.viewId=child.id;layout.title=QStringLiteral("Vista derivata %1").arg(impl_->state.views.size()+1);layout.features=source->features;layout.position=source->position;
    layout.relation.parentId=parent;layout.relation.kind=DrawingDerivedKind::Orthographic;layout.relation.side=side;layout.relation.method=method;
    auto next=impl_->state;next.drawing.views.push_back(child);next.views.append(layout);const auto error=synchronizeDrawingViews(next);if(!error.isEmpty())return error;
    return beginViewPlacement(next.drawing.views.back(),next.views.back());
}
QString DrawingSheetEditor::beginSectionPlacement(const QUuid &parent,QPointF start,QPointF end,const QString &label,bool reverse,DrawingSectionStyle style){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *base=impl_->viewFor(parent);const auto *source=impl_->layoutFor(parent);if(!base||!source||base->kind!=DrawingViewKind::ProjectedEdges)return QStringLiteral("Scegli una vista madre proiettata.");
    DrawingView child;child.sectionStyle=style;DrawingViewLayout layout;layout.viewId=child.id;layout.title=style==DrawingSectionStyle::Removed?QStringLiteral("Sezione rimossa"):style==DrawingSectionStyle::Full?QStringLiteral("Sezione completa"):style==DrawingSectionStyle::HalfRight?QStringLiteral("Mezza sezione destra"):QStringLiteral("Mezza sezione sinistra");layout.features=source->features;
    layout.relation.parentId=parent;layout.relation.kind=DrawingDerivedKind::Section;layout.relation.cutStart=start;layout.relation.cutEnd=end;layout.relation.label=label;layout.relation.reverse=reverse;layout.relation.aligned=false;
    auto next=impl_->state;next.drawing.views.push_back(child);next.views.append(layout);const auto error=synchronizeDrawingViews(next);if(!error.isEmpty())return error;
    return beginViewPlacement(next.drawing.views.back(),next.views.back());
}
QString DrawingSheetEditor::beginSectionLine(const QUuid &parent,const QString &label,bool reverse,const QUuid &existing,DrawingSectionStyle style){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *v=impl_->viewFor(parent);if(!v||v->kind!=DrawingViewKind::ProjectedEdges)return QStringLiteral("Seleziona una vista madre proiettata.");
    // Valida identificativo e relazione senza intersecare i corpi o mutare il foglio.
    DrawingSheet next=impl_->state;DrawingView test;test.sectionStyle=style;DrawingViewLayout l;l.viewId=test.id;l.title="Sezione";l.relation.parentId=parent;l.relation.kind=DrawingDerivedKind::Section;l.relation.label=label;l.relation.cutEnd={1,0};
    if(existing.isNull()){next.drawing.views.push_back(test);next.views.append(l);}
    else{const auto *current=impl_->layoutFor(existing);if(!current||current->relation.parentId!=parent||current->relation.kind!=DrawingDerivedKind::Section)return QStringLiteral("Sezione da ridefinire non disponibile.");}
    const auto error=synchronizeDrawingViews(next);if(!error.isEmpty())return error;QByteArray bytes;const auto valid=encodeDrawingSheet(next,bytes);if(!valid.isEmpty())return valid;
    impl_->quoteAction->setChecked(false);impl_->cancelPlacement();impl_->resetQuote();auto cut=std::make_unique<Impl::CutPlacement>();cut->parent=parent;cut->existing=existing;cut->label=label;cut->reverse=reverse;cut->style=existing.isNull()?style:impl_->viewFor(existing)->sectionStyle;
    cut->item=impl_->scene->addPath({},QPen(QColor("#1469c0"),.3,Qt::DashLine));cut->item->setZValue(50);cut->item->setData(kindRole,"cuttingPreview");cut->item->setAcceptedMouseButtons(Qt::NoButton);impl_->cut=std::move(cut);impl_->view->setCursor(Qt::CrossCursor);impl_->view->setFocus();impl_->say(drawingHalfSection(impl_->cut->existing.isNull()?test:*impl_->viewFor(existing))?QStringLiteral("Indica il punto dell’asse della mezza sezione, poi un punto lungo la direzione del taglio. Esc annulla."):QStringLiteral("Primo punto della linea di taglio sulla vista madre. Snap ai punti della vista; Esc annulla."));return {};
}
QString DrawingSheetEditor::setSectionLine(const QUuid &section,QPointF start,QPointF end){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *l=impl_->layoutFor(section);if(!l||l->relation.kind!=DrawingDerivedKind::Section)return QStringLiteral("Seleziona una sezione da linea di taglio.");
    auto next=impl_->state;for(auto &p:next.views)if(p.viewId==section){p.relation.cutStart=start;p.relation.cutEnd=end;}
    return impl_->changeViews(std::move(next),QStringLiteral("Ridefinisci linea di taglio"));
}
bool DrawingSheetEditor::hasSectionLine()const{return bool(impl_->cut);}
QString DrawingSheetEditor::detachView(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *layout=impl_->layoutFor(id);if(!layout||layout->relation.kind!=DrawingDerivedKind::Orthographic)return QStringLiteral("Seleziona una vista ortogonale derivata. Le sezioni mantengono il legame con la linea di taglio.");
    auto next=impl_->state;for(auto &l:next.views)if(l.viewId==id)l.relation={};return impl_->changeViews(std::move(next),QStringLiteral("Sgancia vista"));
}
QString DrawingSheetEditor::setViewAlignment(const QUuid &id,bool aligned){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *l=impl_->layoutFor(id);if(!l||l->relation.kind!=DrawingDerivedKind::Orthographic)return QStringLiteral("Seleziona una vista derivata ortogonale.");
    auto next=impl_->state;for(auto &p:next.views)if(p.viewId==id)p.relation.aligned=aligned;return impl_->applyLayout(std::move(next),QStringLiteral("Allineamento vista derivata"));
}
QString DrawingSheetEditor::setSectionHatch(const QUuid &id,bool visible){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *l=impl_->layoutFor(id);const auto *v=impl_->viewFor(id);if(!l||!v||v->kind!=DrawingViewKind::Section||(l->relation.kind!=DrawingDerivedKind::Section&&v->sectionStyle==DrawingSectionStyle::Removed))return QStringLiteral("Seleziona una sezione tecnica.");
    auto next=impl_->state;for(auto &p:next.views)if(p.viewId==id)p.relation.hatch=visible;return impl_->applyLayout(std::move(next),QStringLiteral("Tratteggio sezione"));
}
QString DrawingSheetEditor::setSectionStyle(const QUuid &id,DrawingSectionStyle style){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *v=impl_->viewFor(id);if(!v||v->kind!=DrawingViewKind::Section)return QStringLiteral("Seleziona una sezione.");
    if(style==DrawingSectionStyle::Removed)for(const auto &d:impl_->state.drawing.dimensions)if(d.viewId==id){if(d.section.kind==DrawingCurveKind::ProjectedEdge)return QStringLiteral("La sezione contiene quote sui bordi proiettati: eliminarle prima di passare a sezione rimossa.");for(const auto &r:d.references)if(r.curve.kind==DrawingCurveKind::ProjectedEdge)return QStringLiteral("La sezione contiene quote sui bordi proiettati: eliminarle prima di passare a sezione rimossa.");}
    auto next=impl_->state;for(auto &v:next.drawing.views)if(v.id==id)v.sectionStyle=style;if(style==DrawingSectionStyle::Removed)for(auto &l:next.views)if(l.viewId==id)l.showMesh=false;return impl_->changeViews(std::move(next),QStringLiteral("Presentazione sezione"));
}
QString DrawingSheetEditor::reverseSection(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *l=impl_->layoutFor(id);if(!l||l->relation.kind!=DrawingDerivedKind::Section)return QStringLiteral("Seleziona una sezione da linea di taglio.");
    auto next=impl_->state;for(auto &p:next.views)if(p.viewId==id)p.relation.reverse=!p.relation.reverse;return impl_->changeViews(std::move(next),QStringLiteral("Inverti sezione"));
}
bool DrawingSheetEditor::hasViewPlacement()const{return bool(impl_->placement);}
void DrawingSheetEditor::cancelViewPlacement(){impl_->cancelPlacement();}
QString DrawingSheetEditor::updateView(DrawingView definition,DrawingViewLayout layout){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
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
    auto next=impl_->state;for(auto &v:next.drawing.views)if(v.id==definition.id)v=definition;
    for(auto &v:next.views)if(v.viewId==definition.id)v=layout;
    return impl_->changeViews(std::move(next),QStringLiteral("Modifica vista e derivate"));
}
QString DrawingSheetEditor::moveView(const QUuid &id,const QPointF &position){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    return impl_->moveViewTo(id,position,QStringLiteral("Sposta vista"));}
QString DrawingSheetEditor::moveDimension(const QUuid &id,const QPointF &position){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
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
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(id==reference||!impl_->viewFor(id)||!impl_->viewFor(reference))return QStringLiteral("Scegliere due viste distinte della tavola.");
    if(alignment!=ViewAlignment::SameRow&&alignment!=ViewAlignment::SameColumn)return QStringLiteral("Direzione di allineamento non valida.");
    try{QPointF center,target;if(!impl_->centerFor(id,center)||!impl_->centerFor(reference,target))return QStringLiteral("Le due viste devono avere curve visibili per allinearne i centri.");
        auto position=impl_->layoutFor(id)->position;if(alignment==ViewAlignment::SameRow)position.ry()+=target.y()-center.y();else position.rx()+=target.x()-center.x();
        return impl_->moveViewTo(id,position,QStringLiteral("Allinea viste"));
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::removeView(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(!impl_->viewFor(id))return QStringLiteral("Vista non trovata.");
    for(const auto &l:impl_->state.views)if(l.relation.parentId==id)return QStringLiteral("La vista ha viste derivate: sganciarle o eliminarle prima di rimuovere la madre.");
    auto next=impl_->state;QSet<QUuid> dimensions;for(const auto &d:next.drawing.dimensions)if(d.viewId==id)dimensions.insert(d.id);
    next.drawing.views.erase(std::remove_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==id;}),next.drawing.views.end());
    next.views.erase(std::remove_if(next.views.begin(),next.views.end(),[&](const auto &p){return p.viewId==id;}),next.views.end());
    next.drawing.dimensions.erase(std::remove_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return dimensions.contains(d.id);}),next.drawing.dimensions.end());
    next.dimensions.erase(std::remove_if(next.dimensions.begin(),next.dimensions.end(),[&](const auto &p){return dimensions.contains(p.dimensionId);}),next.dimensions.end());
    QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
    impl_->state=std::move(next);impl_->curves.erase(id);impl_->silhouettes.erase(id);impl_->visibility.erase(id);impl_->cacheSections();for(auto dimension:dimensions)impl_->results.erase(dimension);
    impl_->record(QStringLiteral("Elimina vista e quote"),std::move(bytes));impl_->redraw();return {};
}
QString DrawingSheetEditor::setHiddenLinesVisible(const QUuid &id,bool visible){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *definition=impl_->viewFor(id);if(!definition||!drawingHasProjection(*definition))return QStringLiteral("Seleziona una vista con proiezione dei bordi.");
    auto next=impl_->state;for(auto &layout:next.views)if(layout.viewId==id)layout.showHiddenLines=visible;const auto error=impl_->applyLayout(std::move(next),visible?QStringLiteral("Mostra linee nascoste"):QStringLiteral("Nascondi linee nascoste"));impl_->refreshHiddenAction();return error;
}
bool DrawingSheetEditor::isComputing()const{return impl_->computing;}
QString DrawingSheetEditor::setShadedFacesVisible(const QUuid &id,bool visible){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *view=impl_->viewFor(id);
    if(!view||!drawingHasProjection(*view))return QStringLiteral("Scegli una vista proiettata o una sezione completa/mezza.");
    auto next=impl_->state;for(auto &layout:next.views)if(layout.viewId==id)layout.showMesh=visible;const auto error=impl_->changeViews(std::move(next),QStringLiteral("Facce ombreggiate"));impl_->refreshHiddenAction();return error;
}
QString DrawingSheetEditor::setCenterMarksVisible(const QUuid &id,bool visible){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(!impl_->viewFor(id))return QStringLiteral("Seleziona una vista della tavola.");
    auto next=impl_->state;for(auto &layout:next.views)if(layout.viewId==id)layout.showCenterMarks=visible;
    const auto error=impl_->applyLayout(std::move(next),QStringLiteral("Visibilita' centri"));impl_->refreshHiddenAction();return error;
}
QString DrawingSheetEditor::setCenterLinesVisible(const QUuid &id,bool visible){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(!impl_->viewFor(id))return QStringLiteral("Seleziona una vista della tavola.");
    auto next=impl_->state;for(auto &layout:next.views)if(layout.viewId==id)layout.showCenterLines=visible;
    const auto error=impl_->applyLayout(std::move(next),QStringLiteral("Visibilita' assi delle coniche"));impl_->refreshHiddenAction();return error;
}
QString DrawingSheetEditor::setRevisionTable(const DrawingRevisionTable &table){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    auto next=impl_->state;next.revisionTable=table;DrawingRevisionTableLayout layout;const auto valid=layoutDrawingRevisionTable(next,impl_->pageContext,layout);if(!valid.isEmpty())return valid;
    return impl_->applyLayout(std::move(next),QStringLiteral("Tabella revisioni"));
}
QString DrawingSheetEditor::setPageSettings(const QString &title,const QSizeF &size,const DrawingTitleBlock &block){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    auto next=impl_->state;next.title=title;next.size=size;next.titleBlock=block;return impl_->applyLayout(std::move(next),QStringLiteral("Formato foglio e cartiglio"));
}
void DrawingSheetEditor::setPageContext(DrawingPageContext context){
    const auto same=[](const DocumentVersion &a,const DocumentVersion &b){return a.documentId==b.documentId&&a.checkinId==b.checkinId;};const auto fallback=defaultDrawingPageContext(impl_->state);
    if(!same(context.drawingVersion,fallback.drawingVersion)||!same(context.modelVersion,fallback.modelVersion))context=fallback;
    impl_->pageContext=std::move(context);for(auto *item:impl_->scene->items())if(auto *page=dynamic_cast<DrawingPageItem *>(item)){page->context=impl_->pageContext;page->update();}
}
DrawingPageContext DrawingSheetEditor::pageContext()const{return impl_->pageContext;}
QString DrawingSheetEditor::exportPdf(const QString &path,DrawingPdfReport &report)const{
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    try{
        const QFileInfo info(path);const auto canonical=info.canonicalFilePath();if(info.suffix().compare("pdf",Qt::CaseInsensitive)!=0||(!canonical.isEmpty()&&QFileInfo(canonical).suffix().compare("pdf",Qt::CaseInsensitive)!=0))return QStringLiteral("La destinazione deve essere un file .pdf distinto dai documenti CAD.");
        if(impl_->model.snapshot.bodies.empty()&&!impl_->state.drawing.views.empty())return QStringLiteral("Modello non disponibile: impossibile esportare le viste.");
        DrawingRevisionTableLayout revisionLayout;const auto tableError=layoutDrawingRevisionTable(impl_->state,impl_->pageContext,revisionLayout);if(!tableError.isEmpty())return tableError;
        QGraphicsScene printable;const int missing=impl_->drawSheet(&printable,false);DrawingPdfReport prepared;
        if(std::any_of(impl_->state.views.cbegin(),impl_->state.views.cend(),[](const auto &v){return v.showMesh;}))prepared.warnings.append(QStringLiteral("Facce ombreggiate raster dalla mesh; bordi e quote restano vettoriali."));
        if(missing){prepared.warnings.append(QStringLiteral("%1 quote da verificare.").arg(missing));auto *label=printable.addSimpleText(QStringLiteral("QUOTE DA VERIFICARE: %1").arg(missing));fontFor(label);label->setBrush(QColor("#c42b32"));label->setPos(14,3);}
        const QRectF paper(QPointF(0,0),impl_->state.size);bool outside=false;for(auto *item:printable.items())if(item->data(kindRole).toString()!=QLatin1String("page")&&item->data(kindRole).toString()!=QLatin1String("paper")&&!paper.adjusted(-.1,-.1,.1,.1).contains(item->sceneBoundingRect()))outside=true;
        if(outside)prepared.warnings.append(QStringLiteral("Parte del disegno e' fuori dal foglio e viene ritagliata nel PDF."));
        for(const auto &[id,paths]:impl_->silhouettes){(void)id;prepared.warnings.append(paths.warnings);}for(const auto &[id,display]:impl_->visibility){(void)id;prepared.warnings.append(display->warnings);}
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return QStringLiteral("Impossibile scrivere il PDF: %1").arg(file.errorString());
        {QPdfWriter writer(&file);writer.setResolution(600);const auto size=impl_->state.size;const QPageSize page(QSizeF(std::min(size.width(),size.height()),std::max(size.width(),size.height())),QPageSize::Millimeter,QStringLiteral("ForgeCAD"),QPageSize::ExactMatch);
            if(!writer.setPageSize(page)||!writer.setPageOrientation(size.width()>=size.height()?QPageLayout::Landscape:QPageLayout::Portrait)||!writer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Millimeter))return QStringLiteral("Formato del PDF non supportato.");
            writer.setTitle(impl_->state.title);writer.setCreator(QStringLiteral("ForgeCAD"));QPainter painter;if(!painter.begin(&writer))return QStringLiteral("Impossibile inizializzare il PDF vettoriale.");painter.setRenderHint(QPainter::Antialiasing);painter.scale(writer.resolution()/25.4,writer.resolution()/25.4);printable.render(&painter,paper,paper,Qt::IgnoreAspectRatio);if(!painter.end())return QStringLiteral("Scrittura del PDF non riuscita.");}
        if(file.error()!=QFileDevice::NoError||file.pos()<100||!file.commit())return QStringLiteral("Impossibile completare il PDF: %1").arg(file.errorString());
        report=std::move(prepared);return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
bool DrawingSheetEditor::canUndo()const{return !impl_->readOnly&&impl_->historyIndex>0;}
bool DrawingSheetEditor::canRedo()const{return !impl_->readOnly&&impl_->historyIndex+1<impl_->history.size();}
QString DrawingSheetEditor::undo(){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    return canUndo()?impl_->restore(impl_->historyIndex-1):QStringLiteral("Nessuna modifica annullabile.");}
QString DrawingSheetEditor::redo(){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    return canRedo()?impl_->restore(impl_->historyIndex+1):QStringLiteral("Nessuna modifica da ripristinare.");}
void DrawingSheetEditor::markSaved(){impl_->savedSheet=impl_->encodedState();}
bool DrawingSheetEditor::isModified()const{return impl_->history[impl_->historyIndex].sheet!=impl_->savedSheet;}
QString DrawingSheetEditor::addDimension(DrawingDimension dimension,const QPointF &position){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{auto next=impl_->state;next.drawing.dimensions.push_back(dimension);next.dimensions.append({dimension.id,position});
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        const auto value=impl_->measured(next,dimension,impl_->curves.at(dimension.viewId));if(value.status!=DrawingDimensionStatus::Ready)return value.message;
        impl_->state=std::move(next);impl_->results[dimension.id]=value;impl_->record(QStringLiteral("Inserisci quota"),std::move(bytes));impl_->redraw();return {};
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}
QString DrawingSheetEditor::updateDimension(DrawingDimension dimension){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
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
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
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
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(model.snapshot.version.documentId!=impl_->state.drawing.model.documentId)return QStringLiteral("La tavola appartiene a un altro modello.");
    auto next=impl_->state;
    try{
        const auto error=retargetDrawingModel(next.drawing,model.snapshot.version);if(!error.isEmpty())return error;
        for(auto &layout:next.views){QVector<quint64> features;for(auto id:layout.features){const auto current=model.replacements.value(id,id);if(!features.contains(current))features.append(current);}layout.features=features;}
        for(auto &dimension:next.drawing.dimensions){dimension.section.featureId=model.replacements.value(dimension.section.featureId,dimension.section.featureId);for(auto &ref:dimension.references)if(!drawingDatum(ref.role))ref.curve.featureId=model.replacements.value(ref.curve.featureId,ref.curve.featureId);}
        impl_->calculate(std::move(next),std::move(model));impl_->cancelPlacement();impl_->pageContext=defaultDrawingPageContext(impl_->state);impl_->resetHistory();impl_->notify();impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}

void DrawingSheetEditor::setChangedCallback(std::function<void(const DrawingSheet &)> callback){impl_->changed=std::move(callback);}
void DrawingSheetEditor::setModelProvider(std::function<DrawingEditorModel()> provider){impl_->provider=std::move(provider);impl_->updateAction->setEnabled(bool(impl_->provider)&&!impl_->readOnly);}
bool DrawingSheetEditor::isReadOnly()const{return impl_->readOnly;}
void DrawingSheetEditor::setReadOnly(bool readOnly){
    if(impl_->computing)return;
    impl_->readOnly=readOnly||!impl_->state.drawing.version.checkinId.isNull();
    if(impl_->readOnly){impl_->cancelPlacement();impl_->quoteAction->setChecked(false);}
    impl_->quoteType->setEnabled(!impl_->readOnly);
    for(const auto &name:{"drawingDerivedViewAction","drawingTechnicalSectionAction","drawingDetachViewAction","drawingReleaseAlignmentAction","drawingSectionHatchAction","drawingEditSectionLineAction","drawingReverseSectionAction","drawingPageSettingsAction","drawingSmartDimensionAction","drawingEditDimensionAction","drawingOriginReferenceAction","drawingAxisXReferenceAction","drawingAxisYReferenceAction","drawingAddProjectionAction","drawingAddViewAction","drawingEditViewAction","drawingAlignViewAction","drawingEndpointDimensionAction","drawingHorizontalDimensionAction","drawingVerticalDimensionAction","drawingLengthDimensionAction","drawingDeleteSelectionAction"})
        findChild<QAction *>(QString::fromLatin1(name))->setEnabled(!impl_->readOnly);
    impl_->refreshHiddenAction();impl_->refreshHistoryActions();impl_->updateAction->setEnabled(!impl_->readOnly&&bool(impl_->provider));
    for(auto *item:impl_->scene->items())if(dynamic_cast<MoveLabel *>(item))item->setFlag(QGraphicsItem::ItemIsMovable,!impl_->readOnly);
}
void DrawingSheetEditor::fitSheet(){impl_->view->fitInView(QRectF(QPointF(-5,-5),impl_->state.size+QSizeF(10,10)),Qt::KeepAspectRatio);}
}
