#include "forgeCad2026_ui_glass.h"
#include <QApplication>
#include <QPainter>
#include <QSettings>
#include <QtMath>
#include <algorithm>
namespace ForgeCad {
UiGlassAppearance readUiGlassAppearance() {
    QSettings settings;
    return {qBound(25,settings.value("view/functionPanelOpacity",settings.value("loft/dialogOpacity",88)).toInt(),100),
            qBound(0,settings.value("view/functionPanelBlur",12).toInt(),40),
            qBound(0,settings.value("view/functionPanelCornerRadius",10).toInt(),40)};
}
QPalette uiGlassPalette(const QWidget *widget) {
    // I contenitori della ribbon hanno Window trasparente nel foglio stile.
    // Colore e testo vengono dalla finestra applicativa, non da quei contenitori.
    const QWidget *anchor=widget;
    while(anchor && anchor->parentWidget())anchor=anchor->parentWidget();
    return anchor && !(anchor==widget && widget->windowType()==Qt::Popup)?anchor->palette():QApplication::palette();
}
static QImage boxPass(const QImage &source,int radius,bool horizontal) {
    if(radius<=0 || source.isNull())return source;
    QImage result(source.size(),QImage::Format_ARGB32_Premultiplied);result.setDevicePixelRatio(source.devicePixelRatio());
    const int w=source.width(),h=source.height(),count=radius*2+1;
    const int lines=horizontal?h:w, length=horizontal?w:h;
    for(int line=0;line<lines;++line) {
        auto pixel=[&](int i){int p=qBound(0,i,length-1);return horizontal?reinterpret_cast<const QRgb *>(source.constScanLine(line))[p]:reinterpret_cast<const QRgb *>(source.constScanLine(p))[line];};
        int r=0,g=0,b=0,a=0;
        for(int k=-radius;k<=radius;++k){auto p=pixel(k);r+=qRed(p);g+=qGreen(p);b+=qBlue(p);a+=qAlpha(p);}
        for(int i=0;i<length;++i) {
            auto *row=reinterpret_cast<QRgb *>(result.scanLine(horizontal?line:i));row[horizontal?i:line]=qRgba(r/count,g/count,b/count,a/count);
            auto removed=pixel(i-radius),added=pixel(i+radius+1);
            r+=qRed(added)-qRed(removed);g+=qGreen(added)-qGreen(removed);b+=qBlue(added)-qBlue(removed);a+=qAlpha(added)-qAlpha(removed);
        }
    }
    return result;
}
QImage blurUiImage(QImage image,int radius) {
    if(image.isNull() || radius<=0)return image;
    image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for(int pass=0;pass<3;++pass){image=boxPass(image,qMax(1,radius/2),true);image=boxPass(image,qMax(1,radius/2),false);}
    return image;
}
QImage uiShadowImage(QSize size,qreal dpr,const QVector<QRectF> &cards,int radius) {
    QImage image(QSize(qCeil(size.width()*dpr),qCeil(size.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
    {QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor(0,0,0,105));
        for(const auto &card:cards)painter.drawRoundedRect(card.translated(0,3),radius,radius);}
    image=blurUiImage(image,qMax(1,qRound(7*dpr)));
    // L'interno traslucido della carta non viene scurito dall'ombra.
    {QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setCompositionMode(QPainter::CompositionMode_Clear);
        for(const auto &card:cards)painter.fillPath([&]{QPainterPath path;path.addRoundedRect(card,radius,radius);return path;}(),Qt::transparent);}
    return image;
}
UiGlassShadow::UiGlassShadow(QWidget *parent):QWidget(parent) {
    setObjectName("ribbonShadow");setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_TranslucentBackground);setAutoFillBackground(false);
}
void UiGlassShadow::setCards(const QVector<QRectF> &cards,int radius) {
    if(cards_==cards && radius_==radius)return;
    cards_=cards;radius_=radius;cached_=QImage();update();
}
void UiGlassShadow::paintEvent(QPaintEvent *) {
    const qreal dpr=devicePixelRatioF();const QSize pixels(qCeil(width()*dpr),qCeil(height()*dpr));
    if(cached_.isNull() || cached_.size()!=pixels || cached_.devicePixelRatio()!=dpr)cached_=uiShadowImage(size(),dpr,cards_,radius_);
    QPainter painter(this);painter.drawImage(QPoint(),cached_);
}
}
