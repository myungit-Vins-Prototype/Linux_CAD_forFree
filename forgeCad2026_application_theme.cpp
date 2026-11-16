#include "forgeCad2026_application_theme.h"

#include <QApplication>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <QEvent>
#include <QPalette>
#include <QStyleHints>
#include <QTimer>
#include <QStyle>
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QScopedValueRollback>

namespace ForgeCad {
namespace {
constexpr auto themeObjectName = "forgecadApplicationTheme";
constexpr auto themeSettingsKey = "view/applicationTheme";

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

QString applicationStyleSheet(bool dark) {
    return dark ? QStringLiteral("QMainWindow { background: #111820; color: #d8e4ea; }QMenuBar,QToolBar,QStatusBar { background: #1b2730; color: #d8e4ea; }QMenu { background: #202d36; color: #d8e4ea; }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #34444f; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:checked { background: #24495f; border-color: #4f9fd0; }"
        "QToolButton:enabled:hover, QToolBar QToolButton:enabled:hover { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QToolButton:enabled:pressed, QToolBar QToolButton:enabled:pressed { background: #1f4258; border-color: #80c8f0; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolButton::menu-button:enabled:hover { background: #5089ad; }"
        "QToolButton::menu-button:enabled:pressed { background: #1f4258; }"
        "QPushButton:enabled:hover { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QPushButton:enabled:pressed { background: #1f4258; border-color: #80c8f0; }"
        "QMenuBar::item:selected:enabled { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QMenuBar::item:pressed:enabled { background: #1f4258; border-color: #80c8f0; color: #ffffff; }"
        "QMenu::item:selected:enabled { background: #365d78; border-color: #80c8f0; color: #ffffff; }"
        "QMenu::item:disabled { color: #788995; }") : QStringLiteral(
        "QMainWindow { background: palette(window); color: palette(window-text); }"
        "QMenuBar,QToolBar,QStatusBar,QMenu { background: palette(window); color: palette(window-text); }"
        "QToolBar { spacing: 1px; padding: 2px 4px; border: none; }"
        "QToolBar::separator { background: #c3cbd2; width: 1px; margin: 5px 3px; }"
        "QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }"
        "QToolBar QToolButton:checked { background: #d6eafb; border-color: #659dca; }"
        "QToolButton:enabled:hover, QToolBar QToolButton:enabled:hover { background: #e2eff9; border-color: #659dca; }"
        "QToolButton:enabled:pressed, QToolBar QToolButton:enabled:pressed { background: #c1dcf1; border-color: #427fab; }"
        "QToolBar QToolButton[popupMode=\"1\"] { padding-right: 11px; }"
        "QToolBar QToolButton::menu-button { border: none; width: 10px; }"
        "QToolButton::menu-button:enabled:hover { background: #c1dcf1; }"
        "QToolButton::menu-button:enabled:pressed { background: #a9cee9; }"
        "QMenuBar::item:selected:enabled,QMenu::item:selected:enabled { background: #d6eafb; color: palette(window-text); }"
        "QMenuBar::item:pressed:enabled { background: #c1dcf1; }"
        "QMenu::item:disabled { color: palette(disabled, window-text); }");
}

class ApplicationTheme final : public QObject {
    QApplication &application_;
    ApplicationThemeMode mode_ = applicationThemeMode();
    bool fallbackDark_;
    bool applying_ = false;
    bool scheduled_ = false;
    QPointer<QStyle> commonStyle_;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    Qt::ColorScheme scheme_;
#endif

    bool dark() const {
        if (mode_ != ApplicationThemeMode::Desktop) return mode_ == ApplicationThemeMode::Dark;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (scheme_ != Qt::ColorScheme::Unknown) return scheme_ == Qt::ColorScheme::Dark;
#endif
        // Compatibilità con Qt precedenti a 6.5 e backend senza colorScheme.
        return fallbackDark_;
    }

