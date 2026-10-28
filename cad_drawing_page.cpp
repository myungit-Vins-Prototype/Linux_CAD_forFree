#include "cad_drawing_page.h"
#include <QFontMetricsF>
#include <QPainter>
#include <cmath>
#include <QTextLayout>
#include <QSet>
#include <array>
#include <stdexcept>
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
namespace {
using RevisionCells=std::array<QString,5>;
std::vector<RevisionCells> revisionCells(const DrawingSheet &sheet,const DrawingPageContext &context){
    std::vector<RevisionCells> rows;QSet<QUuid> seen;
    for(const auto &revision:context.projectRevisions){
        if(revision.version.documentId!=context.modelVersion.documentId||revision.version.checkinId.isNull()||seen.contains(revision.version.checkinId)||!revision.createdUtc.isValid()||revision.label.trimmed().isEmpty())throw std::invalid_argument("Dati delle revisioni del modello non verificati.");
        seen.insert(revision.version.checkinId);QString description=revision.comment,verified;
        for(const auto &note:sheet.revisionTable.notes)if(note.checkinId==revision.version.checkinId){description=note.description;verified=note.verifiedBy;break;}
        rows.push_back({revision.label,revision.createdUtc.toLocalTime().date().toString(Qt::ISODate),description,revision.author,verified});
    }
    if(rows.empty())rows.push_back({QStringLiteral("-"),QStringLiteral("-"),QStringLiteral("Nessun check-in del modello verificato"),QString(),QString()});
    return rows;
}
const double revisionWidths[]={17,23,82,27,26};
double wrappedText(QPainter *painter,const QRectF &rect,const QString &value,double height){
    QFont font(QStringLiteral("DejaVu Sans"));font.setPixelSize(100);const double scale=height/100;
    QString normalized=value;normalized.replace('\n',QChar::LineSeparator);QTextLayout layout(normalized,font);QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);layout.setTextOption(option);layout.beginLayout();double y=0;
    while(true){auto line=layout.createLine();if(!line.isValid())break;line.setLineWidth(rect.width()/scale);line.setPosition({0,y});y+=line.height();}layout.endLayout();
    if(painter){painter->save();painter->translate(rect.topLeft());painter->scale(scale,scale);layout.draw(painter,{0,0});painter->restore();}
    return y*scale;
}
void paintRevisionTable(QPainter &painter,const DrawingSheet &sheet,const DrawingPageContext &context){
    if(!sheet.revisionTable.visible)return;
    DrawingRevisionTableLayout layout;const auto error=layoutDrawingRevisionTable(sheet,context,layout);
    if(!error.isEmpty()){painter.save();painter.setPen(Qt::red);text(painter,{12,12,sheet.size.width()-24,8},error,2.5);painter.restore();return;}
    const auto rows=revisionCells(sheet,context);const auto rect=layout.rect;
    painter.save();painter.setPen(QPen(QColor("#222222"),.25));painter.setBrush(Qt::white);painter.drawRect(rect);painter.setBrush(Qt::NoBrush);
    text(painter,{rect.x()+2,rect.y()+.5,rect.width()-4,5},QStringLiteral("REVISIONI DEL MODELLO"),2.5);
    double y=rect.top()+6;painter.drawLine(QPointF(rect.left(),y),QPointF(rect.right(),y));
    const RevisionCells headings={QStringLiteral("REV."),QStringLiteral("DATA"),QStringLiteral("DESCRIZIONE"),QStringLiteral("AUTORE"),QStringLiteral("VERIFICA")};double x=rect.left();
    for(int column=0;column<5;++column){text(painter,{x+1,y,revisionWidths[column]-2,6},headings[column],1.9);x+=revisionWidths[column];if(column<4)painter.drawLine(QPointF(x,y),QPointF(x,rect.bottom()));}
    y+=6;painter.drawLine(QPointF(rect.left(),y),QPointF(rect.right(),y));
    for(std::size_t i=0;i<rows.size();++i){x=rect.left();for(int column=0;column<5;++column){wrappedText(&painter,{x+1.5,y+1,revisionWidths[column]-3,layout.rowHeights[int(i)]-2},rows[i][column],layout.fontHeight);x+=revisionWidths[column];}y+=layout.rowHeights[int(i)];painter.drawLine(QPointF(rect.left(),y),QPointF(rect.right(),y));}
    painter.restore();
}
}
QString layoutDrawingRevisionTable(const DrawingSheet &sheet,const DrawingPageContext &context,DrawingRevisionTableLayout &result){
    try{
        if(!sheet.revisionTable.visible){result={};return {};}
        if(sheet.size.width()<195)return QStringLiteral("Foglio troppo stretto per la tabella revisioni.");
        const auto rows=revisionCells(sheet,context);const double bottom=sheet.size.height()-(sheet.titleBlock.visible?54:26),available=bottom-12;
        for(double font:{2.5,2.3,2.1,1.9,1.7,1.6}){DrawingRevisionTableLayout layout;layout.fontHeight=font;double total=12;
            for(const auto &row:rows){double height=4;for(int column=0;column<5;++column)height=std::max(height,2+wrappedText(nullptr,{0,0,revisionWidths[column]-3,1e6},row[column],font));layout.rowHeights.append(height);total+=height;}
            if(total<=available){layout.rect={sheet.size.width()-185,bottom-total,175,total};result=std::move(layout);return {};}
        }
        return QStringLiteral("Tabella revisioni troppo alta: scegliere un foglio piu' grande o ridurre i testi. Nessuna riga e' stata troncata.");
    }catch(const std::exception &e){return QString::fromUtf8(e.what());}
}

