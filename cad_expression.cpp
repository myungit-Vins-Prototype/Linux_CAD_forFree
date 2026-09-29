#include "cad_expression.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <cmath>

namespace ForgeCad {

namespace {

// Analisi a discesa ricorsiva:
//   somma    = prodotto (('+' | '-') prodotto)*
//   prodotto = unario (('*' | '/') unario)*   (anche "2pi", "3(4+1)": prodotto implicito)
//   unario   = ('+' | '-') unario | potenza
//   potenza  = primario ('^' unario)?
//   primario = numero | nome | nome '(' somma ')' | '(' somma ')'
class Parser {
public:
    explicit Parser(const QString &text) : text_(text) {}

    bool parse(double &value) {
        if (!sum(value)) return false;
        skipSpaces();
        return position_ == text_.size() && std::isfinite(value);
    }

private:
    const QString &text_;
    int position_ = 0;

    void skipSpaces() {
        while (position_ < text_.size() && text_.at(position_).isSpace()) ++position_;
    }
    QChar peek() {
        skipSpaces();
        return position_ < text_.size() ? text_.at(position_) : QChar();
    }
    bool startsPrimary() {
        const QChar c = peek();
        return c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(',') || c.isLetter() || c == QLatin1Char('(');
    }

    bool sum(double &value) {
        if (!product(value)) return false;
        for (;;) {
            const QChar c = peek();
            if (c != QLatin1Char('+') && c != QLatin1Char('-')) return true;
            ++position_;
            double rhs = 0.0;
            if (!product(rhs)) return false;
            value = c == QLatin1Char('+') ? value + rhs : value - rhs;
        }
    }
    bool product(double &value) {
        if (!unary(value)) return false;
        for (;;) {
            const QChar c = peek();
            double rhs = 0.0;
            if (c == QLatin1Char('*') || c == QLatin1Char('/') || c == QChar(0x00D7) || c == QChar(0x00F7) || c == QLatin1Char(':')) {
                ++position_;
                if (!unary(rhs)) return false;
                if (c == QLatin1Char('*') || c == QChar(0x00D7)) value *= rhs;
                else if (rhs == 0.0) return false;
                else value /= rhs;
            } else if (startsPrimary()) {
                if (!power(rhs)) return false;
                value *= rhs;
            } else {
                return true;
            }
        }
    }
    bool unary(double &value) {
        const QChar c = peek();
        if (c == QLatin1Char('+') || c == QLatin1Char('-')) {
            ++position_;
            if (!unary(value)) return false;
            if (c == QLatin1Char('-')) value = -value;
            return true;
        }
        return power(value);
    }
    bool power(double &value) {
        if (!primary(value)) return false;
        if (peek() == QLatin1Char('^')) {
            ++position_;
            double exponent = 0.0;
            if (!unary(exponent)) return false;
            value = std::pow(value, exponent);
        }
        return std::isfinite(value);
    }
    bool number(double &value) {
        skipSpaces();
        QString digits;
        bool point = false;
        while (position_ < text_.size()) {
            const QChar c = text_.at(position_);
            if (c.isDigit()) {
                digits += c;
            } else if ((c == QLatin1Char('.') || c == QLatin1Char(',')) && !point) {
                digits += QLatin1Char('.');
                point = true;
            } else {
                break;
            }
            ++position_;
        }
        // Esponente: 1e-3, 2.5E4.
        if (position_ < text_.size() && (text_.at(position_) == QLatin1Char('e') || text_.at(position_) == QLatin1Char('E'))) {
            int k = position_ + 1;
            QString exponent = QStringLiteral("e");
            if (k < text_.size() && (text_.at(k) == QLatin1Char('+') || text_.at(k) == QLatin1Char('-'))) exponent += text_.at(k++);
            const int first = k;
            while (k < text_.size() && text_.at(k).isDigit()) exponent += text_.at(k++);
            if (k > first) {
                digits += exponent;
                position_ = k;
            }
        }
        if (digits.isEmpty() || digits == QLatin1String(".")) return false;
        bool ok = false;
        value = QLocale::c().toDouble(digits, &ok);
        return ok;
    }
    bool primary(double &value) {
        const QChar c = peek();
        if (c == QLatin1Char('(')) {
            ++position_;
            if (!sum(value)) return false;
            if (peek() != QLatin1Char(')')) return false;
            ++position_;
            return true;
        }
        if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(',')) return number(value);
        if (!c.isLetter()) return false;
        QString name;
        while (position_ < text_.size() && text_.at(position_).isLetterOrNumber()) name += text_.at(position_++).toLower();
        if (name == QLatin1String("pi") || name == QStringLiteral("π")) {
            value = M_PI;
            return true;
        }
        if (peek() != QLatin1Char('(')) return false;
        ++position_;
        double argument = 0.0;
        if (!sum(argument) || peek() != QLatin1Char(')')) return false;
        ++position_;
        const double degree = M_PI / 180.0;
        if (name == QLatin1String("sqrt")) {
            if (argument < 0.0) return false;
            value = std::sqrt(argument);
        } else if (name == QLatin1String("abs")) value = std::fabs(argument);
        else if (name == QLatin1String("sin")) value = std::sin(argument * degree);
        else if (name == QLatin1String("cos")) value = std::cos(argument * degree);
        else if (name == QLatin1String("tan")) value = std::tan(argument * degree);
        else if (name == QLatin1String("asin")) value = std::asin(argument) / degree;
        else if (name == QLatin1String("acos")) value = std::acos(argument) / degree;
        else if (name == QLatin1String("atan")) value = std::atan(argument) / degree;
        else if (name == QLatin1String("exp")) value = std::exp(argument);
        else if (name == QLatin1String("ln")) value = std::log(argument);
        else if (name == QLatin1String("log")) value = std::log10(argument);
        else return false;
        return std::isfinite(value);
    }
};

}

