#pragma once
#include <QDoubleSpinBox>
#include <QLocale>
#include <algorithm>
namespace ForgeCad {
// La rappresentazione segue Qt/il sistema, il file conserva sempre numeri JSON.
// Nei campi tecnici non sono ammessi separatori delle migliaia: 1,234 vale 1.234.
class NumericInput final : public QDoubleSpinBox {
    QString normalized(QString text)const{
        text=text.trimmed();
        if(!prefix().isEmpty()&&text.startsWith(prefix()))text.remove(0,prefix().size());
        if(!suffix().isEmpty()&&text.endsWith(suffix()))text.chop(suffix().size());
        text=text.trimmed();const auto decimal=locale().decimalPoint();
        if(decimal!=QStringLiteral("."))text.replace(QStringLiteral("."),decimal);
        if(decimal!=QStringLiteral(","))text.replace(QStringLiteral(","),decimal);
        return text;
    }
protected:
    QValidator::State validate(QString &text,int &position)const override{
        auto copy=prefix()+normalized(text)+suffix();int cursor=std::min(position,int(copy.size()));
        return QDoubleSpinBox::validate(copy,cursor);
    }
    double valueFromText(const QString &text)const override{
        auto numericLocale=locale();numericLocale.setNumberOptions(numericLocale.numberOptions()|QLocale::RejectGroupSeparator);
        bool ok=false;const auto value=numericLocale.toDouble(normalized(text),&ok);return ok?value:this->value();
    }
public:
    explicit NumericInput(QWidget *parent=nullptr):QDoubleSpinBox(parent){auto l=QLocale();l.setNumberOptions(l.numberOptions()|QLocale::OmitGroupSeparator|QLocale::RejectGroupSeparator);setLocale(l);setGroupSeparatorShown(false);setToolTip(QStringLiteral("Virgola o punto come separatore decimale; migliaia senza separatore."));}
};
}
