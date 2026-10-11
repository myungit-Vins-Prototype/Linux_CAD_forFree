#include "cad_curve_extract.h"
#include "cad_datum.h"
#include "fk_curve.h"
#include "fk_intersect.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ForgeCad {
namespace {
using namespace Kernel;
constexpr double tolerance = 1e-7;
bool straight(const Curve<3> &c) {
    if(c.type()==CurveType::Trimmed) return straight(*static_cast<const TrimmedCurve<3>&>(c).basis());
    if(c.type()==CurveType::Transformed) return straight(*static_cast<const TransformedCurve&>(c).basis());
    return c.type()==CurveType::Line;
}
struct Cut {int piece; double parameter, fraction;};
}
QVector<ForgeCurve> extractCurves3D(const CurveExtractionParameters &p, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
    QString *error, QVector<double> *cutFractions) {
    if(error) error->clear();
    if(cutFractions) cutFractions->clear();
    const auto fail=[&](const QString &message){if(error)*error=message;return QVector<ForgeCurve>{};};
    try {
        if(!std::isfinite(p.cutHint) || p.cutHint<0 || p.cutHint>1) return fail(QStringLiteral("Posizione di taglio non valida."));
        std::vector<PathSegment> source,tool;
        QString reason;
        if(!geometryRefPath(p.source,owner,sketches,bodies,source,&reason)) return fail(QStringLiteral("Bordo sorgente: %1.").arg(reason));
        for(const auto &s:source)
            if(!s.curve || !std::isfinite(s.range.lo) || !std::isfinite(s.range.hi) || s.range.length()<=0)
                return fail(QStringLiteral("La sorgente deve essere una curva limitata non degenere."));
        QVector<ForgeCurve> result;
        if(!p.trimmed) {
            for(const auto &s:source) result.append(std::make_shared<TrimmedCurve<3>>(s.curve,s.range.lo,s.range.hi));
            return result;
        }
        if(!geometryRefPath(p.tool,owner,sketches,bodies,tool,&reason)) return fail(QStringLiteral("Segmento di taglio: %1.").arg(reason));
        if(tool.size()!=1 || !tool[0].curve || !straight(*tool[0].curve)) return fail(QStringLiteral("Lo strumento di taglio deve essere un segmento rettilineo limitato."));
        const auto &line=tool[0];
        if(!std::isfinite(line.range.lo) || !std::isfinite(line.range.hi) || line.range.length()<=0)
            return fail(QStringLiteral("Lo strumento di taglio deve avere estremi finiti distinti."));
        const Vec3 a=line.curve->point(line.range.lo),b=line.curve->point(line.range.hi);
        if(!std::isfinite(norm(a)) || !std::isfinite(norm(b)) || distance(a,b)<=tolerance) return fail(QStringLiteral("Il segmento di taglio ha lunghezza nulla."));
        std::vector<Cut> cuts;
        const auto add=[&](int k,double t){
            const auto &s=source[k];
            if(t<s.range.lo-1e-12*s.range.length() || t>s.range.hi+1e-12*s.range.length()) return;
            t=std::clamp(t,s.range.lo,s.range.hi);
            if((k==0 && t<=s.range.lo+1e-12*s.range.length()) || (k==int(source.size())-1 && t>=s.range.hi-1e-12*s.range.length())) return;
            const double fraction=(k+(t-s.range.lo)/s.range.length())/source.size();
            for(const auto &old:cuts) if(std::fabs(old.fraction-fraction)<1e-10) return;
            cuts.push_back({k,t,fraction});
        };
        if(p.projected) {
            const Vec3 normal(p.view.normal[0],p.view.normal[1],p.view.normal[2]);
            const Vec3 x(p.view.xAxis[0],p.view.xAxis[1],p.view.xAxis[2]);
            if(!std::isfinite(norm(normal)) || !std::isfinite(norm(x)) || norm(normal)<1e-12 || norm(cross(normal,x))<1e-12)
                return fail(QStringLiteral("Direzione della vista di taglio non valida."));
            const Vec3 origin(p.view.origin[0],p.view.origin[1],p.view.origin[2]);
            if(!std::isfinite(norm(origin))) return fail(QStringLiteral("Origine della vista di taglio non valida."));
            const Frame3 frame(origin,normal,x);
            const auto image=planarImage(*line.curve,line.range,frame);
            if(!image.curve) return fail(QStringLiteral("Il segmento e' visto di punta: scegli un'altra vista di taglio."));
            for(int k=0;k<int(source.size());++k) {
                const auto curve=planarImage(*source[k].curve,source[k].range,frame);
                if(!curve.curve) continue;
                const auto hits=intersectCurves(*curve.curve,curve.range,*image.curve,image.range,tolerance,true);
                if(hits.overlap) return fail(QStringLiteral("Curva e segmento si sovrappongono nella vista: il taglio e' ambiguo."));
                for(const auto &hit:hits.points) add(k,curve.toCurve ? curve.toCurve(hit.s) : hit.s);
            }
        } else {
            const Vec3 direction=normalized(b-a);
            const Vec3 auxiliary=std::fabs(direction.x())<0.9 ? Vec3(1,0,0) : Vec3(0,1,0);
            const Vec3 n1=normalized(cross(direction,auxiliary)),n2=cross(direction,n1);
            const auto onSegment=[&](const Vec3 &q){
                const double d=dot(q-a,direction),size=distance(a,b);
                return d>=-tolerance && d<=size+tolerance && distance(q,a+std::clamp(d,0.0,size)*direction)<=tolerance;
            };
            for(int k=0;k<int(source.size());++k) {
                const auto &s=source[k];
                const auto roots=planeRoots<3>(*s.curve,s.range,n1,dot(n1,a),tolerance);
                for(double t:roots.parameters) if(onSegment(s.curve->point(t))) add(k,t);
                for(const auto &interval:roots.coincident) {
                    const auto other=planeRoots<3>(*s.curve,interval,n2,dot(n2,a),tolerance);
                    for(double t:other.parameters) if(onSegment(s.curve->point(t))) add(k,t);
                    for(const auto &overlap:other.coincident) {
                        const auto atStart=planeRoots<3>(*s.curve,overlap,direction,dot(direction,a),tolerance);
                        const auto atEnd=planeRoots<3>(*s.curve,overlap,direction,dot(direction,b),tolerance);
                        if(onSegment(s.curve->point(overlap.lo)) || onSegment(s.curve->point(overlap.hi))
                           || !atStart.parameters.empty() || !atEnd.parameters.empty()
                           || !atStart.coincident.empty() || !atEnd.coincident.empty())
                            return fail(QStringLiteral("La curva si sovrappone al segmento: il taglio e' ambiguo."));
                    }
                }
            }
        }
        std::sort(cuts.begin(),cuts.end(),[](const Cut&a,const Cut&b){return a.fraction<b.fraction;});
        if(cutFractions) for(const auto &cut:cuts) cutFractions->append(cut.fraction);
        if(cuts.empty()) return fail(p.projected ? QStringLiteral("Il segmento non taglia l'interno della curva nella vista salvata.")
                                               : QStringLiteral("Il segmento non interseca l'interno della curva nello spazio 3D."));
        const Cut cut=*std::min_element(cuts.begin(),cuts.end(),[&](const Cut&a,const Cut&b){return std::fabs(a.fraction-p.cutHint)<std::fabs(b.fraction-p.cutHint);});
        for(int k=0;k<int(source.size());++k) {
            if((p.keepStart && k>cut.piece) || (!p.keepStart && k<cut.piece)) continue;
            double lo=source[k].range.lo,hi=source[k].range.hi;
            if(k==cut.piece) {if(p.keepStart)hi=cut.parameter;else lo=cut.parameter;}
            if(hi-lo>1e-12*source[k].range.length())
                result.append(std::make_shared<TrimmedCurve<3>>(source[k].curve,lo,hi));
        }
        return result;
    } catch(const std::exception &e) {return fail(QString::fromUtf8(e.what()));}
}
}
