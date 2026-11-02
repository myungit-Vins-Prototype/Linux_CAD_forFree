// Regressioni del parser delle espressioni dei campi numerici (cad_expression).
#include "cad_expression.h"

#include <QApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}

void accepts(const char *text, double expected) {
    double value = 0.0;
    require(ForgeCad::evaluateExpression(QString::fromUtf8(text), value), std::string("espressione rifiutata: ") + text);
    require(std::fabs(value - expected) <= 1e-12 * std::max(1.0, std::fabs(expected)),
            std::string("valore errato per ") + text + ": " + std::to_string(value));
}

void rejects(const QString &text, const std::string &label) {
    double value = 0.0;
    require(!ForgeCad::evaluateExpression(text, value), "espressione accettata: " + label + " -> " + std::to_string(value));
}

}  // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    try {
        // Comportamento esistente da conservare.
        accepts("25/2", 12.5);
        accepts("3*(4+1.5)", 16.5);
        accepts("2,5", 2.5);
        accepts("1e3", 1000.0);
        accepts("2.5E-1", 0.25);
        accepts("2pi", 2.0 * M_PI);
        accepts("2 pi", 2.0 * M_PI);
        accepts("3(4+1)", 15.0);
        accepts("(2)(3)", 6.0);
        accepts("2sqrt(9)", 6.0);
        accepts("-(-(3))", 3.0);
        accepts("2^-1", 0.5);
        accepts("360/7", 360.0 / 7.0);
        // Numeri malformati: prima venivano spezzati e moltiplicati.
        for (const char *text : {"1,000.5", "1.000,5", "1.2.3", "12 5", "2 3", "1,2,3", "(2)3", "2(3)4 5"})
            rejects(QString::fromUtf8(text), text);
        // Annidamento: valori ragionevoli accettati, testi patologici rifiutati senza stack overflow.
        accepts(qPrintable(QString(100, QLatin1Char('(')) + QStringLiteral("7") + QString(100, QLatin1Char(')'))), 7.0);
        accepts(qPrintable(QString(100, QLatin1Char('-')) + QStringLiteral("4")), 4.0);
        rejects(QString(200000, QLatin1Char('(')) + QStringLiteral("1") + QString(200000, QLatin1Char(')')), "200000 parentesi");
        rejects(QString(200000, QLatin1Char('-')) + QStringLiteral("1"), "200000 segni");
        rejects(QStringLiteral("2^") + QString(200000, QLatin1Char('-')) + QStringLiteral("1"), "potenza con 200000 segni");
    } catch (const std::exception &error) {
        std::cerr << "FAIL espressioni: " << error.what() << "\n";
        return 1;
    }
    std::cout << "PASS espressioni: numeri malformati rifiutati, annidamento limitato\n";
    return 0;
}
