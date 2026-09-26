#include "forgeCad2026_gui.h"

#include <QApplication>
#include <QFileInfo>
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

// Il viewport usa OpenGL a pipeline fissa (glBegin/glEnd, luci fisse):
// serve un contesto desktop in compatibility profile. Senza questa richiesta
// il driver NVIDIA via EGL restituisce un contesto OpenGL ES, dove quelle
// chiamate non disegnano nulla.
static void requestCompatibilityContext() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
}

int main(int argc, char **argv) {
    preferDiscreteGpu();
    requestCompatibilityContext();
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCAD"));
    QCoreApplication::setApplicationName(QStringLiteral("ForgeCAD"));
    PdfWindow window;
    window.show();
    // ./forgecad documento.prt apre il documento.
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1) window.openDocumentPath(arguments.at(1));
    return application.exec();
}
