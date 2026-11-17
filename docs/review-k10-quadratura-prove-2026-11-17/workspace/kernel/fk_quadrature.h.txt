#ifndef FORGECAD_FK_QUADRATURE_H
#define FORGECAD_FK_QUADRATURE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

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

// Il percorso ordinario conserva le formule; solo gli overflow usano
// mezze coordinate, senza sommare o sottrarre prima gli estremi grandi.
inline double quadratureMiddle(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b)) throw std::domain_error("quadratura: estremi non finiti");
    const double sum = a + b;
    return std::isfinite(sum) ? 0.5 * sum : 0.5 * a + 0.5 * b;
}
inline double quadratureHalf(double a, double b) {
    const double difference = b - a;
    const double half = std::isfinite(difference) ? 0.5 * difference : 0.5 * b - 0.5 * a;
    if (half == 0.0 && a != b) throw std::domain_error("quadratura: semiampiezza non rappresentabile");
    return half;
}
inline double quadratureProduct(double scale, double half, double sum) {
    if (!std::isfinite(scale) || !std::isfinite(half) || !std::isfinite(sum))
        throw std::domain_error("quadratura: fattore non finito");
    int es, eh, ev;
    const double ms = std::frexp(scale, &es), mh = std::frexp(half, &eh), mv = std::frexp(sum, &ev);
    const double value = std::scalbn((ms * mh) * mv, es + eh + ev);
    if (!std::isfinite(value)) throw std::domain_error("quadratura: prodotto non rappresentabile");
    return value;
}
// Il ripiego usa soltanto i 15 campioni memorizzati: nessuna callback
// ripetuta e nessuna perdita completa accettata nel riscalamento.
struct QuadratureSums { double kronrod, gauss, absolute; };
inline QuadratureSums quadratureScaledSums(const std::array<double, 15> &samples, double &scale) {
    scale = 0.0;
    for (double value : samples) scale = std::max(scale, std::fabs(value));
    if (scale == 0.0) return {0.0, 0.0, 0.0};
    std::array<double, 15> scaled;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        scaled[i] = samples[i] / scale;
        if (samples[i] != 0.0 && scaled[i] == 0.0)
            throw std::domain_error("quadratura: campione perso nel riscalamento");
    }
    QuadratureSums result{scaled[0] * kKronrodWeights[7], scaled[0] * kGaussWeights[3],
                          std::fabs(scaled[0]) * kKronrodWeights[7]};
    for (int j = 0; j < 7; ++j) {
        const double left = scaled[1 + 2 * j], right = scaled[2 + 2 * j], sum = left + right;
        result.kronrod += kKronrodWeights[j] * sum;
        result.absolute += kKronrodWeights[j] * (std::fabs(left) + std::fabs(right));
        if (j % 2 == 1) result.gauss += kGaussWeights[j / 2] * sum;
    }
    return result;
}

