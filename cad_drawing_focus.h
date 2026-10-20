#pragma once
#include <QApplication>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace ForgeCad {
// I dialoghi nativi possono restituire l'attivazione al modello proprietario
// della tavola. Restituiscila alla finestra del comando, solo nella nostra app.
class DrawingFocusReturn {
    QPointer<QWidget> window_;
    bool active_;
public:
    explicit DrawingFocusReturn(QWidget *owner):window_(owner->window()),
        active_(QApplication::activeWindow()==window_ ||
                (QApplication::activeWindow()&&window_->isAncestorOf(QApplication::activeWindow()))) {}
    ~DrawingFocusReturn(){
        if(!active_)return;
        const auto restore=[window=window_]{
            if(!window||!window->isVisible()||!window->isEnabled()||
               QGuiApplication::applicationState()!=Qt::ApplicationActive||QApplication::activeModalWidget())return;
            window->raise();window->activateWindow();
        };
        restore();
        if(window_)QTimer::singleShot(0,window_,restore);
    }
};
}
