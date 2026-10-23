// Bootstrap separato della prova; l’entrypoint e il binario abituali restano indipendenti.
#include "forgeCad2026_gui.h"
#include "forgeCad2026_application_theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QDialog>
#include <QToolButton>
#include <QAction>
#include <QIcon>
#include <QSurfaceFormat>
#include <QStyleHints>
#ifdef Q_OS_MACOS
#include "cad_macos_menu.h"
#endif

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

#include "forgeCad2026_ribbon_preview.h"
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTabBar>
#include <iostream>

int main(int argc,char **argv) {
    preferDiscreteGpu();requestCoreContext();QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication app(argc,argv);
    ForgeCad::initializeApplicationTheme(app);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCADPreview"));
    QCoreApplication::setApplicationName(QStringLiteral("RibbonExperiment"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ForgeCAD — Prova ribbon"));
    QGuiApplication::setDesktopFileName(QStringLiteral("forgecad-ribbon-preview"));
    QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus,false);
    const int documentTest=app.arguments().indexOf(QStringLiteral("--test-open-block"));
    const int themeTest=app.arguments().indexOf(QStringLiteral("--test-application-theme"));
    const bool selfTest=app.arguments().contains(QStringLiteral("--self-test")) || documentTest>=0 || themeTest>=0;
    QTemporaryDir settings;
    if(selfTest){QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());}
    QIcon icon;icon.addFile(QStringLiteral(":/icons/forgecad-256.png"));app.setWindowIcon(icon);
    PdfWindow window;
    ForgeCad::RibbonPreview *ribbon=nullptr;
    for(auto *widget:window.findChildren<QWidget *>())if(auto *candidate=dynamic_cast<ForgeCad::RibbonPreview *>(widget)){ribbon=candidate;break;}
    if(!ribbon)return 1;
#ifdef Q_OS_MACOS
    ForgeCad::enableMacMenuIcons(window.menuBar());
#endif
    if(selfTest) {
        if(QGuiApplication::platformName()!=QStringLiteral("offscreen")){window.resize(1280,800);window.show();QApplication::processEvents();}
        if(themeTest>=0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            if(themeTest+1>=app.arguments().size())return 2;
            const auto directory=app.arguments().at(themeTest+1);
            if(!QDir().mkpath(directory))return 2;
            window.resize(1440,900);
            QRect ribbonGeometry;
            for(bool dark:{false,true,false}) {
                const auto scheme=dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light;
                app.styleHints()->setColorScheme(scheme);
                for(int i=0;i<8;++i)QApplication::processEvents();
                if(app.styleHints()->colorScheme()!=scheme){std::cerr<<"Backend senza override nativo dello schema\n";return 77;}
                if((ribbon->palette().color(QPalette::Window).lightness()<128)!=dark ||
                   app.styleSheet().contains(QStringLiteral("#111820"))!=dark) {
                    std::cerr<<"Tema ribbon o foglio stile finestra non aggiornato\n";return 1;
                }
                if(!ribbonGeometry.isNull() && ribbon->geometry()!=ribbonGeometry){std::cerr<<"Geometria ribbon cambiata con il tema\n";return 1;}
                ribbonGeometry=ribbon->geometry();
                const auto path=QDir(directory).filePath(dark?QStringLiteral("ribbon-dark.png"):QStringLiteral("ribbon-light.png"));
                if(!window.grab().save(path))return 1;
            }
            std::cout<<"PASS tema nativo Qt: chiaro/scuro/chiaro, ribbon e foglio stile della finestra aperta, geometria invariata\n";
            return 0;
#else
            std::cerr<<"La prova nativa richiede Qt6.8 o successivo\n";return 77;
#endif
        }
        if(documentTest>=0) {
            if(documentTest+1>=app.arguments().size())return 2;
            window.resize(1800,1000);QApplication::processEvents();
            const auto original=app.arguments().at(documentTest+1);
            const auto originalBytes=QFileInfo(original).size();
            for(int round=0;round<3;++round) {
                const auto copy=settings.filePath(QStringLiteral("document-%1.prt").arg(round));
                if(!QFile::copy(original,copy) || !window.openDocumentPath(copy)){std::cerr<<"Impossibile aprire la copia del documento\n";return 1;}
                QApplication::processEvents();
                auto *tabs=ribbon->findChild<QTabBar *>("ribbonTabs");tabs->setCurrentIndex(0);QApplication::processEvents();QApplication::processEvents();
                QToolButton *block=nullptr;
                for(auto *button:ribbon->findChildren<QToolButton *>())
                    if(button->defaultAction() && button->defaultAction()->text().contains(QStringLiteral("Parallelepipedo")) && !button->isHidden()){block=button;break;}
                if(!block){window.grab().save("/tmp/ribbon-block-test-failure.png");std::cerr<<"Pulsante Blocco non disponibile\n";for(auto *b:ribbon->findChildren<QToolButton *>())if(b->defaultAction())std::cerr<<b->defaultAction()->text().toStdString()<<" hidden="<<b->isHidden()<<"\n";return 1;}
                bool opened=false;
                QTimer::singleShot(0,&window,[&]{for(auto *dialog:window.findChildren<QDialog *>())if(dialog->objectName()=="functionDialogPanel" && dialog->isVisible()){opened=true;dialog->reject();}});
                block->click();QApplication::processEvents();
                if(!opened){std::cerr<<"Pannello Blocco non aperto\n";return 1;}
                std::cout<<"PASS apertura documento e clic Blocco, ciclo "<<round+1<<'\n';
            }
            return QFileInfo(original).size()==originalBytes?0:1;
        }
        try{ribbon->runSelfTest();std::cout<<"PASS ribbon sperimentale: azioni reali, contesti, larghezza, pannelli e isolamento preferenze\n";return 0;}
        catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
    }
    window.resize(1440,900);window.show();
    const auto arguments=app.arguments();
    for(const auto &argument:arguments)if(argument.startsWith(QStringLiteral("--ribbon-tab="))) {
        if(auto *tabs=ribbon->findChild<QTabBar *>(QStringLiteral("ribbonTabs")))
            for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString()==argument.mid(13))tabs->setCurrentIndex(i);
    }
    const int capture=arguments.indexOf(QStringLiteral("--capture"));
    if(capture>=0 && capture+1<arguments.size())QTimer::singleShot(1200,&window,[&window,&app,path=arguments.at(capture+1)]{const bool ok=window.grab().save(path);app.exit(ok?0:1);});
    for(int i=1;i<arguments.size();++i)if(!arguments.at(i).startsWith('-') && (capture<0||i!=capture+1)){window.openDocumentPath(arguments.at(i));break;}
    return app.exec();
}