// Regola 7-15 su [a, b]: restituisce l'integrale, `error` = |K15 - G7| e
// `absolute` = integrale di |f| (serve a stimare l'errore di arrotondamento).
template <class F>
double gaussKronrod(const F &f, double a, double b, double &error, double &absolute) {
    const double center = quadratureMiddle(a, b);
    const double half = quadratureHalf(a, b);
    std::array<double, 15> samples;
    const auto checked = [&f](double t) {
        if (!std::isfinite(t)) throw std::domain_error("quadratura: parametro non finito");
        const double value = f(t);
        if (!std::isfinite(value)) throw std::domain_error("quadratura: campione non finito");
        return value;
    };
    const double fc = samples[0] = checked(center);
    double kronrod = fc * kKronrodWeights[7];
    double gauss = fc * kGaussWeights[3];
    absolute = std::fabs(fc) * kKronrodWeights[7];
    for (int j = 0; j < 7; ++j) {
        const double dx = half * kKronrodNodes[j];
        const double left = samples[1 + 2 * j] = checked(center - dx), right = samples[2 + 2 * j] = checked(center + dx);
        const double sum = left + right;
        kronrod += kKronrodWeights[j] * sum;
        absolute += kKronrodWeights[j] * (std::fabs(left) + std::fabs(right));
        if (j % 2 == 1) gauss += kGaussWeights[j / 2] * sum;
    }
    const bool recover = !std::isfinite(kronrod) || !std::isfinite(gauss) || !std::isfinite(absolute);
    error = std::fabs((kronrod - gauss) * half);
    absolute *= std::fabs(half);
    double value = kronrod * half;
    if (recover || !std::isfinite(value) || !std::isfinite(error) || !std::isfinite(absolute)) {
        double scale;
        const auto sums = quadratureScaledSums(samples, scale);
        value = quadratureProduct(scale, half, sums.kronrod);
        error = std::fabs(quadratureProduct(scale, half, sums.kronrod - sums.gauss));
        absolute = quadratureProduct(scale, std::fabs(half), sums.absolute);
    }
    return value;
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
    const double middle = quadratureMiddle(a, b);
    if (middle == a || middle == b) throw std::domain_error("quadratura: suddivisione non rappresentabile");
    const double result = adaptiveIntegral(f, a, middle, 0.5 * tolerance, depth + 1)
                        + adaptiveIntegral(f, middle, b, 0.5 * tolerance, depth + 1);
    if (!std::isfinite(result)) throw std::domain_error("quadratura: somma non rappresentabile");
    return result;
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
    const double center = quadratureMiddle(a, b);
    const double half = quadratureHalf(a, b);
    Values<N> kronrod, gauss;
    std::array<Evaluation<N>, 15> samples;
    const auto checked = [&f](double t) {
        if (!std::isfinite(t)) throw std::domain_error("quadratura: parametro non finito");
        const auto value = f(t);
        for (std::size_t k = 0; k < N; ++k)
            if (!std::isfinite(value.value[k]) || !std::isfinite(value.magnitude[k]) || value.magnitude[k] < 0.0)
                throw std::domain_error("quadratura: campione vettoriale non valido");
        return value;
    };
    const Evaluation<N> &fc = samples[0] = checked(center);
    for (std::size_t k = 0; k < N; ++k) {
        kronrod[k] = fc.value[k] * kKronrodWeights[7];
        gauss[k] = fc.value[k] * kGaussWeights[3];
        absolute[k] = std::fabs(fc.value[k]) * kKronrodWeights[7];
        integral.magnitude[k] = fc.magnitude[k] * kKronrodWeights[7];
    }
    for (int j = 0; j < 7; ++j) {
        const double dx = half * kKronrodNodes[j];
        const Evaluation<N> &left = samples[1 + 2 * j] = checked(center - dx),
                            &right = samples[2 + 2 * j] = checked(center + dx);
        for (std::size_t k = 0; k < N; ++k) {
            const double sum = left.value[k] + right.value[k];
            kronrod[k] += kKronrodWeights[j] * sum;
            absolute[k] += kKronrodWeights[j] * (std::fabs(left.value[k]) + std::fabs(right.value[k]));
            integral.magnitude[k] += kKronrodWeights[j] * (left.magnitude[k] + right.magnitude[k]);
            if (j % 2 == 1) gauss[k] += kGaussWeights[j / 2] * sum;
        }
    }
    for (std::size_t k = 0; k < N; ++k) {
        const bool recover = !std::isfinite(kronrod[k]) || !std::isfinite(gauss[k]) || !std::isfinite(absolute[k]);
        const bool recoverMagnitude = !std::isfinite(integral.magnitude[k]);
        integral.value[k] = kronrod[k] * half;
        integral.magnitude[k] *= std::fabs(half);
        error[k] = std::fabs((kronrod[k] - gauss[k]) * half);
        absolute[k] *= std::fabs(half);
        if (recover || !std::isfinite(integral.value[k]) || !std::isfinite(error[k]) || !std::isfinite(absolute[k])) {
            std::array<double, 15> values;
            for (std::size_t i = 0; i < samples.size(); ++i) values[i] = samples[i].value[k];
            double scale;
            const auto sums = quadratureScaledSums(values, scale);
            integral.value[k] = quadratureProduct(scale, half, sums.kronrod);
            error[k] = std::fabs(quadratureProduct(scale, half, sums.kronrod - sums.gauss));
            absolute[k] = quadratureProduct(scale, std::fabs(half), sums.absolute);
        }
        if (recoverMagnitude || !std::isfinite(integral.magnitude[k])) {
            std::array<double, 15> values;
            for (std::size_t i = 0; i < samples.size(); ++i) values[i] = samples[i].magnitude[k];
            double scale;
            const auto sums = quadratureScaledSums(values, scale);
            integral.magnitude[k] = quadratureProduct(scale, std::fabs(half), sums.kronrod);
        }
    }
}

// Regola 7-15 su un intervallo, con il suo errore.
template <std::size_t N>
struct Panel {
    double a = 0.0, b = 0.0;
    Evaluation<N> integral;
    Values<N> error{};
};

template <std::size_t N, class F>
Panel<N> panel(const F &f, double a, double b) {
    Panel<N> p;
    p.a = a;
    p.b = b;
    Values<N> absolute;
    gaussKronrod<N>(f, a, b, p.integral, p.error, absolute);
    return p;
}

