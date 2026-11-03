#include "forgeCad2026_viewport.h"
#include "cad_loft_diagnostics.h"
#include "fk_tessellate.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

void CadViewport::prepareLoftDiagnostics(const QString &error) {
    previewLoftErrors_.clear();
    if(preview_.definition.feature!=BodyFeature::Loft)return;
    const auto paths=ForgeCad::loftDiagnosticPaths(ForgeCad::loftFailure(error),preview_.definition,preview_.index,sketches_,extrusions_);
    for(const auto &path:paths) {
        LoftErrorMark mark;mark.label=path.label;
        mark.color=path.guide?QColor(255,65,75):QColor(255,180,45);
        try {
            // Campioni solo per l'overlay: l'intersezione resta quella del kernel esatto.
            for(const auto &piece:path.path) {
                QVector<QVector3D> line;
                for(double parameter:ForgeCad::Kernel::sampleCurve(*piece.curve,piece.range,{})) {
                    const auto p=piece.curve->point(parameter);line.append(QVector3D(p.x(),p.y(),p.z()));
                }
                if(!line.isEmpty())mark.lines.append(std::move(line));
            }
        } catch(const std::exception &) {continue;}
        if(!mark.lines.isEmpty())previewLoftErrors_.append(std::move(mark));
    }
}

void CadViewport::drawLoftDiagnostics(QPainter &painter) const {
    if(previewLoftErrors_.isEmpty())return;
    painter.save();painter.setRenderHint(QPainter::Antialiasing);
    QFont font=painter.font();font.setBold(true);painter.setFont(font);
    QList<QRectF> occupied;
    const QRectF bounds=QRectF(rect()).adjusted(12,12,-12,-12);
    for(int index=0;index<previewLoftErrors_.size();++index) {
        const auto &mark=previewLoftErrors_[index];
        QList<QPointF> targets;
        for(const auto &line:mark.lines) {
            QPainterPath path;
            for(int i=0;i<line.size();++i) {
                const auto p=projectWorldPoint(line[i]);if(i==0)path.moveTo(p);else path.lineTo(p);
                if(bounds.contains(p))targets.append(p);
            }
            painter.setPen(QPen(QColor(25,30,40,210),7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(path);
            painter.setPen(QPen(mark.color,3.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPath(path);
        }
        if(targets.isEmpty())continue;
        const QPointF target=targets[targets.size()/2];
        const qreal maxWidth=qMax(60.0,qMin(330.0,bounds.width()-16));
        const auto text=painter.fontMetrics().boundingRect(QRect(0,0,int(maxWidth),300),Qt::TextWordWrap,mark.label);
        const QSizeF size(qMin(maxWidth,qreal(text.width()))+16,text.height()+12);
        QRectF label;
        // I pannelli e il ribbon sono figli del viewport: cerca una posizione libera.
        for(int attempt=0;attempt<16;++attempt) {
            const bool left=attempt%2;
            QPointF offset(left?-size.width()-52:52,(index?42:-size.height()-42)+(attempt/2)*48);
            QPointF pos=target+offset;
            pos.setX(qBound(bounds.left(),pos.x(),qMax(bounds.left(),bounds.right()-size.width())));
            pos.setY(qBound(bounds.top(),pos.y(),qMax(bounds.top(),bounds.bottom()-size.height())));
            label=QRectF(pos,size);bool collision=false;
            for(const auto &other:occupied)collision=collision||label.intersects(other.adjusted(-8,-8,8,8));
            for(QWidget *widget:findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))
                if(widget->isVisible() && (widget->property("functionDialogPanel").toBool() || widget->height()>70))
                    collision=collision||label.intersects(widget->geometry());
            if(!collision)break;
        }
        occupied.append(label);
        const QPointF from(qBound(label.left(),target.x(),label.right()),qBound(label.top(),target.y(),label.bottom()));
        QPointF direction=from-target;const double length=std::hypot(direction.x(),direction.y());
        if(length>1) {
            direction/=length;const QPointF side(-direction.y(),direction.x());
            painter.setPen(QPen(mark.color,2));painter.drawLine(from,target);
            painter.setPen(Qt::NoPen);painter.setBrush(mark.color);
            painter.drawPolygon(QPolygonF{target,target+direction*12+side*5,target+direction*12-side*5});
        }
        painter.setPen(QPen(mark.color,1.5));painter.setBrush(QColor(25,30,40,235));painter.drawRoundedRect(label,5,5);
        painter.setPen(mark.color);painter.drawText(label.adjusted(8,6,-8,-6),Qt::TextWordWrap|Qt::AlignVCenter,mark.label);
    }
    painter.restore();
}
