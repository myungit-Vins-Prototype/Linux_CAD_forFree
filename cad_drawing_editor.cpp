#include "cad_drawing_editor.h"
#include "cad_drawing_annotations.h"
#include "cad_numeric_input.h"
#include <QTabWidget>
#include <QFontComboBox>
#include <QGraphicsTextItem>
#include <QPlainTextEdit>
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
#include <QThreadPool>
#include <QProgressBar>
#include <QElapsedTimer>
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
struct ViewPreviewCanvas final:QGraphicsView{
    using QGraphicsView::QGraphicsView;
    void fit(){if(scene()&&!scene()->sceneRect().isEmpty())fitInView(scene()->sceneRect(),Qt::KeepAspectRatio);}
    void resizeEvent(QResizeEvent *event)override{QGraphicsView::resizeEvent(event);fit();}
};
struct SheetCanvas final:QGraphicsView{
    using QGraphicsView::QGraphicsView;
    std::function<bool(QPointF,Qt::KeyboardModifiers,bool)> pointer;
    std::function<bool(int)> key;
    std::function<bool(QPointF,Qt::KeyboardModifiers,int)> drag;
    void mouseMoveEvent(QMouseEvent *event)override{if(pointer&&pointer(mapToScene(event->position().toPoint()),event->modifiers(),false)){event->accept();return;}if(drag&&drag(mapToScene(event->position().toPoint()),event->modifiers(),1)){event->accept();return;}QGraphicsView::mouseMoveEvent(event);}
    void mousePressEvent(QMouseEvent *event)override{if(event->button()==Qt::RightButton&&key&&key(Qt::Key_Escape)){event->accept();return;}
        if(event->button()==Qt::LeftButton&&pointer&&pointer(mapToScene(event->position().toPoint()),event->modifiers(),true)){event->accept();return;}if(event->button()==Qt::LeftButton&&drag&&drag(mapToScene(event->position().toPoint()),event->modifiers(),0)){event->accept();return;}QGraphicsView::mousePressEvent(event);}
    bool event(QEvent *event)override{if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent *>(event)->key()==Qt::Key_Tab&&key&&key(Qt::Key_Tab)){event->accept();return true;}return QGraphicsView::event(event);}
    void mouseReleaseEvent(QMouseEvent *event)override{if(pointer&&pointer(mapToScene(event->position().toPoint()),event->modifiers(),false)){event->accept();return;}if(event->button()==Qt::LeftButton&&drag&&drag(mapToScene(event->position().toPoint()),event->modifiers(),2)){event->accept();return;}QGraphicsView::mouseReleaseEvent(event);}
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
struct ViewFrame final:QGraphicsRectItem{
    using QGraphicsRectItem::QGraphicsRectItem;
    void paint(QPainter *p,const QStyleOptionGraphicsItem *,QWidget *)override{if(isSelected()){QPen pen(QColor("#1469c0"),1,Qt::DashLine);pen.setCosmetic(true);p->setPen(pen);p->setBrush(Qt::NoBrush);p->drawRect(rect());}}
};
struct AxisItem final:QGraphicsPathItem{
    std::function<void()> activated;
    explicit AxisItem(const QPainterPath &path):QGraphicsPathItem(path){setFlag(ItemIsSelectable);setZValue(3);}
    QRectF boundingRect()const override{return path().boundingRect().adjusted(-.7,-.7,.7,.7);}
    QPainterPath shape()const override{QPainterPathStroker stroke;stroke.setWidth(1.2);return stroke.createStroke(path());}
    void paint(QPainter *p,const QStyleOptionGraphicsItem *,QWidget *)override{QPen pen(isSelected()?QColor("#1469c0"):QColor("#444444"),isSelected()?.4:.18);pen.setDashPattern({32,5,3,5});p->setPen(pen);p->setBrush(Qt::NoBrush);p->drawPath(path());}
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)override{if(activated){activated();e->accept();}else QGraphicsPathItem::mouseDoubleClickEvent(e);}
};
struct DrawingPageItem final:QGraphicsItem{
    DrawingSheet sheet;DrawingPageContext context;
    DrawingPageItem(DrawingSheet page,DrawingPageContext value):sheet(std::move(page)),context(std::move(value)){setZValue(-19);}
    QRectF boundingRect()const override{return QRectF(QPointF(0,0),sheet.size);}
    void paint(QPainter *painter,const QStyleOptionGraphicsItem *,QWidget *)override{paintDrawingPage(*painter,sheet,context);}
};
struct MoveLabel final:QGraphicsSimpleTextItem{
    QPointF pressPosition;
    struct TextRun { QString text; QFont font; QPointF baseline; };
    std::vector<TextRun> runs;QRectF textBounds;
    void format(const DrawingDimensionLabel &t){
        if(!t.stacked())return;
        prepareGeometryChange();const auto main=font();auto small=main;small.setPixelSize(9);
        const QFontMetricsF fm(main),sm(small);const double h=std::max(fm.height(),2*sm.height()),baseline=(h-fm.height())/2+fm.ascent();double x=0;
        const auto append=[&](const QString &s,const QFont &f,double y){runs.push_back({s,f,{x,y}});};
        append(t.leading,main,baseline);x+=fm.horizontalAdvance(t.leading);if(!t.leading.isEmpty())x+=3;
        append(t.upper,small,sm.ascent());append(t.lower,small,sm.height()+sm.ascent());
        x+=std::max(sm.horizontalAdvance(t.upper),sm.horizontalAdvance(t.lower))+3;append(t.trailing,main,baseline);x+=fm.horizontalAdvance(t.trailing);
        textBounds=QRectF(0,0,x,h).adjusted(-1,-1,1,1);
    }
    QRectF boundingRect()const override{return runs.empty()?QGraphicsSimpleTextItem::boundingRect():textBounds;}
    QPainterPath shape()const override{if(runs.empty())return QGraphicsSimpleTextItem::shape();QPainterPath path;path.addRect(textBounds);return path;}
    void paint(QPainter *p,const QStyleOptionGraphicsItem *option,QWidget *widget)override{
        if(runs.empty()){QGraphicsSimpleTextItem::paint(p,option,widget);return;}
        p->setPen(QPen(brush(),0));for(const auto &run:runs){p->setFont(run.font);p->drawText(run.baseline,run.text);}
        if(isSelected()){p->setPen(QPen(QColor("#1469c0"),0,Qt::DashLine));p->setBrush(Qt::NoBrush);p->drawRect(textBounds);}
    }
    std::function<void(QPointF,QPointF)> moved;
    std::function<void()> activated;
    explicit MoveLabel(const QString &text):QGraphicsSimpleTextItem(text){setFlags(ItemIsMovable|ItemIsSelectable);setZValue(8);}
    void mousePressEvent(QGraphicsSceneMouseEvent *event)override{pressPosition=pos();QGraphicsSimpleTextItem::mousePressEvent(event);}
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)override{if(activated){activated();event->accept();}else QGraphicsSimpleTextItem::mouseDoubleClickEvent(event);}
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event)override{
        QGraphicsSimpleTextItem::mouseReleaseEvent(event);if(moved&&pos()!=pressPosition)moved(pressPosition,pos());
    }
};
struct MoveNote final:QGraphicsTextItem{
    QPointF pressPosition;std::function<void(QPointF)> moved;std::function<void()> activated;
    MoveNote(const DrawingAnnotation &note,const QString &text,const DrawingStyle &style={}){configureDrawingAnnotation(*document(),note,text,style);setScale(note.height/12);setFlags(ItemIsMovable|ItemIsSelectable);setZValue(8);}
    void mousePressEvent(QGraphicsSceneMouseEvent *e)override{pressPosition=pos();QGraphicsTextItem::mousePressEvent(e);}
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e)override{QGraphicsTextItem::mouseReleaseEvent(e);if(moved&&pos()!=pressPosition)moved(pos());}
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e)override{if(activated){activated();e->accept();}else QGraphicsTextItem::mouseDoubleClickEvent(e);}
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
void transportAttachment(DrawingAttachment &ref,const DrawingView &before,const DrawingView &after){
    if(!ref.axisId.isNull()||drawingDatum(ref.role))return;
    if(ref.curve.requiresWitness){const auto world=before.plane.toGlobal(Vec3(ref.curve.witness.x(),ref.curve.witness.y(),0));const auto p=after.plane.toLocal(world);ref.curve.witness={p.x(),p.y()};}
    // Gli agganci polari delle coniche sono espressi negli assi della vista.
    // Con il solo ribaltamento del frame devono restare sul medesimo punto fisico.
    if(ref.polar&&std::abs(dot(before.plane.zDir(),after.plane.zDir()))>1-1e-10
        &&(distance(before.plane.xDir(),after.plane.xDir())>1e-12||distance(before.plane.yDir(),after.plane.yDir())>1e-12)){
        const double angle=2*kPi*ref.fraction;const auto ray=after.plane.directionToLocal(std::cos(angle)*before.plane.xDir()+std::sin(angle)*before.plane.yDir());
        double turn=std::atan2(ray.y(),ray.x())/(2*kPi);if(turn<0)turn+=1;ref.fraction=turn;
    }
}
bool sameDimension(const DrawingDimension &a,const DrawingDimension &b){
    if(a.angleSides!=b.angleSides||a.decimals!=b.decimals||a.references.size()!=b.references.size())return false;
    for(std::size_t i=0;i<a.references.size();++i){const auto &x=a.references[i],&y=b.references[i];
        if(x.axisId!=y.axisId||x.clipBoundary!=y.clipBoundary||x.role!=y.role||x.fraction!=y.fraction||x.polar!=y.polar||x.vertexName!=y.vertexName||x.boundaryName!=y.boundaryName||x.supportFace!=y.supportFace||x.curveSide!=y.curveSide||x.anchorSide!=y.anchorSide
            ||x.curve.featureId!=y.curve.featureId||x.curve.kind!=y.curve.kind||x.curve.faceName!=y.curve.faceName||x.curve.edgeName!=y.curve.edgeName||x.curve.requiresWitness!=y.curve.requiresWitness||distance(x.curve.witness,y.curve.witness)!=0)return false;}
    const auto &x=a.section,&y=b.section;
    return a.viewId==b.viewId&&a.measure==b.measure&&x.featureId==y.featureId&&x.kind==y.kind&&x.faceName==y.faceName&&x.edgeName==y.edgeName
        &&x.requiresWitness==y.requiresWitness&&distance(x.witness,y.witness)==0;
}
void fontFor(QGraphicsSimpleTextItem *item,double scale=0.25){QFont font("DejaVu Sans");font.setPixelSize(12);item->setFont(font);item->setScale(scale);}
void fontFor(QGraphicsSimpleTextItem *item,const DrawingStyle &style,double height){item->setFont(drawingFont(style));item->setScale(height/12);}
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
    QAction *hiddenAction=nullptr,*shadedAction=nullptr,*centersAction=nullptr,*centerLinesAction=nullptr,*cylinderAxesAction=nullptr;
    struct AxisCache{DrawingAxis definition;DrawingAxisResult result;};
    std::map<QUuid,AxisCache> axisCache;
    int axisInput=-1;QUuid editingAxis;
    std::optional<DrawingAnnotation> pendingNote;bool noteLeader=false;QUuid editingNote;
    const DrawingAnnotation *noteFor(const QUuid &id)const{for(const auto &n:state.annotations)if(n.id==id)return &n;return nullptr;}
    QUuid selectedNote()const{const auto items=scene->selectedItems();return items.size()==1&&items[0]->data(kindRole).toString()==QLatin1String("annotation")?items[0]->data(idRole).toUuid():QUuid();}
    DrawingResolvedAttachment noteResult(const DrawingAnnotation &note)const{
        const auto found=curves.find(note.viewId);static const std::vector<DrawingCurve> empty;
        return resolveDrawingAnnotation(state,note,model.snapshot,found==curves.end()?empty:found->second);
    }
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
    struct SnapPick{QUuid view;std::size_t index=0;DrawingPointRole role=DrawingPointRole::Curve;double parameter=std::numeric_limits<double>::quiet_NaN();QPointF paper;double gap=0;QUuid axisId{};};
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
    void annotationDialog(const QUuid &id={},bool leader=false){
        if(readOnly)return;
        DrawingAnnotation original;if(!id.isNull()){const auto *n=noteFor(id);if(!n)return;original=*n;}
        QDialog dialog(owner);dialog.setObjectName("drawingAnnotationDialog");dialog.setWindowTitle(id.isNull()?(leader?QStringLiteral("Nuovo richiamo"):QStringLiteral("Nuova nota")):QStringLiteral("Proprieta' annotazione"));auto *form=new QFormLayout(&dialog);
        auto *text=new QPlainTextEdit(original.text,&dialog);text->setObjectName("drawingAnnotationText");text->setMinimumSize(360,130);form->addRow(QStringLiteral("Testo:"),text);
        auto *symbols=new QComboBox(&dialog);symbols->setObjectName("drawingAnnotationSymbols");symbols->addItems({QStringLiteral("Inserisci simbolo…"),QString::fromUtf8("⌀"),QString::fromUtf8("±"),QString::fromUtf8("°"),QString::fromUtf8("×"),QString::fromUtf8("µ"),QString::fromUtf8("≤"),QString::fromUtf8("≥")});form->addRow(QStringLiteral("Simboli:"),symbols);
        connect(symbols,qOverload<int>(&QComboBox::activated),&dialog,[=](int index){if(index>0){text->insertPlainText(symbols->itemText(index));symbols->setCurrentIndex(0);text->setFocus();}});
        auto *width=new NumericInput(&dialog),*height=new NumericInput(&dialog);width->setObjectName("drawingAnnotationWidth");height->setObjectName("drawingAnnotationHeight");width->setRange(5,1000);height->setRange(1,20);for(auto *s:{width,height}){s->setDecimals(2);s->setSuffix(" mm sul foglio");}width->setValue(original.width);height->setValue(original.height);form->addRow(QStringLiteral("Larghezza testo:"),width);form->addRow(QStringLiteral("Altezza testo:"),height);
        auto *info=new QLabel(QStringLiteral("A capo automatico; il testo e' dichiarato dall'autore. Per misure associate usare Quota."),&dialog);info->setWordWrap(true);form->addRow(info);
        bool redefine=false;if(original.reference){auto *status=new QLabel(noteResult(original).message,&dialog);status->setWordWrap(true);form->addRow(status);auto *button=new QPushButton(QStringLiteral("Ridefinisci aggancio"),&dialog);button->setObjectName("drawingAnnotationRedefine");form->addRow(button);connect(button,&QPushButton::clicked,&dialog,[&]{redefine=true;dialog.reject();});}
        auto *error=new QLabel(&dialog);error->setObjectName("drawingAnnotationError");error->setWordWrap(true);form->addRow(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
            auto note=original;note.text=text->toPlainText();note.width=width->value();note.height=height->value();
            if(id.isNull()){auto probe=state;probe.annotations.append(note);QByteArray checked;const auto result=encodeDrawingSheet(probe,checked);error->setText(result);if(!result.isEmpty())return;const auto start=owner->beginAnnotation(leader);error->setText(start);if(!start.isEmpty())return;pendingNote=note;quoteOverlay();}
            else {const auto result=owner->updateAnnotation(note);error->setText(result);if(!result.isEmpty())return;}dialog.accept();
        });dialog.exec();if(redefine)report(owner->beginAnnotation(true,id));
    }
    const DrawingAxis *axisFor(const QUuid &id)const{for(const auto &a:state.axes)if(a.id==id)return &a;return nullptr;}
    DrawingAxisResult cachedAxis(const DrawingAxis &axis){
        const auto cached=axisCache.find(axis.id);if(cached!=axisCache.end()&&cached->second.definition.kind==axis.kind&&sameDimension(drawingAxisSupport(cached->second.definition),drawingAxisSupport(axis)))return cached->second.result;
        const auto *definition=viewFor(axis.viewId);const auto found=curves.find(axis.viewId);DrawingAxisResult result;
        if(definition&&found!=curves.end())result=evaluateDrawingAxis(axis,*definition,model.snapshot,found->second);
        else result={DrawingDimensionStatus::Missing,QStringLiteral("Vista o geometria dell'asse assenti.")};
        axisCache[axis.id]={axis,result};return result;
    }
    QUuid selectedAxis()const{const auto selected=scene->selectedItems();if(selected.size()==1&&selected[0]->data(kindRole).toString()==QLatin1String("axis"))return selected[0]->data(idRole).toUuid();return {};}
    DrawingAttachmentResolver resolverFor(const QUuid &id)const{
        const auto *v=viewFor(id);const auto found=curves.find(id);if(!v||found==curves.end())return {};
        return drawingSheetAttachmentResolver(state,*v,model.snapshot,found->second);
    }
    bool sameAxisDependencies(const DrawingSheet &next,const DrawingDimension &dimension)const{
        for(const auto &ref:dimension.references)if(!ref.axisId.isNull()){
            const auto *old=axisFor(ref.axisId);const DrawingAxis *now=nullptr;for(const auto &a:next.axes)if(a.id==ref.axisId){now=&a;break;}
            if(bool(old)!=bool(now))return false;
            if(old&&(old->kind!=now->kind||!sameDimension(drawingAxisSupport(*old),drawingAxisSupport(*now))))return false;
        }
        return true;
    }
    DrawingAxis candidateAxis()const{DrawingAxis a;if(const auto *old=axisFor(editingAxis))a=*old;a.viewId=quoteView;a.kind=DrawingAxisKind(axisInput);a.references=quoteReferences;return a;}
    void editAxisDialog(const QUuid &id){
        const auto *found=axisFor(id);if(readOnly||!found)return;const auto original=*found;QDialog dialog(owner);dialog.setObjectName("drawingAxisDialog");dialog.setWindowTitle(QStringLiteral("Proprieta' asse"));auto *form=new QFormLayout(&dialog);
        auto *name=new QLineEdit(original.name,&dialog);name->setObjectName("drawingAxisName");name->setMaxLength(128);form->addRow(QStringLiteral("Nome:"),name);
        auto *extension=new NumericInput(&dialog);extension->setObjectName("drawingAxisExtension");extension->setRange(0,1000);extension->setDecimals(2);extension->setValue(original.extension);extension->setSuffix(QStringLiteral(" mm sul foglio"));form->addRow(QStringLiteral("Prolungamento per lato:"),extension);
        auto *visible=new QCheckBox(QStringLiteral("Mostra asse"),&dialog);visible->setObjectName("drawingAxisVisible");visible->setChecked(original.visible);form->addRow(visible);
        const auto result=cachedAxis(original);auto *status=new QLabel(result.status==DrawingDimensionStatus::Ready?QStringLiteral("Riferimenti geometrici associati."):result.message,&dialog);status->setWordWrap(true);form->addRow(status);
        auto *redefine=new QPushButton(QStringLiteral("Ridefinisci con due riferimenti"),&dialog);redefine->setObjectName("drawingAxisRedefine");form->addRow(redefine);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);bool redefining=false;
        connect(redefine,&QPushButton::clicked,&dialog,[&]{redefining=true;dialog.accept();});connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        if(redefining){report(owner->beginAxis(original.kind,original.id));return;}
        auto next=original;next.name=name->text().trimmed();next.extension=extension->value();next.visible=visible->isChecked();report(owner->updateAxis(next));
    }
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
                ComputedView result;
                if(dialogReady&&sameGeometry(dialogReady->definition,definition)&&dialogReady->definition.scale==definition.scale&&dialogReady->layout.features==layout->features
                   &&sameSource(dialogReady->source,sourceFor(*layout))&&(!layout->showMesh||dialogReady->result.display->meshPrepared)){
                    result=dialogReady->result;seeds=result.cache;
                }else {const auto error=computeView(sourceFor(*layout),definition,*layout,seeds,result,same?&cached->second:nullptr,contourSame?&contour->second:nullptr);if(!error.isEmpty())return error;}
                if(!same)changedViews.insert(definition.id);
                prepared[definition.id]=std::move(result.curves);preparedSilhouettes[definition.id]=std::move(result.contours);preparedVisibility[definition.id]=std::move(result.display);
            }
            std::map<QUuid,DrawingDimensionResult> measured;
            for(const auto &dimension:next.drawing.dimensions){const auto found=results.find(dimension.id);
                const auto old=std::find_if(state.drawing.dimensions.begin(),state.drawing.dimensions.end(),[&](const auto &d){return d.id==dimension.id;});
                measured[dimension.id]=!changedViews.contains(dimension.viewId)&&sameAxisDependencies(next,dimension)&&old!=state.drawing.dimensions.end()&&sameDimension(*old,dimension)&&found!=results.end()
                    ?found->second:this->measured(next,dimension,prepared.at(dimension.viewId));}
            for(auto it=axisCache.begin();it!=axisCache.end();)if(changedViews.contains(it->second.definition.viewId))it=axisCache.erase(it);else ++it;
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
        for(auto &n:next.annotations)if(n.reference){const auto *old=layoutFor(n.viewId);const auto now=std::find_if(next.views.begin(),next.views.end(),[&](const auto &v){return v.viewId==n.viewId;});if(old&&now!=next.views.end())n.position+=now->position-old->position;}
        return {};
    }
    QString changeViews(DrawingSheet next,const QString &label){
        if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
        const auto sync=propagateLayout(next);if(!sync.isEmpty())return sync;
        for(auto &d:next.drawing.dimensions){const auto *old=viewFor(d.viewId);const auto current=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});
            if(!old||current==next.drawing.views.end()||sameGeometry(*old,*current))continue;
            const auto transform=[&](DrawingSectionReference &ref){if(!ref.requiresWitness)return;const auto world=old->plane.toGlobal(Vec3(ref.witness.x(),ref.witness.y(),0));const auto local=current->plane.toLocal(world);ref.witness=Vec2(local.x(),local.y());};
            transform(d.section);for(auto &ref:d.references)transportAttachment(ref,*old,*current);
        }
        for(const auto &d:next.drawing.dimensions){const auto v=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});
            if(v==next.drawing.views.end()||v->kind!=DrawingViewKind::ProjectedEdges||v->sectionReferences)continue;
            if(d.section.featureId&&d.section.kind==DrawingCurveKind::Section)return QStringLiteral("Sono presenti quote di sezione: conservare i riferimenti del piano.");
            for(const auto &ref:d.references)if(ref.axisId.isNull()&&!drawingDatum(ref.role)&&ref.curve.kind==DrawingCurveKind::Section)return QStringLiteral("Sono presenti quote di sezione: conservare i riferimenti del piano.");
        }
        for(auto &axis:next.axes){const auto *old=viewFor(axis.viewId);const auto current=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==axis.viewId;});if(!old||current==next.drawing.views.end()||sameGeometry(*old,*current))continue;
            for(auto &ref:axis.references)transportAttachment(ref,*old,*current);}
        for(auto &n:next.annotations)if(n.reference){auto &ref=*n.reference;const auto *old=viewFor(n.viewId);const auto current=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==n.viewId;});
            if(current!=next.drawing.views.end()&&ref.axisId.isNull()&&ref.curve.kind==DrawingCurveKind::Section&&current->kind==DrawingViewKind::ProjectedEdges&&!current->sectionReferences)return QStringLiteral("Sono presenti richiami di sezione: conservare i riferimenti del piano.");
            if(old&&current!=next.drawing.views.end()&&!sameGeometry(*old,*current))transportAttachment(ref,*old,*current);}
        QByteArray bytes;const auto valid=encodeDrawingSheet(next,bytes);if(!valid.isEmpty())return valid;
        const auto applied=replaceState(std::move(next));if(!applied.isEmpty())return applied;
        record(label,std::move(bytes));redraw();return {};
    }
    QString applyLayout(DrawingSheet next,const QString &label){
        if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
        const auto sync=propagateLayout(next);if(!sync.isEmpty())return sync;
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        if(bytes==history[historyIndex].sheet)return {};
        auto nextResults=results;
        for(const auto &d:next.drawing.dimensions)if(!sameAxisDependencies(next,d))nextResults[d.id]=measured(next,d,curves.at(d.viewId));
        state=std::move(next);results=std::move(nextResults);record(label,std::move(bytes));queueRedraw();return {};
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
        return evaluateDrawingAttachments(d,*definition,model.snapshot,pieces,drawingSheetAttachmentResolver(sheet,*definition,model.snapshot,pieces));
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
        if(cylinderAxesAction){QSignalBlocker lock(cylinderAxesAction);cylinderAxesAction->setEnabled(!readOnly&&definition&&drawingHasProjection(*definition));cylinderAxesAction->setChecked(layout&&layout->showCylinderAxes);}
        if(auto *action=owner->findChild<QAction *>("drawingEditAnnotationAction"))action->setEnabled(!readOnly&&!selectedNote().isNull());
        if(auto *action=owner->findChild<QAction *>("drawingEditAxisAction"))action->setEnabled(!readOnly&&!selectedAxis().isNull());
        if(shadedAction){QSignalBlocker lock(shadedAction);shadedAction->setEnabled(!readOnly&&definition&&drawingHasProjection(*definition));shadedAction->setChecked(layout&&layout->showMesh);}
        if(auto *a=owner->findChild<QAction *>("drawingDetachViewAction"))a->setEnabled(!readOnly&&layout&&layout->relation.kind==DrawingDerivedKind::Orthographic);
        if(auto *a=owner->findChild<QAction *>("drawingReleaseAlignmentAction")){a->setEnabled(!readOnly&&layout&&layout->relation.kind==DrawingDerivedKind::Orthographic);a->setText(layout&&layout->relation.aligned?QStringLiteral("Libera allineamento"):QStringLiteral("Ripristina allineamento"));}
        for(auto name:{"drawingSectionHatchAction","drawingEditSectionLineAction","drawingAlignSectionOrientationAction","drawingReverseSectionAction"})if(auto *a=owner->findChild<QAction *>(name))a->setEnabled(!readOnly&&layout&&layout->relation.kind==DrawingDerivedKind::Section);
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
        if(selected.size()==1&&(selected.front()->data(kindRole).toString()==QLatin1String("dimension")||selected.front()->data(kindRole).toString()==QLatin1String("axis")||selected.front()->data(kindRole).toString()==QLatin1String("annotation"))){
            const auto id=selected.front()->data(idRole).toUuid();selectTreeItem(selected.front()->data(kindRole).toString(),id);
            const auto result=results.find(id);if(result!=results.end()&&result->second.status!=DrawingDimensionStatus::Ready)say(result->second.message);
        }else selectTreeItem("view",selectedView());
        refreshHiddenAction();
    }
    void selectionFromTree(){
        if(rebuilding||syncingSelection)return;
        syncingSelection=true;scene->clearSelection();const auto *current=tree->currentItem();
        if(current){const auto kind=current->data(0,Qt::UserRole).toString();const auto id=current->data(0,Qt::UserRole+1).toUuid();
            for(auto *item:scene->items())if((kind==QLatin1String("view")&&item->data(kindRole).toString()==QLatin1String("view")&&item->data(viewRole).toUuid()==id)
                ||((kind==QLatin1String("dimension")||kind==QLatin1String("axis")||kind==QLatin1String("annotation"))&&item->data(kindRole).toString()==kind&&item->data(idRole).toUuid()==id)){
                item->setSelected(true);view->ensureVisible(item,40,40);}}
        syncingSelection=false;selectionFromScene();
    }
    void refreshTree(){
        const auto *current=tree->currentItem();const auto kind=current?current->data(0,Qt::UserRole).toString():QString();
        const auto selected=current?current->data(0,Qt::UserRole+1).toUuid():QUuid();QSignalBlocker block(tree);tree->clear();
        auto *root=new QTreeWidgetItem(tree,{state.title});root->setIcon(0,commandIcon("drawingPage"));root->setExpanded(true);QHash<QUuid,QTreeWidgetItem *> nodes;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);if(!layout)continue;
            auto *item=new QTreeWidgetItem(root,{layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4)});
            item->setIcon(0,commandIcon(definition.kind==DrawingViewKind::Section?(layout->relation.parentId.isNull()?"drawingSection":"drawingTechnicalSection"):(layout->relation.parentId.isNull()?"drawingView":"drawingDerivedView")));
            item->setData(0,Qt::UserRole,"view");item->setData(0,Qt::UserRole+1,definition.id);item->setExpanded(true);nodes.insert(definition.id,item);
            item->setToolTip(0,QStringLiteral("Doppio clic per modificare la vista."));
            if(!layout->relation.parentId.isNull())item->setToolTip(0,layout->relation.kind==DrawingDerivedKind::Section?QStringLiteral("Sezione associata alla linea di taglio della madre; piano, scala e sorgenti seguono la madre."):QStringLiteral("Scala, orientamento e sorgenti ereditati dalla madre. Sgancia per renderla indipendente."));
            const auto contour=silhouettes.find(definition.id);if(contour!=silhouettes.end()&&!contour->second.warnings.empty()){item->setForeground(0,QColor("#ad6d00"));item->setToolTip(0,item->toolTip(0)+QStringLiteral("\n")+contour->second.warnings.join(QStringLiteral("\n")));}
            for(const auto &axis:state.axes)if(axis.viewId==definition.id){const auto result=cachedAxis(axis);auto *child=new QTreeWidgetItem(item,{axis.name+(axis.visible?QString():QStringLiteral(" (nascosto)"))});child->setIcon(0,commandIcon(axis.kind==DrawingAxisKind::ThroughPoints?"drawingAxisThroughPoints":"drawingAxisBetweenLines"));child->setData(0,Qt::UserRole,"axis");child->setData(0,Qt::UserRole+1,axis.id);if(result.status!=DrawingDimensionStatus::Ready){child->setForeground(0,QColor("#c42b32"));child->setText(0,axis.name+QStringLiteral(" · Da verificare"));child->setToolTip(0,result.message);}}
            for(const auto &dimension:state.drawing.dimensions)if(dimension.viewId==definition.id){const auto found=results.find(dimension.id);
                const bool ready=found!=results.end()&&found->second.status==DrawingDimensionStatus::Ready;
                const auto measure=drawingDimensionName(dimension.measure);
                const auto layout=std::find_if(state.dimensions.begin(),state.dimensions.end(),[&](const auto &p){return p.dimensionId==dimension.id;});
                const auto formatted=ready?drawingDimensionLabel(dimension,found->second.value,layout->presentation):DrawingDimensionLabel{};
                const auto text=ready?formatted.summary:QStringLiteral("Da verificare");
                auto *child=new QTreeWidgetItem(item,{measure+QStringLiteral(" · ")+text});
                child->setIcon(0,commandIcon("dimension"));child->setData(0,Qt::UserRole,"dimension");child->setData(0,Qt::UserRole+1,dimension.id);
                if(!ready||!formatted.problem.isEmpty()){child->setForeground(0,QColor("#d34b4b"));child->setToolTip(0,ready?formatted.problem:found!=results.end()?found->second.message:QString());}}
        }
        for(const auto &n:state.annotations){const auto result=noteResult(n);auto *item=new QTreeWidgetItem(n.reference?nodes.value(n.viewId,root):root,{n.text.section('\n',0,0).left(60)});item->setData(0,Qt::UserRole,"annotation");item->setData(0,Qt::UserRole+1,n.id);item->setIcon(0,commandIcon(n.reference?"drawingLeader":"drawingNote"));item->setToolTip(0,n.text);if(result.status!=DrawingDimensionStatus::Ready){item->setForeground(0,QColor("#c42b32"));item->setText(0,item->text(0)+QStringLiteral(" · Da verificare"));item->setToolTip(0,result.message);}}
        for(const auto &l:state.views)if(!l.relation.parentId.isNull()&&nodes.contains(l.viewId)&&nodes.contains(l.relation.parentId)){
            auto *item=nodes.value(l.viewId);auto *oldParent=item->parent();oldParent->takeChild(oldParent->indexOfChild(item));nodes.value(l.relation.parentId)->addChild(item);
        }
        selectTreeItem(kind,selected);
    }
    void editSelectedView(){if(readOnly)return;const auto id=selectedView();if(id.isNull()){say(QStringLiteral("Seleziona una vista o una sua quota nell'albero o sul foglio."));return;}
        const auto *definition=viewFor(id);if(definition){if(!layoutFor(id)->relation.parentId.isNull())derivedProperties(id);else addViewDialog(definition->kind,id);}}
    struct ComputedView {std::vector<DrawingCurve> curves;DrawingSilhouetteResult contours;std::shared_ptr<DrawingViewVisibility> display;DrawingSilhouetteCache cache;};
    static ComputedView buildView(const DrawingModelSnapshot &source,const DrawingView &definition,const DrawingViewLayout &layout,
        DrawingSilhouetteCache seeds,const std::function<void(int)> &phase,const std::vector<DrawingCurve> *reuseCurves=nullptr,const DrawingSilhouetteResult *reuseContours=nullptr){
        ComputedView result;result.cache=std::move(seeds);const auto ok=[](const QString &message){if(!message.isEmpty())throw std::runtime_error(message.toStdString());};
        if(reuseCurves)result.curves=*reuseCurves;else ok(drawingViewCurves(source,definition,result.curves));phase(1);
        if(reuseContours)result.contours=*reuseContours;else ok(drawingViewSilhouettes(source,definition,result.contours,result.cache));phase(2);
        DrawingSectionHatch hatch;ok(drawingSectionHatch(source,definition,result.curves,hatch));phase(3);
        result.display=std::make_shared<DrawingViewVisibility>();ok(drawingViewVisibility(source,definition,result.curves,result.contours,result.cache,*result.display,&hatch.material,layout.showMesh));
        result.display->sectionHatch=std::move(hatch.lines);result.display->sectionMaterial=std::move(hatch.material);result.display->warnings.append(hatch.warnings);phase(4);phase(5);return result;
    }
    struct PreparedView {DrawingView definition;DrawingViewLayout layout;DrawingModelSnapshot source;ComputedView result;};
    std::optional<PreparedView> dialogReady;
    static bool sameSource(const DrawingModelSnapshot &a,const DrawingModelSnapshot &b){
        if(a.version.documentId!=b.version.documentId||a.version.checkinId!=b.version.checkinId||a.bodies.size()!=b.bodies.size())return false;
        for(std::size_t i=0;i<a.bodies.size();++i)if(a.bodies[i].featureId!=b.bodies[i].featureId||a.bodies[i].body!=b.bodies[i].body||a.bodies[i].names!=b.bodies[i].names)return false;
        return true;
    }
    QString computeView(const DrawingModelSnapshot &source,const DrawingView &definition,const DrawingViewLayout &layout,
                        DrawingSilhouetteCache &cache,ComputedView &output,const std::vector<DrawingCurve> *reuseCurves=nullptr,const DrawingSilhouetteResult *reuseContours=nullptr){
        auto oldCurves=reuseCurves?*reuseCurves:std::vector<DrawingCurve>{};auto oldContours=reuseContours?*reuseContours:DrawingSilhouetteResult{};const bool keepCurves=reuseCurves,keepContours=reuseContours;
        const auto error=runDrawingComputation(owner,computing,QStringLiteral("Calcolo vista: %1").arg(layout.title),output,
            [source,definition,layout,seeds=cache,oldCurves=std::move(oldCurves),oldContours=std::move(oldContours),keepCurves,keepContours](auto phase){
                return buildView(source,definition,layout,seeds,phase,keepCurves?&oldCurves:nullptr,keepContours?&oldContours:nullptr);
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
        for(const auto &d:nextState.drawing.dimensions){const auto v=std::find_if(nextState.drawing.views.begin(),nextState.drawing.views.end(),[&](const auto &v){return v.id==d.viewId;});measured[d.id]=d.references.empty()?evaluateDrawingDimension(nextState.drawing,nextModel.snapshot,d.id):evaluateDrawingAttachments(d,*v,nextModel.snapshot,prepared.at(d.viewId),drawingSheetAttachmentResolver(nextState,*v,nextModel.snapshot,prepared.at(d.viewId)));}
        axisCache.clear();state=std::move(nextState);model=std::move(nextModel);silhouetteCache=std::move(seeds);curves=std::move(prepared);silhouettes=std::move(preparedSilhouettes);visibility=std::move(preparedVisibility);results=std::move(measured);cacheSections();
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
        return startPreparedPlacement(std::move(definition),std::move(layout),std::move(result));
    }
    QString startPreparedPlacement(DrawingView definition,DrawingViewLayout layout,ComputedView result){
        if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
        DrawingSheet next;QByteArray bytes;const auto valid=insertionState(definition,layout,next,bytes);if(!valid.isEmpty())return valid;
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
        if(layout.showCylinderAxes){const auto &path=layout.showHiddenLines?prepared->visibility->allCylinderAxes:prepared->visibility->visibleCylinderAxes;auto *item=new QGraphicsPathItem(path.translated(-prepared->center),group);QPen pen(QColor("#1469c0"),.18);pen.setDashPattern({32,5,3,5});item->setPen(pen);item->setAcceptedMouseButtons(Qt::NoButton);}
        auto *label=new QGraphicsSimpleTextItem(layout.title,group);fontFor(label,state.style,state.style.viewTitleHeight);label->setPos(layout.titleOffset?*layout.titleOffset-prepared->center:bounds.bottomLeft()-prepared->center+QPointF(0,state.style.titleGap));label->setBrush(QColor("#1469c0"));label->setAcceptedMouseButtons(Qt::NoButton);
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
    struct ViewDrag {
        QUuid id;bool title=false,moved=false;QPointF press,origin,target;QByteArray base;
        struct Item{QGraphicsItem *item;QPointF position;QUuid owner;};std::vector<Item> items;
        QGraphicsPathItem *guides=nullptr;QSizeF titleSize;
    };
    std::unique_ptr<ViewDrag> viewDrag;
    QRectF viewBounds(const QUuid &id)const{
        const auto *layout=layoutFor(id);const auto found=visibility.find(id);if(!layout||found==visibility.end())return {};
        QRectF box;bool first=true;const auto add=[&](const DrawingVisiblePath &p){const auto path=joinedPaths(p.visible,p.hidden);if(path.isEmpty())return;const auto b=path.boundingRect();if(first){box=b;first=false;}else box=QRectF(QPointF(std::min(box.left(),b.left()),std::min(box.top(),b.top())),QPointF(std::max(box.right(),b.right()),std::max(box.bottom(),b.bottom())));};
        for(const auto &p:found->second->curves)add(p);
        for(const auto &p:found->second->silhouettes)add(p);
        return box.translated(layout->position);
    }
    void cancelViewDrag(){
        if(!viewDrag)return;
        for(const auto &p:viewDrag->items)p.item->setPos(p.position);
        delete viewDrag->guides;viewDrag.reset();view->setCursor(Qt::ArrowCursor);
    }
    QPointF snapViewTitle(QPointF point,Qt::KeyboardModifiers modifiers){
        if(!viewDrag||!viewDrag->title)return point;
        QPainterPath guides;const double zoom=std::max(.01,std::abs(view->transform().m11()));const auto box=viewBounds(viewDrag->id);const double gap=state.style.titleGap,w=viewDrag->titleSize.width(),h=viewDrag->titleSize.height();
        const std::vector<QPointF> candidates={{box.left(),box.top()-gap-h},{box.center().x()-w/2,box.top()-gap-h},{box.right()-w,box.top()-gap-h},{box.right()+gap,box.center().y()-h/2},{box.right()-w,box.bottom()+gap},{box.center().x()-w/2,box.bottom()+gap},{box.left(),box.bottom()+gap},{box.left()-gap-w,box.center().y()-h/2}};
        double best=9;int index=-1;
        if(state.style.titleSnaps&&!(modifiers&Qt::AltModifier))for(std::size_t i=0;i<candidates.size();++i){const auto p=candidates[i];guides.addEllipse(p,2.5/zoom,2.5/zoom);const double distance=QLineF(point,p).length()*zoom;if(distance<best){best=distance;index=int(i);}}
        if(index>=0){point=candidates[std::size_t(index)];guides.addRect(QRectF(point,viewDrag->titleSize));}
        viewDrag->guides->setPath(guides);viewDrag->guides->setData(indexRole,index);return point;
    }
    bool viewDragPointer(QPointF point,Qt::KeyboardModifiers modifiers,int phase){
        if(phase==0){
            if(readOnly||computing||placement||cut||quoteAction->isChecked()||(modifiers&Qt::ControlModifier))return false;
            cancelViewDrag();QGraphicsItem *picked=nullptr;
            for(auto *item:scene->items(point)){const auto kind=item->data(kindRole).toString();if(kind=="dimension"||kind=="annotation"||kind=="axis")return false;
                if(kind=="view"||kind=="projection"||kind=="section"||kind=="silhouette"||kind=="viewFrame"||kind=="viewShading"){picked=item;break;}}
            if(!picked)return false;
            auto drag=std::make_unique<ViewDrag>();drag->id=picked->data(viewRole).toUuid();if(!layoutFor(drag->id))return false;drag->title=picked->data(kindRole).toString()=="view";drag->press=point;drag->base=history[historyIndex].sheet;drag->origin=drag->title?picked->pos():layoutFor(drag->id)->position;drag->target=drag->origin;drag->titleSize=picked->sceneBoundingRect().size();
            scene->clearSelection();picked->setSelected(true);selectTreeItem("view",drag->id);
            for(auto *item:scene->items()){const auto id=item->data(viewRole).toUuid();if(id.isNull()||(drag->title&&item!=picked))continue;drag->items.push_back({item,item->pos(),id});}
            auto *guides=scene->addPath({},QPen(QColor("#1469c0"),0,Qt::DashLine));guides->setData(kindRole,"viewMovePreview");guides->setData(viewRole,drag->id);guides->setZValue(40);guides->setAcceptedMouseButtons(Qt::NoButton);drag->guides=guides;viewDrag=std::move(drag);view->setFocus();return true;
        }
        if(!viewDrag)return false;
        if(readOnly||computing||viewDrag->base!=history[historyIndex].sheet){cancelViewDrag();return true;}
        if(QLineF(point,viewDrag->press).length()*std::abs(view->transform().m11())<4&&!viewDrag->moved){if(phase==2)cancelViewDrag();return true;}
        viewDrag->moved=true;view->setCursor(Qt::ClosedHandCursor);viewDrag->target=viewDrag->origin+point-viewDrag->press;
        if(viewDrag->title){viewDrag->target=snapViewTitle(viewDrag->target,modifiers);for(const auto &p:viewDrag->items)p.item->setPos(p.position+viewDrag->target-viewDrag->origin);}
        else {
            auto next=state;for(auto &v:next.views)if(v.viewId==viewDrag->id)v.position=viewDrag->target;
            const auto error=propagateLayout(next);if(!error.isEmpty()){say(error);cancelViewDrag();return true;}
            QHash<QUuid,QPointF> deltas;for(const auto &v:next.views)deltas[v.viewId]=v.position-layoutFor(v.viewId)->position;
            for(const auto &p:viewDrag->items)p.item->setPos(p.position+deltas.value(p.owner));
            viewDrag->target=layoutFor(viewDrag->id)->position+deltas.value(viewDrag->id);
            QPainterPath outline;outline.addRect(viewBounds(viewDrag->id).translated(deltas.value(viewDrag->id)).adjusted(-1,-1,1,1));viewDrag->guides->setPath(outline);
        }
        if(phase==2){const auto id=viewDrag->id;const auto target=viewDrag->target;const bool title=viewDrag->title;cancelViewDrag();const auto error=title?owner->moveViewTitle(id,target):owner->moveView(id,target);if(!error.isEmpty())say(error);}
        return true;
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
                if(!display->sectionMaterial.isEmpty()){auto *cap=scene->addPath(display->sectionMaterial.translated(layout->position),QPen(Qt::NoPen),Qt::white);cap->setZValue(-2);cap->setData(kindRole,"sectionMaterial");cap->setData(viewRole,definition.id);cap->setAcceptedMouseButtons(Qt::NoButton);}
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
            if(layout->showCylinderAxes){const auto &path=layout->showHiddenLines?display->allCylinderAxes:display->visibleCylinderAxes;if(!path.isEmpty()){QPen pen(QColor("#444444"),.18);pen.setDashPattern({32,5,3,5});auto *item=scene->addPath(path,pen);item->setPos(layout->position);item->setZValue(3);item->setData(kindRole,"cylinderAxes");item->setData(viewRole,definition.id);item->setAcceptedMouseButtons(Qt::NoButton);}}
            int axisRow=0;for(const auto &axis:state.axes)if(axis.viewId==definition.id){const auto result=cachedAxis(axis);
                if(!axis.visible){if(interactive){QPainterPath placeholder;placeholder.addRect(-.1,-.1,.2,.2);auto *item=scene->addPath(placeholder);item->setPos(layout->position);item->setOpacity(0);item->setFlag(QGraphicsItem::ItemIsSelectable);item->setData(kindRole,"axis");item->setData(viewRole,definition.id);item->setData(idRole,axis.id);item->setAcceptedMouseButtons(Qt::NoButton);}continue;}
                if(result.status==DrawingDimensionStatus::Ready){auto *item=new AxisItem(drawingAxisPath(result,definition,axis.extension));item->setPos(layout->position);item->setData(kindRole,"axis");item->setData(viewRole,definition.id);item->setData(idRole,axis.id);item->setToolTip(axis.name+QStringLiteral(" · Doppio clic per modificare."));if(interactive)item->activated=[this,id=axis.id]{QTimer::singleShot(0,owner,[this,id]{editAxisDialog(id);});};else item->setFlag(QGraphicsItem::ItemIsSelectable,false);scene->addItem(item);}
                else{auto *warning=scene->addSimpleText(axis.name+QStringLiteral(": da verificare"));fontFor(warning);warning->setBrush(QColor("#c42b32"));warning->setPos(layout->position+QPointF(0,-8-5*axisRow++));warning->setToolTip(result.message);warning->setData(kindRole,"axis");warning->setData(viewRole,definition.id);warning->setData(idRole,axis.id);warning->setFlag(QGraphicsItem::ItemIsSelectable,interactive);}
            }
            if(drawingHalfSection(definition)&&!bounds.isNull()){
                QPen pen(QColor("#555555"),.18);pen.setDashPattern({10,2,2,2});auto *axis=scene->addLine(layout->position.x(),bounds.top()-3,layout->position.x(),bounds.bottom()+3,pen);axis->setData(kindRole,"sectionAxis");axis->setData(viewRole,definition.id);axis->setAcceptedMouseButtons(Qt::NoButton);axis->setZValue(3);
            }
            auto *label=new MoveLabel((layout->relation.kind==DrawingDerivedKind::Section?layout->relation.label+QStringLiteral("–")+layout->relation.label+QStringLiteral(" · "):QString())+layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4));fontFor(label,state.style,state.style.viewTitleHeight);label->setBrush(QColor("#35495c"));
            label->setPos(layout->titleOffset?layout->position+*layout->titleOffset:bounds.isNull()?layout->position:bounds.bottomLeft()+QPointF(0,state.style.titleGap));label->setData(kindRole,"view");label->setData(viewRole,definition.id);
            label->setFlag(QGraphicsItem::ItemIsMovable,false);
            label->setToolTip(QStringLiteral("Trascina il nome per spostarlo separatamente; Alt esclude gli snap. Trascina la geometria per spostare la vista. Doppio clic per modificarla."));
            label->activated=[this,id=definition.id]{QTimer::singleShot(0,owner,[this,id]{const auto *v=viewFor(id);if(v)addViewDialog(v->kind,id);});};
            const auto id=definition.id;label->moved=[this,id](QPointF before,QPointF after){const auto *layout=layoutFor(id);if(!layout)return;
                Q_UNUSED(before);const auto error=owner->moveViewTitle(id,after);if(!error.isEmpty()){say(error);queueRedraw();}};scene->addItem(label);
            if(interactive){auto *frame=new ViewFrame(bounds.adjusted(-1,-1,1,1));frame->setZValue(-4);frame->setFlag(QGraphicsItem::ItemIsSelectable);frame->setData(kindRole,"viewFrame");frame->setData(viewRole,definition.id);frame->setToolTip(QStringLiteral("Trascina per spostare la vista; Esc annulla."));scene->addItem(frame);}
        }
        for(const auto &dimension:state.drawing.dimensions){
            const auto placement=std::find_if(state.dimensions.begin(),state.dimensions.end(),[&](const auto &p){return p.dimensionId==dimension.id;});
            if(placement==state.dimensions.end())continue;
            const auto result=results.find(dimension.id);
            const bool ready=result!=results.end()&&result->second.status==DrawingDimensionStatus::Ready;
            const auto formatted=ready?drawingDimensionLabel(dimension,result->second.value,placement->presentation):DrawingDimensionLabel{};
            const bool valid=ready&&formatted.problem.isEmpty();
            const QString text=ready?formatted.summary:QStringLiteral("Riferimento da verificare");
            auto *label=new MoveLabel(text);fontFor(label,state.style,state.style.dimensionHeight);if(valid)label->format(formatted);label->setPos(placement->position);label->setBrush(valid?QColor("#27445e"):QColor("#c42b32"));
            if(ready&&!valid){++missing;label->setToolTip(formatted.problem);}
            label->setData(kindRole,"dimension");label->setData(viewRole,dimension.viewId);label->setData(idRole,dimension.id);label->setFlag(QGraphicsItem::ItemIsMovable,!readOnly);
            if(!ready){++missing;if(result!=results.end())label->setToolTip(result->second.message);}
            label->activated=[this,id=dimension.id]{QTimer::singleShot(0,owner,[this,id]{editDimensionDialog(id);});};
            const auto id=dimension.id;label->moved=[this,id](QPointF,QPointF position){const auto error=owner->moveDimension(id,position);if(!error.isEmpty()){say(error);queueRedraw();}};scene->addItem(label);
            auto *background=scene->addRect(label->sceneBoundingRect().adjusted(-0.6,-0.3,0.6,0.3),QPen(Qt::NoPen),QBrush(Qt::white));background->setZValue(6);background->setData(viewRole,dimension.viewId);background->setAcceptedMouseButtons(Qt::NoButton);
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
        for(const auto &n:state.annotations){const auto result=noteResult(n);const bool ready=result.status==DrawingDimensionStatus::Ready;
            auto *item=new MoveNote(n,ready?n.text:QStringLiteral("Riferimento da verificare\n")+n.text,state.style);item->setPos(n.position);item->setDefaultTextColor(ready?QColor("#222222"):QColor("#c42b32"));item->setData(kindRole,"annotation");item->setData(idRole,n.id);item->setData(viewRole,n.viewId);item->setToolTip(ready?QStringLiteral("Doppio clic per modificare. Trascina per posizionare."):result.message);item->setFlag(QGraphicsItem::ItemIsMovable,interactive&&!readOnly);item->setFlag(QGraphicsItem::ItemIsSelectable,interactive);
            if(interactive){item->activated=[this,id=n.id]{QTimer::singleShot(0,owner,[this,id]{annotationDialog(id);});};item->moved=[this,id=n.id](QPointF p){const auto error=owner->moveAnnotation(id,p);if(!error.isEmpty()){say(error);queueRedraw();}};}scene->addItem(item);
            auto *background=scene->addRect(item->sceneBoundingRect().adjusted(-.5,-.2,.5,.2),QPen(Qt::NoPen),Qt::white);background->setData(viewRole,n.viewId);background->setZValue(6);background->setAcceptedMouseButtons(Qt::NoButton);
            if(n.reference&&ready)paintLeader(scene,paperLocal(n.viewId,result.point),item->sceneBoundingRect(),QColor("#27445e"),n.viewId,false,n.id);
        }
        return missing;
    }
    void redraw(){
        cancelViewDrag();cancelPlacement();resetQuote();rebuilding=true;scene->clear();paintedPaths.clear();const int missing=drawSheet(scene,true);
        refreshTree();rebuilding=false;selectionFromTree();refreshNotable();
        const bool projected=std::any_of(state.drawing.views.begin(),state.drawing.views.end(),[](const auto &v){return v.kind==DrawingViewKind::ProjectedEdges;});
        say(missing?QStringLiteral("%1 quote richiedono verifica. Seleziona il testo per leggere il motivo.").arg(missing):projected?
            QStringLiteral("Mostra linee nascoste agisce sulla vista selezionata. Bordi nascosti tratteggiati quotabili; contorni approssimati esclusi dalle quote."):
            QStringLiteral("Seleziona una curva e inserisci una quota. Trascina titoli e testi per posizionarli."));
        int missingNotes=0;for(const auto &n:state.annotations)if(noteResult(n).status!=DrawingDimensionStatus::Ready)++missingNotes;
        if(missingNotes)say(message->text()+QStringLiteral(" · %1 richiami da verificare.").arg(missingNotes));
        QStringList warnings;for(const auto &[id,contour]:silhouettes){(void)id;warnings.append(contour.warnings);}for(const auto &[id,display]:visibility){(void)id;warnings.append(display->warnings);}if(!warnings.empty())say(message->text()+QStringLiteral(" · Attenzione: ")+warnings.join(QStringLiteral(" ")));
    }
    QPointF paperLocal(const QUuid &id,Vec2 p)const{const auto *v=viewFor(id);const auto *l=layoutFor(id);return l->position+QPointF(v->scale*p.x(),-v->scale*p.y());}
    Vec2 localPaper(const QUuid &id,QPointF p)const{const auto *v=viewFor(id);const auto *l=layoutFor(id);return {(p.x()-l->position.x())/v->scale,(l->position.y()-p.y())/v->scale};}
    void clearOverlay(){for(auto *item:overlay)delete item;overlay.clear();}
    void resetQuote(){if(pendingNote&&quoteType)quoteType->setEnabled(!readOnly);pendingNote.reset();editingNote={};clearOverlay();quoteReferences.clear();quoteView={};snapChoices.clear();snapCycle=0;}
    void paintTip(QGraphicsScene *target,QPointF point,QPointF toward,QColor color,const QUuid &viewId,bool preview){
        auto *item=target->addPath(drawingArrowPath(state.style,point,toward),QPen(color,.22),state.style.arrow==DrawingArrowStyle::Filled?QBrush(color):QBrush(Qt::NoBrush));
        item->setData(kindRole,"dimensionArrow");item->setData(viewRole,viewId);item->setZValue(preview?20:4);item->setAcceptedMouseButtons(Qt::NoButton);if(preview)overlay.push_back(item);
    }
    void paintLeader(QGraphicsScene *target,QPointF anchor,const QRectF &box,QColor color,const QUuid &viewId,bool preview,const QUuid &noteId={}){
        auto *line=target->addPath(drawingAnnotationLeader(anchor,box,false),QPen(color,.22));line->setData(kindRole,preview?"annotationLeaderPreview":"annotationLeader");line->setData(idRole,noteId);line->setData(viewRole,viewId);line->setZValue(preview?20:4);line->setAcceptedMouseButtons(Qt::NoButton);if(preview)overlay.push_back(line);
        const bool left=anchor.x()<box.center().x();const QPointF toward((left?box.left():box.right())+(left?-4:4),std::clamp(anchor.y(),box.top(),box.bottom()));paintTip(target,anchor,toward,color,viewId,preview);
    }
    void paintAttached(const DrawingDimension &dimension,const DrawingDimensionResult &result,QPointF text,bool preview=false,QGraphicsScene *target=nullptr){
        auto *scene=target?target:this->scene;
        const auto id=dimension.viewId;const QColor color=preview?QColor("#1469c0"):QColor("#27445e");
        const auto stroke=[&](QPointF a,QPointF b){auto *item=scene->addLine(QLineF(a,b),QPen(color,.22));item->setData(viewRole,id);item->setZValue(preview?20:4);if(preview)overlay.push_back(item);};
        const auto tip=[&](QPointF a,QPointF toward){paintTip(scene,a,toward,color,id,preview);};
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
            auto *item=scene->addPath(arc,QPen(color,.22));item->setData(viewRole,id);item->setZValue(preview?20:4);if(preview)overlay.push_back(item);
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
        notable.clear();if(!quoteAction||(!quoteAction->isChecked()&&!cut))return;
        for(const auto &[id,pieces]:curves){const auto *definition=viewFor(id);const auto *layout=layoutFor(id);if(!definition||!layout||(cut&&id!=cut->parent))continue;
            for(std::size_t i=0;i<pieces.size();++i){const auto &piece=pieces[i];const auto a=piece.curve->point(piece.range.lo),b=piece.curve->point(piece.range.hi);const auto primitive=drawingPrimitive(*definition,piece);
                const auto add=[&](DrawingPointRole role,Vec3 point){const auto paper=paperPoint(*definition,*layout,point);if(pointShown(id,i,paper,role==DrawingPointRole::Center))notable.push_back({id,i,role,std::numeric_limits<double>::quiet_NaN(),paper,0});};
                if(distance(a,b)>definition->tolerance){add(DrawingPointRole::Start,a);add(DrawingPointRole::End,b);}
                if(primitive.kind==DrawingPrimitive::Kind::Line)add(DrawingPointRole::Midpoint,.5*(a+b));
                if(primitive.kind==DrawingPrimitive::Kind::Conic){const auto center=definition->plane.toGlobal(Vec3(primitive.center.x(),primitive.center.y(),0));add(DrawingPointRole::Center,center);
                    if(primitive.circular)for(int k=0;k<4;++k){const auto q=primitive.center+norm(primitive.firstAxis)*Vec2(k==0?1:k==2?-1:0,k==1?1:k==3?-1:0);const auto point=definition->plane.toGlobal(Vec3(q.x(),q.y(),0));
                        if(projectPoint(*piece.curve,point,piece.range).distance<=definition->tolerance)add(DrawingPointRole(int(DrawingPointRole::Quadrant0)+k),point);}}
            }
        }
        refreshAxisNotable();
    }
    void refreshAxisNotable(){
        if(axisInput>=0)return;
        for(const auto &axis:state.axes)if(axis.visible){const auto result=cachedAxis(axis);if(result.status!=DrawingDimensionStatus::Ready)continue;
            for(auto role:{DrawingPointRole::Start,DrawingPointRole::End,DrawingPointRole::Midpoint}){
                const auto p=role==DrawingPointRole::Start?result.first:role==DrawingPointRole::End?result.second:.5*(result.first+result.second);
                notable.push_back({axis.viewId,0,role,std::numeric_limits<double>::quiet_NaN(),paperLocal(axis.viewId,p),0,axis.id});
            }
        }
    }
    DrawingDimension quoted(QPointF cursor)const{return drawingDimensionBetween(*viewFor(quoteView),model.snapshot,curves.at(quoteView),quoteReferences,
        DrawingMeasure(quoteType->currentData().toInt()<0?int(DrawingMeasure::Distance):quoteType->currentData().toInt()),localPaper(quoteView,cursor),quoteType->currentData().toInt()<0,resolverFor(quoteView));}
    void quoteOverlay(){
        clearOverlay();if(!quoteAction->isChecked())return;
        if(pendingNote&&(!noteLeader||pendingNote->reference)){
            auto note=*pendingNote;note.position=quoteCursor;auto *item=new MoveNote(note,note.text,state.style);item->setPos(note.position);item->setDefaultTextColor(QColor("#1469c0"));item->setFlags({});item->setAcceptedMouseButtons(Qt::NoButton);item->setZValue(30);item->setData(kindRole,"annotationPreview");scene->addItem(item);overlay.push_back(item);
            if(note.reference){const auto result=noteResult(note);if(result.status==DrawingDimensionStatus::Ready){paintLeader(scene,paperLocal(note.viewId,result.point),item->sceneBoundingRect(),QColor("#1469c0"),note.viewId,true,note.id);}}
            say(QStringLiteral("Posiziona il testo con un clic. Esc annulla."));return;
        }
        const auto dot=[&](QPointF p,QColor color,const QString &kind){auto *marker=scene->addEllipse(-4,-4,8,8,QPen(color,1),QBrush(Qt::white));marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);marker->setPos(p);marker->setZValue(40);marker->setData(kindRole,kind);overlay.push_back(marker);};
        for(const auto &ref:quoteReferences){const auto found=resolveDrawingAttachment(*viewFor(quoteView),model.snapshot,curves.at(quoteView),ref,resolverFor(quoteView));if(found.status==DrawingDimensionStatus::Ready)dot(paperLocal(quoteView,found.isPoint?found.point:.5*(found.first+found.second)),QColor("#bd7800"),"dimensionPicked");}
        QString hint=QStringLiteral("Quota: clic sui riferimenti, poi posiziona nello spazio libero. Maiusc: curva; Alt: punto piu' vicino; Tab: cambia entita'; Esc: annulla.");
        if(!snapChoices.empty()){
            const auto &pick=snapChoices[snapCycle%snapChoices.size()];const auto *layout=layoutFor(pick.view);
            if(!pick.axisId.isNull()){
                dot(pick.paper,QColor("#1469c0"),"dimensionSnap");const auto *axis=axisFor(pick.axisId);if(!axis){snapChoices.clear();say(QStringLiteral("Asse non piu’ disponibile: scegliere un altro riferimento."));return;}const auto result=evaluateDrawingAxis(*axis,*viewFor(pick.view),model.snapshot,curves.at(pick.view));
                auto *highlight=scene->addPath(drawingAxisPath(result,*viewFor(pick.view),axis->extension),QPen(QColor("#1469c0"),.5));highlight->setPos(layout->position);highlight->setZValue(25);overlay.push_back(highlight);
                hint=snapName(pick.role)+QStringLiteral(" · ")+axis->name+QStringLiteral(" (%1/%2) · Tab cambia entita’").arg(snapCycle%snapChoices.size()+1).arg(snapChoices.size());
            }else{const auto &piece=curves.at(pick.view)[pick.index];
            dot(pick.paper,QColor("#1469c0"),"dimensionSnap");auto *highlight=scene->addPath(paintedPaths.at(pick.view).at(pick.index),QPen(QColor("#1469c0"),.5));highlight->setZValue(25);overlay.push_back(highlight);
            const auto source=std::find_if(model.choices.begin(),model.choices.end(),[&](const auto &c){return c.id==piece.featureId;});
            hint=snapName(pick.role)+QStringLiteral(" · ")+layout->title+(source==model.choices.end()?QString():QStringLiteral(" · ")+source->name)
                +QStringLiteral(" (%1/%2) · Tab cambia entita'; Maiusc curva; Alt punto.").arg(snapCycle%snapChoices.size()+1).arg(snapChoices.size());}
        }
        if(pendingNote){say(QStringLiteral("Richiamo: scegli il punto con gli snap o un punto sulla curva; Tab cambia riferimento. Esc annulla."));return;}
        if(axisInput>=0){
            hint=axisInput==int(DrawingAxisKind::ThroughPoints)?QStringLiteral("Asse: scegli due punti con gli snap, poi clic per confermare. Tab cambia riferimento; Esc annulla."):QStringLiteral("Mediana: scegli due rette parallele, poi clic per confermare. Tab cambia riferimento; Esc annulla.");
            if(quoteReferences.size()==2){const auto axis=candidateAxis();const auto value=evaluateDrawingAxis(axis,*viewFor(quoteView),model.snapshot,curves.at(quoteView));
                if(value.status==DrawingDimensionStatus::Ready){QPen pen(QColor("#1469c0"),.25);pen.setDashPattern({32,5,3,5});auto *item=scene->addPath(drawingAxisPath(value,*viewFor(quoteView),axis.extension),pen);item->setPos(layoutFor(quoteView)->position);item->setZValue(30);item->setData(kindRole,"axisPreview");overlay.push_back(item);}else hint=value.message;}
            say(hint);return;
        }
        if(!quoteReferences.empty())try{
            auto dimension=quoted(quoteCursor);const auto value=evaluateDrawingAttachments(dimension,*viewFor(quoteView),model.snapshot,curves.at(quoteView),resolverFor(quoteView));
            if(value.status==DrawingDimensionStatus::Ready){auto *text=scene->addSimpleText(drawingDimensionText(dimension,value.value));fontFor(text,state.style,state.style.dimensionHeight);text->setBrush(QColor("#1469c0"));text->setZValue(30);text->setPos(quoteCursor+QPointF(3,-4));text->setData(kindRole,"dimensionPreview");overlay.push_back(text);paintAttached(dimension,value,text->sceneBoundingRect().center(),true);}
        }catch(const std::exception &){if(snapChoices.empty())hint=QStringLiteral("Seleziona un secondo riferimento compatibile; Esc cancella la scelta.");}
        say(hint);
    }
    bool quotePointer(QPointF cursor,Qt::KeyboardModifiers modifiers,bool click){
        if(readOnly||!quoteAction->isChecked())return false;
        if(pendingNote&&(!noteLeader||pendingNote->reference)){
            quoteCursor=cursor;if(click){auto note=*pendingNote;note.position=cursor;const auto error=editingNote.isNull()?owner->addAnnotation(note):owner->updateAnnotation(note);if(!error.isEmpty()){say(error);return true;}quoteAction->setChecked(false);return true;}quoteOverlay();return true;
        }
        if(axisInput==int(DrawingAxisKind::BetweenLines))modifiers|=Qt::ShiftModifier;
        const double zoom=std::abs(view->transform().m11()),tolerance=9/std::max(.1,zoom);
        if(QLineF(cursor,lastSnapCursor).length()*zoom>3||modifiers!=quoteModifiers)snapCycle=0;
        lastSnapCursor=cursor;quoteCursor=cursor;quoteModifiers=modifiers;snapChoices.clear();
        const auto measure=DrawingMeasure(quoteType->currentData().toInt());
        const bool wholeCurve=!pendingNote&&(measure==DrawingMeasure::Radius||measure==DrawingMeasure::Diameter||measure==DrawingMeasure::Angle||measure==DrawingMeasure::SectionLength);
        if(!wholeCurve&&!(modifiers&(Qt::ShiftModifier|Qt::AltModifier)))for(auto pick:notable){if(axisInput>=0&&!pick.axisId.isNull())continue;if(!quoteView.isNull()&&pick.view!=quoteView)continue;pick.gap=QLineF(cursor,pick.paper).length()*zoom;if(pick.gap<=9)snapChoices.push_back(pick);}
        if(snapChoices.empty()){
            const auto candidates=scene->items(QRectF(cursor-QPointF(tolerance,tolerance),QSizeF(2*tolerance,2*tolerance)),Qt::IntersectsItemBoundingRect);
            for(auto *item:candidates){const auto kind=item->data(kindRole).toString();
                if(kind==QLatin1String("axis")&&axisInput<0){
                    const auto id=item->data(viewRole).toUuid();if(!quoteView.isNull()&&quoteView!=id)continue;const auto *axis=axisFor(item->data(idRole).toUuid());if(!axis||!axis->visible)continue;
                    const auto result=cachedAxis(*axis);if(result.status!=DrawingDimensionStatus::Ready)continue;
                    const auto p=localPaper(id,cursor),d=result.second-result.first;const double t=dot(p-result.first,d)/dot(d,d),fraction=std::clamp(t,0.0,1.0);
                    const auto q=result.first+fraction*d;const auto paper=paperLocal(id,q);const double gap=QLineF(cursor,paper).length()*zoom;
                    const double extension=axis->extension/viewFor(id)->scale,extent=norm(d);
                    const auto linePaper=paperLocal(id,result.first+t*d);const double lineGap=QLineF(cursor,linePaper).length()*zoom;
                    const bool nearest=pendingNote.has_value()||(modifiers&Qt::AltModifier);
                    if(nearest?gap<=9:(lineGap<=9&&t>=-extension/extent&&t<=1+extension/extent))snapChoices.push_back({id,0,nearest?DrawingPointRole::Nearest:DrawingPointRole::Curve,fraction,nearest?paper:linePaper,nearest?gap:lineGap,axis->id});
                    continue;
                }
                if(kind!=QLatin1String("projection")&&kind!=QLatin1String("section"))continue;
                const auto id=item->data(viewRole).toUuid();if(!quoteView.isNull()&&id!=quoteView)continue;const auto index=item->data(indexRole).toULongLong();const auto *definition=viewFor(id);const auto *layout=layoutFor(id);const auto &piece=curves.at(id).at(index);
                const auto p=localPaper(id,cursor);const auto found=projectPoint(*piece.curve,definition->plane.toGlobal(Vec3(p.x(),p.y(),0)),piece.range);const auto paper=paperPoint(*definition,*layout,found.point);const double gap=QLineF(cursor,paper).length()*zoom;
                if(gap<=9&&pointShown(id,std::size_t(index),paper))snapChoices.push_back({id,std::size_t(index),(pendingNote||(modifiers&Qt::AltModifier))?DrawingPointRole::Nearest:DrawingPointRole::Curve,found.parameter,paper,gap});
            }
        }
        std::stable_sort(snapChoices.begin(),snapChoices.end(),[](const auto &a,const auto &b){if(std::abs(a.gap-b.gap)>1e-6)return a.gap<b.gap;if(a.view!=b.view)return a.view<b.view;if(a.axisId!=b.axisId)return a.axisId<b.axisId;if(a.index!=b.index)return a.index<b.index;return int(a.role)<int(b.role);});
        if(click){
            if(axisInput>=0&&quoteReferences.size()==2){auto axis=candidateAxis();const auto error=editingAxis.isNull()?owner->addAxis(axis):owner->updateAxis(axis);if(!error.isEmpty()){say(error);return true;}quoteAction->setChecked(false);return true;}
            if(axisInput<0&&!quoteReferences.empty()&&(quoteReferences.size()==2||snapChoices.empty())){
                try{auto dimension=quoted(cursor);const auto font=drawingFont(state.style);const auto measured=evaluateDrawingAttachments(dimension,*viewFor(quoteView),model.snapshot,curves.at(quoteView),resolverFor(quoteView));
                    const double width=state.style.dimensionHeight/12*QFontMetricsF(font).horizontalAdvance(drawingDimensionText(dimension,measured.value));const auto error=owner->addDimension(dimension,cursor-QPointF(width/2,1.5));
                    if(!error.isEmpty()){say(error);return true;}resetQuote();return true;
                }catch(const std::exception &e){say(QString::fromUtf8(e.what()));return true;}
            }
            if(snapChoices.empty()){say(QStringLiteral("Avvicina il puntatore a un punto o a una curva della vista."));return true;}
            const auto pick=snapChoices[snapCycle%snapChoices.size()];try{
                DrawingAttachment ref;
                if(!pick.axisId.isNull()){ref.axisId=pick.axisId;ref.role=pick.role;if(pick.role==DrawingPointRole::Nearest)ref.fraction=pick.parameter;}
                else ref=drawingAttachment(*viewFor(pick.view),curves.at(pick.view)[pick.index],model.snapshot,pick.role,pick.parameter);
                if(pendingNote){
                    if(ref.role==DrawingPointRole::Curve){say(QStringLiteral("Scegli un punto geometrico del richiamo."));return true;}
                    const auto resolved=resolveDrawingAttachment(*viewFor(pick.view),model.snapshot,curves.at(pick.view),ref,resolverFor(pick.view));if(resolved.status!=DrawingDimensionStatus::Ready||!resolved.isPoint){say(resolved.message);return true;}
                    pendingNote->viewId=pick.view;pendingNote->reference=ref;snapChoices.clear();quoteOverlay();return true;
                }
                if(axisInput>=0){const auto resolved=resolveDrawingAttachment(*viewFor(pick.view),model.snapshot,curves.at(pick.view),ref,resolverFor(pick.view));
                    if(resolved.status!=DrawingDimensionStatus::Ready){say(resolved.message);return true;}
                    if((axisInput==int(DrawingAxisKind::ThroughPoints)&&!resolved.isPoint)||(axisInput==int(DrawingAxisKind::BetweenLines)&&!resolved.isLine)){say(QStringLiteral("Scegli i riferimenti richiesti dall'asse: punti con snap oppure rette parallele."));return true;}}
                if(!quoteReferences.empty()){const auto a=resolveDrawingAttachment(*viewFor(pick.view),model.snapshot,curves.at(pick.view),quoteReferences[0],resolverFor(pick.view)),b=resolveDrawingAttachment(*viewFor(pick.view),model.snapshot,curves.at(pick.view),ref,resolverFor(pick.view));
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
        if(readOnly||pendingNote)return;
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
        for(int m=0;m<=9;++m){if(m==1)continue;try{auto d=drawingDimensionBetween(*definition,model.snapshot,curves.at(original.viewId),refs,DrawingMeasure(m),cursor,false,resolverFor(original.viewId));d.id=original.id;
                if(d.measure==DrawingMeasure::Angle)d.angleSides=original.measure==DrawingMeasure::Angle?original.angleSides:d.angleSides;
                options.push_back(d);type->addItem(drawingDimensionName(d.measure));}catch(const std::exception &) {}}
        if(options.empty()){options.push_back(original);type->addItem(drawingDimensionName(original.measure));}
        const auto wanted=original.measure==DrawingMeasure::EndpointDistance?DrawingMeasure::Distance:original.measure;
        for(std::size_t i=0;i<options.size();++i)if(options[i].measure==wanted)type->setCurrentIndex(int(i));
        auto *decimals=new QSpinBox(&dialog);decimals->setObjectName("drawingDimensionDecimals");decimals->setRange(0,8);decimals->setValue(original.decimals);
        auto *sector=new QComboBox(&dialog);sector->setObjectName("drawingDimensionSector");sector->addItems({"Direzioni originali","Prima direzione invertita","Seconda direzione invertita","Entrambe invertite"});sector->setCurrentIndex(original.angleSides);
        auto *mode=new QComboBox(&dialog);mode->setObjectName("drawingToleranceMode");mode->addItems({"Nessuna","Simmetrica (±)","Scostamenti superiore / inferiore","Valori limite"});mode->setCurrentIndex(int(placement->presentation.mode));
        auto *precision=new QSpinBox(&dialog);precision->setObjectName("drawingToleranceDecimals");precision->setRange(0,8);precision->setValue(placement->presentation.decimals);
        auto *upper=new NumericInput(&dialog);upper->setObjectName("drawingToleranceUpper");upper->setDecimals(8);upper->setRange(-1e6,1e6);upper->setSingleStep(.01);upper->setValue(placement->presentation.upper);
        auto *lower=new NumericInput(&dialog);lower->setObjectName("drawingToleranceLower");lower->setDecimals(8);lower->setRange(-1e6,1e6);lower->setSingleStep(.01);lower->setValue(placement->presentation.lower);
        auto *prefix=new QLineEdit(placement->presentation.prefix,&dialog);prefix->setObjectName("drawingDimensionPrefix");prefix->setMaxLength(128);
        auto *suffix=new QLineEdit(placement->presentation.suffix,&dialog);suffix->setObjectName("drawingDimensionSuffix");suffix->setMaxLength(128);
        auto *value=new QLabel(&dialog);value->setObjectName("drawingDimensionValue");value->setTextFormat(Qt::PlainText);
        auto *preview=new QGraphicsView(&dialog);preview->setObjectName("drawingTolerancePreview");QGraphicsScene previewScene;preview->setScene(&previewScene);preview->setMinimumHeight(85);preview->setMinimumWidth(380);preview->setRenderHint(QPainter::Antialiasing);preview->setBackgroundBrush(Qt::white);
        auto *error=new QLabel(&dialog);error->setTextFormat(Qt::PlainText);error->setWordWrap(true);
        const auto presentation=[&]{auto p=placement->presentation;p.mode=DrawingToleranceMode(mode->currentIndex());p.decimals=precision->value();p.prefix=prefix->text();p.suffix=suffix->text();p.upper=p.mode==DrawingToleranceMode::None?0:upper->value();p.lower=p.mode==DrawingToleranceMode::None?0:p.mode==DrawingToleranceMode::Symmetric?-p.upper:lower->value();return p;};
        const auto candidate=[&]{auto d=options.at(std::size_t(type->currentIndex()));d.decimals=decimals->value();d.angleSides=d.measure==DrawingMeasure::Angle?sector->currentIndex():0;return d;};
        const auto refresh=[&]{const auto d=candidate();const auto result=measured(state,d,curves.at(d.viewId));const bool ready=result.status==DrawingDimensionStatus::Ready;
            value->setText(ready?drawingDimensionText(d,result.value):QStringLiteral("Riferimento da verificare"));sector->setEnabled(d.measure==DrawingMeasure::Angle);
            const bool active=mode->currentIndex()!=0;upper->setEnabled(active);lower->setEnabled(mode->currentIndex()>=2);precision->setEnabled(active);
            if(mode->currentIndex()==1){const QSignalBlocker block(lower);lower->setValue(-upper->value());}
            const auto unit=d.measure==DrawingMeasure::Angle?QStringLiteral("°"):QStringLiteral(" mm");if(upper->suffix()!=unit)upper->setSuffix(unit);if(lower->suffix()!=unit)lower->setSuffix(unit);
            const auto text=ready?drawingDimensionLabel(d,result.value,presentation()):DrawingDimensionLabel{};previewScene.clear();
            auto *item=new MoveLabel(ready?text.summary:QStringLiteral("Riferimento da verificare"));item->setFont(drawingFont(state.style));if(ready&&text.problem.isEmpty())item->format(text);item->setFlags({});item->setScale(2);previewScene.addItem(item);previewScene.setSceneRect(item->sceneBoundingRect().adjusted(-8,-8,8,8));
            error->setText(!ready?result.message:text.problem);};
        form->addRow(QStringLiteral("Tipo:"),type);form->addRow(QStringLiteral("Decimali:"),decimals);form->addRow(QStringLiteral("Settore angolare:"),sector);form->addRow(QStringLiteral("Valore misurato:"),value);
        form->addRow(QStringLiteral("Tolleranza:"),mode);form->addRow(QStringLiteral("Decimali tolleranza / limiti:"),precision);form->addRow(QStringLiteral("Scostamento superiore / ampiezza ±:"),upper);form->addRow(QStringLiteral("Scostamento inferiore:"),lower);form->addRow(QStringLiteral("Prefisso:"),prefix);form->addRow(QStringLiteral("Suffisso:"),suffix);form->addRow(QStringLiteral("Anteprima:"),preview);form->addRow(error);
        auto *note=new QLabel(QStringLiteral("Quota di riferimento associata al modello salvato. Le dimensioni del modello si modificano nello schizzo 3D."),&dialog);note->setWordWrap(true);form->addRow(note);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{const auto result=owner->updateDimension(candidate(),presentation());error->setText(result);if(result.isEmpty())dialog.accept();});
        connect(type,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});connect(decimals,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){refresh();});connect(sector,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});
        connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});connect(precision,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){refresh();});
        connect(upper,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&](double){refresh();});connect(lower,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&](double){refresh();});
        connect(prefix,&QLineEdit::textChanged,&dialog,[&](const QString &){refresh();});connect(suffix,&QLineEdit::textChanged,&dialog,[&](const QString &){refresh();});refresh();dialog.exec();
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
        auto *depth=new NumericInput(&dialog);depth->setRange(-1e8,1e8);depth->setDecimals(6);depth->setSuffix(" mm");depth->setObjectName("drawingViewDepth");
        auto *scale=new NumericInput(&dialog);scale->setDecimals(4);scale->setRange(0.001,1000);scale->setValue(1);scale->setObjectName("drawingViewScale");
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
        const DrawingView base=editing?*original:DrawingView{};
        const DrawingViewLayout baseLayout=editing?*placement:DrawingViewLayout{};
        const auto candidate=[&]{
            DrawingView definition=base;
            const auto normal=editing&&plane->currentIndex()==originalPlane?base.plane.zDir():normalFor(plane->currentIndex());
            if(editing&&plane->currentIndex()==originalPlane){const double oldDepth=dot(base.plane.origin(),normal);
                if(depth->value()!=initialDepth)definition.plane=Frame3::fromAxes(base.plane.origin()+(depth->value()-oldDepth)*normal,base.plane.xDir(),base.plane.yDir(),normal);
            }else definition.plane=Frame3(depth->value()*normal,normal,xFor(plane->currentIndex()));
            definition.scale=editing&&scale->value()==initialScale?base.scale:scale->value();definition.kind=kind;definition.sectionStyle=projected?DrawingSectionStyle::Removed:DrawingSectionStyle(sectionStyle->currentIndex());definition.sectionReferences=projected&&references->isChecked();
            auto layout=baseLayout;layout.viewId=definition.id;layout.title=title->text().trimmed();layout.features.clear();
            for(int row=0;row<bodies->count();++row)if(bodies->item(row)->checkState()==Qt::Checked)layout.features.append(bodies->item(row)->data(Qt::UserRole).toULongLong());
            if(!editing){const auto local=definition.plane.toLocal(center);layout.position=QPointF(state.size.width()/2,state.size.height()/2)-QPointF(local.x()*definition.scale,-local.y()*definition.scale);}
            return qMakePair(definition,layout);
        };
        const auto validateCandidate=[&](const DrawingView &definition,const DrawingViewLayout &layout){
            if(readOnly)return QStringLiteral("La tavola e' in sola lettura.");
            auto next=state;
            if(editing){const auto d=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==definition.id;});const auto l=std::find_if(next.views.begin(),next.views.end(),[&](const auto &v){return v.viewId==definition.id;});
                if(d==next.drawing.views.end()||l==next.views.end())return QStringLiteral("Vista da modificare non piu' disponibile.");
                *d=definition;*l=layout;
            }else{next.drawing.views.push_back(definition);next.views.append(layout);}
            QByteArray bytes;return encodeDrawingSheet(next,bytes);
        };
        auto *preview=new ViewPreviewCanvas(&dialog);preview->setObjectName("drawingViewPreview");preview->setMinimumSize(380,170);preview->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);preview->setBackgroundBrush(Qt::white);preview->setRenderHint(QPainter::Antialiasing);preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);preview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QGraphicsScene previewScene;preview->setScene(&previewScene);
        auto *status=new QLabel(&dialog);status->setObjectName("drawingViewPreviewStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);
        auto *progress=new QProgressBar(&dialog);progress->setObjectName("drawingViewPreviewProgress");progress->setRange(0,5);progress->setFormat(QStringLiteral("Anteprima · fase %v di %m"));progress->hide();
        form->insertRow(form->rowCount()-2,new QLabel(QStringLiteral("Anteprima della vista:"),&dialog));form->insertRow(form->rowCount()-2,preview);form->insertRow(form->rowCount()-2,status);form->insertRow(form->rowCount()-2,progress);
        bodies->setMaximumHeight(100);
        struct PreviewTask {
            quint64 generation=0;DrawingView definition;DrawingViewLayout layout;DrawingModelSnapshot source;ComputedView result;QString error;
            Kernel::CancellationToken cancel=std::make_shared<std::atomic<bool>>(false);std::atomic<bool> done{false};std::atomic<int> phase{0};
        };
        std::shared_ptr<PreviewTask> running,ready;
        quint64 generation=0;bool pending=false,confirmWhenReady=false;auto sourceStamp=model.snapshot;bool accessStamp=readOnly;
        QTimer debounce(&dialog),poll(&dialog);debounce.setSingleShot(true);debounce.setInterval(160);poll.setInterval(20);
        const auto render=[&](const PreviewTask &task){
            previewScene.clear();QRectF box;bool first=true;
            const auto add=[&](const QPainterPath &path,QColor color,double width=1.2,Qt::PenStyle style=Qt::SolidLine){
                if(path.isEmpty())return;
                QPen pen(color,width,style);pen.setCosmetic(true);auto *item=previewScene.addPath(path,pen);item->setData(kindRole,"previewCurve");
                box=first?path.boundingRect():box.united(path.boundingRect());first=false;
            };
            const auto &display=*task.result.display;
            if(task.layout.showMesh&&!display.shadedImage.isNull()){
                auto *image=previewScene.addPixmap(QPixmap::fromImage(display.shadedImage));image->setTransform(QTransform::fromScale(display.shadedBounds.width()/display.shadedImage.width(),display.shadedBounds.height()/display.shadedImage.height()));image->setPos(display.shadedBounds.topLeft());image->setZValue(-3);
                if(!display.sectionMaterial.isEmpty()){auto *cap=previewScene.addPath(display.sectionMaterial,QPen(Qt::NoPen),Qt::white);cap->setZValue(-2);}
            }
            for(std::size_t i=0;i<display.curves.size();++i){const auto &path=display.curves[i];add(path.visible,task.result.curves[i].kind==DrawingCurveKind::Section?QColor("#187c59"):QColor("#222222"));if(task.layout.showHiddenLines)add(path.hidden,QColor("#80858a"),1,Qt::DashLine);}
            for(const auto &path:display.silhouettes){add(path.visible,QColor("#222222"));if(task.layout.showHiddenLines)add(path.hidden,QColor("#80858a"),1,Qt::DashLine);}
            if(task.layout.relation.hatch)add(display.sectionHatch,QColor("#666666"),.7);
            const auto &centers=task.layout.showHiddenLines?display.allCenters:display.visibleCenters;
            if(task.layout.showCenterMarks||task.layout.showCenterLines)add(centers.crosses,QColor("#555555"),1);
            if(task.layout.showCenterLines)add(centers.axes,QColor("#555555"),1,Qt::DashDotLine);
            if(task.layout.showCylinderAxes)add(task.layout.showHiddenLines?display.allCylinderAxes:display.visibleCylinderAxes,QColor("#555555"),1,Qt::DashDotLine);
            if(first)box=display.shadedBounds;
            preview->setProperty("previewBounds",box);preview->setProperty("previewNormal",QVariantList{task.definition.plane.zDir().x(),task.definition.plane.zDir().y(),task.definition.plane.zDir().z()});preview->setProperty("previewBodyCount",int(task.source.bodies.size()));preview->setProperty("previewGeneration",QVariant::fromValue(task.generation));
            const double margin=.08*std::max({box.width(),box.height(),1.});previewScene.setSceneRect(box.adjusted(-margin,-margin,margin,margin));preview->fit();
        };
        const auto matches=[&](const PreviewTask &task,const DrawingView &definition,const DrawingViewLayout &layout){return sameGeometry(task.definition,definition)&&task.definition.scale==definition.scale&&task.layout.features==layout.features&&sameSource(task.source,sourceFor(layout));};
        std::function<void()> launch,request,commit;
        commit=[&]{
            const auto [definition,layout]=candidate();const auto invalid=validateCandidate(definition,layout);if(!invalid.isEmpty()){errorLabel->setText(invalid);confirmWhenReady=false;buttons->button(QDialogButtonBox::Ok)->setEnabled(true);return;}
            if(!ready||!matches(*ready,definition,layout)){confirmWhenReady=true;buttons->button(QDialogButtonBox::Ok)->setEnabled(false);if(!pending&&!running)request();confirmWhenReady=true;debounce.stop();launch();return;}
            dialog.setEnabled(false);QString result;
            if(editing){dialogReady=PreparedView{definition,layout,ready->source,ready->result};result=owner->updateView(definition,layout);dialogReady.reset();}
            else {silhouetteCache=ready->result.cache;result=startPreparedPlacement(definition,layout,ready->result);}
            dialog.setEnabled(true);confirmWhenReady=false;buttons->button(QDialogButtonBox::Ok)->setEnabled(true);errorLabel->setText(result);if(result.isEmpty())dialog.accept();
        };
        launch=[&]{
            if(!pending||running)return;
            const auto [definition,layout]=candidate();const auto invalid=validateCandidate(definition,layout);if(!invalid.isEmpty()){pending=false;errorLabel->setText(invalid);progress->hide();return;}
            auto task=std::make_shared<PreviewTask>();task->generation=generation;task->definition=definition;task->layout=layout;task->source=sourceFor(layout);auto seeds=silhouetteCache;running=task;pending=false;
            QThreadPool::globalInstance()->start([task,seeds=std::move(seeds)]()mutable{
                try{Kernel::CancellationScope scope(task->cancel);Kernel::cancellationPoint();task->result=buildView(task->source,task->definition,task->layout,std::move(seeds),[&](int phase){task->phase.store(phase,std::memory_order_relaxed);Kernel::cancellationPoint();});
                    if(task->result.curves.empty()&&task->result.contours.paths.empty())task->error=task->definition.kind==DrawingViewKind::Section?QStringLiteral("Il piano non interseca i corpi selezionati."):QStringLiteral("Nessuna geometria proiettabile nei corpi selezionati.");
                }catch(const Kernel::OperationCancelled &){task->error=QStringLiteral("Anteprima annullata.");}catch(const std::exception &e){task->error=QString::fromUtf8(e.what());}
                task->done.store(true,std::memory_order_release);
            });
        };
        request=[&]{
            ++generation;confirmWhenReady=false;if(running)running->cancel->store(true,std::memory_order_relaxed);
            previewScene.clear();preview->setProperty("previewReady",false);ready.reset();sourceStamp=model.snapshot;accessStamp=readOnly;buttons->button(QDialogButtonBox::Ok)->setEnabled(!readOnly);
            const auto [definition,layout]=candidate();const auto invalid=validateCandidate(definition,layout);errorLabel->setText(invalid);pending=invalid.isEmpty();preview->setProperty("previewBusy",pending);
            status->setText(pending?QStringLiteral("Calcolo anteprima…"):QStringLiteral("Anteprima non disponibile."));progress->setVisible(pending);progress->setValue(0);if(pending)debounce.start();else debounce.stop();
        };
        connect(&debounce,&QTimer::timeout,&dialog,[&]{launch();});
        connect(&poll,&QTimer::timeout,&dialog,[&]{
            if(!sameSource(sourceStamp,model.snapshot)||accessStamp!=readOnly)request();
            if(!running)return;
            if(!running->done.load(std::memory_order_acquire)){if(running->generation==generation)progress->setValue(running->phase.load(std::memory_order_relaxed));return;}
            auto completed=std::move(running);running.reset();
            if(completed->generation==generation){
                preview->setProperty("previewBusy",false);progress->hide();errorLabel->setText(completed->error);buttons->button(QDialogButtonBox::Ok)->setEnabled(!readOnly);
                if(completed->error.isEmpty()){ready=completed;render(*ready);preview->setProperty("previewReady",true);status->setText(QStringLiteral("Anteprima adattata al riquadro · scala foglio/modello %1:1").arg(ready->definition.scale));if(confirmWhenReady)commit();}
                else {status->setText(QStringLiteral("Anteprima non disponibile."));confirmWhenReady=false;}
            }
            if(pending&&!debounce.isActive())launch();
        });
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{commit();});
        connect(plane,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){request();});connect(depth,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&](double){request();});
        connect(scale,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&](double){request();});connect(references,&QCheckBox::toggled,&dialog,[&](bool){request();});connect(sectionStyle,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){request();});
        connect(bodies,&QListWidget::itemChanged,&dialog,[&](QListWidgetItem *){request();});
        connect(title,&QLineEdit::textChanged,&dialog,[&](const QString &){confirmWhenReady=false;const auto [d,l]=candidate();const auto invalid=validateCandidate(d,l);errorLabel->setText(invalid);if(invalid.isEmpty()&&!ready&&!running&&!pending)request();});
        request();poll.start();dialog.resize(620,720);dialog.exec();poll.stop();debounce.stop();if(running)running->cancel->store(true,std::memory_order_relaxed);dialogReady.reset();if(this->placement)view->setFocus();
    }
    struct CutPlacement {
        QUuid parent,existing;QString label;DrawingSectionStyle style=DrawingSectionStyle::Removed;bool reverse=false,started=false;QPointF first,cursor;
        QGraphicsPathItem *item=nullptr,*snapMark=nullptr;
        QGraphicsSimpleTextItem *snapLabel=nullptr;
        struct Point {QPointF paper;QString label;};
        std::vector<Point> points,choices;std::size_t cycle=0;QPointF raw;Qt::KeyboardModifiers modifiers{};
    };
    std::unique_ptr<CutPlacement> cut;
    void cancelCut(){if(cut){delete cut->item;delete cut->snapMark;cut.reset();view->setCursor(Qt::ArrowCursor);}}
    void refreshCutPoints(){
        if(!cut)return;
        refreshNotable();cut->points.clear();
        const auto append=[&](QPointF point,const QString &label){if(std::none_of(cut->points.begin(),cut->points.end(),[&](const auto &p){return p.label==label&&QLineF(p.paper,point).length()<1e-8;}))cut->points.push_back({point,label});};
        for(const auto &pick:notable)if(pick.view==cut->parent)append(pick.paper,snapName(pick.role));
        const auto *definition=viewFor(cut->parent);const auto *layout=layoutFor(cut->parent);const auto display=visibility.find(cut->parent);const auto found=curves.find(cut->parent);
        if(!definition||!layout||display==visibility.end()||found==curves.end())return;
        std::map<quint64,QRectF> boxes;QSet<quint64> visible;
        const auto bounds=[&](quint64 id,const QPainterPath &path,bool shown){
            if(path.isEmpty())return;
            const auto b=path.boundingRect().translated(layout->position);const auto at=boxes.find(id);
            if(at==boxes.end())boxes[id]=b;
            else {const auto a=at->second;at->second=QRectF(QPointF(std::min(a.left(),b.left()),std::min(a.top(),b.top())),QPointF(std::max(a.right(),b.right()),std::max(a.bottom(),b.bottom())));}
            if(shown)visible.insert(id);
        };
        for(std::size_t i=0;i<found->second.size();++i){const auto &path=display->second->curves[i];bounds(found->second[i].featureId,joinedPaths(path.visible,path.hidden),!path.visible.isEmpty()||(layout->showHiddenLines&&!path.hidden.isEmpty()));}
        const auto contours=silhouettes.find(cut->parent);
        if(contours!=silhouettes.end())for(std::size_t i=0;i<contours->second.paths.size();++i){const auto &path=display->second->silhouettes[i];bounds(contours->second.paths[i].featureId,joinedPaths(path.visible,path.hidden),!path.visible.isEmpty()||(layout->showHiddenLines&&!path.hidden.isEmpty()));}
        for(const auto &[id,box]:boxes)if(visible.contains(id)){
            const auto name=std::find_if(model.choices.begin(),model.choices.end(),[&](const auto &c){return c.id==id;});
            append(box.center(),QStringLiteral("Centro ingombro in vista · %1").arg(name==model.choices.end()?QString::number(id):name->name));
        }
    }
    void cutSnapFeedback(const CutPlacement::Point *point){
        if(!cut)return;
        if(!point){cut->snapMark->hide();return;}
        const double zoom=std::max(.01,std::abs(view->transform().m11())),radius=5/zoom;QPainterPath mark;
        if(point->label==QStringLiteral("Punto medio")){mark.moveTo(0,-radius);mark.lineTo(radius,radius);mark.lineTo(-radius,radius);mark.closeSubpath();}
        else if(point->label.startsWith(QStringLiteral("Centro ingombro"))){mark.moveTo(0,-radius);mark.lineTo(radius,0);mark.lineTo(0,radius);mark.lineTo(-radius,0);mark.closeSubpath();}
        else mark.addEllipse(QPointF(),radius,radius);
        cut->snapMark->setPath(mark);cut->snapMark->setPos(point->paper);cut->snapMark->setVisible(true);cut->snapMark->setData(indexRole,point->label);
        cut->snapLabel->setText(point->label+(cut->choices.size()>1?QStringLiteral(" (%1/%2) · Tab").arg(cut->cycle%cut->choices.size()+1).arg(cut->choices.size()):QString()));cut->snapLabel->setScale(1/zoom);cut->snapLabel->setPos(8/zoom,-21/zoom);
    }
    void cutOverlay(){
        if(!cut)return;
        QPainterPath path;const auto a=cut->started?cut->first:cut->cursor,b=cut->cursor;path.moveTo(a);path.lineTo(b);
        path.moveTo(b+QPointF(-2,0));path.lineTo(b+QPointF(2,0));path.moveTo(b+QPointF(0,-2));path.lineTo(b+QPointF(0,2));
        if(cut->started&&QLineF(a,b).length()>1e-6){const auto t=(b-a)/QLineF(a,b).length();const auto n=(cut->reverse?-1:1)*QPointF(t.y(),-t.x());for(auto p:((cut->style==DrawingSectionStyle::HalfRight||cut->style==DrawingSectionStyle::HalfLeft)?QVector<QPointF>{b}:QVector<QPointF>{a,b})){path.moveTo(p-n*7);path.lineTo(p);path.moveTo(p-n*2+t);path.lineTo(p);path.lineTo(p-n*2-t);}}
        cut->item->setPath(path);cut->item->setVisible(true);
    }
    bool cutPointer(QPointF cursor,Qt::KeyboardModifiers modifiers,bool click){
        if(!cut)return false;
        const double zoom=std::abs(view->transform().m11());
        if(QLineF(cursor,cut->raw).length()*zoom>3||modifiers!=cut->modifiers)cut->cycle=0;
        cut->raw=cursor;cut->modifiers=modifiers;cut->choices.clear();
        const bool constrained=cut->started&&(modifiers&Qt::ShiftModifier);const bool horizontal=std::abs(cursor.x()-cut->first.x())>=std::abs(cursor.y()-cut->first.y());
        if(constrained){if(horizontal)cursor.setY(cut->first.y());else cursor.setX(cut->first.x());}
        if(!(modifiers&Qt::AltModifier))for(const auto &point:cut->points){
            if(constrained&&std::abs(horizontal?point.paper.y()-cut->first.y():point.paper.x()-cut->first.x())>1e-8)continue;
            if(QLineF(point.paper,cursor).length()*zoom<=9)cut->choices.push_back(point);
        }
        std::stable_sort(cut->choices.begin(),cut->choices.end(),[&](const auto &a,const auto &b){return QLineF(a.paper,cursor).length()<QLineF(b.paper,cursor).length();});
        if(!cut->choices.empty()){const auto &point=cut->choices[cut->cycle%cut->choices.size()];cursor=point.paper;cutSnapFeedback(&point);}else cutSnapFeedback(nullptr);
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
        if(key==Qt::Key_Tab&&!cut->choices.empty()){++cut->cycle;cutPointer(cut->raw,cut->modifiers,false);return true;}
        if(key==Qt::Key_Clear){cut->item->setVisible(false);cut->choices.clear();cutSnapFeedback(nullptr);return true;}
        return true;
    }
    void drawCutLines(QGraphicsScene *target,const DrawingView &parent,const DrawingViewLayout &layout){
        for(const auto &section:state.views){const auto &r=section.relation;if(r.kind!=DrawingDerivedKind::Section||r.parentId!=parent.id)continue;
            const auto point=[&](QPointF p){return layout.position+QPointF(p.x()*parent.scale,-p.y()*parent.scale);};
            auto a=point(r.cutStart),b=point(r.cutEnd);const auto t=(b-a)/QLineF(a,b).length();const auto n=(r.reverse?-1:1)*QPointF(t.y(),-t.x());const auto *sectionView=viewFor(section.viewId);const bool half=sectionView&&drawingHalfSection(*sectionView);if(!half)a-=t*4;b+=t*4;
            const auto add=[&](QPainterPath path,QPen pen){auto *item=target->addPath(path,pen);item->setZValue(5);item->setData(kindRole,"cuttingLine");item->setData(viewRole,parent.id);item->setData(idRole,section.viewId);item->setAcceptedMouseButtons(Qt::NoButton);};
            QPainterPath center;center.moveTo(a);center.lineTo(b);QPen chain(QColor("#222222"),.2);chain.setDashPattern({12,2,2,2});add(center,chain);
            for(const auto p:(half?QVector<QPointF>{b}:QVector<QPointF>{a,b})){QPainterPath arrow;arrow.moveTo(p-n*8);arrow.lineTo(p);arrow.moveTo(p-n*2.5+t);arrow.lineTo(p);arrow.lineTo(p-n*2.5-t);add(arrow,QPen(QColor("#222222"),.4));
                auto *label=target->addSimpleText(r.label);fontFor(label,.3);label->setBrush(QColor("#222222"));label->setPos(p-n*10+QPointF(1,-2));label->setZValue(6);label->setData(kindRole,"cuttingLabel");label->setData(viewRole,parent.id);label->setData(idRole,section.viewId);label->setAcceptedMouseButtons(Qt::NoButton);}
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
    void styleDialog(){
        if(readOnly)return;
        QDialog dialog(owner);dialog.setObjectName("drawingOptionsDialog");dialog.setWindowTitle(QStringLiteral("Opzioni tavola 2D"));auto *layout=new QVBoxLayout(&dialog);
        auto *tabs=new QTabWidget(&dialog);tabs->setObjectName("drawingOptionsTabs");layout->addWidget(tabs);
        const auto tab=[&](const QString &name){auto *page=new QWidget(tabs);auto *form=new QFormLayout(page);tabs->addTab(page,name);return form;};
        auto *texts=tab(QStringLiteral("Testi"));auto *font=new QFontComboBox(&dialog);font->setObjectName("drawingStyleFont");font->setCurrentFont(drawingFont(state.style));texts->addRow(QStringLiteral("Carattere:"),font);
        const auto size=[&](const char *name,double value,double minimum,double maximum){auto *spin=new NumericInput(&dialog);spin->setObjectName(name);spin->setDecimals(2);spin->setRange(minimum,maximum);spin->setSingleStep(.25);spin->setSuffix(QStringLiteral(" mm sul foglio"));spin->setValue(value);return spin;};
        auto *dimension=size("drawingStyleDimensionHeight",state.style.dimensionHeight,.5,20),*title=size("drawingStyleTitleHeight",state.style.viewTitleHeight,.5,20);texts->addRow(QStringLiteral("Altezza nominale quote:"),dimension);texts->addRow(QStringLiteral("Altezza nominale nomi vista:"),title);
        auto *scope=new QLabel(QStringLiteral("Il carattere si applica a quote, nomi vista e annotazioni. Le note conservano la propria altezza; cartiglio e tabella revisioni mantengono l'impaginazione dedicata."),&dialog);scope->setWordWrap(true);texts->addRow(scope);
        auto *arrows=tab(QStringLiteral("Quote e frecce"));auto *arrow=new QComboBox(&dialog);arrow->setObjectName("drawingStyleArrow");arrow->addItems({"Aperta","Triangolo pieno","Triangolo vuoto","Tratto obliquo"});arrow->setCurrentIndex(int(state.style.arrow));auto *length=size("drawingStyleArrowLength",state.style.arrowLength,.5,20);arrows->addRow(QStringLiteral("Frecce di quote e richiami:"),arrow);arrows->addRow(QStringLiteral("Lunghezza:"),length);
        auto *views=tab(QStringLiteral("Viste"));auto *gap=size("drawingStyleTitleGap",state.style.titleGap,0,100);views->addRow(QStringLiteral("Distanza snap del nome:"),gap);auto *snaps=new QCheckBox(QStringLiteral("Snap del nome negli otto punti intorno alla vista"),&dialog);snaps->setObjectName("drawingStyleTitleSnaps");snaps->setChecked(state.style.titleSnaps);views->addRow(snaps);auto *hint=new QLabel(QStringLiteral("Trascina la geometria per spostare la vista con anteprima. Trascina il nome per spostarlo separatamente; Alt esclude gli snap. Il comando Ripristina posizione nome torna alla posizione automatica."),&dialog);hint->setWordWrap(true);views->addRow(hint);
        auto *preview=new QGraphicsView(&dialog);preview->setObjectName("drawingStylePreview");preview->setMinimumHeight(125);preview->setBackgroundBrush(Qt::white);preview->setRenderHint(QPainter::Antialiasing);QGraphicsScene previewScene;preview->setScene(&previewScene);layout->addWidget(preview);
        const auto candidate=[&]{return DrawingStyle{font->currentFont().family(),dimension->value(),title->value(),length->value(),gap->value(),DrawingArrowStyle(arrow->currentIndex()),snaps->isChecked()};};
        const auto refresh=[&]{const auto style=candidate();previewScene.clear();auto *label=previewScene.addSimpleText(QLocale().toString(20.,'f',0)+QStringLiteral(" ±")+QLocale().toString(.1,'f',2)+QStringLiteral(" mm"));fontFor(label,style,style.dimensionHeight);label->setPos(8,0);auto *name=previewScene.addSimpleText(QStringLiteral("Vista A (1:1)"));fontFor(name,style,style.viewTitleHeight);name->setPos(8,10);previewScene.addLine(0,8,45,8,QPen(Qt::black,.22));for(auto p:{QPointF(0,8),QPointF(45,8)})previewScene.addPath(drawingArrowPath(style,p,QPointF(22,8)),QPen(Qt::black,.22),style.arrow==DrawingArrowStyle::Filled?QBrush(Qt::black):QBrush(Qt::NoBrush));previewScene.setSceneRect(previewScene.itemsBoundingRect().adjusted(-3,-3,3,3));preview->fitInView(previewScene.sceneRect(),Qt::KeepAspectRatio);};
        for(auto *spin:{dimension,title,length,gap})connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&](double){refresh();});
        connect(font,&QFontComboBox::currentFontChanged,&dialog,[&](const QFont &){refresh();});connect(arrow,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int){refresh();});
        auto *error=new QLabel(&dialog);error->setObjectName("drawingOptionsError");error->setWordWrap(true);layout->addWidget(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults,&dialog);layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("Ripristina predefiniti"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annulla"));
        connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&]{const DrawingStyle d;font->setCurrentFont(drawingFont(d));dimension->setValue(d.dimensionHeight);title->setValue(d.viewTitleHeight);length->setValue(d.arrowLength);gap->setValue(d.titleGap);arrow->setCurrentIndex(int(d.arrow));snaps->setChecked(d.titleSnaps);refresh();});
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{const auto result=owner->setDrawingStyle(candidate());error->setText(result);if(result.isEmpty())dialog.accept();});dialog.resize(640,490);QTimer::singleShot(0,&dialog,refresh);dialog.exec();
    }
    void pageDialog(){
        if(readOnly)return;
        QDialog dialog(owner);dialog.setObjectName("drawingPageDialog");dialog.setWindowTitle(QStringLiteral("Formato foglio e cartiglio"));auto *form=new QFormLayout(&dialog);
        auto *title=new QLineEdit(state.title,&dialog);title->setMaxLength(256);title->setObjectName("drawingPageTitle");form->addRow(QStringLiteral("Titolo:"),title);
        auto *format=new QComboBox(&dialog);format->setObjectName("drawingPaperFormat");format->addItems({"A4","A3","A2","A1","A0","Personalizzato"});format->setCurrentText(drawingPaperName(state.size));
        auto *orientation=new QComboBox(&dialog);orientation->setObjectName("drawingPaperOrientation");orientation->addItems({"Orizzontale","Verticale"});orientation->setCurrentIndex(state.size.width()>=state.size.height()?0:1);
        auto *width=new NumericInput(&dialog),*height=new NumericInput(&dialog);width->setObjectName("drawingPaperWidth");height->setObjectName("drawingPaperHeight");for(auto *spin:{width,height}){spin->setRange(50,2000);spin->setDecimals(2);spin->setSuffix(" mm");}width->setValue(state.size.width());height->setValue(state.size.height());
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
        if(std::all_of(selected.begin(),selected.end(),[](const auto *i){return i->data(kindRole).toString()==QLatin1String("annotation");})){
            auto next=state;QSet<QUuid> ids;for(auto *i:selected)ids.insert(i->data(idRole).toUuid());next.annotations.erase(std::remove_if(next.annotations.begin(),next.annotations.end(),[&](const auto &n){return ids.contains(n.id);}),next.annotations.end());report(applyLayout(std::move(next),QStringLiteral("Elimina annotazioni")));return;}
        if(std::all_of(selected.begin(),selected.end(),[](const auto *i){return i->data(kindRole).toString()==QLatin1String("axis");})){
            auto next=state;QSet<QUuid> ids;for(auto *i:selected)ids.insert(i->data(idRole).toUuid());next.axes.erase(std::remove_if(next.axes.begin(),next.axes.end(),[&](const auto &a){return ids.contains(a.id);}),next.axes.end());report(applyLayout(std::move(next),QStringLiteral("Elimina assi")));return;}

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
    action(QStringLiteral("Riallinea sezione alla madre"),"drawingAlignSectionOrientationAction",[this]{impl_->report(alignSectionOrientation(impl_->selectedView()));});
    action(QStringLiteral("Inverti verso sezione"),"drawingReverseSectionAction",[this]{impl_->report(reverseSection(impl_->selectedView()));});
    action(QStringLiteral("Opzioni tavola..."),"drawingOptionsAction",[this]{impl_->styleDialog();});
    action(QStringLiteral("Ripristina posizione nome"),"drawingResetViewTitleAction",[this]{impl_->report(resetViewTitle(impl_->selectedView()));});
    action(QStringLiteral("Formato foglio e cartiglio..."),"drawingPageSettingsAction",[this]{impl_->pageDialog();});
    action(QStringLiteral("Modifica vista..."),"drawingEditViewAction",[this]{impl_->editSelectedView();});
    action(QStringLiteral("Allinea vista..."),"drawingAlignViewAction",[this]{impl_->alignDialog();});
    impl_->hiddenAction=action(QStringLiteral("Mostra linee nascoste"),"drawingHiddenLinesAction",[this]{impl_->report(setHiddenLinesVisible(impl_->selectedView(),impl_->hiddenAction->isChecked()));});impl_->hiddenAction->setCheckable(true);impl_->hiddenAction->setToolTip(QStringLiteral("Mostra/nasconde i tratti coperti nella sola vista selezionata, anche per quotarli."));toolbar->addSeparator();
    action(QStringLiteral("Asse per due punti"),"drawingAxisThroughPointsAction",[this]{impl_->report(beginAxis(DrawingAxisKind::ThroughPoints));});
    action(QStringLiteral("Mediana fra rette parallele"),"drawingAxisBetweenLinesAction",[this]{impl_->report(beginAxis(DrawingAxisKind::BetweenLines));});
    action(QStringLiteral("Proprieta' asse..."),"drawingEditAxisAction",[this]{impl_->editAxisDialog(impl_->selectedAxis());});
    impl_->cylinderAxesAction=action(QStringLiteral("Mostra assi cilindrici"),"drawingCylinderAxesAction",[this]{const auto error=setCylinderAxesVisible(impl_->selectedView(),impl_->cylinderAxesAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});impl_->cylinderAxesAction->setCheckable(true);impl_->cylinderAxesAction->setToolTip(QStringLiteral("Asse proiettato delle superfici cilindriche analitiche, con estremi dalla rifilatura della faccia."));
    impl_->centersAction=action(QStringLiteral("Mostra centri"),"drawingCenterMarksAction",[this]{const auto error=setCenterMarksVisible(impl_->selectedView(),impl_->centersAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});
    impl_->centersAction->setCheckable(true);impl_->centersAction->setIcon(commandIcon("drawingCenterMark"));impl_->centersAction->setIconVisibleInMenu(true);impl_->centersAction->setToolTip(QStringLiteral("Croci sui centri esatti di cerchi, archi ed ellissi della vista selezionata. Le quote usano gli snap geometrici esistenti."));
    impl_->centerLinesAction=action(QStringLiteral("Mostra assi delle coniche"),"drawingCenterLinesAction",[this]{const auto error=setCenterLinesVisible(impl_->selectedView(),impl_->centerLinesAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});
    impl_->centerLinesAction->setCheckable(true);impl_->centerLinesAction->setIcon(commandIcon("drawingCenterLines"));impl_->centerLinesAction->setIconVisibleInMenu(true);impl_->centerLinesAction->setToolTip(QStringLiteral("Assi a tratto-punto di cerchi ed ellissi, con prolungamento di 2 mm sul foglio; segue la geometria e la scala della vista."));
    impl_->shadedAction=action(QStringLiteral("Mostra facce ombreggiate"),"drawingShadedFacesAction",[this]{const auto error=setShadedFacesVisible(impl_->selectedView(),impl_->shadedAction->isChecked());impl_->refreshHiddenAction();impl_->report(error);});impl_->shadedAction->setCheckable(true);impl_->shadedAction->setToolTip(QStringLiteral("Facce e colori del modello nella vista selezionata; bordi e quote restano esatti."));
    action(QStringLiteral("Nota tecnica…"),"drawingAddNoteAction",[this]{impl_->annotationDialog();})->setIcon(commandIcon("drawingNote"));
    action(QStringLiteral("Richiamo associato…"),"drawingAddLeaderAction",[this]{impl_->annotationDialog({},true);})->setIcon(commandIcon("drawingLeader"));
    action(QStringLiteral("Proprieta' annotazione…"),"drawingEditAnnotationAction",[this]{impl_->annotationDialog(impl_->selectedNote());});
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
    impl_->view->drag=[this](QPointF p,Qt::KeyboardModifiers m,int phase){try{return impl_->viewDragPointer(p,m,phase);}catch(const std::exception &e){impl_->cancelViewDrag();impl_->say(QString::fromUtf8(e.what()));return true;}};
    impl_->view->key=[this](int key){if(impl_->viewDrag){if(key==Qt::Key_Escape){impl_->cancelViewDrag();return true;}return false;}return impl_->cut?impl_->cutKey(key):impl_->placement?impl_->placementKey(key):impl_->quoteKey(key);};
    connect(impl_->quoteAction,&QAction::toggled,this,[this](bool checked){impl_->axisInput=-1;impl_->editingAxis={};impl_->quoteType->setEnabled(!impl_->readOnly);if(checked){impl_->cancelViewDrag();impl_->cancelPlacement();}impl_->resetQuote();impl_->view->setCursor(checked?Qt::CrossCursor:Qt::ArrowCursor);impl_->refreshNotable();if(checked)impl_->quoteOverlay();});
    connect(impl_->quoteType,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){if(!impl_->quoteAction->isChecked())impl_->quoteAction->setChecked(true);impl_->quoteOverlay();});
    for(const auto &entry:{qMakePair("drawingOriginReferenceAction",DrawingPointRole::Origin),qMakePair("drawingAxisXReferenceAction",DrawingPointRole::AxisX),qMakePair("drawingAxisYReferenceAction",DrawingPointRole::AxisY)}){
        auto *a=new QAction(commandIcon("viewAxes"),impl_->snapName(entry.second),this);a->setObjectName(entry.first);connect(a,&QAction::triggered,this,[this,role=entry.second]{impl_->addDatum(role);});}
    impl_->view->setDragMode(QGraphicsView::RubberBandDrag);impl_->view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);workspace->setCentralWidget(impl_->view);
    auto *dock=new QDockWidget(QStringLiteral("Albero tavola"),workspace);dock->setObjectName("drawingTreeDock");
    impl_->tree=new QTreeWidget(dock);impl_->tree->setObjectName("drawingTree");impl_->tree->setHeaderLabel(QStringLiteral("Oggetti tavola"));impl_->tree->setMinimumWidth(220);
    impl_->tree->setSelectionMode(QAbstractItemView::SingleSelection);impl_->tree->setContextMenuPolicy(Qt::CustomContextMenu);dock->setWidget(impl_->tree);workspace->addDockWidget(Qt::LeftDockWidgetArea,dock);
    connect(impl_->tree,&QTreeWidget::itemSelectionChanged,this,[this]{impl_->selectionFromTree();});
    connect(impl_->scene,&QGraphicsScene::selectionChanged,this,[this]{impl_->selectionFromScene();});
    connect(impl_->tree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int){if(item->data(0,Qt::UserRole).toString()==QLatin1String("dimension"))impl_->editSelectedDimension();else if(item->data(0,Qt::UserRole).toString()==QLatin1String("annotation"))impl_->annotationDialog(item->data(0,Qt::UserRole+1).toUuid());else if(item->data(0,Qt::UserRole).toString()==QLatin1String("axis"))impl_->editAxisDialog(item->data(0,Qt::UserRole+1).toUuid());else impl_->editSelectedView();});
    connect(impl_->tree,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){
        auto *item=impl_->tree->itemAt(point);if(!item)return;impl_->tree->setCurrentItem(item);CadMenu menu(this);
        if(item->data(0,Qt::UserRole).toString()==QLatin1String("dimension"))menu.addAction(findChild<QAction *>("drawingEditDimensionAction"));
        else if(item->data(0,Qt::UserRole).toString()==QLatin1String("annotation"))menu.addAction(findChild<QAction *>("drawingEditAnnotationAction"));
        else if(item->data(0,Qt::UserRole).toString()==QLatin1String("axis"))menu.addAction(findChild<QAction *>("drawingEditAxisAction"));
        else{menu.addAction(findChild<QAction *>("drawingEditViewAction"));menu.addAction(findChild<QAction *>("drawingAlignViewAction"));menu.addAction(findChild<QAction *>("drawingResetViewTitleAction"));menu.addAction(impl_->hiddenAction);menu.addAction(impl_->shadedAction);menu.addAction(impl_->centersAction);menu.addAction(impl_->centerLinesAction);menu.addAction(impl_->cylinderAxesAction);for(auto name:{"drawingDerivedViewAction","drawingTechnicalSectionAction","drawingDetachViewAction","drawingReleaseAlignmentAction","drawingSectionHatchAction","drawingEditSectionLineAction","drawingAlignSectionOrientationAction","drawingReverseSectionAction"})menu.addAction(findChild<QAction *>(name));}
        menu.addSeparator();menu.addAction(findChild<QAction *>("drawingDeleteSelectionAction"));
        menu.exec(impl_->tree->viewport()->mapToGlobal(point));});
    for(auto name:{"drawingResetViewTitleAction","drawingEditAnnotationAction","drawingEditAxisAction","drawingDetachViewAction","drawingReleaseAlignmentAction","drawingSectionHatchAction","drawingEditSectionLineAction","drawingAlignSectionOrientationAction","drawingReverseSectionAction"})toolbar->removeAction(findChild<QAction *>(name));
    impl_->message=new QLabel(this);impl_->message->setWordWrap(false);impl_->message->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);impl_->message->setMinimumWidth(0);workspace->statusBar()->addWidget(impl_->message,1);workspace->statusBar()->setSizeGripEnabled(false);
    const QPair<const char *,const char *> icons[]={
        {"drawingOptionsAction","drawingOptions"},
        {"drawingResetViewTitleAction","drawingResetViewTitle"},
        {"drawingDerivedViewAction","drawingDerivedView"},
        {"drawingTechnicalSectionAction","drawingTechnicalSection"},
        {"drawingDetachViewAction","drawingDetachView"},
        {"drawingReleaseAlignmentAction","drawingReleaseAlignment"},
        {"drawingSectionHatchAction","drawingSectionHatch"},
        {"drawingEditSectionLineAction","drawingEditSectionLine"},
        {"drawingReverseSectionAction","drawingReverseSection"},
        {"drawingSmartDimensionAction","drawingSmartDimension"},
        {"drawingEditDimensionAction","drawingEditDimension"},
        {"drawingUndoAction","undo"},
        {"drawingRedoAction","redo"},
        {"drawingAlignViewAction","drawingAlignView"},
        {"drawingAddProjectionAction","drawingView"},
        {"drawingAddViewAction","drawingSection"},
        {"drawingEditViewAction","drawingEditView"},
        {"drawingEndpointDimensionAction","dimensionAligned"},
        {"drawingHorizontalDimensionAction","dimension"},
        {"drawingVerticalDimensionAction","dimensionVertical"},
        {"drawingLengthDimensionAction","drawingCurveLength"},
        {"drawingPageSettingsAction","drawingPage"},
        {"drawingHiddenLinesAction","drawingHiddenLines"},
        {"drawingDeleteSelectionAction","delete"},
        {"drawingUpdateModelAction","drawingModelUpdate"},
        {"drawingFitAction","zoomAll"},
        {"drawingAlignSectionOrientationAction","drawingAlignSectionOrientation"},
        {"drawingAxisThroughPointsAction","drawingAxisThroughPoints"},
        {"drawingAxisBetweenLinesAction","drawingAxisBetweenLines"},
        {"drawingEditAxisAction","drawingEditAxis"},
        {"drawingCylinderAxesAction","drawingCylinderAxes"},
        {"drawingShadedFacesAction","drawingShadedFaces"},
        {"drawingEditAnnotationAction","drawingEditAnnotation"},
        {"drawingOriginReferenceAction","drawingReferenceOrigin"},
        {"drawingAxisXReferenceAction","drawingReferenceX"},
        {"drawingAxisYReferenceAction","drawingReferenceY"},
    };
    for(const auto &entry:icons){auto *a=findChild<QAction *>(entry.first);a->setIcon(commandIcon(entry.second));a->setIconVisibleInMenu(true);}
    const QPair<QAction *,QKeySequence> shortcuts[]={{impl_->undoAction,QKeySequence::Undo},{impl_->redoAction,QKeySequence::Redo},{findChild<QAction *>("drawingDeleteSelectionAction"),QKeySequence::Delete}};
    for(const auto &entry:shortcuts){entry.first->setShortcut(entry.second);entry.first->setShortcutContext(Qt::WidgetWithChildrenShortcut);impl_->view->addAction(entry.first);impl_->tree->addAction(entry.first);}
    impl_->pageContext=defaultDrawingPageContext(impl_->state);impl_->calculate(impl_->state,impl_->model);impl_->resetHistory();impl_->savedSheet=impl_->history[0].sheet;impl_->redraw();setReadOnly(!impl_->state.drawing.version.checkinId.isNull());QTimer::singleShot(0,this,[this]{fitSheet();});
}
DrawingSheetEditor::~DrawingSheetEditor(){
    // La scena emette selectionChanged anche mentre distrugge le etichette.
    // Impl deve essere ancora vivo quando si scollegano questi callback.
    disconnect(impl_->scene,nullptr,this,nullptr);disconnect(impl_->tree,nullptr,this,nullptr);
    impl_->cancelViewDrag();impl_->view->pointer={};impl_->view->key={};impl_->view->drag={};
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
    auto next=impl_->state;next.drawing.views.push_back(child);next.views.append(layout);const auto oriented=alignDrawingSectionOrientation(next,child.id);if(!oriented.isEmpty())return oriented;
    const auto error=synchronizeDrawingViews(next);if(!error.isEmpty())return error;
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
    cut->item=impl_->scene->addPath({},QPen(QColor("#1469c0"),.3,Qt::DashLine));cut->item->setZValue(50);cut->item->setData(kindRole,"cuttingPreview");cut->item->setAcceptedMouseButtons(Qt::NoButton);QPen markerPen(QColor("#1469c0"),1.5);markerPen.setCosmetic(true);cut->snapMark=impl_->scene->addPath({},markerPen,QColor(255,255,255,210));cut->snapMark->setZValue(52);cut->snapMark->setData(kindRole,"cuttingSnap");cut->snapMark->setAcceptedMouseButtons(Qt::NoButton);cut->snapMark->hide();
    cut->snapLabel=new QGraphicsSimpleTextItem(cut->snapMark);QFont snapFont("DejaVu Sans");snapFont.setPixelSize(12);cut->snapLabel->setFont(snapFont);cut->snapLabel->setBrush(QColor("#1469c0"));cut->snapLabel->setAcceptedMouseButtons(Qt::NoButton);
    impl_->cut=std::move(cut);impl_->refreshCutPoints();impl_->view->setCursor(Qt::CrossCursor);impl_->view->setFocus();impl_->say(drawingHalfSection(impl_->cut->existing.isNull()?test:*impl_->viewFor(existing))?QStringLiteral("Indica il punto dell’asse della mezza sezione, poi un punto lungo la direzione del taglio. Esc annulla."):QStringLiteral("Primo punto del taglio: snap a estremi, punti medi, centri e ingombri in vista. Tab cambia punto; Alt esclude snap; Esc annulla."));return {};
}
QString DrawingSheetEditor::alignSectionOrientation(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    auto next=impl_->state;const auto error=alignDrawingSectionOrientation(next,id);if(!error.isEmpty())return error;
    const auto *before=impl_->viewFor(id);const auto found=std::find_if(next.drawing.views.begin(),next.drawing.views.end(),[&](const auto &v){return v.id==id;});const auto &after=*found;
    if(sameGeometry(*before,after))return {};
    if(drawingHalfSection(*before))return QStringLiteral("Per riallineare una vecchia mezza sezione, passare prima a Completa e poi ridefinire il lato e l’asse della metà desiderata.");
    const bool swapped=std::abs(dot(before->plane.xDir(),after.plane.yDir()))>1-1e-10;
    const bool parallel=std::abs(dot(before->plane.xDir(),after.plane.xDir()))>1-1e-10;
    for(auto &d:next.drawing.dimensions)if(d.viewId==id){
        const bool components=d.measure==DrawingMeasure::HorizontalDistance||d.measure==DrawingMeasure::VerticalDistance;
        const bool axes=std::any_of(d.references.begin(),d.references.end(),[](const auto &ref){return ref.role==DrawingPointRole::AxisX||ref.role==DrawingPointRole::AxisY;});
        if(!swapped&&!parallel&&(components||axes))return QStringLiteral("Prima del riallineamento obliquo ridefinire le quote orizzontali/verticali o riferite agli assi del piano.");
        if(swapped&&components)d.measure=d.measure==DrawingMeasure::HorizontalDistance?DrawingMeasure::VerticalDistance:DrawingMeasure::HorizontalDistance;
        for(std::size_t i=0;i<d.references.size();++i){auto &ref=d.references[i];if(ref.role!=DrawingPointRole::AxisX&&ref.role!=DrawingPointRole::AxisY)continue;
            const auto oldDirection=ref.role==DrawingPointRole::AxisX?before->plane.xDir():before->plane.yDir();
            if(swapped)ref.role=ref.role==DrawingPointRole::AxisX?DrawingPointRole::AxisY:DrawingPointRole::AxisX;
            const auto newDirection=ref.role==DrawingPointRole::AxisX?after.plane.xDir():after.plane.yDir();if(d.measure==DrawingMeasure::Angle&&dot(oldDirection,newDirection)<0)d.angleSides^=1<<i;
        }
        const auto position=std::find_if(next.dimensions.begin(),next.dimensions.end(),[&](const auto &p){return p.dimensionId==d.id;});if(position!=next.dimensions.end()){
            const auto p=impl_->localPaper(id,position->position);const auto local=after.plane.toLocal(before->plane.toGlobal(Vec3(p.x(),p.y(),0)));position->position=impl_->layoutFor(id)->position+QPointF(local.x()*after.scale,-local.y()*after.scale);
        }
    }
    for(auto &n:next.annotations)if(n.viewId==id&&n.reference){const auto p=impl_->localPaper(id,n.position);const auto local=after.plane.toLocal(before->plane.toGlobal(Vec3(p.x(),p.y(),0)));n.position=impl_->layoutFor(id)->position+QPointF(local.x()*after.scale,-local.y()*after.scale);}
    return impl_->changeViews(std::move(next),QStringLiteral("Riallinea sezione alla madre"));
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
        std::vector<DrawingSectionReference> sources;if(dimension.section.featureId)sources.push_back(dimension.section);
        for(const auto &r:dimension.references){if(r.axisId.isNull()){if(!drawingDatum(r.role))sources.push_back(r.curve);}else if(const auto *axis=impl_->axisFor(r.axisId))for(const auto &support:axis->references)if(!drawingDatum(support.role))sources.push_back(support.curve);}
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
QString DrawingSheetEditor::setDrawingStyle(const DrawingStyle &style){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    auto next=impl_->state;next.style=style;return impl_->applyLayout(std::move(next),QStringLiteral("Opzioni tavola"));
}
QString DrawingSheetEditor::moveViewTitle(const QUuid &id,const QPointF &position){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    const auto *old=impl_->layoutFor(id);if(!old)return QStringLiteral("Vista non trovata.");
    auto next=impl_->state;for(auto &v:next.views)if(v.viewId==id)v.titleOffset=position-old->position;return impl_->applyLayout(std::move(next),QStringLiteral("Sposta nome vista"));
}
QString DrawingSheetEditor::resetViewTitle(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(!impl_->layoutFor(id))return QStringLiteral("Vista non trovata.");
    auto next=impl_->state;for(auto &v:next.views)if(v.viewId==id)v.titleOffset.reset();return impl_->applyLayout(std::move(next),QStringLiteral("Ripristina nome vista"));
}
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
    next.axes.erase(std::remove_if(next.axes.begin(),next.axes.end(),[&](const auto &a){return a.viewId==id;}),next.axes.end());
    next.annotations.erase(std::remove_if(next.annotations.begin(),next.annotations.end(),[&](const auto &n){return n.viewId==id;}),next.annotations.end());
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
DrawingAxisResult DrawingSheetEditor::axisResult(const QUuid &id)const{const auto *axis=impl_->axisFor(id);return axis?impl_->cachedAxis(*axis):DrawingAxisResult{DrawingDimensionStatus::Missing,QStringLiteral("Asse non trovato.")};
    }
QString DrawingSheetEditor::setCylinderAxesVisible(const QUuid &id,bool visible){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *view=impl_->viewFor(id);if(!view||!drawingHasProjection(*view))return QStringLiteral("Scegli una vista proiettata o una sezione completa/mezza.");
    auto next=impl_->state;for(auto &l:next.views)if(l.viewId==id)l.showCylinderAxes=visible;const auto error=impl_->applyLayout(std::move(next),QStringLiteral("Assi cilindrici"));impl_->refreshHiddenAction();return error;
}
QString DrawingSheetEditor::beginAxis(DrawingAxisKind kind,const QUuid &existing){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(kind!=DrawingAxisKind::ThroughPoints&&kind!=DrawingAxisKind::BetweenLines)return QStringLiteral("Tipo di asse non valido.");
    const auto *old=impl_->axisFor(existing);if(!existing.isNull()&&!old)return QStringLiteral("Asse da ridefinire non trovato.");
    impl_->quoteAction->setChecked(false);impl_->quoteAction->setChecked(true);impl_->axisInput=int(kind);impl_->editingAxis=existing;if(old)impl_->quoteView=old->viewId;impl_->quoteType->setEnabled(false);impl_->quoteOverlay();impl_->view->setFocus();return {};
}
QString DrawingSheetEditor::addAxis(DrawingAxis axis){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(impl_->axisFor(axis.id))return QStringLiteral("Identita' dell'asse gia' usata.");
    const auto *view=impl_->viewFor(axis.viewId);if(!view)return QStringLiteral("Vista dell'asse non trovata.");
    const auto result=evaluateDrawingAxis(axis,*view,impl_->model.snapshot,curves(axis.viewId));if(result.status!=DrawingDimensionStatus::Ready)return result.message;
    auto next=impl_->state;next.axes.append(axis);const auto error=impl_->applyLayout(std::move(next),QStringLiteral("Inserisci asse associato"));if(error.isEmpty())impl_->axisCache[axis.id]={axis,result};return error;
}
QString DrawingSheetEditor::updateAxis(DrawingAxis axis){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *old=impl_->axisFor(axis.id);if(!old||axis.viewId!=old->viewId)return QStringLiteral("Asse o vista da modificare non validi.");
    if(axis.kind!=old->kind||!sameDimension(drawingAxisSupport(axis),drawingAxisSupport(*old))){const auto result=evaluateDrawingAxis(axis,*impl_->viewFor(axis.viewId),impl_->model.snapshot,curves(axis.viewId));if(result.status!=DrawingDimensionStatus::Ready)return result.message;
    }
    auto next=impl_->state;for(auto &a:next.axes)if(a.id==axis.id)a=axis;return impl_->applyLayout(std::move(next),QStringLiteral("Modifica asse associato"));
}
QString DrawingSheetEditor::removeAxis(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(!impl_->axisFor(id))return QStringLiteral("Asse non trovato.");
    auto next=impl_->state;next.axes.erase(std::remove_if(next.axes.begin(),next.axes.end(),[&](const auto &a){return a.id==id;
    }),next.axes.end());return impl_->applyLayout(std::move(next),QStringLiteral("Elimina asse"));
}
DrawingResolvedAttachment DrawingSheetEditor::annotationResult(const QUuid &id)const{
    const auto *note=impl_->noteFor(id);if(note)return impl_->noteResult(*note);
    DrawingResolvedAttachment result;result.status=DrawingDimensionStatus::Missing;result.message=QStringLiteral("Annotazione non trovata.");return result;
}
QString DrawingSheetEditor::beginAnnotation(bool leader,const QUuid &existing){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    const auto *old=impl_->noteFor(existing);if(!existing.isNull()&&!old)return QStringLiteral("Annotazione da ridefinire non trovata.");
    DrawingAnnotation note=old?*old:DrawingAnnotation();note.reference.reset();note.viewId={};
    impl_->quoteAction->setChecked(false);impl_->quoteAction->setChecked(true);impl_->pendingNote=note;impl_->noteLeader=leader;impl_->editingNote=existing;impl_->quoteType->setEnabled(false);impl_->quoteOverlay();impl_->view->setFocus();return {};
}
QString DrawingSheetEditor::addAnnotation(DrawingAnnotation note){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(impl_->noteFor(note.id))return QStringLiteral("Identita' annotazione gia' usata.");
    if(note.reference){const auto result=impl_->noteResult(note);if(result.status!=DrawingDimensionStatus::Ready||!result.isPoint)return result.message.isEmpty()?QStringLiteral("Richiamo privo di un punto valido."):result.message;}
    auto next=impl_->state;next.annotations.append(note);return impl_->applyLayout(std::move(next),QStringLiteral("Inserisci annotazione"));
}
QString DrawingSheetEditor::updateAnnotation(DrawingAnnotation note){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(!impl_->noteFor(note.id))return QStringLiteral("Annotazione non trovata.");
    // Le modifiche del testo conservano anche un aggancio orfano, visibile come da verificare.
    auto next=impl_->state;for(auto &n:next.annotations)if(n.id==note.id)n=note;return impl_->applyLayout(std::move(next),QStringLiteral("Modifica annotazione"));
}
QString DrawingSheetEditor::moveAnnotation(const QUuid &id,const QPointF &position){
    const auto *old=impl_->noteFor(id);if(!old)return QStringLiteral("Annotazione non trovata.");auto note=*old;note.position=position;return updateAnnotation(note);
}
QString DrawingSheetEditor::removeAnnotation(const QUuid &id){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(!impl_->noteFor(id))return QStringLiteral("Annotazione non trovata.");
    auto next=impl_->state;next.annotations.erase(std::remove_if(next.annotations.begin(),next.annotations.end(),[&](const auto &n){return n.id==id;}),next.annotations.end());return impl_->applyLayout(std::move(next),QStringLiteral("Elimina annotazione"));
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
        int missingAxes=0;for(const auto &a:impl_->state.axes)if(impl_->cachedAxis(a).status!=DrawingDimensionStatus::Ready)++missingAxes;if(missingAxes)prepared.warnings.append(QStringLiteral("%1 assi da verificare.").arg(missingAxes));
        int missingNotes=0;for(const auto &n:impl_->state.annotations)if(impl_->noteResult(n).status!=DrawingDimensionStatus::Ready)++missingNotes;
        if(missingNotes)prepared.warnings.append(QStringLiteral("%1 richiami da verificare.").arg(missingNotes));
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
    const auto layout=std::find_if(impl_->state.dimensions.begin(),impl_->state.dimensions.end(),[&](const auto &p){return p.dimensionId==dimension.id;});
    return updateDimension(dimension,layout==impl_->state.dimensions.end()?DrawingDimensionPresentation{}:layout->presentation);
}
QString DrawingSheetEditor::updateDimension(DrawingDimension dimension,DrawingDimensionPresentation presentation){
    if(impl_->computing)return QStringLiteral("Attendere il calcolo della vista in corso.");
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{auto next=impl_->state;auto found=std::find_if(next.drawing.dimensions.begin(),next.drawing.dimensions.end(),[&](const auto &d){return d.id==dimension.id;});
        if(found==next.drawing.dimensions.end()||found->viewId!=dimension.viewId)return QStringLiteral("Quota non trovata nella vista.");
        *found=dimension;
        auto layout=std::find_if(next.dimensions.begin(),next.dimensions.end(),[&](const auto &p){return p.dimensionId==dimension.id;});
        if(layout==next.dimensions.end())return QStringLiteral("Presentazione quota assente.");
        layout->presentation=std::move(presentation);
        QByteArray bytes;const auto error=encodeDrawingSheet(next,bytes);if(!error.isEmpty())return error;
        const auto value=impl_->measured(next,dimension,impl_->curves.at(dimension.viewId));if(value.status==DrawingDimensionStatus::Invalid)return value.message;
        if(value.status==DrawingDimensionStatus::Ready){const auto formatted=drawingDimensionLabel(dimension,value.value,layout->presentation);if(!formatted.problem.isEmpty())return formatted.problem;}
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
        for(auto &dimension:next.drawing.dimensions){dimension.section.featureId=model.replacements.value(dimension.section.featureId,dimension.section.featureId);for(auto &ref:dimension.references)if(ref.axisId.isNull()&&!drawingDatum(ref.role))ref.curve.featureId=model.replacements.value(ref.curve.featureId,ref.curve.featureId);}
        for(auto &axis:next.axes)for(auto &ref:axis.references)if(ref.axisId.isNull()&&!drawingDatum(ref.role))ref.curve.featureId=model.replacements.value(ref.curve.featureId,ref.curve.featureId);
        for(auto &n:next.annotations)if(n.reference&&n.reference->axisId.isNull())n.reference->curve.featureId=model.replacements.value(n.reference->curve.featureId,n.reference->curve.featureId);
        impl_->calculate(std::move(next),std::move(model));impl_->cancelPlacement();impl_->pageContext=defaultDrawingPageContext(impl_->state);impl_->resetHistory();impl_->notify();impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}

void DrawingSheetEditor::setChangedCallback(std::function<void(const DrawingSheet &)> callback){impl_->changed=std::move(callback);}
void DrawingSheetEditor::setModelProvider(std::function<DrawingEditorModel()> provider){impl_->provider=std::move(provider);impl_->updateAction->setEnabled(bool(impl_->provider)&&!impl_->readOnly);}
bool DrawingSheetEditor::isReadOnly()const{return impl_->readOnly;}
void DrawingSheetEditor::setReadOnly(bool readOnly){
    if(impl_->computing)return;
    impl_->readOnly=readOnly||!impl_->state.drawing.version.checkinId.isNull();
    if(impl_->readOnly){impl_->cancelViewDrag();impl_->cancelPlacement();impl_->quoteAction->setChecked(false);}
    impl_->quoteType->setEnabled(!impl_->readOnly);
    for(const auto &name:{"drawingAddNoteAction","drawingAddLeaderAction","drawingEditAnnotationAction","drawingAxisThroughPointsAction","drawingAxisBetweenLinesAction","drawingEditAxisAction","drawingDerivedViewAction","drawingTechnicalSectionAction","drawingDetachViewAction","drawingReleaseAlignmentAction","drawingSectionHatchAction","drawingEditSectionLineAction","drawingAlignSectionOrientationAction","drawingReverseSectionAction","drawingOptionsAction","drawingResetViewTitleAction","drawingPageSettingsAction","drawingSmartDimensionAction","drawingEditDimensionAction","drawingOriginReferenceAction","drawingAxisXReferenceAction","drawingAxisYReferenceAction","drawingAddProjectionAction","drawingAddViewAction","drawingEditViewAction","drawingAlignViewAction","drawingEndpointDimensionAction","drawingHorizontalDimensionAction","drawingVerticalDimensionAction","drawingLengthDimensionAction","drawingDeleteSelectionAction"})
        findChild<QAction *>(QString::fromLatin1(name))->setEnabled(!impl_->readOnly);
    impl_->refreshHiddenAction();impl_->refreshHistoryActions();impl_->updateAction->setEnabled(!impl_->readOnly&&bool(impl_->provider));
    for(auto *item:impl_->scene->items())if(dynamic_cast<MoveLabel *>(item)||dynamic_cast<MoveNote *>(item))item->setFlag(QGraphicsItem::ItemIsMovable,!impl_->readOnly&&item->data(kindRole).toString()!=QLatin1String("view"));
}
void DrawingSheetEditor::fitSheet(){impl_->view->fitInView(QRectF(QPointF(-5,-5),impl_->state.size+QSizeF(10,10)),Qt::KeepAspectRatio);}
}