void paintDrawingPage(QPainter &painter,const DrawingSheet &sheet,const DrawingPageContext &context){
    painter.save();painter.setPen(QPen(QColor("#222222"),.25));painter.setBrush(Qt::NoBrush);const QRectF frame(10,10,sheet.size.width()-20,sheet.size.height()-20);painter.drawRect(frame);
    paintRevisionTable(painter,sheet,context);
    if(!sheet.titleBlock.visible){text(painter,QRectF(14,sheet.size.height()-21,sheet.size.width()-28,7),sheet.title+QStringLiteral("  |  Quote in mm"),3);painter.restore();return;}
    const QRectF block(frame.right()-175,frame.bottom()-42,175,42);painter.setBrush(Qt::white);painter.drawRect(block);painter.setBrush(Qt::NoBrush);
    const auto line=[&](double x1,double y1,double x2,double y2){painter.drawLine(block.topLeft()+QPointF(x1,y1),block.topLeft()+QPointF(x2,y2));};
    line(0,12,175,12);line(0,23,175,23);line(0,33,175,33);line(105,0,105,23);line(60,23,60,33);line(117,23,117,33);line(60,33,60,42);line(117,33,117,42);
    const auto cell=[&](double x,double y,double w,double h,const QString &label,const QString &value){const QPointF origin=block.topLeft()+QPointF(x,y);text(painter,QRectF(origin+QPointF(2,.7),QSizeF(w-4,3)),label,1.7);text(painter,QRectF(origin+QPointF(2,3.5),QSizeF(w-4,h-4)),value.isEmpty()?QStringLiteral("-"):value,2.6);};
    cell(0,0,105,12,QStringLiteral("TITOLO"),sheet.title);cell(105,0,70,12,QStringLiteral("CODICE TAVOLA"),sheet.titleBlock.code);
    cell(0,12,105,11,QStringLiteral("MODELLO"),context.modelName);cell(105,12,70,11,QStringLiteral("ORGANIZZAZIONE"),sheet.titleBlock.organization);
    cell(0,23,60,10,QStringLiteral("DISEGNATO DA"),sheet.titleBlock.author);cell(60,23,57,10,QStringLiteral("DATA"),sheet.titleBlock.date);cell(117,23,58,10,QStringLiteral("SCALA / UNITA'"),QStringLiteral("%1 / mm").arg([&]{if(drawingUsesSheetScale(sheet))return drawingScaleText(sheet.scale);if(sheet.drawing.views.empty())return QStringLiteral("-");const double scale=sheet.drawing.views[0].scale;for(const auto &view:sheet.drawing.views)if(view.scale!=scale)return QStringLiteral("Vedi viste");return drawingScaleText(scale);}()));
    cell(0,33,60,9,QStringLiteral("REVISIONE TAVOLA"),context.drawingRevision);cell(60,33,57,9,QStringLiteral("REVISIONE MODELLO"),context.modelRevision);cell(117,33,58,9,QStringLiteral("FORMATO / FOGLIO"),drawingPaperName(sheet.size)+QStringLiteral(" / %1 di %2").arg(context.sheetNumber).arg(context.sheetCount));painter.restore();
}
}
