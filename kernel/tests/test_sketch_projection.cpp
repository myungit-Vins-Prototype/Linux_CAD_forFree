#include "fk_sketch_projection.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_intersect.h"
#include "fk_step.h"
#include "fk_iges.h"
#include "fk_helix.h"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace ForgeCad::Kernel;
static void require(bool condition, const char *message) {if (!condition) throw std::runtime_error(message);}
static CurvePtr<3> bezier(std::vector<Vec3> poles) {
    const int degree = int(poles.size()) - 1;
    std::vector<double> knots(std::size_t(degree + 1), 0.0);
    knots.insert(knots.end(), std::size_t(degree + 1), 1.0);
    return std::make_shared<BSplineCurve<3>>(degree, knots, std::move(poles));
}
static std::vector<SketchProjectionSpan> span(CurvePtr<3> curve) {return {{curve, curve->domain()}};}
template<class Function> static void rejects(Function function, const char *message) {
    bool rejected = false;
    try {function();} catch (const std::domain_error &) {rejected = true;}
    require(rejected, message);
}
static void projectionResidual(const std::vector<CurvePtr<3>> &curves, const CurvePtr<3> &a, const Vec3 &na,
    const CurvePtr<3> &b, const Vec3 &nb, double tolerance = 2e-10) {
    const Frame3 fa = normalFrame(na, Vec3()), fb = normalFrame(nb, Vec3());
    const auto imageA = planarImage(*a, a->domain(), fa), imageB = planarImage(*b, b->domain(), fb);
    for (const auto &curve : curves) for (int k = 0; k <= 100; ++k) {
        const auto d = curve->domain();
        const Vec3 point = curve->point(d.lo + d.length() * k / 100.0);
        const Vec3 localA = fa.toLocal(point), localB = fb.toLocal(point);
        require(projectPoint(*imageA.curve, Vec2(localA.x(), localA.y()), imageA.range).distance < tolerance, "prima proiezione esatta");
        require(projectPoint(*imageB.curve, Vec2(localB.x(), localB.y()), imageB.range).distance < tolerance, "seconda proiezione esatta");
    }
}
int main() {
    int passed = 0;
    try {
        const Vec3 n1(0, 0, 1), n2(0, 1, 0);
        // x diverso nei parametri: t^2 contro s. Una somma a pari t
        // fallirebbe il residuo su almeno una delle due proiezioni.
        const auto a = bezier({Vec3(0,0,0), Vec3(0,2,0), Vec3(4,3,0)});
        const auto line = bezier({Vec3(1,0,2), Vec3(3,0,6)});
        const auto rational = projectSketchOnSketch(span(a), n1, span(line), n2);
        require(rational.size() == 1 && !isImplicitSketchProjection(*rational[0]), "curva razionale esatta da retta");
        projectionResidual(rational, a, n1, line, n2);
        require(std::fabs(rational[0]->point(rational[0]->domain().lo).x() - 1.0) < 1e-12, "range comune iniziale");
        require(std::fabs(rational[0]->point(rational[0]->domain().hi).x() - 3.0) < 1e-12, "range comune finale"); ++passed;

        const auto b = bezier({Vec3(0,0,1), Vec3(1,0,4), Vec3(4,0,2)});
        const auto implicit = projectSketchOnSketch(span(a), n1, span(b), n2);
        require(implicit.size() == 1 && isImplicitSketchProjection(*implicit[0]), "intersezione implicita conservata");
        projectionResidual(implicit, a, n1, b, n2);
        for (double fraction : {.0,.2,.5,.8,1.0}) {
            Vec3 jet[4]; implicit[0]->evaluate(implicit[0]->domain().hi * fraction, 3, jet);
            for (const auto &v : jet) require(std::isfinite(norm(v)), "derivate implicite finite");
        } ++passed;

        // Cambio di piani e origine: nessuna ipotesi XY/XZ o ortogonalita'.
        const Frame3 tilted(Vec3(12,-8,3), normalized(Vec3(.2,.8,.55)), Vec3(1,.1,.2));
        const Transform3 rotation = Transform3::fromFrame(tilted);
        const auto ta = std::make_shared<TransformedCurve>(a, rotation);
        const auto tb = std::make_shared<TransformedCurve>(b, rotation);
        const Vec3 tn1 = rotation.applyToVector(n1), tn2 = rotation.applyToVector(normalized(Vec3(0,1,.4)));
        // Il secondo input deve giacere sul secondo piano; modifica il suo
        // asse z nel piano ortogonale alla nuova normale prima della rotazione.
        const Frame3 secondPlane(Vec3(), normalized(Vec3(0,1,.4)), Vec3(1,0,0));
        const Transform3 slant = Transform3::fromFrame(secondPlane);
        const auto slanted = bezier({slant.applyToPoint(Vec3(0,-1,0)), slant.applyToPoint(Vec3(1,-4,0)), slant.applyToPoint(Vec3(4,-2,0))});
        const auto ts = std::make_shared<TransformedCurve>(slanted, rotation);
        const auto arbitrary = projectSketchOnSketch(span(ta), tn1, span(ts), tn2);
        projectionResidual(arbitrary, ta, tn1, ts, tn2, 1e-9); ++passed;

        const auto disconnectedA = bezier({Vec3(0,0,0),Vec3(2,0,0)});
        const auto outside = bezier({Vec3(3,0,0),Vec3(4,0,2)});
        rejects([&]{projectSketchOnSketch(span(disconnectedA),n1,span(outside),n2);}, "nessun intervallo comune");
        rejects([&]{projectSketchOnSketch(span(a),n1,span(b),n1);}, "piani paralleli"); ++passed;

        // Due parabole con verso invertito di x formano due rami distinti.
        const auto folded = bezier({Vec3(0,0,0),Vec3(4,1,0),Vec3(0,2,0)});
        const auto across = bezier({Vec3(.5,0,0),Vec3(1.5,0,2)});
        const auto multiple = projectSketchOnSketch(span(folded),n1,span(across),n2);
        require(multiple.size() == 2, "tutti i rami conservati");
        projectionResidual(multiple,folded,n1,across,n2); ++passed;

        // Una curva a coordinata costante produce un segmento finito,
        // non un'estensione infinita o una curva inventata.
        const auto constant = bezier({Vec3(1,-2,0),Vec3(1,3,0)});
        const auto transverse = bezier({Vec3(0,0,0),Vec3(2,0,4)});
        const auto straight = projectSketchOnSketch(span(constant),n1,span(transverse),n2);
        require(straight.size() == 1 && !isImplicitSketchProjection(*straight[0]), "generatrice costante esatta");
        require(std::fabs(arcLength(*straight[0])-5.0)<1e-10, "generatrice limitata dalla curva sorgente"); ++passed;

        // Degenerazione: due superfici coincidono su un'area.
        const auto constant2 = bezier({Vec3(1,0,-2),Vec3(1,0,3)});
        rejects([&]{projectSketchOnSketch(span(constant),n1,span(constant2),n2);}, "coincidenza non univoca"); ++passed;

        // Due coordinate comuni identiche non impongono un fit: anche due
        // curve non rettilinee possono produrre una NURBS esatta.
        const auto sameParameter = bezier({Vec3(0,0,1),Vec3(0,0,4),Vec3(4,0,2)});
        const auto compatible = projectSketchOnSketch(span(a),n1,span(sameParameter),n2);
        require(compatible.size()==1 && !isImplicitSketchProjection(*compatible[0]), "NURBS esatta con parametro razionale comune");
        projectionResidual(compatible,a,n1,sameParameter,n2); ++passed;

        // Controlla le formule delle derivate con un confronto indipendente
        // in punti interni ai tratti, senza allentare i residui geometrici.
        const auto &ci=*implicit[0];
        for(double t:{.2,.7,1.3,1.7}) {
            Vec3 jet[4]; ci.evaluate(t,3,jet);
            const double h=1e-5;
            const Vec3 numeric=(ci.point(t+h)-ci.point(t-h))/(2*h);
            require(distance(jet[1],numeric)<1e-7,"prima derivata implicita coerente");
            const Vec3 numeric2=(ci.derivative(t+h)-ci.derivative(t-h))/(2*h);
            require(distance(jet[2],numeric2)<1e-6,"seconda derivata implicita coerente");
        } ++passed;

        const auto tangentSource=bezier({Vec3(0,0,0),Vec3(0,0,1),Vec3(0,0,2),Vec3(4,0,3)});
        rejects([&]{projectSketchOnSketch(span(a),n1,span(tangentSource),n2);}, "tangenza comune non regolare: errore esplicito"); ++passed;

        const auto touches=bezier({Vec3(0,0,0),Vec3(2,0,1),Vec3(0,0,2)});
        const auto uniqueGenerator=projectSketchOnSketch(span(constant),n1,span(touches),n2);
        require(uniqueGenerator.size()==1 && std::fabs(arcLength(*uniqueGenerator[0])-5)<1e-10,"generatrice critica non duplicata fra rami adiacenti"); ++passed;

        std::mt19937 random(39173);
        std::uniform_real_distribution<double> offset(-3.0,3.0),interior(.2,3.8);
        for(int k=0;k<40;++k) {
            const auto ra=bezier({Vec3(0,offset(random),0),Vec3(interior(random),offset(random),0),Vec3(4,offset(random),0)});
            const auto rb=bezier({Vec3(0,0,offset(random)),Vec3(interior(random),0,offset(random)),Vec3(4,0,offset(random))});
            const auto projected=projectSketchOnSketch(span(ra),n1,span(rb),n2);
            require(projected.size()==1,"una componente per due input monotoni");
            projectionResidual(projected,ra,n1,rb,n2,5e-10);
            const auto reversed=projectSketchOnSketch(span(rb),n2,span(ra),n1);
            require(reversed.size()==1,"ordine degli input scambiato");
            for(int i=0;i<=20;++i) {
                const auto d=projected[0]->domain(); const Vec3 point=projected[0]->point(d.lo+d.length()*i/20.0);
                require(projectPoint(*reversed[0],point,reversed[0]->domain()).distance<2e-8,"ordine input geometricamente invariante");
            }
        } ++passed;

        const auto trimmed=std::make_shared<TrimmedCurve<3>>(a,.2,.8);
        const auto bounded=projectSketchOnSketch(span(trimmed),n1,span(line),n2);
        projectionResidual(bounded,trimmed,n1,line,n2);
        double minimum=1e30,maximum=-1e30;
        for(const auto &curve:bounded)for(double t:{curve->domain().lo,curve->domain().hi}) {
            minimum=std::min(minimum,curve->point(t).x());maximum=std::max(maximum,curve->point(t).x());
        }
        require(std::fabs(minimum-1)<1e-12 && std::fabs(maximum-2.56)<1e-12,"domini Trimmed conservati"); ++passed;

        const auto almostParallel=bezier({Vec3(0,1,0),Vec3(4,1,0)});
        rejects([&]{projectSketchOnSketch(span(disconnectedA),n1,span(almostParallel),normalized(Vec3(0,1e-6,1)));},
            "piani quasi paralleli: risultato fuori sizeBox rifiutato prima del display"); ++passed;

        std::vector<SketchProjectionSpan> manyFirst,manySecond;
        for(int i=0;i<33;++i) {
            const auto x=bezier({Vec3(0,i,0),Vec3(4,i,0)}), y=bezier({Vec3(0,0,i),Vec3(4,0,i)});
            manyFirst.push_back({x,x->domain()});manySecond.push_back({y,y->domain()});
        }
        rejects([&]{projectSketchOnSketch(manyFirst,n1,manySecond,n2);},"tetto sui tratti generati prima della connessione"); ++passed;

        const auto polynomial=static_cast<const BSplineCurve<3> *>(a.get());
        const auto weighted=std::make_shared<BSplineCurve<3>>(polynomial->degree(),polynomial->knots(),polynomial->poles(),std::vector<double>(3,1e200));
        const auto scaledWeights=projectSketchOnSketch(span(weighted),n1,span(b),n2);
        projectionResidual(scaledWeights,a,n1,b,n2); ++passed;

        // Un peso locale piccolissimo non rende costante una coordinata
        // variabile e non deve far accettare un'identita' razionale falsa.
        const auto unresolvable=std::make_shared<BSplineCurve<3>>(2,polynomial->knots(),polynomial->poles(),std::vector<double>{1e-40,1,1e-40});
        rejects([&]{projectSketchOnSketch(span(unresolvable),n1,span(b),n2);},
            "pesi estremi non diventano coordinate costanti o NURBS fittizie"); ++passed;

        {
            auto token=std::make_shared<std::atomic<bool>>(true); CancellationScope cancel(token);
            bool cancelled=false;
            try {projectSketchOnSketch(span(a),n1,span(b),n2);}catch(const OperationCancelled &){cancelled=true;}
            require(cancelled,"cancellazione kernel propagata");
        } ++passed;

        // Anche le API kernel grezze rifiutano il fit implicito, inclusi
        // wrapper limitati/trasformati; la guardia non dipende dalla GUI.
        std::vector<CurvePtr<3>> protectedCurves{implicit[0],
            std::make_shared<TrimmedCurve<3>>(implicit[0],.1,implicit[0]->domain().hi-.1),
            std::make_shared<TransformedCurve>(implicit[0],rotation)};
        for(const auto &curve:protectedCurves) {
            ExchangeBody exchange; exchange.curve=curve; exchange.curveRange=curve->domain();
            for(bool iges:{false,true}) {
                bool blocked=false;
                try {if(iges)writeIges({exchange});else writeStep({exchange});}
                catch(const std::domain_error &error){blocked=std::string(error.what()).find("implicita")!=std::string::npos;}
                require(blocked,"writer grezzo: nessun fit nascosto di intersezione implicita");
            }
        } ++passed;

        ExchangeBody exactExchange;exactExchange.curve=rational[0];exactExchange.curveRange=rational[0]->domain();
        require(!writeStep({exactExchange}).empty() && !writeIges({exactExchange}).empty(),"NURBS esatte esportabili nei due writer"); ++passed;

        HelixSpec helix;helix.turns=.05;
        ExchangeBody legacyOther;legacyOther.curve=std::make_shared<HelixCurve>(helix);legacyOther.curveRange=legacyOther.curve->domain();
        require(!writeStep({legacyOther}).empty() && !writeIges({legacyOther}).empty(),"fallback legacy delle eliche invariato"); ++passed;

        std::cout << "PASS " << passed << " gruppi proiezione schizzo su schizzo\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "FAIL dopo " << passed << " gruppi: " << failure.what() << '\n';
        return 1;
    }
}
