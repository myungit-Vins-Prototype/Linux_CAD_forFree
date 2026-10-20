#include "cad_drawing_compute.h"
#include <QDialog>
#include <QMainWindow>
#include <QPushButton>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace ForgeCad;
static void need(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static void pump(){for(int i=0;i<30;++i){QApplication::processEvents();QThread::msleep(10);}}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    try{
        QMainWindow model;
        model.setObjectName("model3D");model.show();
        QMainWindow drawing(&model,Qt::Window);
        drawing.setObjectName("drawing2D");
        QWidget editor(&drawing);
        drawing.show();drawing.raise();drawing.activateWindow();pump();
        const bool native=QGuiApplication::platformName()!=QStringLiteral("offscreen")&&QGuiApplication::platformName()!=QStringLiteral("minimal");
        if(native)need(QApplication::activeWindow()==&drawing,"precondition: drawing active");
        bool busy=false,observed=false,correctParent=false,correctModality=false,blocked=false;
        QTimer monitor;monitor.setInterval(10);
        QObject::connect(&monitor,&QTimer::timeout,[&]{
            if(auto *dialog=drawing.findChild<QProgressDialog *>("drawingViewProgress")){
                observed=true;correctParent=dialog->parentWidget()==&drawing;
                correctModality=dialog->windowModality()==Qt::WindowModal;
                blocked=busy&&!editor.isEnabled();
            }
        });monitor.start();
        int result=0;
        auto error=runDrawingComputation(&editor,busy,"Calcolo lento",result,[](auto phase){QThread::msleep(650);phase(5);return 42;});
        pump();monitor.stop();
        need(error.isEmpty()&&result==42&&!busy&&editor.isEnabled(),"successful computation restores input");
        need(observed&&correctParent&&correctModality&&blocked,"progress belongs to drawing and blocks editor");
        if(native)need(QApplication::activeWindow()==&drawing,"drawing active after slow computation");
        error=runDrawingComputation(&editor,busy,"Errore",result,[](auto)->int{throw std::runtime_error("expected");});pump();
        need(error=="expected"&&!busy&&editor.isEnabled(),"exception restores input");
        if(native)need(QApplication::activeWindow()==&drawing,"drawing active after exception");
        QTimer::singleShot(350,&drawing,[&]{
            if(auto *dialog=drawing.findChild<QProgressDialog *>("drawingViewProgress"))
                if(auto *button=dialog->findChild<QPushButton *>())button->click();
        });
        error=runDrawingComputation(&editor,busy,"Annullamento",result,[](auto phase){QThread::msleep(650);phase(5);return 99;});pump();
        need(error==QStringLiteral("Calcolo della vista annullato.")&&!busy&&editor.isEnabled()&&result==42,"cancellation preserves result and restores input");
        if(native)need(QApplication::activeWindow()==&drawing,"drawing active after cancellation");
        // Simula il ritorno nativo alla finestra proprietaria quando un dialogo
        // si chiude; la guardia deve restituire l'attivazione alla tavola.
        {DrawingFocusReturn focus(&drawing);model.raise();model.activateWindow();pump();}
        pump();if(native)need(QApplication::activeWindow()==&drawing,"dialog focus returns to drawing");
        // Un comando avviato senza primo piano non deve sottrarlo al modello.
        model.raise();model.activateWindow();pump();
        {DrawingFocusReturn focus(&drawing);}
        pump();if(native)need(QApplication::activeWindow()==&model,"inactive drawing does not steal focus");
        std::cout<<"PASS progress ownership, input, exception, focus return and inactive owner\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
