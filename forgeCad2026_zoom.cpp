#include "forgeCad2026_zoom.h"
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QSet>
#include <QSettings>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ForgeCad {
namespace {
QSet<ZoomController *> &controllers() { static QSet<ZoomController *> value; return value; }
ZoomPreferences validated(ZoomPreferences p) {
    if (int(p.mode)<0 || int(p.mode)>2) p.mode=ZoomInputMode::Automatic;
    p.sensitivity=std::isfinite(p.sensitivity)?std::clamp(p.sensitivity,.05,10.):ZoomPreferences{}.sensitivity;
    p.duration=std::isfinite(p.duration)?std::clamp(p.duration,0.,10.):ZoomPreferences{}.duration;
    return p;
}
}
ZoomPreferences loadZoomPreferences() {
    QSettings settings;
    const ZoomPreferences defaults;
    return validated({ZoomInputMode(settings.value("view/zoomMode",int(defaults.mode)).toInt()),
                      settings.value("view/zoomSensitivity",defaults.sensitivity).toDouble(),
                      settings.value("view/zoomDuration",defaults.duration).toDouble()});
}
void saveZoomPreferences(const ZoomPreferences &value) {
    const auto p=validated(value);
    QSettings settings;
    settings.setValue("view/zoomMode",int(p.mode));
    settings.setValue("view/zoomSensitivity",p.sensitivity);
    settings.setValue("view/zoomDuration",p.duration);
    for (auto *controller:controllers()) controller->setPreferences(p);
}
void showZoomPreferences(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("zoomPreferencesDialog");
    dialog.setWindowTitle(QStringLiteral("Impostazioni zoom"));
    auto *layout=new QFormLayout(&dialog);
    auto *mode=new QComboBox(&dialog);
    mode->setObjectName("zoomInputMode");
    mode->addItems({QStringLiteral("Automatico"),QStringLiteral("Rotella animata"),QStringLiteral("Touchpad diretto")});
    mode->setToolTip(QStringLiteral("Automatico anima gli eventi incerti. Touchpad diretto applica subito ogni movimento."));
    auto *sensitivity=new QDoubleSpinBox(&dialog);
    sensitivity->setObjectName("zoomSensitivity");
    sensitivity->setRange(5,1000);sensitivity->setSuffix(QStringLiteral(" %"));sensitivity->setDecimals(0);
    auto *duration=new QDoubleSpinBox(&dialog);
    duration->setObjectName("zoomDuration");
    duration->setRange(0,10);duration->setDecimals(3);duration->setSingleStep(.05);duration->setSuffix(QStringLiteral(" s"));
    duration->setToolTip(QStringLiteral("Tempo della transizione della rotella. Zero applica subito lo zoom."));
    const auto p=loadZoomPreferences();mode->setCurrentIndex(int(p.mode));sensitivity->setValue(p.sensitivity*100);duration->setValue(p.duration);
    layout->addRow(QStringLiteral("Modalità"),mode);
    layout->addRow(QStringLiteral("Sensibilità"),sensitivity);
    layout->addRow(QStringLiteral("Durata transizione"),duration);
    auto *note=new QLabel(QStringLiteral("Le impostazioni valgono per la vista 3D e le tavole. Il pinch segue direttamente le dita."),&dialog);
    note->setWordWrap(true);layout->addRow(note);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    layout->addRow(buttons);
    if(dialog.exec()==QDialog::Accepted)
        saveZoomPreferences({ZoomInputMode(mode->currentIndex()),sensitivity->value()/100.,duration->value()});
}

