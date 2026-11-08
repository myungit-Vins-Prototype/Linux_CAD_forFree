#include "fk_blend_section_curve.h"
#include "fk_bspline.h"
#include "fk_curve_algo.h"
#include "fk_sheet.h"
#include "fk_precision.h"
#include <algorithm>
#include <cmath>
#include <exception>
namespace ForgeCad::Kernel::detail {
// Curva dello spigolo per le sezioni. Un edge rifilato da una booleana a
// meno della risoluzione da un nodo della curva tracciata comincia (o finisce)
// con un tratto di Hermite lungo qualche 1e-8: li' la derivata seconda non ha
// significato geometrico e, attraverso il piano normale, fa tornare indietro
// le righe dei contatti. Il tratto si sostituisce con il prolungamento del
// tratto vicino (stesso polinomio, extendBSpline); la curva resta quella del
// B-rep altrove e gli estremi si spostano meno della risoluzione.
static CurvePtr<3> regularSectionCurve(const CurvePtr<3> &curve, const Interval &range) {
    const auto *spline = dynamic_cast<const BSplineCurve<3> *>(curve.get());
    if (!spline)
        return curve;
    double first = range.hi, last = range.lo;
    for (double k : spline->knots())
        if (k > range.lo && k < range.hi)
            first = std::min(first, k), last = std::max(last, k);
    if (!(first < range.hi))
        return curve;
    const bool head = distance(curve->point(range.lo), curve->point(first)) <= linearResolution();
    const bool tail = distance(curve->point(last), curve->point(range.hi)) <= linearResolution();
    if (!head && !tail)
        return curve;
    try {
        BSplineCurve<3> c = spline->clamped();
        const int p = c.degree();
        const auto cut = [&](double k, bool keepRight) {
            c = c.insertKnot(k, p - c.multiplicity(k));
            const std::vector<double> &knots = c.knots();
            const std::size_t s = std::size_t(std::lower_bound(knots.begin(), knots.end(), k) - knots.begin());
            std::vector<double> newKnots;
            std::vector<Vec3> poles;
            std::vector<double> weights;
            if (keepRight) {
                newKnots.push_back(k);
                newKnots.insert(newKnots.end(), knots.begin() + std::ptrdiff_t(s), knots.end());
                for (int i = int(s) - 1; i < c.poleCount(); ++i) {
                    poles.push_back(c.poles()[std::size_t(i)]);
                    if (c.isRational())
                        weights.push_back(c.weight(i));
                }
            } else {
                newKnots.assign(knots.begin(), knots.begin() + std::ptrdiff_t(s + std::size_t(p)));
                newKnots.push_back(k);
                for (int i = 0; i < int(s); ++i) {
                    poles.push_back(c.poles()[std::size_t(i)]);
                    if (c.isRational())
                        weights.push_back(c.weight(i));
                }
            }
            c = BSplineCurve<3>(p, newKnots, poles, weights);
        };
        if (head)
            cut(first, true);
        if (tail && last > first)
            cut(last, false);
        const auto regular = std::make_shared<BSplineCurve<3>>(extendBSpline(c, range.lo, range.hi));
        if (distance(regular->point(range.lo), curve->point(range.lo)) > linearResolution() ||
            distance(regular->point(range.hi), curve->point(range.hi)) > linearResolution())
            return curve;
        return regular;
    } catch (const std::exception &) {
        return curve;
    }
}

CurvePtr<3> blendSectionCurve(const CurvePtr<3> &input, const Interval &range, double reach) {
    const auto curve = regularSectionCurve(input, range);
    const auto *spline = dynamic_cast<const BSplineCurve<3> *>(curve.get());
    if (!spline || !(reach > 0.0))
        return curve;
    const double leftSpeed = norm(curve->derivative(range.lo));
    const double rightSpeed = norm(curve->derivative(range.hi));
    if (!(leftSpeed > 0.0 && rightSpeed > 0.0))
        return curve;
    try {
        // Stessi polinomi su un intervallo piu' ampio: evita la cancellazione
        // dell'estrapolazione su campate terminali di pochi ULP. Non e' un fit.
        return std::make_shared<BSplineCurve<3>>(extendBSpline(*spline, range.lo - reach / leftSpeed, range.hi + reach / rightSpeed));
    } catch (const std::domain_error &) {
        // Un prolungamento razionale puo' attraversare un polo: il percorso
        // precedente conserva il proprio dominio e i controlli di validita'.
        return curve;
    }
}
} // namespace ForgeCad::Kernel::detail