// Accettato se per ogni componente l'errore stimato e' sotto la tolleranza
// o sotto l'arrotondamento (`roundoff` volte l'integrale della grandezza).
// Solo le prime `controlled` componenti guidano il raffinamento; tutte
// restano valutate e devono essere finite. Per default sono controllate tutte.
// Se la bisezione non riduce l'errore (i due figli insieme ne hanno almeno 3/4,
// mentre su un integrando liscio cala di ordini di grandezza) e l'errore e'
// gia' vicino all'arrotondamento, l'intervallo e' dominato dal rumore
// dell'integrando: si prendono i figli. Il rumore puo' superare la stima
// della grandezza, per esempio sulle B-spline con nodi molto fitti (le
// derivate hanno l'arrotondamento dei poli diviso per il passo dei nodi), e
// senza questo controllo la bisezione percorrerebbe l'albero intero fino alla
// profondita' massima (2^30 intervalli).
template <std::size_t N, class F>
Evaluation<N> adaptivePanel(const F &f, const Panel<N> &p, const Values<N> &tolerance, double roundoff, int depth,
                            std::size_t controlled = N) {
    if (controlled > N) throw std::domain_error("quadratura: numero di componenti controllate non valido");
    if (!std::isfinite(p.a) || !std::isfinite(p.b)) throw std::domain_error("quadratura: estremi non finiti");
    for (std::size_t k = 0; k < N; ++k)
        if (!std::isfinite(p.integral.value[k]) || !std::isfinite(p.integral.magnitude[k])
            || p.integral.magnitude[k] < 0.0 || !std::isfinite(p.error[k]) || p.error[k] < 0.0)
            throw std::domain_error("quadratura: pannello non finito o non valido");
    bool accepted = true, nearRoundoff = true;
    for (std::size_t k = 0; k < controlled && depth < 30; ++k) {
        const double floor = roundoff * p.integral.magnitude[k];
        if (p.error[k] <= tolerance[k] || p.error[k] <= floor) continue;
        accepted = false;
        nearRoundoff = nearRoundoff && p.error[k] <= 1e4 * floor;
    }
    if (accepted) return p.integral;
    const double middle = quadratureMiddle(p.a, p.b);
    if (middle == p.a || middle == p.b) throw std::domain_error("quadratura: suddivisione non rappresentabile");
    const Panel<N> left = panel<N>(f, p.a, middle), right = panel<N>(f, middle, p.b);
    bool noise = nearRoundoff;
    for (std::size_t k = 0; k < controlled && noise; ++k) {
        const double floor = roundoff * p.integral.magnitude[k];
        if (p.error[k] <= tolerance[k] || p.error[k] <= floor) continue;
        const double combinedError = left.error[k] + right.error[k];
        if (!std::isfinite(combinedError)) throw std::domain_error("quadratura: errore non rappresentabile");
        noise = combinedError >= 0.75 * p.error[k];
    }
    Evaluation<N> result;
    if (noise) {
        result = left.integral;
        for (std::size_t k = 0; k < N; ++k) {
            result.value[k] += right.integral.value[k];
            result.magnitude[k] += right.integral.magnitude[k];
            if (!std::isfinite(result.value[k]) || !std::isfinite(result.magnitude[k]))
                throw std::domain_error("quadratura: somma vettoriale non rappresentabile");
        }
        return result;
    }
    Values<N> halfTolerance;
    for (std::size_t k = 0; k < N; ++k) halfTolerance[k] = 0.5 * tolerance[k];
    result = adaptivePanel<N>(f, left, halfTolerance, roundoff, depth + 1, controlled);
    const Evaluation<N> second = adaptivePanel<N>(f, right, halfTolerance, roundoff, depth + 1, controlled);
    for (std::size_t k = 0; k < N; ++k) {
        result.value[k] += second.value[k];
        result.magnitude[k] += second.magnitude[k];
        if (!std::isfinite(result.value[k]) || !std::isfinite(result.magnitude[k]))
            throw std::domain_error("quadratura: somma vettoriale non rappresentabile");
    }
    return result;
}

template <std::size_t N, class F>
Evaluation<N> adaptiveIntegral(const F &f, double a, double b, const Values<N> &tolerance, double roundoff, int depth) {
    return adaptivePanel<N>(f, panel<N>(f, a, b), tolerance, roundoff, depth);
}

// N integrali su [a, b]: errore relativo `relativeTolerance` rispetto
// all'integrale di |f_k| (stimato con una prima regola 7-15), che e' la
// misura giusta anche quando i contributi si cancellano, oppure errore sotto
// `roundoff` volte l'integrale della grandezza. Restituisce anche
// l'integrale delle grandezze. `controlled` limita soltanto le componenti
// che guidano l'adattamento, non i controlli di finitezza.
template <std::size_t N, class F>
Evaluation<N> integrateVector(const F &f, double a, double b, double relativeTolerance, double roundoff,
                              std::size_t controlled = N) {
    if (controlled > N) throw std::domain_error("quadratura: numero di componenti controllate non valido");
    Panel<N> first;
    first.a = a;
    first.b = b;
    Values<N> absolute, tolerance;
    gaussKronrod<N>(f, a, b, first.integral, first.error, absolute);
    for (std::size_t k = 0; k < N; ++k) tolerance[k] = relativeTolerance * absolute[k];
    return adaptivePanel<N>(f, first, tolerance, roundoff, 0, controlled);
}

}

#endif
