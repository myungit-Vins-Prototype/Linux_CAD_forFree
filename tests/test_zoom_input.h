#pragma once
#include "../forgeCad2026_zoom.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QNativeGestureEvent>
#include <QWheelEvent>

// Profili sintetici del contratto Qt: non simulano un collaudo hardware degli OS.
static void portableZoomTests() {
    using namespace ForgeCad;
    using Backend=ZoomInputNormalizer::Backend;
    QPointingDevice touchpad(QStringLiteral("touchpad"),1234,QInputDevice::DeviceType::TouchPad,
        QPointingDevice::PointerType::Finger,QInputDevice::Capability::Position|QInputDevice::Capability::PixelScroll,1,0);
    const auto makeWheel=[&](int angle,int pixels,Qt::ScrollPhase phase,bool touch,bool inverted=false) {
        return QWheelEvent(QPointF(50,40),QPointF(50,40),QPoint(0,pixels),QPoint(0,angle),
            Qt::NoButton,Qt::NoModifier,phase,inverted,Qt::MouseEventNotSynthesized,
            touch?&touchpad:QPointingDevice::primaryPointingDevice());
    };
    const auto near=[](double a,double b){return std::abs(a-b)<1e-10;};
    struct Sample { Backend backend;int angle,pixels;Qt::ScrollPhase phase;bool touch,direct;double expected; };
    const Sample samples[]={
        {Backend::X11,120,15,Qt::NoScrollPhase,true,false,.03}, // caso fisico registrato
        {Backend::X11,120,3,Qt::ScrollUpdate,true,false,.03}, // pixel X11 non affidabili
        {Backend::NativePixels,120,0,Qt::NoScrollPhase,false,false,.03},
        {Backend::NativePixels,10,0,Qt::ScrollUpdate,false,false,.0025},
        {Backend::NativePixels,120,20,Qt::ScrollUpdate,false,false,.03},
        {Backend::NativePixels,120,15,Qt::ScrollUpdate,true,false,.03},
        {Backend::NativePixels,12,3,Qt::ScrollUpdate,true,true,.0045},
        {Backend::NativePixels,0,3,Qt::ScrollMomentum,true,true,.0045},
        {Backend::NativePixels,0,3,Qt::NoScrollPhase,true,false,.0045},
        {Backend::X11,0,3,Qt::NoScrollPhase,true,false,.0045}
    };
    for(const auto &sample:samples) {
        ZoomInputNormalizer n(sample.backend);
        const auto event=makeWheel(sample.angle,sample.pixels,sample.phase,sample.touch);
        const auto result=n.wheel(event,{});
        require(result.direct==sample.direct && near(result.logDelta,sample.expected),"normalizzazione per backend e campi Qt senza doppio conteggio");
    }
    ZoomInputNormalizer n(Backend::NativePixels);
    const auto small=makeWheel(12,3,Qt::ScrollUpdate,true);
    const auto coarse=makeWheel(120,15,Qt::ScrollUpdate,true);
    require(n.wheel(small,{}).direct && n.wheel(coarse,{}).direct,"percorso stabile durante una sequenza continua");
    const auto end=makeWheel(0,0,Qt::ScrollEnd,true);
    require(n.wheel(end,{}).logDelta==0 && !n.wheel(coarse,{}).direct,"fine gesto azzera la classificazione senza zoom aggiuntivo");
    n.reset();const auto begin=makeWheel(0,0,Qt::ScrollBegin,true);
    require(n.wheel(begin,{}).logDelta==0 && n.wheel(small,{}).direct,"inizio nullo non forza la classificazione");
    n.reset();require(!n.wheel(small,{ZoomInputMode::AnimatedWheel,1.,.25}).direct,"scelta animata forza la durata anche sul touchpad");
    n.reset();const auto direct=n.wheel(coarse,{ZoomInputMode::DirectTouchpad,2.,.25});
    require(direct.direct && near(direct.logDelta,.045),"scelta diretta usa pixel con sensibilita' indipendente");
    n.reset();const auto inverted=makeWheel(-120,0,Qt::NoScrollPhase,false,true);
    require(near(n.wheel(inverted,{}).logDelta,-.03),"rispetta verso del sistema senza invertire due volte");
    n.reset();double sum=0;const auto fraction=makeWheel(10,0,Qt::NoScrollPhase,false);
    for(int i=0;i<12;++i)sum+=n.wheel(fraction,{}).logDelta;
    require(near(sum,.03),"dodici frazioni non perdono ampiezza");

    // Stesso motore, due adattatori (scala diretta e camera con scala inversa).
    ZoomController first,second;
    double scaleA=1,zoomB=20;
    first.scale=[&]{return scaleA;};second.scale=[&]{return 20/zoomB;};
    first.limits=second.limits=[]{return std::pair<double,double>{.1,10};};
    first.apply=[&](double value,const QPointF &){scaleA=value;};
    second.apply=[&](double value,const QPointF &){zoomB=20/value;};
    const auto previous=loadZoomPreferences();
    saveZoomPreferences({ZoomInputMode::AnimatedWheel,2.,3.});
    require(first.preferences().duration==3 && second.preferences().sensitivity==2 && loadZoomPreferences().duration==3,"preferenze persistenti aggiornano tutti i controller aperti");
    const auto notch=makeWheel(120,15,Qt::NoScrollPhase,true);
    first.wheel(notch);second.wheel(notch);
    first.advance(1.5);second.advance(1.5);
    require(near(scaleA,std::exp(.03)) && near(20/zoomB,scaleA) && near(first.remaining(),1.5),"durata tre secondi indipendente da driver e adattatore");
    first.advance(1.5);second.advance(1.5);
    require(near(scaleA,std::exp(.06)) && near(20/zoomB,scaleA),"identica quantita' finale 3D e tavole");
    first.setPreferences({ZoomInputMode::AnimatedWheel,1.,.25});first.wheel(notch);first.advance(.5);
    require(!first.timer()->isActive(),"avanzamento usa il tempo trascorso anche dopo stallo GUI");
    first.wheel(notch);saveZoomPreferences({ZoomInputMode::Automatic,1.,.25});
    require(!first.timer()->isActive() && first.pendingPoints()==0,"cambio preferenze elimina vecchi callback");
    const auto native=[&](Qt::NativeGestureType type,double delta){return QNativeGestureEvent(type,&touchpad,2,QPointF(50,40),QPointF(50,40),QPointF(50,40),delta,QPointF());};
    const double before=scaleA;first.nativeGesture(native(Qt::ZoomNativeGesture,.1));
    require(near(scaleA,before*1.1) && !first.timer()->isActive(),"pinch incrementale diretto senza seconda inerzia");
    require(!first.nativeGesture(native(Qt::RotateNativeGesture,10)),"rotazione nativa non consumata dallo zoom");
    first.nativeGesture(native(Qt::ZoomNativeGesture,-2));
    require(near(scaleA,before*1.1),"pinch invalido non corrompe scala");
    first.setPreferences({ZoomInputMode::AnimatedWheel,1.,0});first.wheel(notch);
    require(!first.timer()->isActive() && near(scaleA,before*1.1*std::exp(.03)),"durata zero e' immediata");

    // Il dialogo cambia davvero le preferenze, annullarlo le conserva.
    QTimer::singleShot(0,[]{
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
        require(dialog && dialog->objectName()=="zoomPreferencesDialog","dialogo zoom accessibile");
        dialog->findChild<QDoubleSpinBox *>("zoomDuration")->setValue(3);
        dialog->findChild<QDoubleSpinBox *>("zoomSensitivity")->setValue(150);
        dialog->findChild<QComboBox *>("zoomInputMode")->setCurrentIndex(1);dialog->accept();
    });
    showZoomPreferences(nullptr);
    require(loadZoomPreferences().duration==3 && first.preferences().sensitivity==1.5,"dialogo salva durata e sensibilita' e aggiorna finestre aperte");
    QTimer::singleShot(0,[]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());dialog->findChild<QDoubleSpinBox *>("zoomDuration")->setValue(1);dialog->reject();});
    showZoomPreferences(nullptr);
    require(loadZoomPreferences().duration==3,"annullamento non salva modifiche");
    saveZoomPreferences(previous);
    std::cout<<"PASS zoom portabile: profili Qt, fallback X11, sequenze, override, pinch, preferenze e adattatori\n";
}
