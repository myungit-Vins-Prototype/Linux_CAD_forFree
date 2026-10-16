#include "cad_drawing_page.h"
#include <QFontMetricsF>
#include <QPainter>
#include <cmath>
namespace ForgeCad {
namespace {
QString revision(const DocumentVersion &version){return version.checkinId.isNull()?QStringLiteral("Copia di lavoro"):QStringLiteral("Check-in %1").arg(version.checkinId.toString(QUuid::WithoutBraces));}
void text(QPainter &painter,QRectF rect,const QString &value,double height=2.5,Qt::Alignment align=Qt::AlignLeft|Qt::AlignVCenter){
    painter.save();QFont font(QStringLiteral("DejaVu Sans"));font.setPixelSize(100);painter.setFont(font);const double scale=height/100;painter.translate(rect.topLeft());painter.scale(scale,scale);const QRectF local(0,0,rect.width()/scale,rect.height()/scale);const QFontMetricsF metrics(font);
    painter.drawText(local,align,metrics.elidedText(value,Qt::ElideRight,local.width()));painter.restore();
}
}
QString drawingPaperName(const QSizeF &size){const double a=std::min(size.width(),size.height()),b=std::max(size.width(),size.height());const QSizeF sizes[]={{210,297},{297,420},{420,594},{594,841},{841,1189}};for(int i=0;i<5;++i)if(std::abs(a-sizes[i].width())<.01&&std::abs(b-sizes[i].height())<.01)return QStringLiteral("A%1").arg(4-i);return QStringLiteral("Personalizzato");}
DrawingPageContext defaultDrawingPageContext(const DrawingSheet &sheet){DrawingPageContext out;out.drawingVersion=sheet.drawing.version;out.modelVersion=sheet.drawing.model;out.drawingRevision=revision(out.drawingVersion);out.modelRevision=revision(out.modelVersion);return out;}
void paintDrawingPage(QPainter &painter,const DrawingSheet &sheet,const DrawingPageContext &context){
    painter.save();painter.setPen(QPen(QColor("#222222"),.25));painter.setBrush(Qt::NoBrush);const QRectF frame(10,10,sheet.size.width()-20,sheet.size.height()-20);painter.drawRect(frame);
    if(!sheet.titleBlock.visible){text(painter,QRectF(14,sheet.size.height()-21,sheet.size.width()-28,7),sheet.title+QStringLiteral("  |  Quote in mm"),3);painter.restore();return;}
    const QRectF block(frame.right()-175,frame.bottom()-42,175,42);painter.setBrush(Qt::white);painter.drawRect(block);painter.setBrush(Qt::NoBrush);
    const auto line=[&](double x1,double y1,double x2,double y2){painter.drawLine(block.topLeft()+QPointF(x1,y1),block.topLeft()+QPointF(x2,y2));};
    line(0,12,175,12);line(0,23,175,23);line(0,33,175,33);line(105,0,105,23);line(60,23,60,33);line(117,23,117,33);line(60,33,60,42);line(117,33,117,42);
    const auto cell=[&](double x,double y,double w,double h,const QString &label,const QString &value){const QPointF origin=block.topLeft()+QPointF(x,y);text(painter,QRectF(origin+QPointF(2,.7),QSizeF(w-4,3)),label,1.7);text(painter,QRectF(origin+QPointF(2,3.5),QSizeF(w-4,h-4)),value.isEmpty()?QStringLiteral("-"):value,2.6);};
    cell(0,0,105,12,QStringLiteral("TITOLO"),sheet.title);cell(105,0,70,12,QStringLiteral("CODICE TAVOLA"),sheet.titleBlock.code);
    cell(0,12,105,11,QStringLiteral("MODELLO"),context.modelName);cell(105,12,70,11,QStringLiteral("ORGANIZZAZIONE"),sheet.titleBlock.organization);
    cell(0,23,60,10,QStringLiteral("DISEGNATO DA"),sheet.titleBlock.author);cell(60,23,57,10,QStringLiteral("DATA"),sheet.titleBlock.date);cell(117,23,58,10,QStringLiteral("SCALA / UNITA'"),QStringLiteral("%1 / mm").arg([&]{if(sheet.drawing.views.empty())return QStringLiteral("-");const double scale=sheet.drawing.views[0].scale;for(const auto &view:sheet.drawing.views)if(view.scale!=scale)return QStringLiteral("Vedi viste");return QStringLiteral("%1:1").arg(scale,0,'g',6);}()));
    cell(0,33,60,9,QStringLiteral("REVISIONE TAVOLA"),context.drawingRevision);cell(60,33,57,9,QStringLiteral("REVISIONE MODELLO"),context.modelRevision);cell(117,33,58,9,QStringLiteral("FORMATO / FOGLIO"),drawingPaperName(sheet.size)+QStringLiteral(" / 1 di 1"));painter.restore();
}
}
