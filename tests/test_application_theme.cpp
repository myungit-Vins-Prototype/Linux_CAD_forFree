#include "forgeCad2026_application_theme.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QLineEdit>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>

namespace {
int nativeChanges = 0, simulatedChanges = 0;
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void settle() {
    for (int i = 0; i < 8; ++i) QApplication::processEvents();
}

class PaletteEvents final : public QObject {
public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::ApplicationPaletteChange) ++count;
        return false;
    }
};

void chooseScheme(QApplication &app, bool dark) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    if (app.styleHints()->colorScheme() != (dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light)) {
        ++simulatedChanges;
        // Offscreen non espone il tema dell'OS. Esercita la connessione reale
        // al segnale Qt, senza attribuire la prova a un cambio nativo.
        require(QMetaObject::invokeMethod(app.styleHints(), "colorSchemeChanged", Qt::DirectConnection,
                    Q_ARG(Qt::ColorScheme, dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light)),
                "segnale Qt del cambio tema disponibile");
    } else ++nativeChanges;
#elif QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    ++simulatedChanges;
    require(QMetaObject::invokeMethod(app.styleHints(), "colorSchemeChanged", Qt::DirectConnection,
                Q_ARG(Qt::ColorScheme, dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light)),
            "segnale Qt del cambio tema disponibile");
#else
    // Qt più vecchio: esercita il fallback tramite la palette di piattaforma.
    QPalette platform(QColor(dark ? "#151515" : "#fafafa"));
    app.setPalette(platform);
#endif
    settle();
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        PaletteEvents events;
        app.installEventFilter(&events);
        ForgeCad::initializeApplicationTheme(app);
        ForgeCad::initializeApplicationTheme(app);
        require(app.findChildren<QObject *>(QStringLiteral("forgecadApplicationTheme"), Qt::FindDirectChildrenOnly).size() == 1,
                "una sola gestione del tema per applicazione");
        require(app.style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) == 0,
                "stile Fusion senza argomenti da terminale");

        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *tabs = new QTabBar(&window);
        tabs->addTab(QStringLiteral("Modello")); tabs->addTab(QStringLiteral("Superfici"));
        auto *button = new QToolButton(&window);
        button->setText(QStringLiteral("Estrudi")); button->setAutoRaise(true);
        auto *field = new QLineEdit(QStringLiteral("Dimensione"), &window);
        auto *disabled = new QLineEdit(QStringLiteral("Disabilitato"), &window);
        disabled->setEnabled(false);
        auto *combo = new QComboBox(&window); combo->addItem(QStringLiteral("Millimetri"));
        layout->addWidget(tabs); layout->addWidget(button); layout->addWidget(field);
        layout->addWidget(disabled); layout->addWidget(combo);
        window.resize(360, 280); window.show();
        QWidget secondWindow;
        secondWindow.show();
        chooseScheme(app, false);
        const QPalette light = app.palette();
        const QSize tabSize = tabs->sizeHint(), buttonSize = button->sizeHint();
        require(light.color(QPalette::Window).lightness() > 128, "palette iniziale chiara");

        for (bool dark : {true, false, true, false}) {
            chooseScheme(app, dark);
            const auto palette = app.palette();
            require((palette.color(QPalette::Window).lightness() < 128) == dark, "cambio della palette a ogni transizione");
            require((field->palette().color(QPalette::Base).lightness() < 128) == dark, "campi aggiornati nella finestra aperta");
            require(secondWindow.palette().color(QPalette::Window) == palette.color(QPalette::Window), "anche la seconda finestra segue il tema");
            require(disabled->palette().color(QPalette::Disabled, QPalette::Text) != palette.color(QPalette::Active, QPalette::Text),
                    "testo disabilitato distinto e coerente");
            require(tabs->sizeHint() == tabSize && button->sizeHint() == buttonSize, "metriche dei controlli conservate al cambio tema");
            const auto expected = palette;
            auto foreign = palette; foreign.setColor(QPalette::Highlight, Qt::red);
            app.setPalette(foreign); settle();
            require(app.palette() == expected, "gli accenti esterni non sostituiscono la palette comune");
            const int before = events.count; settle();
            require(events.count == before, "nessun ciclo di eventi generato dalla palette applicativa");
        }
        require(app.palette() == light, "ritorno alla medesima palette chiara");
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        require(QMetaObject::invokeMethod(app.styleHints(), "colorSchemeChanged", Qt::DirectConnection,
                    Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Unknown)), "schema assente simulato");
#endif
        for (bool dark : {true, false}) {
            app.setPalette(QPalette(QColor(dark ? "#151515" : "#fafafa"))); settle();
            require((app.palette().color(QPalette::Window).lightness() < 128) == dark,
                    "fallback su palette quando il backend non espone lo schema");
        }
        std::cout << "PASS Fusion, quattro transizioni chiaro/scuro, finestre aperte, campi, disabilitati, metriche e stabilità eventi (Qt "
                  << qVersion() << "); richieste native=" << nativeChanges << ", segnali simulati=" << simulatedChanges
                  << "; fallback palette PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
