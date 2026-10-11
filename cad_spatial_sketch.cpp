#include "cad_spatial_sketch.h"
#include "fk_bspline.h"
#include <cmath>
#include <set>
namespace ForgeCad {
QVector<ForgeCurve> buildSpatialSketch(const SpatialSketchParameters &sketch, QString *error) {
    using namespace Kernel;
    if(error)error->clear();
    const auto fail=[&](const QString &message){if(error)*error=message;return QVector<ForgeCurve>{};};
    if(sketch.entities.isEmpty())return fail(QStringLiteral("Disegna almeno una polilinea o una spline 3D."));
    QVector<ForgeCurve> curves;std::set<int> ids;
    try {
        for(const auto &entity:sketch.entities) {
            if(entity.id<=0 || entity.id>=sketch.nextId || !ids.insert(entity.id).second)
                return fail(QStringLiteral("Identita' delle entita' 3D non valida."));
            if(entity.points.size()<2 || (entity.closed && entity.points.size()<3))
                return fail(QStringLiteral("Servono almeno due punti, oppure tre per chiudere una curva."));
            std::vector<Vec3> points;
            for(const auto &p:entity.points) {
                if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    return fail(QStringLiteral("Le coordinate XYZ devono essere finite."));
                const Vec3 q(p.x,p.y,p.z);
                if(!points.empty() && distance(points.back(),q)<1e-9)
                    return fail(QStringLiteral("Due punti consecutivi dello schizzo 3D coincidono."));
                points.push_back(q);
            }
            if(entity.closed) {
                if(distance(points.front(),points.back())<1e-9)
                    return fail(QStringLiteral("Per chiudere la curva usa Chiusa, senza ripetere il primo punto."));
                points.push_back(points.front());
            }
            if(points.size()==2) {
                curves.append(std::make_shared<TrimmedCurve<3>>(std::make_shared<Line<3>>(points[0],points[1]-points[0]),0,distance(points[0],points[1])));
                continue;
            }
            const int count=int(points.size());std::vector<double> knots;std::vector<Vec3> poles;
            if(!entity.spline) {
                knots.push_back(0);for(int i=0;i<count;++i)knots.push_back(i);knots.push_back(count-1);
                curves.append(std::make_shared<BSplineCurve<3>>(1,std::move(knots),std::move(points)));
                continue;
            }
            // Hermite cubica interpolante a tratti: tangenti condivise nei nodi,
            // senza appiattire i punti 3D su un piano e senza fit dei campioni.
            std::vector<Vec3> tangents(count);
            for(int i=0;i<count;++i) {
                if(entity.closed && (i==0 || i==count-1))tangents[i]=(points[1]-points[count-2])/2;
                else if(i==0)tangents[i]=points[1]-points[0];
                else if(i==count-1)tangents[i]=points[i]-points[i-1];
                else tangents[i]=(points[i+1]-points[i-1])/2;
            }
            for(int i=0;i<count;++i) {
                if(i>0)poles.push_back(points[i]-tangents[i]/3);
                poles.push_back(points[i]);
                if(i<count-1)poles.push_back(points[i]+tangents[i]/3);
                for(int k=0;k<(i==0 || i==count-1?4:3);++k)knots.push_back(i);
            }
            curves.append(std::make_shared<BSplineCurve<3>>(3,std::move(knots),std::move(poles)));
        }
    }catch(const std::exception &e){return fail(QString::fromUtf8(e.what()));}
    return curves;
}
}
