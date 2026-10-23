#pragma once
#include <QImage>
#include <QPainterPath>
#include <QPalette>
#include <QWidget>
#include <QVector>
namespace ForgeCad {
struct UiGlassAppearance {
    int opacity=88, blur=12, radius=10;
    bool operator==(const UiGlassAppearance &other) const {return opacity==other.opacity && blur==other.blur && radius==other.radius;}
    bool operator!=(const UiGlassAppearance &other) const {return !(*this==other);}
};
UiGlassAppearance readUiGlassAppearance();
QPalette uiGlassPalette(const QWidget *widget);
QImage blurUiImage(QImage image,int radius);
QImage uiShadowImage(QSize size,qreal dpr,const QVector<QRectF> &cards,int radius);
// Sorella della ribbon: l'ombra resta fuori dalla sua maschera senza intercettare input.
class UiGlassShadow final : public QWidget {
public:
    explicit UiGlassShadow(QWidget *parent);
    void setCards(const QVector<QRectF> &cards,int radius);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVector<QRectF> cards_;
    int radius_=0;
    QImage cached_;
};
}
