#ifndef FORGECAD_FK_BERNSTEIN_H
#define FORGECAD_FK_BERNSTEIN_H

#include <vector>

// Polinomi scalari in forma di Bernstein su [0, 1]: la base naturale dei
// tratti di Bezier. Il segno dei coefficienti limita il numero di radici
// (regola dei segni di Cartesio per la base di Bernstein), il che permette di
// trovare *tutte* le radici in un intervallo senza campionare a caso.
namespace ForgeCad::Kernel {

class BernsteinPolynomial {
public:
    BernsteinPolynomial() = default;
    explicit BernsteinPolynomial(std::vector<double> coefficients);

    int degree() const { return int(coefficients_.size()) - 1; }
    const std::vector<double> &coefficients() const { return coefficients_; }

    // de Casteljau.
    double evaluate(double s) const;
    BernsteinPolynomial derivative() const;
    BernsteinPolynomial elevated(int degree) const;
    // Coefficienti di [0, s] e [s, 1], ciascuno riparametrizzato su [0, 1].
    void split(double s, BernsteinPolynomial &left, BernsteinPolynomial &right) const;

    BernsteinPolynomial operator+(const BernsteinPolynomial &other) const;
    BernsteinPolynomial operator-(const BernsteinPolynomial &other) const;
    BernsteinPolynomial operator*(const BernsteinPolynomial &other) const;
    BernsteinPolynomial operator*(double factor) const;

    // Radici in [0, 1] in ordine crescente: suddivisione finche' ogni
    // intervallo ha 0 o 1 cambi di segno nei coefficienti, poi bisezione.
    // Una radice multipla (o un grappolo piu' stretto di tolerance) compare
    // una volta sola. Un polinomio identicamente nullo non ha radici isolate:
    // restituisce vuoto.
    std::vector<double> roots(double tolerance = 1.0e-15) const;

private:
    std::vector<double> coefficients_;
};

}

#endif
