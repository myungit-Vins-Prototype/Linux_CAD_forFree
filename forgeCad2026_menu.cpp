#include "cad_menu.h"
#include "forgeCad2026_ui_glass.h"
#include "forgeCad2026_viewport.h"
#include <QOpenGLWidget>
#include <QPointer>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QStyleFactory>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QWindow>

struct CadMenu::Impl {
    static constexpr int shadowMargin=12;
    CadMenu *self;
    ForgeCad::UiGlassAppearance appearance;
    QPalette colors;
    int padding=6;
    QTimer captureTimer,settingsTimer,diagnosticsTimer;
    int diagnosticsCount=0;
    QImage backdrop,shadow,surface;
    QPointer<CadViewport> viewport;
    quint64 frameSerial=0;
    bool capturing=false,reloading=false;
    QRect body() const {return self->rect().adjusted(shadowMargin,shadowMargin,-shadowMargin,-shadowMargin);}
    explicit Impl(CadMenu *owner):self(owner) {
        // Uno stile esplicito impedisce al tema desktop di registrare blur/ombre
        // native sul rettangolo completo, incluso il nostro margine trasparente.
        static QPointer<QStyle> popupStyle;
        if(!popupStyle){popupStyle=QStyleFactory::create(QStringLiteral("Fusion"));popupStyle->setParent(qApp);}
        self->setStyle(popupStyle);
        diagnosticsTimer.setSingleShot(true);
        QObject::connect(&diagnosticsTimer,&QTimer::timeout,self,[this]{diagnose();});
        self->setWindowFlag(Qt::FramelessWindowHint,true);
        self->setWindowFlag(Qt::NoDropShadowWindowHint,true);
        self->setAttribute(Qt::WA_TranslucentBackground);
        captureTimer.setSingleShot(true);
        QObject::connect(&captureTimer,&QTimer::timeout,self,[this]{capture();});
        settingsTimer.setInterval(200);
        QObject::connect(&settingsTimer,&QTimer::timeout,self,[this]{if(reload())schedule(0);});
        QObject::connect(self,&QMenu::aboutToShow,self,[this]{reload();});
        reload();
    }
    void scheduleDiagnostics() {
        if(!qEnvironmentVariableIsEmpty("FORGECAD_MENU_DIAGNOSTICS") && self->isVisible() && diagnosticsCount<20)diagnosticsTimer.start(150);
    }
    void diagnose() {
        if(!self->isVisible() || diagnosticsCount>=20)return;
        const auto directory=qEnvironmentVariable("FORGECAD_MENU_DIAGNOSTICS");if(directory.isEmpty())return;
        QDir dir(directory);if(!dir.mkpath(QStringLiteral(".")))return;
        const auto prefix=QStringLiteral("menu-%1-%2").arg(quintptr(self),0,16).arg(++diagnosticsCount);
        const auto rendered=self->grab().toImage();
        const auto save=[&](const QImage &image,const QString &layer){if(!image.isNull())image.save(dir.filePath(prefix+layer+QStringLiteral(".png")));};
        save(rendered,QStringLiteral("-paint"));save(shadow,QStringLiteral("-shadow"));save(surface,QStringLiteral("-surface"));save(backdrop,QStringLiteral("-backdrop"));
        QFile file(dir.filePath(QStringLiteral("menus.jsonl")));if(!file.open(QIODevice::WriteOnly|QIODevice::Append))return;
        const auto *window=self->windowHandle();const auto *style=self->style();
        const QJsonObject record{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"capture",prefix},{"backend",QGuiApplication::platformName()},{"qt",QString::fromLatin1(qVersion())},
            {"styleClass",QString::fromLatin1(style->metaObject()->className())},{"styleName",style->objectName()},
            {"explicitStyle",self->testAttribute(Qt::WA_SetStyle)},{"styledBackground",self->testAttribute(Qt::WA_StyledBackground)},
            {"autoFill",self->autoFillBackground()},{"opaquePaint",self->testAttribute(Qt::WA_OpaquePaintEvent)},
            {"translucent",self->testAttribute(Qt::WA_TranslucentBackground)},{"noSystemBackground",self->testAttribute(Qt::WA_NoSystemBackground)},
            {"windowAlpha",self->palette().color(QPalette::Window).alpha()},{"alphaBuffer",window?window->format().alphaBufferSize():-1},
            {"dpr",self->devicePixelRatioF()},{"width",self->width()},{"height",self->height()},
            {"windowFlags",QString::number(quint64(self->windowFlags()),16)},{"opacity",appearance.opacity},{"blur",appearance.blur},{"radius",appearance.radius},
            {"paintMarginAlpha",rendered.isNull()?-1:rendered.pixelColor(rendered.width()/2,0).alpha()},
            {"ownerColor",colors.color(QPalette::Window).name()},{"backdropReady",!backdrop.isNull()}};
        file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));file.write("\n");
    }
    bool reload() {
        if(reloading)return false;
        QScopedValueRollback<bool> guard(reloading,true);
        const auto next=ForgeCad::readUiGlassAppearance();const auto palette=ForgeCad::uiGlassPalette(self);
        const bool changed=appearance!=next || colors!=palette;
        appearance=next;colors=palette;
        padding=qMax(6,qCeil(appearance.radius*(1.0-1.0/qSqrt(2.0)))+2);
        // La tinta del vetro è separata dal fondo della finestra Qt: quest'ultimo
        // deve restare trasparente anche quando il tema ripristina le palette.
        auto windowPalette=colors;windowPalette.setColor(QPalette::Window,Qt::transparent);
        if(self->palette()!=windowPalette)self->setPalette(windowPalette);
        const auto sheet=QStringLiteral(
            "QMenu { background: transparent; color: %2; border: 1px solid transparent; padding: %1px; }"
            "QMenu::item { padding: 4px 14px 4px 26px; background: transparent; border-radius: 4px; }"
            "QMenu::item:selected:enabled { background: palette(highlight); color: palette(highlighted-text); }"
            "QMenu::item:disabled { color: %3; }"
            "QMenu::separator { height: 2px; background: palette(mid); margin: 0px 8px; }").arg(padding+shadowMargin).arg(colors.color(QPalette::WindowText).name()).arg(colors.color(QPalette::Disabled,QPalette::WindowText).name());
        if(self->styleSheet()!=sheet)self->setStyleSheet(sheet);
        // Il repolish dei fogli globali può riattivare il riempimento nativo.
        // Il popup possiede già fondo, bordo e ombra nel proprio paintEvent.
        self->setAutoFillBackground(false);
        self->setAttribute(Qt::WA_StyledBackground,false);
        self->setAttribute(Qt::WA_OpaquePaintEvent,false);
        self->setAttribute(Qt::WA_NoSystemBackground,true);
        self->setProperty("glassOpacity",appearance.opacity);self->setProperty("glassBlur",appearance.blur);self->setProperty("glassRadius",appearance.radius);
        if(changed){shadow=QImage();backdrop=QImage();surface=QImage();self->update();}
        return changed;
    }
    QWidget *ownerWindow() const {
        QWidget *root=self->parentWidget();while(root && root->parentWidget())root=root->parentWidget();return root;
    }
    void locateViewport() {
        if(viewport)return;
        if(auto *root=ownerWindow())for(auto *gl:root->findChildren<QOpenGLWidget *>())if(auto *cad=dynamic_cast<CadViewport *>(gl)) {
            viewport=cad;
            QObject::connect(cad,&QOpenGLWidget::frameSwapped,self,[this]{if(viewport && viewport->renderedFrameSerial()!=frameSerial)schedule(80);},Qt::QueuedConnection);
            break;
        }
    }
    void schedule(int delay=0) {
        if(self->isVisible() && (!captureTimer.isActive() || delay==0))captureTimer.start(delay);
    }
    void capture() {
        if(capturing || !self->isVisible())return;
        if(appearance.opacity==100){backdrop=QImage();surface=QImage();self->setProperty("glassBackdropReady",false);self->update();return;}
        auto *root=ownerWindow();if(!root || !root->isVisible() || QGuiApplication::platformName()=="offscreen")return;
        capturing=true;
        const int margin=appearance.blur*2;const QRect global(self->mapToGlobal(QPoint()),self->size());
        const QRect expanded=global.adjusted(-margin,-margin,margin,margin);
        const QRect rootGlobal(root->mapToGlobal(QPoint()),root->size());const QRect visible=expanded.intersected(rootGlobal);
        const qreal dpr=self->devicePixelRatioF();
        QImage scene(QSize(qCeil(expanded.width()*dpr),qCeil(expanded.height()*dpr)),QImage::Format_ARGB32_Premultiplied);scene.setDevicePixelRatio(dpr);
        auto base=colors.color(QPalette::Window);base.setAlpha(255);scene.fill(base);
        if(!visible.isEmpty()) {
            const auto source=root->grab(QRect(root->mapFromGlobal(visible.topLeft()),visible.size()));
            QPainter painter(&scene);painter.drawPixmap(visible.topLeft()-expanded.topLeft(),source);
        }
        scene=ForgeCad::blurUiImage(std::move(scene),qRound(appearance.blur*dpr));
        backdrop=scene.copy(qRound(margin*dpr),qRound(margin*dpr),qCeil(self->width()*dpr),qCeil(self->height()*dpr));backdrop.setDevicePixelRatio(dpr);
        if(viewport)frameSerial=viewport->renderedFrameSerial();
        capturing=false;surface=QImage();self->setProperty("glassBackdropReady",!visible.isEmpty());self->update();
    }
};
CadMenu::CadMenu(QWidget *parent):QMenu(parent) {impl_=std::make_unique<Impl>(this);}
CadMenu::CadMenu(const QString &title,QWidget *parent):QMenu(title,parent) {impl_=std::make_unique<Impl>(this);}
CadMenu::~CadMenu()=default;
bool CadMenu::event(QEvent *event) {
    const auto type=event->type();
    const bool appearanceEvent=type==QEvent::StyleChange || type==QEvent::PaletteChange || type==QEvent::ApplicationPaletteChange || type==QEvent::ThemeChange;
    if(!appearanceEvent)return QMenu::event(event);
    const bool handled=QMenu::event(event);
    // Dopo Qt, per non far sovrascrivere di nuovo gli attributi dal repolish.
    if(impl_ && !impl_->reloading) {
        impl_->reload();impl_->backdrop=QImage();impl_->surface=QImage();impl_->shadow=QImage();
        impl_->schedule();impl_->scheduleDiagnostics();update();
    }
    return handled;
}
void CadMenu::showEvent(QShowEvent *event) {QMenu::showEvent(event);impl_->reload();impl_->locateViewport();impl_->settingsTimer.start();impl_->schedule();impl_->scheduleDiagnostics();}
void CadMenu::hideEvent(QHideEvent *event) {impl_->captureTimer.stop();impl_->settingsTimer.stop();impl_->diagnosticsTimer.stop();impl_->backdrop=QImage();impl_->surface=QImage();setProperty("glassBackdropReady",false);QMenu::hideEvent(event);}
void CadMenu::resizeEvent(QResizeEvent *event) {QMenu::resizeEvent(event);impl_->shadow=QImage();impl_->surface=QImage();impl_->schedule();}
void CadMenu::moveEvent(QMoveEvent *event) {QMenu::moveEvent(event);impl_->schedule();}
void CadMenu::paintEvent(QPaintEvent *event) {
    QPainter painter(this);painter.setCompositionMode(QPainter::CompositionMode_Source);painter.fillRect(rect(),Qt::transparent);painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &a=impl_->appearance;const auto &colors=impl_->colors;const auto body=QRectF(impl_->body()).adjusted(.5,.5,-.5,-.5);
    const qreal radius=qMin(qreal(a.radius),qMin(body.width(),body.height())/2);
    const qreal dpr=devicePixelRatioF();const QSize pixels(qCeil(width()*dpr),qCeil(height()*dpr));
    if(impl_->shadow.isNull() || impl_->shadow.size()!=pixels || impl_->shadow.devicePixelRatio()!=dpr)impl_->shadow=ForgeCad::uiShadowImage(size(),dpr,{body},qRound(radius));
    painter.drawImage(QPoint(),impl_->shadow);
    QPainterPath shape;shape.addRoundedRect(body,radius,radius);
    if(impl_->surface.isNull() || impl_->surface.size()!=pixels || impl_->surface.devicePixelRatio()!=dpr) {
        // Un clip vettoriale ritaglia l'immagine a gradini. Comporre prima
        // il vetro e applicare una sola maschera alpha campionata a 3x.
        QImage mask(pixels*3,QImage::Format_ARGB32_Premultiplied);mask.setDevicePixelRatio(dpr*3);mask.fill(Qt::transparent);
        {QPainter p(&mask);p.setRenderHint(QPainter::Antialiasing);p.fillPath(shape,Qt::white);}
        mask=mask.scaled(pixels,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);mask.setDevicePixelRatio(dpr);
        impl_->surface=QImage(pixels,QImage::Format_ARGB32_Premultiplied);impl_->surface.setDevicePixelRatio(dpr);impl_->surface.fill(Qt::transparent);
        QPainter p(&impl_->surface);
        if(!impl_->backdrop.isNull())p.drawImage(QPoint(),impl_->backdrop);
        auto fill=colors.color(QPalette::Window);fill.setAlpha(qRound(255.0*a.opacity/100));p.fillRect(rect(),fill);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);p.drawImage(QPoint(),mask);
    }
    painter.drawImage(QPoint(),impl_->surface);
    painter.setPen(QPen(colors.color(QPalette::Mid),1));painter.drawPath(shape);
    painter.setClipPath(shape);const int inset=Impl::shadowMargin+impl_->padding;painter.setClipRect(rect().adjusted(inset,inset,-inset,-inset),Qt::IntersectClip);
    for(auto *action:actions()) {
        const auto row=actionGeometry(action);if(!action->isVisible() || row.isEmpty() || !row.intersects(event->rect()))continue;
        QStyleOptionMenuItem option;initStyleOption(&option,action);option.rect=row;option.palette=colors;
        option.checked=false;option.state &= ~(QStyle::State_On|QStyle::State_Off|QStyle::State_NoChange);option.checkType=QStyleOptionMenuItem::NotCheckable;option.icon=QIcon();
        // Il fondale viene composto una volta sola: nessun fillRect opaco per riga.
        style()->drawControl(QStyle::CE_MenuItem,&option,&painter,this);
        if(!action->icon().isNull()) {
            const int size=style()->pixelMetric(QStyle::PM_SmallIconSize,&option,this);
            const int x=layoutDirection()==Qt::RightToLeft?row.right()-22-size:row.left()+22;
            action->icon().paint(&painter,QRect(x,row.center().y()-size/2,size,size),Qt::AlignCenter,action->isEnabled()?QIcon::Normal:QIcon::Disabled,QIcon::Off);
        }
        if(action->isCheckable() && action->isChecked()) {
            const qreal x=layoutDirection()==Qt::RightToLeft?row.right()-17:row.left()+6,y=row.center().y();
            QPainterPath tick;tick.moveTo(x,y);tick.lineTo(x+4,y+4);tick.lineTo(x+11,y-4);
            painter.setPen(QPen(colors.color(action->isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::WindowText),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(tick);
        }
    }
}
