#include "forgeCad2026_gui.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QSurfaceFormat>

// Su portatili ibridi (Intel + NVIDIA) forza il rendering OpenGL sulla GPU
// NVIDIA tramite PRIME render offload. Va fatto prima di creare QApplication,
// perche' le librerie EGL/GLX vengono caricate all'inizializzazione di Qt.
// Impostare FORGECAD_IGPU=1 per restare sulla GPU integrata.
static void preferDiscreteGpu() {
    if (qEnvironmentVariableIsSet("FORGECAD_IGPU")) return;
    const QString nvidiaEgl = QStringLiteral("/usr/share/glvnd/egl_vendor.d/10_nvidia.json");
    if (!QFileInfo::exists(nvidiaEgl)) return;
    qputenv("__EGL_VENDOR_LIBRARY_FILENAMES", nvidiaEgl.toUtf8());
    qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
    qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
}

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
    preferDiscreteGpu();
    requestCoreContext();
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCAD"));
    QCoreApplication::setApplicationName(QStringLiteral("ForgeCAD"));
    // Icona della finestra (icons/forgecad.qrc) e nome del file .desktop: su
    // Wayland il compositore trova l'icona dall'app_id, che e' questo nome
    // (packaging/linux/install-desktop-integration.sh installa forgecad.desktop).
    QGuiApplication::setDesktopFileName(QStringLiteral("forgecad"));
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512}) icon.addFile(QStringLiteral(":/icons/forgecad-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);
    PdfWindow window;
    window.show();
    // ./forgecad documento.prt apre il documento.
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1) window.openDocumentPath(arguments.at(1));
    return application.exec();
}
