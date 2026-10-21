#pragma once
#include "fk_cancel.h"
#include "cad_drawing_focus.h"
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QProgressBar>
#include <QProgressDialog>
#include <QTimer>
#include <future>
#include <algorithm>
#include <chrono>

namespace ForgeCad {
// Facciata sincrona per i comandi esistenti; solo dati geometrici nel worker.
// L'editor resta immutato e bloccato fino alla ricevuta o all'annullamento.
template<class Result,class Work>
QString runDrawingComputation(QWidget *owner,bool &busy,const QString &title,Result &result,Work work,const QStringList &phaseLabels={}){
    if(busy)return QStringLiteral("Attendere il calcolo della vista in corso.");
    DrawingFocusReturn focusReturn(owner);
    struct Guard{QWidget *widget;bool &busy;bool enabled;Guard(QWidget *w,bool &b):widget(w),busy(b),enabled(w->isEnabled()){busy=true;widget->setEnabled(false);}~Guard(){busy=false;widget->setEnabled(enabled);}} guard(owner,busy);
    const auto cancel=Kernel::currentCancellation?Kernel::currentCancellation:std::make_shared<std::atomic<bool>>(false);std::atomic<int> phase{0};
    try{
        auto task=std::async(std::launch::async,[work=std::move(work),cancel,&phase]()mutable{Kernel::CancellationScope scope(cancel);Kernel::cancellationPoint();auto output=work([&](int value){phase.store(value,std::memory_order_relaxed);Kernel::cancellationPoint();});Kernel::cancellationPoint();return output;});
        QProgressDialog dialog(QString(),QStringLiteral("Annulla"),0,5,owner->window());dialog.setObjectName("drawingViewProgress");dialog.setWindowTitle(title);dialog.setWindowModality(Qt::WindowModal);dialog.setAutoClose(false);dialog.setAutoReset(false);dialog.setMinimumDuration(250);dialog.setEnabled(true);
        auto *bar=new QProgressBar(&dialog);bar->setObjectName("drawingViewProgressBar");bar->setRange(0,5);bar->setFormat(QStringLiteral("Fase %v di %m"));dialog.setBar(bar);
        auto *label=new QLabel(&dialog);label->setTextFormat(Qt::PlainText);dialog.setLabel(label);
        QEventLoop loop;QTimer timer;timer.setInterval(16);
        QObject::connect(&dialog,&QProgressDialog::canceled,&loop,[&]{cancel->store(true,std::memory_order_relaxed);dialog.setLabelText(QStringLiteral("Annullamento del calcolo..."));});
        QObject::connect(&timer,&QTimer::timeout,&loop,[&]{
            const int value=std::clamp(phase.load(std::memory_order_relaxed),0,5);const QStringList defaults={QStringLiteral("Intersezioni e bordi esatti"),QStringLiteral("Contorni apparenti"),QStringLiteral("Materiale e tratteggio"),QStringLiteral("Visibilita' e facce ombreggiate"),QStringLiteral("Preparazione del risultato"),QStringLiteral("Calcolo completato")};
            if(!cancel->load(std::memory_order_relaxed)){dialog.setLabelText(title+QStringLiteral("\n")+(phaseLabels.size()==6?phaseLabels[value]:defaults[value]));dialog.setValue(value);}
            if(task.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)loop.quit();
        });
        timer.start();loop.exec();timer.stop();auto prepared=task.get();dialog.hide();
        if(cancel->load(std::memory_order_relaxed))return QStringLiteral("Calcolo della vista annullato.");
        result=std::move(prepared);return {};
    }catch(const Kernel::OperationCancelled &){return QStringLiteral("Calcolo della vista annullato.");}
    catch(const std::exception &error){return QString::fromUtf8(error.what());}
}
}
