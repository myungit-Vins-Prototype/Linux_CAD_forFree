#ifndef FORGECAD_DRAWING_TOLERANCES_H
#define FORGECAD_DRAWING_TOLERANCES_H
#include "cad_drawing_dimension.h"
#include <algorithm>
#include <cmath>
namespace ForgeCad {
enum class DrawingToleranceMode { None, Symmetric, Deviations, Limits };
struct DrawingDimensionPresentation {
    DrawingToleranceMode mode=DrawingToleranceMode::None;
    double upper=0,lower=0; // scostamenti dal nominale esatto, nelle unita' della quota
    int decimals=3;
    QString prefix,suffix;
    bool reverseArrows=false;
    bool numberingExcluded=false; // nasconde il badge e conserva il posto nella sequenza
    double extensionSlant=0; // gradi rispetto alla perpendicolare alla linea di quota
    bool operator==(const DrawingDimensionPresentation &p)const{return mode==p.mode&&upper==p.upper&&lower==p.lower&&decimals==p.decimals&&prefix==p.prefix&&suffix==p.suffix&&reverseArrows==p.reverseArrows&&numberingExcluded==p.numberingExcluded&&extensionSlant==p.extensionSlant;}
    bool active()const{return !(*this==DrawingDimensionPresentation{});}
};
inline QString validateDrawingDimensionPresentation(const DrawingDimensionPresentation &p){
    if(!std::isfinite(p.extensionSlant)||std::abs(p.extensionSlant)>80)return QStringLiteral("Inclinazione dei richiami non valida (−80°…80°).");
    if(int(p.mode)<0||int(p.mode)>3||p.decimals<0||p.decimals>8||!std::isfinite(p.upper)||!std::isfinite(p.lower)||std::abs(p.upper)>1e6||std::abs(p.lower)>1e6)
        return QStringLiteral("Tolleranza o precisione non valida.");
    for(const auto &s:{p.prefix,p.suffix}){
        if(s.size()>128)return QStringLiteral("Prefisso o suffisso troppo lungo (massimo 128 caratteri).");
        for(auto c:s)if(!c.isPrint())return QStringLiteral("Prefisso e suffisso devono essere su una sola riga.");
    }
    if(p.mode==DrawingToleranceMode::None&&(p.upper!=0||p.lower!=0))return QStringLiteral("Eliminare gli scostamenti quando la tolleranza e' disattivata.");
    if(p.upper<p.lower)return QStringLiteral("Lo scostamento superiore deve essere maggiore o uguale a quello inferiore.");
    if(p.mode==DrawingToleranceMode::Symmetric&&(p.upper<0||p.lower!=-p.upper))return QStringLiteral("La tolleranza simmetrica richiede scostamenti opposti e ampiezza positiva.");
    const double factor=std::pow(10.,p.decimals);
    for(double x:{p.upper,p.lower})if((x!=0&&std::round(x*factor)==0)||std::abs(x*factor-std::round(x*factor))>1e-6)
        return QStringLiteral("Aumentare i decimali della tolleranza per rappresentare gli scostamenti senza arrotondarli.");
    return {};
}
inline QString drawingToleranceNumber(double x,int decimals,bool sign=false){
    if(std::abs(x)<.5*std::pow(10.,-decimals))x=0; // evita -0,000
    return (sign&&x>0?QStringLiteral("+"):QString())+QString::number(x,'f',decimals);
}
struct DrawingDimensionLabel {
    QString leading,upper,lower,trailing,summary,problem;
    bool stacked()const{return !upper.isEmpty();}
};
inline DrawingDimensionLabel drawingDimensionLabel(const DrawingDimension &d,double nominal,const DrawingDimensionPresentation &p,bool diametral=false){
    DrawingDimensionLabel t;t.problem=validateDrawingDimensionPresentation(p);
    if(t.problem.isEmpty()&&(!std::isfinite(nominal)||(nominal<0&&!drawingOrdinate(d.measure))))t.problem=QStringLiteral("Misura nominale non valida.");
    if(t.problem.isEmpty()&&!drawingOrdinate(d.measure)&&p.mode!=DrawingToleranceMode::None&&nominal+p.lower<0)t.problem=QStringLiteral("Il limite inferiore della quota e' negativo: verificare la tolleranza.");
    if(!t.problem.isEmpty()){t.leading=t.summary=QStringLiteral("Tolleranza da verificare");return t;}
    const bool angle=d.measure==DrawingMeasure::Angle;
    const QString unit=angle?QStringLiteral("°"):QStringLiteral(" mm");
    const QString symbol=d.measure==DrawingMeasure::ProfileRadius?QStringLiteral("R profilo "):d.measure==DrawingMeasure::OrdinateX?QStringLiteral("X = "):d.measure==DrawingMeasure::OrdinateY?QStringLiteral("Y = "):d.measure==DrawingMeasure::Radius||d.measure==DrawingMeasure::AxisRadius?QStringLiteral("R "):diametral||d.measure==DrawingMeasure::Diameter||d.measure==DrawingMeasure::AxisDiameter?QStringLiteral("⌀ "):d.measure==DrawingMeasure::SectionLength?QStringLiteral("L = "):QString();
    const bool hasDiameter=p.prefix.contains(QChar(0x2300))||p.prefix.contains(QChar(0x00d8));
    t.leading=p.prefix+((symbol==QStringLiteral("⌀ ")&&hasDiameter)?QString():symbol);
    if(p.mode==DrawingToleranceMode::Limits){
        const int precision=std::max(d.decimals,p.decimals);
        t.upper=drawingToleranceNumber(nominal+p.upper,precision);t.lower=drawingToleranceNumber(nominal+p.lower,precision);
        if(p.upper!=p.lower&&t.upper==t.lower){t.problem=QStringLiteral("L'intervallo dei limiti non e' rappresentabile alla precisione disponibile.");t.leading=t.summary=QStringLiteral("Tolleranza da verificare");t.upper.clear();t.lower.clear();return t;}
    }else{
        t.leading+=drawingToleranceNumber(nominal,d.decimals);
        if(p.mode==DrawingToleranceMode::Symmetric)t.leading+=QStringLiteral(" ±")+drawingToleranceNumber(p.upper,p.decimals);
        if(p.mode==DrawingToleranceMode::Deviations){t.upper=drawingToleranceNumber(p.upper,p.decimals,true);t.lower=drawingToleranceNumber(p.lower,p.decimals,true);}
    }
    t.trailing=unit+p.suffix;
    t.summary=t.leading+(t.stacked()?QStringLiteral(" [%1 / %2]").arg(t.upper,t.lower):QString())+t.trailing;
    return t;
}
}
#endif
