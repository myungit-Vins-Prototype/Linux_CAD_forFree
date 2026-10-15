#include "cad_drawing_editor.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_tessellate.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
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
    void wheelEvent(QWheelEvent *event)override{
        const double factor=event->angleDelta().y()>0?1.15:1/1.15;
        if(transform().m11()*factor>=0.1&&transform().m11()*factor<=100)scale(factor,factor);
        event->accept();
    }
};
struct SectionItem final:QGraphicsPathItem{
    using QGraphicsPathItem::QGraphicsPathItem;
    QRectF boundingRect()const override{return path().boundingRect().adjusted(-0.7,-0.7,0.7,0.7);}
    QPainterPath shape()const override{QPainterPathStroker stroke;stroke.setWidth(1.2);return stroke.createStroke(path());}
    void paint(QPainter *painter,const QStyleOptionGraphicsItem *,QWidget *)override{
        painter->setPen(QPen(isSelected()?QColor("#1469c0"):QColor("#222222"),isSelected()?0.65:0.3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);painter->drawPath(path());
    }
};
struct MoveLabel final:QGraphicsSimpleTextItem{
    QPointF pressPosition;
    std::function<void(QPointF,QPointF)> moved;
    explicit MoveLabel(const QString &text):QGraphicsSimpleTextItem(text){setFlags(ItemIsMovable|ItemIsSelectable);setZValue(8);}
    void mousePressEvent(QGraphicsSceneMouseEvent *event)override{pressPosition=pos();QGraphicsSimpleTextItem::mousePressEvent(event);}
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
    std::map<QUuid,std::vector<DrawingSection>> curves;
    std::map<QUuid,DrawingDimensionResult> results;
    std::function<void(const DrawingSheet &)> changed;
    std::function<DrawingEditorModel()> provider;
    bool rebuilding=false;
    bool readOnly=false;
    Impl(DrawingSheetEditor *widget,DrawingSheet sheet,DrawingEditorModel source):owner(widget),state(std::move(sheet)),model(std::move(source)){}
    void say(const QString &text){message->setText(text);}
    void report(const QString &error){if(!error.isEmpty())QMessageBox::warning(owner,QStringLiteral("Tavola 2D"),error);}
    void notify(){if(changed)changed(state);}
    DrawingModelSnapshot sourceFor(const DrawingViewLayout &layout)const{
        DrawingModelSnapshot source;source.version=model.snapshot.version;
        for(const auto &body:model.snapshot.bodies)if(layout.features.contains(body.featureId))source.bodies.push_back(body);
        return source;
    }
    const DrawingViewLayout *layoutFor(const QUuid &id)const{
        for(const auto &layout:state.views)if(layout.viewId==id)return &layout;
        return nullptr;
    }
    const DrawingView *viewFor(const QUuid &id)const{
        for(const auto &definition:state.drawing.views)if(definition.id==id)return &definition;
        return nullptr;
    }
    void calculate(){
        std::map<QUuid,std::vector<DrawingSection>> prepared;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);if(!layout)continue;
            std::vector<DrawingSection> sections;const auto error=sectionDrawingView(sourceFor(*layout),definition,sections);
            if(!error.isEmpty())throw std::runtime_error(error.toStdString());
            for(const auto &piece:sections)sectionPath(definition,*layout,piece);
            prepared.emplace(definition.id,std::move(sections));}
        std::map<QUuid,DrawingDimensionResult> measured;
        for(const auto &dimension:state.drawing.dimensions)measured[dimension.id]=evaluateDrawingDimension(state.drawing,model.snapshot,dimension.id);
        curves=std::move(prepared);results=std::move(measured);
    }
    void queueRedraw(){QTimer::singleShot(0,owner,[this]{redraw();});}
    const DrawingSection *resolved(const DrawingDimension &dimension)const{
        const auto *definition=viewFor(dimension.viewId);const auto it=curves.find(dimension.viewId);if(!definition||it==curves.end())return nullptr;
        const auto source=std::find_if(model.snapshot.bodies.begin(),model.snapshot.bodies.end(),[&](const auto &body){return body.featureId==dimension.section.featureId;});
        if(source==model.snapshot.bodies.end()||!source->names)return nullptr;
        const auto faces=source->names->find(EntityKind::Face,dimension.section.faceName.toStdString());std::vector<const DrawingSection *> candidates;
        for(const auto &piece:it->second)if(piece.featureId==dimension.section.featureId&&std::find(faces.begin(),faces.end(),piece.sourceFace.index)!=faces.end())candidates.push_back(&piece);
        if(candidates.empty())return nullptr;
        if(candidates.size()==1&&!dimension.section.requiresWitness)return candidates.front();
        const auto witness=definition->plane.toGlobal(Vec3(dimension.section.witness.x(),dimension.section.witness.y(),0));const DrawingSection *chosen=nullptr;
        for(const auto *piece:candidates)if(projectPoint(*piece->curve,witness,piece->range).distance<=definition->tolerance){if(chosen)return nullptr;chosen=piece;}
        return chosen;
    }
    void line(const QPointF &a,const QPointF &b,QColor color=QColor("#27445e")){
        auto *item=scene->addLine(QLineF(a,b),QPen(color,0.22));item->setZValue(4);
    }
    void arrow(const QPointF &point,const QPointF &toward){
        auto direction=toward-point;const double size=std::hypot(direction.x(),direction.y());if(size<1e-9)return;direction/=size;
        const QPointF normal(-direction.y(),direction.x());line(point,point+2.0*direction+0.65*normal);line(point,point+2.0*direction-0.65*normal);
    }
    void redraw(){
        rebuilding=true;scene->clear();
        scene->setSceneRect(-8,-8,state.size.width()+16,state.size.height()+16);
        scene->addRect(QRectF(QPointF(0,0),state.size),QPen(QColor("#b5bdc4"),0.25),QBrush(Qt::white))->setZValue(-20);
        scene->addRect(10,10,state.size.width()-20,state.size.height()-20,QPen(QColor("#c3c9cf"),0.18))->setZValue(-19);
        auto *title=scene->addSimpleText(state.title+QStringLiteral("  |  Quote in mm"));fontFor(title,0.28);title->setPos(14,state.size.height()-18);
        int missing=0;
        for(const auto &definition:state.drawing.views){const auto *layout=layoutFor(definition.id);const auto found=curves.find(definition.id);
            if(!layout||found==curves.end())continue;
            QRectF bounds;
            for(std::size_t i=0;i<found->second.size();++i){auto *item=new SectionItem(sectionPath(definition,*layout,found->second[i]));
                item->setFlags(QGraphicsItem::ItemIsSelectable);item->setData(kindRole,"section");item->setData(viewRole,definition.id);item->setData(indexRole,qulonglong(i));scene->addItem(item);
                bounds=bounds.isNull()?item->path().boundingRect():bounds.united(item->path().boundingRect());}
            auto *label=new MoveLabel(layout->title+QStringLiteral("  (%1:1)").arg(definition.scale,0,'g',4));fontFor(label);label->setBrush(QColor("#35495c"));
            label->setPos(bounds.isNull()?layout->position:bounds.bottomLeft()+QPointF(0,6));label->setData(kindRole,"view");label->setData(viewRole,definition.id);
            label->setFlag(QGraphicsItem::ItemIsMovable,!readOnly);
            label->setToolTip(QStringLiteral("Trascina il titolo per spostare la vista."));
            const auto id=definition.id;label->moved=[this,id](QPointF before,QPointF after){const auto delta=after-before;
                for(auto &layout:state.views)if(layout.viewId==id)layout.position+=delta;
                for(auto &placement:state.dimensions){const auto dimension=std::find_if(state.drawing.dimensions.begin(),state.drawing.dimensions.end(),[&](const auto &d){return d.id==placement.dimensionId;});
                    if(dimension!=state.drawing.dimensions.end()&&dimension->viewId==id)placement.position+=delta;}
                notify();queueRedraw();};scene->addItem(label);
        }
        for(const auto &dimension:state.drawing.dimensions){
            const auto placement=std::find_if(state.dimensions.begin(),state.dimensions.end(),[&](const auto &p){return p.dimensionId==dimension.id;});
            if(placement==state.dimensions.end())continue;
            const auto result=results.find(dimension.id);
            const bool ready=result!=results.end()&&result->second.status==DrawingDimensionStatus::Ready;
            const QString text=ready?(dimension.measure==DrawingMeasure::SectionLength?QStringLiteral("L = %1 mm"):QStringLiteral("%1 mm")).arg(result->second.value,0,'f',2):QStringLiteral("Riferimento da verificare");
            auto *label=new MoveLabel(text);fontFor(label);label->setPos(placement->position);label->setBrush(ready?QColor("#27445e"):QColor("#c42b32"));
            label->setData(kindRole,"dimension");label->setData(idRole,dimension.id);label->setFlag(QGraphicsItem::ItemIsMovable,!readOnly);
            if(!ready){++missing;if(result!=results.end())label->setToolTip(result->second.message);}
            const auto id=dimension.id;label->moved=[this,id](QPointF,QPointF position){for(auto &p:state.dimensions)if(p.dimensionId==id)p.position=position;notify();queueRedraw();};scene->addItem(label);
            scene->addRect(label->sceneBoundingRect().adjusted(-0.6,-0.3,0.6,0.3),QPen(Qt::NoPen),QBrush(Qt::white))->setZValue(6);
            if(!ready)continue;
            const auto *piece=resolved(dimension);const auto *definition=viewFor(dimension.viewId);const auto *layout=layoutFor(dimension.viewId);
            if(!piece||!definition||!layout)continue;
            const auto a=paperPoint(*definition,*layout,piece->curve->point(piece->range.lo)),b=paperPoint(*definition,*layout,piece->curve->point(piece->range.hi));
            const auto labelCenter=label->sceneBoundingRect().center();const auto difference=b-a;const double length=std::hypot(difference.x(),difference.y());
            if(dimension.measure==DrawingMeasure::EndpointDistance&&length>1e-8){const QPointF tangent=difference/length,normal(-tangent.y(),tangent.x());
                const double offset=QPointF::dotProduct(labelCenter-(a+b)/2,normal);const auto p=a+offset*normal,q=b+offset*normal;
                line(a,p);line(b,q);line(p,q);arrow(p,q);arrow(q,p);
            }else{const auto midpoint=paperPoint(*definition,*layout,piece->curve->point(0.5*(piece->range.lo+piece->range.hi)));line(midpoint,labelCenter);arrow(midpoint,labelCenter);}
        }
        rebuilding=false;
        say(missing?QStringLiteral("%1 quote richiedono verifica. Seleziona il testo per leggere il motivo.").arg(missing):QStringLiteral("Seleziona una curva e inserisci una quota. Trascina titoli e testi per posizionarli."));
    }
    void addViewDialog(){
        if(readOnly)return;
        QDialog dialog(owner);dialog.setWindowTitle(QStringLiteral("Inserisci sezione"));dialog.setObjectName(QStringLiteral("drawingViewDialog"));auto *form=new QFormLayout(&dialog);
        auto *title=new QLineEdit(QStringLiteral("Sezione %1").arg(state.views.size()+1),&dialog);title->setObjectName("drawingViewTitle");title->setMaxLength(256);
        auto *plane=new QComboBox(&dialog);plane->addItems({"XY","XZ","YZ"});plane->setObjectName("drawingViewPlane");
        auto *depth=new QDoubleSpinBox(&dialog);depth->setRange(-1e8,1e8);depth->setDecimals(6);depth->setSuffix(" mm");depth->setObjectName("drawingViewDepth");
        auto *scale=new QDoubleSpinBox(&dialog);scale->setDecimals(4);scale->setRange(0.001,1000);scale->setValue(1);scale->setObjectName("drawingViewScale");
        auto *bodies=new QListWidget(&dialog);bodies->setObjectName("drawingViewBodies");bodies->setMinimumHeight(100);
        for(const auto &choice:model.choices){auto *item=new QListWidgetItem(choice.name,bodies);item->setData(Qt::UserRole,QVariant::fromValue(choice.id));item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);}
        Box bounds;for(const auto &body:model.snapshot.bodies)for(auto face:body.body->faces())bounds.add(faceBox(*body.body,face));
        const auto normalFor=[](int index){return index==0?Vec3(0,0,1):index==1?Vec3(0,-1,0):Vec3(1,0,0);};
        const Vec3 center=bounds.isEmpty()?Vec3():0.5*(bounds.lo+bounds.hi);
        depth->setValue(dot(center,normalFor(0)));connect(plane,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[=](int index){depth->setValue(dot(center,normalFor(index)));});
        form->addRow(QStringLiteral("Titolo:"),title);form->addRow(QStringLiteral("Piano:"),plane);form->addRow(QStringLiteral("Posizione lungo la normale:"),depth);
        form->addRow(QStringLiteral("Scala foglio/modello:"),scale);form->addRow(QStringLiteral("Corpi e superfici:"),bodies);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        DrawingView definition;const auto normal=normalFor(plane->currentIndex());definition.plane=Frame3(depth->value()*normal,normal,plane->currentIndex()==2?Vec3(0,1,0):Vec3(1,0,0));definition.scale=scale->value();
        DrawingViewLayout layout;layout.viewId=definition.id;layout.title=title->text().trimmed();layout.position=QPointF(state.size.width()/2,state.size.height()/2);
        for(int row=0;row<bodies->count();++row)if(bodies->item(row)->checkState()==Qt::Checked)layout.features.append(bodies->item(row)->data(Qt::UserRole).toULongLong());
        // Centra la geometria, mantenendo il piano riferito al modello.
        const auto local=definition.plane.toLocal(center);layout.position-=QPointF(local.x()*definition.scale,-local.y()*definition.scale);
        report(owner->insertView(definition,layout));
    }
    void removeSelection(){
        if(readOnly)return;
        for(auto *item:scene->selectedItems()){
            if(item->data(kindRole).toString()==QLatin1String("dimension")){
                const auto id=item->data(idRole).toUuid();auto &dimensions=state.drawing.dimensions;
                dimensions.erase(std::remove_if(dimensions.begin(),dimensions.end(),[&](const auto &d){return d.id==id;}),dimensions.end());
                state.dimensions.erase(std::remove_if(state.dimensions.begin(),state.dimensions.end(),[&](const auto &p){return p.dimensionId==id;}),state.dimensions.end());results.erase(id);notify();redraw();return;
            }
        }say(QStringLiteral("Seleziona il testo della quota da eliminare."));
    }
};
DrawingSheetEditor::DrawingSheetEditor(DrawingSheet sheet,DrawingEditorModel model,QWidget *parent):QWidget(parent),impl_(std::make_unique<Impl>(this,std::move(sheet),std::move(model))){
    setObjectName("drawingSheetEditor");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);auto *toolbar=new QToolBar(this);layout->addWidget(toolbar);
    const auto action=[&](const QString &name,const char *object,std::function<void()> callback){auto *a=toolbar->addAction(name);a->setObjectName(object);connect(a,&QAction::triggered,this,std::move(callback));return a;};
    action(QStringLiteral("Inserisci sezione..."),"drawingAddViewAction",[this]{impl_->addViewDialog();});
    action(QStringLiteral("Quota estremi"),"drawingEndpointDimensionAction",[this]{impl_->report(addSelectedDimension(DrawingMeasure::EndpointDistance));});
    action(QStringLiteral("Lunghezza curva"),"drawingLengthDimensionAction",[this]{impl_->report(addSelectedDimension(DrawingMeasure::SectionLength));});
    action(QStringLiteral("Elimina quota"),"drawingDeleteDimensionAction",[this]{impl_->removeSelection();});
    impl_->updateAction=action(QStringLiteral("Aggiorna dal modello"),"drawingUpdateModelAction",[this]{if(impl_->provider){try{impl_->report(updateModel(impl_->provider()));}catch(const std::exception &error){impl_->report(QString::fromUtf8(error.what()));}}});
    impl_->updateAction->setEnabled(false);action(QStringLiteral("Adatta foglio"),"drawingFitAction",[this]{fitSheet();});
    impl_->scene=new QGraphicsScene(this);impl_->view=new SheetCanvas(impl_->scene,this);impl_->view->setObjectName("drawingCanvas");impl_->view->setRenderHint(QPainter::Antialiasing);impl_->view->setBackgroundBrush(QColor("#dfe4e8"));
    impl_->view->setDragMode(QGraphicsView::RubberBandDrag);impl_->view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);layout->addWidget(impl_->view,1);
    impl_->message=new QLabel(this);impl_->message->setWordWrap(true);layout->addWidget(impl_->message);
    impl_->calculate();impl_->redraw();setReadOnly(!impl_->state.drawing.version.checkinId.isNull());QTimer::singleShot(0,this,[this]{fitSheet();});
}
DrawingSheetEditor::~DrawingSheetEditor()=default;
const DrawingSheet &DrawingSheetEditor::sheet()const{return impl_->state;}
QGraphicsView *DrawingSheetEditor::canvas()const{return impl_->view;}
const std::vector<DrawingSection> &DrawingSheetEditor::sections(const QUuid &viewId)const{static const std::vector<DrawingSection> empty;const auto found=impl_->curves.find(viewId);return found==impl_->curves.end()?empty:found->second;}
DrawingDimensionResult DrawingSheetEditor::dimensionResult(const QUuid &id)const{const auto found=impl_->results.find(id);return found==impl_->results.end()?DrawingDimensionResult{}:found->second;}
QString DrawingSheetEditor::insertView(DrawingView definition,DrawingViewLayout layout){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{
        auto next=impl_->state;next.drawing.views.push_back(definition);next.views.append(layout);QByteArray validation;const auto valid=encodeDrawingSheet(next,validation);if(!valid.isEmpty())return valid;
        std::vector<DrawingSection> pieces;const auto error=sectionDrawingView(impl_->sourceFor(layout),definition,pieces);if(!error.isEmpty())return error;
        if(pieces.empty())return QStringLiteral("Il piano non interseca i corpi selezionati. Modificare posizione o piano.");
        for(const auto &piece:pieces)sectionPath(definition,layout,piece);
        impl_->state=std::move(next);impl_->curves[definition.id]=std::move(pieces);impl_->notify();impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::addSelectedDimension(DrawingMeasure measure){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    try{
        QGraphicsItem *selected=nullptr;for(auto *item:impl_->scene->selectedItems())if(item->data(kindRole).toString()==QLatin1String("section")){if(selected)return QStringLiteral("Seleziona una sola curva da quotare.");selected=item;}
        if(!selected)return QStringLiteral("Seleziona una curva della sezione sul foglio.");
        const auto id=selected->data(viewRole).toUuid();const auto index=selected->data(indexRole).toULongLong();const auto *definition=impl_->viewFor(id);const auto *layout=impl_->layoutFor(id);const auto found=impl_->curves.find(id);
        if(!definition||!layout||found==impl_->curves.end()||index>=found->second.size())return QStringLiteral("Selezione della vista non piu' valida.");
        const auto &piece=found->second[index];auto dimension=dimensionOfSection(*definition,piece,measure);auto next=impl_->state;next.drawing.dimensions.push_back(dimension);
        auto anchor=paperPoint(*definition,*layout,piece.curve->point(0.5*(piece.range.lo+piece.range.hi)))+QPointF(0,-10);
        next.dimensions.append({dimension.id,anchor});const auto value=evaluateDrawingDimension(next.drawing,impl_->model.snapshot,dimension.id);
        if(value.status!=DrawingDimensionStatus::Ready)return value.message;
        QFont font("DejaVu Sans");font.setPixelSize(12);
        const auto label=(measure==DrawingMeasure::SectionLength?QStringLiteral("L = %1 mm"):QStringLiteral("%1 mm")).arg(value.value,0,'f',2);
        next.dimensions.back().position.rx()-=0.125*QFontMetricsF(font).horizontalAdvance(label);
        impl_->state=std::move(next);impl_->results[dimension.id]=value;impl_->notify();impl_->redraw();return {};
    }catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
QString DrawingSheetEditor::updateModel(DrawingEditorModel model){
    if(impl_->readOnly)return QStringLiteral("La tavola e' in sola lettura.");
    if(model.snapshot.version.documentId!=impl_->state.drawing.model.documentId)return QStringLiteral("La tavola appartiene a un altro modello.");
    auto oldState=impl_->state;auto oldModel=impl_->model;
    try{
        const auto error=retargetDrawingModel(impl_->state.drawing,model.snapshot.version);if(!error.isEmpty())return error;
        for(auto &layout:impl_->state.views){QVector<quint64> features;for(auto id:layout.features){const auto current=model.replacements.value(id,id);if(!features.contains(current))features.append(current);}layout.features=features;}
        for(auto &dimension:impl_->state.drawing.dimensions)dimension.section.featureId=model.replacements.value(dimension.section.featureId,dimension.section.featureId);
        impl_->model=std::move(model);impl_->calculate();impl_->notify();impl_->redraw();return {};
    }catch(const std::exception &error){impl_->state=std::move(oldState);impl_->model=std::move(oldModel);return QString::fromUtf8(error.what());}
}
void DrawingSheetEditor::setChangedCallback(std::function<void(const DrawingSheet &)> callback){impl_->changed=std::move(callback);}
void DrawingSheetEditor::setModelProvider(std::function<DrawingEditorModel()> provider){impl_->provider=std::move(provider);impl_->updateAction->setEnabled(bool(impl_->provider)&&!impl_->readOnly);}
bool DrawingSheetEditor::isReadOnly()const{return impl_->readOnly;}
void DrawingSheetEditor::setReadOnly(bool readOnly){
    impl_->readOnly=readOnly||!impl_->state.drawing.version.checkinId.isNull();
    for(const auto &name:{"drawingAddViewAction","drawingEndpointDimensionAction","drawingLengthDimensionAction","drawingDeleteDimensionAction"})
        findChild<QAction *>(QString::fromLatin1(name))->setEnabled(!impl_->readOnly);
    impl_->updateAction->setEnabled(!impl_->readOnly&&bool(impl_->provider));
    for(auto *item:impl_->scene->items())if(dynamic_cast<MoveLabel *>(item))item->setFlag(QGraphicsItem::ItemIsMovable,!impl_->readOnly);
}
void DrawingSheetEditor::fitSheet(){impl_->view->fitInView(QRectF(QPointF(-5,-5),impl_->state.size+QSizeF(10,10)),Qt::KeepAspectRatio);}
}
