#pragma once
#include <QString>
#include <QPainterPath>
#include <QFont>
#include <cmath>
namespace ForgeCad {
enum class DrawingArrowStyle { Open, Filled, Closed, Tick };
struct DrawingStyle {
    QString fontFamily=QStringLiteral("DejaVu Sans");
    double dimensionHeight=3,viewTitleHeight=3,arrowLength=2,titleGap=6;
    DrawingArrowStyle arrow=DrawingArrowStyle::Open;
    bool titleSnaps=true;
    bool operator==(const DrawingStyle &s)const{return fontFamily==s.fontFamily&&dimensionHeight==s.dimensionHeight&&viewTitleHeight==s.viewTitleHeight&&arrowLength==s.arrowLength&&titleGap==s.titleGap&&arrow==s.arrow&&titleSnaps==s.titleSnaps;}
    bool active()const{return !(*this==DrawingStyle{});}
};
inline QString validateDrawingStyle(const DrawingStyle &s){
    if(s.fontFamily.trimmed().isEmpty()||s.fontFamily.size()>256)return QStringLiteral("Famiglia del carattere non valida.");
    for(auto c:s.fontFamily)if(!c.isPrint())return QStringLiteral("Famiglia del carattere non stampabile.");
    for(double x:{s.dimensionHeight,s.viewTitleHeight,s.arrowLength})if(!std::isfinite(x)||x<.5||x>20)return QStringLiteral("Dimensione di testi o frecce non valida (0,5–20 mm).");
    if(!std::isfinite(s.titleGap)||s.titleGap<0||s.titleGap>100||int(s.arrow)<0||int(s.arrow)>3)return QStringLiteral("Stile di freccia o distanza del titolo non validi.");
    return {};
}
inline QFont drawingFont(const DrawingStyle &s){QFont f(s.fontFamily);f.setPixelSize(12);return f;}
inline QPainterPath drawingArrowPath(const DrawingStyle &s,QPointF tip,QPointF toward){
    QPainterPath path;auto d=toward-tip;const double norm=std::hypot(d.x(),d.y());if(norm<1e-10)return path;d/=norm;const QPointF n(-d.y(),d.x());
    if(s.arrow==DrawingArrowStyle::Tick){path.moveTo(tip+s.arrowLength*.5*(d+n));path.lineTo(tip-s.arrowLength*.5*(d+n));return path;}
    path.moveTo(tip+s.arrowLength*d+s.arrowLength*.325*n);path.lineTo(tip);path.lineTo(tip+s.arrowLength*d-s.arrowLength*.325*n);
    if(s.arrow!=DrawingArrowStyle::Open)path.closeSubpath();
    return path;
}
}
