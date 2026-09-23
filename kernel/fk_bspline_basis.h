#ifndef FORGECAD_FK_BSPLINE_BASIS_H
#define FORGECAD_FK_BSPLINE_BASIS_H

#include <algorithm>
#include <vector>

#include "fk_math.h"

// Funzioni di base B-spline condivise da curve e superfici (uso interno).
// Vettori dei nodi sempre "espansi" (ogni nodo ripetuto per la molteplicita').
namespace ForgeCad::Kernel::detail {

// Controlla un vettore dei nodi per `poleCount` poli di grado `degree`
// (dimensione, crescenza, molteplicita', dominio non vuoto). Lancia
// std::invalid_argument con il prefisso `what`.
void validateKnotVector(const std::vector<double> &knots, int degree, int poleCount, const char *what);

// Indice i del tratto con knots[i] <= t < knots[i+1] (A2.1), limitato al dominio.
int findSpan(const std::vector<double> &knots, int degree, int poleCount, double t);
// Come findSpan ma con knots[i] < t <= knots[i+1] (tratto a sinistra di un nodo).
int findSpanLeft(const std::vector<double> &knots, int degree, int poleCount, double t);

// Buffer di lavoro azzerato: sullo stack fino a Inline elementi, oltre sullo
// heap. Le valutazioni sono nel percorso caldo di proiezioni e intersezioni,
// e allocare a ogni chiamata ne dominava il costo.
template <class T, std::size_t Inline = 64>
class ScratchBuffer {
public:
    explicit ScratchBuffer(std::size_t size) {
        if (size > Inline) heap_.resize(size);
        data_ = size > Inline ? heap_.data() : inline_;
        std::fill(data_, data_ + size, T());
    }
    ScratchBuffer(const ScratchBuffer &) = delete;
    ScratchBuffer &operator=(const ScratchBuffer &) = delete;

    T &operator[](std::size_t i) { return data_[i]; }
    const T &operator[](std::size_t i) const { return data_[i]; }
    T *data() { return data_; }

private:
    T inline_[Inline];
    std::vector<T> heap_;
    T *data_;
};

// Funzioni di base non nulle e derivate fino all'ordine n (A2.3):
// ders[k * (p + 1) + j] = derivata k-esima di N_{span-p+j, p}(u); ders deve
// avere (n + 1) * (p + 1) elementi.
void basisFunctionDerivatives(const std::vector<double> &knots, int span, double u, int p, int n, double *ders);

// Estremi di range piu' i nodi distinti al suo interno.
std::vector<double> knotBreakpoints(const std::vector<double> &knots, const Interval &range);

double binomial(int n, int k);

}

#endif
