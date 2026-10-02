#ifndef FORGECAD_EXPRESSION_H
#define FORGECAD_EXPRESSION_H

#include <QDoubleSpinBox>
#include <QString>
#include <functional>

class QWidget;

// Espressioni nei campi numerici: ogni casella dei valori fa da calcolatrice
// ("25/2", "3*(4+1.5)", "sqrt(2)*10", "40-2*3.2", "360/7"). Operatori + - * /
// ^ (potenza), parentesi, pi, funzioni sqrt, abs, sin, cos, tan, asin, acos,
// atan (in gradi), exp, ln, log (base 10). Il separatore decimale e' il punto
// o la virgola. Tutto in double.
namespace ForgeCad {

// Valore dell'espressione; falso se non e' completa o non e' valida.
bool evaluateExpression(const QString &text, double &value);

// QDoubleSpinBox che accetta le espressioni: finche' il testo non e' completo
// resta "intermedio", con Invio (o all'uscita dal campo) si calcola e si
// mostra il risultato.
class ExpressionSpinBox : public QDoubleSpinBox {
public:
    explicit ExpressionSpinBox(QWidget *parent = nullptr);
    QValidator::State validate(QString &text, int &pos) const override;
    double valueFromText(const QString &text) const override;
    void fixup(QString &input) const override;
    // Se impostato, Invio conferma il valore senza accettare il dialogo.
    // Lo chiamano anche i passi delle frecce e della rotella (un valore
    // completo scelto dall'utente), non i caratteri scritti.
    std::function<void()> onReturn;
    void stepBy(int steps) override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QString expressionText(const QString &text) const;
};

// Come QInputDialog::getDouble, con la casella a espressioni.
double getDouble(QWidget *parent, const QString &title, const QString &label, double value, double minimum, double maximum,
                 int decimals, bool *ok);

}

#endif
