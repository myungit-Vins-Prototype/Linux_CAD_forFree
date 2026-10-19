#pragma once
#include "toolbar_editor.h"
#include "cad_toolbar.h"
#include <QMainWindow>
#include <QMenuBar>
#include <QSet>
#include <QDockWidget>
namespace ForgeCad {
// Usa gli stessi editor del 3D; layout e blocco del 2D hanno chiavi proprie.
class DrawingToolbarController final:public QObject {
    QMainWindow *window_,*workspace_;
    QMenu *menu_;
    QVector<QToolBar *> bars_;
    QVector<ToolbarEditor *> editors_;
    QVector<QVector<QPointer<QAction>>> defaults_;
public:
    DrawingToolbarController(QMainWindow *window,QMainWindow *workspace,QMenu *view,QVector<QToolBar *> bars,std::function<void()> afterRestore={}):QObject(window),window_(window),workspace_(workspace),bars_(bars){
        ToolbarEditor::Commands commands;QSet<QAction *> seen;
        const std::function<void(QMenu *,QString)> visit=[&](QMenu *menu,QString path){for(auto *a:menu->actions()){
            if(a->isSeparator()||a->property("toolbarConfiguration").toBool())continue;
            auto label=a->text();label.remove('&');if(a->menu())visit(a->menu(),path+label+'/');else if(!label.isEmpty()&&!seen.contains(a)){commands.append({path+label,a});seen.insert(a);}
        }};
        for(auto *a:window->menuBar()->actions())if(a->menu())visit(a->menu(),a->text().remove('&')+'/');
        menu_=addCadMenu(view,QStringLiteral("Barre degli strumenti"));menu_->setObjectName("drawingToolbarsMenu");menu_->menuAction()->setProperty("toolbarConfiguration",true);
        auto *lock=menu_->addAction(QStringLiteral("Blocca barre degli strumenti"));lock->setObjectName("drawingLockToolbarsAction");lock->setCheckable(true);
        lock->setChecked(QSettings().value("drawingToolbar/locked",QSettings().value("toolbar/locked",false)).toBool());
        for(auto *bar:bars_){
            QVector<QPointer<QAction>> initial;for(auto *a:bar->actions())initial.append(a);defaults_.append(initial);
            auto *editor=new ToolbarEditor(bar,commands,[lock]{return lock->isChecked();});editors_.append(editor);
            auto *edit=menu_->addAction(QStringLiteral("Personalizza %1…").arg(bar->windowTitle()));edit->setObjectName(bar->objectName()+"EditorAction");edit->setEnabled(!lock->isChecked());
            connect(edit,&QAction::triggered,editor,[editor]{editor->edit();});
            bar->setMovable(!lock->isChecked());bar->setFloatable(!lock->isChecked());
            connect(lock,&QAction::toggled,bar,[bar,edit](bool value){bar->setMovable(!value);bar->setFloatable(!value);edit->setEnabled(!value);});
            auto *visible=bar->toggleViewAction();menu_->addAction(visible);visible->setObjectName(bar->objectName()+"VisibleAction");
            bar->setVisible(QSettings().value("drawingToolbar/visible/"+bar->objectName(),true).toBool());
            connect(visible,&QAction::toggled,this,[bar](bool value){QSettings().setValue("drawingToolbar/visible/"+bar->objectName(),value);});
            bar->setContextMenuPolicy(Qt::CustomContextMenu);connect(bar,&QWidget::customContextMenuRequested,this,[bar,edit,lock](QPoint point){CadMenu menu(bar);menu.addAction(edit);menu.addAction(lock);menu.exec(bar->mapToGlobal(point));});
        }
        connect(lock,&QAction::toggled,this,[this](bool value){QSettings().setValue("drawingToolbar/locked",value);saveLayout();});
        // Ripristina dopo il layout del nuovo contenitore: farlo mentre e' ancora
        // largo zero puo' assegnare tutto lo spazio al pannello revisioni.
        QTimer::singleShot(0,this,[this,afterRestore]{
            if(!window_->property("drawingLayoutRestored").toBool()){
                if(!window_->restoreState(QSettings().value("drawingInterface/state").toByteArray()))
                    if(auto *dock=window_->findChild<QDockWidget *>("drawingRevisionDock"))window_->resizeDocks({dock},{320},Qt::Horizontal);
                window_->setProperty("drawingLayoutRestored",true);
            }
            workspace_->restoreState(QSettings().value("drawingInterface/editorState").toByteArray());
            if(afterRestore)afterRestore();
        });
    }
    void saveLayout(){if(!window_->property("drawingLayoutRestored").toBool())return;QSettings().setValue("drawingInterface/state",window_->saveState());QSettings().setValue("drawingInterface/editorState",workspace_->saveState());}
    ~DrawingToolbarController()override{
        saveLayout();
        for(int i=0;i<bars_.size();++i){auto *bar=bars_[i];delete editors_[i];const auto old=bar->actions();bar->clear();for(auto *a:old)if(qobject_cast<QWidgetAction *>(a)&&!defaults_[i].contains(a))delete a;for(const auto &a:defaults_[i])if(a)bar->addAction(a);}
        delete menu_;
    }
};
}
