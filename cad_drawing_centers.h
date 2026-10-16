#pragma once
#include "cad_drawing_dimension.h"
#include "fk_cancel.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace ForgeCad {
// Geometria grafica in mm sul foglio, relativa all'origine della vista.
// Nessun riferimento persistente a indici di mesh o a una simmetria presunta.
struct DrawingCenterGeometry { QPainterPath axes,crosses; };
inline DrawingCenterGeometry drawingCenterGeometry(const DrawingView &view,
    const std::vector<DrawingCurve> &curves,const std::vector<bool> &eligible){
    using namespace Kernel;
    struct Axis { QPointF center,direction;double radius=0; };
    // La quantizzazione serve solo a evitare tratti grafici sovrapposti (0,1 nm
    // sul foglio). Non modifica tolleranze o riferimenti geometrici delle quote.
    using Key=std::tuple<qint64,qint64,qint64>;
    std::map<Key,Axis> axes;
    for(std::size_t i=0;i<curves.size();++i){
        cancellationPoint();
        if(i>=eligible.size()||!eligible[i]||!curves[i].curve)continue;
        const auto p=drawingPrimitive(view,curves[i]);
        if(p.kind!=DrawingPrimitive::Kind::Conic)continue;
        const auto a=p.firstAxis,b=p.secondAxis;
        const double xx=a.x()*a.x()+b.x()*b.x(),yy=a.y()*a.y()+b.y()*b.y(),xy=a.x()*a.y()+b.x()*b.y();
        const double major2=.5*(xx+yy+std::hypot(xx-yy,2*xy));
        const double determinant=a.x()*b.y()-a.y()*b.x();
        // Il determinante evita cancellazione nel semiasse minore di un'ellisse
        // quasi di taglio. Una conica degenerata non ha due assi nel piano.
        const double minor2=major2>0?determinant*determinant/major2:0;
        if(!std::isfinite(major2)||!std::isfinite(minor2)||minor2<=view.tolerance*view.tolerance)continue;
        const double angle=p.circular?0:.5*std::atan2(2*xy,xx-yy);
        const QPointF center(p.center.x()*view.scale,-p.center.y()*view.scale);
        if(!std::isfinite(center.x())||!std::isfinite(center.y())||std::abs(center.x())>1e8||std::abs(center.y())>1e8)continue;
        for(int k=0;k<2;++k){
            double theta=std::fmod(angle+k*kPi/2+kPi,kPi);
            if(theta<0)theta+=kPi;
            if(std::abs(theta-kPi)<1e-12)theta=0;
            const QPointF direction(std::cos(theta),-std::sin(theta));
            const double radius=std::sqrt(k?minor2:major2)*view.scale;
            if(!std::isfinite(radius)||radius>1e8)continue;
            const Key key{std::llround(center.x()*1e7),std::llround(center.y()*1e7),std::llround(theta*1e8)};
            auto [it,inserted]=axes.emplace(key,Axis{center,direction,radius});
            if(!inserted)it->second.radius=std::max(it->second.radius,radius);
        }
    }
    DrawingCenterGeometry result;
    for(const auto &[key,axis]:axes){
        (void)key;const auto extent=(axis.radius+2)*axis.direction;
        // La mezza sezione ha gia' un asse verticale continuo nell'editor.
        // Non sovrapporre due tratto-punto con fasi diverse sullo stesso asse.
        if(!(drawingHalfSection(view)&&std::abs(axis.center.x())<1e-7&&std::abs(axis.direction.x())<1e-8)){
            result.axes.moveTo(axis.center-extent);result.axes.lineTo(axis.center+extent);
        }
        const auto cross=.8*axis.direction;
        result.crosses.moveTo(axis.center-cross);result.crosses.lineTo(axis.center+cross);
    }
    return result;
}
}