bool evaluateExpression(const QString &text, double &value) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    Parser parser(trimmed);
    return parser.parse(value);
}

ExpressionSpinBox::ExpressionSpinBox(QWidget *parent) : QDoubleSpinBox(parent) {
    setToolTip(QStringLiteral("Si possono scrivere espressioni: 25/2, 3*(4+1.5), sqrt(2)*10, 360/7, sin(30)...\n"
                              "Invio calcola il risultato."));
    setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
}

QString ExpressionSpinBox::expressionText(const QString &text) const {
    QString body = text;
    if (!prefix().isEmpty() && body.startsWith(prefix())) body.remove(0, prefix().size());
    if (!suffix().isEmpty() && body.endsWith(suffix())) body.chop(suffix().size());
    return body.trimmed();
}

QValidator::State ExpressionSpinBox::validate(QString &text, int &pos) const {
    (void)pos;
    const QString body = expressionText(text);
    if (body.isEmpty()) return QValidator::Intermediate;
    for (const QChar c : body)
        if (!(c.isLetterOrNumber() || c.isSpace() || QStringLiteral("+-*/^().,:×÷π").contains(c))) return QValidator::Invalid;
    double value = 0.0;
    if (!evaluateExpression(body, value)) return QValidator::Intermediate;
    // Una semplice cifra si accetta come la accetta QDoubleSpinBox; un'espressione
    // fuori dall'intervallo resta intermedia (all'uscita si porta al limite).
    return value >= minimum() && value <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
}

double ExpressionSpinBox::valueFromText(const QString &text) const {
    double value = 0.0;
    if (!evaluateExpression(expressionText(text), value)) return this->value();
    return qBound(minimum(), value, maximum());
}

void ExpressionSpinBox::fixup(QString &input) const {
    double value = 0.0;
    if (evaluateExpression(expressionText(input), value)) input = prefix() + textFromValue(qBound(minimum(), value, maximum())) + suffix();
}

double getDouble(QWidget *parent, const QString &title, const QString &label, double value, double minimum, double maximum, int decimals,
                 bool *ok) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QLabel(label, &dialog);
    text->setWordWrap(true);
    layout->addWidget(text);
    auto *box = new ExpressionSpinBox(&dialog);
    box->setDecimals(decimals);
    box->setRange(minimum, maximum);
    box->setValue(value);
    box->selectAll();
    layout->addWidget(box);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    box->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    if (!accepted) return value;
    box->interpretText();
    return box->value();
}

}
