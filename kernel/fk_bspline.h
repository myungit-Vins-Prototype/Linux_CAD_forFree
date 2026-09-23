#ifndef FORGECAD_FK_BSPLINE_H
#define FORGECAD_FK_BSPLINE_H

#include <memory>

#include "fk_curve.h"

// Curve B-spline e NURBS non periodiche. Riferimento per gli algoritmi:
// Piegl & Tiller, "The NURBS Book" (2a ed.), citati come A2.1, A2.3, ...
namespace ForgeCad::Kernel {

// Vettore dei nodi "espanso": ogni nodo ripetuto quante volte vale la sua
// molteplicita' (formato del NURBS Book e di IGES/STEP). OCCT invece separa
// nodi distinti e molteplicita': vedi expandKnots().
template <int N>
class BSplineCurve final : public Curve<N> {
public:
    // knots.size() == poles.size() + degree + 1; weights vuoto = polinomiale.
    // Dominio [knots[degree], knots[poles.size()]]; molteplicita' interne <= degree.
    BSplineCurve(int degree, std::vector<double> knots, std::vector<Vec<N>> poles, std::vector<double> weights = {});

    CurveType type() const override { return CurveType::BSpline; }
    Interval domain() const override;
    // Fuori dal dominio prolunga il polinomio del primo/ultimo tratto.
    void evaluate(double t, int order, Vec<N> *out) const override;
    void evaluateLeft(double t, int order, Vec<N> *out) const override;
    std::vector<double> breakpoints(const Interval &range) const override;

    int degree() const { return degree_; }
    int poleCount() const { return int(poles_.size()); }
    const std::vector<double> &knots() const { return knots_; }
    const std::vector<Vec<N>> &poles() const { return poles_; }
    const std::vector<double> &weights() const { return weights_; }
    bool isRational() const { return !weights_.empty(); }
    double weight(int index) const { return weights_.empty() ? 1.0 : weights_[index]; }
    // Nodi di estremita' con molteplicita' degree + 1 (la curva interpola i poli estremi).
    bool isClamped() const;

    // Indice i del tratto con knots[i] <= t < knots[i+1] (A2.1), limitato al dominio.
    int findSpan(double t) const;
    // Come findSpan ma con knots[i] < t <= knots[i+1]: il tratto a sinistra di un nodo.
    int findSpanLeft(double t) const;
    // Numero di nodi uguali (esattamente) a u.
    int multiplicity(double u) const;

    // Inserimento del nodo u (nel dominio, estremi compresi) `times` volte,
    // senza superare la molteplicita' degree (A5.1). La curva non cambia.
    BSplineCurve insertKnot(double u, int times = 1) const;
    // Stessa curva sullo stesso dominio con nodi di estremita' di
    // molteplicita' degree + 1 (si scartano i poli che agiscono solo fuori).
    BSplineCurve clamped() const;
    // Decomposizione nei tratti di Bezier, uno per intervallo tra nodi distinti.
    std::vector<BSplineCurve> bezierSegments() const;
    // La stessa, calcolata una volta per curva e conservata (le curve sono
    // immutabili; la cache si legge e si scrive in modo atomico).
    std::shared_ptr<const std::vector<BSplineCurve>> cachedBezierSegments() const;

private:
    void validate() const;
    void evaluateSpan(int span, double t, int order, Vec<N> *out) const;

    int degree_;
    std::vector<double> knots_;
    std::vector<Vec<N>> poles_;
    std::vector<double> weights_;
    mutable std::shared_ptr<const std::vector<BSplineCurve>> bezierCache_;
};

template <int N>
using BSplineCurvePtr = std::shared_ptr<const BSplineCurve<N>>;

// Da nodi distinti + molteplicita' (formato OCCT) al vettore espanso.
std::vector<double> expandKnots(const std::vector<double> &distinctKnots, const std::vector<int> &multiplicities);

// Arco di conica P(t) = center + cos t A + sin t B, t in [first, last], come
// NURBS razionale quadratica esatta (A7.1): un tratto ogni al piu' 90 gradi.
// La curva coincide esattamente con la conica ma il parametro NURBS non e'
// l'angolo; solo i nodi (estremi dei tratti) cadono sugli stessi angoli.
template <int N>
BSplineCurve<N> conicArcToBSpline(const Vec<N> &center, const Vec<N> &a, const Vec<N> &b, double first, double last);
template <int N>
BSplineCurve<N> toBSpline(const Circle<N> &circle, double first, double last);
template <int N>
BSplineCurve<N> toBSpline(const Ellipse<N> &ellipse, double first, double last);

extern template class BSplineCurve<2>;
extern template class BSplineCurve<3>;

}

#endif
