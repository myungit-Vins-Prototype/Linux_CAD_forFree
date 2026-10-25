#pragma once
#include "../cad_drawing_file.h"
#include "../cad_drawing_derived.h"
#include "../cad_drawing_silhouette.h"
#include "../cad_drawing_visibility.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace {
// Diagnostica esplicita su .drw esterno: nessuna scrittura, lock o ricostruzione
// salvata. Una cache mesh nuova per foglio, condivisa fra le viste come nell'editor.
void drawingPerformanceTest(const QString &path) {
    using namespace ForgeCad;
    const auto check=[](const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());};
    const auto output=[](const QJsonObject &data){std::cout<<QJsonDocument(data).toJson(QJsonDocument::Compact).constData()<<std::endl;};
    QElapsedTimer timer;timer.start();
    const auto elapsed=[&]{const double ms=timer.nsecsElapsed()/1e6;timer.restart();return ms;};
    DrawingFile drawing;check(loadDrawingFile(path,drawing));
    DocumentState document;QString resolved;bool changed=false;
    check(loadDrawingModel(path,drawing,document,resolved,changed));
    if(changed)throw std::runtime_error("Il modello differisce dalla versione della tavola: benchmark rifiutato.");
    const double load=elapsed();DrawingEditorModel model;check(prepareDrawingEditorModel(document,model));
    output({{"phase","model"},{"load_ms",load},{"prepare_ms",elapsed()}});
    for(int sheetIndex=0;sheetIndex<drawingSheetCount(drawing);++sheetIndex){
        const auto &sheet=drawingSheetAt(drawing,sheetIndex);DrawingSilhouetteCache cache;
        for(const auto &view:sheet.drawing.views){
            const auto layout=std::find_if(sheet.views.begin(),sheet.views.end(),[&](const auto &l){return l.viewId==view.id;});
            if(layout==sheet.views.end())throw std::runtime_error("Layout vista mancante.");
            DrawingModelSnapshot source;source.version=model.snapshot.version;
            for(const auto &body:model.snapshot.bodies)if(layout->features.contains(body.featureId))source.bodies.push_back(body);
            std::vector<DrawingCurve> curves;DrawingSilhouetteResult contours;DrawingSectionHatch hatch;DrawingViewVisibility display;
            timer.restart();check(drawingViewCurves(source,view,curves));const double curveMs=elapsed();
            check(drawingViewSilhouettes(source,view,contours,cache));const double contourMs=elapsed();
            check(drawingSectionHatch(source,view,curves,hatch));const double hatchMs=elapsed();
            check(drawingViewVisibility(source,view,curves,contours,cache,display,&hatch.material,layout->showMesh));const double visibilityMs=elapsed();
            // Impronta prima del ritaglio dettaglio/interruzione: comprende anche
            // geometria che non si vede nella finestra ritagliata del foglio.
            QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
            stream<<hatch.material<<hatch.lines<<hatch.warnings;
            for(const auto *paths:{&display.curves,&display.silhouettes})for(const auto &p:*paths)stream<<p.visible<<p.hidden;
            output({{"phase","view"},{"sheet",sheetIndex+1},{"view",layout->title},{"scale",view.scale},
                {"curves_ms",curveMs},{"contours_ms",contourMs},{"hatch_ms",hatchMs},{"visibility_ms",visibilityMs},
                {"curves",int(curves.size())},{"contours",int(contours.paths.size())},
                {"paths_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});
        }
        timer.restart();DrawingSheetEditor editor(sheet,model);
        output({{"phase","editor"},{"sheet",sheetIndex+1},{"prepare_ms",elapsed()}});
    }
}
}
