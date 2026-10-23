#include "forgeCad2026_application_theme.h"

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QStyleHints>
#include <QTimer>

namespace ForgeCad {
namespace {
constexpr auto themeObjectName = "forgecadApplicationTheme";

QPalette applicationPalette(bool dark) {
    const QColor window(dark ? "#111820" : "#f0f0f0");
    const QColor text(dark ? "#d8e4ea" : "#202020");
    const QColor base(dark ? "#202d36" : "#ffffff");
    const QColor button(dark ? "#1b2730" : "#f0f0f0");
    const QColor muted(dark ? "#788995" : "#737373");
    // Il costruttore deriva anche i ruoli di rilievo/ombra dal colore comune,
    // senza importare accenti o colori del tema nativo.
    QPalette palette(window);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, button);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Highlight, QColor(dark ? "#24495f" : "#d6eafb"));
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::Link, QColor(dark ? "#80c8f0" : "#1b65a0"));
    palette.setColor(QPalette::LinkVisited, QColor(dark ? "#c5a4eb" : "#704096"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, QColor(dark ? "#4f9fd0" : "#427fab"));
#endif
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                      QPalette::HighlightedText})
        palette.setColor(QPalette::Disabled, role, muted);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, button);
    return palette;
}

class ApplicationTheme final : public QObject {
    QApplication &application_;
    bool fallbackDark_;
    bool applying_ = false;
    bool scheduled_ = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    Qt::ColorScheme scheme_;
#endif

    bool dark() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (scheme_ != Qt::ColorScheme::Unknown) return scheme_ == Qt::ColorScheme::Dark;
#endif
        // Compatibilità con Qt precedenti a 6.5 e backend senza colorScheme.
        return fallbackDark_;
    }

    void apply() {
        applying_ = true;
        const auto palette = applicationPalette(dark());
        if (application_.palette() != palette) application_.setPalette(palette);
        applying_ = false;
    }

    void schedule() {
        if (scheduled_) return;
        scheduled_ = true;
        // colorSchemeChanged precede l'aggiornamento della palette Qt:
        // applicare i nostri colori dopo che il cambio nativo è terminato.
        QTimer::singleShot(0, this, [this] { scheduled_ = false; apply(); });
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::ApplicationPaletteChange && !applying_) {
            fallbackDark_ = application_.palette().color(QPalette::Window).lightness() < 128;
            schedule();
        }
        return QObject::eventFilter(object, event);
    }

public:
    explicit ApplicationTheme(QApplication &application)
        : QObject(&application), application_(application),
          fallbackDark_(application.palette().color(QPalette::Window).lightness() < 128)
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
          , scheme_(application.styleHints()->colorScheme())
#endif
    {
        setObjectName(QString::fromLatin1(themeObjectName));
        application_.setStyle(QStringLiteral("Fusion"));
        application_.installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(application_.styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme scheme) { scheme_ = scheme; schedule(); });
#endif
        apply();
    }
};
}

void initializeApplicationTheme(QApplication &application) {
    if (!application.findChild<QObject *>(QString::fromLatin1(themeObjectName), Qt::FindDirectChildrenOnly))
        new ApplicationTheme(application);
}
}
