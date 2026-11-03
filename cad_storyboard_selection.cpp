#include "cad_storyboard_selection.h"
#include "forgeCad2026_storyboard.h"
#include "forgeCad2026_viewport.h"
#include "cad_datum.h"
#include <QtWidgets>

Q_DECLARE_METATYPE(GeometryRef)

namespace ForgeCad {
namespace {
constexpr int referenceRole=Qt::UserRole+12;
constexpr int generatedRole=Qt::UserRole+13;
constexpr int indicatorRole=Qt::UserRole+14;

class StoryboardSelection final : public QObject {
    QMainWindow *window_;
    QPointer<CadViewport> viewport_;
    QPointer<StoryboardTree> tree_;
    QPointer<QDockWidget> dock_;
    QTimer timer_;
    QString context_;
    bool active_=false, drag_=false, drops_=false, railEnabled_=false;
    QAbstractItemView::EditTriggers editTriggers_;

    bool guarded() const {
        return viewport_ && tree_ && !tree_->property("storyboardSourcePicking").toBool()
            && (viewport_->interactionLocked() || !viewport_->storyboardPickContext().isEmpty());
    }
    void removeGenerated() {
        if(!tree_)return;
        const QSignalBlocker block(tree_);
        QList<QTreeWidgetItem *> remove;
        for(QTreeWidgetItemIterator it(tree_);*it;++it)
            if((*it)->data(0,generatedRole).toBool())remove.append(*it);
        for(auto *item:remove)delete item;
    }
    void populate(QTreeWidgetItem *item) {
        if(!guarded() || !item || item->data(0,generatedRole).toBool())return;
        const int type=item->data(0,Qt::UserRole).toInt();
        if(type!=kTreeSketch && type!=kTreeExtrusion && type!=kTreeBody)return;
        for(int i=0;i<item->childCount();++i)if(item->child(i)->data(0,generatedRole).toBool())return;
        const auto refs=viewport_->storyboardReferences(type==kTreeSketch?SceneObjectKind::Sketch:SceneObjectKind::Extrusion,
                                                       item->data(0,Qt::UserRole+1).toInt(),type==kTreeBody);
        if(refs.isEmpty())return;
        const QSignalBlocker block(tree_);
        auto *root=new QTreeWidgetItem(item,{QStringLiteral("Selezione per il pannello")});
        root->setData(0,Qt::UserRole,kTreeInfo);root->setData(0,generatedRole,true);
        root->setFlags(Qt::ItemIsEnabled);root->setExpanded(true);
        QHash<QString,QTreeWidgetItem *> groups;
        for(const auto &ref:refs) {
            const QString group=ref.kind==7?(ref.element.kind<0?QStringLiteral("Schizzo intero"):QStringLiteral("Entita' dello schizzo"))
                :ref.kind==6?QStringLiteral("Punti dello schizzo"):ref.kind==4?QStringLiteral("Spigoli / bordi")
                :ref.kind==5?QStringLiteral("Facce"):ref.kind==3?QStringLiteral("Vertici")
                :ref.kind==9?QStringLiteral("Curve 3D"):QStringLiteral("Riferimenti");
            auto *parent=groups.value(group);
            if(!parent){parent=new QTreeWidgetItem(root,{group});parent->setData(0,Qt::UserRole,kTreeInfo);parent->setFlags(Qt::ItemIsEnabled);parent->setExpanded(true);groups.insert(group,parent);}
            QString label=geometryRefText(ref,viewport_->sketches(),viewport_->extrusions());
            if(ref.kind==4)label=QStringLiteral("Spigolo E%1").arg(ref.point.subshape);
            if(ref.kind==5)label=QStringLiteral("Faccia F%1").arg(ref.point.subshape);
            if(ref.kind==9)label+=QStringLiteral(" — curva %1").arg(parent->childCount()+1);
            auto *child=new QTreeWidgetItem(parent,{label});child->setData(0,Qt::UserRole,kTreeInfo);
            child->setData(0,referenceRole,QVariant::fromValue(ref));child->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
            child->setToolTip(0,QStringLiteral("Clic: seleziona nel campo attivo del pannello; un altro clic rimuove quando il campo lo consente."));
        }
    }
    void sync() {
        if(!tree_ || !viewport_)return;
        const bool next=guarded();
        const QString context=next?viewport_->storyboardPickContext():QString();
        if(next && !active_) {
            drag_=tree_->dragEnabled();drops_=tree_->acceptDrops();editTriggers_=tree_->editTriggers();railEnabled_=tree_->historyRail()->isEnabled();
            tree_->setDragEnabled(false);tree_->setAcceptDrops(false);tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            tree_->historyRail()->setEnabled(false);
        }
        if(active_ && (!next || context_!=context))removeGenerated();
        if(!next && active_) {
            const QSignalBlocker block(tree_);
            for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->data(0,indicatorRole).isValid()) {
                (*it)->setChildIndicatorPolicy(static_cast<QTreeWidgetItem::ChildIndicatorPolicy>((*it)->data(0,indicatorRole).toInt()));
                (*it)->setData(0,indicatorRole,QVariant());
            }
            tree_->setDragEnabled(drag_);tree_->setAcceptDrops(drops_);tree_->setEditTriggers(editTriggers_);
            tree_->historyRail()->setEnabled(railEnabled_ && !tree_->property("storyboardSourcePicking").toBool());
        }
        active_=next;context_=context;
        if(!active_)return;
        QList<QTreeWidgetItem *> expanded;
        {
            const QSignalBlocker block(tree_);
            for(QTreeWidgetItemIterator it(tree_);*it;++it) {
                const int type=(*it)->data(0,Qt::UserRole).toInt();
                if(type!=kTreeSketch && type!=kTreeExtrusion && type!=kTreeBody)continue;
                if(!(*it)->data(0,indicatorRole).isValid())(*it)->setData(0,indicatorRole,int((*it)->childIndicatorPolicy()));
                (*it)->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                if((*it)->isExpanded())expanded.append(*it);
            }
        }
        for(auto *item:expanded)populate(item);
    }
    void choose(QTreeWidgetItem *item) {
        if(!item || !viewport_ || !tree_)return;
        if(StoryboardTree::isHistoryCursor(item))return;
        if(item->data(0,referenceRole).isValid()) {
            const auto reference=item->data(0,referenceRole).value<GeometryRef>();
            tree_->setCurrentItem(item);
            const auto error=viewport_->pickStoryboardReference(reference);
            if(!error.isEmpty())window_->statusBar()->showMessage(error,6000);
            return; // il callback puo' ricostruire l'albero
        }
        const int type=item->data(0,Qt::UserRole).toInt();
        const int index=item->data(0,Qt::UserRole+1).toInt();
        const auto refs=viewport_->storyboardReferences(type==kTreeSketch?SceneObjectKind::Sketch
            :(type==kTreeExtrusion || type==kTreeBody)?SceneObjectKind::Extrusion:type==kTreePlane?SceneObjectKind::Plane:SceneObjectKind::None,
            index,type==kTreeBody);
        for(const auto &ref:refs)if((type==kTreeSketch && ref.kind==7 && ref.element.kind<0)
            ||(type==kTreeExtrusion && (ref.kind==8 || (ref.kind==9 && refs.size()==1))) || type==kTreePlane || type==kTreeOrigin) {
            tree_->setCurrentItem(item);
            const auto error=viewport_->pickStoryboardReference(ref);
            if(!error.isEmpty())window_->statusBar()->showMessage(error,6000);
            return;
        }
        if(type==kTreeSketch || type==kTreeExtrusion || type==kTreeBody)populate(item);
        item->setExpanded(!item->isExpanded());
        if(context_.isEmpty())window_->statusBar()->showMessage(QStringLiteral("Attiva il campo di selezione nel pannello, poi scegli nella storyboard."),6000);
        else if(refs.isEmpty() && (type==kTreeSketch || type==kTreeExtrusion || type==kTreeBody))
            window_->statusBar()->showMessage(QStringLiteral("Nessun riferimento compatibile in questa voce; scegli una geometria precedente alla funzione."),6000);
    }
    bool eventFilter(QObject *object,QEvent *event) override {
        if(!guarded())return false;
        if(object==tree_->viewport()) {
            if(event->type()==QEvent::MouseButtonPress) {
                auto *mouse=static_cast<QMouseEvent *>(event);
                auto *item=tree_->itemAt(mouse->position().toPoint());
                const QPersistentModelIndex clicked=item?tree_->indexFromItem(item):QModelIndex();
                const QVariant reference=item?item->data(0,referenceRole):QVariant();
                const bool branch=item && mouse->position().x()<tree_->visualItemRect(item).left();
                // Cambiare campo puo' rimuovere i figli temporanei e spostare
                // le righe. Mantiene la voce su cui e' avvenuta la pressione.
                sync();
                if(mouse->button()==Qt::LeftButton) {
                    item=clicked.isValid()?tree_->itemFromIndex(clicked):nullptr;
                    if(item && branch) {populate(item);item->setExpanded(!item->isExpanded());}
                    else if(item) choose(item);
                    else if(reference.isValid()) {
                        const auto error=viewport_->pickStoryboardReference(reference.value<GeometryRef>());
                        if(!error.isEmpty())window_->statusBar()->showMessage(error,6000);
                    }
                }
                return true;
            }
            if(event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick
                || event->type()==QEvent::ContextMenu
                || (event->type()==QEvent::MouseMove && static_cast<QMouseEvent *>(event)->buttons()!=Qt::NoButton)
                || event->type()==QEvent::DragEnter || event->type()==QEvent::DragMove || event->type()==QEvent::Drop)return true;
        }
        if(object==tree_ && event->type()==QEvent::KeyPress) {
            auto *key=static_cast<QKeyEvent *>(event);
            if(key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter || key->key()==Qt::Key_Space) {
                const QPersistentModelIndex clicked=tree_->currentIndex();
                const QVariant reference=tree_->currentItem()?tree_->currentItem()->data(0,referenceRole):QVariant();
                sync();
                if(clicked.isValid())choose(tree_->itemFromIndex(clicked));
                else if(reference.isValid()) {
                    const auto error=viewport_->pickStoryboardReference(reference.value<GeometryRef>());
                    if(!error.isEmpty())window_->statusBar()->showMessage(error,6000);
                }
                return true;
            }
            if(key->key()==Qt::Key_Escape) {QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(viewport_,&escape);return true;}
            if(StoryboardTree::isHistoryCursor(tree_->currentItem()))return true;
            return key->key()!=Qt::Key_Up && key->key()!=Qt::Key_Down && key->key()!=Qt::Key_Left
                && key->key()!=Qt::Key_Right && key->key()!=Qt::Key_Home && key->key()!=Qt::Key_End
                && key->key()!=Qt::Key_PageUp && key->key()!=Qt::Key_PageDown;
        }
        if(event->type()==QEvent::Shortcut && object->parent()==tree_)return true;
        auto *widget=qobject_cast<QWidget *>(object);
        auto *rail=tree_->historyRail();
        if(widget && (widget==rail || rail->isAncestorOf(widget))
            && (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonRelease
                || event->type()==QEvent::MouseMove || event->type()==QEvent::KeyPress || event->type()==QEvent::Wheel))return true;
        return false;
    }
public:
    StoryboardSelection(QMainWindow *window,CadViewport *viewport,StoryboardTree *tree):QObject(window),window_(window),viewport_(viewport),tree_(tree),timer_(this) {
        dock_=qobject_cast<QDockWidget *>(tree->parentWidget());
        if(dock_)dock_->setProperty("storyboardPanelSelection",true);
        qApp->installEventFilter(this);
        connect(tree,&QTreeWidget::itemExpanded,this,[this](QTreeWidgetItem *item){populate(item);});
        timer_.setInterval(75);connect(&timer_,&QTimer::timeout,this,[this]{sync();});timer_.start();
    }
    ~StoryboardSelection() override {qApp->removeEventFilter(this);if(dock_)dock_->setProperty("storyboardPanelSelection",false);}
};
}
void installStoryboardSelection(QMainWindow *window,CadViewport *viewport,StoryboardTree *tree) {
    new StoryboardSelection(window,viewport,tree);
}
}