    void apply() {
        if (applying_) { schedule(); return; }
        const QScopedValueRollback<bool> applying(applying_, true);
        const bool isDark = dark();
        // Alcuni temi di piattaforma sostituiscono QStyle durante il cambio OS.
        // Seguendo la vita dello stile reale funzioniamo anche sotto QStyleSheetStyle.
        if (!commonStyle_) {
            commonStyle_=application_.setStyle(QStringLiteral("Fusion"));
            connect(commonStyle_,&QObject::destroyed,this,[this] {
                if(QCoreApplication::closingDown())return;
                trace("external-style-replacement");
                if(!applying_)schedule();
            });
            trace("install-fusion");
        }
        const auto palette = applicationPalette(isDark);
        if (application_.palette() != palette) application_.setPalette(palette);
        const auto sheet = applicationStyleSheet(isDark);
        if (application_.styleSheet() != sheet) application_.setStyleSheet(sheet);
        trace("apply-palette");
    }

    void trace(const char *reason) const {
        const auto directory=qEnvironmentVariable("FORGECAD_MENU_DIAGNOSTICS");if(directory.isEmpty())return;
        QDir dir(directory);if(!dir.mkpath(QStringLiteral(".")))return;
        QFile file(dir.filePath(QStringLiteral("theme.jsonl")));if(!file.open(QIODevice::WriteOnly|QIODevice::Append))return;
        const auto *style=application_.style();
        const QJsonObject record{{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"reason",QString::fromLatin1(reason)},{"qt",QString::fromLatin1(qVersion())},
            {"backend",QGuiApplication::platformName()},{"styleClass",QString::fromLatin1(style->metaObject()->className())},
            {"styleName",style->objectName()},{"commonStyleAlive",bool(commonStyle_)},
            {"dark",dark()},{"windowColor",application_.palette().color(QPalette::Window).name()}};
        file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));file.write("\n");
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
    void setMode(ApplicationThemeMode mode) { mode_ = mode; apply(); }

    explicit ApplicationTheme(QApplication &application)
        : QObject(&application), application_(application),
          fallbackDark_(application.palette().color(QPalette::Window).lightness() < 128)
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
          , scheme_(application.styleHints()->colorScheme())
#endif
    {
        setObjectName(QString::fromLatin1(themeObjectName));
        application_.installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(application_.styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme scheme) { scheme_ = scheme; schedule(); });
#endif
        apply();
    }
};
}

ApplicationThemeMode applicationThemeMode() {
    const auto saved = QSettings().value(QString::fromLatin1(themeSettingsKey)).toString();
    if (saved == QStringLiteral("light")) return ApplicationThemeMode::Light;
    if (saved == QStringLiteral("dark")) return ApplicationThemeMode::Dark;
    return ApplicationThemeMode::Desktop;
}

void setApplicationThemeMode(QApplication &application, ApplicationThemeMode mode) {
    initializeApplicationTheme(application);
    const QString saved = mode == ApplicationThemeMode::Light ? QStringLiteral("light")
        : mode == ApplicationThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("desktop");
    QSettings().setValue(QString::fromLatin1(themeSettingsKey), saved);
    auto *controller = static_cast<ApplicationTheme *>(application.findChild<QObject *>(
        QString::fromLatin1(themeObjectName), Qt::FindDirectChildrenOnly));
    controller->setMode(mode);
}

void setupApplicationThemeMenu(QMenu &themeMenu) {
    auto *menu = &themeMenu;
    menu->setObjectName(QStringLiteral("applicationThemeMenu"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    const QStringList labels{QStringLiteral("Segui il desktop"), QStringLiteral("Chiaro"), QStringLiteral("Scuro")};
    const QStringList names{QStringLiteral("applicationThemeDesktop"), QStringLiteral("applicationThemeLight"), QStringLiteral("applicationThemeDark")};
    const ApplicationThemeMode modes[]{ApplicationThemeMode::Desktop, ApplicationThemeMode::Light, ApplicationThemeMode::Dark};
    for (int i = 0; i < labels.size(); ++i) {
        auto *action = menu->addAction(labels[i]);
        action->setObjectName(names[i]);
        action->setCheckable(true);
        action->setData(int(modes[i]));
        action->setChecked(applicationThemeMode() == modes[i]);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [mode = modes[i]] { setApplicationThemeMode(*qApp, mode); });
    }
    QObject::connect(menu, &QMenu::aboutToShow, menu, [group] {
        for (auto *action : group->actions()) action->setChecked(action->data().toInt() == int(applicationThemeMode()));
    });
}

void initializeApplicationTheme(QApplication &application) {
    if (!application.findChild<QObject *>(QString::fromLatin1(themeObjectName), Qt::FindDirectChildrenOnly))
        new ApplicationTheme(application);
}
}