ZoomInputNormalizer::ZoomInputNormalizer(Backend backend):backend_(backend) {}
void ZoomInputNormalizer::reset() { latched_=false;lastEvent_.invalidate(); }
ZoomInput ZoomInputNormalizer::wheel(const QWheelEvent &event,const ZoomPreferences &p) {
    const auto *device=event.pointingDevice();
    const auto id=device?device->systemId():0;
    if(event.phase()==Qt::ScrollBegin || !lastEvent_.isValid() || lastEvent_.elapsed()>180 || id!=device_) reset();
    device_=id;lastEvent_.start();
    const double angle=event.angleDelta().y(),pixels=event.pixelDelta().y();
    if(angle==0 && pixels==0) {
        if(event.phase()==Qt::ScrollEnd) reset();
        return {};
    }
    if(!latched_) {
        // Le fasi e il tipo sono indizi congiunti, mai il solo nome del device.
        // Su X11 Qt dichiara pixelDelta inaffidabile: fallback angolare animato.
        const bool quantized=angle!=0 && std::fmod(std::abs(angle),120.)==0 && std::abs(pixels)>=8.;
        direct_=backend_!=Backend::X11 && device && device->type()==QInputDevice::DeviceType::TouchPad
            && pixels!=0 && !quantized && (event.phase()==Qt::ScrollBegin || event.phase()==Qt::ScrollUpdate || event.phase()==Qt::ScrollMomentum);
        latched_=true;
    }
    const bool direct=p.mode==ZoomInputMode::DirectTouchpad || (p.mode==ZoomInputMode::Automatic && direct_);
    // Un solo campo per evento. Niente conversione pixel->scatti, ne' moltiplicazione
    // per DPR: Qt consegna gia' le coordinate dell'evento al livello del widget.
    const bool usePixels=(direct && pixels!=0) || angle==0;
    const double delta=(usePixels?pixels*.0015:angle*(.03/120.))*p.sensitivity;
    if(event.phase()==Qt::ScrollEnd) reset();
    return {delta,direct};
}
ZoomController::ZoomController(QObject *parent):QObject(parent),preferences_(loadZoomPreferences()),
    normalizer_(QGuiApplication::platformName()==QStringLiteral("xcb")?ZoomInputNormalizer::Backend::X11:ZoomInputNormalizer::Backend::NativePixels) {
    timer_.setTimerType(Qt::PreciseTimer);timer_.setInterval(16);
    connect(&timer_,&QTimer::timeout,this,[this] {
        const double seconds=double(clock_.nsecsElapsed())/1e9;
        clock_.restart();advance(seconds);
    });
    controllers().insert(this);
}
ZoomController::~ZoomController() { controllers().remove(this); }
void ZoomController::cancelMotion() { timer_.stop();clock_.invalidate();pending_=remaining_=0; }
void ZoomController::stop() { cancelMotion();normalizer_.reset(); }
void ZoomController::setPreferences(const ZoomPreferences &p) { stop();preferences_=validated(p); }
bool ZoomController::wheel(const QWheelEvent &event) {
    const auto input=normalizer_.wheel(event,preferences_);
    if(qEnvironmentVariableIsSet("FORGECAD_ZOOM_TRACE")) {
        const auto *device=event.pointingDevice();
        const QJsonObject record{{"angle",event.angleDelta().y()},{"pixel",event.pixelDelta().y()},
            {"phase",int(event.phase())},{"inverted",event.inverted()},{"timestamp",double(event.timestamp())},
            {"device",device?device->name():QString()},{"type",device?int(device->type()):0},
            {"deviceId",device?QString::number(device->systemId()):QString()},
            {"x",event.position().x()},{"y",event.position().y()},
            {"backend",QGuiApplication::platformName()},{"logDelta",input.logDelta},{"direct",input.direct},
            {"duration",preferences_.duration},{"sensitivity",preferences_.sensitivity},{"mode",int(preferences_.mode)}};
        qInfo().noquote()<<"ZOOM"<<QJsonDocument(record).toJson(QJsonDocument::Compact);
    }
    if(input.logDelta==0) return event.phase()!=Qt::NoScrollPhase;
    enqueue(input,event.position());return true;
}
bool ZoomController::nativeGesture(const QNativeGestureEvent &event) {
    if(event.gestureType()==Qt::BeginNativeGesture || event.gestureType()==Qt::EndNativeGesture) { stop();return true; }
    if(event.gestureType()!=Qt::ZoomNativeGesture) return false;
    // Qt esprime il pinch come incremento relativo del fattore: .1 = +10%.
    if(std::isfinite(event.value()) && event.value()>-1.)
        enqueue({std::log1p(event.value())*preferences_.sensitivity,true},event.position());
    return true;
}
void ZoomController::enqueue(ZoomInput input,const QPointF &anchor) {
    if(!scale || !limits || !apply || !std::isfinite(input.logDelta)) return;
    anchor_=anchor;
    const auto [minimum,maximum]=limits();
    const double current=std::clamp(scale(),minimum,maximum);
    if(input.direct || preferences_.duration==0) {
        cancelMotion();applyLog(std::log(current)+input.logDelta);return;
    }
    if(pending_*input.logDelta<0) pending_=0;
    pending_=std::clamp(pending_+input.logDelta,std::log(minimum/current),std::log(maximum/current));
    if(std::abs(pending_)<1e-12) { cancelMotion();return; }
    remaining_=preferences_.duration;
    if(!timer_.isActive()) { clock_.start();timer_.start(); }
}
void ZoomController::applyLog(double value) {
    const auto [minimum,maximum]=limits();
    apply(std::exp(std::clamp(value,std::log(minimum),std::log(maximum))),anchor_);
}
void ZoomController::advance(double seconds) {
    if(!timer_.isActive() || !std::isfinite(seconds) || seconds<=0) return;
    const double dt=std::min(seconds,remaining_);
    const double applied=dt>=remaining_?pending_:pending_*dt/remaining_;
    pending_-=applied;remaining_=std::max(0.,remaining_-dt);
    applyLog(std::log(scale())+applied);
    const auto [minimum,maximum]=limits();
    constexpr double roundoff=8*std::numeric_limits<float>::epsilon();
    if(pending_==0 || scale()<=minimum*(1+roundoff) || scale()>=maximum*(1-roundoff)) cancelMotion();
}
}
