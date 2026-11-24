#include "forgeCad2026_gui.h"
#include "cad_document_tabs.h"
#include "forgeCad2026_application_theme.h"

#include <QApplication>
#include "cad_gpu_broad_phase.h"
#include <QIcon>
#include <QSurfaceFormat>
#ifdef Q_OS_MACOS
#include "cad_macos_menu.h"
#endif

// Tutta la scena usa shader, VAO e VBO OpenGL 3.3; il profilo Core impedisce
// che nuovi percorsi ricadano accidentalmente nella pipeline fissa.
static void requestCoreContext() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
}

int main(int argc, char **argv) {
    requestCoreContext();
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication application(argc, argv);
    ForgeCad::initializeGeometryAcceleration();
#ifdef Q_OS_MACOS
    // Il tema di piattaforma puo' nascondere le icone dei menu (macOS).
    // Impostarlo dopo QApplication e prima di creare le azioni e i menu.
    QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCAD"));
    // Identificatore stabile per conservare le preferenze QSettings esistenti.
    QCoreApplication::setApplicationName(QStringLiteral("ForgeCAD"));
    ForgeCad::initializeApplicationTheme(application);
#if defined(Q_OS_MACOS)
    QGuiApplication::setApplicationDisplayName(QStringLiteral("MacOs Cad for free"));
#elif defined(Q_OS_LINUX)
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Linux Cad for free"));
#else
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ForgeCAD"));
#endif
    // Icona della finestra (icons/forgecad.qrc) e nome del file .desktop: su
    // Wayland il compositore trova l'icona dall'app_id, che e' questo nome
    // (packaging/linux/install-desktop-integration.sh installa forgecad.desktop).
    QGuiApplication::setDesktopFileName(QStringLiteral("forgecad"));
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512}) icon.addFile(QStringLiteral(":/icons/forgecad-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);
    ForgeCad::DocumentTabsWindow window;
    window.newModel();
#ifdef Q_OS_MACOS
    ForgeCad::enableMacMenuIcons(window.menuBar());
#endif
    window.show();
    // ./forgecad documento.prt apre il documento.
    const QStringList arguments = application.arguments();
    for (int i=1;i<arguments.size();++i) window.openPath(arguments.at(i));
    return application.exec();
}
