#ifndef FORGECAD_FK_QUADRATURE_H
#define FORGECAD_FK_QUADRATURE_H

#include <array>
#include <cmath>
#include <cstddef>

// Quadratura adattiva di Gauss-Kronrod 7-15 (nodi e pesi di QUADPACK), per
// integrali lungo le curve (lunghezze, aree, numeri di avvolgimento) e,
// annidata, sulle facce (proprieta' di massa).
namespace ForgeCad::Kernel::detail {

inline constexpr double kKronrodNodes[8] = {
    0.991455371120812639206854697526329, 0.949107912342758524526189684047851,
    0.864864423359769072789712788640926, 0.741531185599394439863864773280788,
    0.586087235467691130294144845693013, 0.405845151377397166906606412076961,
    0.207784955007898467600689403773245, 0.0};
inline constexpr double kKronrodWeights[8] = {
    0.022935322010529224963732008058970, 0.063092092629978553290700663189204,
    0.104790010322250183839876322541518, 0.140653259715525918745189590510238,
    0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
    0.204432940075298892414161999234649, 0.209482141084727828012999174891714};
inline constexpr double kGaussWeights[4] = {
    0.129484966168869693270611432679082, 0.279705391489276667901467771423780,
    0.381830050505118944950369775488975, 0.417959183673469387755102040816327};

// Regola 7-15 su [a, b]: restituisce l'integrale, `error` = |K15 - G7| e
// `absolute` = integrale di |f| (serve a stimare l'errore di arrotondamento).
template <class F>
double gaussKronrod(const F &f, double a, double b, double &error, double &absolute) {
    const double center = 0.5 * (a + b);
    const double half = 0.5 * (b - a);
    const double fc = f(center);
    double kronrod = fc * kKronrodWeights[7];
    double gauss = fc * kGaussWeights[3];
    absolute = std::fabs(fc) * kKronrodWeights[7];
    for (int j = 0; j < 7; ++j) {
        const double dx = half * kKronrodNodes[j];
        const double left = f(center - dx), right = f(center + dx);
        const double sum = left + right;
        kronrod += kKronrodWeights[j] * sum;
        absolute += kKronrodWeights[j] * (std::fabs(left) + std::fabs(right));
        if (j % 2 == 1) gauss += kGaussWeights[j / 2] * sum;
    }
    error = std::fabs((kronrod - gauss) * half);
    absolute *= std::fabs(half);
    return kronrod * half;
}

// Oltre alla tolleranza richiesta, un intervallo e' accettato quando l'errore
// stimato e' sotto l'arrotondamento dei double (50 eps volte l'integrale di
// |f|, come in QUADPACK): altrimenti con integrandi grandi la bisezione
// scenderebbe fino in fondo ovunque.
template <class F>
double adaptiveIntegral(const F &f, double a, double b, double tolerance, int depth) {
    double error, absolute;
    const double value = gaussKronrod(f, a, b, error, absolute);
    if (error <= tolerance || error <= 50.0 * 2.220446049250313e-16 * absolute || depth >= 30) return value;
    const double middle = 0.5 * (a + b);
    return adaptiveIntegral(f, a, middle, 0.5 * tolerance, depth + 1)
         + adaptiveIntegral(f, middle, b, 0.5 * tolerance, depth + 1);
}

// Integrale di f su [a, b] con errore assoluto stimato <= tolerance.
template <class F>
double integrate(const F &f, double a, double b, double tolerance) {
    return adaptiveIntegral(f, a, b, tolerance, 0);
}

// --- Versione vettoriale: N integrali calcolati sugli stessi nodi ---

template <std::size_t N>
using Values = std::array<double, N>;

// Valori dell'integrando e loro "grandezza": una stima di quanto sono grandi
// i termini da cui nascono, che fissa l'errore di arrotondamento (per esempio
// |x| |n| per x.n, che puo' essere nullo per cancellazione).
template <std::size_t N>
struct Evaluation {
    Values<N> value{};
    Values<N> magnitude{};
};

template <std::size_t N, class F>
void gaussKronrod(const F &f, double a, double b, Evaluation<N> &integral, Values<N> &error, Values<N> &absolute) {
    const double center = 0.5 * (a + b);
    const double half = 0.5 * (b - a);
    Values<N> kronrod, gauss;
    const Evaluation<N> fc = f(center);
    for (std::size_t k = 0; k < N; ++k) {
        kronrod[k] = fc.value[k] * kKronrodWeights[7];
        gauss[k] = fc.value[k] * kGaussWeights[3];
        absolute[k] = std::fabs(fc.value[k]) * kKronrodWeights[7];
        integral.magnitude[k] = fc.magnitude[k] * kKronrodWeights[7];
    }
    for (int j = 0; j < 7; ++j) {
        const double dx = half * kKronrodNodes[j];
        const Evaluation<N> left = f(center - dx), right = f(center + dx);
        for (std::size_t k = 0; k < N; ++k) {
            const double sum = left.value[k] + right.value[k];
            kronrod[k] += kKronrodWeights[j] * sum;
            absolute[k] += kKronrodWeights[j] * (std::fabs(left.value[k]) + std::fabs(right.value[k]));
            integral.magnitude[k] += kKronrodWeights[j] * (left.magnitude[k] + right.magnitude[k]);
            if (j % 2 == 1) gauss[k] += kGaussWeights[j / 2] * sum;
        }
    }
    for (std::size_t k = 0; k < N; ++k) {
        integral.value[k] = kronrod[k] * half;
        integral.magnitude[k] *= std::fabs(half);
        error[k] = std::fabs((kronrod[k] - gauss[k]) * half);
        absolute[k] *= std::fabs(half);
    }
}

// Accettato se per ogni componente l'errore stimato e' sotto la tolleranza
// o sotto l'arrotondamento (`roundoff` volte l'integrale della grandezza).
template <std::size_t N, class F>
Evaluation<N> adaptiveIntegral(const F &f, double a, double b, const Values<N> &tolerance, double roundoff, int depth) {
    Evaluation<N> integral;
    Values<N> error, absolute;
    gaussKronrod<N>(f, a, b, integral, error, absolute);
    bool accepted = true;
    for (std::size_t k = 0; k < N && accepted && depth < 30; ++k)
        accepted = error[k] <= tolerance[k] || error[k] <= roundoff * integral.magnitude[k];
    if (accepted) return integral;
    Values<N> halfTolerance;
    for (std::size_t k = 0; k < N; ++k) halfTolerance[k] = 0.5 * tolerance[k];
    const double middle = 0.5 * (a + b);
    Evaluation<N> result = adaptiveIntegral<N>(f, a, middle, halfTolerance, roundoff, depth + 1);
    const Evaluation<N> right = adaptiveIntegral<N>(f, middle, b, halfTolerance, roundoff, depth + 1);
    for (std::size_t k = 0; k < N; ++k) {
        result.value[k] += right.value[k];
        result.magnitude[k] += right.magnitude[k];
    }
    return result;
}

// N integrali su [a, b]: errore relativo `relativeTolerance` rispetto
// all'integrale di |f_k| (stimato con una prima regola 7-15), che e' la
// misura giusta anche quando i contributi si cancellano, oppure errore sotto
// `roundoff` volte l'integrale della grandezza. Restituisce anche
// l'integrale delle grandezze.
template <std::size_t N, class F>
Evaluation<N> integrateVector(const F &f, double a, double b, double relativeTolerance, double roundoff) {
    Evaluation<N> first;
    Values<N> error, absolute, tolerance;
    gaussKronrod<N>(f, a, b, first, error, absolute);
    for (std::size_t k = 0; k < N; ++k) tolerance[k] = relativeTolerance * absolute[k];
    return adaptiveIntegral<N>(f, a, b, tolerance, roundoff, 0);
}

}

#endif
