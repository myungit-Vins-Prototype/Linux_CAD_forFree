#include <condition_variable>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include "../cad_gpu_broad_phase.h"
#include "../cad_cuda_tessellation.h"
#include "../cuda_support.h"
#include <QSaveFile>
// I componenti GUI sono dichiarati negli header e compilati separatamente.
#include "../forgeCad2026_gui.h"
#include "../forgeCad2026_viewport.h"
#include "../forgeCad2026_storyboard.h"
#include "../forgeCad2026_dialogs.h"
#include "../cad_expression.h"
#include "../cad_features.h"
#include "../cad_feature_build.h"
#include "../cad_feature_dependencies.h"
#include "../cad_datum.h"
#include "../cad_forge.h"
#include "../cad_pattern.h"
#include "../cad_extrude.h"
#include "../cad_sketch_refs.h"
#include "../cad_history_graph.h"
#include "../cad_model_history.h"
#include "../cad_topology_ref.h"
#include "../cad_icons.h"
#include "../cad_menu.h"
#include "../cad_kernel.h"
#include "../cad_curve_extract.h"
#include "../cad_surface_tools.h"
#include "../cad_spatial_sketch.h"
#include "../kernel/tests/surface_tools_fixture.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include "../cad_measure.h"
#include "../toolbar_editor.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

using ForgeCad::Gui::uiFont;
using ForgeCad::Gui::pointLength;
using ForgeCad::Gui::pointDistance;
using ForgeCad::Gui::featureIconName;
using ForgeCad::Gui::kSelectionColor;
using ForgeCad::Gui::kHoverColor;
using ForgeCad::Gui::runUntilApplied;
using ForgeCad::Gui::wrappedNote;
#include "fk_blend.h"
#include "fk_blend_surface.h"
#include "fk_body_check.h"
#include "fk_boolean.h"
#include "fk_body_io.h"
#include "../cad_snapshot_chunks.h"
#include "fk_step.h"
#include "fk_iges.h"
#include "fk_classify.h"
#include "fk_extrude.h"
#include "fk_sew.h"
#include "fk_helix.h"
#include "fk_mass.h"
#include "fk_pcurve.h"
#include "fk_primitives.h"
#include "fk_revolve.h"
#include "fk_sheet.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include "fk_loft.h"
#include "fk_project.h"
#include "fk_parallel.h"
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include "../cad_element_names.h"
#include <QProcess>
#include <QDateTime>
#include <QPointingDevice>
#include <QSemaphore>
#include <QCryptographicHash>
#include <QtEndian>
#include <fstream>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

// Configurabile anche nei lanci diretti del runner; CTest passa la cartella da CMake.
static QString exampleDocumentPath(const char *name) {
    const QString directory = qEnvironmentVariable("FORGECAD_EXAMPLE_DIR", QStringLiteral(FORGECAD_SOURCE_DIR "/File_Esempio"));
    return QDir(directory).filePath(QString::fromUtf8(name));
}

#include "test_declared_history_cache.h"
#include "test_boolean_history.h"
#include "test_extrusion_history.h"
#include "test_revolution_history.h"
#include "test_all_history.h"
#include "test_native_revisions.h"
#include "test_workspace_revisions.h"
#include "test_linked_drawing.h"
#include "test_drawing_revisions.h"
#include "test_projected_drawing_file.h"
#include "test_drawing_multisheet.h"
#include "test_drawing_revision_restore.h"
#include "test_drawing_release.h"
#include "test_drawing_mouse.h"
#include "test_drawing_section_model.h"
#include "test_drawing_performance.h"
#include "document_regression.h"
#include "test_document_read.h"
#include "test_history_suppression.h"
#include "test_pattern_placements.h"
#include "test_zoom_input.h"
#include "test_solver_partition.h"
class ViewportInteractionTest {
public:
#include "test_sketch_interaction.h"
#include "test_sketch_slot_point.h"
#include "test_helix_start_angle.h"
#include "test_sketch_menu_cancel.h"
#include "test_document_recovery.h"
#include "test_document_consultation.h"
#include "test_catalog_recovery_gui.h"
#include "test_surface_tools_gui.h"
#include "test_document_units.h"
#include "test_function_panels.h"
#include "test_lazy_mesh.h"
#include "test_display_mesh.h"
#include "test_mesh_memory.h"
#include "test_history_preview_mesh.h"
    static void extrusionActionPreconditions() {
        using namespace ForgeCad;
        PdfWindow window;
        window.resize(1000, 760);
        window.show();
        QApplication::processEvents();
        auto *view = window.viewport_;
        QAction *extrude = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Estrusione...")) { extrude = action; break; }
        require(extrude && extrude->isEnabled(), "azione reale Estrusione disponibile");

        SketchObject valid;
        valid.name = QStringLiteral("Profilo di prova");
        valid.segments = {{{0,0},{4,0}},{{4,0},{4,3}},{{4,3},{0,3}},{{0,3},{0,0}}};
        SketchObject construction = valid;
        construction.constructionSegments = {0,1,2,3};
        CurveObject circle; circle.tool = DrawingTool::Circle; circle.controlPoints = {{0,0},{2,0}};
        circle.construction = true; construction.curves.append(circle);
        SketchObject pointOnly;
        CurveObject point; point.tool = DrawingTool::Point; point.controlPoints = {{1,2}}; pointOnly.curves.append(point);
        SketchObject degenerate; degenerate.segments = {{{1,1},{1,1}}};
        SketchObject circleOnly; circle.construction = false; circleOnly.curves.append(circle);
        struct Scenario { const char *name; SketchObject sketch; bool present; int active; int selected; bool warning; };
        const QVector<Scenario> scenarios = {
            {"nessuno schizzo", {}, false, -1, -1, true},
            {"schizzo vuoto attivo", {}, true, 0, -1, true},
            {"sola geometria di costruzione", construction, true, 0, -1, true},
            {"soli punti", pointOnly, true, 0, -1, true},
            {"segmenti degeneri", degenerate, true, 0, -1, true},
            {"profilo valido senza selezione o schizzo attivo", valid, true, -1, -1, true},
            {"profilo valido attivo", valid, true, 0, -1, false},
            {"profilo valido selezionato", valid, true, -1, 0, false},
            {"curva valida selezionata", circleOnly, true, -1, 0, false}
        };
        for (const auto &scenario : scenarios) {
            DocumentState document;
            if (scenario.present) document.sketches.append(scenario.sketch);
            view->loadDocument(document);
            view->activeSketch_ = scenario.active;
            view->sketchMode_ = scenario.active >= 0;
            view->selection_ = scenario.selected >= 0 ? SceneSelection{SceneObjectKind::Sketch, scenario.selected, -1} : SceneSelection{};
            bool messageSeen = false, informative = false, panelSeen = false, timedOut = false;
            QString observedMessage; int observedIcon = -1;
            QElapsedTimer elapsed; elapsed.start();
            QTimer inspect;
            QObject::connect(&inspect, &QTimer::timeout, &window, [&] {
                if (elapsed.elapsed() > 5000) timedOut = true;
                for (auto *opened : window.findChildren<QDialog *>()) {
                    if (!opened->isVisible()) continue;
                    if (auto *message = qobject_cast<QMessageBox *>(opened)) {
                        messageSeen = true;
                        observedMessage = message->text(); observedIcon = int(message->icon());
                        informative = message->icon() == QMessageBox::Information
                            && message->text().contains(QStringLiteral("schizzo"), Qt::CaseInsensitive)
                            && message->text().contains(QStringLiteral("profilo"), Qt::CaseInsensitive);
                        message->reject();
                    } else if (auto *panel = dynamic_cast<FunctionDialogPanel *>(opened)) {
                        panelSeen = panel->panelSizeSettingsKey() == QStringLiteral("view/functionPanelSizes/v2/extrusion");
                        panel->reject();
                    } else if (timedOut) opened->reject();
                }
            });
            inspect.start(10);
            extrude->trigger();
            inspect.stop();
            require(!timedOut && view->extrusions().isEmpty(), "azione Estrusione annullata senza creare feature o timeout");
            const bool expected = scenario.warning ? messageSeen && informative && !panelSeen : panelSeen && !messageSeen;
            require(expected, qPrintable(QStringLiteral("azione Estrusione: %1; message=%2 informative=%3 icon=%4 panel=%5 timeout=%6 text=%7")
                .arg(QString::fromUtf8(scenario.name)).arg(messageSeen).arg(informative).arg(observedIcon).arg(panelSeen).arg(timedOut).arg(observedMessage)));
        }
        std::cout << "PASS extrusion action preconditions: warnings and valid active/selected profiles" << std::endl;
    }
    static void previewPerformance(bool gl = false) {
        using namespace ForgeCad;using namespace ForgeCad::Kernel;
        const auto settle=[](CadViewport &view) {
            QElapsedTimer timer;timer.start();
            while((view.previewRunning_ || (view.previewTimer_ && view.previewTimer_->isActive())
                   || (!view.preview_.valid && view.preview_.error.isEmpty())) && timer.elapsed()<10000)
                QApplication::processEvents();
            if(!view.preview_.error.isEmpty())std::cerr<<view.preview_.error.toStdString()<<std::endl;
            require(view.preview_.valid && !view.previewRunning_,"anteprima pronta e valida");
        };
        for(int mode=0;mode<4;++mode) {
            CadViewport view;
            SketchObject profile;
            profile.segments={{{2,0},{4,0}},{{4,0},{4,2}},{{4,2},{2,2}},{{2,2},{2,0}}};
            if(mode==1)profile.plane=1;
            view.sketches_.append(profile);
            ExtrusionObject a;a.sketchIndex=0;a.distance=5;
            if(mode==1){a.feature=BodyFeature::Revolution;a.revolveAxis=-2;a.revolveAngle=90;}
            if(mode==2){
                SketchObject path;path.plane=1;path.segments={{{0,0},{0,8}}};view.sketches_.append(path);
                a.feature=BodyFeature::Sweep;a.pathSketch=1;a.pathSegments={0};
            }
            if(mode==3){
                auto upper=profile;upper.customFrame=true;upper.frame.origin[2]=8;
                upper.frame.normal[2]=1;upper.frame.xAxis[0]=1;view.sketches_.append(upper);
                a.feature=BodyFeature::Loft;a.loftSketches={0,1};
            }
            view.requestPreview(a);settle(view);
            const auto first=view.preview_.geometry;
            const auto firstDisplay=view.preview_.display;
            auto b=a;
            if(mode==0)b.distance=6;
            if(mode==1)b.revolveAngle=120;
            if(mode==2)b.sweepSurface=true;
            if(mode==3)b.loftRuled=true;
            view.requestPreview(b);settle(view);
            QElapsedTimer hit;hit.start();view.requestPreview(a);
            require(view.preview_.valid && view.preview_.geometry==first && !view.previewRunning_,
                    "parametri ripetuti riusano immediatamente il risultato esatto");
            require(view.preview_.display.vertices==firstDisplay.vertices
                    && view.preview_.display.constructionCurves==firstDisplay.constructionCurves,
                    "riuso conserva mesh e U/V");
            std::cout<<"preview cache mode="<<mode<<" hit_ms="<<hit.nsecsElapsed()/1e6<<std::endl;
            if(!qEnvironmentVariable("FORGECAD_PREVIEW_QA_DIR").isEmpty()) {
                view.resize(1000,750);view.setViewPreset(4);view.fitView(view.preview_.display.vertices);
                view.show();QApplication::processEvents();
                require(view.grabFramebuffer().save(qEnvironmentVariable("FORGECAD_PREVIEW_QA_DIR")
                        +QString("/preview-%1.png").arg(mode)),"acquisizione anteprima nativa");view.hide();
            }
            if (gl && mode < 2) {
                // Estrusione e rivoluzione aperte dallo schizzo lo mantengono
                // attivo: SketchViewUnlock sospende soltanto l'editing 2D.
                view.selectSketch(0);
                view.setSketchViewUnlocked(true);
                view.resize(1000,750);view.setViewPreset(4);view.fitView(view.preview_.display.vertices);
                view.show();QApplication::processEvents();
                require(view.sketchMode_ && view.sketchViewUnlocked_ && view.preview_.valid,
                        "anteprima valida con schizzo attivo e vista sbloccata");
                const auto visible = view.grabFramebuffer();
                require(!visible.isNull(), "framebuffer nativo disponibile per anteprima");
                // Stessa scena e camera: togliendo solo la validita' della
                // preview deve sparire una superficie, non un'etichetta UI.
                view.preview_.valid = false;
                const auto hidden = view.grabFramebuffer();
                view.preview_.valid = true;
                int changed = 0;
                for (int y=0;y<visible.height();++y)
                    for (int x=0;x<visible.width();++x)
                        if (visible.pixel(x,y)!=hidden.pixel(x,y)) ++changed;
                std::cout << "preview sketch mode=" << mode << " changed_pixels=" << changed << std::endl;
                const QString directory=qEnvironmentVariable("FORGECAD_PREVIEW_QA_DIR");
                if (!directory.isEmpty()) {
                    require(visible.save(directory+QString("/sketch-preview-%1.png").arg(mode)), "cattura anteprima da schizzo");
                    require(hidden.save(directory+QString("/sketch-without-preview-%1.png").arg(mode)), "cattura controllo senza anteprima");
                }
                require(changed>visible.width()*visible.height()/100,
                        "anteprima estrusione/rivoluzione disegnata anche dallo schizzo");
                view.setSketchViewUnlocked(false);
                const auto locked = view.grabFramebuffer();
                view.preview_.valid = false;
                const auto lockedWithoutPreview = view.grabFramebuffer();
                view.preview_.valid = true;
                require(locked==lockedWithoutPreview, "editing 2D normale continua a escludere le anteprime feature");
                view.endSketchMode();view.hide();
            }
            if(mode==0){
                // Il worker superato non deve sostituire un risultato ripreso dalla cache.
                auto *pool=QThreadPool::globalInstance();const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
                QSemaphore entered,release;
                pool->start([&]{entered.release();release.acquire();});entered.acquire();
                auto c=a;c.distance=7;view.requestPreview(c);
                QElapsedTimer timer;timer.start();while(!view.previewRunning_ && timer.elapsed()<1000)QApplication::processEvents();
                const bool running=view.previewRunning_;view.requestPreview(a);
                const bool restored=view.preview_.valid && view.preview_.geometry==first;
                release.release();pool->setMaxThreadCount(threads);settle(view);
                require(running && restored && view.preview_.geometry==first,
                        "cancellazione di un calcolo precedente preserva il risultato riusato");
            }
            for(auto &segment:view.sketches_[0].segments){segment.first+=QPointF(.25,0);segment.second+=QPointF(.25,0);}
            view.requestPreview(a);settle(view);
            require(view.preview_.geometry!=first,"modifica dello schizzo invalida i risultati precedenti");
            const auto previous=view.preview_.geometry;
            view.tessellationQuality_=0;view.requestPreview(a);settle(view);
            require(view.preview_.geometry!=previous && view.preview_.display.quality==0,
                    "cambio qualita' invalida il risultato grafico");
            view.clearPreview();require(view.previewCache_.empty() && !view.previewCacheContextValid_,
                                        "chiusura funzione libera cache e contesto");
        }
        {
            CadViewport view;SketchObject profile;
            profile.segments={{{0,0},{2,0}},{{2,0},{2,2}},{{2,2},{0,2}},{{0,2},{0,0}}};view.sketches_.append(profile);
            ExtrusionObject path;path.feature=BodyFeature::Helix;
            auto line=std::make_shared<Line<3>>(Vec3(),Vec3(0,0,1));
            path.curve=std::make_shared<TrimmedCurve<3>>(line,0,3);view.extrusions_.append(path);
            ExtrusionObject sweep;sweep.feature=BodyFeature::Sweep;sweep.sketchIndex=0;sweep.sweepPath=1;sweep.firstBody=0;
            view.requestPreview(sweep);settle(view);const auto first=view.preview_.geometry;
            require(std::abs(massProperties(*first).volume-12)<1e-7,"sweep iniziale sul percorso operando");
            view.extrusions_[0].curve=std::make_shared<TrimmedCurve<3>>(line,0,7);
            view.requestPreview(sweep);settle(view);
            require(view.preview_.geometry!=first && std::abs(massProperties(*view.preview_.geometry).volume-28)<1e-7,
                    "modifica dell'operando invalida la cache e cambia la geometria");
        }
        // Una feature nascosta puo' avere solo il B-rep: aprirne l'editor
        // richiede una mesh dell'anteprima, anche senza cambiare parametri.
        for (bool revolution : {false,true}) {
            DocumentState definition;SketchObject profile;
            profile.segments={{{2,0},{4,0}},{{4,0},{4,2}},{{4,2},{2,2}},{{2,2},{2,0}}};
            if(revolution)profile.plane=1;
            definition.sketches.append(profile);
            ExtrusionObject feature;feature.sketchIndex=0;feature.distance=5;
            if(revolution){feature.feature=BodyFeature::Revolution;feature.revolveAxis=-2;feature.revolveAngle=90;}
            definition.extrusions.append(feature);
            ExtrusionObject next;next.feature=BodyFeature::Scale;next.firstBody=0;next.scaleFactor=1.1;
            definition.extrusions.append(next);normalizeModelHistory(definition);
            CadViewport view;view.tessellationQuality_=1;view.loadDocument(definition);
            require(!view.isBodyDrawn(0) && view.extrusions_[0].forgeBody,"stadio da modificare nascosto ma geometria pronta");
            const auto geometry=view.extrusions_[0].forgeBody;
            BodyDisplay expected;tessellateGeometry(view.extrusions_[0],1,expected);
            for(int cachedQuality : {-1,0,1}) {
                view.clearPreview();view.extrusions_[0].display={};
                if(cachedQuality>=0)tessellateGeometry(view.extrusions_[0],cachedQuality,view.extrusions_[0].display);
                const auto snapshot=view.documentState();
                view.requestPreview(view.extrusions_[0],0);settle(view);
                const auto &display=view.preview_.display;
                require(view.preview_.geometry==geometry,"anteprima stadio nascosto riusa il B-rep senza ricostruirlo");
                require(display.quality==1 && !display.vertices.isEmpty() && !display.constructionCurves.isEmpty(),
                        "anteprima riusata contiene superficie e U/V alla qualita' richiesta");
                require(display.vertices==expected.vertices && display.normals==expected.normals
                        && display.triangleFaces==expected.triangleFaces && display.edges==expected.edges,
                        "anteprima riusata concorde con tassellazione esplicita");
                require(view.extrusions_[0].display.quality==cachedQuality
                        && snapshot.extrusions[0].display.quality==cachedQuality,
                        "anteprima non materializza o modifica cache e snapshot dello stadio nascosto");
                if(cachedQuality==1)require(display.vertices.constData()==snapshot.extrusions[0].display.vertices.constData(),
                                           "anteprima conserva il riuso del buffer gia' valido");
            }
        }
        if(!qEnvironmentVariable("FORGECAD_PREVIEW_QA_FILE").isEmpty()) {
            const QString path=qEnvironmentVariable("FORGECAD_PREVIEW_QA_FILE");QFile file(path);
            require(file.open(QIODevice::ReadOnly),"lettura modello QA anteprima");const auto original=file.readAll();file.close();
            DocumentState state;require(ForgeCad::decodeDocument(original,state,true).isEmpty(),"modello QA letto");
            CadViewport view;view.loadDocument(state);require(!view.extrusions().isEmpty(),"funzione del modello QA disponibile");
            view.requestPreview(view.extrusions().first(),0);settle(view);
            require(!view.preview_.display.constructionCurves.isEmpty(),"U/V sul modello reale");
            require(!view.preview_.display.vertices.isEmpty() && view.preview_.display.quality==view.tessellationQuality_,
                    "superficie del modello reale pronta nell'anteprima riusata");
            view.resize(1000,750);view.setViewPreset(4);view.fitView(view.preview_.display.vertices);view.show();QApplication::processEvents();
            require(view.grabFramebuffer().save(qEnvironmentVariable("FORGECAD_PREVIEW_QA_DIR")+"/preview-real.png"),"acquisizione modello reale");view.hide();
            require(file.open(QIODevice::ReadOnly)&&file.readAll()==original,"modello QA originale invariato");
        }
        for (bool revolution : {false,true}) for (bool merge : {false,true}) {
            CadViewport view;
            PrimitiveParameters box;box.size[0]=3;box.size[1]=3;box.size[2]=1;
            require(view.createPrimitive(box,QStringLiteral("Base")).isEmpty(),"base conferma da schizzo");
            const int sketch=view.createSketch(0,QStringLiteral("Profilo"));
            view.sketches_[sketch].segments={{{2,0},{4,0}},{{4,0},{4,2}},{{4,2},{2,2}},{{2,2},{2,0}}};
            view.endSketchMode();box.origin[0]=20;
            require(view.createPrimitive(box,QStringLiteral("Futuro indipendente")).isEmpty(),"feature futura per filtro storico");
            view.selectSketch(sketch);
            ExtrusionObject definition;definition.sketchIndex=sketch;definition.distance=5;
            if(revolution){definition.feature=BodyFeature::Revolution;definition.revolveAxis=-2;definition.revolveAngle=-90;}
            if(merge){definition.mergeOperation=1;definition.mergeAuto=false;definition.mergeBodies={0};}
            {
                const SketchViewUnlock unlock(&view);
                view.requestPreview(definition);settle(view);
                const QString error=revolution?view.createBody(definition):view.createExtrusion(definition,QStringLiteral("Estrusione confermata"));
                require(error.isEmpty(),"conferma della feature da schizzo riuscita");
                view.clearPreview();
            }
            const int result=view.extrusions_.size()-1;
            require(view.sketchMode_ && !view.sketchViewUnlocked_ && view.preview_.key.isEmpty(),"OK torna allo schizzo senza anteprima");
            require(view.isBodyDrawn(result),"il solido confermato resta visibile nello schizzo aperto");
            require(!view.isBodyDrawn(1),"la conferma non mostra lavorazioni future estranee");
            require(view.isBodyDrawn(0)!=merge,"la fusione sostituisce la base senza sovrapporre gli stadi");
            require(!view.referenceBodyEligible(result),"il risultato non diventa un riferimento futuro dello schizzo");
            view.undo();require(view.extrusions_.size()==2,"Undo rimuove il risultato confermato");
            view.redo();require(view.isBodyDrawn(result),"Redo ripristina il risultato nella stessa sessione schizzo");
            view.setHistoryPosition(0);require(!view.isBodyDrawn(result),"rollback esclude anche i risultati della sessione");
            view.setHistoryPosition(view.extrusions_.size());require(view.isBodyDrawn(result),"ritorno al cursore ripristina il risultato della sessione");
            const int model=view.modelBodyIndexForFeature(result);
            view.setModelBodyVisible(model,false);require(!view.isBodyDrawn(result),"risultato confermato rispetta il corpo nascosto dall'utente");
            view.setModelBodyVisible(model,true);require(view.isBodyDrawn(result),"corpo confermato nuovamente visibile");
            if(gl) {
                view.resize(900,700);view.setViewPreset(4);view.fitView(view.extrusions_.at(result).display.vertices);
                view.show();QApplication::processEvents();const auto visible=view.grabFramebuffer();
                require(!visible.isNull(),"framebuffer dopo OK disponibile");
                view.sketchDisplayBodies_[result]=false;const auto hidden=view.grabFramebuffer();
                view.sketchDisplayBodies_[result]=true;
                int changed=0;
                for(int y=0;y<visible.height();++y)for(int x=0;x<visible.width();++x)
                    if(visible.pixel(x,y)!=hidden.pixel(x,y))++changed;
                require(changed>visible.width()*visible.height()/100,"solido dopo OK effettivamente disegnato in OpenGL");
                std::cout<<"confirmed revolution="<<revolution<<" merge="<<merge<<" pixels="<<changed<<std::endl;
                const QString directory=qEnvironmentVariable("FORGECAD_PREVIEW_QA_DIR");
                if(!directory.isEmpty())require(visible.save(directory+QString("/confirmed-%1-%2.png").arg(revolution).arg(merge)),"cattura solido dopo OK");
                view.hide();
            }
            view.endSketchMode();view.selectSketch(sketch);
            require(!view.isBodyDrawn(result) && view.isBodyDrawn(0),"riaprire lo schizzo ripristina il solo contesto storico");
            view.endSketchMode();require(view.isBodyDrawn(result),"uscita schizzo mostra il risultato finale");
        }
        // Ritaglio esatto di una U/V con un foro piu' stretto di un passo grafico.
        std::vector<Vec3> vertices;std::vector<Body::BuildEdge> edges;
        Body::BuildFace face;face.surface=std::make_shared<BSplineSurface>(1,1,
            std::vector<double>{-2,-2,2,2},std::vector<double>{-2,-2,2,2},2,2,
            std::vector<Vec3>{{-2,-2,0},{-2,2,0},{2,-2,0},{2,2,0}});face.sense=true;
        for(const auto &loop:std::vector<std::vector<Vec2>>{{{-2,-2},{2,-2},{2,2},{-2,2}},
                                                          {{-.02,.001},{-.02,.003},{.02,.003},{.02,.001}}}){
            std::vector<Body::BuildFin> fins;const int base=int(vertices.size());
            for(const auto &point:loop)vertices.emplace_back(point.x(),point.y(),0);
            for(int i=0;i<4;++i){const auto a=loop[std::size_t(i)],b=loop[std::size_t((i+1)%4)];
                Body::BuildEdge edge;edge.start=base+i;edge.end=base+(i+1)%4;
                edge.curve=std::make_shared<Line<3>>(Vec3(a.x(),a.y(),0),Vec3(b.x()-a.x(),b.y()-a.y(),0));
                edge.range={0,distance(a,b)};
                fins.push_back({int(edges.size()),true,std::make_shared<Line<2>>(a,b-a),0});edges.push_back(edge);
            }face.loops.push_back(fins);
        }
        const auto sheet=Body::buildSheet(vertices,edges,{face});BodyDisplay display;
        forgeSurfaceConstructionCurves(sheet,display,4,true);
        int centerPieces=0;
        for(const auto &line:display.constructionCurves) {
            if(std::abs(line.first().x())<1e-7 && std::abs(line.last().x())<1e-7)++centerPieces;
            for(int i=1;i<line.size();++i){const auto a=line[i-1],b=line[i];
                require(!(std::abs(a.x())<.02 && std::abs(b.x())<.02
                    && qMin(a.y(),b.y())<.001 && qMax(a.y(),b.y())>.003),
                    "nessun segmento U/V attraversa il foro sottile");
            }
        }
        require(centerPieces==2,"isoparametrica divisa esattamente ai bordi del foro");
        std::cout<<"PASS preview performance and exact UV clipping"<<std::endl;
    }
    static void featureHandles() {
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        CadViewport v;
        v.resize(1000, 750);
        SketchObject sketch;
        sketch.segments = {{{2,2},{8,2}}, {{8,2},{8,8}}, {{8,8},{2,8}}, {{2,8},{2,2}}};
        v.sketches_.append(sketch);
        v.camera_.zoom_ = 40;
        ExtrusionObject definition;
        definition.sketchIndex = 0;
        definition.distance = 5;
        // Il callback reale rientra nel refresh del pannello a ogni movimento.
        std::function<void(double)> changed;
        changed = [&](double value) {
            if (definition.feature == BodyFeature::Revolution) definition.revolveAngle = value;
            else definition.distance = value;
            v.setFeatureHandle(definition, changed);
        };
        QImage image(v.size(), QImage::Format_ARGB32_Premultiplied);
        const auto draw = [&] { image.fill(QColor(25,32,40)); QPainter painter(&image); v.drawFeatureHandle(painter); };
        const auto press = [&](QPointF point) {
            QMouseEvent event(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            v.mousePressEvent(&event);
        };
        const auto move = [&](QPointF point) {
            QMouseEvent event(QEvent::MouseMove, point, point, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            v.mouseMoveEvent(&event);
        };
        const auto release = [&](QPointF point) {
            QMouseEvent event(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            v.mouseReleaseEvent(&event);
        };
        v.setFeatureHandle(definition, changed);
        draw();
        require(v.featureHandlePath_.size() == 2, "freccia estrusione dal centro");
        require((v.featureHandleOrigin_ - ForgeCad::sketchToDisplay(QPointF(5,5), sketch)).length() < 1e-6,
                "freccia centrata sul profilo decentrato");
        require(pointDistance(v.featureHandlePath_.first(), v.projectWorldPoint(v.featureHandleOrigin_)) < 0.01,
                "base freccia sul centro del piano schizzo");
        require(v.featureHandleAt(v.featureHandlePath_.last()), "freccia selezionabile");
        v.interactionLocked_ = true;
        const QPointF tip = v.featureHandlePath_.last();
        const QPointF screen = v.projectWorldPoint(v.featureHandleOrigin_ + 5 * v.featureHandleAxis_) - v.projectWorldPoint(v.featureHandleOrigin_);
        press(tip);
        require(v.featureHandleDragging_, "freccia attiva durante finestra funzione");
        // Anche una normale vista frontalmente deve poter oltrepassare lo zero.
        const QPointF delta = std::hypot(screen.x(),screen.y()) > 12 ? -2 * screen : QPointF(0, 10 * v.height()/v.camera_.zoom_);
        move(tip + delta);
        release(tip + delta);
        require(std::abs(definition.distance + 5) < 1e-4, "estrusione attraversa il piano e inverte il verso");
        draw();
        if (qEnvironmentVariableIsSet("FORGECAD_HANDLE_QA")) {
            v.featureHandleBox_->render(&image, v.featureHandleBox_->pos());
            image.save(QStringLiteral("/tmp/forgecad-feature-handles-extrusion.png"));
        }
        require(v.featureHandleBox_->value() < 0 && !v.featureHandleDragging_, "quota firmata e fine drag");
        v.featureHandleBox_->setValue(7.25);
        require(definition.distance == 7.25, "box aggiorna parametro estrusione");
        definition.extrudeSides = 1;
        v.setFeatureHandle(definition, changed);
        draw();
        require(pointDistance(v.featureHandlePath_.last(),v.projectWorldPoint(v.featureHandleOrigin_ + float(7.25/2) * v.featureHandleAxis_)) < 0.01,
                "simmetrica mostra meta distanza e quota totale");
        for (int plane = 0; plane < 3; ++plane) {
            v.sketches_[0].plane = plane;
            QVector3D origin, axis, radial;
            require(v.featureHandleGeometry(origin,axis,radial), "geometria manipolatore piani standard");
            require((origin - ForgeCad::sketchToDisplay(QPointF(5,5), v.sketches_[0])).length() < 1e-6,
                    "centro profilo sui piani standard");
            const auto expected = ForgeCad::extrusionVector(v.sketches_[0],1);
            require((axis-QVector3D(expected.x(),expected.y(),expected.z())).length() < 1e-6, "direzione coerente con kernel");
        }
        v.sketches_[0].customFrame = true;
        v.sketches_[0].frame.origin[0]=3; v.sketches_[0].frame.origin[1]=4; v.sketches_[0].frame.origin[2]=5;
        v.sketches_[0].frame.normal[0]=0; v.sketches_[0].frame.normal[1]=1; v.sketches_[0].frame.normal[2]=0;
        v.sketches_[0].frame.xAxis[0]=1; v.sketches_[0].frame.xAxis[1]=0; v.sketches_[0].frame.xAxis[2]=0;
        QVector3D origin, axis, radial;
        require(v.featureHandleGeometry(origin,axis,radial) && (origin-ForgeCad::sketchToDisplay(QPointF(5,5), v.sketches_[0])).length()<1e-6 && axis==QVector3D(0,1,0),
                "origine e normale piano personalizzato");
        v.sketches_[0].segments.append({{-100,-200},{300,400}});
        v.sketches_[0].constructionSegments.append(4);
        require(v.featureHandleGeometry(origin,axis,radial)
                && (origin-ForgeCad::sketchToDisplay(QPointF(5,5), v.sketches_[0])).length()<1e-6,
                "geometria di costruzione esclusa dal centro");
        v.sketches_[0] = SketchObject();
        CurveObject curvedProfile;
        curvedProfile.samples = {{20,30},{30,40},{40,30},{30,20},{20,30}};
        v.sketches_[0].curves.append(curvedProfile);
        CurveObject constructionCurve = curvedProfile;
        constructionCurve.construction = true;
        constructionCurve.samples = {{-500,-500},{500,500}};
        v.sketches_[0].curves.append(constructionCurve);
        require(v.featureHandleGeometry(origin,axis,radial)
                && (origin-ForgeCad::sketchToDisplay(QPointF(30,30), v.sketches_[0])).length()<1e-6,
                "centro profilo curvo senza curve di costruzione");
        v.sketches_[0] = sketch;
        definition.feature = BodyFeature::Revolution;
        definition.revolveAxis = -1;
        definition.revolveAngle = 30;
        v.setFeatureHandle(definition, changed);
        draw();
        require(v.featureHandlePath_.size() > 2 && v.featureHandleBox_->suffix().contains(QChar(0x00b0)), "arco con angolo editabile");
        // Il centro dell'arco segue il profilo lungo l'asse, anche su piani diversi.
        for (int plane = 0; plane < 4; ++plane) {
            v.sketches_[0] = sketch;
            if (plane < 3) v.sketches_[0].plane = plane;
            else {
                v.sketches_[0].customFrame = true;
                auto &frame = v.sketches_[0].frame;
                frame.origin[0]=3; frame.origin[1]=4; frame.origin[2]=5;
                frame.normal[0]=0; frame.normal[1]=1; frame.normal[2]=0;
                frame.xAxis[0]=1; frame.xAxis[1]=0; frame.xAxis[2]=0;
            }
            v.sketches_[0].segments.append({{-100,-200},{300,400}});
            v.sketches_[0].constructionSegments.append(4);
            for (int selectedAxis : {-1, -2, 0}) {
                definition.revolveAxis = selectedAxis;
                v.setFeatureHandle(definition, changed);
                draw();
                const auto center = ForgeCad::sketchToDisplay(QPointF(5,5), v.sketches_[0]);
                QPointF point, direction; QString error;
                require(ForgeCad::sketchRevolutionAxis(v.sketches_[0],selectedAxis,point,direction,&error), "asse fixture rivoluzione");
                const auto axisPoint = ForgeCad::sketchToDisplay(point,v.sketches_[0]);
                const auto expected = axisPoint + v.featureHandleAxis_*QVector3D::dotProduct(center-axisPoint,v.featureHandleAxis_);
                require((v.featureHandleOrigin_-expected).length()<1e-5, "centro arco alla proiezione del centro profilo sull'asse");
                const auto offset = center-expected;
                if (offset.lengthSquared()>1e-10f)
                    require((v.featureHandleRadial_-offset.normalized()).length()<1e-5, "arco orientato verso il centro del profilo");
                require(!v.featureHandleBox_->isHidden() && v.featureHandleAt(v.featureHandlePath_.last()), "campo e freccia seguono l'arco decentrato");
            }
        }
        v.sketches_[0] = SketchObject();
        v.sketches_[0].curves = {curvedProfile, constructionCurve};
        definition.revolveAxis = -1;
        v.setFeatureHandle(definition, changed);
        require(v.featureHandleGeometry(origin,axis,radial)
                && (origin-ForgeCad::sketchToDisplay(QPointF(30,0),v.sketches_[0])).length()<1e-5,
                "rivoluzione centrata sul profilo curvo senza costruzioni");
        v.sketches_[0] = sketch;
        v.setFeatureHandle(definition, changed);
        draw();
        const auto angularPoint = [&](double angle) { return v.projectWorldPoint(v.featureHandleOrigin_ + v.featureHandleRadius_*v.featureHandleRadialAt(angle)); };
        press(v.featureHandlePath_.last());
        for (int angle = 25; angle >= -210; angle -= 5) move(angularPoint(angle));
        release(angularPoint(-210));
        require(std::abs(definition.revolveAngle + 210) < 2, "rivoluzione attraversa zero e -180 senza salto");
        draw();
        if (qEnvironmentVariableIsSet("FORGECAD_HANDLE_QA")) {
            v.featureHandleBox_->render(&image, v.featureHandleBox_->pos());
            image.save(QStringLiteral("/tmp/forgecad-feature-handles-revolution.png"));
        }
        require(v.featureHandleBox_->value() < 0, "angolo negativo nel box");
        v.featureHandleBox_->setValue(360);
        require(definition.revolveAngle == 360, "rivoluzione completa dal box");
        draw();
        press(v.featureHandlePath_.last());
        for (int angle = 355; angle >= 330; angle -= 5) move(angularPoint(angle));
        release(angularPoint(330));
        require(std::abs(definition.revolveAngle-330)<2, "drag da rivoluzione completa non salta a zero");
        v.camera_.yaw_=0; v.camera_.pitch_=0; v.camera_.roll_=0;
        draw();
        QVector3D rayOrigin, rayDirection;
        v.viewRay(QPoint(v.width()/2,v.height()/2),rayOrigin,rayDirection);
        require(std::abs(QVector3D::dotProduct(rayDirection,v.featureHandleAxis_))<0.08, "fixture arco visto di taglio");
        const double beforeEdgeOn=definition.revolveAngle;
        const QPointF edgeTip=v.featureHandlePath_.last();
        const QPointF tangent=angularPoint(beforeEdgeOn+1)-angularPoint(beforeEdgeOn);
        press(edgeTip);
        const QPointF edgeDelta=QPointF::dotProduct(tangent,tangent)>0.05 ? 4*tangent : QPointF(0,-4);
        move(edgeTip+edgeDelta); release(edgeTip+edgeDelta);
        require(definition.revolveAngle>beforeEdgeOn+3, "trascinamento angolare anche di taglio");
        v.documentReadOnly_=true;
        require(!v.featureHandleAt(edgeTip), "manipolatore disabilitato in sola lettura");
        v.documentReadOnly_=false;
        definition.feature=BodyFeature::Extrusion;
        v.setFeatureHandle(definition,changed);
        require(v.featureHandleBox_->isLengthMeasurement(), "unita di lunghezza ripristinata dopo angolo");
        definition.feature=BodyFeature::Revolution;
        definition.revolveAxis = 999;
        v.setFeatureHandle(definition, changed);
        draw();
        require(v.featureHandlePath_.isEmpty() && v.featureHandleBox_->isHidden(), "asse invalido nasconde controlli");
        v.clearFeatureHandle();
        require(!v.featureHandleChanged_ && !v.featureHandleDragging_ && v.featureHandleBox_->isHidden(), "pulizia manipolatore");
        // Movimento continuo: devono arrivare risultati prima del rilascio,
        // anche se gli eventi sono piu' frequenti del timer dell'anteprima.
        definition = ExtrusionObject(); definition.sketchIndex = 0; definition.distance = 5;
        int liveFrames = 0;
        v.setPreviewCallback([&](const QString &error) { if (error.isEmpty() && v.preview_.valid) ++liveFrames; });
        changed = [&](double value) {
            definition.distance = value;
            v.setFeatureHandle(definition, changed);
            v.requestPreview(definition);
        };
        v.setFeatureHandle(definition, changed);
        draw();
        press(v.featureHandlePath_.last());
        require(v.featureHandleDragging_, "drag per anteprima continua attivo");
        QElapsedTimer liveTime; liveTime.start();
        qint64 nextMove = 0;
        int immediateFrames = 0;
        QPointF livePosition = v.featureHandlePath_.last();
        while (liveTime.elapsed() < 1500) {
            if (liveTime.elapsed() >= nextMove) {
                livePosition += QPointF(0,-1);
                move(livePosition);
                if (v.previewDragVisible_ && v.previewDragSource_) {
                    const auto axis = v.featureHandleAxis_;
                    const auto base = ForgeCad::sketchToDisplay(QPointF(), sketch);
                    double low = 1e100, high = -1e100;
                    for (const auto &point : v.previewDragDisplay_.vertices) {
                        const double height = QVector3D::dotProduct(point-base,axis);
                        low = qMin(low,height); high = qMax(high,height);
                    }
                    require(std::abs(low-qMin(0.0,definition.distance)) < 1e-4
                            && std::abs(high-qMax(0.0,definition.distance)) < 1e-4,
                            "mesh grafica segue subito l'altezza corrente senza attendere il kernel");
                    require(!v.previewDragDisplay_.constructionCurves.isEmpty(),
                            "curve UV conservate durante il movimento immediato");
                    for (const auto &line : v.previewDragDisplay_.constructionCurves)
                        for (const auto &point : line) {
                            const double height = QVector3D::dotProduct(point-base,axis);
                            require(height >= low-1e-4 && height <= high+1e-4,
                                    "curve UV seguono l'altezza della mesh");
                        }
                    ++immediateFrames;
                }
                nextMove = liveTime.elapsed() + 10;
            }
            QApplication::processEvents();
        }
        require(liveFrames >= 2 && v.featureHandleDragging_, "anteprima aggiornata durante movimento senza rilascio");
        require(immediateFrames > 20, "aggiornamenti grafici immediati tra i risultati esatti");
        release(livePosition);
        QElapsedTimer finalTime; finalTime.start();
        while ((!v.preview_.valid || v.previewRunning_ || v.previewHandlePending_ || v.previewTimer_->isActive())
               && finalTime.elapsed() < 10000) QApplication::processEvents();
        require(v.preview_.valid && !v.previewRunning_
                && std::abs(v.preview_.definition.distance - definition.distance) < 1e-6,
                "anteprima finale allineata all'ultima posizione");
        require(!v.previewDragVisible_, "risultato esatto sostituisce la mesh grafica al termine");
        v.featureHandleDragging_ = true;
        auto reversed = definition;
        reversed.distance = -2 * std::abs(definition.distance);
        v.updateDragPreview(reversed, -1);
        double reversedLow = 1e100, reversedHigh = -1e100;
        const auto planeOrigin = ForgeCad::sketchToDisplay(QPointF(), sketch);
        for (const auto &point : v.previewDragDisplay_.vertices) {
            const double height = QVector3D::dotProduct(point-planeOrigin,v.featureHandleAxis_);
            reversedLow = qMin(reversedLow,height); reversedHigh = qMax(reversedHigh,height);
        }
        require(v.previewDragVisible_ && std::abs(reversedLow-reversed.distance)<1e-4 && std::abs(reversedHigh)<1e-4,
                "mesh immediata invertita ancorata al piano schizzo");
        for (const auto &normal : v.previewDragDisplay_.normals)
            require(std::isfinite(normal.length()) && std::abs(normal.length()-1)<1e-4,
                    "normali della mesh invertita finite e normalizzate");
        v.featureHandleDragging_ = false;
        v.previewDragVisible_ = false;
        if (qEnvironmentVariableIsSet("FORGECAD_HANDLE_GL_QA")) {
            v.camera_.yaw_ = 35; v.camera_.pitch_ = 25; v.camera_.roll_ = 0;
            v.show();
            QApplication::processEvents();
            const auto frame = v.grabFramebuffer();
            require(!frame.isNull() && frame.save(QStringLiteral("/tmp/extrusion-preview-uv.png")),
                    "acquisizione OpenGL anteprima semitrasparente con UV");
            v.hide();
        }
        v.clearPreview(); v.setPreviewCallback({}); v.clearFeatureHandle();
        require(!v.previewDragVisible_ && !v.previewDragSource_, "pulizia cache grafica del drag");
        // Verifica i collegamenti dei veri pannelli e la pulizia su Annulla.
        QMainWindow window;
        auto *viewport = new CadViewport(&window);
        window.setCentralWidget(viewport);
        viewport->sketches_.append(sketch);
        bool synchronized = false, visited = false;
        const auto editPanel = [&](bool angular) {
            QTimer::singleShot(0, viewport, [&, angular] {
                for (auto *dialog : viewport->findChildren<QDialog *>()) {
                    if (dialog->objectName() != QStringLiteral("functionDialogPanel")) continue;
                    visited = true;
                    if (viewport->featureHandleChanged_ && viewport->featureHandleBox_) {
                        viewport->featureHandleBox_->setValue(angular ? -75.5 : -12.5);
                        synchronized = std::abs(viewport->featureHandleValue() - (angular ? -75.5 : -12.5)) < 1e-6;
                        if (angular) {
                            bool reverse = false;
                            for (auto *button : dialog->findChildren<QPushButton *>())
                                if (button->text() == QStringLiteral("Inverti il verso")) reverse = button->isChecked();
                            synchronized = synchronized && reverse;
                        }
                    }
                    dialog->reject();
                    return;
                }
                for (auto *dialog : window.findChildren<QDialog *>()) dialog->reject();
            });
        };
        definition = ExtrusionObject(); definition.sketchIndex=0; definition.distance=5;
        editPanel(false);
        require(!extrusionDialog(&window, viewport, QStringLiteral("Test estrusione"), -1, definition,
                                 [](const ExtrusionObject &) { return QString(); }), "annullamento estrusione");
        require(visited && synchronized && !viewport->featureHandleChanged_, "pannello estrusione sincronizzato e pulito");
        visited = synchronized = false;
        definition.feature=BodyFeature::Revolution; definition.revolveAngle=60;
        editPanel(true);
        require(!revolutionDialog(&window, viewport, QStringLiteral("Test rivoluzione"), -1, definition, false,
                                  [](const ExtrusionObject &) { return QString(); }), "annullamento rivoluzione");
        require(visited && synchronized && !viewport->featureHandleChanged_ && viewport->extrusions_.isEmpty(),
                "pannello angolo/verso sincronizzato, Annulla non cambia documento");
        std::cout << "PASS feature handles" << std::endl;
    }
    static void linkedDrawingUi() {
        using namespace ForgeCad;
        QTemporaryDir temporary;const auto path=temporary.filePath("modello.prt");clientOk(saveDocumentFile(path,clientModel()));
        PdfWindow main;require(main.openDocumentPath(path),"modello aperto per nuova tavola GUI");
        auto *action=main.findChild<QAction *>("newDrawingAction");require(action&&main.findChild<QAction *>("openDrawingAction"),"menu Tavole 2D disponibile");
        action->trigger();DrawingWindow *drawing=nullptr;
        for(auto *child:main.children())if(auto *window=dynamic_cast<DrawingWindow *>(child))drawing=window;
        require(drawing&&drawing->editor()&&drawing->isModified(),"nuova tavola dal comando GUI");
        bool cancelSeen=false;
        QTimer::singleShot(0,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(widget)){box->button(QMessageBox::Cancel)->click();cancelSeen=true;}});
        require(!main.close()&&cancelSeen&&drawing->isVisible(),"chiusura principale rispetta Annulla per tavola non salvata");
        clientOk(drawing->savePath(temporary.filePath("modello.drw")));
        main.close();QElapsedTimer closing;closing.start();
        while(!main.recoveryCloseReady_&&closing.elapsed()<5000)QApplication::processEvents();
        require(main.recoveryCloseReady_,"chiusura asincrona principale dopo salvataggio tavola");QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    static void workspaceRevisionsUi() {
        using namespace ForgeCad;
        QTemporaryDir temporary;require(temporary.isValid(),"directory GUI revisioni");
        const auto path=temporary.filePath("parte.prt");QUuid token;
        {
            PdfWindow window;window.viewport_->loadDocument(clientModel());require(window.saveDocumentPath(path),"salva modello GUI");
            for(const auto &name:{"revisionCheckoutAction","revisionCheckinAction","revisionCompleteAction","revisionCancelAction","revisionHistoryAction"})
                require(window.findChild<QAction *>(QString::fromLatin1(name)),"comando GUI revisioni presente");
            clientOk(window.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"progettista"));
            WorkspaceRevisionStatus status;clientOk(workspaceRevisionStatus(path,window.viewport_->currentDocument(),status));token=status.session.checkout.token;
        }
        PdfWindow reopened;require(reopened.openDocumentPath(path),"riapertura GUI della copia di lavoro");
        bool ownerDialogSeen=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=reopened.findChild<QInputDialog *>();if(!dialog)return;
            dialog->setTextValue(QStringLiteral("progettista"));ownerDialogSeen=true;dialog->accept();
        });
        reopened.findChild<QAction *>(QStringLiteral("revisionCheckoutAction"))->trigger();
        require(ownerDialogSeen,"dialogo proprietario check-out effettivamente utilizzato");
        WorkspaceRevisionStatus status;clientOk(workspaceRevisionStatus(path,reopened.viewport_->currentDocument(),status));
        require(status.session.checkout.token==token,"GUI riprende sessione persistente dopo chiusura");
        auto beforeCheckin=reopened.viewport_->extrusions()[0];beforeCheckin.primitive.size[0]=12;clientOk(reopened.viewport_->updateBody(0,beforeCheckin));
        bool dialogSeen=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=reopened.findChild<QDialog *>(QStringLiteral("revisionCheckinDialog"));
            if(!dialog)return;
            auto *label=dialog->findChild<QLineEdit *>(QStringLiteral("revisionLabel"));
            auto *comment=dialog->findChild<QTextEdit *>(QStringLiteral("revisionComment"));
            if(!label || !comment){dialog->reject();return;}
            require(label->isReadOnly()&&label->text()=="1","revisione proposta automaticamente e non modificabile");comment->setPlainText("verifica interattiva");dialogSeen=true;dialog->accept();
        });
        reopened.findChild<QAction *>(QStringLiteral("revisionCheckinAction"))->trigger();
        require(dialogSeen,"dialogo check-in effettivamente utilizzato");
        auto current=reopened.viewport_->currentDocument();require(!current.baseCheckinId.isNull() && !reopened.documentModified_,"GUI applica ricevuta e salva base");
        require(reopened.viewport_->documentReadOnly()&&reopened.windowTitle().contains("sola lettura"),"modello bloccato immediatamente dopo check-in");
        require(!reopened.findChild<QAction *>("documentSaveAction")->isEnabled()&&!reopened.findChild<QAction *>("documentImportAction")->isEnabled(),"salva/importa disabilitati anche nelle barre");
        const auto protectedBytes=clientRead(path);auto definition=current.extrusions[0];definition.primitive.size[0]=23;
        require(!reopened.viewport_->updateBody(0,definition).isEmpty()&&!reopened.saveDocumentPath(path),"modifica diretta e sovrascrittura rifiutate senza check-out");
        reopened.viewport_->setLengthUnit(LengthUnit::Inch);reopened.viewport_->undo();reopened.viewport_->redo();
        require(reopened.viewport_->lengthUnit()==current.lengthUnit&&!reopened.documentModified_&&clientRead(path)==protectedBytes,"nessuna mutazione con unità/undo/redo in sola lettura");
        reopened.viewport_->zoomIn();reopened.viewport_->setViewPreset(2);
        require(!reopened.viewport_->interactionLocked(),"sola lettura non impedisce navigazione e comandi revisione");
        {PdfWindow copy;const auto copyPath=temporary.filePath("copia.prt");
         require(reopened.saveDocumentPath(copyPath)&&!reopened.viewport_->documentReadOnly()&&reopened.viewport_->documentVersion().documentId!=current.version.documentId,"Salva con nome sblocca solo una nuova identità");
         require(reopened.openDocumentPath(path)&&reopened.viewport_->documentReadOnly(),"riapertura originale torna in sola lettura");
         require(clientRead(path)==protectedBytes,"copia indipendente non riscrive originale");}
        QVector<RevisionRecord> records;clientOk(workspaceDocumentHistory(path,current,records));
        require(records.size()==1 && records[0].technicalRevision=="1" && records[0].comment=="verifica interattiva","metadati del dialogo nel catalogo");
        bool historySeen=false;
        QTimer::singleShot(0,[&] {
            auto *dialog=reopened.findChild<QDialog *>(QStringLiteral("documentRevisionHistoryDialog"));if(!dialog)return;
            auto *table=dialog->findChild<QTableWidget *>(QStringLiteral("documentRevisionHistoryTable"));
            historySeen=table && table->rowCount()==1 && table->item(0,1)->text()=="1" && table->editTriggers()==QAbstractItemView::NoEditTriggers;
            dialog->reject();
        });
        reopened.findChild<QAction *>(QStringLiteral("revisionHistoryAction"))->trigger();require(historySeen,"GUI mostra storico in sola lettura");
        clientOk(reopened.runDocumentRevisionAction(PdfWindow::DocumentRevisionAction::Checkout,"progettista"));
        require(!reopened.viewport_->documentReadOnly(),"nuovo check-out sblocca il modello");
        {CadViewport undoView;auto original=clientModel();undoView.loadDocument(original);auto body=original.extrusions[0];body.primitive.size[0]=16;clientOk(undoView.updateBody(0,body));
         undoView.adoptDocumentIdentity(current.version,current.baseCheckinId);undoView.undo();require(undoView.currentDocument().baseCheckinId==current.baseCheckinId&&undoView.documentVersion().documentId==current.version.documentId,"undo non ripristina una vecchia base di check-in");
         undoView.redo();require(undoView.currentDocument().baseCheckinId==current.baseCheckinId,"redo conserva ricevuta corrente");}
        reopened.viewport_->setLengthUnit(LengthUnit::Inch);
        bool cancelConfirmed=false;
        QTimer::singleShot(0,[&] {
            for(QWidget *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(widget))
                if(auto *button=box->button(QMessageBox::Yes)){cancelConfirmed=true;button->click();}
        });
        reopened.findChild<QAction *>(QStringLiteral("revisionCancelAction"))->trigger();
        require(cancelConfirmed,"pulsante di conferma annullamento effettivamente premuto");
        clientOk(workspaceRevisionStatus(path,reopened.viewport_->currentDocument(),status));
        require(!status.hasSession && reopened.viewport_->currentDocument().lengthUnit==LengthUnit::Inch && reopened.documentModified_,"annullamento GUI conserva modifiche non salvate");
    }

    static ForgeCad::ForgeBody preparedBlendHistoryPreview(CadViewport &view, const QVector<EdgePoint> &edges,
                                                          double size, bool chamfer, const ChamferSpec &spec) {
        view.requestBlendPreview(0, edges, size, chamfer, -1, spec);
        view.startPreviewJob();
        QElapsedTimer timeout; timeout.start();
        while (view.previewRunning_ && timeout.elapsed() < 10000) QApplication::processEvents();
        require(view.preview_.valid && view.preview_.geometry, "anteprima reale pronta per la genealogia");
        // Forza anche il caso in cui il worker abbia gia' calcolato i nomi
        // temporanei: la conferma deve pubblicare quelli dell'ID definitivo.
        require(bool(ForgeCad::elementNamesOf(*view.preview_.geometry)), "nomi provvisori dell'anteprima");
        return view.preview_.geometry;
    }

    static void copyFaces() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const Body box = makeBox(Frame3(), 4, 5, 6);
        const auto faces = box.faces();
        const auto edgesOf = [](const Body &body, FaceId face) {
            std::vector<EdgeId> edges;
            for (LoopId loop : body.face(face).loops)
                for (FinId fin : body.loopFins(loop)) edges.push_back(body.fin(fin).edge);
            return edges;
        };
        const auto firstEdges = edgesOf(box, faces.front());
        FaceId adjacent;
        for (FaceId f : faces) {
            if (f == faces.front()) continue;
            const auto edges = edgesOf(box,f);
            for (EdgeId edge : edges)
                if (std::find(firstEdges.begin(),firstEdges.end(),edge)!=firstEdges.end()) adjacent=f;
            if (adjacent.valid()) break;
        }
        require(adjacent.valid(), "due facce adiacenti per la copia cucita");
        const Body sheet = facesAsSheet(box, {faces.front(), adjacent});
        const auto base = std::make_shared<const Body>(sheet);
        QVector<EdgePoint> points;
        for(FaceId f : sheet.faces()) {
            const auto &face = sheet.face(f);
            const FinId fin=sheet.loop(face.loops.front()).first;
            points.append(faceReference(sheet,f,sheet.finPoint(fin,0.5)));
        }
        QString error;
        for(const auto &selection : {QVector<EdgePoint>{points.front()}, points, QVector<EdgePoint>{}}) {
            const auto copied=forgeOffsetFaces(base,selection,0,&error);
            require(copied && copied.get()!=base.get() && copied->isSheet() && error.isEmpty()
                && checkBody(*copied).empty(), "copia esatta di una faccia o superficie cucita");
            require(copied->faces().size()==(selection.size()==1 ? 1u : 2u), "copia contiene tutte e sole le facce scelte");
            require(copied->shells().size()==1, "facce adiacenti restano cucite");
            double area=0;for(FaceId f:copied->faces())area+=faceArea(*copied,f);
            double expected=0;
            for(int i=0;i<(selection.size()==1?1:points.size());++i)expected+=faceArea(sheet,sheet.faces()[i]);
            require(std::abs(area-expected)<1e-9, "copia conserva area e trim delle facce");
        }
        const auto solid=std::make_shared<const Body>(box);
        const auto surfaceCopy=forgeOffsetFaces(solid,{},0,&error);
        require(surfaceCopy && surfaceCopy->isSheet() && surfaceCopy->faces().size()==6 && checkBody(*surfaceCopy).empty(),
            "copia delle facce di un solido crea una superficie");
        const auto sphere=std::make_shared<const Body>(makeSphere(Frame3(),2));
        const auto sphereCopy=forgeOffsetFaces(sphere,{},0,&error);
        require(sphereCopy && sphereCopy->isSheet() && checkBody(*sphereCopy).empty()
            && std::abs(faceArea(*sphereCopy,sphereCopy->faces().front())-16*M_PI)<1e-8,
            "copia di superficie periodica senza bordo conserva geometria");
        const auto offset=forgeOffsetFaces(base,{points.front()},1,&error);
        require(offset && checkBody(*offset).empty(), "offset non nullo continua a funzionare");

        ExchangeBody exchange;exchange.name="Superficie cucita";exchange.body=sheet;
        ExtrusionObject imported;imported.feature=BodyFeature::Imported;
        imported.importData=QByteArray::fromStdString(writeStep({exchange}));
        imported.name=QStringLiteral("Superficie originale");
        DocumentState source;source.extrusions={imported};
        CadViewport view;view.loadDocument(source);
        const auto original=view.extrusions_[0].forgeBody;
        ExtrusionObject definition;definition.feature=BodyFeature::SurfaceOffset;definition.firstBody=0;definition.distance=0;
        QMainWindow window;std::exception_ptr failure;
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=window.findChild<QDialog *>();
            try {
                require(dialog && dialog->findChild<QDoubleSpinBox *>("offsetDistance")->value()==0,
                    "pannello copia facce parte a distanza zero");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                require(dialog->result()==QDialog::Accepted, "copia accettata nel pannello");
            } catch(...) {failure=std::current_exception();if(dialog)dialog->reject();}
        });
        require(offsetDialog(&window,&view,QStringLiteral("Copia facce"),-1,definition,
            [&](const ExtrusionObject &d){return view.createBody(d);}), "comando copia facce riuscito");
        if(failure)std::rethrow_exception(failure);
        require(view.extrusions_.size()==2 && view.extrusions_[0].forgeBody==original && view.extrusions_[0].visible
            && view.extrusions_[1].visible && view.extrusions_[1].forgeBody->isSheet(), "copia e originale restano disponibili");
        require(view.extrusions_[0].modelBodyId!=view.extrusions_[1].modelBodyId && view.resultBodiesBefore(-1)==QVector<int>({0,1}),
            "copia e originale sono corpi logici distinti");
        auto edited=view.extrusions_[1];edited.distance=1;
        require(view.updateBody(1,edited).isEmpty(), "copia modificabile come offset");
        view.undo();require(view.extrusions_[1].distance==0 && view.extrusions_[0].visible, "annulla offset ripristina copia");
        QTemporaryDir directory;const auto file=directory.filePath(QStringLiteral("copy-faces.prt"));
        DocumentState document;document.extrusions=view.extrusions_;
        require(saveDocumentFile(file,document).isEmpty(), "salvataggio copia facce");
        DocumentState loaded;require(loadDocumentFile(file,loaded).isEmpty(), "riapertura copia facce");
        for(int i=0;i<loaded.extrusions.size();++i) {
            loaded.extrusions[i].cachedGeometry=false;
            buildBody(loaded.extrusions[i],i,loaded.sketches,loaded.extrusions,0);
            require(loaded.extrusions[i].error.isEmpty() && loaded.extrusions[i].forgeBody, "rigenerazione originale e copia");
        }
        require(loaded.extrusions[1].distance==0 && loaded.extrusions[1].forgeBody->shells().size()==1
            && loaded.extrusions[1].forgeBody->faces().size()==2 && loaded.extrusions[0].visible,
            "riapertura conserva copia cucita e originale");
        std::cout << "Copy faces PASS\n";
    }

    static void referenceListHighlight() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        DocumentState source;
        ExtrusionObject box; box.feature = BodyFeature::Primitive; box.name = QStringLiteral("Base");
        box.primitive.size[0] = box.primitive.size[1] = box.primitive.size[2] = 10;
        source.extrusions = {box};
        CadViewport view; view.loadDocument(source); view.resize(800,600);
        const auto geometry = view.extrusions_[0].forgeBody;
        QVector<GeometryRef> refs;
        for (EdgeId edge : geometry->edges()) {
            GeometryRef ref; ref.kind = 4; ref.index = 0;
            const auto &e = geometry->edge(edge);
            ref.point = edgeReference(*geometry, edge, e.curve->point((e.range.lo + e.range.hi) / 2));
            refs.append(ref);
        }
        const auto focused = [&](const GeometryRef &ref) {
            require(view.referenceListMarks_.size() == 1 && view.referenceListMarks_[0].kind == ref.kind
                && view.referenceListMarks_[0].index == ref.index && view.referenceListMarks_[0].point.subshape == ref.point.subshape,
                "la voce scelta evidenzia esattamente il bordo corrispondente");
            FaceHit hit; hit.edges = {ref.point};
            const auto displayEdges = view.faceDisplayEdges(ref.index, hit);
            require(displayEdges.size() == 1 && view.extrusions_[ref.index].display.edgeIds[displayEdges[0]] == ref.point.subshape,
                "evidenziazione risolta sul bordo della mesh");
            QImage image(800,600,QImage::Format_ARGB32); image.fill(Qt::transparent);
            { QPainter painter(&image); view.drawGeometryRef(painter, view.referenceListMarks_[0], kHoverColor); }
            int pixels = 0;
            for (int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
                if (image.pixelColor(x,y).alpha()) ++pixels;
            require(pixels > 10, "bordo selezionato realmente disegnato nell'overlay");
        };
        // Navigazione della lista, cambio elenco, aggiornamento e rimozione.
        {
            QDialog dialog;
            auto *first = new GeometryReferenceList(dialog, &view, QStringLiteral("first"));
            auto *second = new GeometryReferenceList(dialog, &view, QStringLiteral("second"));
            first->setReferences(refs); second->setReferences({refs[3]});
            first->setCurrentRow(1); focused(refs[1]);
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QApplication::sendEvent(first, &down); focused(refs[2]);
            const auto shape = view.extrusions_[0].forgeBody;
            second->setCurrentRow(0); focused(refs[3]);
            first->setReferences(refs); focused(refs[3]);
            first->itemClicked(first->item(2)); focused(refs[2]);
            first->setReferences({refs[2], refs[1]}); focused(refs[2]);
            require(first->currentRow() == 0, "selezione segue lo stesso bordo dopo riordino");
            first->setReferences({refs[1]}); require(view.referenceListMarks_.isEmpty(), "rimozione bordo azzera evidenziazione");
            first->setCurrentRow(0); first->setEnabled(false);
            require(view.referenceListMarks_.isEmpty(), "lista disabilitata non lascia bordi evidenziati");
            second->itemClicked(second->item(0)); focused(refs[3]);
            require(view.extrusions_[0].forgeBody == shape, "selezione della lista non modifica il modello");
            dialog.reject(); require(view.referenceListMarks_.isEmpty(), "chiusura pannello pulisce evidenziazione");
        }
        {
            QDialog dialog;
            auto *list = new GeometryReferenceList(dialog, &view, QStringLiteral("destroyed"));
            list->setReferences(refs); list->setCurrentRow(0); focused(refs[0]);
            delete list; require(view.referenceListMarks_.isEmpty(), "distruzione elenco pulisce evidenziazione");
        }
        // Verifica i pannelli reali che prima mostravano solo testo.
        for (int kind=0;kind<4;++kind) {
            QMainWindow window; std::exception_ptr failure;
            ExtrusionObject initial;
            initial.planarRefs = refs.mid(0,4); initial.ruledFirst = {refs[0]}; initial.ruledSecond = {refs[1]};
            QTimer::singleShot(0,&window,[&] {
                auto *dialog = window.findChild<QDialog *>();
                try {
                    const QString name = kind==0 ? "boundaryEdges" : kind==1 ? "planarEdges" : kind==2 ? "ruledFirstEdges" : "fillBoundaryEdges";
                    auto *list = dialog ? dialog->findChild<QListWidget *>(name) : nullptr;
                    require(list && list->count() == (kind==2 ? 1 : 4), "pannello mostra tutti i bordi selezionabili");
                    list->setCurrentRow(0); focused(refs[0]);
                    if(kind==2) {
                        auto *other = dialog->findChild<QListWidget *>("ruledSecondEdges");
                        other->setCurrentRow(0); focused(refs[1]);
                    }
                    dialog->reject();
                } catch(...) { failure=std::current_exception(); if(dialog) dialog->reject(); }
            });
            const auto apply=[](const ExtrusionObject &){return QString();};
            if(kind==0) boundaryDialog(&window,&view,"Bordi",-1,initial,apply);
            else if(kind==1) planarDialog(&window,&view,"Bordi",-1,initial,apply);
            else if(kind==2) ruledDialog(&window,&view,"Bordi",-1,initial,apply);
            else fillDialog(&window,&view,"Bordi",-1,initial,apply);
            if(failure) std::rethrow_exception(failure);
            require(view.referenceListMarks_.isEmpty(), "chiusura funzione rimuove il bordo evidenziato");
        }
        std::cout << "Reference list highlight PASS\n";
    }

    static void surfacePatterns() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        ExtrusionObject base;
        base.feature = BodyFeature::Imported;
        base.modelBodyId = 1;
        base.name = QStringLiteral("Superficie");
        base.forgeBody = std::make_shared<const Body>(makePlaneSheet(Frame3({3,0,0}, {0,0,1}, {1,0,0}), 1));
        ExchangeBody exchange; exchange.name = "Superficie"; exchange.body = *base.forgeBody;
        base.importData = QByteArray::fromStdString(writeStep({exchange}));
        base.solid = false;
        QVector<ExtrusionObject> bodies{base};
        const auto check = [&](const ExtrusionObject &body, int count) {
            require(body.error.isEmpty() && body.forgeBody && !body.solid && body.forgeBody->isSheet(), "ripetizione resta superficie valida");
            require(body.forgeBody->faces().size() == base.forgeBody->faces().size() * count, "tutte le facce delle copie conservate");
            require(body.forgeBody->shells().size() == base.forgeBody->shells().size() * count, "copie restano shell separate");
            require(checkBody(*body.forgeBody).empty(), "topologia e geometria delle copie valide");
            double area = 0;
            for (auto face : body.forgeBody->faces()) area += faceArea(*body.forgeBody, face);
            require(std::abs(area - 4.0 * count) < 1e-8, "area delle superfici conservata");
        };
        ExtrusionObject pattern;
        pattern.feature = BodyFeature::Pattern;
        pattern.firstBody = 0;
        pattern.modelBodyId = 1;
        pattern.pattern.count = 4;
        GeometryRef ref; ref.kind = 2; ref.index = 0;
        pattern.pattern.refs = {ref};
        for (double spacing : {5.0, 2.0, 0.5}) {
            pattern.pattern.spacing = spacing; // separate, a contatto, sovrapposte
            buildBody(pattern, 1, {}, bodies, 0);
            check(pattern, 4);
            require(!pattern.display.vertices.isEmpty(), "ripetizione superficie tassellata");
        }
        GeometryRef y; y.kind = 2; y.index = 1;
        pattern.pattern.count2 = 3;
        pattern.pattern.spacing2 = 3;
        pattern.pattern.refs.append(y);
        buildBody(pattern, 1, {}, bodies, 0); check(pattern, 12);
        pattern.pattern.count2 = 1;
        pattern.pattern.refs.resize(1);
        pattern.pattern.kind = 1;
        pattern.pattern.refs[0].index = 2;
        pattern.pattern.angle = 360;
        pattern.pattern.spread = true;
        buildBody(pattern, 1, {}, bodies, 0); check(pattern, 4);
        pattern.pattern.kind = 2;
        pattern.pattern.refs[0].kind = 1; // piano YZ
        for (bool keep : {false, true}) {
            pattern.pattern.keepOriginal = keep;
            buildBody(pattern, 1, {}, bodies, 0); check(pattern, keep ? 2 : 1);
            bool reflected = false;
            for (auto vertex : pattern.forgeBody->vertices())
                reflected = reflected || pattern.forgeBody->vertex(vertex).point.x() < -1;
            require(reflected, "specchio trasforma la superficie rispetto al piano YZ");
        }
        // Originale sul piano: copie coincidenti non cucite ne' eliminate.
        pattern.pattern.refs[0].index = 0; // piano XY
        buildBody(pattern, 1, {}, bodies, 0); check(pattern, 2);
        // Anche superfici periodiche senza bordo e con piu' facce.
        for (const Body &solid : {makeSphere(Frame3(), 2), makeBox(Frame3(), 2, 3, 4)}) {
            const auto sheet = std::make_shared<const Body>(facesAsSheet(solid, solid.faces()));
            QString error;
            const auto copies = forgePattern(sheet, {Transform3::translation({6,0,0})}, true, &error);
            require(copies && error.isEmpty() && copies->isSheet() && checkBody(*copies).empty(), "ripetizione superfici curve e multifaccia");
            require(copies->faces().size() == sheet->faces().size() * 2, "facce curve conservate");
        }
        // Il comportamento dei solidi rimane una vera unione, anche a contatto.
        QString error;
        const auto box = std::make_shared<const Body>(makeBox(Frame3(), 2, 3, 4));
        const auto united = forgePattern(box, {Transform3::translation({2,0,0})}, true, &error);
        require(united && !united->isSheet() && checkBody(*united).empty(), "ripetizione dei solidi valida");
        require(std::abs(massProperties(*united).volume - 48) < 1e-8, "volume unione dei solidi conservato");
        // I comandi esistenti accettano superfici e restano modificabili nella storia.
        for (int kind = 0; kind < 3; ++kind) {
            CadViewport view;
            DocumentState source; source.extrusions = bodies;
            view.loadDocument(source);
            require(view.resultBodiesBefore(-1) == QVector<int>{0}, "superficie disponibile nel selettore corpi");
            ExtrusionObject initial;
            initial.feature = BodyFeature::Pattern; initial.firstBody = 0; initial.pattern.kind = kind;
            initial.pattern.count = 4;
            QMainWindow window;
            std::exception_ptr failure;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<QDialog *>();
                try {
                    require(dialog, "pannello ripetizione superficie aperto");
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    require(dialog->result() == QDialog::Accepted, "pannello accetta la superficie");
                } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
            });
            patternDialog(&window, &view, QStringLiteral("Ripetizione superficie"), -1, initial,
                [&](const ExtrusionObject &d) { return view.createBody(d, {d.firstBody}); });
            if (failure) std::rethrow_exception(failure);
            require(view.extrusions_.size() == 2, "comando crea ripetizione superficie");
            check(view.extrusions_.last(), kind == 2 ? 2 : 4);
            ExtrusionObject edited = view.extrusions_.last();
            if (kind == 2) edited.pattern.keepOriginal = false;
            else edited.pattern.count = 3;
            require(view.updateBody(1, edited).isEmpty(), "modifica ripetizione superficie");
            check(view.extrusions_.last(), kind == 2 ? 1 : 3);
            view.undo(); check(view.extrusions_.last(), kind == 2 ? 2 : 4);
        }
        // Salvataggio e rigenerazione mantengono tipo e copie.
        DocumentState document; document.extrusions = bodies; document.extrusions.append(pattern);
        QTemporaryDir directory; const auto file = directory.filePath(QStringLiteral("surface-pattern.prt"));
        require(saveDocumentFile(file, document).isEmpty(), "salva ripetizione superficie");
        DocumentState loaded; require(loadDocumentFile(file, loaded).isEmpty(), "riapre ripetizione superficie");
        loaded.extrusions[1].cachedGeometry = false;
        buildBody(loaded.extrusions[1], 1, loaded.sketches, loaded.extrusions, 0);
        check(loaded.extrusions[1], 2);
        std::cout << "Surface patterns PASS\n";
    }

    static void sweepReferences() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        SketchObject circle;
        circle.name = QStringLiteral("Sezione");
        CurveObject ring; ring.tool = DrawingTool::Circle; ring.controlPoints = {QPointF(0, 0), QPointF(1, 0)};
        circle.curves = {ring};
        QVector<SketchObject> sketches{circle};
        GeometryRef source; source.kind = 7; source.index = 0; source.element = {1, 0, -1};
        ExtrusionObject extracted; extracted.feature = BodyFeature::ExtractedCurve; extracted.name = QStringLiteral("Cerchio estratto");
        extracted.curveExtraction.source = source;
        buildGeometry(extracted, 0, sketches, {});
        require(extracted.curve && extracted.error.isEmpty(), "sezione circolare estratta");
        ExtrusionObject spatial; spatial.feature = BodyFeature::SpatialSketch; spatial.name = QStringLiteral("Percorsi 3D");
        spatial.spatialSketch.nextId = 3;
        SpatialCurve3D wrong; wrong.id = 1; wrong.points = {{20,0,0}, {30,0,0}};
        SpatialCurve3D right; right.id = 2; right.points = {{0,0,0}, {0,0,8}};
        spatial.spatialSketch.entities = {wrong, right};
        buildGeometry(spatial, 1, sketches, {extracted});
        require(spatial.curves.size() == 2, "schizzo 3D con due entita'");
        QVector<ExtrusionObject> bodies{extracted, spatial};
        GeometryRef profile; profile.kind = 9; profile.index = 0;
        GeometryRef path; path.kind = 9; path.index = 1; path.point.subshape = 2; path.point.geometry = kSpatialCurveReference; path.point.x = 20;
        ExtrusionObject sweep; sweep.feature = BodyFeature::Sweep; sweep.sketchIndex = -1;
        sweep.sweepProfileRef = profile; sweep.sweepPathRef = path;
        buildGeometry(sweep, 2, sketches, bodies);
        require(sweep.forgeBody && sweep.solid && sweep.error.isEmpty(), "sweep: curva estratta e seconda entita' dello schizzo 3D");
        require(std::abs(massProperties(*sweep.forgeBody).volume - 8 * M_PI) < 1e-7, "volume sweep lungo entita' 3D scelta");
        // Anche un bordo di faccia mantiene la sua geometria e il suo intervallo.
        ExtrusionObject box; box.feature = BodyFeature::Primitive; box.primitive.size[0] = 5; box.primitive.size[1] = 5; box.primitive.size[2] = 8;
        buildGeometry(box, 2, sketches, bodies); bodies.append(box);
        GeometryRef edge; edge.kind = 4; edge.index = 2;
        for (auto id : box.forgeBody->edges()) {
            const auto &e = box.forgeBody->edge(id);
            if (std::abs(normalized(e.curve->derivative((e.range.lo + e.range.hi) / 2)).z()) > 0.99) {
                edge.point = edgeReference(*box.forgeBody, id, e.curve->point((e.range.lo + e.range.hi) / 2)); break;
            }
        }
        sweep.sweepPathRef = edge; buildGeometry(sweep, 3, sketches, bodies);
        require(sweep.forgeBody && sweep.error.isEmpty() && std::abs(massProperties(*sweep.forgeBody).volume - 8 * M_PI) < 1e-7,
                "sweep lungo bordo di faccia");
        // Sezione non piana rifiutata, senza proiettarla silenziosamente.
        auto nonplanar = std::make_shared<BSplineCurve<3>>(1, std::vector<double>{0,0,1,2,3,3},
            std::vector<Vec3>{{0,0,0},{1,0,0},{1,1,0},{0,1,1}});
        QString error;
        require(!forgeSweep(curvePath(nonplanar), curvePath(spatial.curves[1]), 0, &error) && error.contains(QStringLiteral("piana")),
                "sezione non piana rifiutata");
        sweep.sweepPathRef = path; buildGeometry(sweep, 3, sketches, bodies); bodies.append(sweep);
        QTemporaryDir folder; DocumentState document; document.sketches = sketches; document.extrusions = bodies;
        const QString file = folder.filePath(QStringLiteral("sweep.prt"));
        require(saveDocumentFile(file, document).isEmpty(), "salva sweep con riferimenti");
        DocumentState loaded; require(loadDocumentFile(file, loaded).isEmpty(), "riapre sweep con riferimenti");
        for (int i = 0; i < 3; ++i) buildGeometry(loaded.extrusions[i], i, loaded.sketches, loaded.extrusions);
        auto restored = loaded.extrusions.back(); restored.cachedGeometry = false;
        require(restored.sweepProfileRef.kind == 9 && restored.sweepPathRef.point.subshape == 2, "riferimenti sweep conservati");
        buildGeometry(restored, 3, loaded.sketches, loaded.extrusions);
        require(restored.forgeBody && restored.error.isEmpty(), "sweep riaperta rigenera");
        // Il grab e' attivo subito, passa al percorso, poi termina e libera i callback.
        CadViewport view; view.sketches_ = sketches; view.extrusions_ = bodies.mid(0, 3);
        QMainWindow window; std::exception_ptr failure; ExtrusionObject submitted;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("sweepDialog");
            try {
                require(dialog && view.referencePicking(), "sweep: grab attivo all'apertura");
                require(dialog->findChild<QComboBox *>("sweepProfileSource")->currentIndex() == 1
                    && dialog->findChild<QComboBox *>("sweepPathSource")->currentIndex() == 2, "selettori predefiniti dalla vista");
                auto callback = view.refPickFinished_; callback(true, profile);
                require(dialog->findChild<QPushButton *>("sweepPickPath")->isChecked(), "passaggio automatico al percorso");
                view.cancelReferencePick(); callback = view.refPickFinished_; callback(true, path);
                require(!dialog->findChild<QPushButton *>("sweepPickPath")->isChecked(), "scelta percorso completata");
                dialog->grab().save(QStringLiteral("/tmp/sweep-panel.png"));
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
        });
        ExtrusionObject initial; initial.feature = BodyFeature::Sweep;
        sweepDialog(&window, &view, QStringLiteral("Sweep"), -1, initial, [&](const ExtrusionObject &d) {submitted = d; return QString();});
        if (failure) std::rethrow_exception(failure);
        require(submitted.sweepProfileRef.kind == 9 && submitted.sweepPathRef.point.subshape == 2, "pannello restituisce i riferimenti scelti");
        require(!view.referencePicking() && !view.refPickFinished_, "sweep libera i callback alla chiusura");
        std::cout << "Sweep references PASS\n";
    }

    static void documentAccess() {
        using namespace ForgeCad;
        QTemporaryDir directory;
        require(directory.isValid(), "document lock test directory");
        const QString a=directory.filePath("a.prt"), b=directory.filePath("b.prt");
        DocumentState empty;
        require(saveDocumentFile(a,empty,false).isEmpty(), "create document with temporary lock");
        require(!QFileInfo::exists(a+".lock"), "temporary writer releases lock");
        {
            DocumentFileLock owner(a), other(a);
            require(owner.acquire().isEmpty() && owner.owns(a), "exclusive document lock");
            require(!other.acquire().isEmpty(), "second session rejected");
            require(!saveDocumentFile(a,empty,false).isEmpty(), "unowned writer rejected");
            require(saveDocumentFile(a,empty,false,&owner).isEmpty() && owner.owns(a), "atomic save retains lock");
            const QString alias=directory.filePath("alias.prt");
            if(QFile::link(a,alias) && QFileInfo(alias).isSymLink()) {
                DocumentFileLock aliased(alias);
                require(!aliased.acquire().isEmpty(), "symbolic alias cannot bypass lock");
                require(saveDocumentFile(alias,empty,false,&owner).isEmpty() && QFileInfo(alias).isSymLink(),
                        "save through alias preserves symbolic link");
            }
        }
        require(!QFileInfo::exists(a+".lock"), "document lifetime releases lock");
        {
            // Simula il marker di un altro computer, vecchio di un giorno:
            // non deve essere sottratto anche senza un lock nativo del server.
            QFile marker(b+".lock"); require(marker.open(QIODevice::WriteOnly), "remote lock fixture");
            marker.write("12345\nForgeCAD\nremote-forgecad-test.invalid\nremote-machine\nremote-boot\n");
            require(marker.flush() && marker.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1),QFileDevice::FileModificationTime),
                    "remote marker older than default timeout"); marker.close();
            DocumentFileLock remote(b); require(!remote.acquire().isEmpty(), "remote lock never expires by age");
            require(QFileInfo::exists(b+".lock"), "remote marker preserved");
            require(QFile::remove(b+".lock"), "remove test remote marker");
        }
        {
            QProcess worker;
            worker.start(QCoreApplication::applicationFilePath(), {"--document-lock-worker",a});
            require(worker.waitForStarted(5000) && worker.waitForReadyRead(5000)
                    && worker.readAllStandardOutput().contains("READY"), "independent process holds lock");
            DocumentFileLock competing(a);
            require(!competing.acquire().isEmpty(), "cross process exclusion");
            QProcess writer;
            writer.start(QCoreApplication::applicationFilePath(), {"--document-lock-worker",a,"--save"});
            require(writer.waitForFinished(5000) && writer.exitCode()==2, "independent writer cannot overwrite open file");
            worker.write("\n"); require(worker.waitForFinished(5000) && worker.exitCode()==0, "worker releases normally");
            require(competing.acquire().isEmpty(), "lock available after independent process closes");
        }
        {
            QProcess worker;
            worker.start(QCoreApplication::applicationFilePath(), {"--document-lock-worker",a});
            require(worker.waitForStarted(5000) && worker.waitForReadyRead(5000)
                    && worker.readAllStandardOutput().contains("READY"), "crash worker holds lock");
            worker.kill(); require(worker.waitForFinished(5000), "simulated local process crash");
            DocumentFileLock recovered(a); require(recovered.acquire().isEmpty(), "dead local owner can be recovered");
        }
        QSettings().remove("document/recentFiles");
        PdfWindow first, second;
        auto *menu=first.findChild<QMenu *>("recentDocumentsMenu");
        require(menu && menu->actions().size()==1 && !menu->actions().first()->isEnabled(), "empty recent menu");
        require(first.openDocumentPath(a), "window opens and holds document");
        const auto dismiss=[](int result=QMessageBox::Ok) {
            auto *timer=new QTimer(qApp);
            QObject::connect(timer,&QTimer::timeout,qApp,[timer,result] {
                for(QWidget *widget:QApplication::topLevelWidgets())
                    if(auto *box=qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                        timer->stop(); timer->deleteLater();
                        if(auto *button=box->button(QMessageBox::StandardButton(result))) button->click();
                        else box->reject();
                        return;
                    }
            });
            timer->start(5);
        };
        require(second.openDocumentPath(a), "second window opens owned document for consultation");
        require(first.documentLock_ && first.documentLock_->owns(a) && !second.documentLock_
                && second.viewport_->documentReadOnly(), "one editor and one reader of the same document");
        require(!second.saveDocumentPath(a), "reader cannot overwrite the editor's file");
        require(first.openDocumentPath(a), "reopening same document preserves current session");
        require(QSettings().value("document/recentFiles").toStringList()==QStringList{canonicalDocumentPath(a)}, "recent files deduplicate");
        {
            DocumentFileLock blocked(b); require(blocked.acquire().isEmpty(), "save as target owned elsewhere");
            dismiss(); require(!first.saveDocumentPath(b) && first.documentLock_->owns(a), "failed save as preserves original lock");
            require(!QFileInfo::exists(b), "failed save as does not create file");
        }
        const auto originalId = first.viewport_->currentDocument().version.documentId;
        require(first.saveDocumentPath(b), "save as new file");
        const auto copiedId = first.viewport_->currentDocument().version.documentId;
        require(copiedId != originalId && !copiedId.isNull(), "save as creates independent identity");
        DocumentState originalFile, copiedFile;
        require(loadDocumentFile(a,originalFile).isEmpty() && loadDocumentFile(b,copiedFile).isEmpty()
                && originalFile.version.documentId == originalId && copiedFile.version.documentId == copiedId,
                "save as preserves source identity and stores new identity");
        require(first.documentLock_->owns(b) && !QFileInfo::exists(a+".lock"), "save as transfers lock after success");
        dismiss(); require(!first.saveDocumentPath(directory.path()) && first.documentLock_->owns(b), "write failure preserves previous session");
        require(first.viewport_->currentDocument().version.documentId == copiedId, "failed save as preserves document identity");
        const QString invalid=directory.filePath("invalid.prt");
        {QFile file(invalid); require(file.open(QIODevice::WriteOnly),"invalid document fixture"); file.write("invalid");}
        dismiss(); require(!first.openDocumentPath(invalid) && first.documentLock_->owns(b)
                           && !QFileInfo::exists(invalid+".lock"), "load failure preserves old lock and releases candidate");
        first.documentModified_=true;
        require(first.openDocumentPath(b) && first.documentModified_, "same recent document preserves unsaved edits");
        dismiss(QMessageBox::Cancel);
        require(!first.openDocumentPath(a) && first.documentLock_->owns(b) && !QFileInfo::exists(a+".lock"),
                "cancel open preserves original session and releases candidate");
        dismiss(QMessageBox::Cancel); first.newDocument();
        require(first.documentLock_->owns(b) && first.documentModified_, "cancel new keeps document lock");
        dismiss(QMessageBox::Cancel); require(!first.close() && first.documentLock_->owns(b), "cancel close keeps lock");
        first.documentModified_=false;
        first.newDocument(); require(!first.documentLock_ && !QFileInfo::exists(b+".lock"), "new document releases lock");
        require(second.openDocumentPath(b), "released document opens in other window");
        second.close();QElapsedTimer closing;closing.start();
        while(!second.recoveryCloseReady_&&closing.elapsed()<5000)QApplication::processEvents();
        require(second.recoveryCloseReady_ && !QFileInfo::exists(b+".lock"), "accepted asynchronous close releases lock");
        first.refreshRecentDocuments();
        require(menu->actions().first()->data().toString()==canonicalDocumentPath(b), "most recent file first");
        // L'azione del menu usa il normale percorso di apertura esclusiva.
        menu->actions().first()->trigger();
        require(first.documentLock_ && first.documentLock_->owns(b), "recent action opens protected document");
        menu->popup(QPoint(20,20)); QApplication::processEvents();
        require(menu->grab().save(QStringLiteral("/tmp/forgecad-file-recent-menu.png")), "recent menu visual snapshot");
        menu->hide();
        for(int i=0;i<12;++i) first.rememberRecentDocument(directory.filePath(QString("recent%1.prt").arg(i)));
        require(QSettings().value("document/recentFiles").toStringList().size()==10, "recent list bounded at ten");
        PdfWindow restored;
        require(restored.recentDocumentsMenu_->actions().first()->data().toString().endsWith("recent11.prt"), "recent menu persists across windows");
        menu->findChild<QAction *>("clearRecentDocuments")->trigger();
        require(QSettings().value("document/recentFiles").toStringList().isEmpty()
                && menu->actions().size()==1 && !menu->actions().first()->isEnabled(), "clear recent menu");
        first.documentLock_.reset();
        {
            DocumentFileLock missing(a); require(missing.acquire().isEmpty(), "lost lock fixture");
            QFile before(a); require(before.open(QIODevice::ReadOnly),"read before lost lock save"); const QByteArray bytes=before.readAll(); before.close();
            require(QFile::remove(a+".lock"),"simulate lost sidecar");
            require(!saveDocumentFile(a,empty,false,&missing).isEmpty(), "lost lock prevents save");
            QFile after(a); require(after.open(QIODevice::ReadOnly) && after.readAll()==bytes,"lost lock save preserves document bytes");
        }
        std::cout<<"PASS document locks and recent files"<<std::endl;
    }
    static void edgeLoftWorkflow() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const auto line=[](Vec3 a,Vec3 b)->PathSegment {return {std::make_shared<Line<3>>(a,b-a),{0,distance(a,b)}};};
        // Exact spatial profiles, including a nonplanar curve, with endpoint and interior guides.
        auto curve=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec3>{{0,0,10},{3,2,12},{7,-1,11},{10,0,10}});
        LoftSection top,bottom; top.path={{curve,{0,1}}}; bottom.path={line({0,0,0},{10,0,0})};
        LoftOptions options;
        for(double u : {0.0,0.5,1.0}) options.guides.push_back({line({10*u,0,0},curve->point(u))});
        auto sheet=loftSheet({top,bottom},options);
        require(sheet.isSheet() && checkBody(sheet).empty(),"spatial sections with three guides build");
        for(int k=0;k<=32;++k) {
            const Vec3 p=curve->point(k/32.0); double nearest=1e300;
            for(EdgeId e:sheet.edges()) nearest=std::min(nearest,projectPoint(*sheet.edge(e).curve,p,sheet.edge(e).range).distance);
            require(nearest<1e-7,"spatial section remains exact on loft boundary");
        }
        require(checkBody(loftSheet({bottom,top},options)).empty(),"reversed spatial sections and guides");
        options.guides={{line({20,0,0},{20,0,10})}};
        bool rejected=false; try {loftSheet({top,bottom},options);} catch(const std::domain_error &) {rejected=true;}
        require(rejected,"spatial guide missing section rejected");
        // Associate a section to a sheet edge, and continuation to its adjacent face.
        const std::vector<PathSegment> first{line({0,0,0},{10,0,0})};
        auto source=std::make_shared<const Body>(ruledSurface(first,{line({0,-5,0},{10,-5,0})}));
        ExtrusionObject body; body.feature=BodyFeature::Imported; body.name="Superficie sorgente"; body.featureId=701;
        body.forgeBody=source; body.importData=QByteArray::fromStdString(writeStep({{"Superficie",*source, false, {0.0, 0.0, 0.0}, nullptr, {}}}));
        EdgeId selected;
        for(EdgeId e:source->edges()) if(projectPoint(*source->edge(e).curve,{5,0,0},source->edge(e).range).distance<1e-8) {selected=e;break;}
        require(selected.valid(),"source section edge found");
        GeometryRef edge; edge.kind=4; edge.index=0; edge.featureId=701; edge.point=edgeReference(*source,selected,{5,0,0});
        GeometryRef face; face.kind=5; face.index=0; face.featureId=701; face.point=faceReference(*source,source->faces().front(),{5,-2,0});
        SketchObject end; end.name="Sezione finale"; end.customFrame=true; end.frame.origin[1]=10; end.frame.origin[2]=5;
        end.segments={{{0,0},{10,0}}}; end.constraints={-1}; end.segmentLengths={0}; end.segmentAngles={-1};
        GeometryRef sketch; sketch.kind=7; sketch.index=0; sketch.element={-1,-1,-1};
        ExtrusionObject loft; loft.feature=BodyFeature::Loft; loft.loftSurface=true; loft.name="Loft da bordo";
        loft.loftSections={edge,sketch}; loft.loftStartContinuity=3; loft.loftStartFaces={face};
        CadViewport view; view.sketches_={end}; view.extrusions_={body}; view.tessellationQuality_=0;
        QString error;
        auto result=forgeReferencedLoft(loft,1,view.sketches_,view.extrusions_,&error);
        if(!result) throw std::runtime_error(error.toStdString());
        require(checkBody(*result).empty(),"edge section with G1 contact builds");
        // A trimmed edge may leave a last Bezier span only a few ulp wide.
        // Its Greville coordinates must remain distinct for G1 collocation.
        const double last=std::nextafter(1.0,0.0);
        auto narrow=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,last,last,last,1,1,1,1},
            std::vector<Vec3>{{0,0,0},{3,0,0},{6,0,0},{9,0,0},{9+1.0/3,0,0},{9+2.0/3,0,0},{10,0,0}});
        LoftSection tinySection,other; tinySection.path={{narrow,{0,1}}}; other.path={line({0,10,5},{10,10,5})};
        LoftOptions tangent; tangent.startContinuity=1; tangent.startFaces={{source,source->faces().front()}};
        require(checkBody(loftSheet({tinySection,other},tangent)).empty(),"G1 on an exact edge with an ulp-sized final span");

        for(FaceId f:result->faces()) for(double u:{0.15,0.5,0.85})
            require(std::fabs(normalAt(*result->face(f).surface,u,0).z())>1.0-1e-4,"G1 edge contact normal equals adjacent plane normal");
        require(view.createBody(loft).isEmpty(),"create associative edge loft in viewport");
        require(ForgeCad::bodyOperands(view.extrusions_[1]).contains(0) && ForgeCad::sketchesOf(view.extrusions_[1]).contains(0),"edge and sketch dependencies tracked");
        require(view.withDependentBodies({0}).contains(1),"source sheet changes reach loft");
        // Saved references can rebuild without display cache; reordering remaps persistent owners.
        QTemporaryDir dir; DocumentState loaded;
        require(saveDocumentFile(dir.filePath("edge-loft.prt"),view.documentState(),false).isEmpty(),"save edge loft format 42");
        require(loadDocumentFile(dir.filePath("edge-loft.prt"),loaded).isEmpty(),"reopen edge loft format 42");
        require(loaded.extrusions[1].loftSections.size()==2 && loaded.extrusions[1].loftSections[0].featureId==701,"edge loft references persist");
        CadViewport reopened; reopened.restoreDocument(loaded); reopened.regenerateAll();
        require(reopened.extrusions_[1].forgeBody && reopened.extrusions_[1].error.isEmpty(),"full edge loft regeneration after reopen");
        auto bad=loft; bad.loftSections[0].featureId=99999;
        require(!forgeReferencedLoft(bad,1,view.sketches_,view.extrusions_,&error),"missing persistent owner is rejected");
        bad=loft; bad.loftSections[0].index=1; bad.loftSections[0].featureId=0;
        require(!forgeReferencedLoft(bad,1,view.sketches_,view.extrusions_,&error),"forward edge reference rejected");
        const auto oldGeometry=reopened.extrusions_[1].forgeBody;
        auto shifted=std::make_shared<const Body>(ruledSurface({line({0,0,1},{10,0,1})},{line({0,-5,1},{10,-5,1})}));
        reopened.extrusions_[0].forgeBody=shifted;
        reopened.extrusions_[0].importData=QByteArray::fromStdString(writeStep({{"Superficie",*shifted, false, {0.0, 0.0, 0.0}, nullptr, {}}}));
        reopened.regenerateAfter(0);
        require(reopened.extrusions_[1].forgeBody && reopened.extrusions_[1].forgeBody!=oldGeometry
                && reopened.extrusions_[1].error.isEmpty(),"source surface change actually rebuilds associative edge loft");
        bool follows=false;
        for(EdgeId e:reopened.extrusions_[1].forgeBody->edges()) {
            const auto &r=reopened.extrusions_[1].forgeBody->edge(e);
            if(projectPoint(*r.curve,{5,0,1},r.range).distance<1e-7) follows=true;
        }
        require(follows,"regenerated loft follows the moved source edge");
        // UI accepts a body edge in either selector and clears callbacks on close.
        QMainWindow window;
        std::exception_ptr failure;
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=window.findChild<QDialog *>("loftDialog");
            try {
                require(dialog,"loft dialog open");
                auto *list=dialog->findChild<QListWidget *>("loftSections");
                require(list && list->count()==2,"mixed edge and sketch sections restored in order");
                dialog->findChild<QPushButton *>("loftPickSections")->click();
                require(view.referencePicking(),"edge section picker armed");
                auto callback=view.refPickFinished_; callback(true,edge);
                require(list->item(0)->checkState()==Qt::Unchecked,"second click toggles body edge section");
                callback=view.refPickFinished_; callback(true,edge);
                require(list->item(0)->checkState()==Qt::Checked,"body edge section selected again");
                dialog->findChild<QPushButton *>("loftPickGuides")->click();
                callback=view.refPickFinished_; callback(true,edge);
                auto *guideList=dialog->findChild<QListWidget *>("loftGuides");
                require(guideList && guideList->count()==2 && guideList->item(1)->data(Qt::UserRole).toInt()==2
                    && guideList->item(1)->checkState()==Qt::Checked,"body edge added to unified guides");
                guideList->setCurrentRow(1);
                dialog->findChild<QPushButton *>("loftGuideUp")->click();
                require(guideList->item(0)->data(Qt::UserRole).toInt()==2,"edge guide moves before whole sketch guide");
                callback=view.refPickFinished_; callback(false,{});
                require(!dialog->findChild<QPushButton *>("loftPickGuides")->isChecked(),"Esc ends guide selection");
            } catch(...) {failure=std::current_exception();}
            if(dialog) dialog->reject();
        });
        loftDialog(&window,&view,QStringLiteral("Loft da bordi"),1,loft,[](const ExtrusionObject &){return QString();});
        if(failure) std::rethrow_exception(failure);
        require(!view.referencePicking() && !view.refPickFinished_,"loft dialog releases picker callbacks");

        // The first selected guide determines the seam of a closed loft.
        // Verify the actual kernel result when all three guide types mix.
        auto guideSheet=std::make_shared<const Body>(ruledSurface({line({0,2,0},{0,2,3})},{line({0,3,0},{0,3,3})}));
        ExtrusionObject guideBody; guideBody.feature=BodyFeature::Imported; guideBody.featureId=802;
        guideBody.name="Superficie guida"; guideBody.forgeBody=guideSheet;
        guideBody.importData=QByteArray::fromStdString(writeStep({{"guida",*guideSheet, false, {0.0, 0.0, 0.0}, nullptr, {}}}));
        GeometryRef edgeGuide; edgeGuide.kind=4; edgeGuide.index=0; edgeGuide.featureId=802;
        for(EdgeId id:guideSheet->edges()) if(projectPoint(*guideSheet->edge(id).curve,{0,2,1.5},guideSheet->edge(id).range).distance<1e-8)
            edgeGuide.point=edgeReference(*guideSheet,id,{0,2,1.5});
        CurveObject circle; circle.tool=DrawingTool::Circle; circle.controlPoints={{0,0},{2,0}};
        SketchObject c0; c0.name="Cerchio iniziale"; c0.curves={circle};
        SketchObject c1=c0; c1.name="Cerchio finale"; c1.customFrame=true; c1.frame.origin[2]=3;
        const auto verticalSketch=[](double x) {
            SketchObject guide; guide.customFrame=true;
            guide.frame.origin[0]=x; guide.frame.normal[1]=-1; guide.frame.normal[2]=0;
            guide.segments={{{0,0},{0,3}}}; guide.constraints={-1}; guide.segmentLengths={0}; guide.segmentAngles={-1};
            return guide;
        };
        auto wholeGuide=verticalSketch(2); wholeGuide.name="Guida intera";
        auto partialGuide=verticalSketch(-2); partialGuide.name="Guida parziale";
        partialGuide.segments.append({{10,0},{10,3}}); // excluded disconnected component
        CadViewport orderedView; orderedView.sketches_={c0,c1,wholeGuide,partialGuide};
        orderedView.extrusions_={guideBody}; orderedView.tessellationQuality_=0;
        ExtrusionObject ordered; ordered.feature=BodyFeature::Loft; ordered.loftSurface=true;
        ordered.loftSketches={0,1}; ordered.loftGuides={2}; ordered.loftGuidePaths={{3,{0},{}}};
        ordered.loftEdgeGuides={edgeGuide}; ordered.loftGuideContinuity=0;
        ordered.loftGuideContinuities={0,1,2}; ordered.loftGuideInfluences={0.15,0.55,0.85};
        auto previewChanged=ordered;previewChanged.loftGuideContinuities[1]=2;
        require(CadViewport::previewKey(previewChanged,1)!=CadViewport::previewKey(ordered,1),"guide continuity invalidates preview cache");
        previewChanged=ordered;previewChanged.loftGuideInfluences[2]=0.5;
        require(CadViewport::previewKey(previewChanged,1)!=CadViewport::previewKey(ordered,1),"guide influence invalidates preview cache");
        const Vec3 starts[3]={{2,0,0},{-2,0,0},{0,2,0}};
        for (const QVector<int> &sequence : {QVector<int>{2,1,0},QVector<int>{1,0,2},QVector<int>{0,2,1}}) {
            ordered.loftGuideOrder=sequence;
            auto shape=forgeReferencedLoft(ordered,1,orderedView.sketches_,orderedView.extrusions_,&error);
            if(!shape) throw std::runtime_error(error.toStdString());
            require(checkBody(*shape).empty(),"mixed ordered guides build valid loft");
            require(distance(shape->face(shape->faces().front()).surface->point(0,0),starts[sequence.front()])<1e-6,
                "first mixed guide actually sets loft seam");
        }
        auto invalidOrder=ordered; invalidOrder.loftGuideOrder={0,0,2};
        require(!forgeReferencedLoft(invalidOrder,1,orderedView.sketches_,orderedView.extrusions_,&error),"duplicate guide order rejected");
        ordered.loftGuideOrder={2,1,0};
        require(orderedView.createBody(ordered).isEmpty(),"create mixed ordered guide loft");
        DocumentState orderedLoaded;
        require(saveDocumentFile(dir.filePath("ordered-guides.prt"),orderedView.documentState(),false).isEmpty(),"save mixed guide order");
        require(loadDocumentFile(dir.filePath("ordered-guides.prt"),orderedLoaded).isEmpty(),"reopen mixed guide order");
        require(orderedLoaded.extrusions[1].loftGuideOrder==QVector<int>({2,1,0}),"mixed guide order survives serialization");
        require(orderedLoaded.extrusions[1].loftGuideContinuities==ordered.loftGuideContinuities
            && orderedLoaded.extrusions[1].loftGuideInfluences==ordered.loftGuideInfluences,"individual guide conditions survive serialization");
        CadViewport orderedReopened; orderedReopened.restoreDocument(orderedLoaded); orderedReopened.regenerateAll();
        require(orderedReopened.extrusions_[1].forgeBody && orderedReopened.extrusions_[1].error.isEmpty(),"regenerate mixed guides after reopen");
        ExtrusionObject submitted; failure={};
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=window.findChild<QDialog *>("loftDialog");
            try {
                auto *guideList=dialog->findChild<QListWidget *>("loftGuides");
                require(guideList && guideList->item(0)->data(Qt::UserRole).toInt()==2
                    && guideList->item(1)->data(Qt::UserRole).toInt()==1
                    && guideList->item(2)->data(Qt::UserRole).toInt()==0,"unified guide list restores mixed saved order");
                guideList->setCurrentRow(1);
                auto *mode=dialog->findChild<QComboBox *>("loftGuideContinuity");
                auto *influence=dialog->findChild<QDoubleSpinBox *>("loftGuideInfluence");
                require(mode && influence && mode->currentIndex()==1 && influence->value()==55,"selected guide restores its own controls");
                mode->setCurrentIndex(2); influence->setValue(25);
                dialog->findChild<QPushButton *>("loftGuideUp")->click();
                require(mode->currentIndex()==2 && influence->value()==25,"guide controls follow reordered item");
                require(guideList->item(0)->data(Qt::UserRole).toInt()==1,"partial guide moves before edge guide");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            } catch(...) {failure=std::current_exception(); if(dialog)dialog->reject();}
        });
        loftDialog(&window,&orderedView,QStringLiteral("Guide ordinate"),1,ordered,[&](const ExtrusionObject &d){submitted=d;return QString();});
        if(failure)std::rethrow_exception(failure);
        require(submitted.loftGuideOrder==QVector<int>({1,2,0}) && submitted.loftGuides==QVector<int>({2})
            && submitted.loftGuidePaths.size()==1 && submitted.loftEdgeGuides.size()==1,"dialog submits mixed visible order and all guide types");
        require(submitted.loftGuideContinuities==QVector<int>({0,2,2})
            && submitted.loftGuideInfluences==QVector<double>({0.15,0.25,0.85}),"editing and reordering one guide keeps other conditions intact");
        std::cout<<"PASS exact spatial edge loft, guides, tangency, persistence, dependencies and UI selection"<<std::endl;
    }
    static void individualFillControls() {
        using namespace ForgeCad; using namespace ForgeCad::Kernel;
        const auto line=[](Vec3 a,Vec3 b)->PathSegment{return {std::make_shared<Line<3>>(a,normalized(b-a)),{0,distance(a,b)}};};
        const Vec3 a[4]={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
        const Vec3 offset[4]={{0,-3,0},{3,0,0},{0,3,0},{-3,0,0}};
        CadViewport view; QMainWindow window; view.tessellationQuality_=0;
        ExtrusionObject fill;fill.feature=BodyFeature::FillSurface;fill.operation=-1;fill.name="Riempimento locale";
        for(int i=0;i<4;++i) {
            const Vec3 start=a[i],end=a[(i+1)%4],mid=(start+end)/2;
            auto body=std::make_shared<const Body>(ruledSurface({line(start,end)},{line(start+offset[i],end+offset[i])}));
            ExtrusionObject source;source.feature=BodyFeature::Imported;source.featureId=900+i;source.forgeBody=body;
            source.importData=QByteArray::fromStdString(writeStep({{"Supporto",*body, false, {0.0, 0.0, 0.0}, nullptr, {}}}));source.name=QStringLiteral("Supporto %1").arg(i+1);
            view.extrusions_.append(source);
            GeometryRef edge;edge.kind=4;edge.index=i;edge.featureId=source.featureId;
            bool found=false;for(EdgeId e:body->edges())if(projectPoint(*body->edge(e).curve,mid,body->edge(e).range).distance<1e-8){edge.point=edgeReference(*body,e,mid);found=true;break;}
            require(found,"boundary edge exists");fill.planarRefs.append(edge);
            GeometryRef face;face.kind=5;face.index=i;face.featureId=source.featureId;face.point=faceReference(*body,body->faces().front(),mid);
            fill.ruledSecond.append(face);
        }
        const auto originalFaces=fill.ruledSecond;
        auto shapeChanged=fill;shapeChanged.fillShapeFace=1;require(CadViewport::previewKey(shapeChanged,4)!=CadViewport::previewKey(fill,4),"shape reference invalidates preview");
        fill.fillFaceInfluences={0.4,1.2,2.3,3.4};fill.fillFaceContinuities={0,1,2,-1};
        auto changed=fill;changed.fillFaceInfluences[1]=2.0;
        require(CadViewport::previewKey(changed,4)!=CadViewport::previewKey(fill,4),"individual face influence invalidates preview cache");
        ExtrusionObject submitted;std::exception_ptr failure;
        QTimer::singleShot(0,&window,[&]{
            auto *dialog=window.findChild<QDialog *>("fillDialog");
            try {
                require(dialog,"fill dialog open");
                auto *list=dialog->findChild<QListWidget *>("fillContactFaces");
                auto *influence=dialog->findChild<QDoubleSpinBox *>("fillFaceInfluence");
                require(list && influence && list->count()==4,"four independently editable supports");
                list->setCurrentRow(1);require(std::fabs(influence->value()-1.2)<1e-9,"selected face restores its value");
                influence->setValue(2.5);
                auto *continuity=dialog->findChild<QComboBox *>("fillContactContinuity");require(continuity&&continuity->currentIndex()==2,"G1 della faccia selezionata");continuity->setCurrentIndex(3);
                auto *shape=dialog->findChild<QCheckBox *>("fillShapeFace");require(shape,"directional shape control exists");shape->setChecked(true);
                list->setCurrentRow(2);require(!shape->isChecked(),"selecting another face does not transfer shape reference");continuity->setCurrentIndex(1);require(!shape->isEnabled(),"G0 non guida la forma");list->setCurrentRow(1);require(shape->isChecked()&&continuity->currentIndex()==3,"forma e G2 seguono identita faccia");
                dialog->findChild<QPushButton *>("fillPickFaces")->click();
                auto callback=view.refPickFinished_; callback(true,originalFaces[0]);
                require(list->count()==3 && std::fabs(influence->value()-2.5)<1e-9,"removing another face preserves selection and influence");
                callback=view.refPickFinished_; callback(true,originalFaces[0]);
                require(list->count()==4,"removed face can be added again");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            } catch(...) {failure=std::current_exception();if(dialog)dialog->reject();}
        });
        require(fillDialog(&window,&view,"Riempimento",4,fill,[&](const ExtrusionObject &d){submitted=d;return QString();}),"accept individual fill definition");
        if(failure)std::rethrow_exception(failure);
        require(submitted.fillFaceContinuities==QVector<int>({2,0,-1,-1}),"continuita segue identita dopo rimozione e riaggiunta");
        require(submitted.fillFaceInfluences==QVector<double>({2.5,2.3,3.4,1.0}),"face influence follows identity after remove/add");
        require(submitted.fillShapeFace==0&&submitted.fillShapeWeight==1,"shape reference remaps across removed face");
        buildGeometry(submitted,4,view.sketches_,view.extrusions_);require(submitted.forgeBody && submitted.error.isEmpty(),"fill built with individual support influences");
        require(checkBody(*submitted.forgeBody).empty(),"individual fill body valid");
        DocumentState document;document.extrusions=view.extrusions_;document.extrusions.append(submitted);
        QTemporaryDir dir;DocumentState loaded;const QString path=dir.filePath("individual.prt");
        require(saveDocumentFile(path,document,false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),"save and reopen individual fill");
        require(loaded.extrusions.last().fillFaceContinuities==submitted.fillFaceContinuities,"continuita individuali persistenti");
        require(loaded.extrusions.last().fillFaceInfluences==submitted.fillFaceInfluences,"individual influences survive serialization");
        require(loaded.extrusions.last().fillShapeFace==submitted.fillShapeFace&&loaded.extrusions.last().fillShapeWeight==submitted.fillShapeWeight,"directional shape condition survives serialization");
        loaded.extrusions.last().fillFaceInfluences={1.0};
        require(saveDocumentFile(path,loaded,false).isEmpty() && !loadDocumentFile(path,document).isEmpty(),"reject mismatched face influence count in document");
        std::cout<<"PASS individual fill influence, picking identity and persistence"<<std::endl;
    }
    static void savedLoftSurface(const QString &path) {
        using namespace ForgeCad;using namespace ForgeCad::Kernel;
        DocumentState document;QString error=loadDocumentFile(path,document);
        if(!error.isEmpty())throw std::runtime_error(error.toStdString());
        int owner=-1;
        for(int k=0;k<document.extrusions.size();++k)if(document.extrusions[k].feature==BodyFeature::Loft){owner=k;break;}
        require(owner>=0,"saved loft exists");
        auto definition=document.extrusions[owner];
        require(!definition.loftSections.isEmpty() && definition.loftSections.back().kind==4,"last section is surface edge");
        const auto section=definition.loftSections.back();const auto &source=document.extrusions[section.index];
        GeometryRef face;face.kind=5;face.index=section.index;face.featureId=source.featureId;
        const auto &edge=source.forgeBody->edge(EdgeId(section.point.subshape));
        face.point=faceReference(*source.forgeBody,source.forgeBody->faces().front(),edge.curve->point((edge.range.lo+edge.range.hi)/2));
        CadViewport view;view.restoreDocument(document);view.tessellationQuality_=0;
        QMainWindow window;std::exception_ptr failure;QString applyError;bool applied=false;
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=window.findChild<QDialog *>("loftDialog");
            try {
                require(dialog,"saved loft panel open");
                dialog->findChild<QComboBox *>("surfaceEndContinuity")->setCurrentIndex(3);
                dialog->findChild<QPushButton *>("surfaceEndFacesPick")->click();
                require(view.referencePicking(),"adjacent face picker active");
                auto callback=view.refPickFinished_;callback(true,face);
                callback=view.refPickFinished_;callback(false,{});
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                if(!applyError.isEmpty())throw std::runtime_error(applyError.toStdString());
            }catch(...){failure=std::current_exception();if(dialog)dialog->reject();}
        });
        loftDialog(&window,&view,QStringLiteral("Tangenza Loft salvato"),owner,definition,[&](const ExtrusionObject &d){
            applyError=view.updateBody(owner,d);applied=applyError.isEmpty();return applyError;
        });
        if(failure)std::rethrow_exception(failure);
        require(applied && view.extrusions_[owner].loftEndContinuity==3 && view.extrusions_[owner].loftEndFaces.size()==1,
            "face tangency applies and panel closes");
        const auto shape=view.extrusions_[owner].forgeBody;require(shape && checkBody(*shape).empty(),"saved loft tangency valid");
        for(FaceId f:shape->faces())for(int k=1;k<40;++k){
            const auto &surface=*shape->face(f).surface;const double u=surface.uDomain().lo+surface.uDomain().length()*k/40.0;
            const Vec3 p=surface.point(u,1),n=normalAt(surface,u,1);
            const auto projected=projectPoint(*source.forgeBody->face(source.forgeBody->faces().front()).surface,p);
            const auto expected=normalAt(*source.forgeBody->face(source.forgeBody->faces().front()).surface,projected.u,projected.v);
            require(norm(cross(n,expected))<1e-4,"actual adjacent face normals agree");
        }
        std::cout<<"PASS saved Loft panel: adjacent face pick, apply, close, mesh and normals"<<std::endl;
    }
    static void openLoftTangency(const QString &documentPath) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const auto line = [](Vec3 a, Vec3 b) -> PathSegment {
            return {std::make_shared<Line<3>>(a, normalized(b - a)), {0.0, distance(a, b)}};
        };
        const auto section = [](double z) {
            LoftSection s;
            s.frame = Frame3(Vec3(0, 0, z), Vec3(0, 0, 1), Vec3(1, 0, 0));
            s.loop.segments = {{std::make_shared<Line<2>>(Vec2(), Vec2(1, 0)), {0, 10}}};
            return s;
        };
        const auto checked = [](const Body &body) {
            require(body.isSheet() && checkBody(body).empty(), "valid sheet topology");
            TessellationOptions o; o.deflection = 0.05;
            const auto mesh=tessellate(body, o);
            require(mesh.failedFaces == 0, "all sheet faces tessellate");
            for (const auto &faceMesh : mesh.faces) {
                const Face &face=body.face(faceMesh.face);
                int regularNormals=0;
                const auto uRange=face.surface->uDomain(),vRange=face.surface->vDomain();
                const auto uBreaks=face.surface->uBreakpoints(uRange),vBreaks=face.surface->vBreakpoints(vRange);
                for (std::size_t k=0;k<faceMesh.normals.size();++k) {
                    const auto uv=faceMesh.parameters[k];
                    Vec3 d[4]; face.surface->evaluate(uv.x(),uv.y(),1,d);
                    // Negli angoli singolari la mesh usa il limite interno:
                    // il prodotto vettoriale al punto e' numericamente instabile.
                    if (norm(cross(d[1],d[2])) <= 1e-5*norm(d[1])*norm(d[2])) continue;
                    const Vec3 expected=normalAt(*face.surface,uv.x(),uv.y())*(face.sense?1.0:-1.0);
                    double agreement=dot(normalized(faceMesh.normals[k]),normalized(expected));
                    bool onKnot=false;
                    for(double knot:uBreaks)onKnot=onKnot||std::fabs(uv.x()-knot)<1e-7;
                    for(double knot:vBreaks)onKnot=onKnot||std::fabs(uv.y()-knot)<1e-7;
                    // Su un nodo C0 la mesh usa la normale dal lato della
                    // cella triangolata, mentre normalAt sceglie il lato destro.
                    if(onKnot && agreement<=1.0-1e-6)
                        for(double du:{-1e-8,0.0,1e-8})for(double dv:{-1e-8,0.0,1e-8}) {
                            const double u=uv.x()+du,v=uv.y()+dv;
                            if(u<uRange.lo||u>uRange.hi||v<vRange.lo||v>vRange.hi)continue;
                            const Vec3 sided=normalAt(*face.surface,u,v)*(face.sense?1.0:-1.0);
                            agreement=std::max(agreement,dot(normalized(faceMesh.normals[k]),normalized(sided)));
                        }
                    require(agreement>1.0-1e-6,"mesh normals agree with exact face orientation");
                    ++regularNormals;
                }
                require(regularNormals>0,"regular mesh normals verified on every face");
            }
        };
        for (double x : {0.0, 3.0, 10.0}) {
            LoftOptions o;
            o.guides = {{line(Vec3(x, 0, 0), Vec3(x, 0, 10))}};
            const Body b = loftSheet({section(0), section(10)}, o);
            checked(b);
            bool found = false;
            for (EdgeId e : b.edges()) {
                const auto &edge = b.edge(e);
                if (distance(edge.curve->point(edge.range.lo), Vec3(x, 0, 0)) < 1e-6
                    && distance(edge.curve->point(edge.range.hi), Vec3(x, 0, 10)) < 1e-6) found = true;
            }
            // Le pezze complanari vengono unificate: la guida interna puo'
            // appartenere alla faccia senza diventare uno spigolo visibile.
            for (FaceId f : b.faces())
                if (projectPoint(*b.face(f).surface, Vec3(x,0,5)).distance < 1e-6) found = true;
            require(found, "guide on first/interior/last open-section vertex");
            checked(loftSheet({section(10), section(0)}, o));
        }
        LoftOptions multiple;
        multiple.guides = {{line(Vec3(3, 0, 0), Vec3(4, 0, 10))}, {line(Vec3(7, 0, 0), Vec3(8, 0, 10))}};
        checked(loftSheet({section(0), section(10)}, multiple));
        multiple.guides[1] = {line(Vec3(7, 0, 0), Vec3(2, 0, 10))};
        bool rejected = false;
        try { loftSheet({section(0), section(10)}, multiple); } catch (const std::domain_error &) { rejected = true; }
        require(rejected, "crossing open-section guides rejected");
        multiple.guides = {{line(Vec3(12, 0, 0), Vec3(12, 0, 10))}};
        rejected = false;
        try { loftSheet({section(0), section(10)}, multiple); } catch (const std::domain_error &) { rejected = true; }
        require(rejected, "guide missing section rejected");

        const std::vector<PathSegment> first{line(Vec3(0, 0, 0), Vec3(10, 0, 0))};
        const std::vector<PathSegment> second{line(Vec3(0, 10, 5), Vec3(10, 10, 5))};
        auto adjacent = std::make_shared<const Body>(ruledSurface(first, {line(Vec3(0, -5, 0), Vec3(10, -5, 0))}));
        auto adjacentEnd = std::make_shared<const Body>(ruledSurface(second, {line(Vec3(0, 15, 10), Vec3(10, 15, 10))}));
        const auto face = adjacent->faces().front(), endFace = adjacentEnd->faces().front();
        const QVector<QPair<ForgeBody, EdgePoint>> startRefs{{adjacent, faceReference(*adjacent, face, Vec3(5, 0, 0))}};
        const QVector<QPair<ForgeBody, EdgePoint>> endRefs{{adjacentEnd, faceReference(*adjacentEnd, endFace, Vec3(5, 10, 5))}};
        QString error;
        auto free = forgeRuledSurface(first, second, &error);
        require(bool(free), "unconstrained ruled surface"); checked(*free);
        for (const auto &strength : {QPair<double,double>(1.0,1.0), QPair<double,double>(0.35,0.7)}) {
            auto tangent = forgeRuledSurface(first, second, &error, 3, 3, strength.first, strength.second, startRefs, endRefs);
            if (!tangent) throw std::runtime_error(error.toStdString());
            checked(*tangent);
            const Surface &s = *tangent->face(tangent->faces().front()).surface;
            for (double u : {0.0, 0.17, 0.5, 0.83, 1.0}) {
                Vec3 d[4]; s.evaluate(u, 0, 1, d);
                require(std::fabs(normalized(d[1]).z()) < 1e-6, "start derivative tangent to adjacent plane");
                s.evaluate(u, 1, 1, d);
                require(std::fabs(normalized(d[1]).y() - normalized(d[1]).z()) < 1e-6, "end derivative tangent to inclined adjacent plane");
                require(distance(s.point(u,0), Vec3(10*u,0,0)) < 1e-6 && distance(s.point(u,1),Vec3(10*u,10,5)) < 1e-6,
                        "tangency preserves exact boundary profiles");
            }
        }
        require(!forgeRuledSurface(first, second, &error, 3, 0), "missing tangent face rejected");
        require(!forgeRuledSurface(first, second, &error, 3, 0, 1, 1, endRefs), "non-adjacent face rejected");
        auto startSection = section(0), endSection = section(5);
        endSection.frame = Frame3(Vec3(0,10,5),Vec3(0,0,1),Vec3(1,0,0));
        LoftOptions tangentOptions;
        tangentOptions.startContinuity = tangentOptions.endContinuity = 1;
        tangentOptions.startFaces = {{adjacent, face}}; tangentOptions.endFaces = {{adjacentEnd, endFace}};
        checked(loftSheet({startSection,endSection},tangentOptions));
        tangentOptions.ruled = true;
        checked(loftSheet({startSection,endSection},tangentOptions));

        const auto ring = [](double radius, double z) {
            auto circle = std::make_shared<Circle<3>>(Vec3(0,0,z),Vec3(1,0,0),Vec3(0,1,0),radius);
            return std::vector<PathSegment>{{circle,circle->domain()}};
        };
        auto cylinder = std::make_shared<const Body>(ruledSurface(ring(2,0),ring(2,-5)));
        QVector<QPair<ForgeBody,EdgePoint>> cylinderRefs;
        for (FaceId f : cylinder->faces()) cylinderRefs.append({cylinder,faceReference(*cylinder,f,Vec3(2,0,0))});
        auto roundBlend = forgeRuledSurface(ring(2,0),ring(3,5),&error,3,0,1,1,cylinderRefs);
        if (!roundBlend) throw std::runtime_error(error.toStdString());
        checked(*roundBlend);
        for (FaceId f : roundBlend->faces()) {
            const Surface &surface = *roundBlend->face(f).surface;
            for (double u : {0.0,0.25,0.5,0.75,1.0})
                require(std::fabs(normalAt(surface,u,0).z()) < 1e-4,"G1 to cylindrical neighbor along rational circular profile");
        }

        DocumentState saved;
        ExtrusionObject definition; definition.feature=BodyFeature::Ruled; definition.operation=-1;
        definition.loftStartContinuity=definition.loftEndContinuity=3;
        definition.loftStartInfluence=0.35; definition.loftEndInfluence=0.7;
        GeometryRef r; r.kind=5; r.index=0; r.featureId=123; r.point=startRefs[0].second;
        definition.loftStartFaces={r}; r.index=1; r.featureId=124; r.point=endRefs[0].second; definition.loftEndFaces={r};
        saved.extrusions={definition};
        QTemporaryDir dir;
        require(saveDocumentFile(dir.filePath("tangent.prt"),saved,false).isEmpty(),"save tangent parameters");
        DocumentState loaded;
        require(loadDocumentFile(dir.filePath("tangent.prt"),loaded).isEmpty(),"reload format 40");
        const auto &read=loaded.extrusions.front();
        require(read.loftStartContinuity==3 && read.loftEndContinuity==3 && read.loftStartFaces.size()==1 && read.loftEndFaces.size()==1
                && read.loftStartFaces[0].featureId==123 && read.loftEndInfluence==0.7,"tangent modes faces influences persist");
        {
            CadViewport view;
            ExtrusionObject a,b;
            a.name="Piano iniziale"; a.forgeBody=adjacent; b.name="Piano finale"; b.forgeBody=adjacentEnd;
            view.extrusions_={a,b};
            ExtrusionObject dependent=definition;
            dependent.feature=BodyFeature::Loft;
            view.extrusions_.append(dependent);
            require(ForgeCad::bodyOperands(dependent).contains(0) && ForgeCad::bodyOperands(dependent).contains(1),
                    "loft tracks both adjacent face owners");
            require(view.withDependentBodies({0}).contains(2),"adjacent face change reaches dependent loft");
            view.requestPreview(dependent);
            require(view.preview_.replaced.isEmpty(),"loft preview keeps adjacent faces visible for picking");
            view.clearPreview();
            ExtrusionObject controlsDefinition;
            QDialog dialog;
            auto *form=new QFormLayout(&dialog);
            {
                SurfaceEndControls controls(dialog,*form,&view,-1,controlsDefinition);
                auto *mode=dialog.findChild<QComboBox *>("surfaceStartContinuity");
                auto *end=dialog.findChild<QComboBox *>("surfaceEndContinuity");
                require(mode && end && mode->count()==4 && end->count()==4,"independent surface continuity controls");
                mode->setCurrentIndex(3); end->setCurrentIndex(1);
                auto *pick=dialog.findChild<QPushButton *>("surfaceStartFacesPick");
                require(pick && pick->isEnabled(),"adjacent face picking enabled for G1 face mode");
                pick->click();
                require(view.referencePicking(),"surface face pick starts");
                GeometryRef faceRef; faceRef.kind=5; faceRef.index=0; faceRef.point=startRefs[0].second;
                view.refPickFinished_(true,faceRef);
                require(controlsDefinition.loftStartFaces.size()==1 && controlsDefinition.loftStartContinuity==3
                        && controlsDefinition.loftEndContinuity==1,"face pick updates independent endpoint definitions");
                view.refPickFinished_(true,faceRef);
                require(controlsDefinition.loftStartFaces.isEmpty(),"repeat face pick removes face");
            }
            require(!view.referencePicking() && !view.refPickFinished_,"surface controls release callback on close");
        }
        if (!documentPath.isEmpty()) {
            DocumentState doc;
            require(loadDocumentFile(documentPath,doc).isEmpty(),"load mouse2 read only");
            require(doc.sketches.size()>=4,"mouse2 sections and guide");
            for (bool reversed : {false,true}) {
                QVector<SketchObject> sections{doc.sketches[reversed?1:0],doc.sketches[reversed?0:1]};
                auto unguided=forgeLoft(sections,{},false,0,0,1,1,1,1,&error,true);
                require(bool(unguided),"mouse2 original loft without guide"); checked(*unguided);
                auto guided=forgeLoft(sections,{doc.sketches[3]},false,0,0,1,1,1,1,&error,true);
                if (!guided) throw std::runtime_error(error.toStdString());
                checked(*guided);
                std::cout<<"PASS mouse2 open guided loft order="<<reversed<<" faces="<<guided->faces().size()<<std::endl;
            }
            if (QApplication::arguments().contains(QStringLiteral("--render"))) {
                QMainWindow window;
                auto *view=new CadViewport(&window); window.setCentralWidget(view); window.resize(1100,850);
                doc.extrusions.clear(); doc.modelBodies.clear();
                view->loadDocument(doc);
                ExtrusionObject loft; loft.operation=-1; loft.feature=BodyFeature::Loft; loft.loftSketches={1,0}; loft.loftSurface=true;
                require(view->createBody(loft).isEmpty(),"mouse2 original loft for shading verification");
                window.show(); view->fitAll();
                for (bool back : {false,true}) {
                    if (back) view->camera_.yaw_+=180.0f;
                    view->update(); QEventLoop loop; QTimer::singleShot(250,&loop,&QEventLoop::quit); loop.exec();
                    require(view->grabFramebuffer().save(back?"/tmp/forgecad-mouse2-loft-back.png":"/tmp/forgecad-mouse2-loft-front.png"),"mouse2 shading screenshot");
                }
                loft.loftGuides={3};
                require(view->updateBody(0,loft).isEmpty(),"mouse2 guided loft in viewport");
                view->camera_.yaw_-=180.0f; view->update(); QEventLoop loop; QTimer::singleShot(250,&loop,&QEventLoop::quit); loop.exec();
                require(view->grabFramebuffer().save("/tmp/forgecad-mouse2-loft-guided.png"),"mouse2 guided loft screenshot");
                std::exception_ptr uiFailure;
                QTimer::singleShot(100,&window,[&] {
                    auto *dialog=window.findChild<QDialog *>("loftDialog");
                    try {
                        require(dialog && dialog->findChild<QComboBox *>("surfaceStartContinuity")
                                && dialog->findChild<QComboBox *>("surfaceEndContinuity"),"loft exposes both tangent controls");
                        require(dialog->grab().save("/tmp/forgecad-loft-tangency-panel.png"),"loft controls screenshot");
                    } catch (...) { uiFailure=std::current_exception(); }
                    if(dialog) dialog->reject();
                });
                loftDialog(&window,view,QStringLiteral("Loft con guida e tangenza"),0,loft,[](const ExtrusionObject &){return QString();});
                if(uiFailure)std::rethrow_exception(uiFailure);
            }
        }
        std::cout<<"PASS open loft and surface tangency"<<std::endl;
    }
    static void asyncLifetimeRegressions() {
        QThreadPool *pool = QThreadPool::globalInstance();
        require(pool->waitForDone(10000), "background pool initially idle");
        const int previousLimit = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        for (bool destroy : {false, true}) {
            auto viewport = std::make_unique<CadViewport>();
            PrimitiveParameters primitive;
            primitive.size[0] = primitive.size[1] = primitive.size[2] = 4;
            require(viewport->createPrimitive(primitive, QStringLiteral("Base")).isEmpty(), "lifetime test base");
            int finished = 0;
            viewport->workCallback_ = [&](bool active, const QString &, bool) { if (!active) ++finished; };
            QSemaphore entered, release;
            pool->start([&] { entered.release(); release.acquire(); });
            entered.acquire();
            viewport->requestPreview(viewport->extrusions().first(), 0);
            viewport->startPreviewJob();
            viewport->previewTimer_->stop();
            viewport->setFeatureHighlights({0});
            if (destroy) viewport.reset();  // i due worker sono ancora in coda
            release.release();
            const bool completed = pool->waitForDone(10000);
            QCoreApplication::processEvents();
            if (!completed) { pool->setMaxThreadCount(previousLimit); require(false, "lifetime workers completed"); }
            require(finished == (destroy ? 0 : 1), "deliver result only to living viewport");
            if (viewport) {
                require(viewport->preview_.valid && viewport->preview_.geometry, "living viewport receives preview");
                require(viewport->featureHighlights_.size() == 1 && !viewport->featureHighlights_.first().display.vertices.isEmpty(),
                        "living viewport receives feature highlight");
            }
        }
        // Annulla before dispatch: a queued job must exit without publishing geometry.
        {
            CadViewport viewport;
            PrimitiveParameters primitive;
            require(viewport.createPrimitive(primitive, QStringLiteral("Base")).isEmpty(), "cancel test base");
            QSemaphore entered, release;
            pool->start([&] { entered.release(); release.acquire(); });
            entered.acquire();
            int finished = 0;
            viewport.workCallback_ = [&](bool active, const QString &, bool) { if (!active) ++finished; };
            viewport.requestPreview(viewport.extrusions().first(), 0);
            viewport.startPreviewJob();
            const auto token = viewport.previewCancellation_;
            viewport.clearPreview();
            require(token && token->load(), "Annulla signals worker cancellation");
            require(!viewport.previewTimer_->isActive(), "Annulla stops pending preview timer");
            release.release();
            require(pool->waitForDone(10000), "cancelled queued preview completes");
            QCoreApplication::processEvents();
            require(!viewport.previewRunning_ && !viewport.previewCancellation_, "cancelled preview releases job state");
            require(!viewport.preview_.valid && !viewport.preview_.geometry && finished == 1, "cancelled result is discarded and progress closes");
            viewport.requestPreview(viewport.extrusions().first(), 0);
            viewport.startPreviewJob();
            viewport.previewTimer_->stop();
            require(pool->waitForDone(10000), "preview restarts after cancellation");
            QCoreApplication::processEvents();
            require(viewport.preview_.valid && viewport.preview_.geometry, "new preview succeeds after cancellation");
        }
        pool->setMaxThreadCount(previousLimit);
    }
    static void mouseSnapshotRegression(const QString &output = {}) {
        using namespace ForgeCad;
        const QString input = exampleDocumentPath("Mouse.prt");
        DocumentState original;
        const auto loadError = loadDocumentFile(input, original);
        require(loadError.isEmpty(), qPrintable(QStringLiteral("lettura Mouse cache precedente (%1): %2; configurare FORGECAD_EXAMPLE_DIR")
                                               .arg(input, loadError)));
        QVector<std::string> geometry;
        for (const auto &feature : original.extrusions)
            geometry.append(feature.forgeBody ? Kernel::writeBodyBinary(*feature.forgeBody) : std::string());
        QByteArray sharedGeometry;
        QDataStream geometryOut(&sharedGeometry, QIODevice::WriteOnly);
        SnapshotChunkWriter geometryWriter(geometryOut);
        for (const auto &binary : geometry) geometryWriter.write(QByteArray(binary.data(), qsizetype(binary.size())));
        // Il file di esempio puo' essere gia' compatto: confrontare gli stessi
        // snapshot con/senza condivisione, non due salvataggi di epoche diverse.
        QByteArray unsharedGeometry;
        QDataStream unsharedOut(&unsharedGeometry, QIODevice::WriteOnly);
        for (const auto &binary : geometry) unsharedOut << QByteArray(binary.data(), qsizetype(binary.size()));
        require(sharedGeometry.size() < unsharedGeometry.size() / 2, "riduzione blocchi B-rep Mouse rispetto agli snapshot non condivisi");
        QDataStream geometryIn(sharedGeometry);
        SnapshotChunkReader geometryReader(geometryIn);
        for (const auto &binary : geometry)
            require(geometryReader.read() == QByteArray(binary.data(), qsizetype(binary.size()))
                    && geometryIn.status() == QDataStream::Ok, "blocchi B-rep Mouse identici byte per byte");
        DocumentState clean = original;
        for (auto &feature : clean.extrusions) feature.display = {};
        CadViewport viewport;
        viewport.setTessellationQuality(2);
        viewport.loadDocument(clean);
        int planar = -1;
        for (int i = 0; i < clean.extrusions.size(); ++i)
            if (clean.extrusions[i].name == QStringLiteral("Superficie planare 1")
                || clean.extrusions[i].name == QStringLiteral("Superficie planare1")) planar = i;
        require(planar >= 0 && planar + 1 < clean.extrusions.size(), "superficie planare Mouse");
        const quint64 id = viewport.extrusions_[planar].modelBodyId;
        int bodyIndex = -1;
        for (int i = 0; i < viewport.modelBodies_.size(); ++i)
            if (viewport.modelBodies_[i].id == id) bodyIndex = i;
        require(bodyIndex >= 0, "corpo superficie planare Mouse");
        const bool preference = viewport.modelBodies_[bodyIndex].visible;
        viewport.setHistoryPosition(planar + 1);
        viewport.setModelBodyVisible(bodyIndex, true);
        for (int repeat = 0; repeat < 5; ++repeat) {
            require(viewport.extrusions_[planar].visible, "planare visibile prima della cucitura");
            viewport.setHistoryPosition(planar + 2);
            require(!viewport.extrusions_[planar].visible && viewport.storyboardRoots().value(id) != id,
                    "cucitura nasconde il corpo consumato Mouse");
            viewport.setHistoryPosition(planar + 1);
        }
        viewport.setModelBodyVisible(bodyIndex, preference);
        viewport.setHistoryPosition(viewport.extrusions_.size());
        const auto state = viewport.currentDocument();
        QTemporaryDir temporary;
        require(temporary.isValid(), "directory cache Mouse");
        const QString path = output.isEmpty() ? temporary.filePath(QStringLiteral("Mouse.prt")) : output;
        require(saveDocumentFile(path, state).isEmpty(), "salvataggio Mouse compatto");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty(), "rilettura cache compatta");
        require(loaded.extrusions.size() == geometry.size() && loaded.sketches.size() == original.sketches.size(),
                "storia parametrica Mouse conservata");
        for (int i = 0; i < geometry.size(); ++i) {
            const auto &feature = loaded.extrusions[i];
            require(bool(feature.forgeBody) == !geometry[i].empty(), "snapshot Mouse conservato");
            if (feature.forgeBody) {
                require(Kernel::writeBodyBinary(*state.extrusions[i].forgeBody) == geometry[i], "tassellazione non modifica il B-rep Mouse");
                // Anche la lettura binaria ordinaria normalizza i frame analitici:
                // il riferimento compie lo stesso ciclo, senza la nuova cache.
                const auto reference = Kernel::readBodyBinary(geometry[i]);
                require(Kernel::writeBodyBinary(*feature.forgeBody) == Kernel::writeBodyBinary(reference),
                        "B-rep Mouse come nel round trip binario ordinario");
            }
            require(feature.featureId == original.extrusions[i].featureId
                    && feature.modelBodyId == original.extrusions[i].modelBodyId, "identita' storia Mouse");
            if (feature.visible && !state.extrusions[i].display.vertices.isEmpty()) {
                const auto &expected = state.extrusions[i].display;
                require(feature.display.vertices == expected.vertices && feature.display.normals == expected.normals
                        && feature.display.edges == expected.edges && feature.display.faceLabelPoints == expected.faceLabelPoints
                        && feature.display.triangleFaces == expected.triangleFaces, "mesh float conservata senza perdita");
            }
        }
        CadViewport reopened;
        reopened.setTessellationQuality(2);
        reopened.loadDocument(loaded);
        reopened.setHistoryPosition(planar + 1);
        reopened.setModelBodyVisible(bodyIndex, true);
        reopened.setHistoryPosition(planar + 2);
        require(!reopened.extrusions_[planar].visible, "visibilita' Mouse dopo riapertura");
        std::cout << "Mouse original=" << QFileInfo(input).size() << " compact=" << QFileInfo(path).size()
                  << " geometry_unshared=" << unsharedGeometry.size() << " geometry_shared=" << sharedGeometry.size()
                  << " stages=" << geometry.size() << " exact_geometry=standard_binary_round_trip" << std::endl;
    }
    static void storyboardScroll() {
        PdfWindow window;auto *view=window.viewport_;auto *tree=window.modelTree_;
        require(view && tree,"storyboard disponibile");view->tessellationQuality_=0;
        DocumentState state;
        for(int i=0;i<120;++i){SketchObject sketch;sketch.name=QStringLiteral("Schizzo lungo %1 per verifica dello scorrimento orizzontale").arg(i);
            sketch.segments={{{0,0},{1,0}}};state.sketches.append(sketch);}
        for(int i=0;i<20;++i){ExtrusionObject curve;curve.feature=BodyFeature::SpatialSketch;curve.name=QStringLiteral("Schizzo 3D %1").arg(i);
            curve.spatialSketch.nextId=2;SpatialCurve3D e;e.id=1;e.points={{0,0,double(i)},{1,1,double(i)}};curve.spatialSketch.entities={e};
            ForgeCad::buildBody(curve,i,state.sketches,state.extrusions,0);state.extrusions.append(curve);}
        view->restoreDocument(state);
        const auto settle=[&]{for(int i=0;i<4;++i)QApplication::processEvents();tree->doItemsLayout();QApplication::processEvents();};
        settle();tree->resize(260,180);
        tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        tree->header()->setStretchLastSection(false);tree->header()->setSectionResizeMode(0,QHeaderView::Interactive);tree->setColumnWidth(0,900);settle();
        require(tree->verticalScrollBar()->maximum()>300 && tree->horizontalScrollBar()->maximum()>100,"storyboard con entrambi gli scorrimenti");
        const auto item=[&](int type,int index)->QTreeWidgetItem*{
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toInt()==type && (*it)->data(0,Qt::UserRole+1).toInt()==index)return *it;
            return nullptr;
        };
        // La selezione del viewport e' lontana dalla zona letta dall'utente:
        // ricostruire e riselezionare quella voce non deve spostare la vista.
        view->selectObject(SceneObjectKind::Sketch,0);settle();
        for(int offset:{1,2,3}) {
            for(bool checked:{false,true}) {
                tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->maximum()*offset/4);
                tree->horizontalScrollBar()->setValue(tree->horizontalScrollBar()->maximum()/2);
                const int vertical=tree->verticalScrollBar()->value(),horizontal=tree->horizontalScrollBar()->value();
                auto *row=item(kTreeSketch,85);require(row,"voce dello schizzo presente");row->setCheckState(0,checked?Qt::Checked:Qt::Unchecked);settle();
                require(view->sketches_[85].visible==checked,"casella modifica visibilita' schizzo");
                require(tree->verticalScrollBar()->value()==vertical && tree->horizontalScrollBar()->value()==horizontal,"cambio visibilita' conserva scorrimento verticale e orizzontale");
            }
        }
        tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->maximum()*3/4);const int before=tree->verticalScrollBar()->value();
        auto *curve=item(kTreeExtrusion,15);require(curve,"voce curva 3D presente");curve->setCheckState(0,Qt::Unchecked);settle();
        require(!view->extrusions_[15].visible && tree->verticalScrollBar()->value()==before,"visibilita' curva conserva posizione");
        // Anche con il cursore selezionato, un refresh di visibilita' non
        // richiede di raggiungerlo. Il suo spostamento esplicito invece si'.
        QTreeWidgetItem *cursor=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if(StoryboardTree::isHistoryCursor(*it)){cursor=*it;break;}
        require(cursor,"cursore della storia presente");tree->setCurrentItem(cursor);settle();
        tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->maximum());const int cursorBefore=tree->verticalScrollBar()->value();
        item(kTreeSketch,85)->setCheckState(0,Qt::Unchecked);settle();
        require(tree->verticalScrollBar()->value()==cursorBefore,"refresh con cursore selezionato conserva posizione");
        require(StoryboardTree::isHistoryCursor(tree->currentItem()),"refresh mantiene cursore selezionato anche con schizzo fuori vista");
        view->setHistoryPosition(0);settle();
        require(tree->verticalScrollBar()->value()<cursorBefore,"spostamento esplicito segue cursore della storia");
        view->setHistoryPosition(state.extrusions.size());settle();
        auto *story = static_cast<StoryboardTree *>(tree);
        for (auto mode : {StoryboardTree::CursorMode::Left, StoryboardTree::CursorMode::Right}) {
            story->setCursorMode(mode);settle();
            auto *rail = story->historyRail();
            for (int scroll : {0, tree->verticalScrollBar()->maximum()/4, tree->verticalScrollBar()->maximum()}) {
                tree->verticalScrollBar()->setValue(scroll);settle();
                cursor = nullptr;
                for (QTreeWidgetItemIterator it(tree);*it;++it)
                    if (StoryboardTree::isHistoryCursor(*it)) {cursor=*it;break;}
                require(cursor,"scrolling cursor row present");
                const QRect handle=static_cast<StoryboardPositionSlider *>(rail)->handleRect();
                const int handleY=rail->mapTo(tree,handle.center()).y()-tree->viewport()->geometry().top();
                require(qAbs(handleY-tree->visualItemRect(cursor).center().y())<=1,"handle follows actual cursor row through scrolling");
                require(rail->parentWidget()->height()==tree->viewport()->height(),"rail clipped to visible storyboard height");
            }
        }
    }

    static void sketchGhostRender() {
        for (bool featurePreview : {false, true}) {
        CadViewport view;view.resize(480,480);view.setGridVisible(false);view.setAxesVisible(false);
        for(int p=0;p<3;++p)view.setPlaneVisible(p,false);
        PrimitiveParameters box;
        require(view.createPrimitive(box,QStringLiteral("Strato frontale")).isEmpty() && view.createPrimitive(box,QStringLiteral("Corpo dietro")).isEmpty(),"corpi per trasparenza schizzo");
        view.setModelBodyVisible(1,false);
        view.createSketch(0,QStringLiteral("Schizzo non ancora usato"));view.setDisplayMode(1);
        BodyDisplay front;front.vertices={{-2,-2,1},{2,-2,1},{2,2,1},{-2,-2,1},{2,2,1},{-2,2,1}};
        front.normals.fill(QVector3D(0,0,1),front.vertices.size());
        if (featurePreview) {
            view.setSketchViewUnlocked(true);
            view.preview_.key = QStringLiteral("ghost-render-feature-preview");
            view.preview_.valid = true;
            view.preview_.definition.feature = BodyFeature::Extrusion;
            view.preview_.display = front;
            // Patch visibile in un angolo; il centro misura soltanto i
            // solidi esistenti, anche con l'anteprima feature attiva.
            for (auto &point : view.preview_.display.vertices)
                point = QVector3D(1.5f+0.1f*point.x(),1.5f+0.1f*point.y(),2);
        }
        view.extrusions_[0].display={};view.setViewFrame({1,0,0},{0,0,1});view.fitView({{-3,-3,0},{3,3,1}});
        view.show();for(int i=0;i<10;++i)QApplication::processEvents();require(view.isValid(),"contesto GL trasparenza schizzo");
        const auto capture=[&]{view.update();QApplication::processEvents();return view.grabFramebuffer();};
        const auto background=capture();view.extrusions_[0].display=front;
        view.setSketchBodyOpacity(1);const auto opaque=capture();
        view.setSketchBodyOpacity(0.35);const auto single=capture();
        auto layered=front;
        for(int layer=1;layer<=24;++layer)for(int v=0;v<front.vertices.size();++v){layered.vertices.append(front.vertices[v]-QVector3D(0,0,float(layer)*0.03f));layered.normals.append(front.normals[v]);}
        view.extrusions_[0].display=layered;const auto many=capture();
        BodyDisplay instances=front;instances.instancedBase=std::make_shared<BodyDisplay>(front);
        for(int layer=0;layer<25;++layer){QMatrix4x4 transform;transform.translate(0,0,-float(layer)*0.03f);instances.instanceTransforms.append(transform);}
        view.extrusions_[0].display=instances;const auto instanced=capture();
        view.extrusions_[0].display=front;view.setModelBodyVisible(1,true);
        auto back=front;for(auto &point:back.vertices)point.setZ(0);
        view.extrusions_[1].display=back;const auto bodies=capture();
        int maximum=0,alphaError=0,signal=0;
        for(int y=single.height()/2-8;y<=single.height()/2+8;++y)for(int x=single.width()/2-8;x<=single.width()/2+8;++x) {
            const auto pixel=single.pixelColor(x,y),bg=background.pixelColor(x,y),solid=opaque.pixelColor(x,y);
            signal=qMax(signal,qAbs(solid.red()-bg.red())+qAbs(solid.green()-bg.green())+qAbs(solid.blue()-bg.blue()));
            for(auto image:{many,instanced,bodies}){const auto other=image.pixelColor(x,y);maximum=qMax(maximum,qMax(qAbs(pixel.red()-other.red()),qMax(qAbs(pixel.green()-other.green()),qAbs(pixel.blue()-other.blue()))));}
            alphaError=qMax(alphaError,qMax(qAbs(pixel.red()-qRound(0.35*solid.red()+0.65*bg.red())),qMax(qAbs(pixel.green()-qRound(0.35*solid.green()+0.65*bg.green())),qAbs(pixel.blue()-qRound(0.35*solid.blue()+0.65*bg.blue())))));
        }
        std::cout<<"ghost feature_preview="<<featurePreview<<" hidden-layer max delta="<<maximum<<"; opacity35% error="<<alphaError<<std::endl;
        require(signal>40,"campione GL contiene effettivamente la superficie");
        require(maximum<=2,"trasparenza indipendente dagli strati nascosti, dalle istanze e dai corpi sovrapposti");
        require(alphaError<=3,"opacita35% applicata una sola volta sullo sfondo");
        view.makeCurrent();require(view.glGetError()==GL_NO_ERROR,"stato OpenGL valido dopo anteprima schizzo");view.doneCurrent();
        const auto dir=qEnvironmentVariable("FORGECAD_SKETCH_GL_QA_DIR")+(featurePreview?QStringLiteral("/feature-preview"):QString());if(!qEnvironmentVariable("FORGECAD_SKETCH_GL_QA_DIR").isEmpty()){QDir().mkpath(dir);single.save(dir+"/ghost-single.png");many.save(dir+"/ghost-many.png");opaque.save(dir+"/ghost-opaque.png");}
        view.close();
        }
    }

    static void sketchStoryRegressions() {
        CadViewport view;
        view.tessellationQuality_ = 0;
        PrimitiveParameters box; box.size[0] = box.size[1] = box.size[2] = 4;
        require(view.createPrimitive(box, QStringLiteral("Base")).isEmpty(), "base della storia");
        ExtrusionObject datum; datum.name = QStringLiteral("Appoggio"); datum.feature = BodyFeature::DatumPlane;
        GeometryRef xy; xy.kind = 1; xy.index = 0;
        datum.datum.refs = {xy}; datum.datum.distance = 3; datum.datum.size = 10;
        require(view.createBody(datum).isEmpty(), "piano precedente allo schizzo");
        const int s = view.createSketch(0, QStringLiteral("Profilo autonomo"));
        view.sketches_[s].segments = {{{0,0},{1,0}},{{1,0},{1,1}},{{1,1},{0,1}},{{0,1},{0,0}}};
        const auto segments = view.sketches_[s].segments;
        view.endSketchMode();
        require(view.createScale(0, 1.2, 0, {}, QStringLiteral("Scala indipendente")).isEmpty(), "lavorazione fra schizzo e utilizzatore");
        ExtrusionObject extrusion; extrusion.sketchIndex = s; extrusion.distance = 2; extrusion.solid = true;
        require(view.createExtrusion(extrusion, QStringLiteral("Estrusione del profilo")).isEmpty(), "utilizzatore dello schizzo");
        const quint64 identity = view.sketches_[s].storyId;
        require(identity && view.storyboardIndex(true,s) == 2 && view.storyboard().size() == 5, "schizzo conserva la posizione di creazione");
        require(view.moveStoryboardStep(2,3).isEmpty() && view.storyboardIndex(true,s) == 3, "schizzo si sposta indipendentemente dalla feature");
        view.selectSketch(s);
        require(view.isBodyDrawn(2) && !view.isBodyDrawn(0) && !view.isBodyDrawn(3), "contesto segue la posizione autonoma dello schizzo");
        view.endSketchMode();
        require(!view.moveStoryboardStep(3,4).isEmpty() && view.storyboardIndex(true,s) == 3, "rifiuto atomico dopo l'utilizzatore");
        GeometryRef support; support.kind = 8; support.index = 1;
        require(view.changeSketchSupport(s, support).isEmpty(), "sostituzione del piano di riferimento");
        require(std::abs(view.sketches_[s].frame.origin[2]-3) < 1e-9 && view.sketches_[s].datumPlane == 1, "schizzo associato al nuovo piano");
        require(view.sketches_[s].segments.size() == segments.size() && view.sketches_[s].segments[1].second == segments[1].second,
                "profilo locale conservato durante cambio piano");
        const auto zBounds = [&] {
            const auto body = view.extrusions_.back().forgeBody;
            require(bool(body), "geometria dipendente disponibile");
            double lo = 1e100, hi = -1e100;
            for (auto vertex : body->vertices()) { const double z = body->vertex(vertex).point.z(); lo = std::min(lo,z); hi = std::max(hi,z); }
            return QPair<double,double>{lo,hi};
        };
        require(std::abs(zBounds().first-3) < 1e-8 && std::abs(zBounds().second-5) < 1e-8, "estrusione rigenerata sul nuovo piano");
        require(!view.moveStoryboardStep(3,0).isEmpty() && view.storyboardIndex(true,s) == 3, "schizzo non puo' precedere il suo piano");
        auto changedDatum = view.extrusions_[1]; changedDatum.datum.distance = 4;
        require(view.updateBody(1,changedDatum).isEmpty(), "modifica del piano sostituito");
        require(std::abs(view.sketches_[s].frame.origin[2]-4) < 1e-8 && std::abs(zBounds().first-4) < 1e-8, "appoggio e utilizzatore seguono il datum");
        GeometryRef xz; xz.kind = 1; xz.index = 1;
        require(view.changeSketchSupport(s,xz).isEmpty() && view.sketches_[s].plane == 1 && view.sketches_[s].datumPlane == -1,
                "sostituzione con piano di origine");
        view.undo();
        require(view.sketches_[s].datumPlane == 1 && std::abs(zBounds().first-4) < 1e-8, "Undo ripristina appoggio e risultato");
        view.redo(); require(view.sketches_[s].plane == 1, "Redo del cambio piano"); view.undo();
        GeometryRef face; face.kind = 5; face.index = 0; face.featureId = view.extrusions_[0].featureId;
        const auto base = view.extrusions_[0].forgeBody;
        bool found = false;
        for (auto f : base->faces()) {
            if (base->face(f).surface->type() != ForgeCad::Kernel::SurfaceType::Plane) continue;
            const auto first = base->loop(base->face(f).loops.front()).first;
            const auto p = base->finPoint(first,0.5);
            if (std::abs(p.z()-4) < 1e-8 && std::abs(base->face(f).surface->normal(0,0).z()) > 0.9) {
                face.point = ForgeCad::faceReference(*base,f,p); found = true; break;
            }
        }
        require(found && view.changeSketchSupport(s,face).isEmpty(), "sostituzione con faccia piana precedente");
        auto changedBase = view.extrusions_[0]; changedBase.primitive.size[2] = 5;
        require(view.updateBody(0,changedBase).isEmpty(), "modifica del produttore della faccia");
        require(std::abs(view.sketches_[s].frame.origin[2]-5) < 1e-8 && std::abs(zBounds().first-5) < 1e-8,
                "schizzo su faccia e dipendenti seguono la rigenerazione");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("schizzi.prt"));
        require(ForgeCad::saveDocumentFile(path,view.documentState()).isEmpty(), "salvataggio storia unificata");
        DocumentState saved;
        require(ForgeCad::loadDocumentFile(path,saved).isEmpty(), "rilettura storia unificata");
        CadViewport restored; restored.loadDocument(saved);
        require(restored.sketches_[s].storyId == identity && restored.storyboardIndex(true,s) == 3
                    && restored.sketches_[s].support && restored.sketches_[s].support->featureId == face.featureId,
                "identita, posizione e appoggio conservati nel file");
        auto legacy = saved;
        for (auto &sketch : legacy.sketches) { sketch.storyId = 0; sketch.beforeFeatureId = 0; }
        ForgeCad::normalizeModelHistory(legacy);
        const auto migrated = ForgeCad::modelStory(legacy.sketches,legacy.extrusions);
        require(migrated[3].sketch && legacy.sketches[s].storyId != 0, "migrazione degli schizzi prima del primo utilizzatore");
        restored.setStoryboardPosition(2);
        const int countBefore = restored.extrusions_.size();
        require(!restored.createExtrusion(extrusion, QStringLiteral("Uso anticipato")).isEmpty()
                    && restored.extrusions_.size() == countBefore, "nuova lavorazione non puo' usare uno schizzo futuro");
        const int free = restored.createSketch(0,QStringLiteral("Schizzo al cursore")); restored.endSketchMode();
        require(restored.storyboardIndex(true,free) == 2 && restored.storyboardPosition() == 3, "nuovo schizzo al cursore indipendente");
        restored.setStoryboardPosition(2); require(!restored.isSketchDrawn(free), "rollback prima dello schizzo");
        restored.setStoryboardPosition(3); require(restored.isSketchDrawn(free), "rollback dopo lo schizzo");
        restored.undo(); require(restored.sketches_.size() == 1, "Undo creazione del passo schizzo");
        restored.redo(); require(restored.sketches_.size() == 2 && restored.storyboardIndex(true,free) == 2, "Redo conserva la posizione");
        restored.setDocumentReadOnly(true);
        require(!restored.moveStoryboardStep(2,3).isEmpty() && !restored.changeSketchSupport(s,xy).isEmpty(), "documento consultato immutabile");
        CadViewport eraseConsumer;
        eraseConsumer.loadDocument(saved);
        require(eraseConsumer.createPrimitive(box, QStringLiteral("Ultimo indipendente")).isEmpty(), "passo dopo l'utilizzatore");
        eraseConsumer.deleteObject(SceneObjectKind::Extrusion, 3);
        require(eraseConsumer.storyboardIndex(true,s) == 3 && eraseConsumer.storyboardIndex(false,3) == 4,
                "eliminare l'utilizzatore conserva la posizione autonoma dello schizzo");
        CadViewport deleted;
        deleted.loadDocument(saved);
        deleted.deleteObject(SceneObjectKind::Extrusion, 0);
        require(!deleted.sketches_[s].supportError.isEmpty() && !deleted.extrusions_.back().forgeBody,
                "eliminare l'appoggio impedisce l'uso della geometria obsoleta");
        PdfWindow window;
        window.viewport_->loadDocument(saved);
        window.rebuildModelTree();
        auto *tree = window.modelTree_;
        QTreeWidgetItem *root = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0,Qt::UserRole+2).toString() == QStringLiteral("H")) root = tree->topLevelItem(i);
        require(root && root->childCount() == 6 && root->child(3)->data(0,Qt::UserRole).toInt() == kTreeSketch,
                "albero espone lo schizzo come passo autonomo");
        require(root->child(3)->data(0,Qt::UserRole+4).toInt() == 3 && (root->child(3)->flags() & Qt::ItemIsDragEnabled),
                "schizzo trascinabile alla propria posizione della storia");
        require(tree->grab().save(QStringLiteral("/tmp/sketch-story/tree.png")), "cattura storyboard con schizzo autonomo");
        bool menuSeen = false, dialogSeen = false;
        QElapsedTimer menuClock; menuClock.start();
        QTimer chooseSupport; chooseSupport.setInterval(1);
        QObject::connect(&chooseSupport, &QTimer::timeout, &window, [&] {
            if (menuClock.elapsed() > 5000) {
                if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) menu->close();
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
                chooseSupport.stop(); return;
            }
            if (!menuSeen) if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
                for (auto *action : menu->actions()) if (action->text() == QStringLiteral("Cambia piano di riferimento...")) {
                    menuSeen = true; menu->setActiveAction(action);
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QApplication::sendEvent(menu, &enter); return;
                }
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                if (auto *choice = dialog->findChild<QComboBox *>(QStringLiteral("sketchSupportChoice"))) {
                    dialogSeen = true;
                    require(choice->count() > 3, "dialogo offre gli appoggi precedenti");
                    dialog->grab().save(QStringLiteral("/tmp/sketch-story/change-plane.png"));
                    choice->setCurrentIndex(0); chooseSupport.stop(); dialog->accept();
                }
        });
        chooseSupport.start(); tree->doItemsLayout();
        tree->customContextMenuRequested(tree->visualItemRect(root->child(3)).center());
        require(menuSeen && dialogSeen && window.viewport_->sketches_[s].plane == 0
                    && !window.viewport_->sketches_[s].support, "menu e dialogo reali cambiano il piano dello schizzo");
        std::cout << "PASS sketch story: independent order, dependencies, supports, regeneration, persistence, undo, rollback, tree and plane dialog" << std::endl;
    }

    static void historyPositionRegressions() {
        {
            CadViewport edit;
            PrimitiveParameters box;box.size[0]=box.size[1]=box.size[2]=4;
            require(edit.createPrimitive(box,QStringLiteral("Base prima dello schizzo")).isEmpty(),"base per contesto schizzo");
            const int sketch=edit.createSketch(0,QStringLiteral("Profilo intermedio"));
            edit.sketches_[sketch].segments={{{0,0},{1,0}},{{1,0},{1,1}},{{1,1},{0,1}},{{0,1},{0,0}}};
            edit.endSketchMode();
            ExtrusionObject extrusion;extrusion.sketchIndex=sketch;extrusion.distance=2;extrusion.solid=true;
            require(edit.createExtrusion(extrusion,QStringLiteral("Uso del profilo")).isEmpty(),"feature consumatrice del profilo");
            require(edit.createScale(0,10,0,{},QStringLiteral("Lavorazione successiva")).isEmpty(),"stadio successivo dello stesso corpo");
            const auto before=edit.documentState();const int cursor=edit.historyPosition();
            edit.selectSketch(sketch);
            const auto points=edit.sceneGeometryPoints(false,false);float extent=0;
            for(const auto &point:points)extent=qMax(extent,qMax(qAbs(point.x()),qMax(qAbs(point.y()),qAbs(point.z()))));
            require(extent<=4.01f,"in modifica schizzo si vede lo stadio precedente, senza lavorazioni future");
            require(edit.historyPosition()==cursor,"il contesto schizzo non sposta il cursore storyboard");
            for(int i=0;i<before.extrusions.size();++i)require(edit.extrusions_[i].visible==before.extrusions[i].visible,"la visibilita documentale non cambia in modifica schizzo");
            require(edit.referenceBodyEligible(0) && !edit.referenceBodyEligible(2),"riferimenti limitati allo stesso contesto della vista");
            edit.endSketchMode();
            require(edit.extrusions_.at(2).visible && edit.historyPosition()==cursor,"uscita schizzo ripristina la vista finale");
            edit.setModelBodyVisible(0,false);edit.selectSketch(sketch);
            require(!edit.isBodyDrawn(0),"il contesto rispetta il corpo nascosto dall'utente");
            edit.endSketchMode();edit.setModelBodyVisible(0,true);
            edit.setHistoryPosition(0);edit.selectSketch(sketch);
            for(int i=0;i<edit.extrusions_.size();++i)require(!edit.isBodyDrawn(i),"il cursore prima dello schizzo limita il contesto");
            edit.endSketchMode();edit.setHistoryPosition(cursor);
            const int unused=edit.createSketch(0,QStringLiteral("Profilo non usato"));
            require(edit.isBodyDrawn(2) && !edit.isBodyDrawn(0),"schizzo non usato mostra i tip al cursore corrente");
            edit.endSketchMode();edit.selectSketch(sketch);
            require(!edit.isSketchDrawn(unused),"schizzi non usati successivi esclusi dal contesto");
            edit.endSketchMode();

        }
        {
            CadViewport timeline;
            PrimitiveParameters primitive;
            primitive.size[0] = primitive.size[1] = primitive.size[2] = 4;
            require(timeline.createPrimitive(primitive, QStringLiteral("Base")).isEmpty(), "timeline base");
            require(timeline.createScale(0, 2, 0, {}, QStringLiteral("Later")).isEmpty(), "timeline later");
            const quint64 laterId = timeline.extrusions().at(1).featureId;
            timeline.setHistoryPosition(0);
            require(!timeline.extrusions().at(0).visible && !timeline.extrusions().at(1).visible, "timeline start empty");
            require(timeline.modelBodyTip(0) == -1, "body does not exist before its first feature");
            timeline.setHistoryPosition(1);
            require(timeline.extrusions().at(0).visible && !timeline.extrusions().at(1).visible, "timeline rollback");
            require(timeline.modelBodyTip(0) == 0, "body selection follows rollback");
            require(timeline.createScale(timeline.modelBodyTip(0), 1.5, 0, {}, QStringLiteral("Inserted")).isEmpty(), "timeline insertion from selected body");
            require(timeline.extrusions().at(2).featureId == laterId && timeline.extrusions().at(2).firstBody == 1
                        && timeline.extrusions().at(2).error.isEmpty(), "timeline downstream recalculated");
            timeline.setHistoryPosition(3);
            require(timeline.extrusions().at(2).visible && !timeline.extrusions().at(1).visible, "timeline forward to end");
            require(timeline.modelBodyTip(0) == 2, "body selection returns to final tip");
            timeline.undo();
            require(timeline.extrusions().size() == 2 && timeline.extrusions().at(1).featureId == laterId, "timeline undo insertion");
        }
        {
            CadViewport timeline;
            PrimitiveParameters primitive;
            primitive.size[0] = primitive.size[1] = primitive.size[2] = 4;
            require(timeline.createPrimitive(primitive, QStringLiteral("Base")).isEmpty()
                        && timeline.createScale(0, 2, 0, {}, QStringLiteral("Intermedia")).isEmpty()
                        && timeline.createScale(1, 2, 0, {}, QStringLiteral("Finale")).isEmpty(), "failed stage timeline");
            timeline.extrusions_[1].error = QStringLiteral("Errore di rigenerazione");
            timeline.setHistoryPosition(2);
            require(timeline.modelBodyTip(0) == 0 && timeline.extrusions_[0].visible, "rollback skips failed stage");
            timeline.extrusions_[1].error.clear();
            timeline.extrusions_[1].suppressed = true;
            timeline.setHistoryPosition(2);
            require(timeline.modelBodyTip(0) == 0 && timeline.extrusions_[0].visible, "rollback skips suppressed stage");
        }
        {
            CadViewport timeline;
            PrimitiveParameters base;
            base.size[0] = 4; base.size[1] = 3; base.size[2] = 2;
            PrimitiveParameters tool = base;
            tool.origin[0] = 3;
            require(timeline.createPrimitive(base, QStringLiteral("Base")).isEmpty()
                        && timeline.createPrimitive(tool, QStringLiteral("Utensile")).isEmpty()
                        && timeline.createBoolean(::BooleanOperation::Difference, 0, {1}, QStringLiteral("Differenza")).isEmpty(), "consumed body timeline");
            const quint64 a = timeline.modelBodies_[0].id, b = timeline.modelBodies_[1].id;
            require(timeline.storyboardRoots().value(b) == a, "tool belongs to final boolean history");
            timeline.setHistoryPosition(2);
            require(timeline.storyboardRoots().value(b) == b && timeline.modelBodyTip(1) == 1,
                    "rollback restores independent tool history branch");
            timeline.setModelBodyVisible(1, true);
            require(timeline.extrusions().at(1).visible, "tool can be shown before consumption");
            for (int repeat = 0; repeat < 3; ++repeat) {
                timeline.setHistoryPosition(3);
                require(!timeline.extrusions().at(1).visible && timeline.extrusions().at(2).visible,
                        "consumed tool cannot remain visible without a body row");
                timeline.setHistoryPosition(2);
                require(timeline.extrusions().at(1).visible, "rollback preserves shown tool preference");
            }
            timeline.setHistoryPosition(3);
            timeline.setModelBodyMeshColor(0, QColor(Qt::green));
            require(timeline.documentState().modelBodies.at(1).visible, "shown consumed body preference survives document change");
            CadViewport restored;
            restored.loadDocument(timeline.documentState());
            restored.setHistoryPosition(2);
            require(restored.extrusions().at(1).visible, "shown consumed body preference survives reload");
            timeline.setHistoryPosition(2);
            timeline.setModelBodyVisible(1, false);
            timeline.setHistoryPosition(3);
            timeline.setHistoryPosition(2);
            require(!timeline.extrusions().at(1).visible, "hidden tool remains hidden after cursor changes");
            timeline.setHistoryPosition(3);
            require(timeline.setFeatureSuppressed(2, true).isEmpty()
                    && timeline.extrusions().at(0).visible && timeline.extrusions().at(1).visible,
                    "suppression restores operands despite consumed scene filter");
            require(timeline.setFeatureSuppressed(2, false).isEmpty()
                    && !timeline.extrusions().at(1).visible && timeline.extrusions().at(2).visible,
                    "reactivation consumes restored operand again");
            timeline.extrusions_[2].error = QStringLiteral("Booleana fallita");
            timeline.setHistoryPosition(3);
            require(timeline.storyboardRoots().value(b) == b && timeline.resultBodiesBefore(-1).contains(1),
                    "failed operation cannot consume a body");
        }
        {
            PdfWindow window;
            auto *model = dynamic_cast<CadViewport *>(window.centralWidget());
            auto *tree = dynamic_cast<StoryboardTree *>(window.findChild<QTreeWidget *>());
            require(model && tree, "storyboard UI available");
            QMenu *viewMenu = nullptr;
            for (QAction *action : window.menuBar()->actions())
                if (action->text() == QStringLiteral("Visualizza")) viewMenu = action->menu();
            require(viewMenu, "view menu available");
            int viewIcons = 0;
            const std::function<void(QMenu *)> checkViewIcons = [&](QMenu *menu) {
                for (QAction *action : menu->actions()) {
                    if (action->isSeparator()) continue;
                    require(!action->icon().isNull() && action->isIconVisibleInMenu(), "all view commands have visible icons");
                    ++viewIcons;
                    if (action->menu()) checkViewIcons(action->menu());
                }
            };
            checkViewIcons(viewMenu);
            require(viewIcons > 40, "view submenu icon coverage");
            viewMenu->ensurePolished();
            viewMenu->adjustSize();
            require(viewMenu->grab().save(QStringLiteral("/tmp/forgecad-view-menu-icons.png")), "view menu icon snapshot");
            PrimitiveParameters box;
            box.size[0] = box.size[1] = box.size[2] = 4;
            require(model->createPrimitive(box, QStringLiteral("Base")).isEmpty()
                        && model->createScale(0, 2, 0, {}, QStringLiteral("Later")).isEmpty(), "storyboard UI model");
            const auto history = [&]() -> QTreeWidgetItem * {
                for (int i = 0; i < tree->topLevelItemCount(); ++i)
                    if (tree->topLevelItem(i)->data(0, Qt::UserRole + 2).toString() == QStringLiteral("H")) return tree->topLevelItem(i);
                return nullptr;
            };
            QApplication::processEvents();
            auto *root = history();
            require(root && root->childCount() == 3 && StoryboardTree::isHistoryCursor(root->child(2)), "cursor inside tree at end");
            model->setHistoryPosition(1);
            QApplication::processEvents();
            root = history();
            require(StoryboardTree::isHistoryCursor(root->child(1)) && root->child(2)->font(0).italic(), "cursor separates active and future rows");
            require(root->child(0)->text(0).contains(QStringLiteral("risultato"))
                        && !root->child(2)->text(0).contains(QStringLiteral("risultato")), "result badge follows cursor");
            tree->setCurrentItem(root->child(1));
            QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
            QApplication::sendEvent(tree, &end);
            QApplication::processEvents();
            require(model->historyPosition() == 2 && StoryboardTree::isHistoryCursor(history()->child(2)), "cursor keyboard end");
            tree->resize(400, 500);
            tree->show();
            QApplication::processEvents();
            root = history();
            const QPointF from(tree->visualItemRect(root->child(2)).center());
            const QPointF to(tree->visualItemRect(root->child(1)).topLeft() + QPoint(30, 1));
            QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &press);
            QApplication::sendEvent(tree->viewport(), &move);
            QApplication::sendEvent(tree->viewport(), &release);
            QApplication::processEvents();
            require(model->historyPosition() == 1 && StoryboardTree::isHistoryCursor(history()->child(1)), "cursor drag between features");
            require(tree->grab().save(QStringLiteral("/tmp/forgecad-storyboard-cursor.png")), "cursor visual snapshot");
            for (int mode : {1, 2}) {
                auto *action = window.findChild<QAction *>(QStringLiteral("storyboardCursorMode%1").arg(mode));
                require(action, "cursor mode menu option");
                action->trigger();
                QApplication::processEvents();
                require(int(tree->cursorMode()) == mode && model->historyPosition() == 1,
                        "mode change preserves calculation position");
                auto *rail = tree->historyRail();
                require(!rail->isHidden() && QSettings().value(QStringLiteral("view/storyboardCursorMode")).toInt() == mode,
                        "vertical rail enabled and preference saved");
                require((mode == 1 && rail->parentWidget()->geometry().right() <= tree->viewport()->geometry().left())
                            || (mode == 2 && rail->parentWidget()->geometry().left() > tree->viewport()->geometry().right()),
                        "vertical handle occupies side gutter");
                rail->triggerAction(QAbstractSlider::SliderToMinimum);
                QApplication::processEvents();
                require(model->historyPosition() == 0, "vertical cursor beginning");
                rail->triggerAction(QAbstractSlider::SliderToMaximum);
                QApplication::processEvents();
                require(model->historyPosition() == 2, "vertical cursor end");
                // Move the native handle to the gap before the second feature.
                model->setHistoryPosition(1);
                QApplication::processEvents();
                rail->setSliderDown(true);
                rail->setSliderPosition(rail->value());
                rail->sliderMoved(rail->value());
                rail->setSliderDown(false);
                QApplication::processEvents();
                require(model->historyPosition() == 1, "vertical drag retains insertion boundary");
                const QPalette originalPalette = rail->palette();
                for (const QColor highlight : {QColor(12, 116, 168), QColor(112, 204, 245)}) {
                    QPalette theme = originalPalette;
                    theme.setColor(QPalette::Highlight, highlight);
                    rail->setPalette(theme);
                    QApplication::processEvents();
                    const QImage handleImage = rail->grab().toImage();
                    bool foundHighlight = false;
                    for (int y = 0; y < handleImage.height() && !foundHighlight; ++y)
                        for (int x = 0; x < handleImage.width(); ++x)
                            if (const QColor pixel = handleImage.pixelColor(x, y);
                                qAbs(pixel.red() - highlight.red()) <= 20
                                && qAbs(pixel.green() - highlight.green()) <= 20
                                && qAbs(pixel.blue() - highlight.blue()) <= 20) { foundHighlight = true; break; }
                    require(foundHighlight, "handle renders highlight from current theme");
                }
                rail->setPalette(originalPalette);
                auto *checkpointRail = static_cast<StoryboardPositionSlider *>(rail);
                require(checkpointRail->checkpoints.size() == 2, "one checkpoint for each feature");
                const auto checkpoint = checkpointRail->checkpoints.last();
                const QPointF point(rail->width() / 2.0, checkpoint.first);
                QMouseEvent click(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(rail, &click);
                QApplication::processEvents();
                require(model->historyPosition() == checkpoint.second, "click checkpoint moves calculation after its feature");
                model->setHistoryPosition(1);
                QApplication::processEvents();
                require(tree->grab().save(QStringLiteral("/tmp/forgecad-storyboard-mode%1.png").arg(mode)), "vertical cursor visual snapshot");
                root = history();
                const QPointF cursorPoint(tree->visualItemRect(root->child(1)).center());
                const QPointF firstPoint(tree->visualItemRect(root->child(0)).center());
                QMouseEvent sidePress(QEvent::MouseButtonPress, cursorPoint, cursorPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent sideMove(QEvent::MouseMove, firstPoint, firstPoint, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent sideRelease(QEvent::MouseButtonRelease, firstPoint, firstPoint, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(tree->viewport(), &sidePress);
                QApplication::sendEvent(tree->viewport(), &sideMove);
                QApplication::sendEvent(tree->viewport(), &sideRelease);
                require(model->historyPosition() == 1, "lateral mode prevents dragging Inserisci qui row");
                model->setHistoryPosition(2);
                QApplication::processEvents();
                require(tree->grab().save(QStringLiteral("/tmp/forgecad-storyboard-mode%1-end.png").arg(mode)), "terminal rail visual snapshot");
                model->setHistoryPosition(1);
                QApplication::processEvents();
            }
            {
                PdfWindow restored;
                auto *restoredTree = dynamic_cast<StoryboardTree *>(restored.findChild<QTreeWidget *>());
                require(restoredTree && restoredTree->cursorMode() == StoryboardTree::CursorMode::Right,
                        "cursor preference restored in new window");
            }
            window.findChild<QAction *>(QStringLiteral("storyboardCursorMode0"))->trigger();
            QApplication::processEvents();
            require(tree->cursorMode() == StoryboardTree::CursorMode::Horizontal && tree->historyRail()->isHidden(),
                        "original horizontal mode restored");
        }
    }

    static void denseFixedSketchRegressions() {
        using namespace ForgeCad;
        SketchObject original;
        for (int c = 0; c < 6; ++c) {
            CurveObject curve; curve.tool = DrawingTool::Converted; curve.degree = 1;
            curve.knots.append(0.0);
            for (int i = 0; i < 1200; ++i) {
                curve.controlPoints.append(QPointF(i * 0.1, c * 10.0 + std::sin(i * 0.01)));
                curve.knots.append(i);
            }
            curve.knots.append(1199.0);
            original.curves.append(curve);
            SketchConstraint fixed; fixed.type = ConstraintType::Fix; fixed.first = {1,c,-1};
            fixed.positions = curve.controlPoints; original.geometricConstraints.append(fixed);
        }
        QElapsedTimer timer; timer.start();
        SketchObject work = original;
        work.segments.append({QPointF(0,5),QPointF(100,5.2)});
        SketchConstraint horizontal; horizontal.type = ConstraintType::Horizontal; horizontal.first = {0,0,-1};
        work.geometricConstraints.append(horizontal);
        require(solveSketch(work).ok, "segmento libero con migliaia di poli fissi");
        require(std::abs(work.segments[0].first.y()-work.segments[0].second.y()) < 1e-8, "orizzontalita' risolta");
        // Aggancio alla curva copiata: sia soluzione sia analisi devono
        // derivare solo le coordinate libere del segmento.
        const QPointF onCurve = work.curves[0].controlPoints[500];
        work.segments[0] = {onCurve + QPointF(0,0.2), onCurve + QPointF(5,0.1)};
        SketchConstraint on; on.type = ConstraintType::PointOnCurve; on.first = {0,0,0}; on.second = {1,0,-1};
        work.geometricConstraints.append(on);
        require(solveSketch(work).ok, "aggancio su curva copiata fissa");
        const auto analysis = analyzeSketch(work);
        require(analysis.degreesOfFreedom == 2, "gradi di liberta' del segmento agganciato");
        for (int c = 0; c < original.curves.size(); ++c)
            require(work.curves[c].controlPoints == original.curves[c].controlPoints, "curve fisse conservate esattamente");
        const auto before = work;
        require(!solveSketch(work, {{{1,0,500},onCurve+QPointF(1,1)}}).ok, "trascinamento di un polo fisso rifiutato");
        require(work.curves[0].controlPoints == before.curves[0].controlPoints && work.segments == before.segments,
                "conflitto non modifica lo schizzo");
        work.geometricConstraints.append(work.geometricConstraints.first());
        work.geometricConstraints.last().positions[0] += QPointF(1,0);
        require(!solveSketch(work).ok, "Fix contraddittori rifiutati");
        require(timer.elapsed() < 3000, "curve fisse non devono bloccare l'interfaccia");
        std::cout << "dense fixed interactions ms=" << timer.elapsed() << std::endl;
        // Un Fix deve anche ripristinare una coordinata alterata prima della
        // chiamata, senza ritornare prima di scrivere i punti risolti.
        auto restore = original; restore.curves[0].controlPoints[10] += QPointF(1,1);
        require(solveSketch(restore).ok && restore.curves[0].controlPoints == original.curves[0].controlPoints,
                "ripristino diretto dei poli fissi");
        SketchObject handles;
        CurveObject spline; spline.tool = DrawingTool::Spline;
        spline.controlPoints = {{0,0},{10,2},{20,0}}; recalculateCurve(spline,0); handles.curves.append(spline);
        SketchConstraint fix; fix.type = ConstraintType::Fix; fix.first = {1,0,-1}; fix.positions = spline.controlPoints;
        for (const auto &pair : spline.tangentHandles) fix.positions << pair.first << pair.second;
        handles.geometricConstraints.append(fix);
        handles.curves[0].controlPoints[0] += QPointF(2,1);
        require(solveSketch(handles).ok && handles.curves[0].controlPoints == spline.controlPoints
                    && handles.curves[0].tangentHandles == spline.tangentHandles, "Fix di spline e maniglie");
    }

    static void sketchReferenceWorkflow() {
        using namespace ForgeCad;
        {
            using namespace Kernel;
            const auto wall=[](double x) { return makePlaneSheet(Frame3(Vec3(x,0,0),Vec3(1,0,0),Vec3(0,1,0)),4); };
            const Body surface=wall(2);
            const auto ref=[](const Body &body,int owner,quint64 id) {
                GeometryRef r; r.kind=5; r.index=owner; r.featureId=id;
                r.point=faceReference(body,body.faces().front(),Vec3(2,0,0)); return r;
            };
            SketchObject sketch; sketch.name=QStringLiteral("Pierce"); sketch.segments={{{5,1},{8,1}}};
            sketch.constraints={-1}; sketch.segmentLengths={0}; sketch.segmentAngles={-1};
            SketchConstraint pierce; pierce.type=ConstraintType::Pierce; pierce.first={0,0,0};
            require(refreshSurfacePierce(sketch,pierce,surface,ref(surface,0,123),"Parete").isEmpty(),"cache Pierce sulla faccia piana");
            sketch.geometricConstraints.append(pierce);
            require(solveSketch(sketch).ok && pointDistance(sketch.segments[0].first,QPointF(2,1))<1e-8,
                    "Pierce sposta il punto sulla superficie");
            require(analyzeSketch(sketch).degreesOfFreedom==3,"Pierce lascia libero lo scorrimento sulla sezione");
            require(solveSketch(sketch,{{{0,0,0},QPointF(2,3)}}).ok,"punto Pierce scorre sulla sezione");
            const auto before=sketch;
            require(!solveSketch(sketch,{{{0,0,0},QPointF(2,7)}}).ok && sketch.segments==before.segments,
                    "Pierce non prosegue oltre il bordo rifilato");
            sketch.segments[0].first={5,8};
            require(solveSketch(sketch).ok && pointDistance(sketch.segments[0].first,QPointF(2,4))<1e-7,
                    "Pierce converge al bordo della faccia");
            SketchConstraint invalid=pierce;
            require(!refreshSurfacePierce(sketch,invalid,makePlaneSheet(Frame3(),4),ref(makePlaneSheet(Frame3(),4),0,123),"Piano").isEmpty(),
                    "Pierce complanare rifiutato");
            SketchObject missing=sketch; missing.geometricConstraints={invalid};
            require(!solveSketch(missing).ok && !std::isfinite(constraintError(missing,invalid)),"Pierce non valido non viene ignorato");
            const auto ring=[](double z) {
                auto circle=std::make_shared<Circle<3>>(Vec3(0,0,z),Vec3(1,0,0),Vec3(0,1,0),2);
                return std::vector<PathSegment>{{circle,circle->domain()}};
            };
            const Body tube=ruledSurface(ring(-3),ring(3));
            SketchObject round; round.segments={{{4,1},{6,1}}};
            SketchConstraint curved=pierce;
            require(refreshSurfacePierce(round,curved,tube,ref(tube,0,124),"Cilindro").isEmpty(),"Pierce su superficie curva");
            round.geometricConstraints={curved};
            require(solveSketch(round).ok && std::fabs(pointLength(round.segments[0].first)-2)<1e-7,
                    "punto Pierce sulla sezione circolare");
            require(analyzeSketch(round).degreesOfFreedom==3,"gradi di liberta' Pierce curvo");
            CadViewport view;
            view.sketches_.append(before); view.sketches_[0].geometricConstraints.clear();
            view.activeSketch_=0; view.sketchMode_=true;
            ExtrusionObject body; body.name="Parete"; body.feature=BodyFeature::Imported; body.featureId=123;
            body.forgeBody=std::make_shared<const Body>(surface);
            body.importData=QByteArray::fromStdString(writeStep({{"Parete",surface, false, {0.0, 0.0, 0.0}, nullptr, {}}}));
            view.extrusions_.append(body);
            const GeometryRef picked=ref(surface,0,123);
            require(view.addSurfacePierce({0,0,0},picked).isEmpty(),"vincolo Pierce dal viewport");
            require(view.sketches_[0].segments.size()==1 && view.sketches_[0].curves.isEmpty()
                    && view.sketches_[0].geometricConstraints.size()==1,"Pierce non aggiunge copie di geometria allo schizzo");
            ExtrusionObject dependent; dependent.feature=BodyFeature::Extrusion; dependent.sketchIndex=0;
            dependent.extrudeSurface=true; dependent.distance=2; dependent.name="Superficie dipendente";
            view.extrusions_.append(dependent);
            view.regenerateDependents(0);
            require(view.extrusions_[1].forgeBody && view.extrusions_[1].error.isEmpty(),"superficie generata dallo schizzo Pierce");
            const auto previousBody=view.extrusions_[1].forgeBody;
            const auto oldCache=view.sketches_[0].geometricConstraints[0].pierce;
            view.extrusions_[0].forgeBody=std::make_shared<const Body>(wall(3));
            view.extrusions_[0].importData=QByteArray::fromStdString(writeStep({{"Parete",wall(3), false, {0.0, 0.0, 0.0}, nullptr, {}}}));
            view.regenerateAfter(0);
            require(std::fabs(view.sketches_[0].segments[0].first.x()-3)<1e-8,"Pierce segue la superficie modificata");
            require(oldCache->segments[0].first.x()==2,"cache Pierce delle istantanee immutabile");
            require(view.extrusions_[1].forgeBody && view.extrusions_[1].forgeBody!=previousBody
                    && view.extrusions_[1].error.isEmpty(),"rigenerazione a cascata dal vincolo Pierce");
            QTemporaryDir dir; DocumentState loaded;
            const QString path=dir.filePath("pierce.prt");
            require(saveDocumentFile(path,view.documentState(),false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),"persistenza Pierce formato 41");
            require(loaded.sketches[0].geometricConstraints.size()==1 && loaded.sketches[0].geometricConstraints[0].type==ConstraintType::Pierce
                    && loaded.sketches[0].geometricConstraints[0].pierce->surface.featureId==123
                    && solveSketch(loaded.sketches[0]).ok,"riferimento e sezione Pierce dopo riapertura");
            CadViewport reloaded;
            reloaded.restoreDocument(loaded); reloaded.regenerateAll();
            require(reloaded.extrusions_[1].forgeBody && reloaded.extrusions_[1].error.isEmpty()
                    && constraintError(reloaded.sketches_[0],reloaded.sketches_[0].geometricConstraints[0])<1e-8,
                    "rigenerazione completa Pierce dopo riapertura");
            reloaded.sketches_[0].customFrame=true; reloaded.sketches_[0].frame.origin[0]=1;
            reloaded.regenerateDependents(0);
            require(std::fabs(reloaded.sketches_[0].segments[0].first.x()-2)<1e-8,"Pierce segue il piano dello schizzo modificato");
            view.undo();
            require(view.sketches_[0].geometricConstraints.isEmpty(),"Undo aggiunta Pierce");
            view.redo();
            require(view.sketches_[0].geometricConstraints.size()==1 && view.sketches_[0].geometricConstraints[0].type==ConstraintType::Pierce,
                    "Redo aggiunta Pierce");
            view.selectedPoints_={{0,0,0}};
            require(view.beginSurfacePierce().isEmpty() && view.referencePicking() && view.sketchViewUnlocked_,"selezione faccia Pierce nello schizzo");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
            QApplication::sendEvent(&view,&escape);
            require(!view.referencePicking() && !view.sketchViewUnlocked_ && !view.refPickFinished_,"Esc Pierce ripristina la vista");
            view.extrusions_[0].sketchIndex=0; view.extrusions_[0].feature=BodyFeature::Extrusion;
            require(!view.addSurfacePierce({0,0,1},picked).isEmpty(),"dipendenza circolare Pierce rifiutata");
            view.extrusions_[0].sketchIndex=-1; view.extrusions_[0].feature=BodyFeature::Imported;
            view.removeBodies({0});
            require(!view.sketches_[0].geometricConstraints[0].pierce->error.isEmpty()
                    && !solveSketch(view.sketches_[0]).ok,"superficie Pierce eliminata segnalata");
        }
        {
            using namespace Kernel;
            const Body wall = makePlaneSheet(Frame3(Vec3(), Vec3(1,0,0), Vec3(0,1,0)), 4);
            SketchObject section;
            require(appendSectionCurves(section, wall, true, true).isEmpty(), "sezione aperta della superficie");
            require(section.segments.size()==1 && section.curves.isEmpty(), "sezione limitata ai bordi della superficie");
            require(std::fabs(pointDistance(section.segments[0].first, section.segments[0].second)-8)<1e-7,
                    "lunghezza esatta della sezione aperta");
            require(section.constructionSegments.size()==1 && section.geometricConstraints.size()==1,
                    "sezione superficie di costruzione e fissa");
            SketchObject coplanar;
            require(appendSectionCurves(coplanar, makePlaneSheet(Frame3(),4), false, true).isEmpty()
                    && coplanar.segments.size()==4, "contorno superficie complanare");
            SketchObject outside; outside.customFrame=true; outside.frame.origin[2]=10;
            require(!appendSectionCurves(outside,wall,true,true).isEmpty() && outside.segments.isEmpty(),
                    "superficie fuori dal piano senza riferimenti");
            const auto ring = [](double z) {
                auto circle=std::make_shared<Circle<3>>(Vec3(0,0,z),Vec3(1,0,0),Vec3(0,1,0),2);
                return std::vector<PathSegment>{{circle,circle->domain()}};
            };
            const Body cylinder=ruledSurface(ring(-3),ring(3));
            SketchObject curved;
            require(appendSectionCurves(curved,cylinder,false,true).isEmpty() && !curved.curves.isEmpty(),
                    "sezione circolare della superficie curva");
            for(const auto &curve : curved.curves) for(const auto &piece : curveGeometry(curve))
                for(int k=0;k<=20;++k) {
                    const auto point=piece.curve->point(piece.range.lo+piece.range.length()*k/20.0);
                    require(std::fabs(norm(point)-2)<1e-7,"raggio della sezione curva");
                }
            CadViewport viewport;
            viewport.sketches_.append(SketchObject()); viewport.activeSketch_=0; viewport.sketchMode_=true;
            ExtrusionObject sheet; sheet.feature=BodyFeature::Ruled; sheet.solid=false; sheet.visible=true;
            sheet.forgeBody=std::make_shared<const Body>(wall); viewport.extrusions_.append(sheet);
            int added=0;
            require(viewport.addSectionReferences(&added).isEmpty() && added==1,
                    "comando viewport include le superfici visibili");
        }
        {
            SketchObject source;
            CurveObject spline; spline.tool = DrawingTool::Spline;
            spline.controlPoints = {{-6,0},{-2,3},{2,-1},{6,0}};
            recalculateCurve(spline); source.curves.append(spline);
            source.segments.append({QPointF(-2,1),QPointF(3,2)});
            for (bool construction : {false,true}) {
                SketchObject target; target.customFrame = true; target.frame.origin[2] = 25;
                require(!appendSketchContactReference(target,source,{1,0}).isEmpty(), "parallel planes have no contacts");
                require(appendProjectedSketchEntity(target,source,{1,0},construction).isEmpty(), "convert spline onto offset plane");
                require(target.curves.size()==1 && target.segments.isEmpty()
                        && target.curves[0].construction==construction, "selected spline and construction setting preserved");
                const auto before=curveGeometry(spline),after=curveGeometry(target.curves[0]);
                require(before.size()==after.size(),"projected spline pieces");
                for(std::size_t i=0;i<before.size();++i) for(int k=0;k<=30;++k) {
                    const auto a=before[i].curve->point(before[i].range.lo+before[i].range.length()*k/30.0);
                    const auto b=after[i].curve->point(after[i].range.lo+after[i].range.length()*k/30.0);
                    require(Kernel::distance(a,b)<1e-8,"parallel projection preserves spline exactly");
                }
                require(solveSketch(target).ok,"projected spline fixed constraints valid");
                require(appendProjectedSketchEntity(target,source,{0,0},construction).isEmpty()
                        && target.segments.size()==1 && target.isConstructionSegment(0)==construction,
                        "convert selected segment and respect construction setting");
            }
            CadViewport view; view.resize(800,600);
            SketchObject target; target.customFrame=true; target.frame.origin[2]=25;
            view.sketches_={source,target}; view.activeSketch_=1; view.sketchMode_=true;
            view.referencesConstruction_=false;
            const QPoint click=view.projectWorldPoint(sketchToDisplay(spline.samples.at(spline.samples.size()/2),source)).toPoint();
            require(view.convertReferenceAt(click).isEmpty(),"viewport converts spline from parallel sketch");
            require(view.sketches_[1].curves.size()==1 && !view.sketches_[1].curves[0].construction,
                    "viewport uses projection and respects reference setting");
        }
        {
            CadViewport view; view.resize(800,600);
            SketchObject source, target;
            source.customFrame = true;
            source.frame.normal[0]=0; source.frame.normal[1]=1; source.frame.normal[2]=0;
            source.frame.xAxis[0]=1; source.frame.xAxis[1]=0; source.frame.xAxis[2]=0;
            CurveObject circle; circle.tool=DrawingTool::Circle;
            circle.controlPoints={{5,0},{8,0}}; recalculateCurve(circle);
            source.curves.append(circle);
            view.sketches_={source,target}; view.activeSketch_=1; view.sketchMode_=true;
            const QPoint click=view.projectWorldPoint(sketchToDisplay(QPointF(8,0),source)).toPoint();
            require(view.convertReferenceAt(click).isEmpty(), "convert crossing circle contacts");
            const auto &contacts=view.sketches_[1];
            require(contacts.curves.isEmpty() && contacts.segments.size()==2, "circle produces two intersection points rather than projection");
            for(const auto &segment:contacts.segments)
                require(segment.first==segment.second && std::abs(segment.first.y())<1e-8
                        && (std::abs(segment.first.x()-2)<1e-8 || std::abs(segment.first.x()-8)<1e-8), "exact circle intersections");
        }
        {
            CadViewport view; view.resize(800,600);
            SketchObject sketch;
            sketch.segments={{{2,3},{12,3}},{{6,5},{7,8}}};
            sketch.constraints={-1,-1}; sketch.segmentLengths={0,0}; sketch.segmentAngles={-1,-1};
            sketch.geometricConstraints.append(makeConstraint(sketch,ConstraintType::Fix,{{0,0,-1}}));
            view.sketches_={sketch}; view.activeSketch_=0; view.sketchMode_=true;
            require(view.segmentMidpointAt({7,3})==0, "midpoint hover identifies segment");
            require(view.selectPointWithControl({6,5}), "select endpoint with Ctrl");
            require(view.selectPointWithControl({7,3}), "select midpoint with Ctrl");
            require(applicableConstraints(view.sketches_[0],view.constraintSelection()).contains(ConstraintType::Midpoint), "Ctrl midpoint selection offers midpoint constraint");
            require(view.addConstraint(ConstraintType::Midpoint).isEmpty(), "apply midpoint constraint");
            require(pointDistance(view.sketches_[0].segments[1].first,QPointF(7,3))<1e-8, "solver places endpoint at fixed segment midpoint");
            require(view.selectPointWithControl({7,3}), "midpoint selection remains usable after solve");
        }
        CurveObject reference;
        reference.tool = DrawingTool::Converted;
        reference.construction = true;
        reference.degree = 3;
        reference.controlPoints = {QPointF(-6,0),QPointF(-2,0),QPointF(2,0),QPointF(6,0)};
        reference.knots = {0,0,0,0,1,1,1,1};
        for (bool arcCutter : {false,true}) {
            SketchObject sketch;
            sketch.curves.append(reference);
            sketch.geometricConstraints.append(makeConstraint(sketch,ConstraintType::Fix,{{1,0,-1}}));
            if (arcCutter) {
                CurveObject arc; arc.tool=DrawingTool::Arc;
                arc.controlPoints={QPointF(0,0),QPointF(0,-2),QPointF(0,2)};
                sketch.curves.append(arc);
            } else sketch.segments.append({QPointF(2,-3),QPointF(2,3)});
            require(!trimPreview(sketch,{1,0},QPointF(5,0)).isEmpty(),"anteprima taglio riferimento");
            require(trimSketchEntity(sketch,{1,0},QPointF(5,0)).error.isEmpty(),"taglio riferimento con segmento/arco");
            require(sketch.curves[0].tool==DrawingTool::Converted && sketch.curves[0].construction,
                    "riferimento tagliato come curva derivata di costruzione");
            const auto g=curveGeometry(sketch.curves[0]).front();
            require(Kernel::distance(g.start(),Kernel::Vec2(-6,0))<1e-7 && Kernel::distance(g.end(),Kernel::Vec2(2,0))<1e-7,
                    "estremi esatti del riferimento tagliato");
            for(const auto &c:sketch.geometricConstraints) require(c.type!=ConstraintType::Fix,"Fix obsoleto dopo taglio");
        }
        // Lo stesso bordo copiato due volte non e' un anello con inversione a U.
        SketchObject duplicates; duplicates.curves={reference,reference};
        SketchOffset duplicateOffset;duplicateOffset.distance=0.5;
        QVector<SketchEntity> duplicateMade;
        require(offsetSketchEntities(duplicates,{{1,0},{1,1}},duplicateOffset,&duplicateMade).error.isEmpty()
                && duplicateMade.size()==1 && duplicates.curves.size()==3,"offset riferimenti duplicati");
        require(duplicates.curves[0].controlPoints==reference.controlPoints
                && duplicates.curves[1].controlPoints==reference.controlPoints,"riferimenti duplicati conservati");
        SketchObject sketch; sketch.curves.append(reference);
        SketchOffset offset; offset.distance=0.5;
        QVector<SketchEntity> made;
        require(offsetSketchEntities(sketch,{{1,0}},offset,&made).error.isEmpty() && made.size()==1,"offset riferimento 0,5 mm");
        require(sketch.curves[0].construction && !sketch.curves[1].construction && sketch.curves[1].tool==DrawingTool::Converted,
                "offset come curva derivata e riferimento di costruzione conservato");
        const auto g=curveGeometry(sketch.curves[1]).front();
        for(int k=0;k<=40;++k) require(std::fabs(g.curve->point(g.range.lo+g.range.length()*k/40.0).y()-0.5)<1e-7,"distanza offset");
        sketch.segments.append({QPointF(2,-3),QPointF(2,3)});
        require(trimSketchEntity(sketch,{1,1},QPointF(5,0.5)).error.isEmpty(),"taglio offset");
        // Completa il contorno con segmenti: il riferimento di costruzione
        // deve restare escluso dall'utensile di modellazione.
        sketch.segments.clear();
        sketch.segments.append({QPointF(2,0.5),QPointF(2,3)});
        sketch.segments.append({QPointF(2,3),QPointF(-6,3)});
        sketch.segments.append({QPointF(-6,3),QPointF(-6,0.5)});
        require(forgeSketchSegments(sketch).size()==4,"profilo utensile senza riferimenti di costruzione");
        QString extrusionError;
        const auto tool=forgeExtrusion(sketch,5,&extrusionError);
        require(tool && extrusionError.isEmpty() && !tool->isSheet(),"profilo composto utilizzabile per estrusione");
        QTemporaryDir directory;
        DocumentState state,loaded;state.sketches={sketch};
        const QString path=directory.filePath(QStringLiteral("references.prt"));
        require(saveDocumentFile(path,state,false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),"persistenza riferimenti e offset");
        require(loaded.sketches[0].curves[0].construction && !loaded.sketches[0].curves[1].construction
                && loaded.sketches[0].curves[1].tool==DrawingTool::Converted,"tipo e costruzione dopo riapertura");
    }

    static void inwardOffsetAndProjection() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        SketchObject sketch;
        CurveObject parabola;parabola.tool=DrawingTool::Converted;parabola.degree=2;parabola.construction=true;
        parabola.knots={0,0,0,1,1,1};parabola.controlPoints={{-2,4},{0,-4},{2,4}};
        sketch.curves={parabola};
        const auto source=curveGeometry(parabola).front();
        SketchOffset options;options.distance=1;options.dimensioned=true;
        require(offsetSketchEntities(sketch,{{1,0}},options).error.isEmpty(),"offset interno oltre il minimo raggio locale");
        const auto check=[&](double distance) {
            const auto result=curveGeometry(sketch.curves[1]).front();
            double previous=-1e300;
            for(int k=0;k<=500;++k) {
                const auto point=result.curve->point(result.range.lo+result.range.length()*k/500.0);
                require(point.x()>=previous-1e-8,"offset interno senza anello ripiegato");previous=point.x();
                require(point.y()>=distance*distance+0.25-1e-6,"anello oltre le cuspidi rimosso");
                require(std::fabs(projectPoint(*source.curve,point,source.range).distance-distance)<2e-7,"distanza vera dell'offset rifilato");
            }
        };
        check(1);
        {
            SketchObject collapsed;CurveObject circle;circle.tool=DrawingTool::Converted;
            const auto rational=toBSpline(makeCircle(Vec2(),0.5),0,2*M_PI);
            circle.degree=rational.degree();
            for(const auto &p:rational.poles()) circle.controlPoints.append(QPointF(p.x(),p.y()));
            for(double t:rational.knots()) circle.knots.append(t);
            for(double w:rational.weights()) circle.weights.append(w);
            collapsed.curves={circle};auto inward=options;inward.reverse=true;
            require(!offsetSketchEntities(collapsed,{{1,0}},inward).error.isEmpty() && collapsed.curves.size()==1,
                    "offset NURBS collassato non riappare dal lato opposto");
        }
        sketch.geometricConstraints.last().value=1.1;
        require(solveSketch(sketch).ok,"quota dell'offset interno con cuspidi");check(1.1);
        // Curva spaziale procedurale senza forma razionale esposta dal tipo.
        struct Procedural final : Curve<3> {
            CurveType type() const override{return CurveType::Other;}
            Interval domain() const override{return {-1,1};}
            void evaluate(double t,int order,Vec3 *out) const override {
                out[0]=Vec3(t,t*t,t*t*t);
                if(order>=1)out[1]=Vec3(1,2*t,3*t*t);
                if(order>=2)out[2]=Vec3(0,2,6*t);
                if(order>=3)out[3]=Vec3(0,0,6);
                for(int i=4;i<=order;++i)out[i]=Vec3();
            }
        };
        const auto basis=std::make_shared<Procedural>();
        const auto moved=std::make_shared<TransformedCurve>(basis,Transform3::translation(Vec3(3,5,7)));
        for(const CurvePtr<3> &curve:{CurvePtr<3>(basis),CurvePtr<3>(moved)}) for(int plane:{0,1,2,3}) {
            SketchObject projection;projection.plane=plane;
            if(plane==3){projection.customFrame=true;projection.frame=faceSketchFrame(Vec3(1,2,3),normalized(Vec3(1,1,1)),Vec3(0,0,1));}
            require(appendProjectedCurve(projection,curve,curve->domain(),true,true).isEmpty(),"proiezione di curva procedurale complessa");
            require(projection.curves.size()==1 && projection.curves[0].construction,"riferimento proiettato di costruzione");
            const auto projected=curveGeometry(projection.curves[0]).front();
            for(int k=0;k<=100;++k) {
                const auto world=curve->point(-1+2*k/100.0);
                const auto p=worldToSketch(world,projection);
                require(projectPoint(*projected.curve,Vec2(p.x(),p.y()),projected.range).distance<2e-7,"curva nel piano: proiezione ortogonale corretta");
            }
        }
    }

    // Copia di offset di un bordo copiato: curva derivata con molti poli,
    // usata come un'unica entita'. Ctrl+clic e snap non vedono i poli interni,
    // la curva resta rigida nel risolutore e un segmento che termina su di essa
    // (punto sulla curva) la taglia proprio li'.
    static void derivedCurveEntity() {
        using namespace ForgeCad;
        CurveObject reference;
        reference.tool = DrawingTool::Converted; reference.construction = true; reference.degree = 3;
        // Bordo ondulato: la copia a distanza richiede molti poli.
        const int poles = 40;
        for (int k = 0; k < poles; ++k) reference.controlPoints.append(QPointF(-10.0 + 20.0 * k / (poles - 1), 0.6 * std::sin(k * 0.7)));
        reference.knots = {0, 0, 0, 0};
        for (int k = 1; k <= poles - 4; ++k) reference.knots.append(double(k));
        for (int k = 0; k < 4; ++k) reference.knots.append(double(poles - 3));
        CadViewport v; v.createSketch(0, QStringLiteral("Derivata"));
        v.sketchMode_ = true; v.activeSketch_ = 0;
        SketchObject &sketch = v.sketches_[0];
        sketch.curves.append(reference);
        sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{1, 0, -1}}));
        SketchOffset offset; offset.distance = 0.5;
        QVector<SketchEntity> made;
        require(offsetSketchEntities(sketch, {{1, 0}}, offset, &made).error.isEmpty() && made.size() == 1, "offset del bordo copiato");
        const int copy = made.first().index;
        CurveObject &derived = sketch.curves[copy];
        derived.samples.clear(); recalculateCurve(derived, 2);
        require(derived.tool == DrawingTool::Converted && derived.controlPoints.size() > 8, "copia come curva derivata con molti poli");
        const QVector<QPointF> shape = derived.controlPoints;
        const int last = int(shape.size()) - 1;
        // Ctrl+clic su un polo interno: nessun punto (si sceglie l'entita'); sull'estremo: il punto.
        const QPointF interior = shape.at(last / 2);
        v.selectedPoints_.clear();
        const bool pickedInterior = v.selectPointWithControl(interior);
        require(!pickedInterior || v.selectedPoints_.isEmpty() || v.selectedPoints_.first().element != copy
                    || v.selectedPoints_.first().point == 0 || v.selectedPoints_.first().point == last,
                "Ctrl+clic non sceglie i poli interni della curva derivata");
        v.selectedPoints_.clear();
        require(v.selectPointWithControl(shape.first()) && v.selectedPoints_.size() == 1
                    && v.selectedPoints_.first().element == copy && v.selectedPoints_.first().point == 0,
                "Ctrl+clic sceglie l'estremo della curva derivata");
        v.selectedPoints_.clear();
        // Lo snap non propone i poli interni.
        for (const QPointF &p : v.snapCandidates(sketch))
            for (int k = 1; k < last; ++k)
                require(pointDistance(p, shape.at(k)) > 1e-12, "snap su un polo interno della curva derivata");
        // Gradi di liberta': la curva derivata non ne aggiunge.
        require(analyzeSketch(sketch).degreesOfFreedom == 0, "curva derivata rigida: nessun grado di liberta'");
        // Segmento che parte nel vuoto e finisce sulla curva (punto sulla curva):
        // il risolutore muove il segmento, non la curva.
        const auto g = curveGeometry(derived).front();
        const auto mid = g.curve->point(g.range.lo + 0.37 * g.range.length());
        const QPointF foot(mid.x(), mid.y());
        sketch.segments.append({foot + QPointF(0.3, 4.0), foot + QPointF(0.05, 0.02)});
        sketch.constraints.append(-1); sketch.segmentLengths.append(0.0); sketch.segmentAngles.append(-1.0);
        const int segment = int(sketch.segments.size()) - 1;
        SketchConstraint on; on.type = ConstraintType::PointOnCurve; on.first = {0, segment, 1}; on.second = {1, copy, -1};
        sketch.geometricConstraints.append(on);
        const SolveResult solved = solveSketch(sketch, {});
        require(solved.ok, "punto sulla curva derivata risolto");
        require(sketch.curves[copy].controlPoints == shape, "la curva derivata non si deforma");
        const auto onCurve = Kernel::projectPoint(*g.curve, Kernel::Vec2(sketch.segments[segment].second.x(), sketch.segments[segment].second.y()), g.range);
        require(onCurve.distance < 1e-9, "estremo del segmento sulla curva derivata");
        require(analyzeSketch(sketch).degreesOfFreedom == 3, "gradi di liberta' del solo segmento");
        // Taglio della curva derivata contro il segmento che vi termina.
        const QPointF cut = sketch.segments[segment].second;
        const auto right = g.curve->point(g.range.lo + 0.8 * g.range.length());
        require(trimSketchEntity(sketch, {1, copy}, QPointF(right.x(), right.y())).error.isEmpty(), "taglio nel punto del segmento");
        int piece = -1;
        for (int k = 0; k < sketch.curves.size(); ++k)
            if (sketch.curves[k].tool == DrawingTool::Converted && !sketch.curves[k].construction) piece = k;
        require(piece >= 0, "il tratto tagliato resta una curva derivata");
        const auto trimmed = curveGeometry(sketch.curves[piece]).front();
        const Kernel::Vec2 c(cut.x(), cut.y());
        require(std::min(Kernel::distance(trimmed.start(), c), Kernel::distance(trimmed.end(), c)) < 1e-7, "estremo del tratto nel punto di taglio");
        // Il trascinamento di un estremo coincidente non sposta i poli della curva derivata.
        const QVector<QPointF> before = sketch.curves[piece].controlPoints;
        v.moveSketchPoint(sketch, -1, cut, QPointF(1.0, 1.0), QPointF(qQNaN(), qQNaN()));
        require(sketch.curves[piece].controlPoints == before, "la curva derivata non segue il trascinamento");
    }

    static void associativeOffset() {
        using namespace ForgeCad;
        SketchObject sketch;
        CurveObject reference;reference.tool=DrawingTool::Converted;reference.degree=3;reference.construction=true;
        reference.controlPoints={{-6,0},{-2,0},{2,0},{6,0}};reference.knots={0,0,0,0,1,1,1,1};
        sketch.curves={reference};
        sketch.geometricConstraints.append(makeConstraint(sketch,ConstraintType::Fix,{{1,0,-1}}));
        SketchOffset offset;offset.distance=.5;offset.dimensioned=true;
        QVector<SketchEntity> made;
        require(offsetSketchEntities(sketch,{{1,0}},offset,&made).error.isEmpty(),"creazione quota offset");
        const int dimension=sketch.geometricConstraints.size()-1;
        require(sketch.geometricConstraints[dimension].type==ConstraintType::Offset,"vincolo Offset persistente");
        require(solveSketch(sketch).ok,"risoluzione offset associativo");
        sketch.geometricConstraints[dimension].value=.8;
        require(solveSketch(sketch).ok,"modifica distanza offset");
        for(const auto &p:sketch.curves[1].controlPoints) require(std::fabs(p.y()-.8)<1e-7,"offset aggiornato alla quota");
        QPointF a,b;
        require(dimensionPoints(sketch,sketch.geometricConstraints[dimension],a,b) && std::fabs(QLineF(a,b).length()-.8)<1e-7,"freccia della quota offset");
        require(analyzeSketch(sketch).degreesOfFreedom==0,"offset determinato dal riferimento fisso");
        const auto before=sketch;
        require(!solveSketch(sketch,{{{1,1,0},QPointF(-6,2)}}).ok && sketch.curves[1].controlPoints==before.curves[1].controlPoints,
                "trascinamento incompatibile rifiutato senza deformare offset");
        QTemporaryDir directory;DocumentState state,loaded;state.sketches={sketch};
        const auto file=directory.filePath(QStringLiteral("offset-quota.prt"));
        require(saveDocumentFile(file,state,false).isEmpty() && loadDocumentFile(file,loaded).isEmpty(),"persistenza quota offset");
        sketch=loaded.sketches[0];sketch.geometricConstraints[dimension].value=1.2;
        require(solveSketch(sketch).ok && std::fabs(sketch.curves[1].controlPoints[0].y()-1.2)<1e-7,"quota offset modificabile dopo riapertura");
        // Sorgente libera modificata: la relazione rigenera la copia.
        sketch.geometricConstraints.removeFirst();
        for(auto &p:sketch.curves[0].controlPoints) p+=QPointF(0,2);
        require(solveSketch(sketch).ok && std::fabs(sketch.curves[1].controlPoints[0].y()-3.2)<1e-7,"offset segue sorgente modificata");
        require(constraintError(sketch,sketch.geometricConstraints[0])<1e-7,"residuo offset");
        // Fallimento atomico quando un cerchio collassa.
        SketchObject circular;CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{0,0},{2,0}};circular.curves={circle};
        offset.reverse=true;offset.distance=.5;
        require(offsetSketchEntities(circular,{{1,0}},offset).error.isEmpty(),"offset parametrico cerchio");
        const auto saved=circular.curves[1].controlPoints;
        circular.geometricConstraints.last().value=3;
        require(!solveSketch(circular).ok && circular.curves[1].controlPoints==saved,"offset impossibile atomico");
    }

    // Diagnostica: taglio e curve con la proiezione di uno schizzo su un corpo
    // del documento (nessun salvataggio). PROJECTION_OUT=prefisso salva i body.
    static void profileProjection(const QString &path, const QString &sketchName, int bodyIndex) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura del documento");
        int sketch = -1;
        for (int i = 0; i < document.sketches.size(); ++i)
            if (document.sketches[i].name == sketchName) sketch = i;
        require(sketch >= 0, "schizzo presente");
        require(bodyIndex >= 0 && bodyIndex < document.extrusions.size() && document.extrusions[bodyIndex].forgeBody, "corpo con geometria");
        const ForgeBody body = document.extrusions[bodyIndex].forgeBody;
        double area0 = 0.0;
        for (Kernel::FaceId f : body->faces()) area0 += Kernel::faceArea(*body, f, 1e-9);
        std::cout << "corpo " << bodyIndex << " facce " << body->faces().size() << " area " << area0 << std::endl;
        {
            const Kernel::Profile profile = Kernel::buildProfile(forgeSketchSegments(document.sketches[sketch]), kSketchConnectionTolerance);
            double planArea = 0.0;
            for (const auto &region : profile.regions) planArea += Kernel::area(region);
            std::cout << "regioni " << profile.regions.size() << " area in pianta della regione " << planArea << std::endl;
        }
        const QStringList modes = qEnvironmentVariable("PROJECTION_MODES", QStringLiteral("0,1,2")).split(QLatin1Char(','));
        for (const QString &modeText : modes) {
            const int mode = modeText.toInt();
            QElapsedTimer timer;
            timer.start();
            QString error, summary;
            const ForgeBody result = forgeProjectedCut(body, document.sketches[sketch], mode, false, &error, &summary);
            std::cout << "modo " << mode << " " << timer.elapsed() << " ms ";
            if (!result) {
                std::cout << "ERRORE " << error.toStdString() << std::endl;
                continue;
            }
            double area = 0.0;
            for (Kernel::FaceId f : result->faces()) area += Kernel::faceArea(*result, f, 1e-9);
            double tolerance = 0.0;
            for (Kernel::EdgeId e : result->edges()) tolerance = std::max(tolerance, result->edge(e).tolerance);
            const auto issues = Kernel::checkBody(*result);
            Kernel::TessellationOptions options;
            options.deflection = 1e-2;
            std::cout << "facce " << result->faces().size() << " area " << area << " (tolta " << area0 - area << ") tolleranza edge " << tolerance
                      << " controlli " << issues.size() << " tassellazione fallita " << Kernel::tessellate(*result, options).failedFaces << " | "
                      << summary.toStdString() << std::endl;
            if (qEnvironmentVariableIsSet("PROJECTION_OUT")) {
                std::ofstream file(qEnvironmentVariable("PROJECTION_OUT").toStdString() + QString::number(mode).toStdString() + ".body", std::ios::binary);
                file << Kernel::writeBodyBinary(*result);
            }
        }
        // Copia del documento con il taglio aggiunto dal viewport (nessuna modifica all'originale).
        if (qEnvironmentVariableIsSet("PROJECTION_DOC")) {
            CadViewport viewport;
            viewport.loadDocument(document);
            ExtrusionObject cut;
            cut.feature = BodyFeature::SheetTrim;
            cut.firstBody = bodyIndex;
            cut.sketchIndex = sketch;
            cut.trimProject = true;
            const QString error = viewport.createSheetTrim(cut, QStringLiteral("Taglio proiettato 1"));
            std::cout << "feature nel documento: " << (error.isEmpty() ? std::string("riuscita") : error.toStdString()) << std::endl;
            require(error.isEmpty() && saveDocumentFile(qEnvironmentVariable("PROJECTION_DOC"), viewport.currentDocument(), true).isEmpty(), "copia con il taglio");
            if (qEnvironmentVariableIsSet("PROJECTION_STEP")) {
                std::ofstream file(qEnvironmentVariable("PROJECTION_STEP").toStdString());
                file << Kernel::writeStep({{"taglio", *viewport.extrusions_.last().forgeBody, false, {0.0, 0.0, 0.0}, nullptr, {}}});
            }
        }
        QElapsedTimer timer;
        timer.start();
        QString error, summary;
        const QVector<ForgeCurve> curves = forgeProjectedCurves(body, document.sketches[sketch], false, &error, &summary);
        std::cout << "curve " << timer.elapsed() << " ms " << curves.size() << " " << error.toStdString() << summary.toStdString() << std::endl;
        for (const ForgeCurve &curve : curves) {
            double worst = 0.0;
            const auto domain = curve->domain();
            for (int k = 0; k <= 400; ++k) {
                const Kernel::Vec3 p = curve->point(domain.lo + domain.length() * k / 400.0);
                double best = 1e300;
                for (Kernel::FaceId f : body->faces()) {
                    if (Kernel::classifyPointOnFace(*body, f, p, 1e-6) == Kernel::PointLocation::Outside) continue;
                    best = std::min(best, Kernel::projectPoint(*body->face(f).surface, p).distance);
                }
                worst = std::max(worst, best);
            }
            std::cout << "  curva lunghezza " << Kernel::arcLength(*curve, domain, 1e-9) << " distanza massima dal corpo " << worst << std::endl;
        }
    }

    // Diagnostica: Spessore sul corpo del documento, come nell'app (env
    // THICKEN_CUT_SKETCH=nome: prima il taglio con lo schizzo proiettato;
    // THICKEN_DOC=copia.prt salva la copia). Nessuna modifica all'originale.
    static void profileThicken(const QString &path, int bodyIndex, double thickness, int side) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura del documento");
        CadViewport viewport;
        viewport.loadDocument(document);
        int base = bodyIndex;
        QElapsedTimer timer;
        if (qEnvironmentVariableIsSet("THICKEN_CUT_SKETCH")) {
            int sketch = -1;
            for (int i = 0; i < viewport.sketches_.size(); ++i)
                if (viewport.sketches_[i].name == qEnvironmentVariable("THICKEN_CUT_SKETCH")) sketch = i;
            require(sketch >= 0, "schizzo del taglio");
            ExtrusionObject cut;
            cut.firstBody = bodyIndex; cut.sketchIndex = sketch; cut.trimProject = true;
            timer.start();
            const QString error = viewport.createSheetTrim(cut, QStringLiteral("Taglio proiettato 1"));
            std::cout << "taglio proiettato " << timer.elapsed() << " ms " << (error.isEmpty() ? std::string("riuscito") : error.toStdString()) << std::endl;
            require(error.isEmpty(), "taglio");
            base = int(viewport.extrusions_.size()) - 1;
        }
        ExtrusionObject thick;
        thick.feature = BodyFeature::Thicken; thick.firstBody = base; thick.distance = thickness; thick.thickenSide = side;
        thick.name = QStringLiteral("Spessore 1");
        timer.start();
        const QString error = viewport.createBody(thick);
        std::cout << "spessore " << thickness << " lato " << side << ": " << timer.elapsed() << " ms " << (error.isEmpty() ? std::string("riuscito") : error.toStdString()) << std::endl;
        require(error.isEmpty(), "spessore");
        const ExtrusionObject &result = viewport.extrusions_.last();
        const Kernel::Body &solid = *result.forgeBody;
        double tolerance = 0.0;
        for (Kernel::EdgeId e : solid.edges()) tolerance = std::max(tolerance, solid.edge(e).tolerance);
        Kernel::TessellationOptions options;
        options.deflection = 1e-2;
        std::cout << "solido " << result.solid << " facce " << solid.faces().size() << " controlli " << Kernel::checkBody(solid).size()
                  << " tolleranza edge " << tolerance << " tassellazione fallita " << Kernel::tessellate(solid, options).failedFaces
                  << " stesso corpo logico " << (result.modelBodyId == viewport.extrusions_.at(base).modelBodyId) << std::endl;
        // Coerenza: dal centro delle facce della superficie, a meta' spessore dentro, oltre fuori.
        const Kernel::Body &sheet = *viewport.extrusions_.at(base).forgeBody;
        Kernel::SolidClassifier classifier(solid, 1e-7);
        int ok = 0, bad = 0;
        const double sign = side == 1 ? -1.0 : 1.0, offset = side == 2 ? 0.0 : 0.5;
        for (Kernel::FaceId f : sheet.faces()) {
            const Kernel::Surface &surface = *sheet.face(f).surface;
            const Kernel::Interval U = surface.uDomain(), V = surface.vDomain();
            for (int i = 1; i < 6; ++i)
                for (int j = 1; j < 6; ++j) {
                    const double u = U.lo + U.length() * i / 6.0, v = V.lo + V.length() * j / 6.0;
                    const Kernel::Vec3 p = surface.point(u, v);
                    if (Kernel::classifyPointOnFace(sheet, f, p, 1e-7) != Kernel::PointLocation::Inside) continue;
                    Kernel::Vec3 n = Kernel::normalAt(surface, u, v);
                    if (!sheet.face(f).sense) n = -1.0 * n;
                    const bool inside = classifier.classify(p + sign * offset * thickness * n) == Kernel::PointLocation::Inside;
                    const bool beyond = classifier.classify(p + sign * (side == 2 ? 0.7 : 1.2) * thickness * n) == Kernel::PointLocation::Outside;
                    (inside && beyond) ? ++ok : ++bad;
                }
        }
        std::cout << "campioni coerenti " << ok << ", incoerenti " << bad << std::endl;
        if (qEnvironmentVariableIsSet("THICKEN_DOC"))
            require(saveDocumentFile(qEnvironmentVariable("THICKEN_DOC"), viewport.currentDocument(), true).isEmpty(), "copia salvata");
    }

    static void profileMouseOffset(const QString &path) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path,document).isEmpty(),"lettura Mouse");
        int index=-1;
        for(int i=0;i<document.sketches.size();++i)
            if(document.sketches[i].name==QStringLiteral("Schizzo 4")) index=i;
        require(index>=0,"Schizzo 4 presente");
        const auto original=document.sketches[index];
        for(int i=0;i<original.curves.size();++i) {
            const auto geometry=curveGeometry(original.curves[i]);
            require(!geometry.empty(),"geometria curva");
            const auto g=geometry.front();
            double curvature=0;
            for(int k=0;k<=1024;++k) {
                Kernel::Vec2 d[3];g.curve->evaluate(g.range.lo+g.range.length()*k/1024.0,2,d);
                const double speed=Kernel::norm(d[1]);
                curvature=std::max(curvature,std::fabs(Kernel::cross(d[1],d[2]))/(speed*speed*speed));
            }
            std::cout<<"CURVE "<<i<<" minimo raggio campionato="<<1/curvature<<std::endl;
        }
        for(int i=0;i<=original.curves.size();++i) for(bool reverse:{false,true}) {
            auto work=original; SketchOffset offset; offset.distance=0.5; offset.reverse=reverse;
            QElapsedTimer timer;timer.start();
            QVector<SketchEntity> selected;
            if(i==original.curves.size()) for(int j=0;j<i;++j) selected.append({1,j});
            else selected.append({1,i});
            std::cout<<"START offset "<<i<<" reverse="<<reverse<<std::endl;
            QVector<SketchEntity> made;
            const auto result=offsetSketchEntities(work,selected,offset,&made);
            std::cout<<"DONE "<<timer.elapsed()<<" ms error="<<result.error.toStdString()<<" made="<<made.size()<<std::endl;
        }
        // Offset dei soli riferimenti copiati: copie come curve derivate rigide.
        auto work=original; SketchOffset offset; offset.distance=0.5; offset.reverse=true;
        QVector<SketchEntity> selected, made;
        for(int j=0;j<original.curves.size();++j) if(original.curves[j].tool==DrawingTool::Converted) selected.append({1,j});
        require(offsetSketchEntities(work,selected,offset,&made).error.isEmpty(),"offset dei riferimenti");
        int poles=0;
        for(const auto &e:made) {
            require(e.kind==0 || work.curves[e.index].tool!=DrawingTool::Nurbs,"copia di offset come curva derivata");
            if(e.kind==1) poles+=work.curves[e.index].controlPoints.size();
        }
        QElapsedTimer timer;timer.start();
        const auto analysis=analyzeSketch(work);
        std::cout<<"DERIVED copies="<<made.size()<<" poles="<<poles<<" dof="<<analysis.degreesOfFreedom
                 <<" analysis ms="<<timer.elapsed()<<std::endl;
        timer.restart();
        const auto solved=solveSketch(work,{});
        std::cout<<"DERIVED solve ok="<<solved.ok<<" ms="<<timer.elapsed()<<std::endl;
    }

    static void profileSketchInteraction(const QString &path) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento");
        int index = -1;
        for (int i = 0; i < document.sketches.size(); ++i)
            if (document.sketches.at(i).name == QStringLiteral("Schizzo 4")) index = i;
        require(index >= 0, "Schizzo 4 presente");
        CadViewport v;
        v.sketches_ = document.sketches; v.activeSketch_ = index;
        const SketchObject original = v.sketches_.at(index);
        for (const auto &c : original.geometricConstraints)
            std::cout << "constraint " << int(c.type) << " curve=" << c.first.element << " point=" << c.first.point
                      << " positions=" << c.positions.size() << std::endl;
        auto measure = [&](const char *name, auto action) {
            std::cout << "START " << name << std::endl;
            QElapsedTimer timer; timer.start(); action();
            std::cout << "DONE " << name << " ms=" << timer.elapsed() << std::endl;
        };
        measure("snap", [&] { for (int i=0;i<4;++i) v.snapPoint(QPointF(50+i,26)); });
        measure("analysis", [&] { (void)analyzeSketch(original); });
        measure("solve unchanged", [&] { auto copy=original; require(solveSketch(copy).ok,"solve unchanged"); });
        measure("drag fixed pole", [&] {
            auto copy=original;
            require(!solveSketch(copy, {{{1,0,0},copy.curves[0].controlPoints[0]+QPointF(1,1)}}).ok,"polo fisso");
        });
        measure("crossing segment horizontal", [&] {
            auto copy=original; copy.segments.append({QPointF(0,5),QPointF(100,6)});
            SketchConstraint horizontal; horizontal.type=ConstraintType::Horizontal; horizontal.first={0,0,-1};
            copy.geometricConstraints.append(horizontal);
            require(solveSketch(copy).ok,"solve horizontal");
        });
        auto onCopy = original;
        const auto piece = curveGeometry(onCopy.curves[0]).front();
        const auto point = piece.curve->point(0.5*(piece.range.lo+piece.range.hi));
        const QPointF q(point.x(),point.y());
        onCopy.segments.append({q+QPointF(0,0.05),q+QPointF(5,0.1)});
        SketchConstraint on; on.type=ConstraintType::PointOnCurve; on.first={0,0,0}; on.second={1,0,-1};
        onCopy.geometricConstraints.append(on);
        measure("solve point on copied curve", [&] { require(solveSketch(onCopy).ok,"solve point on copied curve"); });
        measure("analysis point on copied curve", [&] { (void)analyzeSketch(onCopy); });
        measure("viewport solve and display curves", [&] {
            v.sketches_[index]=onCopy;
            require(v.solveActive({},onCopy),"viewport solve");
        });
    }

    static void blendPreviewReload() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        std::ifstream file(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/mouse_sewn.body", std::ios::binary);
        require(bool(file), "fixture mouse disponibile");
        std::stringstream bytes; bytes << file.rdbuf();
        const Body mouse = readBodyBinary(bytes.str());
        const Body mouseReloaded = readBodyBinary(writeBodyBinary(mouse));
        BodyDisplay unchanged;
        forgeBlendPreviewDisplay(mouse, mouseReloaded, 0, unchanged, 3);
        std::cout << "mouse invariato: " << unchanged.vertices.size() << " vertici evidenziati" << std::endl;
        require(unchanged.vertices.isEmpty() && unchanged.edges.isEmpty() && unchanged.constructionCurves.isEmpty(),
                "nessuna faccia invariata del mouse deve accendersi dopo rilettura");

        // Le cache dell'Applicatore contengono rivoluzioni con meridiani
        // limitati: superfici invarianti anche senza puntatori condivisi.
        std::ifstream revolutionFile(std::string(FORGECAD_SOURCE_DIR)
                                     + "/kernel/tests/data/applicatore_revolution_blend.body", std::ios::binary);
        require(bool(revolutionFile), "fixture rivoluzione Applicatore disponibile");
        std::stringstream revolutionBytes; revolutionBytes << revolutionFile.rdbuf();
        const Body revolution = readBodyBinary(revolutionBytes.str());
        const Body revolutionReloaded = readBodyBinary(writeBodyBinary(revolution));
        BodyDisplay revolutionPreview;
        forgeBlendPreviewDisplay(revolution, revolutionReloaded, 0, revolutionPreview, 3);
        require(revolutionPreview.vertices.isEmpty() && revolutionPreview.constructionCurves.isEmpty(),
                "le rivoluzioni preesistenti non si evidenziano dopo riapertura");

        // Supporto prolungato dalla mitra, ma identico sul vecchio dominio.
        std::vector<Vec3> poles;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) poles.emplace_back(-1.0 + 2.0 * i / 3.0, -1.0 + 2.0 * j / 3.0, 0.0);
        const std::vector<double> knots{-1,-1,-1,-1,1,1,1,1};
        const BSplineSurface support(3, 3, knots, knots, 4, 4, poles);
        Body sheet = makePlaneSheet(Frame3(), 1.0);
        const FaceId sheetFace = sheet.faces().front();
        sheet.face(sheetFace).surface = std::make_shared<BSplineSurface>(support);
        Body extended = readBodyBinary(writeBodyBinary(sheet));
        extended.face(sheetFace).surface = std::make_shared<BSplineSurface>(
            extendBSplineSurface(support, {-1.25,1.5}, {-1.5,1.25}));
        BodyDisplay extensionPreview;
        forgeBlendPreviewDisplay(sheet, extended, 0, extensionPreview, 3);
        require(extensionPreview.vertices.isEmpty() && extensionPreview.edges.isEmpty(),
                "il prolungamento polinomiale dell'appoggio non e' una faccia di raccordo nuova");
        // Una modifica interna reale, anche con gli stessi bordi, resta visibile.
        poles[5][2] = 0.01;
        const BSplineSurface changed(3, 3, knots, knots, 4, 4, poles);
        extended.face(sheetFace).surface = std::make_shared<BSplineSurface>(
            extendBSplineSurface(changed, {-1.25,1.5}, {-1.5,1.25}));
        forgeBlendPreviewDisplay(sheet, extended, 0, extensionPreview, 3);
        require(!extensionPreview.vertices.isEmpty() && !extensionPreview.triangleFaces.isEmpty(),
                "una superficie modificata non viene nascosta dal confronto dei prolungamenti");
        require(extensionPreview.triangleFaces.size() == extensionPreview.vertices.size() / 3
                    && extensionPreview.edgeIds.size() == extensionPreview.edges.size(),
                "l'anteprima appena tassellata conserva gli ID delle facce e dei bordi");

        const Body box = makeBox(Frame3(), 10, 10, 10);
        const EdgeId first = nearestEdge(box, Vec3(5,0,10), 1e-8);
        require(first.valid(), "primo spigolo valido");
        const Body base = blendSurfaceChains(box, {first}, 0.5, false);
        const EdgeId second = nearestEdge(base, Vec3(5,10,0), 1e-8);
        require(second.valid(), "secondo spigolo valido");
        const Body result = blendSurfaceChains(base, {second}, 0.75, false);
        BodyDisplay fresh, reopened;
        forgeBlendPreviewDisplay(base, result, 0, fresh, 3);
        const Body savedBase = readBodyBinary(writeBodyBinary(base));
        const Body savedResult = readBodyBinary(writeBodyBinary(result));
        forgeBlendPreviewDisplay(savedBase, savedResult, 0, reopened, 3);
        require(!fresh.vertices.isEmpty() && !fresh.constructionCurves.isEmpty(), "nuovo raccordo evidenziato");
        require(fresh.vertices.size() == reopened.vertices.size() && fresh.edges.size() == reopened.edges.size()
                    && fresh.constructionCurves.size() == reopened.constructionCurves.size(),
                "dopo rilettura si evidenzia soltanto lo stesso nuovo raccordo");
        for (const auto &p : reopened.vertices)
            require(p.y() > 9.0f && p.z() < 1.0f, "il raccordo preesistente resta fuori dall'anteprima");
        BodyDisplay existingMesh, reused;
        forgeTessellate(savedResult, 0, existingMesh);
        forgeBlendPreviewDisplay(savedBase, savedResult, 0, reused, 3, &existingMesh);
        require(!reused.vertices.isEmpty() && !reused.constructionCurves.isEmpty(), "patch e U/V dalla mesh gia' presente");
        require(reused.constructionCurves == reopened.constructionCurves, "il riuso conserva il taglio delle U/V sulla faccia");
        QVector<QVector3D> expectedVertices, expectedNormals;
        for (qsizetype triangle = 0; triangle < existingMesh.triangleFaces.size(); ++triangle) {
            if (!reused.triangleFaces.contains(existingMesh.triangleFaces[triangle])) continue;
            for (int corner = 0; corner < 3; ++corner) {
                expectedVertices.append(existingMesh.vertices[3 * triangle + corner]);
                expectedNormals.append(existingMesh.normals[3 * triangle + corner]);
            }
        }
        require(reused.vertices == expectedVertices && reused.normals == expectedNormals,
                "vertici e normali riutilizzati esattamente, senza nuova tassellazione");
        for (const auto &p : reused.vertices)
            require(p.y() > 9.0f && p.z() < 1.0f, "il riuso evidenzia soltanto il raccordo modificato");
        BodyDisplay legacy = existingMesh, fallback;
        legacy.triangleFaces.clear();
        forgeBlendPreviewDisplay(savedBase, savedResult, 0, fallback, 3, &legacy);
        require(fallback.vertices == reopened.vertices && fallback.constructionCurves == reopened.constructionCurves,
                "le cache precedenti senza ID conservano l'anteprima corretta");
    }

    // Diagnostica sul documento fornito dall'utente, senza incorporarne la
    // geometria nei dati persistenti del repository.
    static void applicatorePreview(const QString &path) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura Applicatore");
        require(document.extrusions.size() > 9, "storia Applicatore disponibile");
        const auto &fillet = document.extrusions[9];
        require(fillet.forgeBody && fillet.firstBody == 8 && document.extrusions[8].forgeBody,
                "raccordo finale e base disponibili");
        BodyDisplay fresh, reused;
        forgeBlendPreviewDisplay(*document.extrusions[8].forgeBody, *fillet.forgeBody, 0, fresh, 3);
        forgeBlendPreviewDisplay(*document.extrusions[8].forgeBody, *fillet.forgeBody, 0, reused, 3, &fillet.display);
        std::set<int> faces(fresh.triangleFaces.begin(), fresh.triangleFaces.end());
        require(faces.size() == 20, "solo le venti facce nuove del raccordo R0,05");
        require(faces == std::set<int>(reused.triangleFaces.begin(), reused.triangleFaces.end()),
                "stesse facce nuove nelle anteprime fresca e da cache");
        for (int face : faces) require(face >= 19, "nessuna faccia preesistente nell'anteprima");
        const ForgeBody base = document.extrusions[4].forgeBody;
        require(bool(base), "Differenza5 disponibile");
        const std::string original = writeBodyBinary(*base);
        QVector<EdgePoint> references;
        for (int id : {11,15,18}) {
            const auto &edge = base->edge(EdgeId(id));
            const Vec3 point = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
            references.append(EdgePoint{point.x(),point.y(),point.z(),id});
        }
        QString error;
        require(!forgeBlend(base, references, 0.3, false, &error)
                    && error.contains(QStringLiteral("raccordo non valido nell'angolo: scarto")),
                "R0,3 instabile rifiutato con lo scarto geometrico");
        require(writeBodyBinary(*base) == original, "base invariata dopo il rifiuto");
        CadViewport viewport;
        viewport.loadDocument(document);
        viewport.requestBlendPreview(4, references, 0.3, false, -1, {});
        viewport.startPreviewJob();
        QElapsedTimer timer; timer.start();
        while (viewport.previewRunning_ && timer.elapsed() < 30000) QApplication::processEvents();
        require(!viewport.previewRunning_ && !viewport.preview_.valid
                    && viewport.preview_.display.vertices.isEmpty()
                    && viewport.preview_.error.contains(QStringLiteral("scarto")),
                "anteprima asincrona invalida, senza triangoli falsi, con errore visibile");
        std::cout << "Applicatore: 20 facce nuove, cache/fresco coerenti, R0,3 rifiutato anche in anteprima asincrona" << std::endl;
    }

    // --bench-blend-edit file.prt indice: confronto della sola vista di un
    // raccordo esistente, senza ricostruirne la geometria.
    static void benchmarkBlendEdit(const QString &path, int index) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura del documento raccordo");
        CadViewport viewport;
        viewport.loadDocument(document);
        require(index >= 0 && index < viewport.extrusions_.size(), "indice raccordo");
        const ExtrusionObject &feature = viewport.extrusions_.at(index);
        require(feature.feature == BodyFeature::Blend && feature.forgeBody && feature.firstBody >= 0,
                "raccordo esistente disponibile");
        require(viewport.unchangedFeature(feature, index), "definizione invariata");
        const Kernel::Body &base = *viewport.extrusions_.at(feature.firstBody).forgeBody;
        BodyDisplay recalculated, reused;
        QElapsedTimer timer;
        timer.start();
        forgeBlendPreviewDisplay(base, *feature.forgeBody, feature.display.quality, recalculated, 3);
        const auto before = timer.elapsed();
        timer.restart();
        forgeBlendPreviewDisplay(base, *feature.forgeBody, feature.display.quality, reused, 3, &feature.display);
        const auto after = timer.elapsed();
        require(!reused.vertices.isEmpty() && !reused.triangleFaces.isEmpty(), "mesh esistente riutilizzata");
        require(reused.constructionCurves == recalculated.constructionCurves, "stesse U/V trimmate");
        BodyDisplay serialCurves;
        QVector<int> patchFaces = reused.triangleFaces;
        std::sort(patchFaces.begin(), patchFaces.end());
        patchFaces.erase(std::unique(patchFaces.begin(), patchFaces.end()), patchFaces.end());
        {
            struct SerialScope {
                SerialScope() { ++Kernel::parallelDepth; }
                ~SerialScope() { --Kernel::parallelDepth; }
            } serialScope;
            forgeSurfaceConstructionCurves(*feature.forgeBody, serialCurves, 3, true, patchFaces);
        }
        require(serialCurves.constructionCurves == reused.constructionCurves,
                "curve U/V identiche nel percorso seriale e parallelo");
        std::cout << feature.name.toStdString() << ": vista precedente " << before << " ms, riuso " << after
                  << " ms, triangoli " << reused.vertices.size() / 3 << ", curve U/V " << reused.constructionCurves.size() << std::endl;
        viewport.requestBlendPreview(feature.firstBody, feature.blendEdges, feature.blendSize, feature.blendChamfer, index, feature.chamferSpec);
        viewport.startPreviewJob();
        timer.restart();
        while (viewport.previewRunning_ && timer.elapsed() < 30000) QApplication::processEvents();
        require(viewport.preview_.valid && viewport.preview_.geometry == feature.forgeBody
                    && viewport.preview_.display.vertices == reused.vertices, "apertura modifica conserva il corpo e riusa la mesh");
    }

    static void reviewRegressions() {
        {
            // La costruzione del modello e' utilizzabile senza un CadViewport.
            QVector<ExtrusionObject> bodies;
            ExtrusionObject base;
            base.feature = BodyFeature::Primitive;
            base.primitive.kind = PrimitiveKind::Box;
            base.primitive.size[0] = 4.0;
            base.primitive.size[1] = 3.0;
            base.primitive.size[2] = 2.0;
            base.modelBodyId = 1;
            ForgeCad::buildBody(base, 0, {}, bodies, 0);
            require(base.error.isEmpty() && base.forgeBody && base.solid && !base.display.vertices.isEmpty(),
                    "modulo feature costruisce geometria e mesh senza viewport");
            const ForgeCad::ForgeBody geometry = base.forgeBody;
            const auto *vertices = base.display.vertices.constData();
            base.cachedGeometry = true;
            ForgeCad::buildBody(base, 0, {}, bodies, 0);
            require(base.forgeBody == geometry && base.display.vertices.constData() == vertices && !base.cachedGeometry,
                    "modulo feature riusa snapshot e mesh alla stessa qualita'");
            bodies.append(base);
            ExtrusionObject scale;
            scale.feature = BodyFeature::Scale;
            scale.modelBodyId = 1;
            scale.firstBody = 0;
            scale.scaleFactor = 2.0;
            ForgeCad::buildGeometry(scale, 1, {}, bodies);
            require(scale.error.isEmpty() && scale.forgeBody
                        && std::fabs(ForgeCad::Kernel::massProperties(*scale.forgeBody).volume - 192.0) < 1e-8,
                    "modulo feature risolve lo stadio precedente e scala il corpo");
            scale.suppressed = true;
            ForgeCad::buildGeometry(scale, 1, {}, bodies);
            require(scale.error.isEmpty() && scale.forgeBody == geometry,
                    "modulo feature conserva la base per uno stadio soppresso");
            scale.suppressed = false;
            scale.firstBody = 1;
            ForgeCad::buildGeometry(scale, 1, {}, bodies);
            require(!scale.error.isEmpty() && !scale.forgeBody,
                    "modulo feature rifiuta una base futura e rimuove il risultato precedente");
        }
        {
            ForgeCad::HistoryGraphDialog historyGraph;
            DocumentState trimGraph;
            trimGraph.sketches.append(SketchObject{});
            ExtrusionObject base;
            base.feature = BodyFeature::Imported;
            trimGraph.extrusions.append(base);
            ExtrusionObject trim;
            trim.feature = BodyFeature::SheetTrim;
            trim.firstBody = 0;
            trim.sketchIndex = 0;
            trimGraph.extrusions.append(trim);
            historyGraph.setDocument(trimGraph);
            require(historyGraph.edgeCount() == 2 && historyGraph.invalidDependencyCount() == 0,
                    "grafo taglio superficie include lo schizzo strumento");
            trimGraph.extrusions[1].sketchIndex = 3;
            historyGraph.setDocument(trimGraph);
            require(historyGraph.edgeCount() == 2 && historyGraph.invalidDependencyCount() == 1,
                    "grafo taglio superficie segnala schizzo strumento mancante");
        }
        for (bool symmetry : {false, true}) {
            CadViewport v; v.createSketch(0, QStringLiteral("Undo"));
            v.sketches_[0].segments.append({QPointF(0,0), QPointF(10,0)});
            v.sketchSelections_ = {{0,0}};
            require((symmetry ? v.toggleSymmetryAxis() : v.toggleConstruction()).isEmpty(), "toggle sketch role");
            require(v.sketches_.at(0).constructionSegments.size() == 1, "role applied");
            v.undo();
            require(v.sketches_.at(0).constructionSegments.isEmpty() && v.sketches_.at(0).symmetryAxes.isEmpty(), "undo sketch role");
            v.redo();
            require(v.sketches_.at(0).constructionSegments.size() == 1, "redo sketch role");
        }
        for (DrawingTool tool : {DrawingTool::Circle, DrawingTool::Ellipse}) {
            CadViewport v; v.createSketch(0, QStringLiteral("Quota"));
            CurveObject c; c.tool = tool; c.controlPoints = {{0,0},{10,0}};
            if (tool == DrawingTool::Ellipse) c.controlPoints.append(QPointF(0,5));
            ForgeCad::recalculateCurve(c,0); v.sketches_[0].curves.append(c);
            bool changed = false;
            QTimer::singleShot(0,[&] {
                auto *dialog = v.findChild<QDialog *>();
                if (!dialog) return;
                auto *box = dialog->findChild<QDoubleSpinBox *>();
                if (box) { box->setValue(12); changed = true; }
                dialog->accept();
            });
            require(v.editCurveDimension(0).isEmpty() && changed, "edit curve dimension");
            require(v.sketches_.at(0).curves.at(0).controlPoints != c.controlPoints, "curve dimension changed");
            v.undo();
            require(v.sketches_.at(0).curves.at(0).controlPoints == c.controlPoints, "undo curve dimension");
        }
        {
            CadViewport v; v.createSketch(0, QStringLiteral("Spline"));
            CurveObject c; c.tool = DrawingTool::Spline; c.controlPoints = {{0,0},{10,0},{20,0}};
            ForgeCad::recalculateCurve(c,0); c.tangentLinked.clear(); // documento precedente al formato 16
            v.sketches_[0].curves.append(c);
            SketchConstraint fix; fix.type = ConstraintType::Fix; fix.first = {1,0,2}; fix.positions = {QPointF(20,0)};
            v.sketches_[0].geometricConstraints.append(fix);
            v.addControlPointToCurve(QPointF(5,0));
            const auto &after = v.sketches_.at(0);
            require(after.curves.at(0).controlPoints.size() == 4, "insert spline point");
            require(after.curves.at(0).tangentLinked.at(1), "nuovo nodo inserito con tangenza");
            require(after.geometricConstraints.at(0).first.point == 3, "remap fixed endpoint");
            require(after.curves.at(0).tangentHandles.last() == c.tangentHandles.last(), "preserve existing tangent handles");
            require(after.curves.at(0).controlPoints.last() == QPointF(20,0), "fixed endpoint preserved");
            v.undo();
            require(v.sketches_.at(0).curves.at(0).controlPoints == c.controlPoints, "undo inserted point");
            require(v.sketches_.at(0).geometricConstraints.at(0).first.point == 2, "undo constraint remap");
        }
        {
            CadViewport v; v.createSketch(0, QStringLiteral("NURBS"));
            CurveObject c; c.tool = DrawingTool::Nurbs; c.degree = 2;
            c.controlPoints = {{0,0},{10,0},{20,0}}; c.knots = {0,0,0,1,1,1}; c.weights = {1,1,1};
            ForgeCad::recalculateCurve(c,0); v.sketches_[0].curves.append(c); v.history_.clear();
            v.addControlPointToCurve(QPointF(5,0));
            require(v.sketches_.at(0).curves.at(0).controlPoints == c.controlPoints && !v.canUndo(), "reject unsupported NURBS insertion atomically");
            CurveObject bad = c; bad.controlPoints.insert(1,QPointF(5,0)); bad.weights.insert(1,1);
            v.commitFreeCurveEdit(0,bad,{0,-1,1,2});
            require(!v.canUndo() && v.sketches_.at(0).curves.at(0).numericallyValid, "invalid NURBS commit rejected");
            bool controlsDisabled = false;
            QTimer::singleShot(0,[&] {
                auto *dialog = v.findChild<QDialog *>();
                if (!dialog) return;
                auto *add = dialog->findChild<QPushButton *>(QStringLiteral("freeCurveAdd"));
                auto *remove = dialog->findChild<QPushButton *>(QStringLiteral("freeCurveRemove"));
                controlsDisabled = add && remove && !add->isEnabled() && !remove->isEnabled();
                dialog->reject();
            });
            v.editFreeCurve(0);
            require(controlsDisabled, "NURBS unsupported controls disabled");
            CurveObject moved = c; moved.controlPoints[1] += QPointF(0,1);
            v.commitFreeCurveEdit(0,moved,{0,1,2});
            require(v.sketches_.at(0).curves.at(0).numericallyValid && v.sketches_.at(0).curves.at(0).knots == c.knots, "NURBS point movement supported");
            v.undo(); require(v.sketches_.at(0).curves.at(0).controlPoints == c.controlPoints, "undo NURBS point movement");
        }
        {
            QTemporaryDir directory;
            const QString path = directory.filePath(QStringLiteral("document.prt"));
            DocumentState state; state.lengthUnit = LengthUnit::Inch;
            require(ForgeCad::saveDocumentFile(path,state,false).isEmpty(), "save unit regression");
            DocumentState loaded;
            require(ForgeCad::loadDocumentFile(path,loaded).isEmpty() && loaded.lengthUnit == LengthUnit::Inch, "valid unit round trip");
            QFile file(path); require(file.open(QIODevice::ReadOnly), "read regression document");
            const QByteArray data = file.readAll(); file.close();
            const QByteArray payload = qUncompress(data.mid(11));
            for (int missing = 1; missing <= 4; ++missing) {
                const QByteArray compressed = qCompress(payload.left(payload.size()-missing));
                QByteArray modified = data.left(7);
                QDataStream out(&modified,QIODevice::Append); out << quint32(compressed.size());
                out.writeRawData(compressed.constData(),int(compressed.size()));
                require(file.open(QIODevice::WriteOnly), "write incomplete document");
                require(file.write(modified) == modified.size(), "write complete test bytes"); file.close();
                require(!ForgeCad::loadDocumentFile(path,loaded).isEmpty(), "reject incomplete unit field");
                require(loaded.lengthUnit == LengthUnit::Inch, "failed load preserves destination");
            }
        }
    }

    static void shapeAnalysisUi(bool render) {
        QMainWindow window;
        auto *v=new CadViewport(&window); window.setCentralWidget(v); window.resize(1000,750);
        PrimitiveParameters primitive; primitive.kind=PrimitiveKind::Cylinder;
        primitive.size[0]=2; primitive.size[1]=4;
        require(v->createPrimitive(primitive,QStringLiteral("Cilindro analisi")).isEmpty(),"create analysis cylinder");
        ExtrusionObject scaled; scaled.feature=BodyFeature::Scale; scaled.firstBody=0; scaled.scaleFactor=2;
        scaled.name=QStringLiteral("Ultima lavorazione scala");
        require(v->createBody(scaled).isEmpty(),"analysis result feature");
        require(v->renameModelBody(0,QStringLiteral("Corpo prova analisi")).isEmpty(),"rename logical analysis body");
        v->selection_={SceneObjectKind::Extrusion,0,-1}; // selezione di uno stadio precedente
        SketchObject sketch; sketch.name=QStringLiteral("Cerchio test");
        CurveObject circle; circle.tool=DrawingTool::Circle; circle.controlPoints={{0,0},{1,0}};
        sketch.curves.append(circle);
        CurveObject spline; spline.tool=DrawingTool::Spline; spline.controlPoints={{0,0},{1,2},{2,-1},{3,0}};
        sketch.curves.append(spline); v->sketches_.append(sketch);
        v->fitAll();
        if(render) { window.show(); QApplication::processEvents(); }
        std::exception_ptr failure;
        QTimer::singleShot(100,[&] {
            auto *dialog=window.findChild<QDialog *>();
            try {
                require(dialog!=nullptr,"analysis dialog");
                auto *body=dialog->findChild<QComboBox *>(QStringLiteral("shapeAnalysisBody"));
                auto *modeBox=dialog->findChild<QComboBox *>(QStringLiteral("shapeAnalysisMode"));
                auto *face=dialog->findChild<QComboBox *>(QStringLiteral("shapeAnalysisFace"));
                require(body && modeBox && face,"analysis selectors");
                require(body->count()==2 && body->currentData().toInt()==1,"one result body plus sketch, old feature maps to tip");
                require(body->currentText()==QStringLiteral("Corpo prova analisi"),"logical body name, not last feature");
                require(face->count()==4,"all and three cylinder faces");
                for(int mode=0;mode<6;++mode) {
                    modeBox->setCurrentIndex(mode);
                    require(v->shapeAnalysisMode_==mode,"analysis mode applied");
                    if(mode==1 || mode==2) require(!v->shapeAnalysis_.triangles.empty(),"analysis map triangles");
                    if(mode==3) require(!v->shapeAnalysis_.borders.empty(),"analysis boundary colors");
                    if(mode==4) require(!v->shapeAnalysis_.lines.empty(),"analysis comb");
                    if(mode==5) require(!v->shapeAnalysis_.grid.empty() && !v->shapeAnalysis_.lines.empty(),"surface UV comb");
                    if(render) {
                        v->makeCurrent(); require(v->displayCache_.shaderAvailable(),"zebra shader linked"); v->doneCurrent();
                        require(v->grabFramebuffer().save(QStringLiteral("/tmp/forgecad-analysis-%1.png").arg(mode)),"analysis framebuffer");
                    }
                }
                auto *pick=dialog->findChild<QPushButton *>(QStringLiteral("shapeAnalysisPickFace"));
                pick->click(); require(v->referencePicking(),"face picking active");
                GeometryRef ref; ref.kind=5; ref.index=1;
                ref.point.subshape=face->itemData(1).toInt();
                const auto picked=v->refPickFinished_; picked(true,ref);
                require(v->shapeAnalysisFace_==ref.point.subshape && !v->referencePicking(),"picked face applied");
                require(!v->shapeAnalysisFaceDisplay_.vertices.empty(),"face zebra mesh");
                for(int m=0;m<6;++m) {
                    modeBox->setCurrentIndex(m);
                    require(v->shapeAnalysisFace_==ref.point.subshape && v->shapeAnalysisBody_==1,"face filter survives mode changes");
                    if(m==1 || m==2) require(v->shapeAnalysis_.triangles.size()==v->shapeAnalysisFaceDisplay_.vertices.size(),"map only selected face");
                    if(render && (m==0 || m==5))
                        require(v->grabFramebuffer().save(QStringLiteral("/tmp/analysis-face-%1.png").arg(m)),"single face render");
                }
                if(render) require(dialog->grab().save(QStringLiteral("/tmp/analysis-face-panel.png")),"face panel");
                face->setCurrentIndex(0); require(v->shapeAnalysisFace_==-1,"restore whole body");
                auto *through=dialog->findChild<QCheckBox *>(QStringLiteral("shapeCombThrough"));
                require(through && through->isChecked() && v->shapeAnalysisThrough_,"surface comb visible through faces");
                through->setChecked(false); require(!v->shapeAnalysisThrough_,"surface comb depth toggle");
                through->setChecked(true);
                if(render) require(dialog->grab().save(QStringLiteral("/tmp/forgecad-analysis-uv-panel.png")),"UV analysis panel screenshot");
                modeBox->setCurrentIndex(4);
                body->setCurrentIndex(body->count()-1);
                require(v->shapeAnalysisBody_<-1 && !v->shapeAnalysis_.lines.empty(),"sketch comb");
                if(render) require(dialog->grab().save(QStringLiteral("/tmp/forgecad-analysis-panel.png")),"analysis panel screenshot");
            } catch(...) { failure=std::current_exception(); }
            if(dialog) dialog->reject();
        });
        shapeAnalysisDialog(&window,v);
        if(failure) std::rethrow_exception(failure);
        require(v->shapeAnalysisBody_==-1 && v->shapeAnalysis_.triangles.empty(),"analysis cleanup");
        require(v->extrusions_.size()==2 && !v->referencePicking() && !v->refPickFinished_,"analysis preserves model and clears picking");
    }
    static void sketchCombUi(bool render) {
        QMainWindow window;
        auto *v=new CadViewport(&window); window.setCentralWidget(v); window.resize(1100,850);
        v->sketches_.append(SketchObject{}); v->activeSketch_=0; v->sketchMode_=true;
        v->setDrawingTool(DrawingTool::Select);
        if(render) { window.show(); QApplication::processEvents(); }
        for (DrawingTool tool : {DrawingTool::Spline,DrawingTool::Nurbs}) {
            CurveObject curve; curve.tool=tool; curve.controlPoints={{0,0},{2,3},{4,-2},{6,0}};
            if(tool==DrawingTool::Spline) ForgeCad::initializeTangentHandles(curve);
            else curve.weights={1,2,1,1};
            ForgeCad::recalculateCurve(curve);
            v->sketches_[0].curves={curve}; v->fitAll();
            for(bool accept : {false,true}) {
                const auto before=v->sketches_[0].curves[0];
                std::exception_ptr failure;
                QTimer::singleShot(0,[&] {
                    auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    try {
                        require(dialog!=nullptr,"free curve edit dialog");
                        auto *comb=dialog->findChild<QCheckBox *>(QStringLiteral("sketchCurvatureComb"));
                        auto *automatic=dialog->findChild<QCheckBox *>(QStringLiteral("sketchCurvatureAuto"));
                        auto *gain=dialog->findChild<QDoubleSpinBox *>(QStringLiteral("sketchCurvatureScale"));
                        auto *x=dialog->findChild<QDoubleSpinBox *>(QStringLiteral("freeCurveX"));
                        require(comb && automatic && gain && x,"sketch comb panel controls");
                        comb->setChecked(true);
                        require(v->sketchCombVisible_ && v->sketchCombPreviewIndex_==0,"sketch comb live preview enabled");
                        require(!v->sketchCurveComb(0,v->sketchCombPreview_).empty(),"spline and rational NURBS comb");
                        x->setValue(x->value()+.5);
                        require(v->sketchCombPreview_.controlPoints[0].x()==before.controlPoints[0].x()+.5,"comb follows point preview");
                        require(v->sketches_[0].curves[0].controlPoints==before.controlPoints,"preview leaves model untouched");
                        if(tool==DrawingTool::Spline) {
                            for(auto *box:dialog->findChildren<QCheckBox *>())
                                if(box->text()==QStringLiteral("Rilassa la curva")) box->setChecked(true);
                            require(v->sketchCombPreview_.tangentHandles!=before.tangentHandles,"comb follows relaxation");
                        }
                        automatic->setChecked(false); gain->setValue(2);
                        require(v->sketchCombScale_==2,"manual comb scale");
                        require(gain->suffix().isEmpty(),"comb scale is not formatted as a length");
                        automatic->setChecked(true); require(v->sketchCombScale_==0,"automatic comb scale");
                        if(render) {
                            require(v->grabFramebuffer().save(QStringLiteral("/tmp/sketch-comb-%1.png").arg(int(tool))),"sketch comb render");
                            require(dialog->grab().save(QStringLiteral("/tmp/sketch-comb-panel-%1.png").arg(int(tool))),"sketch comb panel");
                        }
                        comb->setChecked(false); require(!v->sketchCombVisible_ && v->sketchCombPreviewIndex_==-1,"hide sketch comb");
                        comb->setChecked(true);
                    } catch(...) { failure=std::current_exception(); }
                    if(dialog) { if(accept) dialog->accept(); else dialog->reject(); }
                });
                v->editFreeCurve(0,0);
                if(failure) std::rethrow_exception(failure);
                require(v->sketchCombVisible_ && v->sketchCombPreviewIndex_==-1,"comb persists without working preview");
                require((v->sketches_[0].curves[0].controlPoints!=before.controlPoints)==accept,"accept and cancel geometry");
                require(!v->sketchCurveComb(0,v->sketches_[0].curves[0]).empty(),"committed comb");
                const auto old=v->sketchCurveComb(0,v->sketches_[0].curves[0]).front().position;
                auto changed=v->sketches_[0].curves[0]; changed.controlPoints[0]+=QPointF(1,1);
                require(v->sketchCurveComb(0,changed).front().position!=old,"comb cache follows subsequent edits");
                if(accept) {
                    v->undo(); require(v->sketches_[0].curves[0].controlPoints==before.controlPoints,"undo curve edit");
                    require(!v->sketchCurveComb(0,v->sketches_[0].curves[0]).empty(),"comb after undo");
                }
            }
        }
    }
    static void splineSelectionClicks(const QString &path) {
        using namespace ForgeCad;
        if (QApplication::arguments().contains(QStringLiteral("--user-settings"))) {
            QSettings settings;
            QDir().mkpath(QFileInfo(settings.fileName()).absolutePath());
            QFile::copy(QDir::homePath()+QStringLiteral("/.config/ForgeCAD/ForgeCAD.conf"), settings.fileName());
        }
        DocumentState state;
        if (path.isEmpty()) {
            for (int plane : {0,2}) {
                SketchObject sketch;
                sketch.plane = plane;
                sketch.name = QStringLiteral("Spline con offset %1").arg(plane);
                CurveObject curve;
                curve.tool = DrawingTool::Spline;
                curve.controlPoints = {{0,0},{5,3},{10,0}};
                recalculateCurve(curve);
                sketch.curves.append(curve);
                SketchOffset offset;
                offset.distance = 0.5;
                offset.dimensioned = true;
                require(offsetSketchEntities(sketch,{{1,0}},offset).error.isEmpty(), "offset associativo della spline");
                state.sketches.append(sketch);
            }
        } else require(loadDocumentFile(path, state).isEmpty(), "lettura del documento per i clic spline");
        for (SketchObject &sketch : state.sketches)
            for (CurveObject &curve : sketch.curves) recalculateCurve(curve);
        PdfWindow window;
        auto *viewport = dynamic_cast<CadViewport *>(window.centralWidget());
        require(viewport != nullptr, "viewport della finestra completa");
        window.resize(1200,900);
        window.show();
        viewport->loadDocument(state);
        QTimer dismissDialogs;
        QObject::connect(&dismissDialogs,&QTimer::timeout,&window,[] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        dismissDialogs.start(20);
        const auto settle = [] {
            QEventLoop loop;
            QTimer::singleShot(30,&loop,&QEventLoop::quit);
            loop.exec();
        };
        const auto click = [&](QPoint pixel, bool doubleClick = false, QPoint movement = {}) {
            const QPointF local(pixel), global(viewport->mapToGlobal(pixel));
            QMouseEvent hover(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &hover);
            QMouseEvent press(doubleClick ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress,
                              local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &press);
            if (!movement.isNull()) {
                const QPointF end = local + QPointF(movement);
                QMouseEvent drag(QEvent::MouseMove, end, global + QPointF(movement), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &drag);
            }
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &release);
            settle();
        };
        for (int sketch = 0; sketch < state.sketches.size(); ++sketch) {
            viewport->selectSketch(sketch);
            viewport->setDrawingTool(DrawingTool::Select);
            viewport->fitAll();
            settle();
            for (const CurveObject &curve : state.sketches[sketch].curves) {
                if (curve.tool != DrawingTool::Spline) continue;
                for (int fraction : {2,4,6,8}) {
                    const QPointF sample = curve.samples.value(curve.samples.size()*fraction/10);
                    const QPoint pixel = viewport->projectWorldPoint(viewport->mapSketchPoint(sample,state.sketches[sketch])).toPoint();
                    if (!viewport->rect().contains(pixel)) continue;
                    for (const QPoint offset : {QPoint(3,0),QPoint(0,8),QPoint(16,0),QPoint(0,-16)}) {
                        std::cerr << "spline clicks sketch=" << sketch << " sample=" << fraction
                                  << " offset=" << offset.x() << ',' << offset.y() << std::endl;
                        click(pixel);
                        click(pixel+offset);
                        click(pixel);
                        click(pixel+offset,true);
                    }
                }
                for (int control = 0; control < curve.controlPoints.size(); ++control) {
                    const QPoint pixel = viewport->projectWorldPoint(viewport->mapSketchPoint(curve.controlPoints[control],state.sketches[sketch])).toPoint();
                    std::cerr << "spline node click sketch=" << sketch << " control=" << control << std::endl;
                    click(pixel,false,QPoint(1,1));
                    click(pixel+QPoint(8,0));
                }
                viewport->draggingControlPoint_ = true;
                viewport->draggingCurveIndex_ = 0;
                viewport->draggingControlIndex_ = 1;
                viewport->draggingPointKind_ = EditablePointKind::Control;
                viewport->dragSnapshot_ = viewport->currentDocument();
                const QPoint pixel = viewport->projectWorldPoint(viewport->mapSketchPoint(
                    viewport->sketches_[sketch].curves[0].controlPoints[1],state.sketches[sketch])).toPoint();
                const QPointF local(pixel), global(viewport->mapToGlobal(pixel));
                QMouseEvent drag(QEvent::MouseMove,local,global,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                std::cerr << "spline forced drag sketch=" << sketch << std::endl;
                QApplication::sendEvent(viewport,&drag);
            }
            viewport->endSketchMode();
        }
        std::cout << "PASS spline selection clicks" << std::endl;
    }
    static void splineShapeOptions() {
        using namespace ForgeCad;
        const auto near = [](QPointF a, QPointF b) { return std::hypot(a.x()-b.x(), a.y()-b.y()) < 1e-9; };
        CurveObject curve;
        curve.tool = DrawingTool::Spline;
        curve.controlPoints = {{0,0}, {1,3}, {2,3.1}, {12,4}, {13,0}};
        const auto original = curve.controlPoints;
        initializeTangentHandles(curve);
        for (bool linked : curve.tangentLinked) require(linked, "tangenza predefinita dei nuovi nodi");
        curve.tangentLinked[2] = false;
        initializeTangentHandles(curve);
        require(!curve.tangentLinked[2] && curve.tangentLinked[1], "inizializzazione conserva lo svincolo esplicito");
        {
            CadViewport insert;
            insert.createSketch(0, QStringLiteral("Spline"));
            CurveObject initial;
            initial.tool = DrawingTool::Spline;
            initial.controlPoints = {{0,0},{10,0},{20,0}};
            recalculateCurve(initial);
            insert.sketches_[0].curves.append(initial);
            insert.addControlPointToCurve(QPointF(5,0));
            require(insert.sketches_[0].curves[0].controlPoints.size() == 4
                && insert.sketches_[0].curves[0].tangentLinked[1], "inserimento di un nodo con tangenza predefinita");
        }
        // Trascinamento reale di un nodo: gli offset delle maniglie non cambiano.
        for (bool linked : {false, true}) {
            CadViewport drag;
            drag.resize(800,600);
            drag.createSketch(0, QStringLiteral("Spline"));
            drag.setDrawingTool(DrawingTool::Select);
            CurveObject initial;
            initial.tool = DrawingTool::Spline;
            initial.controlPoints = {{0,0},{1,2},{3,0}};
            recalculateCurve(initial);
            initial.tangentLinked[1] = linked;
            drag.sketches_[0].curves.append(initial);
            drag.draggingControlPoint_ = true;
            drag.draggingCurveIndex_ = 0;
            drag.draggingControlIndex_ = 1;
            drag.draggingPointKind_ = EditablePointKind::Control;
            drag.dragSnapshot_ = drag.currentDocument();
            const QPoint pixel(470,210);
            const QPointF target = drag.screenToSketchPoint(pixel);
            QMouseEvent move(QEvent::MouseMove, QPointF(pixel), QPointF(pixel), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            drag.mouseMoveEvent(&move);
            const CurveObject moved = drag.sketches_[0].curves[0];
            const QPointF delta = target - initial.controlPoints[1];
            require(pointLength(delta) > 0.1 && near(moved.controlPoints[1],target), "nodo segue il trascinamento");
            require(near(moved.tangentHandles[1].first,initial.tangentHandles[1].first+delta)
                && near(moved.tangentHandles[1].second,initial.tangentHandles[1].second+delta), "maniglie seguono il nodo anche svincolate");
            drag.undo();
            require(drag.sketches_[0].curves[0].tangentHandles == initial.tangentHandles, "undo ripristina le maniglie del nodo");
            drag.redo();
            require(drag.sketches_[0].curves[0].tangentHandles == moved.tangentHandles, "redo ripristina le maniglie traslate");
            if (linked) {
                drag.sketches_[0].curves[0] = initial;
                drag.draggingControlPoint_ = true;
                drag.draggingCurveIndex_ = 0;
                drag.draggingControlIndex_ = 1;
                drag.draggingPointKind_ = EditablePointKind::TangentOut;
                drag.mouseMoveEvent(&move);
                const CurveObject tangent = drag.sketches_[0].curves[0];
                const QPointF in = tangent.tangentHandles[1].first - tangent.controlPoints[1];
                const QPointF out = tangent.tangentHandles[1].second - tangent.controlPoints[1];
                require(std::abs(in.x()*out.y()-in.y()*out.x()) < 1e-9
                    && QPointF::dotProduct(in,out) < 0.0, "trascinamento della maniglia mantiene la tangenza opposta");
                require(std::abs(pointLength(in)-pointLength(initial.tangentHandles[1].first-initial.controlPoints[1])) < 1e-9,
                    "tangenza conserva la lunghezza della maniglia opposta");
            }
        }
        shapeSpline(curve, {true, false, false});
        const auto secondStart = [&](int i) {
            return 6.0 * (curve.controlPoints[i] - 2.0*curve.tangentHandles[i].second + curve.tangentHandles[i+1].first);
        };
        const auto secondEnd = [&](int i) {
            return 6.0 * (curve.controlPoints[i+1] - 2.0*curve.tangentHandles[i+1].first + curve.tangentHandles[i].second);
        };
        require(curve.controlPoints == original, "regolarizzazione conserva i punti");
        require(near(secondStart(0), {}) && near(secondEnd(3), {}), "estremi naturali");
        for (int i = 1; i < 4; ++i) require(near(secondEnd(i-1), secondStart(i)), "continuita C2 interna");
        const auto uniformHandles = curve.tangentHandles;
        shapeSpline(curve, {true, true, false});
        for (int i = 0; i < original.size(); ++i)
            require(near(curve.tangentHandles[i].second-original[i], (uniformHandles[i].second-original[i])*0.5), "rilassamento tangenti");
        for (bool uniform : {false, true}) for (bool relaxed : {false, true}) {
            shapeSpline(curve, {uniform, relaxed, true});
            for (int i = 0; i+1 < original.size(); ++i) {
                const QPointF a = original[i], b = original[i+1];
                QPointF previous = a;
                for (int k = 1; k <= 100; ++k) {
                    const double t = k/100.0, u = 1.0-t;
                    const QPointF q = u*u*u*a + 3*u*u*t*curve.tangentHandles[i].second
                        + 3*u*t*t*curve.tangentHandles[i+1].first + t*t*t*b;
                    require(q.x() >= std::min(a.x(),b.x())-1e-9 && q.x() <= std::max(a.x(),b.x())+1e-9
                        && q.y() >= std::min(a.y(),b.y())-1e-9 && q.y() <= std::max(a.y(),b.y())+1e-9, "nessun overshoot");
                    require((q.x()-previous.x())*(b.x()-a.x()) >= -1e-9
                        && (q.y()-previous.y())*(b.y()-a.y()) >= -1e-9, "nessuna inversione interna");
                    previous = q;
                }
            }
        }
        curve.controlPoints = {{0,0},{2,0},{2,2},{0,2},{0,0}};
        shapeSpline(curve, {true,false,false});
        require(near(curve.tangentHandles.first().second, curve.tangentHandles.last().second), "chiusura C1");
        require(near(secondStart(0), secondEnd(3)), "chiusura C2");
        curve.controlPoints = {{0,0},{0,0},{1,0}};
        shapeSpline(curve, {true,true,true});
        recalculateCurve(curve);
        require(curve.numericallyValid, "punti ripetuti gestiti");
        curve.controlPoints = {{0,0},{2,4}};
        shapeSpline(curve, {true,false,false});
        require(near(curve.tangentHandles.first().second, QPointF(2.0/3.0,4.0/3.0)), "due punti: segmento rettilineo");

        // La creazione ordinaria, senza scegliere opzioni, deve gia' essere C2.
        for (bool closed : {false, true}) {
            CadViewport automatic;
            automatic.sketches_.append(SketchObject{}); automatic.activeSketch_ = 0;
            automatic.setDrawingTool(DrawingTool::Spline);
            automatic.sketchMode_ = true;
            automatic.curveControlPoints_ = original;
            if (closed) automatic.curveControlPoints_.append(original.first());
            const auto points = automatic.curveControlPoints_;
            automatic.cursorSketchPoint_ = points.last();
            CurveObject automaticPreview;
            require(automatic.previewCurve(automaticPreview), "anteprima C2 predefinita");
            automatic.finalizeCurve();
            require(automatic.sketches_[0].curves.size() == 1, "spline C2 automatica creata");
            const auto &created = automatic.sketches_[0].curves.first();
            require(created.controlPoints == points && created.tangentHandles == automaticPreview.tangentHandles,
                    "C2 automatica conserva punti e coerenza con anteprima");
            const auto startSecond = [&](int i) {
                return 6.0*(created.controlPoints[i]-2.0*created.tangentHandles[i].second+created.tangentHandles[i+1].first);
            };
            const auto endSecond = [&](int i) {
                return 6.0*(created.controlPoints[i+1]-2.0*created.tangentHandles[i+1].first+created.tangentHandles[i].second);
            };
            for (int i = 1; i+1 < points.size(); ++i)
                require(near(endSecond(i-1),startSecond(i)), "derivata seconda continua nella spline creata senza opzioni");
            if (closed) require(near(endSecond(points.size()-2),startSecond(0)), "chiusura periodica C2 automatica");
            automatic.undo(); require(automatic.sketches_[0].curves.isEmpty(), "undo creazione C2 automatica");
            automatic.redo(); require(automatic.sketches_[0].curves.first().controlPoints == points, "redo C2 automatica");
        }
        CadViewport viewport;
        viewport.sketches_.append(SketchObject{}); viewport.activeSketch_ = 0;
        viewport.setDrawingTool(DrawingTool::Spline);
        viewport.setSplineShapeOptions({true,true,true});
        viewport.curveControlPoints_ = original;
        viewport.sketchMode_ = true;
        viewport.cursorSketchPoint_ = original.last();
        CurveObject preview;
        require(viewport.previewCurve(preview), "anteprima spline disponibile");
        viewport.finalizeCurve();
        require(preview.tangentHandles == viewport.sketches_[0].curves[0].tangentHandles, "anteprima coerente con risultato");
        require(viewport.sketches_[0].curves.size() == 1, "creazione spline regolarizzata");
        CurveObject expected; expected.tool = DrawingTool::Spline; expected.controlPoints = original;
        shapeSpline(expected, {true,true,true});
        require(viewport.sketches_[0].curves[0].tangentHandles == expected.tangentHandles, "creazione usa le opzioni");
        viewport.undo(); require(viewport.sketches_[0].curves.isEmpty(), "undo spline");
        viewport.redo(); require(viewport.sketches_[0].curves[0].tangentHandles == expected.tangentHandles, "redo spline");
        QTemporaryDir files;
        const QString path = files.path()+QStringLiteral("/spline.prt");
        require(saveDocumentFile(path,viewport.currentDocument(),false).isEmpty(), "salva spline");
        DocumentState loaded;
        require(loadDocumentFile(path,loaded).isEmpty(), "carica spline");
        require(loaded.sketches[0].curves[0].tangentHandles == expected.tangentHandles, "maniglie conservate nel documento");
        require(loaded.sketches[0].curves[0].tangentLinked == expected.tangentLinked, "tangenza conservata nel documento");
        for (bool accept : {false, true}) {
            bool found = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                for (QCheckBox *box : dialog->findChildren<QCheckBox *>())
                    if (box->text() == QStringLiteral("Uniforma la curvatura (C2)")) { box->setChecked(true); found = true; }
                if (accept) dialog->accept(); else dialog->reject();
            });
            viewport.editFreeCurve(0);
            require(found, "spunta disponibile nella modifica spline");
            if (accept) shapeSpline(expected, {true,false,false});
            require(viewport.sketches_[0].curves[0].tangentHandles == expected.tangentHandles, "conferma/annulla modifica spline");
        }
        if (QApplication::arguments().contains(QStringLiteral("--render"))) {
            viewport.resize(1000,750);
            viewport.setDrawingTool(DrawingTool::Select);
            viewport.setViewNormal(0);
            viewport.fitAll();
            viewport.show();
            QApplication::processEvents();
            require(viewport.grabFramebuffer().save(QStringLiteral("/tmp/forgecad-spline-handles.png")), "immagine curva e maniglie");
        }
    }
    static void sheetDressupFeatures() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const Body box = makeBox(Frame3(),20,10,5);
        std::vector<FaceId> walls;
        for (FaceId f : box.faces())
            if (std::fabs(static_cast<const Plane &>(*box.face(f).surface).frame().zDir().z()) < 0.1) walls.push_back(f);
        ExchangeBody exchange; exchange.name = "superfici cucite"; exchange.body = facesAsSheet(box,walls);
        ExtrusionObject source; source.feature = BodyFeature::Imported; source.name = QStringLiteral("Superficie cucita");
        source.importSource = QStringLiteral("superficie.step");
        source.importData = QByteArray::fromStdString(writeStep({exchange}));
        DocumentState state; state.extrusions = {source};
        CadViewport v; v.loadDocument(state);
        require(v.extrusions_[0].forgeBody && !v.extrusions_[0].solid, "importazione superficie cucita");
        require(v.startEdgePick(false).isEmpty() && v.edgePickEligible(0), "selezione spigoli su superficie");
        v.cancelEdgePick();
        const auto base = v.extrusions_[0].forgeBody;
        ExtrusionObject draft; draft.feature = BodyFeature::Draft; draft.firstBody = 0;
        draft.draftNeutral.kind = 1; draft.draftNeutral.index = 0; draft.draftAngle = 5;
        for (FaceId f : base->faces()) draft.offsetFaces.append(faceReference(*base,f,static_cast<const Plane &>(*base->face(f).surface).frame().origin()));
        require(v.createBody(draft).isEmpty(), "sformo della superficie");
        require(!v.extrusions_.last().solid && v.extrusions_.last().forgeBody->isSheet(), "sformo resta superficie");
        v.undo(); require(v.extrusions_.size() == 1, "undo sformo superficie");
        v.redo(); require(v.extrusions_.size() == 2 && !v.extrusions_.last().solid, "redo sformo superficie");
        for (bool chamfer : {false,true}) {
            const int at = v.extrusions_.size()-1;
            const auto body = v.extrusions_[at].forgeBody;
            EdgeId edge;
            for (EdgeId e : body->edges()) if (!body->isLaminar(e)) { edge = e; break; }
            require(edge.valid(), "spigolo cucito disponibile");
            const auto &geometry = body->edge(edge);
            const QVector<EdgePoint> edges{edgeReference(*body,edge,geometry.curve->point(0.5*(geometry.range.lo+geometry.range.hi)))};
            v.requestBlendPreview(at,edges,0.3,chamfer);
            QElapsedTimer timeout; timeout.start();
            while (!v.preview_.valid && v.preview_.error.isEmpty() && timeout.elapsed() < 10000) QApplication::processEvents();
            require(v.preview_.valid && v.preview_.geometry && v.preview_.geometry->isSheet(), "anteprima finitura superficie");
            const QString error = v.createBlend(at,edges,0.3,chamfer,QStringLiteral("Finitura superficie"));
            require(error.isEmpty(),error.toStdString().c_str());
            require(!v.extrusions_.last().solid && v.extrusions_.last().forgeBody->isSheet(), "finitura conserva la superficie aperta");
            require(checkBody(*v.extrusions_.last().forgeBody).empty(), "superficie finita valida");
            v.undo();
            if (chamfer) {
                v.redo();
                require(!v.extrusions_.last().solid, "redo smusso conserva superficie");
            }
            v.clearPreview();
        }
        QTemporaryDir files;
        const QString path = files.path()+QStringLiteral("/superficie.prt");
        require(saveDocumentFile(path,v.currentDocument(),false).isEmpty(), "salvataggio sformo e smusso superficie");
        DocumentState loaded;
        require(loadDocumentFile(path,loaded).isEmpty(), "rilettura superficie");
        v.loadDocument(loaded);
        require(v.extrusions_.last().forgeBody && !v.extrusions_.last().solid, "rigenerazione sformo e smusso superficie");
    }
    // Taglio con schizzo proiettato e curva proiettata: anteprima, storia,
    // modi, verso, Undo/Redo, riferimenti, persistenza e pannello.
    static void projectionFeatures() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        CadViewport viewport;
        PrimitiveParameters primitive;
        primitive.size[0] = 20; primitive.size[1] = 10; primitive.size[2] = 5;
        require(viewport.createPrimitive(primitive, QStringLiteral("Base proiezione")).isEmpty(), "creazione base proiezione");
        SketchObject sketch;
        sketch.name = QStringLiteral("Profilo sopra");
        sketch.plane = kFacePlane;
        sketch.frame.origin[2] = 12.0;
        const QPointF a(4, 2), b(12, 2), c(12, 6), d(4, 6);
        sketch.segments = {{a, b}, {b, c}, {c, d}, {d, a}};
        sketch.constraints = {-1, -1, -1, -1};
        sketch.segmentLengths = {0, 0, 0, 0};
        sketch.segmentAngles = {-1, -1, -1, -1};
        viewport.sketches_.append(sketch);
        const auto totalArea = [](const ForgeBody &body) {
            double area = 0.0;
            for (FaceId f : body->faces()) area += faceArea(*body, f, 1e-11);
            return area;
        };
        ExtrusionObject cut;
        cut.feature = BodyFeature::SheetTrim; cut.firstBody = 0; cut.sketchIndex = 0; cut.trimProject = true;
        viewport.requestPreview(cut, -1);
        QElapsedTimer timeout; timeout.start();
        while (!viewport.preview_.valid && viewport.preview_.error.isEmpty() && timeout.elapsed() < 20000) QApplication::processEvents();
        require(viewport.preview_.valid && viewport.preview_.geometry, "anteprima taglio proiettato");
        require(viewport.preview_.replaced.contains(0), "l'anteprima della proiezione nasconde la superficie di partenza");
        require(viewport.createSheetTrim(cut, QStringLiteral("Taglio 2")).isEmpty(), "creazione taglio proiettato");
        require(viewport.extrusions_.at(1).feature == BodyFeature::SheetTrim && viewport.extrusions_.at(1).trimProject, "taglio superficie con schizzo proiettato");
        viewport.clearPreview();
        require(viewport.extrusions_.size() == 2 && viewport.extrusions_[0].modelBodyId == viewport.extrusions_[1].modelBodyId,
                "il taglio resta nello stesso corpo logico");
        const auto last = [&] { return viewport.extrusions_.at(1).forgeBody; };
        require(last() && last()->isSheet() && std::fabs(totalArea(last()) - 668.0) < 1e-8, "foro nella faccia superiore, pareti intere");
        require(viewport.extrusions_.at(1).notice.contains(QStringLiteral("4 tratti impressi")), "esito del taglio");
        ExtrusionObject edited = viewport.extrusions_.at(1);
        edited.projectionMode = 1;
        require(viewport.updateBody(1, edited).isEmpty() && std::fabs(totalArea(last()) - 32.0) < 1e-8, "modo: tiene solo la regione");
        viewport.undo(); require(std::fabs(totalArea(last()) - 668.0) < 1e-8, "undo modo del taglio");
        viewport.redo(); require(std::fabs(totalArea(last()) - 32.0) < 1e-8, "redo modo del taglio");
        edited = viewport.extrusions_.at(1); edited.projectionMode = 2;
        require(viewport.updateBody(1, edited).isEmpty() && !last()->isSheet() && last()->faces().size() == 7
                && std::fabs(massProperties(*last()).volume - 1000.0) < 1e-8, "linea di divisione su un solido");
        const auto before = writeBodyBinary(*last());
        edited = viewport.extrusions_.at(1); edited.projectionReverse = true;
        require(!viewport.updateBody(1, edited).isEmpty(), "verso opposto: la proiezione non incontra il corpo");
        require(writeBodyBinary(*last()) == before, "errore non modifica il taglio");

        ExtrusionObject curves;
        curves.feature = BodyFeature::ProjectedCurve; curves.firstBody = 1; curves.sketchIndex = 0; curves.name = QStringLiteral("Curva proiettata 1");
        require(viewport.createBody(curves).isEmpty(), "creazione curva proiettata");
        const ExtrusionObject &curveBody = viewport.extrusions_.last();
        require(curveBody.curves.size() == 1 && curveBody.curve == curveBody.curves.first(), "una curva per il contorno chiuso");
        const Interval domain = curveBody.curve->domain();
        require(std::fabs(arcLength(*curveBody.curve, domain, 1e-10) - 24.0) < 1e-7, "lunghezza del contorno proiettato");
        for (int k = 0; k <= 50; ++k) require(std::fabs(curveBody.curve->point(domain.lo + domain.length() * k / 50).z() - 5.0) < 1e-8, "curva sulla faccia superiore");
        require(!curveBody.display.edges.isEmpty(), "curva visibile");
        GeometryRef ref; ref.kind = 9; ref.index = 2; ref.featureId = curveBody.featureId;
        std::vector<PathSegment> path;
        QString error;
        require(geometryRefPath(ref, 3, viewport.sketches_, viewport.extrusions_, path, &error) && path.size() == 1, "curva proiettata come riferimento");

        QTemporaryDir files;
        const QString path2 = files.path() + QStringLiteral("/proiezione.prt");
        require(saveDocumentFile(path2, viewport.currentDocument(), false).isEmpty(), "salvataggio proiezione senza cache");
        DocumentState loaded;
        require(loadDocumentFile(path2, loaded).isEmpty(), "lettura formato 38");
        require(loaded.extrusions.at(1).feature == BodyFeature::SheetTrim && loaded.extrusions.at(1).trimProject && loaded.extrusions.at(1).projectionMode == 2
                && !loaded.extrusions.at(1).projectionReverse && loaded.extrusions.at(2).feature == BodyFeature::ProjectedCurve,
                "parametri della proiezione persistenti");
        CadViewport reopened; reopened.loadDocument(loaded);
        require(reopened.extrusions_.at(1).forgeBody && reopened.extrusions_.at(1).error.isEmpty() && reopened.extrusions_.at(1).forgeBody->faces().size() == 7,
                "rigenerazione del taglio riletto");
        require(reopened.extrusions_.at(2).curves.size() == 1 && reopened.extrusions_.at(2).error.isEmpty(), "rigenerazione della curva riletta");

        QMainWindow window;
        auto *view = new CadViewport(&window); window.setCentralWidget(view); window.resize(1000, 720);
        view->loadDocument(loaded); window.show();
        bool locked = false;
        QTimer::singleShot(0, &window, [&] {
            locked = view->interactionLocked();
            for (QDialog *dialog : window.findChildren<QDialog *>())
                if (dialog->windowTitle() == QStringLiteral("Test proiezione")) dialog->reject();
        });
        require(!projectionDialog(&window, view, QStringLiteral("Test proiezione"), 1, view->extrusions_.at(1), [](const ExtrusionObject &) { return QString(); }),
                "annullamento pannello proiezione");
        require(locked && !view->interactionLocked() && view->extrusions_.size() == 3, "pannello proiezione esclusivo e ripristino");
        // Taglia superficie: lo schizzo si sceglie cliccandolo nella vista e si
        // proietta di default. Sul solido gia' diviso i quattro lati seguono
        // edge esistenti: si toglie la faccia della regione senza nuovi tagli.
        bool sketchChosen = false, projectionRows = false, previewReady = false;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, &window, [&] {
            QDialog *dialog = nullptr;
            for (QDialog *candidate : window.findChildren<QDialog *>())
                if (candidate->windowTitle() == QStringLiteral("Test taglio") && candidate->isVisible()) dialog = candidate;
            if (!dialog) return;
            poll.stop();
            QVector<QPushButton *> pickers;
            for (QPushButton *button : dialog->findChildren<QPushButton *>())
                if (button->text() == QStringLiteral("Dalla vista")) pickers.append(button);
            if (pickers.size() == 2) {
                pickers.at(1)->click();
                GeometryRef ref; ref.kind = 7; ref.index = 0; ref.element.kind = 0; ref.element.element = 0;
                if (view->refPickFinished_) view->refPickFinished_(true, ref);
            }
            for (QComboBox *box : dialog->findChildren<QComboBox *>()) {
                if (box->currentText() == QStringLiteral("Schizzo: Profilo sopra")) sketchChosen = true;
                if (box->currentText() == QStringLiteral("Proiezione sulla prima faccia incontrata")) projectionRows = box->isVisibleTo(dialog);
            }
            QElapsedTimer wait; wait.start();
            while (!view->preview_.valid && view->preview_.error.isEmpty() && wait.elapsed() < 20000) QApplication::processEvents();
            previewReady = view->preview_.valid;
            for (QDialogButtonBox *buttons : dialog->findChildren<QDialogButtonBox *>()) buttons->button(QDialogButtonBox::Ok)->click();
            // Se il taglio non si applica il pannello resta aperto: si chiude per non bloccare il test.
            QTimer::singleShot(2000, dialog, [dialog] { if (dialog->isVisible()) dialog->reject(); });
        });
        poll.start();
        ExtrusionObject start;
        start.firstBody = 1;  // la superficie da tagliare: lo schizzo si sceglie come strumento
        const bool trimmed = trimDialog(&window, view, QStringLiteral("Test taglio"), -1, start, [view](const ExtrusionObject &d) {
            return view->createSheetTrim(d, QStringLiteral("Taglio dal pannello"));
        });
        if (!(sketchChosen && projectionRows && previewReady))
            std::cerr << "schizzo " << sketchChosen << " righe " << projectionRows << " anteprima " << previewReady << " errore " << view->preview_.error.toStdString() << std::endl;
        require(sketchChosen && projectionRows && previewReady, "schizzo scelto nella vista, proiezione di default e anteprima");
        require(trimmed && view->extrusions_.size() == 4 && view->extrusions_.last().trimProject && view->extrusions_.last().sketchIndex == 0,
                "taglio dal pannello con lo schizzo proiettato");
        require(std::fabs(totalArea(view->extrusions_.last().forgeBody) - 668.0) < 1e-8 && view->extrusions_.last().notice.contains(QStringLiteral("0 tratti impressi")),
                "regione delimitata solo da edge esistenti");
        std::cout << "PASS proiezione: taglio, modi, curva, riferimenti, undo/redo, persistenza e pannello" << std::endl;
    }

    // Spessore: facce di un solido (corpo nuovo), lato e direzione, Undo/Redo,
    // errore senza modifiche, persistenza (formato 39) e pannello con anteprima.
    static void thickenFeature() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        CadViewport viewport;
        PrimitiveParameters primitive;
        primitive.size[0] = 20; primitive.size[1] = 10; primitive.size[2] = 5;
        require(viewport.createPrimitive(primitive, QStringLiteral("Blocco")).isEmpty(), "blocco per lo spessore");
        const auto base = viewport.extrusions_.front().forgeBody;
        FaceId top;
        for (FaceId f : base->faces()) {
            const Frame3 &frame = static_cast<const Plane &>(*base->face(f).surface).frame();
            if (std::fabs(frame.origin().z() - 5.0) < 1e-9 && std::fabs(frame.zDir().z()) > 0.99) top = f;
        }
        require(top.valid(), "faccia superiore");
        ExtrusionObject thicken;
        thicken.feature = BodyFeature::Thicken; thicken.firstBody = 0; thicken.distance = 1.5; thicken.name = QStringLiteral("Spessore 1");
        thicken.offsetFaces = {faceReference(*base, top, Vec3(10.0, 5.0, 5.0))};
        require(viewport.createBody(thicken).isEmpty(), "spessore della faccia superiore");
        const auto volume = [&] { return massProperties(*viewport.extrusions_.at(1).forgeBody).volume; };
        const auto zmin = [&] {
            double z = 1e300;
            for (VertexId v : viewport.extrusions_.at(1).forgeBody->vertices()) z = std::min(z, viewport.extrusions_.at(1).forgeBody->vertex(v).point.z());
            return z;
        };
        require(viewport.extrusions_.at(1).solid && std::fabs(volume() - 300.0) < 1e-9 && std::fabs(zmin() - 5.0) < 1e-12, "lastra sopra la faccia");
        require(viewport.extrusions_.at(1).modelBodyId != viewport.extrusions_.at(0).modelBodyId && viewport.extrusions_.at(0).visible,
                "facce di un solido: corpo nuovo, il solido resta");
        ExtrusionObject edited = viewport.extrusions_.at(1);
        edited.thickenSide = 2;
        edited.thickenDirection.kind = 2; edited.thickenDirection.index = 2;  // asse Z
        require(viewport.updateBody(1, edited).isEmpty() && std::fabs(volume() - 300.0) < 1e-9 && std::fabs(zmin() - 4.25) < 1e-12,
                "meta' per parte lungo l'asse Z");
        viewport.undo(); require(std::fabs(zmin() - 5.0) < 1e-12, "undo dello spessore");
        viewport.redo(); require(std::fabs(zmin() - 4.25) < 1e-12, "redo dello spessore");
        const auto before = writeBodyBinary(*viewport.extrusions_.at(1).forgeBody);
        edited = viewport.extrusions_.at(1);
        edited.thickenDirection.index = 0;  // asse X: parallelo alla faccia
        require(!viewport.updateBody(1, edited).isEmpty(), "direzione parallela alla superficie rifiutata");
        require(writeBodyBinary(*viewport.extrusions_.at(1).forgeBody) == before, "errore non modifica lo spessore");

        // Tutta una lamina (la sola faccia superiore rimasta): solido nello stesso corpo logico.
        {
            CadViewport plate;
            require(plate.createPrimitive(primitive, QStringLiteral("Lastra")).isEmpty(), "blocco per la lamina");
            const auto block = plate.extrusions_.front().forgeBody;
            ExtrusionObject keepTop;
            keepTop.feature = BodyFeature::DeleteFace; keepTop.firstBody = 0; keepTop.name = QStringLiteral("Solo la faccia superiore");
            for (FaceId f : block->faces())
                if (f != top) keepTop.offsetFaces.append(faceReference(*block, f, block->finPoint(block->loop(block->face(f).loops.front()).first, 0.5)));
            require(plate.createBody(keepTop).isEmpty() && plate.extrusions_.at(1).forgeBody->isSheet(), "lamina della faccia superiore");
            ExtrusionObject whole;
            whole.feature = BodyFeature::Thicken; whole.firstBody = 1; whole.distance = 2.0; whole.thickenSide = 1; whole.name = QStringLiteral("Spessore lamina");
            require(plate.createBody(whole).isEmpty(), "spessore di tutta la lamina");
            const ExtrusionObject &solid = plate.extrusions_.at(2);
            require(solid.solid && std::fabs(massProperties(*solid.forgeBody).volume - 400.0) < 1e-9, "lamina ispessita verso il basso");
            require(solid.modelBodyId == plate.extrusions_.at(1).modelBodyId && plate.resultBodiesBefore(-1) == QVector<int>{2},
                    "tutta la superficie: stesso corpo logico, lo spessore e' lo stadio finale");
        }
        QTemporaryDir files;
        const QString path = files.path() + QStringLiteral("/spessore.prt");
        require(saveDocumentFile(path, viewport.currentDocument(), false).isEmpty(), "salvataggio dello spessore");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty(), "lettura formato 39");
        require(loaded.extrusions.at(1).feature == BodyFeature::Thicken && loaded.extrusions.at(1).thickenSide == 2
                && loaded.extrusions.at(1).thickenDirection.kind == 2 && loaded.extrusions.at(1).thickenDirection.index == 2
                && loaded.extrusions.at(1).offsetFaces.size() == 1, "parametri dello spessore persistenti");
        CadViewport reopened; reopened.loadDocument(loaded);
        require(reopened.extrusions_.at(1).forgeBody && std::fabs(massProperties(*reopened.extrusions_.at(1).forgeBody).volume - 300.0) < 1e-9,
                "rigenerazione dello spessore riletto");

        // Pannello: tutta la faccia laterale di un'altra lastra, con anteprima e OK.
        QMainWindow window;
        auto *view = new CadViewport(&window); window.setCentralWidget(view); window.resize(1000, 720);
        view->loadDocument(loaded); window.show();
        bool previewReady = false;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, &window, [&] {
            QDialog *dialog = nullptr;
            for (QDialog *candidate : window.findChildren<QDialog *>())
                if (candidate->windowTitle() == QStringLiteral("Test spessore") && candidate->isVisible()) dialog = candidate;
            if (!dialog) return;
            poll.stop();
            QElapsedTimer wait; wait.start();
            while (!view->preview_.valid && view->preview_.error.isEmpty() && wait.elapsed() < 20000) QApplication::processEvents();
            previewReady = view->preview_.valid;
            for (QDialogButtonBox *buttons : dialog->findChildren<QDialogButtonBox *>()) buttons->button(QDialogButtonBox::Ok)->click();
            QTimer::singleShot(2000, dialog, [dialog] { if (dialog->isVisible()) dialog->reject(); });
        });
        poll.start();
        ExtrusionObject start;
        start.feature = BodyFeature::Thicken; start.firstBody = 0; start.distance = 0.5;
        start.offsetFaces = {faceReference(*view->extrusions_.at(0).forgeBody, top, Vec3(10.0, 5.0, 5.0))};
        const bool made = thickenDialog(&window, view, QStringLiteral("Test spessore"), -1, start, [view](const ExtrusionObject &d) {
            ExtrusionObject result = d;
            result.name = QStringLiteral("Spessore dal pannello");
            return view->createBody(result);
        });
        require(previewReady, "anteprima dello spessore nel pannello");
        require(made && view->extrusions_.size() == 3 && std::fabs(massProperties(*view->extrusions_.last().forgeBody).volume - 100.0) < 1e-9
                && !view->interactionLocked(), "spessore creato dal pannello");
        std::cout << "PASS spessore: facce, lato, direzione, undo/redo, errore, persistenza e pannello" << std::endl;
    }

    static void draftFeature() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        CadViewport viewport;
        PrimitiveParameters primitive;
        primitive.size[0] = 20; primitive.size[1] = 10; primitive.size[2] = 5;
        require(viewport.createPrimitive(primitive, QStringLiteral("Base sformo")).isEmpty(), "creazione base sformo");
        const auto base = viewport.extrusions_.front().forgeBody;
        ExtrusionObject draft;
        draft.feature = BodyFeature::Draft; draft.firstBody = 0; draft.name = QStringLiteral("Sformo 1");
        draft.draftNeutral.kind = 1; draft.draftNeutral.index = 0; draft.draftAngle = 5;
        FaceId bottom;
        for (FaceId f : base->faces()) {
            const auto &frame = static_cast<const Plane &>(*base->face(f).surface).frame();
            const auto point = faceReference(*base, f, frame.origin());
            if (std::fabs(frame.zDir().z()) < 0.1) draft.offsetFaces.append(point);
            else if (std::fabs(frame.origin().z()) < 1e-8) bottom = f;
        }
        require(draft.offsetFaces.size() == 4 && bottom.valid(), "quattro pareti e base neutra");
        viewport.requestPreview(draft, -1);
        QElapsedTimer timeout; timeout.start();
        while (!viewport.preview_.valid && viewport.preview_.error.isEmpty() && timeout.elapsed() < 10000) QApplication::processEvents();
        require(viewport.preview_.valid && viewport.preview_.geometry, "anteprima sformo valida");
        require(viewport.createBody(draft).isEmpty(), "creazione sformo");
        viewport.clearPreview();
        require(viewport.extrusions_.size() == 2 && viewport.extrusions_[0].modelBodyId == viewport.extrusions_[1].modelBodyId,
                "sformo eredita il corpo logico");
        require(viewport.resultBodiesBefore(-1) == QVector<int>{1}, "solo lo sformo e' lo stadio finale");
        const auto volume = [&] { return massProperties(*viewport.extrusions_.last().forgeBody).volume; };
        const double initialVolume = volume();
        ExtrusionObject edited = viewport.extrusions_.last(); edited.draftAngle = 8;
        require(viewport.updateBody(1,edited).isEmpty() && volume() < initialVolume, "modifica dell'angolo rigenera lo sformo");
        viewport.undo(); require(std::fabs(volume()-initialVolume)<1e-8, "undo modifica sformo");
        viewport.redo(); require(volume()<initialVolume, "redo modifica sformo");
        edited = viewport.extrusions_.last();
        edited.draftNeutral.kind = 5; edited.draftNeutral.index = 0;
        edited.draftNeutral.point = faceReference(*base,bottom,Vec3());
        edited.draftReverse = true;
        require(viewport.updateBody(1,edited).isEmpty(), "piano neutro associato alla faccia");
        const double savedVolume = volume();
        QTemporaryDir files;
        const QString path = files.path()+QStringLiteral("/sformo.prt");
        require(saveDocumentFile(path,viewport.currentDocument(),false).isEmpty(), "salvataggio sformo senza cache");
        DocumentState loaded;
        require(loadDocumentFile(path,loaded).isEmpty(), "lettura formato sformo");
        require(loaded.extrusions.last().feature == BodyFeature::Draft && loaded.extrusions.last().draftAngle == 8
                && loaded.extrusions.last().draftReverse && loaded.extrusions.last().draftNeutral.kind == 5
                && loaded.extrusions.last().draftNeutral.featureId != 0, "parametri e riferimento persistenti");
        CadViewport reopened; reopened.loadDocument(loaded);
        require(reopened.extrusions_.last().forgeBody && reopened.extrusions_.last().error.isEmpty(), "rigenerazione sformo riletto");
        require(std::fabs(massProperties(*reopened.extrusions_.last().forgeBody).volume-savedVolume)<1e-8, "volume conservato dopo riapertura");
        const auto before = writeBodyBinary(*viewport.extrusions_.last().forgeBody);
        edited = viewport.extrusions_.last(); edited.draftAngle = 88;
        require(!viewport.updateBody(1,edited).isEmpty(), "rifiuto sformo che collassa il corpo");
        require(writeBodyBinary(*viewport.extrusions_.last().forgeBody)==before, "errore non modifica il corpo");
        // Il pannello usa lo stesso isolamento delle altre funzioni, con anteprima
        // e chiusura tramite Annulla senza aggiungere una feature.
        QMainWindow window;
        auto *view = new CadViewport(&window); window.setCentralWidget(view); window.resize(1000,720);
        view->loadDocument(loaded); window.show();
        bool locked = false;
        QTimer::singleShot(0, &window, [&] {
            locked = view->interactionLocked();
            for (QDialog *dialog : window.findChildren<QDialog *>())
                if (dialog->windowTitle() == QStringLiteral("Test sformo")) dialog->reject();
        });
        require(!draftDialog(&window,view,QStringLiteral("Test sformo"),1,view->extrusions_.last(),[](const ExtrusionObject &) { return QString(); }), "annullamento pannello sformo");
        require(locked && !view->interactionLocked() && view->extrusions_.size()==2, "pannello sformo esclusivo e ripristino");
        std::cout << "PASS sformo: anteprima, storia, modifica, undo/redo, persistenza e pannello" << std::endl;
    }

    static void workflowUi() {
        {
            QMainWindow window;
            CadViewport viewport(&window);
            viewport.resize(800,600);
            viewport.createSketch(0,QStringLiteral("Offset senza preselezione"));
            CurveObject curve;curve.tool=DrawingTool::Converted;curve.degree=3;curve.construction=true;
            curve.controlPoints={{-30,0},{-10,0},{10,0},{30,0}};
            curve.knots={0,0,0,0,1,1,1,1};
            ForgeCad::recalculateCurve(curve,0);
            viewport.sketches_[0].curves.append(curve);
            int ci=-1,pi=-1;EditablePointKind kind;
            require(!viewport.findCurveEditPoint(QPointF(-10,0),ci,pi,kind),"poli Converted non intercettano il clic");
            viewport.setSketchViewUnlocked(true);
            bool opened=false, selected=false, toggled=false, preview=false;
            QTimer::singleShot(0,&window,[&] {
                auto *dialog=window.findChild<QDialog *>();
                if(!dialog) { for(auto *w:QApplication::topLevelWidgets()) if(auto *d=qobject_cast<QDialog *>(w)) d->reject();return; }
                auto *buttons=dialog->findChild<QDialogButtonBox *>();
                opened=viewport.sketchSelection().isEmpty() && buttons && !buttons->button(QDialogButtonBox::Ok)->isEnabled() && !dialog->isModal() && !viewport.sketchViewUnlocked_;
                const QPointF cursor=viewport.projectWorldPoint(viewport.mapSketchPoint(QPointF(-10,0),viewport.sketches_[0]));
                const auto click=[&] {
                    QMouseEvent press(QEvent::MouseButtonPress,cursor,cursor,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    viewport.mousePressEvent(&press);
                    QMouseEvent release(QEvent::MouseButtonRelease,cursor,cursor,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    viewport.mouseReleaseEvent(&release);
                };
                click();
                selected=viewport.sketchSelection().size()==1 && !viewport.draggingControlPoint_;
                preview=!viewport.sketchPatternPreview_.isEmpty() && buttons->button(QDialogButtonBox::Ok)->isEnabled();
                click();
                toggled=viewport.sketchSelection().isEmpty() && viewport.sketchPatternPreview_.isEmpty() && !buttons->button(QDialogButtonBox::Ok)->isEnabled();
                // Anche il riquadro deve selezionare, senza avviare l'orbita.
                const QPointF from=viewport.projectWorldPoint(viewport.mapSketchPoint(QPointF(-35,5),viewport.sketches_[0]));
                const QPointF to=viewport.projectWorldPoint(viewport.mapSketchPoint(QPointF(35,-5),viewport.sketches_[0]));
                QMouseEvent press(QEvent::MouseButtonPress,from,from,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                viewport.mousePressEvent(&press);
                QMouseEvent move(QEvent::MouseMove,to,to,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                viewport.mouseMoveEvent(&move);
                QMouseEvent release(QEvent::MouseButtonRelease,to,to,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                viewport.mouseReleaseEvent(&release);
                selected=selected && viewport.sketchSelection().size()==1;
                dialog->reject();
            });
            require(!sketchOffsetDialog(&window,&viewport),"annulla offset");
            require(opened && selected && preview && toggled,"pannello offset immediato e selezione per clic");
            require(!viewport.sketchOffsetSelectionCallback_ && viewport.sketchPatternPreview_.isEmpty()
                    && viewport.sketches_[0].curves.size()==1,"annulla ripulisce selezione offset e anteprima");
            viewport.sketchSelections_={{1,0}};
            QTimer::singleShot(0,&window,[&] {
                if(auto *dialog=window.findChild<QDialog *>())
                    if(auto *buttons=dialog->findChild<QDialogButtonBox *>()) buttons->button(QDialogButtonBox::Ok)->click();
            });
            require(sketchOffsetDialog(&window,&viewport),"conferma offset con preselezione");
            require(viewport.sketches_[0].curves.size()==2 && !viewport.sketchOffsetSelectionCallback_
                    && viewport.sketchPatternPreview_.isEmpty(),"offset confermato e callback rimossa");
            viewport.undo();
            require(viewport.sketches_[0].curves.size()==1,"undo offset da pannello");
        }

        {
            using namespace ForgeCad;
            QMainWindow window;
            CadViewport viewport(&window);viewport.resize(800,600);
            ExtrusionObject block;block.feature=BodyFeature::Imported;
            block.forgeBody=std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3(),20,10,5));
            forgeTessellate(*block.forgeBody,0,block.display);
            viewport.extrusions_={block};
            viewport.createSketch(0,QStringLiteral("Offset bordo diretto"));
            bool picked=false;
            const auto schedule=[&](bool accept) {
                QTimer::singleShot(0,&window,[&,accept] {
                    auto *dialog=window.findChild<QDialog *>();if(!dialog)return;
                    const QPointF cursor=viewport.projectWorldPoint(QVector3D(10,0,5));
                    QMouseEvent press(QEvent::MouseButtonPress,cursor,cursor,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    viewport.mousePressEvent(&press);
                    QMouseEvent release(QEvent::MouseButtonRelease,cursor,cursor,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    viewport.mouseReleaseEvent(&release);
                    picked=viewport.sketchSelection().size()==1 && viewport.sketches_[0].segments.size()==1
                        && viewport.sketches_[0].isConstructionSegment(0);
                    auto *buttons=dialog->findChild<QDialogButtonBox *>();
                    if(accept && buttons && buttons->button(QDialogButtonBox::Ok)->isEnabled()) buttons->button(QDialogButtonBox::Ok)->click();
                    else dialog->reject();
                });
            };
            schedule(false);
            require(!sketchOffsetDialog(&window,&viewport) && picked && viewport.sketches_[0].segments.isEmpty(),"annulla copia bordo provvisoria");
            schedule(true);
            require(sketchOffsetDialog(&window,&viewport) && picked && viewport.sketches_[0].segments.size()==2,"copia bordo e offset in un comando");
            const int ci=viewport.sketches_[0].geometricConstraints.size()-1;
            require(viewport.sketches_[0].geometricConstraints[ci].type==ConstraintType::Offset,"quota del bordo copiato");
            require(viewport.setConstraintValue(ci,0.5).isEmpty(),"modifica quota offset da viewport");
            require(std::fabs(std::fabs(viewport.sketches_[0].segments[1].first.y())-0.5)<1e-7,"distanza aggiornata da pannello quote");
            viewport.undo();viewport.undo();
            require(viewport.sketches_[0].segments.isEmpty(),"undo unico per bordo e offset");
        }

        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        QMainWindow window;
        auto *viewport = new CadViewport(&window);
        window.setCentralWidget(viewport);
        window.resize(1000, 720);
        window.show();
        auto *action = window.menuBar()->addAction(QStringLiteral("Modifica"));
        {
            QDialog keep(&window);
            WindowLock outer(&window, viewport, &keep);
            { WindowLock inner(&window, viewport, &keep); }
            require(viewport->interactionLocked(), "un blocco annidato non sblocca quello esterno");
        }
        require(!viewport->interactionLocked() && action->isEnabled(), "blocco ripristinato");
        {
            FunctionDialogPanel panel(&window);
            panel.setPanelOpacity(45);
            panel.show();
            require(viewport->interactionLocked() && !action->isEnabled(), "ogni pannello funzione blocca gli altri comandi");
            viewport->lastMousePosition_ = QPoint(-100, -100);
            const QPoint pos = panel.pos() + QPoint(3, 3);
            QMouseEvent press(QEvent::MouseButtonPress, pos, viewport->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &press);
            QMouseEvent twice(QEvent::MouseButtonDblClick, pos, viewport->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &twice);
            require(viewport->lastMousePosition_ == QPoint(-100, -100), "la zona trasparente del pannello intercetta i clic");
            require(panel.testAttribute(Qt::WA_NoMousePropagation), "sfondo pannello senza propagazione al viewport");
            panel.hide();
            require(!viewport->interactionLocked() && action->isEnabled(), "chiusura pannello ripristina comandi");
        }
        // Lo stesso percorso usato dalle opzioni STL e OBJ: la finestra resta
        // aperta durante l'orbita, senza selezionare o modificare la scena.
        for (int format = 0; format < 2; ++format) {
            QDialog options(&window);
            bool locked = false, rotated = false, selected = false;
            QTimer::singleShot(0, &options, [&] {
                locked = viewport->interactionLocked() && !options.isModal();
                const float beforeYaw = viewport->camera_.yaw_, beforePitch = viewport->camera_.pitch_;
                const SceneSelection selection = viewport->selection_;
                const QPoint from(800, 400), to(860, 430);
                QMouseEvent press(QEvent::MouseButtonPress, from, viewport->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &press);
                QMouseEvent move(QEvent::MouseMove, to, viewport->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &move);
                QMouseEvent release(QEvent::MouseButtonRelease, to, viewport->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &release);
                rotated = viewport->camera_.yaw_ != beforeYaw || viewport->camera_.pitch_ != beforePitch;
                selected = viewport->selection_ == selection;
                options.accept();
            });
            require(runModeless(&window, viewport, options), "opzioni esportazione confermate");
            require(locked && rotated && selected, "orbita consentita senza selezione durante esportazione");
            require(!viewport->interactionLocked(), "esportazione ripristina interazione");
        }
        QTemporaryDir files;
        std::vector<ExchangeBody> bodies;
        for (int i = 0; i < 200; ++i) {
            ExchangeBody body;
            body.name = "corpo " + std::to_string(i);
            body.body = makeBox(Frame3(Vec3(0, i * 5, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4, 3, 2);
            bodies.push_back(std::move(body));
        }
        for (bool iges : {false, true}) {
            const QString path = files.path() + (iges ? QStringLiteral("/test.igs") : QStringLiteral("/test.step"));
            const std::string content = iges ? writeIges(bodies) : writeStep(bodies);
            QFile file(path);
            require(file.open(QIODevice::WriteOnly), "file import di prova");
            file.write(content.data(), qsizetype(content.size())); file.close();
            QTimer heartbeat;
            int ticks = 0;
            QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
            heartbeat.start(1);
            QVector<ImportedPart> parts;
            QStringList notes;
            require(viewport->readImportFile(path, parts, notes).isEmpty(), "importazione dal worker");
            require(parts.size() == 200 && ticks > 0, "GUI attiva durante lettura STEP/IGES");
            int last = -1;
            bool mainThread = true, monotonic = true;
            const int meshTicks = ticks;
            const QStringList failures = viewport->importParts(parts, path, [&](int done, int total) {
                mainThread = mainThread && QThread::currentThread() == qApp->thread();
                monotonic = monotonic && done >= last && done <= total;
                last = done;
            });
            heartbeat.stop();
            require(failures.empty() && last == 200 && mainThread && monotonic, "avanzamento mesh sul thread GUI fino al totale");
            require(ticks > meshTicks && viewport->extrusions_.size() == 200, "GUI attiva durante mesh e pubblicazione completa");
            viewport->undo();
            require(viewport->extrusions_.isEmpty(), "importazione annullabile in un solo passo");
        }
        QVector<ImportedPart> parts;
        QStringList notes;
        require(!viewport->readImportFile(files.path() + "/assente.step", parts, notes).isEmpty(), "errore import riportato dal worker");
        require(viewport->extrusions_.isEmpty(), "errore import non modifica il documento");
        std::cout << "PASS workflow UI: import responsive, orbita export, pannelli esclusivi" << std::endl;
    }

    // Analisi -> Micro-geometrie sul vecchio corpo finale di Loft_offset
    // (importato da STEP): le due strisce sottili, i quattro spigoli corti e i
    // due giunti quasi tangenti, tutti ritrovati come riferimenti nella vista.
    static void microFeatureMarksTest() {
        using namespace ForgeCad;
        std::ifstream in(std::string(FORGECAD_SOURCE_DIR) + "/kernel/tests/data/loft_offset_trimmed.body", std::ios::binary);
        require(bool(in), "fixture del corpo rumoroso");
        std::stringstream content;
        content << in.rdbuf();
        const Kernel::Body noisy = Kernel::readBodyBinary(content.str());
        DocumentState state;
        ExtrusionObject body;
        body.feature = BodyFeature::Imported;
        body.name = QStringLiteral("Corpo finale rumoroso");
        body.importSource = QStringLiteral("loft_offset_trimmed.body");
        Kernel::ExchangeBody exchange;
        exchange.name = "corpo";
        exchange.body = noisy;
        body.importData = QByteArray::fromStdString(Kernel::writeStep({exchange}));
        state.extrusions.append(body);
        CadViewport viewport;
        viewport.loadDocument(state);
        require(viewport.extrusions_.size() == 1 && viewport.extrusions_.front().forgeBody, "corpo importato");
        QStringList lines;
        const QVector<GeometryRef> marks = microFeatureMarks(viewport.extrusions_.front(), 0, lines);
        int thin = 0, shortEdges = 0, nearTangent = 0;
        for (const QString &line : lines) {
            thin += line.startsWith(QStringLiteral("Faccia sottile"));
            shortEdges += line.startsWith(QStringLiteral("Spigolo corto"));
            nearTangent += line.startsWith(QStringLiteral("Spigolo quasi tangente"));
        }
        std::cout << lines.join(QLatin1Char('\n')).toStdString() << std::endl;
        require(thin == 2 && shortEdges == 4 && nearTangent == 2, "micro-geometrie del corpo rumoroso");
        for (const GeometryRef &mark : marks) {
            ResolvedRef resolved;
            QString reason;
            if (!resolveGeometryRef(mark, 1, viewport.sketches_, viewport.extrusions_, resolved, &reason))
                std::cout << "tipo " << mark.kind << " id " << mark.point.subshape << ": " << reason.toStdString() << std::endl;
            require(reason.isEmpty(), "segno risolto nella vista");
        }
        viewport.setReferenceMarks(marks);
        require(viewport.refMarks_.size() == marks.size(), "segni nella vista");
        // Il raccordo inferiore R1 fallisce ancora su questo corpo: l'errore
        // indica le micro-geometrie vicine agli spigoli scelti.
        const ForgeBody base = viewport.extrusions_.front().forgeBody;
        QVector<EdgePoint> bottom;
        for (Kernel::FaceId face : base->faces()) {
            if (base->face(face).surface->type() != Kernel::SurfaceType::Plane) continue;
            const auto box = Kernel::faceBox(*base, face);
            if (std::fabs(box.lo.y() + 0.5) > 1e-6 || std::fabs(box.hi.y() + 0.5) > 1e-6) continue;
            for (Kernel::EdgeId edge : faceBoundaryEdges(*base, face)) {
                const auto &geometry = base->edge(edge);
                bottom.append(edgeReference(*base, edge, geometry.curve->point(0.5 * (geometry.range.lo + geometry.range.hi))));
            }
        }
        require(bottom.size() == 6, "contorno del fondo");
        QString error;
        const ForgeBody blended = forgeBlend(base, bottom, 1.0, false, &error);
        std::cout << error.toStdString() << std::endl;
        require(!blended && error.contains(QStringLiteral("micro-geometrie")) && error.contains(QStringLiteral("faccia sottile")),
                "errore del raccordo con le micro-geometrie vicine");
    }
    // --fill-document [file.prt [copia.prt]]: superficie di riempimento di
    // "superfice piana influenzata.prt": contorno = cerchio dello Schizzo 4,
    // guide = archi degli Schizzi 2 e 3, faccia adiacente = il cono della
    // Rivoluzione 1 (rilevata dal bordo libero).
    static void fillDocument(const QStringList &args) {
        using namespace ForgeCad;
        const QString path = args.value(0, QStringLiteral(FORGECAD_SOURCE_DIR "/tests/data/superfice_piana_influenzata.prt"));
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento riempimento");
        for (int i = 0; i < document.extrusions.size(); ++i)
            if (!document.extrusions[i].forgeBody) ForgeCad::buildGeometry(document.extrusions[i], i, document.sketches, document.extrusions);
        require(document.sketches.size() >= 4 && !document.extrusions.isEmpty() && document.extrusions.first().forgeBody, "documento atteso");
        const auto entity = [](int sketch) {
            GeometryRef ref;
            ref.kind = 7;
            ref.index = sketch;
            ref.element = {1, 0, -1};
            return ref;
        };
        ExtrusionObject fill;
        fill.feature = BodyFeature::FillSurface;
        fill.operation = -1;
        fill.name = QStringLiteral("Riempimento 1");
        fill.planarRefs = {entity(3)};
        fill.ruledFirst = {entity(1), entity(2)};
        const int index = int(document.extrusions.size());
        std::vector<Kernel::PathSegment> boundary;
        require(geometryRefPath(fill.planarRefs.first(), index, document.sketches, document.extrusions, boundary, nullptr), "contorno");
        const QVector<EdgePoint> faces = forgeFillContactFaces(boundary, *document.extrusions.first().forgeBody);
        require(faces.size() == 1, "faccia adiacente rilevata sul bordo libero della rivoluzione");
        GeometryRef face;
        face.kind = 5;
        face.index = 0;
        face.featureId = document.extrusions.first().featureId;
        face.point = faces.first();
        fill.ruledSecond = {face};
        for (int continuity : {0, 1, 2}) {
            ExtrusionObject body = fill;
            body.fillContinuity = continuity;
            if (qEnvironmentVariableIsSet("FILL_INFLUENCE")) body.fillInfluence = qEnvironmentVariable("FILL_INFLUENCE").toDouble();
            if (qEnvironmentVariableIsSet("FILL_STEP_CONTINUITY") && args.size() > 2 && continuity == qEnvironmentVariableIntValue("FILL_STEP_CONTINUITY")) {
                ForgeCad::buildGeometry(body, index, document.sketches, document.extrusions);
                std::ofstream step(args.at(2).toStdString());
                step << Kernel::writeStep({{"riempimento", Kernel::sewSheets({document.extrusions.first().forgeBody.get(), body.forgeBody.get()}, 1e-6, true).body, false, {0.0, 0.0, 0.0}, nullptr, {}}});
            }
            QElapsedTimer timer;
            timer.start();
            ForgeCad::buildGeometry(body, index, document.sketches, document.extrusions);
            std::cout << "continuita' " << continuity << ": " << timer.elapsed() << " ms, " << body.error.toStdString() << body.notice.toStdString() << std::endl;
            require(body.forgeBody && body.forgeBody->isSheet() && Kernel::checkBody(*body.forgeBody).empty(), "riempimento valido");
            require(continuity == 0 || body.notice.contains(QStringLiteral("non sono compatibili")), "conflitto guide/tangenza segnalato");
            const Kernel::SewResult sewn = Kernel::sewSheets({document.extrusions.first().forgeBody.get(), body.forgeBody.get()}, 1e-6, true);
            require(sewn.closed && sewn.solid, "riempimento e rivoluzione cuciti in un solido");
            if (continuity == 1) {
                fill = body;
                if (args.size() > 2 && !qEnvironmentVariableIsSet("FILL_STEP_CONTINUITY")) {
                    std::ofstream step(args.at(2).toStdString());
                    step << Kernel::writeStep({{"riempimento", sewn.body, false, {0.0, 0.0, 0.0}, nullptr, {}}});
                }
            }
        }
        if (args.size() > 1 && !args.at(1).isEmpty()) {
            // Come dal comando: la feature nuova con identita' e corpo logico propri.
            DocumentState original;
            require(loadDocumentFile(path, original).isEmpty(), "rilettura documento");
            CadViewport viewport;
            viewport.loadDocument(original);
            fill.forgeBody.reset();
            fill.display = {};
            require(viewport.createBody(fill).isEmpty(), "creazione del riempimento");
            require(saveDocumentFile(args.at(1), viewport.currentDocument(), true).isEmpty(), "salvataggio copia");
            DocumentState reloaded;
            require(loadDocumentFile(args.at(1), reloaded).isEmpty() && reloaded.extrusions.back().feature == BodyFeature::FillSurface
                        && reloaded.extrusions.back().ruledSecond.size() == 1 && reloaded.extrusions.back().fillContinuity == 1,
                    "rilettura del riempimento");
        }
    }
    // --rebuild-document in.prt out.prt [da]: ricalcola le feature da `da` in
    // poi con il kernel attuale (senza toccare i riferimenti) e salva la copia
    // con lo snapshot nuovo.
    static void rebuildDocument(const QStringList &args) {
        using namespace ForgeCad;
        require(args.size() >= 2, "uso: --rebuild-document in.prt out.prt [da]");
        DocumentState state;
        require(loadDocumentFile(args.at(0), state).isEmpty(), "lettura documento da ricalcolare");
        const int from = args.value(2, QStringLiteral("0")).toInt();
        for (int index = 0; index < state.extrusions.size(); ++index) {
            ExtrusionObject &feature = state.extrusions[index];
            if (index < from && feature.forgeBody) continue;
            if (feature.suppressed) continue;
            feature.cachedGeometry = false;
            ForgeCad::buildGeometry(feature, index, state.sketches, state.extrusions);
            std::cout << index << " " << feature.name.toStdString() << ": " << (feature.error.isEmpty() ? "ok" : feature.error.toStdString()) << std::endl;
        }
        CadViewport viewport;
        viewport.loadDocument(state);
        require(saveDocumentFile(args.at(1), viewport.currentDocument(), true).isEmpty(), "salvataggio copia ricalcolata");
    }
    // Nomi e ID nel viewport: rendering, clic e corpi senza nomi registrati.
    static void topologyLabels() {
        using namespace ForgeCad;
        {
            CadViewport topology;
            topology.resize(800, 600);
            PrimitiveParameters box;
            box.kind = PrimitiveKind::Box;
            box.size[0] = 4.0;
            box.size[1] = 3.0;
            box.size[2] = 2.0;
            require(topology.createPrimitive(box, QStringLiteral("Blocco ID")).isEmpty(),
                    "corpo per le etichette topologiche");
            topology.fitAll();
            topology.setTopologyIdsVisible(true);
            const QVector<CadViewport::TopologyLabelItem> labels = topology.topologyLabelItems();
            require(!labels.isEmpty(), "etichette topologiche visibili");
            CadViewport::TopologyLabelItem edgeLabel, faceLabel;
            bool haveEdge = false, haveFace = false;
            for (const CadViewport::TopologyLabelItem &item : labels) {
                require(pointDistance(item.anchor, item.box.center()) > 8.0,
                        "spazio per la freccia dell'etichetta topologica");
                if (item.edge && !haveEdge) edgeLabel = item, haveEdge = true;
                if (!item.edge && !haveFace) faceLabel = item, haveFace = true;
            }
            require(haveEdge && haveFace, "etichette di bordi e facce");
            const auto geometry = topology.extrusions_.first().forgeBody;
            for (const auto &item : labels) {
                const QString name = ForgeCad::elementName(*geometry,
                    item.edge ? Kernel::EntityKind::Edge : Kernel::EntityKind::Face, item.subshape);
                require(!name.isEmpty() && item.text.endsWith(QChar(u'\n') + name), "nomi topologici nelle etichette");
                require(item.box.height() > QFontMetrics(uiFont(8, QFont::DemiBold)).height(), "riquadro per entrambe le righe");
                require(QRectF(topology.rect()).contains(item.box), "etichette intere ai margini del viewport");
            }
            topology.extrusions_[0].forgeBody = std::make_shared<const Kernel::Body>(*geometry);
            const auto unnamedLabels = topology.topologyLabelItems();
            require(unnamedLabels.size() == labels.size(), "etichette del B-rep non registrato");
            for (const auto &item : unnamedLabels)
                require(!item.text.contains(QChar(u'\n')), "ripiego sull'ID in assenza di nomi");
            topology.extrusions_[0].forgeBody = geometry;
            const auto click = [&](const CadViewport::TopologyLabelItem &item) {
                const QPointF p = item.box.center();
                QMouseEvent press(QEvent::MouseButtonPress, p, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                topology.mousePressEvent(&press);
            };
            click(edgeLabel);
            require(topology.topologyHighlightBody_ == edgeLabel.body
                        && topology.topologyHighlightEdge_ == edgeLabel.subshape
                        && topology.topologyHighlightFace_ < 0,
                    "clic sull'ID illumina il bordo");
            click(edgeLabel);
            require(topology.topologyHighlightBody_ < 0, "secondo clic spegne il bordo");
            click(faceLabel);
            require(topology.topologyHighlightBody_ == faceLabel.body
                        && topology.topologyHighlightFace_ == faceLabel.subshape
                        && topology.topologyHighlightEdge_ < 0,
                    "clic sull'ID della faccia illumina il contorno");
            QImage topologyLabels(topology.size(), QImage::Format_ARGB32_Premultiplied);
            topologyLabels.fill(QColor(35, 45, 58));
            QPainter topologyPainter(&topologyLabels);
            topology.drawTopologyLabels(topologyPainter);
            topologyPainter.end();
            require(topologyLabels.save(QStringLiteral("/tmp/forgecad-topology-labels.png")),
                    "rendering delle frecce topologiche");
            topology.setTopologyIdsVisible(false);
            require(topology.topologyHighlightBody_ < 0, "nascondere gli ID spegne l'evidenziazione");
        }
    }

    static void topologyLabelVisibility() {
        using namespace ForgeCad;
        CadViewport view;
        view.resize(800, 600);
        PrimitiveParameters box;
        box.size[0] = box.size[1] = box.size[2] = 4;
        require(view.createPrimitive(box, QStringLiteral("Visibilita' etichette")).isEmpty(), "blocco per occlusione etichette");
        view.setViewFrame(QVector3D(1, 0, 0), QVector3D(0, 0, 1));
        view.fitAll();
        view.setTopologyIdsVisible(true);
        const auto front = view.topologyLabelItems();
        require(!front.isEmpty(), "etichette anteriori visibili");
        int faces = 0, edges = 0;
        for (const auto &item : front) {
            if (item.edge) { ++edges; continue; }
            ++faces;
            require(item.text.endsWith(QStringLiteral(":+z")), "solo la faccia verso l'osservatore ha un'etichetta");
        }
        require(faces == 1 && edges == 4, "faccia e quattro bordi anteriori; etichette posteriori nascoste");
        view.setViewFrame(QVector3D(1, 0, 0), QVector3D(0, 0, -1));
        view.fitAll();
        const auto back = view.topologyLabelItems();
        faces = 0;
        for (const auto &item : back) if (!item.edge) {
            ++faces;
            require(item.text.endsWith(QStringLiteral(":-z")), "ruotando compare la faccia prima nascosta");
        }
        require(faces == 1, "rotazione aggiorna le etichette visibili");
        for (const auto &item : front) for (const auto &other : back)
            require(item.edge != other.edge || item.subshape != other.subshape, "le entita' posteriori non mantengono etichette anteriori");
        view.setViewFrame(QVector3D(1, 0, 0), QVector3D(0, 0, 1));
        view.fitAll();
        box.origin[2] = 8;
        require(view.createPrimitive(box, QStringLiteral("Occludente")).isEmpty(), "secondo corpo davanti al primo");
        view.selection_ = {SceneObjectKind::Extrusion, 0};
        view.hover_ = {};
        require(view.topologyLabelItems().isEmpty(), "anche un altro corpo copre le etichette del corpo selezionato");
        view.extrusions_[1].visible = false;
        require(view.topologyLabelItems().size() == front.size(), "un corpo nascosto non copre le etichette");
        view.selection_ = {};
        view.setTopologyIdsVisible(false);
    }

    // Nomi persistenti, facce divise, ambiguita' e persistenza nel formato 49.
    static void genealogyReferences() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        {
            ExtrusionObject root;
            root.feature = BodyFeature::Primitive;
            root.featureId = 90100;
            root.forgeBody = std::make_shared<const Body>(makeBox(Frame3(), 2, 3, 4));
            registerFeatureGenealogy(root, 0, {}, {});
            const auto sourceNames = elementNamesOf(*root.forgeBody);
            require(bool(sourceNames), "nomi della base per l'offset dichiarato");
            for (double d : {0.0, 0.2, -0.2}) {
                QString error;
                ExtrusionObject offset;
                offset.feature = BodyFeature::SurfaceOffset;
                offset.firstBody = 0;
                offset.featureId = 90101;
                offset.distance = d;
                offset.forgeBody = forgeOffsetFaces(root.forgeBody, {}, d, &error);
                require(offset.forgeBody && error.isEmpty(), "costruzione offset con genealogia dichiarata");
                require(!featureHistoryOf(*offset.forgeBody), "una dichiarazione preparata non registra da sola la feature");
                registerFeatureGenealogy(offset, 1, {}, {root});
                const auto history = featureHistoryOf(*offset.forgeBody);
                const auto names = elementNamesOf(*offset.forgeBody);
                require(history && names, "dichiarazioni acquisite dalla registrazione della feature");
                for (FaceId f : offset.forgeBody->faces()) {
                    require(history->isDeclared(EntityKind::Face, f.index), "provenienza della faccia emessa dall'offset");
                    const auto &links = history->origins(EntityKind::Face, f.index);
                    require(links.size() == 1, "una sola faccia originale per ogni faccia a distanza");
                    require(names->primary(EntityKind::Face, f.index) == sourceNames->primary(EntityKind::Face, links.front().index),
                        "nome originale conservato cambiando superficie e distanza");
                    require(links.front().relation == (d == 0 ? HistoryRelation::Same : HistoryRelation::Modified),
                        "copia invariata e offset modificato distinti");
                    require(entityEvolutionText(*offset.forgeBody, EntityKind::Face, f.index).contains(QStringLiteral("dichiarata")),
                        "diagnostica distingue l'origine dichiarata");
                }
                // Anteprima confermata: registrare lo stesso B-rep e'
                // idempotente, anche quando il thread ha gia' calcolato i nomi.
                registerFeatureGenealogy(offset, 1, {}, {root});
                require(featureHistoryOf(*offset.forgeBody) == history, "conferma anteprima conserva la storia gia' calcolata");
            }
            QString error;
            ExtrusionObject mismatch;
            mismatch.feature = BodyFeature::SurfaceOffset;
            mismatch.firstBody = 0;
            mismatch.featureId = 90102;
            mismatch.forgeBody = forgeOffsetFaces(root.forgeBody, {}, 0.2, &error);
            ExtrusionObject other = root;
            other.forgeBody = std::make_shared<const Body>(*root.forgeBody);
            registerFeatureGenealogy(other, 0, {}, {});
            registerFeatureGenealogy(mismatch, 1, {}, {other});
            const auto mismatched = featureHistoryOf(*mismatch.forgeBody);
            require(bool(mismatched), "ripiego per operando diverso da quello della dichiarazione");
            for (FaceId f : mismatch.forgeBody->faces())
                require(!mismatched->isDeclared(EntityKind::Face, f.index), "nessun riuso di dichiarazioni su un altro B-rep");
        }
        // Caso legacy di 1.prt: ID e punto descrivono due spigoli diversi,
        // ma la base non e' nota. Il save deve rispettare la stessa scelta
        // per geometria di buildGeometry, non introdurre il nome dell'altro.
        {
            ExtrusionObject root;root.feature=BodyFeature::Primitive;root.featureId=100;
            root.forgeBody=std::make_shared<const Body>(makeBox(Frame3(),10,10,10));
            registerFeatureGenealogy(root,0,{},{});
            const EdgeId top=nearestEdge(*root.forgeBody,Vec3(5,0,10),1e-8);
            const EdgeId bottom=nearestEdge(*root.forgeBody,Vec3(5,0,0),1e-8);
            auto reference=edgeReference(*root.forgeBody,top,Vec3(5,0,0));reference.name.clear();
            ExtrusionObject legacy;legacy.feature=BodyFeature::Blend;legacy.featureId=101;
            legacy.firstBody=0;legacy.blendSize=0.5;legacy.blendEdges={reference};
            const auto before=forgeBlend(root.forgeBody,legacy.blendEdges,0.5,false,nullptr,{},false);
            QVector<ExtrusionObject> migrated{root,legacy};upgradeTopologyReferences(migrated);
            require(migrated[1].blendEdges[0].subshape==bottom.index&&!migrated[1].blendEdges[0].name.isEmpty(),
                    "migrazione su base ignota conserva il bordo effettivamente risolto");
            const auto after=forgeBlend(root.forgeBody,migrated[1].blendEdges,0.5,false,nullptr,{},false);
            require(before&&after&&writeBodyBinary(*before)==writeBodyBinary(*after),
                    "il salvataggio legacy non cambia la geometria del raccordo");
            legacy.blendBaseFeature=root.featureId;migrated={root,legacy};upgradeTopologyReferences(migrated);
            require(migrated[1].blendEdges[0].subshape==top.index,"sulla base nota resta autorevole l'ID");
            EdgePoint facePoint;facePoint.x=facePoint.y=5;facePoint.z=10;
            const FaceId topFace=resolveFaceReference(*root.forgeBody,facePoint,1e-6);
            auto face=faceReference(*root.forgeBody,topFace,Vec3(5,5,10));face.name.clear();face.role=kEdgePointFaceBoundary;
            legacy.blendEdges={face};migrated={root,legacy};upgradeTopologyReferences(migrated);
            require(!migrated[1].blendEdges[0].name.isEmpty()&&migrated[1].blendEdges[0].role==kEdgePointFaceBoundary,
                    "migrazione dei riferimenti di faccia conserva il ruolo bordi della faccia");
        }
        DocumentState doc;
        ExtrusionObject box; box.feature = BodyFeature::Primitive; box.name = QStringLiteral("Base");
        box.primitive.size[0] = box.primitive.size[1] = box.primitive.size[2] = 10;
        ExtrusionObject slot; slot.feature = BodyFeature::Primitive; slot.name = QStringLiteral("Cava");
        slot.primitive.origin[0] = 4; slot.primitive.origin[1] = -1; slot.primitive.origin[2] = 5;
        slot.primitive.size[0] = 2; slot.primitive.size[1] = 12; slot.primitive.size[2] = 10;
        ExtrusionObject cut; cut.name = QStringLiteral("Taglio"); cut.operation = 2; cut.firstBody = 0; cut.secondBody = 1;
        doc.extrusions = {box, slot, cut};
        CadViewport view;
        view.loadDocument(doc);
        require(view.extrusions_.size() == 3 && view.extrusions_.at(2).forgeBody && view.extrusions_.at(2).error.isEmpty(), "taglio costruito");
        const ForgeBody geometry = view.extrusions_.at(2).forgeBody;
        const auto faceThrough = [&](const Body &body, const Vec3 &p) {
            for (FaceId f : body.faces()) {
                const SurfaceProjection q = projectPoint(*body.face(f).surface, p);
                if (q.distance <= 1e-9 && classifyPointOnFace(body, f, p, 1e-9) == PointLocation::Inside) return f;
            }
            return FaceId();
        };
        const FaceId left = faceThrough(*geometry, Vec3(1, 5, 10)), right = faceThrough(*geometry, Vec3(9, 5, 10));
        require(left.valid() && right.valid() && left != right, "faccia superiore divisa in due");
        EdgePoint ref = faceReference(*geometry, left, Vec3(1, 5, 10));
        require(!ref.name.isEmpty() && ref.name.endsWith(QStringLiteral(":+z")), "nome semantico della faccia della primitiva");
        require(elementNamesOf(*geometry)->find(EntityKind::Face, ref.name.toStdString()).size() == 2, "i due pezzi hanno lo stesso nome");
        clearReferenceWarnings();
        require(resolveFaceReference(*geometry, ref, 1e-3, ReferenceState::Other) == left, "il punto sceglie il pezzo giusto");
        // Indice sbagliato (come dopo uno spostamento nella storia): vale il nome con il punto.
        EdgePoint shifted = ref;
        shifted.subshape = right.index;
        require(resolveFaceReference(*geometry, shifted, 1e-3, ReferenceState::Same) == left, "il nome vale piu' dell'indice");
        require(takeReferenceWarnings().isEmpty(), "nessun avviso con un solo pezzo sul punto");
        EdgePoint gap = ref;
        gap.x = 5;
        require(resolveFaceReference(*geometry, gap, 1e-3, ReferenceState::Other).valid(), "riferimento ambiguo comunque risolto");
        const QStringList warnings = takeReferenceWarnings();
        require(warnings.size() == 1 && warnings.at(0).contains(QStringLiteral("ambiguo")), "riferimento ambiguo segnalato");

        // Anche i pezzi omonimi hanno etichette distinte e selezionabili per ID.
        view.resize(1000, 700);
        view.setViewFrame(QVector3D(1, 0, 0), QVector3D(0, 0, 1));
        view.fitAll();
        view.setTopologyIdsVisible(true);
        const auto labels = view.topologyLabelItems();
        int splitLabels = 0, namedEdges = 0;
        for (const auto &item : labels) {
            const QString name = elementName(*geometry, item.edge ? EntityKind::Edge : EntityKind::Face, item.subshape);
            require(!name.isEmpty() && item.text.endsWith(QChar(u'\n') + name), "nome persistente nell'etichetta del viewport");
            require(item.text.startsWith(QStringLiteral("B2:%1%2").arg(item.edge ? QChar(u'E') : QChar(u'F')).arg(item.subshape)),
                    "ID della sotto-entita' conservato accanto al nome");
            if (!item.edge && (item.subshape == left.index || item.subshape == right.index)) {
                require(item.text.endsWith(ref.name), "nome ereditato della faccia divisa nel viewport");
                ++splitLabels;
            }
            if (item.edge) ++namedEdges;
        }
        require(splitLabels == 2 && namedEdges > 0, "etichette delle due facce omonime e degli spigoli");
        view.setTopologyIdsVisible(false);
        require(view.topologyLabelItems().isEmpty(), "etichette disattivabili");

        // Evoluzione e risoluzione strutturata, indipendenti dal testo degli avvisi.
        require(entityEvolutionText(*geometry, EntityKind::Face, left.index).contains(QStringLiteral("divisa (2 pezzi)")),
                "descrizione della faccia divisa con numero dei pezzi");
        const QString cutEvolution = featureEvolutionText(*geometry);
        require(cutEvolution.contains(QStringLiteral("invariata")) && cutEvolution.contains(QStringLiteral("modificata"))
                && cutEvolution.contains(QStringLiteral("eliminata")), "evoluzione completa del taglio");
        TopologyReferenceResolution report;
        clearReferenceWarnings();
        require(resolveFaceReference(*geometry, ref, 1e-3, ReferenceState::Other, &report) == left
                && report.method == TopologyResolutionMethod::Name && report.candidates == 2, "esito per nome, disambiguato dal punto");
        require(resolveFaceReference(*geometry, gap, 1e-3, ReferenceState::Other, &report).valid()
                && report.method == TopologyResolutionMethod::Ambiguous && !report.warning.isEmpty(), "esito ambiguo con avviso");
        EdgePoint byId = ref; byId.name.clear();
        require(resolveFaceReference(*geometry, byId, 1e-3, ReferenceState::Same, &report) == left
                && report.method == TopologyResolutionMethod::Identity, "risoluzione per ID distinta dalla geometria");
        EdgePoint byGeometry = ref; byGeometry.subshape = -1; byGeometry.name = QStringLiteral("nome non presente");
        require(resolveFaceReference(*geometry, byGeometry, 1e-3, ReferenceState::Other, &report) == left
                && report.method == TopologyResolutionMethod::Geometry && report.warning.contains(QStringLiteral("non ritrovato")),
                "ripiego geometrico con nome mancante");
        byGeometry.x = byGeometry.y = byGeometry.z = 1e8;
        require(!resolveFaceReference(*geometry, byGeometry, 1e-3, ReferenceState::Other, &report).valid()
                && report.method == TopologyResolutionMethod::Missing, "riferimento non risolto distinto dall'ambiguita'");
        require(takeReferenceWarnings().isEmpty(), "la diagnostica esplicita non altera gli avvisi del calcolo");
        EdgePoint corner; corner.x = corner.y = corner.z = 0;
        require(resolveFaceReference(*view.extrusions_.at(0).forgeBody, corner, 1e-3, ReferenceState::Other, &report).valid()
                && report.method == TopologyResolutionMethod::Ambiguous && report.candidates == 3,
                "anche il ripiego geometrico segnala piu' facce equivalenti");
        beginTopologyResolutionCapture();
        beginTopologyResolutionCapture();
        resolveFaceReference(*geometry, ref, 1e-3, ReferenceState::Other, &report);
        const auto nested = endTopologyResolutionCapture(view.extrusions_);
        const auto parent = endTopologyResolutionCapture(view.extrusions_);
        require(nested.size() == 1 && parent.size() == 1 && parent.first().sourceIndex == 2,
                "audit annidati senza perdere i riferimenti del calcolo interno");


        // Raccordo su uno spigolo del pezzo sinistro, salvato e riletto.
        const EdgeId edge = nearestEdge(*geometry, Vec3(2, 0, 10), 1e-6);
        require(edge.valid(), "spigolo del pezzo sinistro");
        const EdgePoint edgeRef = edgeReference(*geometry, edge, Vec3(2, 0, 10));
        require(!edgeRef.name.isEmpty(), "nome dello spigolo");
        ExtrusionObject blend; blend.name = QStringLiteral("Raccordo"); blend.feature = BodyFeature::Blend; blend.firstBody = 2;
        blend.blendSize = 0.5; blend.blendEdges = {edgeRef}; blend.blendBaseFeature = view.extrusions_.at(2).featureId;
        require(view.createBody(blend).isEmpty() && view.extrusions_.size() == 4 && view.extrusions_.at(3).error.isEmpty(), "raccordo creato");
        const auto filletNames = elementNamesOf(*view.extrusions_.at(3).forgeBody);
        require(filletNames != nullptr, "nomi del raccordo");
        require(featureEvolutionText(*view.extrusions_.at(3).forgeBody).contains(QStringLiteral("generata da spigolo")),
                "provenienza della faccia del raccordo dallo spigolo");
        require(!view.extrusions_.at(3).referenceResolutions.isEmpty(), "audit reale della risoluzione nel calcolo");
        HistoryGraphDialog diagnosticGraph;
        diagnosticGraph.setDocument(view.currentDocument());
        require(diagnosticGraph.featureDiagnostics(2).contains(QStringLiteral("divisa (2 pezzi)"))
                && diagnosticGraph.featureDiagnostics(3).contains(QStringLiteral("per nome")), "evoluzione e audit nei dettagli del grafo");
        const auto recorded = diagnosticGraph.referenceDiagnostics(3);
        require(!recorded.isEmpty() && !recorded.first().currentCheck && recorded.first().sourceIndex == 2,
                "il grafo usa il vero esito del calcolo e il suo operando");
        DocumentState snapshotDiagnostic = view.currentDocument();
        snapshotDiagnostic.extrusions[3].referenceResolutions.clear();
        addReferenceWarning(QStringLiteral("sentinella"));
        diagnosticGraph.setDocument(snapshotDiagnostic);
        const auto checked = diagnosticGraph.referenceDiagnostics(3);
        require(!checked.isEmpty() && checked.first().currentCheck && checked.first().method == TopologyResolutionMethod::Name,
                "snapshot senza audit: verifica corrente esplicitamente distinta");
        require(takeReferenceWarnings() == QStringList{QStringLiteral("sentinella")}, "aprire il grafo non inquina gli avvisi");

        QTemporaryDir directory;
        const QString file = directory.filePath(QStringLiteral("genealogia.prt"));
        require(saveDocumentFile(file, view.currentDocument(), false).isEmpty(), "salvataggio formato 49");
        DocumentState loaded;
        require(loadDocumentFile(file, loaded).isEmpty() && loaded.extrusions.size() == 4, "rilettura");
        require(loaded.extrusions.at(3).blendEdges.size() == 1 && loaded.extrusions.at(3).blendEdges.at(0).name == edgeRef.name,
                "nome del riferimento conservato nel documento");

        // La cava si sposta sotto il punto del riferimento: lo spigolo con quel
        // nome resta diviso in due pezzi e nessuno contiene il punto. Il raccordo
        // si rigenera e l'esito dice che il riferimento era ambiguo.
        ExtrusionObject moved = view.extrusions_.at(1);
        moved.primitive.origin[0] = 1;
        require(view.updateBody(1, moved).isEmpty(), "modifica della cava");
        require(view.extrusions_.at(3).error.isEmpty() && view.extrusions_.at(3).forgeBody, "raccordo rigenerato");
        require(view.extrusions_.at(3).notice.contains(QStringLiteral("ambiguo")), "esito del raccordo con l'avviso");
        diagnosticGraph.setDocument(view.currentDocument());
        require(diagnosticGraph.featureDiagnostics(3).contains(QStringLiteral("AMBIGUO"))
                && diagnosticGraph.featureDiagnostics(3).contains(QStringLiteral("Avviso:")), "grafo con ambiguita' e avviso reali");
        bool ambiguityBadge = false;
        for (auto *scene : diagnosticGraph.findChildren<QGraphicsScene *>())
            for (auto *item : scene->items()) if (auto *label = dynamic_cast<QGraphicsSimpleTextItem *>(item))
                ambiguityBadge = ambiguityBadge || label->text().contains(QStringLiteral("AMBIGUI"));
        require(ambiguityBadge, "ambiguita' evidente sul blocco del grafo");

        // Tornando indietro l'avviso sparisce.
        moved.primitive.origin[0] = 4;
        require(view.updateBody(1, moved).isEmpty() && view.extrusions_.at(3).error.isEmpty(), "cava rimessa a posto");
        require(!view.extrusions_.at(3).notice.contains(QStringLiteral("ambiguo")), "nessun avviso con il riferimento univoco");
        DocumentState unite;
        ExtrusionObject one = box, two = box;
        two.primitive.origin[0] = 10;
        ExtrusionObject unionFeature; unionFeature.operation = 0; unionFeature.firstBody = 0; unionFeature.secondBody = 1;
        unite.extrusions = {one, two, unionFeature};
        CadViewport joined; joined.loadDocument(unite);
        require(joined.extrusions_.at(2).forgeBody && featureEvolutionText(*joined.extrusions_.at(2).forgeBody).contains(QStringLiteral("unita")),
                "entita' unite con provenienza da entrambi gli operandi");
    }
    // Modifica -> Rigenera tutto (Ctrl+B): anche le feature lette dallo
    // snapshot del documento si ricalcolano; un passo di Undo.
    static void rebuildAllCommand() {
        using namespace ForgeCad;
        // Una fusione gia' associata non perde silenziosamente la base
        // quando una feature precedente fallisce durante la rigenerazione.
        ExtrusionObject missing;missing.name=QStringLiteral("Base fallita");missing.error=QStringLiteral("errore geometrico");
        ExtrusionObject merged;merged.mergeOperation=1;merged.mergeBodies={0};
        const auto tool=std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3(),1,1,1));
        QString mergeError;
        require(!forgeMergeFeatureResult(merged,tool,1,{missing},&mergeError)
                    && mergeError.contains(missing.name), "base della fusione assente segnalata, senza prisma isolato");
        merged.mergeProbe=true;mergeError.clear();
        require(forgeMergeFeatureResult(merged,tool,1,{missing},&mergeError)==tool&&mergeError.isEmpty(),
                "la ricerca automatica iniziale puo' creare un corpo senza candidati");
        CadViewport viewport;
        ExtrusionObject box;
        box.name = QStringLiteral("Parallelepipedo");
        box.feature = BodyFeature::Primitive;
        box.primitive.size[0] = 2.0;
        box.primitive.size[1] = 3.0;
        box.primitive.size[2] = 4.0;
        require(viewport.createBody(box).isEmpty(), "creazione del parallelepipedo");
        require(std::fabs(Kernel::massProperties(*viewport.extrusions_.at(0).forgeBody).volume - 24.0) < 1e-9, "volume iniziale");
        // Snapshot superato (come un documento salvato da un kernel precedente).
        viewport.extrusions_[0].forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3(), 1.0, 1.0, 1.0));
        viewport.extrusions_[0].cachedGeometry = true;
        require(viewport.rebuildAllFeatures() == 0, "rigenerazione senza errori");
        require(std::fabs(Kernel::massProperties(*viewport.extrusions_.at(0).forgeBody).volume - 24.0) < 1e-9, "corpo ricalcolato");
        viewport.undo();
        require(std::fabs(Kernel::massProperties(*viewport.extrusions_.at(0).forgeBody).volume - 1.0) < 1e-9, "annulla la rigenerazione");
    }
    static void extrusionSurfaceOnly() {
        using namespace ForgeCad;
        // Quadrato 2 x 2 con un foro circolare di raggio 0.5 e un segmento aperto a parte.
        SketchObject sketch;
        sketch.name = QStringLiteral("Profilo");
        const QPointF a(0, 0), b(2, 0), c(2, 2), d(0, 2);
        sketch.segments = {{a, b}, {b, c}, {c, d}, {d, a}, {QPointF(4, 0), QPointF(4, 1.5)}};
        sketch.constraints = QVector<int>(sketch.segments.size(), 0);
        sketch.segmentLengths = QVector<double>(sketch.segments.size(), 0.0);
        sketch.segmentAngles = QVector<double>(sketch.segments.size(), 0.0);
        CurveObject circle;
        circle.tool = DrawingTool::Circle;
        circle.controlPoints = {QPointF(1, 1), QPointF(1.5, 1)};
        sketch.curves = {circle};
        const double h = 3.0;
        const auto build = [&](ExtrusionObject body, QString &error) {
            body.feature = BodyFeature::Extrusion;
            body.operation = -1;
            body.sketchIndex = 0;
            return forgeExtrusionFeature(body, 0, {sketch}, {}, &error);
        };
        const auto totalArea = [](const Kernel::Body &body) {
            double area = 0.0;
            for (Kernel::FaceId face : body.faces()) area += Kernel::faceArea(body, face);
            return area;
        };
        ExtrusionObject body;
        body.distance = h;
        QString error;
        const ForgeBody solid = build(body, error);
        require(solid && !solid->isSheet(), "estrusione solida");
        body.extrudeSurface = true;
        const ForgeBody sheet = build(body, error);
        require(sheet && sheet->isSheet(), ("estrusione di superficie: " + error).toStdString().c_str());
        require(Kernel::checkBody(*sheet).empty(), "lamina non valida");
        const double expected = (8.0 + Kernel::kPi + 1.5) * h;
        require(std::fabs(totalArea(*sheet) - expected) < 1e-7 * expected, "area dei fianchi");
        for (Kernel::FaceId face : sheet->faces()) {
            const Kernel::Surface &surface = *sheet->face(face).surface;
            if (surface.type() == Kernel::SurfaceType::Plane)
                require(std::fabs(static_cast<const Kernel::Plane &>(surface).frame().zDir().z()) < 1e-9, "coperchio rimasto");
        }
        // Simmetrica: stessa area, meta' sotto il piano.
        body.extrudeSides = 1;
        const ForgeBody symmetric = build(body, error);
        require(symmetric && symmetric->isSheet() && std::fabs(totalArea(*symmetric) - expected) < 1e-7 * expected, "superficie simmetrica");
        // Salvataggio e rilettura del flag.
        QTemporaryDir dir;
        DocumentState state;
        state.sketches = {sketch};
        ExtrusionObject saved = body;
        saved.feature = BodyFeature::Extrusion;
        saved.operation = -1;
        saved.name = QStringLiteral("Estrusione 1");
        state.extrusions = {saved};
        const QString path = dir.filePath(QStringLiteral("superficie.prt"));
        require(saveDocumentFile(path, state, false).isEmpty(), "salvataggio");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions.size() == 1 && loaded.extrusions.first().extrudeSurface,
                "rilettura del flag di superficie");
    }
    static void midpointQuadrantConstraints() {
        using namespace ForgeCad;
        const auto near = [](QPointF a, QPointF b) { return QLineF(a,b).length() < 1e-6; };
        SketchObject midpoint;
        midpoint.segments = {{QPointF(0,0),QPointF(10,0)}, {QPointF(5,0),QPointF(5,4)}};
        require(applicableConstraints(midpoint, {{0,0,-1},{0,1,0}}).contains(ConstraintType::Midpoint), "punto medio disponibile");
        midpoint.geometricConstraints = {makeConstraint(midpoint,ConstraintType::Midpoint,{{0,0,-1},{0,1,0}})};
        midpoint.segments[0].second = QPointF(20,6);
        midpoint.geometricConstraints.append(makeConstraint(midpoint,ConstraintType::Fix,{{0,0,-1}}));
        require(solveSketch(midpoint).ok && near(midpoint.segments[1].first,QPointF(10,3)), "punto medio segue il segmento");
        const QPointF directions[] = {{1,0},{0,1},{-1,0},{0,-1}};
        for (int k = 0; k < 4; ++k) {
            CadViewport v;
            v.sketches_ = {SketchObject()}; v.activeSketch_ = 0; v.originSnap_ = false;
            auto &sketch = v.sketches_[0];
            CurveObject circle; circle.tool = DrawingTool::Circle;
            circle.controlPoints = {{10,10},{15,10}}; recalculateCurve(circle);
            sketch.curves = {circle};
            const QPointF p = QPointF(10,10) + 5*directions[k];
            sketch.segments = {{p,p+QPointF(2,3)}};
            require(applicableConstraints(sketch,{{1,0,-1},{0,0,0}}).contains(ConstraintType::Quadrant), "quadrante manuale disponibile");
            const auto manual = makeConstraint(sketch,ConstraintType::Quadrant,{{1,0,-1},{0,0,0}});
            require(manual.value == k, "quadrante manuale piu vicino");
            v.recordPointCoincidences(sketch,{0,0,0},p);
            require(sketch.geometricConstraints.size() == 1 && sketch.geometricConstraints[0].type == ConstraintType::Quadrant
                && sketch.geometricConstraints[0].value == k, "snap crea quadrante persistente anche sul punto radiale");
            sketch.curves[0].controlPoints = {{12,13},{12,21}};
            sketch.geometricConstraints.append(makeConstraint(sketch,ConstraintType::Fix,{{1,0,-1}}));
            require(solveSketch(sketch).ok && near(sketch.segments[0].first,QPointF(12,13)+8*directions[k]), "quadrante segue centro e raggio senza ruotare");
            QTemporaryDir files;
            const QString path = files.path()+QStringLiteral("/quadrante.prt");
            require(saveDocumentFile(path,v.currentDocument(),false).isEmpty(), "salva quadrante");
            DocumentState loaded;
            require(loadDocumentFile(path,loaded).isEmpty() && loaded.sketches[0].geometricConstraints[0].type == ConstraintType::Quadrant
                && loaded.sketches[0].geometricConstraints[0].value == k, "rilettura quadrante e direzione");
        }
        CurveObject arc; arc.tool = DrawingTool::Arc;
        const auto polar = [](double degrees) { const double a = degrees*M_PI/180; return QPointF(5*std::cos(a),5*std::sin(a)); };
        arc.controlPoints = {QPointF(),polar(350),polar(100)};
        auto quadrants = curveQuadrants(arc);
        require(quadrants.size() == 2 && quadrants[0].first == 0 && quadrants[1].first == 1, "quadranti arco attraverso zero");
        SketchObject sketch; sketch.curves = {arc}; sketch.segments = {{QPointF(-5,0),QPointF(-6,1)}};
        auto c = makeConstraint(sketch,ConstraintType::Quadrant,{{0,0,0},{1,0,-1}});
        require(c.value == 1, "quadrante manuale solo sul tratto presente");
        sketch.geometricConstraints = {c,makeConstraint(sketch,ConstraintType::Fix,{{1,0,-1}})};
        require(solveSketch(sketch).ok && near(sketch.segments[0].first,QPointF(0,5)), "risoluzione quadrante arco");
        arc.controlPoints = {QPointF(),polar(10),polar(80)}; sketch.curves = {arc};
        require(curveQuadrants(arc).isEmpty() && !applicableConstraints(sketch,{{0,0,0},{1,0,-1}}).contains(ConstraintType::Quadrant), "nessun quadrante fuori arco");
    }
    static void automaticSnapConstraints() {
        using namespace ForgeCad;
        const auto has = [](const SketchObject &sketch, ConstraintType type, ConstraintRef a, ConstraintRef b) {
            for (const auto &c : sketch.geometricConstraints)
                if (c.type==type && c.first==a && c.second==b) return true;
            return false;
        };
        for (auto tool : {DrawingTool::Circle,DrawingTool::Ellipse,DrawingTool::Arc}) {
            CadViewport v;
            v.sketches_={SketchObject()}; v.activeSketch_=0;
            v.geometrySnap_=true; v.originSnap_=true;
            v.drawingTool_=tool;
            v.sketches_[0].segments.append({QPointF(3,0),QPointF(5,0)});
            v.curveControlPoints_={QPointF(0,0),QPointF(3,0)};
            if(tool==DrawingTool::Ellipse) v.curveControlPoints_.append(QPointF(0,2));
            if(tool==DrawingTool::Arc) v.curveControlPoints_.append(QPointF(0,3));
            v.finalizePrimitive();
            auto &sketch=v.sketches_[0];
            require(sketch.curves.size()==1,"creazione primitiva con snap");
            require(has(sketch,ConstraintType::Coincident,{1,0,0},{2,0,-1}),"centro non vincolato all'origine");
            require(has(sketch,ConstraintType::Coincident,{1,0,1},{0,0,0}),"snap primitiva non persistente");
            sketch.curves[0].controlPoints[0]+=QPointF(0.2,0.1);
            require(solveSketch(sketch).ok,"risoluzione vincolo origine");
            require(QLineF(sketch.curves[0].controlPoints[0],QPointF()).length()<1e-6,"origine non mantenuta");
        }
        CadViewport v;
        v.sketches_={SketchObject()}; v.activeSketch_=0;
        v.geometrySnap_=true; v.originSnap_=true; v.gridSnap_=false;
        v.drawingTool_=DrawingTool::Spline;
        v.sketches_[0].segments.append({QPointF(2,2),QPointF(4,2)});
        v.curveControlPoints_={QPointF(),QPointF(3,2),QPointF(4,-2)};
        const QPointF closing=v.snapPoint(QPointF(0.001,0.001),false);
        require(closing==QPointF(),"snap chiusura spline");
        v.curveControlPoints_.append(closing);
        v.finalizeCurve();
        auto &sketch=v.sketches_[0];
        require(has(sketch,ConstraintType::Midpoint,{1,0,1},{0,0,-1}),"snap punto interno spline non vincolato");
        require(has(sketch,ConstraintType::Coincident,{1,0,0},{1,0,3}),"chiusura spline non vincolata");
        const auto &curve=sketch.curves[0];
        int handles=0;
        for(int control : {0,3}) for(int side : {0,1}) handles+=CadViewport::visibleSplineHandle(curve,control,side);
        require(handles==2 && CadViewport::visibleSplineHandle(curve,0,1)
            && CadViewport::visibleSplineHandle(curve,3,0),"maniglie ridondanti alla chiusura");
        const QPointF hidden=curve.tangentHandles[0].first;
        int ci=-1,pi=-1; EditablePointKind kind=EditablePointKind::Control;
        const bool found=v.findCurveEditPoint(hidden,ci,pi,kind);
        require(!found || pi!=0 || kind!=EditablePointKind::TangentIn,"maniglia nascosta selezionabile");
        sketch.curves[0].controlPoints[3]=QPointF(0.1,0.1);
        require(solveSketch(sketch).ok && QLineF(sketch.curves[0].controlPoints[0],sketch.curves[0].controlPoints[3]).length()<1e-6,
                "chiusura persa dopo modifica");

        // La chiusura deve valere anche lontano dall'origine, senza due
        // coincidenze all'origine che nascondano un vincolo non riconosciuto.
        SketchObject closed;
        closed.curves.append(sketch.curves[0]);
        for(auto &p:closed.curves[0].controlPoints) p+=QPointF(10,10);
        for(auto &h:closed.curves[0].tangentHandles) { h.first+=QPointF(10,10); h.second+=QPointF(10,10); }
        SketchConstraint closure;
        closure.type=ConstraintType::Coincident; closure.first={1,0,0}; closure.second={1,0,3};
        closed.geometricConstraints.append(closure);
        closed.curves[0].controlPoints[3]+=QPointF(0.3,-0.2);
        require(applicableConstraints(closed,{closure.first,closure.second}).contains(ConstraintType::Coincident),
            "chiusura spline rifiutata dal sistema di vincoli");
        require(solveSketch(closed).ok && QLineF(closed.curves[0].controlPoints[0],closed.curves[0].controlPoints[3]).length()<1e-6,
            "vincolo di chiusura inefficace fuori dall'origine");

        SketchObject onCurve;
        CurveObject ellipse;
        ellipse.tool=DrawingTool::Ellipse; ellipse.controlPoints={QPointF(10,10),QPointF(15,10),QPointF(10,12)};
        onCurve.curves.append(ellipse);
        const QPointF p(10+5/std::sqrt(2.0),10+2/std::sqrt(2.0));
        onCurve.segments.append({p,QPointF(20,20)});
        v.recordCoincidences(onCurve,0);
        require(has(onCurve,ConstraintType::PointOnCurve,{0,0,0},{1,0,-1}),"snap su ellisse non vincolato");
        const int count=onCurve.geometricConstraints.size();
        v.recordCoincidences(onCurve,0);
        require(onCurve.geometricConstraints.size()==count,"vincoli snap duplicati");
        onCurve.segments[0].first+=QPointF(0.05,0.03);
        require(solveSketch(onCurve).ok,"risoluzione punto su ellisse");
        for(const auto &c:onCurve.geometricConstraints) require(constraintError(onCurve,c)<1e-6,"snap non mantenuto dopo modifica");
        QTemporaryDir directory;
        DocumentState state,loaded; state.sketches={sketch,onCurve};
        const auto path=directory.filePath(QStringLiteral("snaps.prt"));
        require(saveDocumentFile(path,state,false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),"persistenza snap .prt");
        require(has(loaded.sketches[0],ConstraintType::Coincident,{1,0,0},{1,0,3}),"vincolo chiusura perso alla riapertura");
    }

    static void ellipseTrim() {
        using namespace ForgeCad;
        for (bool rotated : {false, true}) {
            SketchObject sketch;
            CurveObject ellipse;
            ellipse.tool = DrawingTool::Ellipse;
            ellipse.construction = true;
            const QPointF center(7,-3), axis = rotated ? QPointF(0.6,0.8) : QPointF(1,0);
            const QPointF perpendicular(-axis.y(),axis.x());
            ellipse.controlPoints = {center, center + 2*axis, center + 5*perpendicular};
            sketch.curves.append(ellipse);
            const auto original = curveGeometry(ellipse).front();
            const auto point = [&](double t) {
                const auto p = original.curve->point(t);
                return QPointF(p.x(),p.y());
            };
            const auto diameter = [&](double t) {
                const QPointF d = 2*(point(t)-center);
                return SketchSegment{center-d,center+d};
            };
            sketch.segments.append(diameter(0));
            const auto preview = trimPreview(sketch,{1,0},point(M_PI/2));
            require(preview.size()>2,"anteprima taglio ellisse assente");
            auto result = trimSketchEntity(sketch,{1,0},point(M_PI/2));
            require(result.error.isEmpty() && sketch.curves.size()==1,"taglio ellisse fallito");
            require(sketch.curves[0].tool==DrawingTool::Nurbs && sketch.curves[0].construction,
                    "arco ellittico razionale e costruzione");
            const auto verify = [&] {
                const auto pieces = curveGeometry(sketch.curves[0]);
                require(pieces.size()==1,"arco ellittico non valido");
                const auto &g=pieces.front();
                for(int i=0;i<=100;++i) {
                    const auto p=g.curve->point(g.range.lo+g.range.length()*i/100.0);
                    const QPointF d=QPointF(p.x(),p.y())-center;
                    const double x=QPointF::dotProduct(d,axis)/2, y=QPointF::dotProduct(d,perpendicular)/5;
                    require(std::fabs(x*x+y*y-1)<1e-10,"arco fuori dall'ellisse originale");
                }
            };
            verify();
            QTemporaryDir directory;
            DocumentState state, loaded;
            state.sketches.append(sketch);
            const QString path=directory.filePath(QStringLiteral("ellipse.prt"));
            require(saveDocumentFile(path,state,false).isEmpty() && loadDocumentFile(path,loaded).isEmpty(),
                "salvataggio e riapertura arco ellittico");
            require(loaded.sketches.size()==1 && loaded.sketches[0].curves.size()==1,
                "arco ellittico perso alla riapertura");
            sketch=loaded.sketches[0];
            verify();
            sketch.segments.append(diameter(M_PI/2));
            result=trimSketchEntity(sketch,{1,0},point(5*M_PI/4));
            require(result.error.isEmpty() && sketch.curves.size()==1,"secondo taglio arco ellittico fallito");
            verify();
            const auto kept=curveGeometry(sketch.curves[0]).front();
            const auto expected=original.curve->point(3*M_PI/2);
            require(Kernel::distance(kept.start(),expected)<1e-7,"secondo taglio estremo errato");

            SketchObject wrap;
            wrap.curves.append(ellipse);
            wrap.segments.append(diameter(M_PI/2));
            result=trimSketchEntity(wrap,{1,0},point(0));
            require(result.error.isEmpty() && wrap.curves.size()==1,"taglio sulla chiusura ellisse");
            const auto retained=curveGeometry(wrap.curves[0]).front();
            require(Kernel::distance(retained.start(),original.curve->point(M_PI/2))<1e-7
                && Kernel::distance(retained.end(),original.curve->point(3*M_PI/2))<1e-7,
                "taglio sulla chiusura tiene il tratto sbagliato");
            SketchObject isolated;
            isolated.curves.append(ellipse);
            require(trimSketchEntity(isolated,{1,0},point(0)).error.isEmpty() && isolated.curves.isEmpty(),
                "ellisse senza intersezioni non eliminata");
        }
    }

    static void extensionContourPicking() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const ForgeBody plate = std::make_shared<const Body>(makePlaneSheet(Frame3(), 10.0));
        const auto edge = plate->edges().front();
        const auto &e = plate->edge(edge);
        const QVector<EdgePoint> selected{edgeReference(*plate, edge, e.curve->point(0.5 * (e.range.lo + e.range.hi)))};
        require(completeExtensionContours(plate, selected).size() == 4, "completamento del contorno libero");
        require(completeExtensionContours(plate, selected + selected).size() == 4, "contorno senza bordi duplicati");
        CadViewport viewport;
        ExtrusionObject feature;
        feature.forgeBody = plate;
        forgeTessellate(*plate, 0, feature.display);
        viewport.extrusions_ = {feature};
        QWidget parent;
        int appliedEdges = 0;
        QTimer::singleShot(0, [&] {
            auto *dialog = parent.findChild<QDialog *>();
            if (!dialog) return;
            dialog->findChild<QCheckBox *>()->setChecked(true);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        const auto result = extendDialog(&parent, &viewport, QStringLiteral("Test contorni estensione"), 0, -1,
            selected, 1.0, false, true, [&](const QVector<EdgePoint> &edges, double, bool) {
                appliedEdges = edges.size();
                return QString();
            });
        require(result.applied && appliedEdges == 4 && result.edges.size() == 4,
                "conferma e riselezione usano il contorno completo");
        std::cout << "Estensione: scelta del contorno completo OK" << std::endl;
    }
    static void trimPartPicking() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        CadViewport v;
        v.resize(800, 600);
        v.setViewNormal(0);
        v.setReferencePlanesVisible(false);
        const ForgeBody plate = std::make_shared<const Body>(makePlaneSheet(Frame3(), 10));
        const ForgeBody wall = std::make_shared<const Body>(makePlaneSheet(
            Frame3(Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0)), 5));
        ExtrusionObject a, b;
        a.name = QStringLiteral("Lastra"); a.forgeBody = plate;
        b.name = QStringLiteral("Parete"); b.forgeBody = wall;
        forgeTessellate(*plate, 0, a.display);
        forgeTessellate(*wall, 1, b.display);
        v.extrusions_ = {a, b};
        QString error;
        const auto regions = forgeSheetPieces(wall, plate, 0, &error);
        require(regions.size() == 2, "regioni della parete");
        int side = -1, part = -1;
        // Selezione sul secondo corpo, anche se il primo non ha regioni.
        v.setViewNormal(2);
        v.setTrimPartPickCallback(0, {}, 1, regions,
            [&](int s, int p, EdgePoint) { side = s; part = p; });
        const auto point = regions.at(0).point;
        const QPoint cursor = v.projectWorldPoint(QVector3D(point.x, point.y, point.z)).toPoint();
        v.updateHover(cursor);
        require(v.trimPartHover_ == 0, "hover delle regioni disponibili del secondo corpo");
        v.trimPartHover_ = -1;
        v.extrusions_[1].visible = false;
        const auto click = [&](Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(cursor), QPointF(cursor), Qt::LeftButton, Qt::LeftButton, modifiers);
            v.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(cursor), QPointF(cursor), Qt::LeftButton, Qt::NoButton, modifiers);
            v.mouseReleaseEvent(&release);
        };
        click();
        require(side == 1 && part == 0, "clic senza hover sul corpo nascosto dall'anteprima");
        v.selection_ = {SceneObjectKind::Extrusion, 0};
        v.setTrimPartPickCallback(0, {}, 1, {}, [](int, int, EdgePoint) {});
        click(); click(Qt::ControlModifier);
        require(v.selection_.kind == SceneObjectKind::Extrusion && v.selection_.index == 0 && !v.boxSelecting_,
                "taglio senza regioni non seleziona la scena");
        v.setTrimPartPickCallback(-1, {}, -1, {}, {});
        v.extrusions_[1].visible = true;
        for (bool reverse : {false, true}) {
            QWidget parent;
            ExtrusionObject definition;
            definition.firstBody = reverse ? 1 : 0;
            definition.secondBody = reverse ? 0 : 1;
            definition.trimBoth = true;
            std::exception_ptr failure;
            bool inspected = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = parent.findChild<QDialog *>();
                if (!dialog) return;
                try {
                    auto *partBox = dialog->findChild<QComboBox *>(QStringLiteral("trimPart"));
                    auto *toolPartBox = dialog->findChild<QComboBox *>(QStringLiteral("trimToolPart"));
                    require(partBox && toolPartBox && partBox->count() == (reverse ? 2 : 0)
                                && toolPartBox->count() == (reverse ? 0 : 2),
                            "pannello conserva le regioni quando un solo corpo si divide");
                    require(bool(v.trimPartPickFinished_) && v.trimPartPickDisplays_.size() == 2,
                            "pannello mantiene attiva la scelta delle regioni disponibili");
                    QPushButton *pick = nullptr;
                    for (auto *button : dialog->findChildren<QPushButton *>())
                        if (button->text() == QStringLiteral("Dalla vista")) { pick = button; break; }
                    require(pick != nullptr, "pulsante scelta corpo");
                    pick->click();
                    require(v.refPicking_ && !v.trimPartPickFinished_, "Dalla vista sospende la scelta delle parti");
                    pick->click();
                    require(!v.refPicking_ && bool(v.trimPartPickFinished_), "ritorno alla scelta delle parti");
                    if (reverse) {
                        dialog->findChild<QCheckBox *>(QStringLiteral("trimBoth"))->setChecked(false);
                        require(partBox->count() == 2 && toolPartBox->count() == 0 && v.preview_.valid,
                                "taglio singolo disponibile dopo errore del secondo corpo");
                    }
                    inspected = true;
                } catch (...) { failure = std::current_exception(); }
                dialog->reject();
            });
            trimDialog(&parent, &v, QStringLiteral("Test taglio"), -1, definition,
                       [](const ExtrusionObject &) { return QString(); });
            if (failure) std::rethrow_exception(failure);
            require(inspected && !v.trimPartPickFinished_, "chiusura pannello ripristina la selezione normale");
        }
        std::cout << "Taglio: hover, clic e regioni parziali OK" << std::endl;
    }
    static void offsetFacePicking() {
        using namespace ForgeCad;
        CadViewport v;
        v.resize(800, 600);
        v.setViewNormal(0);
        v.setReferencePlanesVisible(true);
        PrimitiveParameters box;
        require(v.createPrimitive(box, QStringLiteral("Box selezione facce")).isEmpty(), "box selezione facce");
        ExtrusionObject datum;
        datum.feature = BodyFeature::DatumPlane;
        datum.name = QStringLiteral("Piano davanti al box");
        GeometryRef xy;
        xy.kind = 1; xy.index = 0;
        datum.datum.refs = {xy}; datum.datum.distance = 100; datum.datum.size = 100;
        require(v.createBody(datum).isEmpty(), "datum davanti al corpo");
        require(v.beginReferencePick(DatumRoleFace, -1).isEmpty(), "inizio scelta facce");
        GeometryRef picked;
        const QPoint cursor = v.projectWorldPoint(QVector3D(0.5f, 0.5f, 0)).toPoint();
        require(v.pickReference(cursor, picked) && picked.kind == 5 && picked.index == 0,
                "scelta facce ignora il datum davanti al corpo");
        v.updateHover(cursor);
        require(v.refHoverValid_ && v.refHover_.kind == 5 && v.hover_.kind == SceneObjectKind::None,
                "hover delle facce senza evidenziare i piani");
        v.extrusions_[0].visible = false;
        require(!v.pickReference(cursor, picked), "nessun piano selezionabile nella scelta facce");
        v.refPickRoles_ = DatumRolePlane;
        require(v.pickReference(cursor, picked) && (picked.kind == 8 || picked.kind == 1),
                "i piani restano selezionabili quando richiesti");
        ExtrusionObject definition;
        definition.feature = BodyFeature::SurfaceOffset;
        ExtrusionObject separate = definition;
        separate.offsetSew = false;
        require(CadViewport::previewKey(definition, -1) != CadViewport::previewKey(separate, -1),
                "la checkbox invalida la cache dell'anteprima");
        std::cout << "Selezione facce offset e cache cucitura: OK" << std::endl;
    }
    static void deletionDuringEdgePick() {
        for (int removed : {0, 2}) {
            CadViewport viewport;
            viewport.extrusions_.resize(3);
            viewport.edgePicking_ = true;
            viewport.edgePickBody_ = 2;
            viewport.edgePickEdit_ = 2;
            viewport.pickedEdges_ = {0};
            viewport.hoverEdgeBody_ = 2;
            viewport.hoverEdge_ = 0;
            viewport.preview_.valid = true;
            const auto generation = viewport.preview_.generation;
            viewport.removeBodies(QSet<int>{removed});
            require(viewport.extrusions_.size() == 2, "rimozione corpo durante scelta spigoli");
            require(!viewport.edgePicking_ && viewport.edgePickBody_ == -1 && viewport.edgePickEdit_ == -1,
                    "scelta annullata prima del ricalcolo, per corpo rimosso o rinumerato");
            require(viewport.pickedEdges_.isEmpty() && viewport.hoverEdgeBody_ == -1 && viewport.hoverEdge_ == -1,
                    "nessun indice di spigolo residuo dopo la rimozione");
            require(!viewport.preview_.valid && viewport.preview_.generation > generation,
                    "anteprima invalidata anche per risultati asincroni in arrivo");
        }
        std::cout << "Rimozione durante scelta spigoli: OK" << std::endl;
    }
    // Diagnostica facoltativa: --display-stats file.prt. Dimensioni della
    // visualizzazione salvata (o ricalcolata) di ogni corpo e del suo B-rep.
    static void displayStats(const QString &path) {
        using namespace ForgeCad;
        DocumentState document;
        QElapsedTimer timer;
        timer.start();
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento");
        std::cout << "lettura " << timer.elapsed() << " ms" << std::endl;
        timer.restart();
        CadViewport viewport;
        viewport.loadDocument(document);
        std::cout << "loadDocument " << timer.elapsed() << " ms" << std::endl;
        for (int i = 0; i < viewport.extrusions_.size(); ++i) {
            const ExtrusionObject &body = viewport.extrusions_.at(i);
            qsizetype edgePoints = 0;
            for (const auto &edge : body.display.edges) edgePoints += edge.size();
            std::cout << "B" << i << " " << body.name.toStdString() << " visibile=" << body.visible
                      << " cache=" << body.cachedGeometry << " qualita'=" << body.display.quality
                      << " triangoli=" << body.display.vertices.size() / 3 << " polilinee=" << body.display.edges.size()
                      << " punti spigoli=" << edgePoints << " triangleFaces=" << body.display.triangleFaces.size();
            if (body.forgeBody) {
                std::size_t poles = 0;
                for (auto f : body.forgeBody->faces())
                    if (auto spline = std::dynamic_pointer_cast<const Kernel::BSplineSurface>(body.forgeBody->face(f).surface))
                        poles += std::size_t(spline->uPoleCount()) * std::size_t(spline->vPoleCount());
                std::cout << " facce=" << body.forgeBody->faces().size() << " poli B-spline=" << poles;
            }
            std::cout << std::endl;
        }
    }
    // Diagnostica facoltativa: --dump-document file.prt. Schizzi (piano,
    // entita' in coordinate del modello) e corpi (feature, facce, bordi liberi).
    static void dumpDocument(const QString &path, int detail = -1) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento");
        const auto v3 = [](const Kernel::Vec3 &p) {
            return QStringLiteral("(%1, %2, %3)").arg(p.x(), 0, 'g', 10).arg(p.y(), 0, 'g', 10).arg(p.z(), 0, 'g', 10).toStdString();
        };
        for (int i = 0; i < document.sketches.size(); ++i) {
            const SketchObject &sketch = document.sketches.at(i);
            const Kernel::Frame3 frame = sketchAxes(sketch);
            std::cout << "S" << i << " " << sketch.name.toStdString() << " piano=" << sketch.plane << " datum=" << sketch.datumPlane
                      << " origine=" << v3(frame.origin()) << " x=" << v3(frame.xDir()) << " n=" << v3(frame.zDir())
                      << " segmenti=" << sketch.segments.size() << " curve=" << sketch.curves.size()
                      << " vincoli=" << sketch.geometricConstraints.size() << std::endl;
            for (int k = 0; k < sketch.segments.size(); ++k)
                std::cout << "  seg" << k << (sketch.constructionSegments.contains(k) ? " [costr]" : "") << " "
                          << v3(sketchToWorld(sketch.segments[k].first, sketch)) << " -> " << v3(sketchToWorld(sketch.segments[k].second, sketch)) << std::endl;
            for (int k = 0; k < sketch.curves.size(); ++k) {
                const CurveObject &curve = sketch.curves.at(k);
                std::cout << "  curva" << k << " tool=" << int(curve.tool) << (curve.construction ? " [costr]" : "") << " grado=" << curve.degree
                          << " nodi=" << curve.knots.size() << " punti:";
                for (const QPointF &q : curve.controlPoints) std::cout << " " << v3(sketchToWorld(q, sketch));
                std::cout << std::endl;
            }
        }
        for (int i = 0; i < document.extrusions.size(); ++i) {
            ExtrusionObject &body = document.extrusions[i];
            if (!body.forgeBody && !body.suppressed) ForgeCad::buildGeometry(body, i, document.sketches, document.extrusions);
            if (i == detail) {
                std::cout << "  raccordo: misura=" << body.blendSize << " smusso=" << body.blendChamfer << " base=" << body.blendBaseFeature << " spigoli:";
                for (const EdgePoint &e : body.blendEdges) std::cout << " (" << e.x << ", " << e.y << ", " << e.z << " id=" << e.subshape << " ruolo=" << e.role << ")";
                std::cout << std::endl;
            }
            std::cout << "B" << i << " " << body.name.toStdString() << " feature=" << int(body.feature) << " op=" << body.operation
                      << " schizzo=" << body.sketchIndex << " first=" << body.firstBody << " distanza=" << body.distance
                      << " superficie=" << body.extrudeSurface << " visibile=" << body.visible << " errore=" << body.error.toStdString() << std::endl;
            if (!body.forgeBody) continue;
            const Kernel::Body &b = *body.forgeBody;
            if (i == detail && qEnvironmentVariableIsSet("DUMP_BODY_OUT")) {
                std::ofstream file(qEnvironmentVariable("DUMP_BODY_OUT").toStdString(), std::ios::binary);
                file << Kernel::writeBodyBinary(b);
            }
            std::cout << "  sheet=" << b.isSheet() << " facce=" << b.faces().size() << " edge=" << b.edges().size() << std::endl;
            for (Kernel::FaceId f : b.faces()) {
                std::cout << "  F" << f.index << " tipo=" << int(b.face(f).surface->type());
                if (i == detail)
                    for (Kernel::LoopId l : b.face(f).loops) {
                        std::cout << " [";
                        for (Kernel::FinId fin : b.loopFins(l)) std::cout << " " << (b.fin(fin).sense ? "+" : "-") << "E" << b.fin(fin).edge.index;
                        std::cout << " ]";
                    }
                std::cout << std::endl;
            }
            for (Kernel::EdgeId e : b.edges()) {
                const Kernel::Edge &g = b.edge(e);
                std::cout << "  E" << e.index << (b.isLaminar(e) ? " libero" : "") << " tipo=" << int(g.curve->type()) << " "
                          << v3(g.curve->point(g.range.lo)) << " -> " << v3(g.curve->point(g.range.hi)) << std::endl;
            }
        }
    }
    static void meshExportAutomatic() {
        using namespace ForgeCad;
        const QVector<ExportBody> bodies{{QStringLiteral("Blocco"),
            std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3(), 10, 6, 2)), {}, QColor()}};
        StlExportOptions manual;
        manual.maxEdgeLength = 2.0;
        const auto strict = buildBinaryStl(bodies, manual);
        require(strict.error.isEmpty() && strict.tessellationAttempts == 1
                    && strict.usedOptions.maxEdgeLength == manual.maxEdgeLength, "esportazione manuale invariata");
        StlExportOptions automatic = manual;
        automatic.maxEdgeLength = 1e-8; // impossibile da rispettare con il limite del tessellatore
        automatic.automaticRefinement = true;
        const auto stl = buildBinaryStl(bodies, automatic);
        const auto obj = buildQuadObj(bodies, automatic);
        require(stl.error.isEmpty() && obj.error.isEmpty() && stl.triangleCount > 0 && obj.quadCount > 0,
                "raffinamento automatico recupera una densita' irrealizzabile per entrambi i formati");
        require(stl.usedOptions.maxEdgeLength > automatic.maxEdgeLength
                    && stl.usedOptions.maxEdgeLength == obj.usedOptions.maxEdgeLength
                    && stl.usedOptions.deflection == obj.usedOptions.deflection
                    && stl.usedOptions.angle == obj.usedOptions.angle, "STL e OBJ scelgono lo stesso raffinamento valido");
        StlExportOptions selected = stl.usedOptions;
        selected.automaticRefinement = false;
        require(buildBinaryStl(bodies, selected).data == stl.data && buildQuadObj(bodies, selected).data == obj.data,
                "i parametri dichiarati riproducono esattamente i file esportati");
        require(stl.data.size() == 84 + 50 * qint64(stl.triangleCount), "STL completo senza facce omesse");
        for (int i = 0; i + 2 < stl.preview.vertices.size(); i += 3) {
            const auto &a = stl.preview.vertices[i], &b = stl.preview.vertices[i + 1], &c = stl.preview.vertices[i + 2];
            require(std::max({double((a-b).length()), double((b-c).length()), double((c-a).length())})
                        <= stl.usedOptions.maxEdgeLength * (1.0 + 1e-5), "mesh entro il lato massimo effettivo");
        }
        QTemporaryDir directory;
        require(saveBinaryStl(directory.filePath(QStringLiteral("auto.stl")), stl.data).isEmpty()
                    && saveQuadObj(directory.filePath(QStringLiteral("auto.obj")), obj.data).isEmpty(), "scrittura delle mesh automatiche");
        StlExportOptions invalid = automatic;
        invalid.deflection = std::numeric_limits<double>::infinity();
        require(!buildBinaryStl(bodies, invalid).error.isEmpty() && !buildQuadObj(bodies, invalid).error.isEmpty(),
                "parametri non finiti rifiutati anche in automatico");
        require(!buildBinaryStl({}, automatic).error.isEmpty() && !buildQuadObj({}, automatic).error.isEmpty(),
                "nessuna esportazione vuota in automatico");
        Kernel::Body damaged = *bodies.first().body;
        damaged.face(damaged.faces().front()).loops.clear(); // una faccia non triangolabile
        QVector<ExportBody> incomplete = bodies;
        incomplete.append({QStringLiteral("Corpo incompleto"), std::make_shared<const Kernel::Body>(damaged), {}, QColor()});
        automatic.maxEdgeLength = 2;
        const auto failedStl = buildBinaryStl(incomplete, automatic);
        const auto failedObj = buildQuadObj(incomplete, automatic);
        std::cout << "ricerca senza soluzione STL=" << failedStl.tessellationAttempts << " OBJ=" << failedObj.tessellationAttempts
                  << " errori: " << failedStl.error.toStdString() << " / " << failedObj.error.toStdString() << std::endl;
        require(!failedStl.error.isEmpty() && !failedObj.error.isEmpty() && failedStl.data.isEmpty() && failedObj.data.isEmpty()
                    && failedStl.tessellationAttempts == 9 && failedObj.tessellationAttempts == 9,
                "ricerca limitata: una faccia mancante impedisce anche l'esportazione parziale degli altri corpi");
        const QVector<ExportBody> sphere{{QStringLiteral("Sfera"),
            std::make_shared<const Kernel::Body>(Kernel::makeSphere(Kernel::Frame3(), 5)), {}, QColor()}};
        automatic.maxEdgeLength = 2;
        const auto curvedStl = buildBinaryStl(sphere, automatic);
        const auto curvedObj = buildQuadObj(sphere, automatic);
        require(curvedStl.error.isEmpty() && curvedObj.error.isEmpty() && curvedObj.quadCount > 0,
                "raffinamento automatico delle superfici curve e periodiche");
        std::cout << "PASS export mesh: automatico STL/OBJ, parametri riproducibili, lato massimo, scrittura, manuale e input invalidi" << std::endl;
    }
    // Diagnostica facoltativa: --mesh-stats file.prt [lato scarto angolo].
    static void meshStats(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(!args.isEmpty() && loadDocumentFile(args.first(), document).isEmpty(), "lettura documento mesh");
        QVector<ExportBody> bodies;
        for (int i = 0; i < document.extrusions.size(); ++i) {
            ExtrusionObject &feature = document.extrusions[i];
            if (!feature.forgeBody && !feature.suppressed)
                ForgeCad::buildGeometry(feature, i, document.sketches, document.extrusions);
            if (feature.visible && feature.error.isEmpty() && feature.forgeBody) {
                std::cout << "corpo " << feature.name.toStdString() << " sheet=" << feature.forgeBody->isSheet() << std::endl;
                bodies.append({feature.name, feature.forgeBody, {}, QColor()});
            }
        }
        StlExportOptions options;
        options.maxEdgeLength = args.size() > 1 ? args.at(1).toDouble() : 1.0;
        options.deflection = args.size() > 2 ? args.at(2).toDouble() : 0.05;
        options.angle = args.size() > 3 ? args.at(3).toDouble() : 10.0;
        options.automaticRefinement = qEnvironmentVariableIsSet("MESH_AUTO_REFINE");
        const StlBuildResult stl = buildBinaryStl(bodies, options);
        const ObjBuildResult obj = buildQuadObj(bodies, options);
        std::cout << "STL triangoli=" << stl.triangleCount << " errore=" << stl.error.toStdString() << '\n'
                  << "OBJ quad=" << obj.quadCount << " triangoli=" << obj.triangleCount
                  << " errore=" << obj.error.toStdString() << std::endl;
        std::cout << "raffinamento STL: " << stl.usedOptions.maxEdgeLength << " " << stl.usedOptions.deflection << " " << stl.usedOptions.angle
                  << " tentativi=" << stl.tessellationAttempts << "\nraffinamento OBJ: " << obj.usedOptions.maxEdgeLength << " "
                  << obj.usedOptions.deflection << " " << obj.usedOptions.angle << " tentativi=" << obj.tessellationAttempts << std::endl;
        require(stl.error.isEmpty() && obj.error.isEmpty(), "costruzione mesh del documento");
        if (args.size() > 4) require(saveQuadObj(args.at(4), obj.data).isEmpty(), "salvataggio OBJ diagnostico");
    }
    // --offset-lofts [distanza] file.prt ...
    static void offsetLofts(const QStringList &arguments) {
        using namespace ForgeCad;
        bool explicitDistance = false;
        const double parsed = arguments.value(0).toDouble(&explicitDistance);
        const double offsetDistance = explicitDistance ? parsed : 0.1;
        const QStringList paths = explicitDistance ? arguments.mid(1) : arguments;
        require(!paths.isEmpty(), "specificare un documento per la diagnostica loft");
        for (const QString &path : paths) {
            DocumentState document;
            require(loadDocumentFile(path, document).isEmpty(), "lettura documento offset loft");
            for (int i = 0; i < document.extrusions.size(); ++i) {
                auto &feature = document.extrusions[i];
                if (!feature.forgeBody && !feature.suppressed)
                    ForgeCad::buildGeometry(feature, i, document.sketches, document.extrusions);
                if (feature.feature != BodyFeature::Loft || !feature.forgeBody) continue;
                for (bool sew : {false, true}) {
                    QString error;
                    std::cout << path.toStdString() << " loft " << i << " tutte le facce, cucitura=" << sew << std::endl;
                    const auto result = forgeOffsetFaces(feature.forgeBody, {}, offsetDistance, &error, nullptr, sew);
                    std::cout << "  " << (result ? "OK" : error.toStdString()) << std::endl;
                    require(bool(result), "offset di tutte le facce del loft del documento");
                }
                for (const auto face : feature.forgeBody->faces()) {
                    if (feature.forgeBody->face(face).surface->type() != Kernel::SurfaceType::BSpline) continue;
                    const auto surface = feature.forgeBody->face(face).surface;
                    const auto u = surface->uDomain(), v = surface->vDomain();
                    const auto ref = faceReference(*feature.forgeBody, face, surface->point((u.lo+u.hi)/2, (v.lo+v.hi)/2));
                    QString error;
                    std::cout << path.toStdString() << " loft " << i << " face " << face.index << std::endl;
                    const auto result = forgeOffsetFaces(feature.forgeBody, {ref}, offsetDistance, &error, nullptr, false);
                    std::cout << "  " << (result ? "OK" : error.toStdString()) << std::endl;
                    require(bool(result), "offset della faccia loft del documento");
                }
            }
        }
    }
    static void offsetLoftSides(const QString &path, double distance, bool rebuild = false, const QString &output = {}) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento offset laterali");
        CadViewport viewport;
        viewport.loadDocument(document);
        const int count = viewport.extrusions_.size();
        int tested = 0;
        for (int i = 0; i < count; ++i) {
            auto feature = viewport.extrusions_.at(i);
            if (feature.feature != BodyFeature::Loft || !feature.forgeBody) continue;
            if (rebuild) {
                feature.cachedGeometry = false;
                ForgeCad::buildGeometry(feature, i, viewport.sketches_, viewport.extrusions_);
                require(feature.error.isEmpty() && bool(feature.forgeBody), "rigenerazione loft dalle sezioni");
                viewport.extrusions_[i] = feature;
            }
            ExtrusionObject definition;
            definition.feature = BodyFeature::SurfaceOffset;
            definition.firstBody = i;
            definition.distance = distance;
            definition.name = QStringLiteral("Offset laterali regressione");
            for (const auto face : feature.forgeBody->faces()) {
                const auto surface = feature.forgeBody->face(face).surface;
                if (surface->type() != Kernel::SurfaceType::BSpline) continue;
                const auto u = surface->uDomain(), v = surface->vDomain();
                definition.offsetFaces.push_back(faceReference(*feature.forgeBody, face,
                    surface->point(u.lo + 0.25 * u.length(), v.lo + 0.5 * v.length())));
            }
            if (definition.offsetFaces.isEmpty()) continue;
            if (!output.isEmpty()) {
                require(rebuild, "salvare la variante solo dopo la rigenerazione");
                require(QFileInfo(path).absoluteFilePath() != QFileInfo(output).absoluteFilePath(), "salvare una copia separata");
                viewport.extrusions_.resize(i + 1);
                for (auto &body : viewport.extrusions_) body.visible = false;
                for (auto &sketch : viewport.sketches_) sketch.visible = false;
            }
            for (bool sew : {false, true}) {
                if (!output.isEmpty() && !sew) continue;
                definition.offsetSew = sew;
                const QString error = viewport.createBody(definition);
                std::cout << feature.name.toStdString() << " laterali: d=" << distance << " cucitura=" << sew
                          << " " << (error.isEmpty() ? "OK" : error.toStdString()) << std::endl;
                require(error.isEmpty(), "creazione offset laterali dal comando dell'applicazione");
                const auto result = viewport.extrusions_.back().forgeBody;
                require(result && Kernel::checkBody(*result).empty(), "offset laterali valido");
                std::cout << "  laterali loft=" << definition.offsetFaces.size() << " facce offset=" << result->faces().size() << std::endl;
                if (sew) require(result->shells().size() == 1, "laterali dell'offset cucite");
                Kernel::TessellationOptions options;
                options.deflection = 0.02;
                require(Kernel::tessellate(*result, options).failedFaces == 0, "tutte le facce dell'offset visualizzabili");
                ++tested;
                if (!output.isEmpty()) {
                    require(saveDocumentFile(output, viewport.documentState()).isEmpty(), "salvataggio variante loft e offset rigenerati");
                    DocumentState saved;
                    require(loadDocumentFile(output, saved).isEmpty() && saved.extrusions.back().forgeBody
                            && Kernel::checkBody(*saved.extrusions.back().forgeBody).empty(), "rilettura della variante rigenerata");
                    return;
                }
            }
        }
        require(tested > 0, "almeno un loft verificato");
    }
    static void extendTopologyFace(const QString &path, int bodyIndex, int faceIndex, double distance) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento per estensione faccia");
        CadViewport viewport;
        viewport.loadDocument(document);
        require(bodyIndex >= 0 && bodyIndex < viewport.extrusions_.size(), "indice corpo dell'estensione");
        const ExtrusionObject &feature = viewport.extrusions_.at(bodyIndex);
        require(bool(feature.forgeBody), "geometria del corpo da estendere");
        const Kernel::Body &body = *feature.forgeBody;
        const Kernel::FaceId face(faceIndex);
        require(body.contains(face), "faccia da estendere");
        const Kernel::Surface &surface = *body.face(face).surface;
        std::cout << "B" << bodyIndex << ":F" << faceIndex << " " << feature.name.toStdString()
                  << " feature=" << int(feature.feature) << " faces=" << body.faces().size()
                  << " edges=" << body.edges().size()
                  << " domain=[" << surface.uDomain().lo << "," << surface.uDomain().hi
                  << "]x[" << surface.vDomain().lo << "," << surface.vDomain().hi << "]" << std::endl;
        int tested = 0;
        bool sameSurfaceFailed = false;
        QVector<EdgePoint> allBoundaryEdges;
        for (Kernel::LoopId loop : body.face(face).loops)
            for (Kernel::FinId fin : body.loopFins(loop)) {
                const Kernel::EdgeId edge = body.fin(fin).edge;
                if (!body.isLaminar(edge)) continue;
                const Kernel::Edge &geometry = body.edge(edge);
                const Kernel::Fin &finGeometry = body.fin(fin);
                if (finGeometry.pcurve) {
                    std::cout << "  E" << edge.index << " UV";
                    for (double fraction : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                        const Kernel::Vec2 uv = finGeometry.pcurve->point(
                            geometry.range.lo + fraction * geometry.range.length());
                        std::cout << " (" << uv.x() << "," << uv.y() << ")";
                    }
                    std::cout << std::endl;
                }
                const auto reference = edgeReference(body, edge,
                    geometry.curve->point(0.5 * (geometry.range.lo + geometry.range.hi)));
                allBoundaryEdges.append(reference);
                for (bool linear : {false, true}) {
                    QString error;
                    const ForgeBody result = forgeExtendSheet(feature.forgeBody, {reference}, distance, linear, &error);
                    std::cout << "  E" << edge.index << " linear=" << linear << ": "
                              << (result ? "OK" : error.toStdString()) << std::endl;
                    if (!linear && !result) sameSurfaceFailed = true;
                    if (result) {
                        require(Kernel::checkBody(*result).empty(), "corpo esteso valido");
                        Kernel::TessellationOptions options;
                        options.deflection = 0.02;
                        const Kernel::Tessellation mesh = Kernel::tessellate(*result, options);
                        std::size_t points = 0, triangles = 0;
                        for (const Kernel::FaceMesh &part : mesh.faces) {
                            points += part.points.size();
                            triangles += part.triangles.size();
                        }
                        std::cout << "    result faces=" << result->faces().size()
                                  << " meshFaces=" << mesh.faces.size() << " points=" << points
                                  << " triangles=" << triangles << " failed=" << mesh.failedFaces << std::endl;
                        require(mesh.failedFaces == 0 && triangles > 0, "corpo esteso visualizzabile");
                    }
                    CadViewport applied;
                    applied.loadDocument(document);
                    const QString createError = applied.createSheetExtend(
                        bodyIndex, {reference}, distance, linear, QStringLiteral("Estensione regressione"));
                    require(createError.isEmpty(), "applicazione dell'estensione dal pannello");
                    const ExtrusionObject &created = applied.extrusions_.back();
                    std::cout << "    applied visible=" << created.visible
                              << " vertices=" << created.display.vertices.size()
                              << " edges=" << created.display.edges.size()
                              << " sourceVisible=" << applied.extrusions_.at(bodyIndex).visible << std::endl;
                    require(created.visible && !created.display.vertices.isEmpty(),
                            "la feature applicata conserva una mesh visibile");
                    if (edge.index == 0 && !linear) {
                        CadViewport previewed;
                        previewed.loadDocument(document);
                        previewed.requestExtendPreview(bodyIndex, {reference}, distance, linear, -1);
                        QElapsedTimer timeout;
                        timeout.start();
                        while (!(previewed.preview_.valid || !previewed.preview_.error.isEmpty()) && timeout.elapsed() < 30000)
                            QApplication::processEvents(QEventLoop::AllEvents, 50);
                        require(previewed.preview_.valid && previewed.preview_.geometry,
                                "anteprima dell'estensione pronta");
                        const ForgeBody previewGeometry = previewed.preview_.geometry;
                        const QString promotedError = previewed.createSheetExtend(
                            bodyIndex, {reference}, distance, linear, QStringLiteral("Estensione da anteprima"));
                        require(promotedError.isEmpty(), "conferma dell'estensione mostrata in anteprima");
                        require(previewed.preview_.key.isEmpty(), "anteprima chiusa alla conferma");
                        require(previewed.extrusions_.back().forgeBody == previewGeometry
                                    && previewed.extrusions_.back().visible
                                    && !previewed.extrusions_.back().display.vertices.isEmpty(),
                                "geometria dell'anteprima promossa e visibile");
                    }
                }
                ++tested;
            }
        for (bool linear : {false, true}) {
            QString error;
            const ForgeBody result = forgeExtendSheet(feature.forgeBody, allBoundaryEdges, distance, linear, &error);
            std::cout << "  ALL linear=" << linear << ": " << (result ? "OK" : error.toStdString()) << std::endl;
            require(bool(result), "estensione contemporanea di tutti i bordi della faccia");
            Kernel::TessellationOptions options;
            options.deflection = 0.02;
            const Kernel::Tessellation mesh = Kernel::tessellate(*result, options);
            std::size_t triangles = 0;
            for (const Kernel::FaceMesh &part : mesh.faces) triangles += part.triangles.size();
            std::cout << "    result faces=" << result->faces().size() << " triangles=" << triangles
                      << " failed=" << mesh.failedFaces << std::endl;
            require(Kernel::checkBody(*result).empty() && mesh.failedFaces == 0 && triangles > 0,
                    "risultato visibile dell'estensione dell'intera faccia");
            CadViewport applied;
            applied.loadDocument(document);
            const QString createError = applied.createSheetExtend(
                bodyIndex, allBoundaryEdges, distance, linear, QStringLiteral("Estensione intera faccia"));
            require(createError.isEmpty(), "applicazione dell'estensione dell'intera faccia");
            const ExtrusionObject &created = applied.extrusions_.back();
            std::cout << "    applied visible=" << created.visible
                      << " vertices=" << created.display.vertices.size()
                      << " edges=" << created.display.edges.size()
                      << " sourceVisible=" << applied.extrusions_.at(bodyIndex).visible << std::endl;
            require(created.visible && !created.display.vertices.isEmpty(),
                    "feature visibile dell'estensione dell'intera faccia");
            if (!linear) {
                CadViewport previewed;
                previewed.loadDocument(document);
                previewed.requestExtendPreview(bodyIndex, allBoundaryEdges, distance, linear, -1);
                QElapsedTimer timeout;
                timeout.start();
                while (!(previewed.preview_.valid || !previewed.preview_.error.isEmpty()) && timeout.elapsed() < 30000)
                    QApplication::processEvents(QEventLoop::AllEvents, 50);
                require(previewed.preview_.valid && previewed.preview_.geometry,
                        "anteprima dell'estensione dell'intera faccia pronta");
                const ForgeBody previewGeometry = previewed.preview_.geometry;
                const QString promotedError = previewed.createSheetExtend(
                    bodyIndex, allBoundaryEdges, distance, linear, QStringLiteral("Estensione faccia da anteprima"));
                require(promotedError.isEmpty(), "conferma dell'estensione dell'intera faccia");
                const ExtrusionObject &promoted = previewed.extrusions_.back();
                require(previewed.preview_.key.isEmpty() && promoted.forgeBody == previewGeometry
                            && promoted.visible && !promoted.display.vertices.isEmpty(),
                        "estensione dell'intera faccia promossa e visibile");
            }
        }
        require(tested > 0, "almeno un bordo libero della faccia da estendere");
        require(!sameSurfaceFailed, "estensione B-spline lungo la stessa superficie");
    }
    static void repairExtensionContours(const QString &path, const QString &output, const QStringList &indices) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        DocumentState state;
        require(loadDocumentFile(path, state).isEmpty(), "lettura modello da rigenerare");
        // "from=N": rigenera tutta la storia da N senza cambiare i contorni.
        QSet<int> changed;
        int first = state.extrusions.size();
        bool nearestRemap = false;
        for (const auto &index : indices) {
            if (index == QStringLiteral("nearest")) nearestRemap = true;
            else if (index.startsWith(QStringLiteral("from="))) first = std::min(first, index.mid(5).toInt());
            else changed.insert(index.toInt());
        }
        const auto original = state.extrusions;
        for (int index : changed) {
            require(index >= 0 && index < state.extrusions.size(), "indice estensione");
            auto &feature = state.extrusions[index];
            require(feature.feature == BodyFeature::SheetExtend && feature.firstBody >= 0, "feature di estensione");
            feature.blendEdges = completeExtensionContours(original.at(feature.firstBody).forgeBody, feature.blendEdges);
            require(!feature.blendEdges.isEmpty(), "contorni dell'estensione completi");
            first = std::min(first, index);
        }
        for (int index = first; index < state.extrusions.size(); ++index) {
            auto &feature = state.extrusions[index];
            const auto remap = [&](int base, EdgePoint &point, int kind) {
                if (base < first || base >= index || !state.extrusions.at(base).forgeBody) return;
                const auto &body = *state.extrusions.at(base).forgeBody;
                Box box;
                for (auto v : body.vertices()) box.add(body.vertex(v).point);
                const double reach = 1e-3 * std::max(1.0, box.diagonal());
                const Vec3 p(point.x, point.y, point.z);
                if (kind == 4 && nearestRemap) {
                    // Topologia cambiata a monte: gli ID non valgono piu', conta solo lo spigolo piu' vicino.
                    EdgeId best;
                    double bestDistance = reach * 100.0;
                    for (auto candidate : body.edges()) {
                        const auto &geometry = body.edge(candidate);
                        const double distance = norm(projectPoint(*geometry.curve, p, geometry.range).point - p);
                        if (distance < bestDistance) { bestDistance = distance; best = candidate; }
                    }
                    require(best.valid(), "spigolo piu' vicino dopo rigenerazione");
                    point = edgeReference(body, best, projectPoint(*body.edge(best).curve, p, body.edge(best).range).point);
                } else if (kind == 4) {
                    const auto e = resolveEdgeReference(body, point, reach, ReferenceState::Other);
                    require(e.valid(), "riferimento allo spigolo dopo rigenerazione");
                    point = edgeReference(body, e, projectPoint(*body.edge(e).curve, p, body.edge(e).range).point);
                } else if (kind == 5) {
                    const auto f = resolveFaceReference(body, point, reach, ReferenceState::Other);
                    require(f.valid(), "riferimento alla faccia dopo rigenerazione");
                    point = faceReference(body, f, p);
                }
            };
            for (auto &point : feature.blendEdges) remap(feature.firstBody, point, 4);
            for (auto *refs : {&feature.planarRefs, &feature.datum.refs})
                for (auto &ref : *refs) remap(ref.index, ref.point, ref.kind);
            if (nearestRemap) {
                // Piu' frammenti vecchi possono finire sullo stesso spigolo nuovo.
                QSet<QPair<int, int>> seen;
                for (int k = feature.blendEdges.size() - 1; k >= 0; --k) {
                    const QPair<int, int> key(-1, feature.blendEdges[k].subshape);
                    if (seen.contains(key)) feature.blendEdges.remove(k); else seen.insert(key);
                }
                seen.clear();
                for (int k = feature.planarRefs.size() - 1; k >= 0; --k) {
                    if (feature.planarRefs[k].kind != 4) continue;
                    const QPair<int, int> key(feature.planarRefs[k].index, feature.planarRefs[k].point.subshape);
                    if (seen.contains(key)) feature.planarRefs.remove(k); else seen.insert(key);
                }
            }
            feature.cachedGeometry = false;
            ForgeCad::buildGeometry(feature, index, state.sketches, state.extrusions);
            std::cout << index << " " << feature.name.toStdString() << ": " << feature.error.toStdString() << std::endl;
            if (!feature.error.isEmpty() && feature.firstBody >= 0 && state.extrusions.at(feature.firstBody).forgeBody) {
                const auto &body = *state.extrusions.at(feature.firstBody).forgeBody;
                for (const auto &point : feature.blendEdges)
                    std::cout << "  rif (" << point.x << "," << point.y << "," << point.z << ") id=" << point.subshape << std::endl;
                for (auto e : body.edges()) if (body.isLaminar(e)) {
                    const auto &edge = body.edge(e);
                    const auto a = edge.curve->point(edge.range.lo), m = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi)), b = edge.curve->point(edge.range.hi);
                    std::cout << "  libero E" << e.index << " (" << a.x() << "," << a.y() << "," << a.z() << ") ("
                              << m.x() << "," << m.y() << "," << m.z() << ") (" << b.x() << "," << b.y() << "," << b.z() << ")" << std::endl;
                }
            }
            require(feature.error.isEmpty(), "rigenerazione della storia dopo estensione");
            ForgeCad::tessellateGeometry(feature, 0, feature.display);
        }
        require(saveDocumentFile(output, state).isEmpty(), "salvataggio modello con contorni completi");
    }
    static void auditSheetHistory(const QString &path, const QString &outputDirectory) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento per controllo superfici");
        if (!outputDirectory.isEmpty())
            require(QDir().mkpath(outputDirectory), "cartella di esportazione della geometria diagnostica");
        CadViewport viewport;
        viewport.loadDocument(document);
        for (int i = 0; i < viewport.extrusions_.size(); ++i) {
            const auto &feature = viewport.extrusions_.at(i);
            std::cout << "B" << i << " " << feature.name.toStdString() << " base=" << feature.firstBody
                      << " second=" << feature.secondBody << " length=" << feature.blendSize
                      << " linear=" << feature.extendLinear << " sewTol=" << feature.sewTolerance << std::endl;
            if (!feature.forgeBody) continue;
            const auto &body = *feature.forgeBody;
            if (!outputDirectory.isEmpty()) {
                std::ofstream file((outputDirectory + QStringLiteral("/B%1.body").arg(i)).toStdString(), std::ios::binary);
                file << writeBodyBinary(body);
                require(bool(file), "scrittura della geometria diagnostica");
            }
            const auto issues = checkBody(body);
            int free = 0;
            for (auto e : body.edges()) free += body.isLaminar(e);
            std::cout << " faces=" << body.faces().size() << " free=" << free << " issues=" << issues.size() << std::endl;
            for (const auto &issue : issues) std::cout << " ISSUE " << issue.message << std::endl;
            for (auto f : body.faces()) {
                const auto box = faceBox(body, f);
                std::cout << " F" << f.index << " y=" << box.lo.y() << ".." << box.hi.y() << " free:";
                for (auto e : faceBoundaryEdges(body, f)) if (body.isLaminar(e)) {
                    const auto &edge = body.edge(e);
                    const auto a = edge.curve->point(edge.range.lo), b = edge.curve->point(edge.range.hi);
                    std::cout << " E" << e.index << "(" << a.x() << "," << a.y() << "," << a.z()
                              << " -> " << b.x() << "," << b.y() << "," << b.z() << ")";
                }
                std::cout << std::endl;
            }
            if (feature.feature == BodyFeature::SheetExtend && feature.firstBody >= 0) {
                require(feature.firstBody < viewport.extrusions_.size()
                            && bool(viewport.extrusions_.at(feature.firstBody).forgeBody), "base dell'estensione disponibile");
                const auto &base = *viewport.extrusions_.at(feature.firstBody).forgeBody;
                for (const auto &ref : feature.blendEdges) {
                    const auto edge = resolveEdgeReference(base, ref, 0.1);
                    std::cout << " selected=" << edge.index << " stored=" << ref.subshape;
                    if (edge.valid()) {
                        const auto &e = base.edge(edge);
                        const auto mid = e.curve->point((e.range.lo + e.range.hi) * 0.5);
                        std::cout << " midpoint=" << mid.x() << "," << mid.y() << "," << mid.z();
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
    static void trimTopologyBodies(const QString &path, int first, int second, bool verifyClosed = false) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura documento per taglio superfici");
        CadViewport viewport;
        viewport.loadDocument(document);
        QVector<SheetPiece> regions[2];
        for (int target : {first, second}) {
            const int tool = target == first ? second : first;
            require(target >= 0 && target < viewport.extrusions_.size()
                        && tool >= 0 && tool < viewport.extrusions_.size(), "indici corpi del taglio");
            const ExtrusionObject &a = viewport.extrusions_.at(target), &b = viewport.extrusions_.at(tool);
            std::cout << "B" << target << " " << a.name.toStdString() << " feature=" << int(a.feature)
                      << " solidFlag=" << a.solid << " loftSurface=" << a.loftSurface
                      << " sheet=" << bool(a.forgeBody && a.forgeBody->isSheet())
                      << " faces=" << (a.forgeBody ? a.forgeBody->faces().size() : 0)
                      << " <- B" << tool << " " << b.name.toStdString() << " feature=" << int(b.feature)
                      << " solidFlag=" << b.solid << " loftSurface=" << b.loftSurface
                      << " sheet=" << bool(b.forgeBody && b.forgeBody->isSheet())
                      << " faces=" << (b.forgeBody ? b.forgeBody->faces().size() : 0) << std::endl;
            require(bool(a.forgeBody) && bool(b.forgeBody), "geometria dei corpi del taglio disponibile");
            for (auto face : a.forgeBody->faces()) {
                const auto box = Kernel::faceBox(*a.forgeBody, face);
                std::cout << "  F" << face.index << " type=" << int(a.forgeBody->face(face).surface->type())
                          << " box=" << box.lo.x() << "," << box.lo.y() << "," << box.lo.z()
                          << " / " << box.hi.x() << "," << box.hi.y() << "," << box.hi.z() << std::endl;
            }
            QString error;
            const QVector<SheetPiece> pieces = forgeSheetPieces(a.forgeBody, b.forgeBody, 0, &error);
            regions[target == first ? 0 : 1] = pieces;
            std::cout << "  pieces=" << pieces.size() << " error=" << error.toStdString();
            for (const SheetPiece &piece : pieces) std::cout << " area=" << piece.area;
            std::cout << std::endl;
        }
        if (verifyClosed) {
            require(regions[0].size() == 2 && regions[1].size() == 2, "due regioni su entrambi i corpi");
            bool closed = false;
            for (const auto &a : regions[0]) for (const auto &b : regions[1]) {
                QString error;
                const auto joined = forgeSew({a.geometry, b.geometry}, 1e-6, false, &error);
                require(bool(joined) && error.isEmpty() && Kernel::checkBody(*joined).empty(), "applicazione del taglio reciproco");
                int free = 0;
                for (auto e : joined->edges()) free += joined->isLaminar(e);
                closed = closed || free == 0;
                std::cout << "  taglio reciproco: bordi liberi=" << free << std::endl;
            }
            require(closed, "almeno una scelta produce il loft chiuso dal piano");
        }
    }
    // Riproduzione del raccordo sui contorni delle facce di un documento.
    // Salva solo dopo aver verificato risultato, chiusura e rilettura della copia.
    static void regularizeSplineDocument(const QStringList &args) {
        using namespace ForgeCad;
        require(args.size() >= 2 && QFileInfo(args[0]).absoluteFilePath() != QFileInfo(args[1]).absoluteFilePath(), "input e copia distinti");
        DocumentState state;
        require(loadDocumentFile(args[0],state).isEmpty(), "lettura documento");
        require(!state.sketches.isEmpty() && !state.sketches[0].curves.isEmpty(), "spline del profilo");
        auto &sketch = state.sketches[0];
        auto &curve = sketch.curves[0];
        for (const auto &c : sketch.geometricConstraints) std::cout << describeConstraint(sketch,c).toStdString() << std::endl;
        require(curve.tool == DrawingTool::Spline, "profilo spline");
        shapeSpline(curve,{true,false,false});
        const SolveResult solved = solveSketch(sketch);
        require(solved.ok, "vincoli compatibili con la spline regolarizzata");
        recalculateCurve(curve);
        for (auto &body : state.extrusions) { body.forgeBody.reset(); body.display = {}; }
        CadViewport v; v.loadDocument(state);
        for (int i = 0; i < v.extrusions_.size(); ++i) {
            const auto &body = v.extrusions_[i];
            std::cout << "B" << i << " " << body.error.toStdString() << std::endl;
            require(body.error.isEmpty(), "rigenerazione della storia dopo regolarizzazione");
        }
        require(saveDocumentFile(args[1],v.currentDocument()).isEmpty(), "salvataggio copia regolarizzata");
    }
    static void blendDocumentFaces(const QStringList &args) {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        require(args.size() >= 5, "uso: --blend-document-faces input output corpo raggio facce... (o E<spigolo>)");
        require(QFileInfo(args[0]).absoluteFilePath() != QFileInfo(args[1]).absoluteFilePath(),
                "il risultato deve essere una copia del documento");
        bool validBody = false, validRadius = false;
        const int baseIndex = args[2].toInt(&validBody);
        const double radius = args[3].toDouble(&validRadius);
        require(validBody && validRadius && std::isfinite(radius) && radius > 0.0, "corpo e raggio validi");
        DocumentState state;
        require(loadDocumentFile(args[0], state).isEmpty(), "lettura documento da raccordare");
        require(baseIndex >= 0 && baseIndex < state.extrusions.size(), "indice del corpo da raccordare");
        const auto base = state.extrusions.at(baseIndex).forgeBody;
        require(bool(base), "geometria del corpo da raccordare disponibile");
        QVector<EdgePoint> references;
        std::set<int> selected;
        const auto faces = base->faces();
        for (const auto &value : args.mid(4)) {
            bool valid = false;
            // "E<n>": un solo spigolo invece dei bordi di una faccia.
            if (value.startsWith(QLatin1Char('E'))) {
                const EdgeId edge(value.mid(1).toInt(&valid));
                const auto edges = base->edges();
                require(valid && std::find(edges.begin(), edges.end(), edge) != edges.end(), "indice spigolo valido");
                if (!selected.insert(edge.index).second) continue;
                const auto &e = base->edge(edge);
                references.append(edgeReference(*base, edge, e.curve->point(0.5 * (e.range.lo + e.range.hi))));
                continue;
            }
            const FaceId face(value.toInt(&valid));
            require(valid && std::find(faces.begin(), faces.end(), face) != faces.end(), "indice faccia valido");
            for (const auto edge : faceBoundaryEdges(*base, face)) {
                if (!selected.insert(edge.index).second) continue;
                const auto &e = base->edge(edge);
                references.append(edgeReference(*base, edge, e.curve->point(0.5 * (e.range.lo + e.range.hi))));
            }
        }
        require(!references.isEmpty(), "contorni delle facce selezionati");
        // La copia conserva la storia originale; mostra soltanto la nuova variante.
        for (auto &feature : state.extrusions) feature.visible = false;
        for (auto &sketch : state.sketches) sketch.visible = false;
        CadViewport viewport;
        viewport.loadDocument(state);
        // Prima il percorso dell'anteprima (thread, tassellazione della patch), come nell'app.
        viewport.requestBlendPreview(baseIndex, references, radius, false);
        viewport.startPreviewJob();
        while (viewport.previewRunning_) QApplication::processEvents(QEventLoop::AllEvents, 50);
        std::cout << "anteprima: " << (viewport.preview_.valid ? std::string("riuscita") : viewport.preview_.error.toStdString()) << std::endl;
        const QString error = viewport.createBlend(baseIndex, references, radius, false,
            QStringLiteral("Raccordi facce loft R%1 mm").arg(radius));
        std::cout << "R=" << radius << " mm: " << error.toStdString() << std::endl;
        require(error.isEmpty(), "creazione raccordi sui contorni delle facce");
        // Il corpo logico era nascosto con gli altri: la variante nuova deve vedersi all'apertura.
        viewport.setObjectVisible(SceneObjectKind::Extrusion, int(viewport.extrusions_.size()) - 1, true);
        require(viewport.extrusions_.back().visible, "raccordo visibile nella copia");
        const auto result = viewport.extrusions_.back().forgeBody;
        require(bool(result) && forgeBlendHasEffect(base, result), "raccordi effettivamente costruiti");
        require(checkBody(*result).empty(), "B-rep raccordato valido");
        const auto mesh = tessellate(*result, {});
        require(mesh.failedFaces == 0 && mesh.faces.size() == result->faces().size(), "tutte le facce raccordate visualizzabili");
        for (const auto edge : result->edges()) require(!result->isLaminar(edge), "corpo raccordato chiuso");
        require(saveDocumentFile(args[1], viewport.documentState()).isEmpty(), "salvataggio copia raccordata");
        DocumentState saved;
        require(loadDocumentFile(args[1], saved).isEmpty(), "rilettura copia raccordata");
        require(saved.extrusions.back().forgeBody
                    && checkBody(*saved.extrusions.back().forgeBody).empty()
                    && saved.extrusions.back().forgeBody->faces().size() == result->faces().size(),
                "raccordi conservati nella copia salvata");
        std::cout << "Copia verificata: " << args[1].toStdString() << ", facce=" << result->faces().size() << std::endl;
    }
    static void loftCorner(const QString &path) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(path, document).isEmpty(), "lettura loft di regressione");
        for (int i = 0; i < document.extrusions.size(); ++i) {
            auto &feature = document.extrusions[i];
            feature.cachedGeometry = false;
            ForgeCad::buildGeometry(feature, i, document.sketches, document.extrusions);
            std::cout << i << " " << feature.name.toStdString() << std::endl;
        }
        const auto body = document.extrusions.back().forgeBody;
        require(bool(body), "loft rigenerato");
        // Riproduzione facoltativa della catena del file di esempio: bordo
        // longitudinale e le due continuazioni sui raccordi dei coperchi.
        QVector<EdgePoint> references;
        for (int index : {11, 77, 98}) {
            const Kernel::EdgeId edge(index);
            const auto &e = body->edge(edge);
            const auto p = e.curve->point(0.5 * (e.range.lo + e.range.hi));
            references.push_back(edgeReference(*body, edge, p));
        }
        QString error;
        const auto result = forgeBlend(body, references, 0.5, false, &error);
        std::cout << "Catena loft: " << error.toStdString() << std::endl;
        require(bool(result), "raccordo della catena tangente del loft rigenerato");
    }
    // Rendering di un file STEP importato da un punto di vista dato, per
    // confrontare a occhio la tassellazione: --render-step file.stp out.png
    // cx cy cz nx ny nz [semi-lato] (centro, normale verso l'osservatore).
    static void renderStep(const QStringList &args) {
        using namespace ForgeCad;
        QVector<ImportedPart> parts;
        require(importCadFile(args.at(0), parts).isEmpty() && !parts.isEmpty(), "importazione STEP");
        CadViewport v;
        v.resize(1200, 1200);
        v.importParts(parts, args.at(0));
        v.show();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        const QVector3D c(args.at(2).toFloat(), args.at(3).toFloat(), args.at(4).toFloat());
        const QVector3D n = QVector3D(args.at(5).toFloat(), args.at(6).toFloat(), args.at(7).toFloat()).normalized();
        const float half = args.size() > 8 ? args.at(8).toFloat() : 2.0f;
        QVector3D x = QVector3D::crossProduct(QVector3D(0, 0, 1), n);
        if (x.length() < 1e-3f) x = QVector3D(1, 0, 0);
        v.setViewFrame(x.normalized(), n);
        v.fitView({c - QVector3D(half, half, half), c + QVector3D(half, half, half)});
        v.update();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        require(v.grabFramebuffer().save(args.at(1)), "salvataggio dell'immagine");
    }
    static void renderSketchContext(const QStringList &args) {
        DocumentState document;
        require(ForgeCad::loadDocumentFile(args.at(0),document).isEmpty(),"lettura documento per contesto schizzo");
        CadViewport v;v.resize(1000,900);v.loadDocument(document);
        const int index=args.at(1).toInt();require(index>=0 && index<v.sketches_.size(),"indice schizzo valido");
        v.selectSketch(index);v.setSketchBodyOpacity(0.35);v.setViewFrame({1,0,0},{1,-2,1});v.fitAll();v.show();
        for(int i=0;i<10;++i)QApplication::processEvents();
        std::cout<<"sketch="<<index<<" boundary="<<v.sketchContextEnd_<<" cursor="<<v.historyPosition()<<" drawn=";
        for(int i=0;i<v.extrusions_.size();++i)if(v.isBodyDrawn(i))std::cout<<i<<",";
        std::cout<<std::endl;require(v.grabFramebuffer().save(args.at(2)),"cattura contesto schizzo");
    }

    // Stato del viewport durante "Modifica parametri" della funzione `index`
    // di un documento: --render-edit doc.prt indice prima.png dopo.png.
    static void renderEdit(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(args.at(0), document).isEmpty(), "lettura del documento");
        CadViewport v;
        v.resize(1000, 800);
        v.loadDocument(document);
        v.show();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        if (qEnvironmentVariableIsSet("FORGECAD_RENDER_VIEW")) v.setViewFrame(QVector3D(0, -1, 0), QVector3D(-1, 0, 0));
        v.fitAll();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        for (int i = 0; i < v.extrusions_.size(); ++i) {
            const ExtrusionObject &e = v.extrusions_.at(i);
            std::cout << i << " " << e.name.toStdString() << " visible=" << e.visible << " feature=" << int(e.feature)
                      << " body=" << e.modelBodyId << " suppressed=" << e.suppressed << " ops=";
            for (int o : ForgeCad::bodyOperands(e)) std::cout << o << ",";
            std::cout << " err=" << e.error.toStdString() << std::endl;
        }
        require(v.grabFramebuffer().save(args.at(2)), "immagine prima");
        const int index = args.at(1).toInt();
        v.requestPreview(v.extrusions_.at(index), index);
        QElapsedTimer timer;
        timer.start();
        while ((!v.preview_.valid && v.preview_.error.isEmpty()) && timer.elapsed() < 120000) QApplication::processEvents(QEventLoop::AllEvents, 50);
        std::cout << "preview valid=" << v.preview_.valid << " error=" << v.preview_.error.toStdString() << " replaced=";
        for (int r : v.preview_.replaced) std::cout << r << ",";
        std::cout << " triangles=" << v.preview_.display.vertices.size() / 3 << std::endl;
        v.update();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        require(v.grabFramebuffer().save(args.at(3)), "immagine dopo");
    }
    // Tempi della vista su un documento: --bench-view doc.prt [ripetizioni].
    // Disegno dopo rotazione e zoom, movimento del mouse senza tasti (hover)
    // e trascinamento con il sinistro (orbita), come li fa l'utente.
    static void benchView(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(args.at(0), document).isEmpty(), "lettura del documento");
        const int count = args.size() > 1 ? args.at(1).toInt() : 30;
        CadViewport v;
        v.resize(1400, 900);
        const auto paintNow = [](CadViewport &w) {
            w.makeCurrent();
            w.paintGL();
            glFinish();
            w.doneCurrent();
        };
        v.loadDocument(document);
        if (args.size() > 2) v.setAntialiasing(args.at(2).toInt());
        v.show();
        for (int i = 0; i < 10; ++i) QApplication::processEvents();
        v.fitAll();
        paintNow(v);
        v.makeCurrent();
        std::cout << "renderer: " << reinterpret_cast<const char *>(glGetString(GL_RENDERER)) << " dpr " << v.devicePixelRatioF()
                  << " campioni " << v.sceneRenderer_.bufferSamples_ << std::endl;
        v.doneCurrent();
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < count; ++i) { v.camera_.yaw_ += 2.0f; paintNow(v); }
        std::cout << "paint dopo rotazione: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) { v.setZoom(v.camera_.zoom_ * (i % 2 ? 1.05f : 0.95f)); paintNow(v); }
        std::cout << "paint dopo zoom: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) {
            const QPointF p(300 + 20 * i, 300 + 7 * i);
            v.updateHover(p.toPoint());
        }
        if (qEnvironmentVariableIsSet("BENCH_HOVER_ONLY")) {
            t.restart();
            for (int i = 0; i < 400; ++i) {
                const QPointF p(200 + (i * 37) % 900, 150 + (i * 53) % 600);
                v.updateHover(p.toPoint());
            }
            std::cout << "hover x400: " << double(t.nsecsElapsed()) / 1e6 / 400 << " ms" << std::endl;
            return;
        }
        std::cout << "hover (solo evento): " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        {
            QPointF p(400, 400);
            QMouseEvent press(QEvent::MouseButtonPress, p, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            v.mousePressEvent(&press);
            for (int i = 0; i < count; ++i) {
                p += QPointF(6, 2);
                QMouseEvent move(QEvent::MouseMove, p, p, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                v.mouseMoveEvent(&move);
                paintNow(v);
            }
            QMouseEvent release(QEvent::MouseButtonRelease, p, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            v.mouseReleaseEvent(&release);
        }
        std::cout << "orbita (evento + paint): " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) {
            QWheelEvent wheel(QPointF(500, 400), QPointF(500, 400), QPoint(), QPoint(0, i % 2 ? 120 : -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            v.wheelEvent(&wheel);
            paintNow(v);
        }
        std::cout << "rotella (evento + paint): " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
    }
    // Confronta la ricostruzione parametrica completa con l'apertura dello
    // snapshot B-rep + mesh: --bench-load doc.prt [ripetizioni snapshot].
    static void benchLoad(const QStringList &args) {
        using namespace ForgeCad;
        const int count = args.size() > 1 ? qMax(1, args.at(1).toInt()) : 5;
        QElapsedTimer timer;
        timer.start();
        DocumentState source;
        require(loadDocumentFile(args.at(0), source).isEmpty(), "lettura del documento da misurare");
        for (ExtrusionObject &body : source.extrusions) {
            body.cachedGeometry = false;
            body.forgeBody.reset();
            body.curve.reset();
            body.display = {};
        }
        CadViewport rebuilt;
        rebuilt.loadDocument(std::move(source));
        const double rebuiltMs = double(timer.nsecsElapsed()) / 1e6;

        QTemporaryDir directory;
        require(directory.isValid(), "directory temporanea per lo snapshot");
        const QString snapshot = directory.filePath(QStringLiteral("snapshot.prt"));
        require(saveDocumentFile(snapshot, rebuilt.currentDocument(), true).isEmpty(), "salvataggio dello snapshot");
        double snapshotMs = 0.0;
        for (int i = 0; i < count; ++i) {
            timer.restart();
            DocumentState state;
            require(loadDocumentFile(snapshot, state).isEmpty(), "lettura dello snapshot");
            CadViewport opened;
            opened.loadDocument(std::move(state));
            snapshotMs += double(timer.nsecsElapsed()) / 1e6;
        }
        snapshotMs /= count;
        std::cout << "ricostruzione completa: " << rebuiltMs << " ms\n"
                  << "apertura snapshot: " << snapshotMs << " ms (media di " << count << ")\n"
                  << "accelerazione: " << rebuiltMs / snapshotMs << "x\n"
                  << "file origine: " << QFileInfo(args.at(0)).size() << " byte, snapshot: "
                  << QFileInfo(snapshot).size() << " byte" << std::endl;
    }
    // Come --bench-view ma nella finestra completa dell'app, con le
    // impostazioni dell'utente copiate: tempo da un evento al fotogramma
    // mostrato (frameSwapped). --bench-window doc.prt [ripetizioni]
    static void benchWindow(const QStringList &args) {
        const int count = args.size() > 1 ? args.at(1).toInt() : 30;
        PdfWindow window;
        window.resize(2560, 1400);
        window.show();
        for (int i = 0; i < 20; ++i) QApplication::processEvents();
        require(window.openDocumentPath(args.at(0)), "apertura del documento");
        CadViewport *v = nullptr;
        for (QOpenGLWidget *w : window.findChildren<QOpenGLWidget *>())
            if (auto *c = dynamic_cast<CadViewport *>(w); c && (!v || c->width() > v->width())) v = c;
        require(v, "viewport");
        for (int i = 0; i < 20; ++i) QApplication::processEvents();
        int frames = 0;
        QObject::connect(v, &QOpenGLWidget::frameSwapped, [&] { ++frames; });
        v->makeCurrent();
        std::cout << "renderer: " << reinterpret_cast<const char *>(glGetString(GL_RENDERER)) << " size " << v->width() << "x" << v->height()
                  << " campioni " << v->sceneRenderer_.bufferSamples_ << std::endl;
        v->doneCurrent();
        const auto waitFrame = [&] {
            const int start = frames;
            QElapsedTimer t;
            t.start();
            while (frames == start && t.elapsed() < 2000) QApplication::processEvents(QEventLoop::AllEvents, 5);
        };
        if (!qEnvironmentVariableIsEmpty("FORGECAD_BENCH_SHOW")) {
            v->setObjectVisible(SceneObjectKind::Extrusion, qEnvironmentVariableIntValue("FORGECAD_BENCH_SHOW"), true);
            v->fitAll();
            for (int i = 0; i < 20; ++i) QApplication::processEvents();
        }
        const QPointF center(v->width() / 2.0, v->height() / 2.0);
        if (!qEnvironmentVariableIsEmpty("FORGECAD_BENCH_SHOT")) v->grabFramebuffer().save(qEnvironmentVariable("FORGECAD_BENCH_SHOT"));
        {
            const SceneSelection hit = v->pickSceneObject(center.toPoint(), false);
            std::cout << "al centro: tipo " << int(hit.kind) << " indice " << hit.index << std::endl;
        }
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < count; ++i) {
            QWheelEvent wheel(center, v->mapToGlobal(center), QPoint(), QPoint(0, i % 2 ? 120 : -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(v, &wheel);
            waitFrame();
        }
        std::cout << "rotella -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        QPointF p = center;
        // Hover sul corpo prima del clic, poi il primo fotogramma dell'orbita
        // a parte: e' l'attesa che si nota prima che la vista cominci a girare.
        t.restart();
        {
            QMouseEvent hover(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &hover);
            waitFrame();
        }
        std::cout << "primo hover -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
        t.restart();
        QMouseEvent press(QEvent::MouseButtonPress, p, v->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(v, &press);
        std::cout << "pressione: " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
        t.restart();
        for (int i = 0; i < count; ++i) {
            p += QPointF(8, 3);
            QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &move);
            waitFrame();
            if (i == 0) {
                std::cout << "primo fotogramma dell'orbita: " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
                t.restart();
            }
        }
        std::cout << "orbita -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 / std::max(1, count - 1) << " ms" << std::endl;
        QMouseEvent release(QEvent::MouseButtonRelease, p, v->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v, &release);
        t.restart();
        for (int i = 0; i < count; ++i) {
            p += QPointF(-8, 4);
            QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &move);
            waitFrame();
        }
        std::cout << "hover -> fotogramma: " << double(t.nsecsElapsed()) / 1e6 / count << " ms" << std::endl;
        if (!qEnvironmentVariableIsEmpty("FORGECAD_BENCH_IDLE")) {
            QElapsedTimer idle;
            idle.start();
            while (idle.elapsed() < qEnvironmentVariableIntValue("FORGECAD_BENCH_IDLE")) QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        // Clic semplice sul corpo (selezione al rilascio): tempo sincrono, poi il primo fotogramma dell'orbita successiva.
        for (int i = 0; i < 3; ++i) {
            const QPointF c = center + QPointF(20 * i, 10 * i);
            t.restart();
            QMouseEvent clickPress(QEvent::MouseButtonPress, c, v->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &clickPress);
            QMouseEvent clickRelease(QEvent::MouseButtonRelease, c, v->mapToGlobal(c), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &clickRelease);
            std::cout << "clic " << i << ": " << double(t.nsecsElapsed()) / 1e6 << " ms";
            t.restart();
            QPointF q = c;
            QMouseEvent orbitPress(QEvent::MouseButtonPress, q, v->mapToGlobal(q), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &orbitPress);
            for (int k = 0; k < 3; ++k) {
                q += QPointF(8, 3);
                QMouseEvent move(QEvent::MouseMove, q, v->mapToGlobal(q), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(v, &move);
            }
            waitFrame();
            std::cout << ", poi orbita al primo fotogramma " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
            QMouseEvent orbitRelease(QEvent::MouseButtonRelease, q, v->mapToGlobal(q), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &orbitRelease);
        }
        // Lavoro sincrono di ogni movimento senza tasti (hover), senza aspettare i fotogrammi.
        for (int i = 0; i < 6; ++i) {
            p += QPointF(i % 2 ? -15 : 12, 6);
            t.restart();
            QMouseEvent move(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &move);
            const double sent = double(t.nsecsElapsed()) / 1e6;
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            std::cout << "hover " << i << ": evento " << sent << " ms, con il timer " << double(t.nsecsElapsed()) / 1e6 << " ms" << std::endl;
        }
        window.close();
    }
    // Funzioni proprietarie delle facce cliccate su una griglia di pixel:
    // --pick-owner doc.prt
    static void pickOwner(const QStringList &args) {
        using namespace ForgeCad;
        DocumentState document;
        require(loadDocumentFile(args.at(0), document).isEmpty(), "lettura del documento");
        CadViewport v;
        v.resize(1000, 800);
        v.loadDocument(document);
        v.show();
        for (int i = 0; i < 8; ++i) QApplication::processEvents();
        v.fitAll();
        std::map<std::string, int> owners;
        QElapsedTimer t;
        double worst = 0.0, total = 0.0, pickTotal = 0.0;
        int clicks = 0;
        for (int y = 40; y < 800; y += 40)
            for (int x = 40; x < 1000; x += 40) {
                t.restart();
                const SceneSelection hit = v.pickSceneObject(QPoint(x, y));
                if (hit.kind != SceneObjectKind::Extrusion) continue;
                FaceHit face;
                if (!v.pickBodyFace(hit.index, QPoint(x, y), face)) continue;
                const double pickMs = double(t.nsecsElapsed()) / 1e6;
                const int owner = v.faceOwnerFeature(hit.index, QPoint(x, y), face);
                const double ms = double(t.nsecsElapsed()) / 1e6;
                pickTotal += pickMs;
                worst = std::max(worst, ms);
                total += ms;
                ++clicks;
                ++owners[v.extrusions_.at(hit.index).name.toStdString() + " -> " + v.extrusions_.at(owner).name.toStdString()];
            }
        // Dettaglio del raggio esatto sul corpo visibile: con e senza la finestra
        // attorno al punto della tassellazione.
        for (int i = 0; i < v.extrusions_.size(); ++i) {
            const ExtrusionObject &e = v.extrusions_.at(i);
            if (!e.visible || !e.forgeBody) continue;
            double withWindow = 0.0, without = 0.0, sketchPart = 0.0;
            int rays = 0, agree = 0;
            for (int y = 40; y < 800; y += 40)
                for (int x = 40; x < 1000; x += 40) {
                    QVector3D origin, direction;
                    v.viewRay(QPoint(x, y), origin, direction);
                    float meshT = 0.0f;
                    if (!CadViewport::meshRayHit(e.display, origin, direction, meshT)) continue;
                    ++rays;
                    if (qEnvironmentVariableIsSet("DUMP_RAYS")) {
                        static std::ofstream raysOut(qgetenv("DUMP_RAYS").toStdString());
                        raysOut.precision(17);
                        raysOut << origin.x() << " " << origin.y() << " " << origin.z() << " " << direction.x() << " " << direction.y() << " "
                                << direction.z() << " " << meshT << "\n";
                        static bool dumped = false;
                        if (!dumped) {
                            std::ofstream bodyOut(qgetenv("DUMP_RAYS").toStdString() + ".bin", std::ios::binary);
                            const std::string data = Kernel::writeBodyBinary(*e.forgeBody);
                            bodyOut.write(data.data(), std::streamsize(data.size()));
                            dumped = true;
                        }
                    }
                    FaceHit a, b;
                    t.restart();
                    const double slack = std::max(1e-6, 0.01 * e.display.rayIndex->bounds.diagonal());
                    const Kernel::Interval window{double(meshT) - slack, double(meshT) + slack};
                    const bool ha = forgePickFace(*e.forgeBody, origin, direction, a, e.display.rayIndex.get(), &window);
                    withWindow += double(t.nsecsElapsed()) / 1e6;
                    t.restart();
                    const bool hb = forgePickFace(*e.forgeBody, origin, direction, b, e.display.rayIndex.get());
                    without += double(t.nsecsElapsed()) / 1e6;
                    agree += ha == hb && (!ha || a.face == b.face);
                    t.restart();
                    v.pickSceneObject(QPoint(x, y), false);
                    sketchPart += double(t.nsecsElapsed()) / 1e6;
                }
            std::cout << "corpo " << i << " raggi " << rays << " finestra " << withWindow / std::max(1, rays) << " ms, tutte le facce "
                      << without / std::max(1, rays) << " ms, concordi " << agree << ", pick sulla mesh " << sketchPart / std::max(1, rays) << " ms" << std::endl;
        }
        for (const auto &[name, count] : owners) std::cout << count << "  " << name << std::endl;
        std::cout << "clic " << clicks << " media " << total / std::max(1, clicks) << " ms (di cui raggio " << pickTotal / std::max(1, clicks)
                  << "), peggiore " << worst << " ms" << std::endl;
        for (int i = 0; i < v.extrusions_.size(); ++i) if (v.extrusions_.at(i).visible) std::cout << "visibile " << i << " " << v.extrusions_.at(i).name.toStdString() << std::endl;
    }
    static void spatialSketch(bool gl=false) {
        using namespace ForgeCad;using namespace ForgeCad::Kernel;
        SpatialSketchParameters p;SpatialCurve3D first;first.id=1;first.spline=true;
        first.points={{0,0,0},{3,2,4},{7,-1,-2},{10,0,5}};p.entities={first};p.nextId=2;
        QString error;auto curves=buildSpatialSketch(p,&error);
        require(error.isEmpty() && curves.size()==1,"costruzione spline libera spaziale");
        for(int i=0;i<first.points.size();++i){const auto &q=first.points[i];require(distance(curves[0]->point(i),{q.x,q.y,q.z})<1e-10,"spline 3D interpola punti XYZ esatti");}
        for(int i=1;i<3;++i)require(distance(curves[0]->derivative(i),curves[0]->derivative(i-1e-8))<1e-6,"spline 3D continua nelle tangenti");
        p.entities[0].spline=false;curves=buildSpatialSketch(p,&error);
        require(error.isEmpty() && curves[0]->type()==CurveType::BSpline,"polilinea 3D mantiene le pieghe");
        p.entities[0].closed=true;curves=buildSpatialSketch(p,&error);
        require(error.isEmpty() && distance(curves[0]->point(0),curves[0]->point(4))<1e-10,"polilinea spaziale chiusa");
        p.entities[0].spline=true;curves=buildSpatialSketch(p,&error);
        require(error.isEmpty() && distance(curves[0]->derivative(0),curves[0]->derivative(4))<1e-10,"spline chiusa continua alla cucitura");
        p.entities[0]=first;
        SpatialCurve3D second;second.id=2;second.points={{0,20,0},{10,20,5}};p.entities.append(second);p.nextId=3;
        ExtrusionObject sketch;sketch.feature=BodyFeature::SpatialSketch;sketch.featureId=701;sketch.name="Schizzo 3D";sketch.spatialSketch=p;
        buildBody(sketch,0,{}, {},0);
        require(sketch.error.isEmpty() && sketch.curves.size()==2 && sketch.display.edges.size()==2 && isCurveBody(sketch),"entita' multiple visualizzate in schizzo 3D");
        require(bodyOperands(sketch).isEmpty() && sketchesOf(sketch).isEmpty(),"nessun legame a un piano per schizzo spaziale");
        GeometryRef ref;ref.kind=9;ref.index=0;ref.featureId=701;ref.point.geometry=kSpatialCurveReference;ref.point.subshape=2;
        std::vector<PathSegment> path;QVector<ExtrusionObject> bodies{sketch};
        require(geometryRefPath(ref,1,{},bodies,path,&error) && distance(path[0].curve->point(0),{0,20,0})<1e-10,"riferimento a entita' spaziale per ID");
        GeometryRef endpoint=ref;endpoint.kind=10;endpoint.element.point=1;ResolvedRef resolved;
        require(resolveGeometryRef(endpoint,1,{},bodies,resolved,&error) && distance(resolved.point,{10,20,5})<1e-10,"estremo di seconda entita' 3D");
        bodies[0].spatialSketch.entities.removeFirst();buildBody(bodies[0],0,{}, {},0);
        require(geometryRefPath(ref,1,{},bodies,path,&error) && distance(path[0].curve->point(0),{0,20,0})<1e-10,"eliminazione di altra entita' non cambia riferimento");
        bodies[0].spatialSketch.entities[0].points[0]={0,30,7};buildBody(bodies[0],0,{}, {},0);
        require(geometryRefPath(ref,1,{},bodies,path,&error) && distance(path[0].curve->point(0),{0,30,7})<1e-10,"riferimento segue entita' spostata lontano dal vecchio punto");
        bodies[0].spatialSketch.entities[0].id=1;
        require(!geometryRefPath(ref,1,{},bodies,path,&error),"entita' eliminata rifiutata senza scegliere curva vicina");
        bodies={sketch};
        // Collegamento con le funzioni esistenti e taglio di una spline libera.
        ref.point.subshape=1;
        SketchObject cutter;cutter.segments={{{5,-10},{5,10}}};cutter.constraints={-1};cutter.segmentLengths={0};cutter.segmentAngles={-1};
        CurveExtractionParameters trim;trim.source=ref;trim.trimmed=true;trim.projected=true;trim.tool.kind=7;trim.tool.index=0;trim.tool.element={0,0,-1};
        auto cut=extractCurves3D(trim,1,{cutter},bodies,&error);
        require(error.isEmpty() && cut.size()==1 && cut[0]->domain().hi>1 && cut[0]->domain().hi<2,"taglio proiettato di spline libera 3D");
        SketchObject end;end.customFrame=true;end.frame.origin[1]=10;end.frame.origin[2]=10;
        end.segments={{{0,0},{10,0}}};end.constraints={-1};end.segmentLengths={0};end.segmentAngles={-1};
        GeometryRef endRef;endRef.kind=7;endRef.index=0;endRef.element={-1,-1,-1};
        ExtrusionObject loft;loft.feature=BodyFeature::Loft;loft.loftSurface=true;loft.loftSections={ref,endRef};
        auto sheet=forgeReferencedLoft(loft,1,{end},bodies,&error);
        require(sheet && checkBody(*sheet).empty(),"schizzo 3D libero come sezione loft");
        ExtrusionObject ruled;ruled.feature=BodyFeature::Ruled;ruled.ruledFirst={ref};endRef.element={0,0,-1};ruled.ruledSecond={endRef};
        buildGeometry(ruled,1,{end},bodies);require(ruled.forgeBody && ruled.error.isEmpty(),"schizzo 3D libero come bordo rigata");
        DocumentState state;state.extrusions={sketch};QTemporaryDir dir;const auto file=dir.filePath("spatial.prt");
        require(saveDocumentFile(file,state,false).isEmpty(),"salvataggio schizzo 3D formato 45");
        DocumentState loaded;require(loadDocumentFile(file,loaded).isEmpty(),"riapertura schizzo 3D");
        require(loaded.extrusions[0].spatialSketch.entities.size()==2 && loaded.extrusions[0].spatialSketch.nextId==3
            && loaded.extrusions[0].spatialSketch.entities[0].points[2].z==-2,"persistenza entita' e coordinate spaziali");
        CadViewport view;view.tessellationQuality_=0;view.restoreDocument(loaded);view.regenerateAll();
        require(view.extrusions_[0].curves.size()==2 && view.extrusions_[0].error.isEmpty(),"rigenerazione schizzo 3D senza cache");
        auto invalid=p;invalid.entities[0].points[1]=invalid.entities[0].points[0];
        require(buildSpatialSketch(invalid,&error).isEmpty() && !error.isEmpty(),"punti consecutivi coincidenti rifiutati");
        invalid=p;invalid.entities[0].points[0].z=qQNaN();require(buildSpatialSketch(invalid,&error).isEmpty(),"coordinate non finite rifiutate");
        invalid=p;invalid.entities[1].id=1;require(buildSpatialSketch(invalid,&error).isEmpty(),"ID di entita' duplicati rifiutati");
        // Clic a profondita' diversa, aggancio esatto e trascinamento per ruotare.
        CadViewport drawing;drawing.resize(800,600);drawing.setViewNormal(0);drawing.camera_.zoom_=40;drawing.camera_.autoFit_=false;
        QVector<SpatialPoint> picked;bool stopped=false;
        drawing.configureSpatialDrawing(1,4,{}, {},false);drawing.setSpatialDrawing([&](SpatialPoint q){picked.append(q);},[&]{stopped=true;});
        drawing.setInteractionLocked(true);
        const auto click=[&](const QPoint &at){
            QMouseEvent press(QEvent::MouseButtonPress,at,at,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);drawing.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease,at,at,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);drawing.mouseReleaseEvent(&release);
        };
        click(drawing.projectWorldPoint({2,3,4}).toPoint());require(picked.size()==1 && std::fabs(picked[0].z-4)<1e-8,"clic inserisce XYZ sul piano temporaneo a quota 4");
        drawing.configureSpatialDrawing(1,-3,{}, {},false);click(drawing.projectWorldPoint({8,5,-3}).toPoint());
        require(picked.size()==2 && std::fabs(picked[1].z+3)<1e-8,"clic successivo puo' cambiare profondita'");
        const SpatialPoint precise{2.123456789,3.987654321,11.123456789};
        drawing.configureSpatialDrawing(1,0,{}, {precise},true);SpatialPoint snapped;
        require(drawing.spatialPointAt(drawing.projectWorldPoint(QVector3D(precise.x,precise.y,precise.z)).toPoint(),snapped)
            && snapped.x==precise.x && snapped.y==precise.y && snapped.z==precise.z,"snap mantiene double e profondita' originale");
        const QPoint a(100,100),b(130,115);const float yaw=drawing.camera_.yaw_;
        QMouseEvent press(QEvent::MouseButtonPress,a,a,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);drawing.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,b,b,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);drawing.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,b,b,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);drawing.mouseReleaseEvent(&release);
        require(picked.size()==2 && drawing.camera_.yaw_!=yaw,"trascinamento ruota senza aggiungere punti");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);drawing.keyPressEvent(&escape);require(stopped,"Esc interrompe inserimento spaziale");
        drawing.setSpatialDrawing({},{});require(!drawing.spatialAdd_ && !drawing.spatialFinish_,"callback disegno puliti");
        // Il pannello crea e modifica entita', mantiene un undo locale e chiude.
        CadViewport editor;editor.tessellationQuality_=0;editor.resize(800,600);editor.setViewNormal(0);
        QMainWindow window;std::exception_ptr failure;bool applied=false;
        QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog*>("spatialSketchDialog");
            try {
                require(dialog && editor.spatialAdd_,"pannello 3D e disegno attivi");
                auto add=editor.spatialAdd_;add({0,0,0});add({3,2,4});
                auto *table=dialog->findChild<QTableWidget*>("spatialPoints");require(table->rowCount()==2,"punti inseriti nella vista compaiono nella tabella");
                table->item(1,2)->setText("7.125");
                double displayedZ=0; require(ForgeCad::evaluateExpression(table->item(1,2)->text(),displayedZ) && std::abs(displayedZ-7.125)<1e-9,"modifica XYZ nella tabella indipendente dal locale");
                QKeyEvent undoKey(QEvent::KeyPress,Qt::Key_Z,Qt::ControlModifier);editor.keyPressEvent(&undoKey);
                require(table->item(1,2)->text().startsWith("4"),"Ctrl Z locale ripristina quota");
                dialog->findChild<QPushButton*>("spatialRedo")->click();require(ForgeCad::evaluateExpression(table->item(1,2)->text(),displayedZ) && std::abs(displayedZ-7.125)<1e-9,"redo locale");
                dialog->findChild<QPushButton*>("spatialNewSpline")->click();add=editor.spatialAdd_;add({0,10,0});add({5,10,3});add({10,10,0});
                require(dialog->findChild<QComboBox*>("spatialEntities")->count()==2,"seconda entita' spline");
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            }catch(...){failure=std::current_exception();if(dialog)dialog->reject();}
        });
        const bool accepted=spatialSketchDialog(&window,&editor,"Nuovo schizzo 3D",-1,{},[&](const ExtrusionObject &d){applied=true;return editor.createBody(d);});
        if(failure)std::rethrow_exception(failure);
        require(accepted && applied && editor.extrusions_.size()==1 && editor.extrusions_[0].curves.size()==2,"pannello applica schizzo multientita'");
        require(!editor.spatialAdd_ && !editor.spatialUndo_ && !editor.spatialRedo_,"chiusura pannello elimina callback");
        editor.undo();require(editor.extrusions_.isEmpty(),"undo documento dopo schizzo 3D");editor.redo();require(editor.extrusions_.size()==1,"redo documento dopo schizzo 3D");
        QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog*>("spatialSketchDialog");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();});
        require(!spatialSketchDialog(&window,&editor,"Annulla",0,editor.extrusions_[0],[&](const ExtrusionObject &){return QString();})
            && !editor.spatialAdd_ && !editor.spatialUndo_ && editor.extrusions_.size()==1,"Annulla modifica conserva storia e pulisce callback");
        // Il picker riconosce anche la seconda entita' e i suoi estremi.
        editor.setViewNormal(0);editor.camera_.autoFit_=false;editor.camera_.zoom_=40;
        require(editor.beginReferencePick(DatumRoleCurve,-1).isEmpty(),"picker curve 3D attivo");GeometryRef pickedRef;
        require(editor.pickReference(editor.projectWorldPoint({5,10,3}).toPoint(),pickedRef)
            && pickedRef.kind==9 && pickedRef.point.geometry==kSpatialCurveReference && pickedRef.point.subshape==2,"picker identifica seconda curva per ID");
        editor.cancelReferencePick();editor.beginReferencePick(DatumRolePoint,-1);
        require(editor.pickReference(editor.projectWorldPoint({10,10,0}).toPoint(),pickedRef) && pickedRef.kind==10 && pickedRef.point.subshape==2,"picker estremo seconda curva");editor.cancelReferencePick();
        if(gl){view.resize(900,650);view.setViewPreset(4);view.fitAll();view.show();QApplication::processEvents();view.repaint();QApplication::processEvents();
            for(const auto &line:view.extrusions_[0].display.edges)for(const auto &point:line)
                require(QRectF(view.rect()).contains(view.projectWorldPoint(point)),"curve 3D interamente inquadrate");
            require(view.isValid(),"contesto GL per schizzo 3D");require(view.grabFramebuffer().save("/tmp/forgecad-spatial-sketch.png"),"immagine schizzo 3D");view.close();}
    }

    static void curveExtraction() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        const auto line=[](Vec3 a,Vec3 b)->PathSegment{return {std::make_shared<Line<3>>(a,b-a),{0,distance(a,b)}};};
        const auto spatial=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec3>{{0,0,2},{3,2,5},{7,-1,3},{10,0,4}});
        auto sheet=std::make_shared<const Body>(ruledSurface({{spatial,{0,1}}},{line({0,-5,0},{10,-5,0})}));
        EdgeId selected;
        for(EdgeId e:sheet->edges()) if(projectPoint(*sheet->edge(e).curve,spatial->point(.5),sheet->edge(e).range).distance<1e-8){selected=e;break;}
        require(selected.valid(),"bordo spaziale sorgente individuato");
        ExtrusionObject source;source.feature=BodyFeature::Imported;source.featureId=701;source.name="Superficie";
        source.forgeBody=sheet;source.importData=QByteArray::fromStdString(writeStep({{"Superficie",*sheet, false, {0.0, 0.0, 0.0}, nullptr, {}}}));
        GeometryRef edge;edge.kind=4;edge.index=0;edge.featureId=701;edge.point=edgeReference(*sheet,selected,spatial->point(.5));
        QVector<ExtrusionObject> bodies{source};QString error;QVector<double> cuts;
        CurveExtractionParameters p;p.source=edge;
        auto extracted=extractCurves3D(p,1,{},bodies,&error);
        require(error.isEmpty() && extracted.size()==1,"estrazione esatta bordo 3D");
        for(int i=0;i<=20;++i)require(distance(extracted[0]->point(i/20.),spatial->point(i/20.))<1e-8,"estrazione conserva forma e profondita'");
        const Vec3 middle=spatial->point(.5);
        SketchObject cutter;cutter.name="Segmento";cutter.customFrame=true;cutter.frame.origin[2]=middle.z()+8;
        cutter.segments={{{middle.x(),-8},{middle.x(),8}}};cutter.constraints={-1};cutter.segmentLengths={0};cutter.segmentAngles={-1};
        QVector<SketchObject> sketches{cutter};p.tool.kind=7;p.tool.index=0;p.tool.element={0,0,-1};p.trimmed=true;
        require(extractCurves3D(p,1,sketches,bodies,&error).isEmpty() && !error.isEmpty(),"intersezione reale rifiuta segmento a profondita' diversa");
        p.projected=true;
        extracted=extractCurves3D(p,1,sketches,bodies,&error,&cuts);
        require(error.isEmpty() && extracted.size()==1 && cuts.size()==1,"taglio proiettato senza intersezione reale");
        require(std::fabs(extracted[0]->domain().hi-.5)<1e-7 && distance(extracted[0]->point(.5),middle)<1e-7,"taglio proiettato conserva punto sulla curva 3D");
        p.keepStart=false;extracted=extractCurves3D(p,1,sketches,bodies,&error);
        require(error.isEmpty() && std::fabs(extracted[0]->domain().lo-.5)<1e-7,"parte finale della curva");
        sketches[0].frame.origin[2]=middle.z();p.projected=false;
        extracted=extractCurves3D(p,1,sketches,bodies,&error);
        require(error.isEmpty() && std::fabs(extracted[0]->domain().lo-.5)<1e-7,"intersezione reale su curva spaziale");
        p.projected=true;p.keepStart=true;
        ExtrusionObject definition;definition.feature=BodyFeature::ExtractedCurve;definition.featureId=702;definition.name="Curva tagliata";definition.curveExtraction=p;
        buildGeometry(definition,1,sketches,bodies);
        require(definition.error.isEmpty() && hasGeometry(definition) && isCurveBody(definition),"feature di curva estratta valida");
        require(bodyOperands(definition)==QVector<int>{0} && sketchesOf(definition)==QVector<int>{0},"dipendenze di sorgente e segmento");
        bodies.append(definition);
        GeometryRef extractedRef;extractedRef.kind=9;extractedRef.index=1;extractedRef.featureId=702;
        SketchObject end;end.customFrame=true;end.frame.origin[1]=10;end.frame.origin[2]=5;
        end.segments={{{0,0},{5,0}}};end.constraints={-1};end.segmentLengths={0};end.segmentAngles={-1};sketches.append(end);
        GeometryRef endRef;endRef.kind=7;endRef.index=1;endRef.element={-1,-1,-1};
        ExtrusionObject loft;loft.feature=BodyFeature::Loft;loft.loftSurface=true;loft.loftSections={extractedRef,endRef};
        auto result=forgeReferencedLoft(loft,2,sketches,bodies,&error);
        require(result && checkBody(*result).empty(),"curva estratta e tagliata usabile come sezione loft");
        ExtrusionObject ruled;ruled.feature=BodyFeature::Ruled;ruled.ruledFirst={extractedRef};endRef.element={0,0,-1};ruled.ruledSecond={endRef};
        buildGeometry(ruled,2,sketches,bodies);
        require(ruled.forgeBody && ruled.error.isEmpty(),"curva tagliata usabile per superficie tra curve");
        DocumentState state;state.sketches=sketches;state.extrusions=bodies;
        QTemporaryDir dir;const QString path=dir.filePath("extracted.prt");
        require(saveDocumentFile(path,state,false).isEmpty(),"salva definizione associativa formato 44");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty(),"riapre curva estratta");
        const auto &saved=loaded.extrusions[1].curveExtraction;
        require(saved.source.featureId==701 && saved.source.point.subshape==p.source.point.subshape && saved.tool.element.element==0
            && saved.projected && saved.trimmed && saved.keepStart && saved.view.normal[2]==p.view.normal[2],"persistenza riferimenti e vista");
        CadViewport view;view.tessellationQuality_=0;view.restoreDocument(loaded);view.regenerateAll();
        require(view.extrusions_[1].curve && view.extrusions_[1].error.isEmpty(),"rigenerazione dopo riapertura");
        require(view.withDependentBodies({0}).contains(1),"modifiche del bordo propagano alla curva");
        view.sketches_[0].segments[0]={{3,-8},{3,8}};view.regenerateAll();
        const double shorter=view.extrusions_[1].curve->domain().hi;
        require(shorter<.4 && shorter>.2,"taglio si aggiorna quando cambia il segmento");
        auto shiftedCurve=std::make_shared<BSplineCurve<3>>(3,std::vector<double>{0,0,0,0,1,1,1,1},
            std::vector<Vec3>{{0,0,12},{3,2,15},{7,-1,13},{10,0,14}});
        auto shifted=std::make_shared<const Body>(ruledSurface({{shiftedCurve,{0,1}}},{line({0,-5,10},{10,-5,10})}));
        view.extrusions_[0].forgeBody=shifted;view.extrusions_[0].importData=QByteArray::fromStdString(writeStep({{"Superficie",*shifted, false, {0.0, 0.0, 0.0}, nullptr, {}}}));
        view.regenerateAfter(0);
        require(view.extrusions_[1].curve && view.extrusions_[1].error.isEmpty()
            && distance(view.extrusions_[1].curve->point(shorter),shiftedCurve->point(shorter))<1e-6,"taglio segue bordo rigenerato senza perdere profondita'");
        auto bad=p;bad.source.featureId=99999;
        require(extractCurves3D(bad,1,sketches,bodies,&error).isEmpty(),"riferimento persistente mancante segnalato");
        bad=p;bad.tool.index=99;
        require(extractCurves3D(bad,1,sketches,bodies,&error).isEmpty(),"segmento mancante segnalato");
        bad=p;for(double &v:bad.view.normal)v=0;
        require(extractCurves3D(bad,1,sketches,bodies,&error).isEmpty(),"vista non valida segnalata");
        // Una curva con due incroci consente di scegliere l'intersezione.
        ExtrusionObject arc;arc.feature=BodyFeature::Helix;arc.curve=std::make_shared<BSplineCurve<3>>(2,
            std::vector<double>{0,0,0,1,1,1},std::vector<Vec3>{{0,0,2},{10,4,3},{0,8,4}});arc.curves={arc.curve};
        CurveExtractionParameters multi;multi.source.kind=9;multi.source.index=0;multi.trimmed=true;multi.projected=true;multi.tool=p.tool;multi.cutHint=.9;
        sketches[0].segments[0]={{3,-1},{3,9}};
        auto twice=extractCurves3D(multi,1,sketches,{arc},&error,&cuts);
        require(error.isEmpty() && cuts.size()==2 && twice[0]->domain().hi>.5,"scelta del secondo incrocio proiettato");
        multi.cutHint=.1;twice=extractCurves3D(multi,1,sketches,{arc},&error);
        require(error.isEmpty() && twice[0]->domain().hi<.5,"scelta del primo incrocio proiettato");
        arc.curve=std::make_shared<TrimmedCurve<3>>(std::make_shared<Line<3>>(Vec3(0,0,0),Vec3(1,0,0)),0,100);arc.curves={arc.curve};
        sketches[0].frame.origin[2]=0;sketches[0].segments[0]={{3,0},{4,0}};multi.projected=false;
        require(extractCurves3D(multi,1,sketches,{arc},&error).isEmpty() && error.contains("sovrappone"),"sovrapposizione corta interna rifiutata");
        multi.projected=true;
        require(extractCurves3D(multi,1,sketches,{arc},&error).isEmpty() && error.contains("sovrappongono"),"sovrapposizione proiettata rifiutata");
        CurveExtractionParameters joined;
        GeometryRef top=edge;
        QVector<GeometryRef> others;
        for(EdgeId id:sheet->edges())if(id!=selected){GeometryRef ref=top;ref.point=edgeReference(*sheet,id,sheet->edge(id).curve->point((sheet->edge(id).range.lo+sheet->edge(id).range.hi)/2));others.append(ref);}
        GeometryRef sideA,sideB,bottom;
        for(const auto &ref:others){const auto &e=sheet->edge(EdgeId(ref.point.subshape));const auto a=e.curve->point(e.range.lo),b=e.curve->point(e.range.hi);
            if(distance(a,spatial->point(0))<1e-7||distance(b,spatial->point(0))<1e-7)sideA=ref;
            else if(distance(a,spatial->point(1))<1e-7||distance(b,spatial->point(1))<1e-7)sideB=ref;
            else bottom=ref;
        }
        require(sideA.kind>=0&&sideB.kind>=0&&bottom.kind>=0,"three adjacent edges for unified extraction");
        joined.source=top;joined.sources={top,sideB,sideA};
        auto united=extractCurves3D(joined,1,{},bodies,&error);require(error.isEmpty()&&united.size()==1&&united[0]->type()==CurveType::BSpline,"adjacent unordered sources yield one NURBS");
        for(const auto &ref:joined.sources){const auto &e=sheet->edge(EdgeId(ref.point.subshape));for(int i=0;i<=24;++i)require(projectPoint(*united[0],e.curve->point(e.range.lo+e.range.length()*i/24.)).distance<1e-6,"unified curve preserves original edges exactly");}
        auto trimmedJoin=joined;trimmedJoin.trimmed=true;trimmedJoin.projected=true;trimmedJoin.tool=p.tool;trimmedJoin.view=p.view;
        auto partialJoin=extractCurves3D(trimmedJoin,1,{cutter},bodies,&error,&cuts);require(error.isEmpty()&&partialJoin.size()==1&&!cuts.isEmpty(),"unified chain supports trimming");
        auto badJoin=joined;badJoin.sources={top,bottom};require(extractCurves3D(badJoin,1,{},bodies,&error).isEmpty()&&!error.isEmpty(),"disconnected edges rejected");
        badJoin.sources={top,top};require(extractCurves3D(badJoin,1,{},bodies,&error).isEmpty()&&!error.isEmpty(),"duplicate edge rejected");
        DocumentState joinState;joinState.extrusions={source};ExtrusionObject unifiedFeature;unifiedFeature.feature=BodyFeature::ExtractedCurve;unifiedFeature.curveExtraction=joined;
        buildGeometry(unifiedFeature,1,{},joinState.extrusions);require(unifiedFeature.curve&&unifiedFeature.curves.size()==1,"unified extraction feature built");joinState.extrusions.append(unifiedFeature);
        QTemporaryDir joinDir;const QString joinFile=joinDir.filePath("joined.prt");require(saveDocumentFile(joinFile,joinState).isEmpty(),"save unified source chain");DocumentState joinLoaded;
        require(loadDocumentFile(joinFile,joinLoaded).isEmpty()&&joinLoaded.extrusions[1].curveExtraction.sources.size()==3,"multi-source references round trip");
        buildGeometry(joinLoaded.extrusions[1],1,{},joinLoaded.extrusions);require(joinLoaded.extrusions[1].curve&&joinLoaded.extrusions[1].error.isEmpty(),"multi-source curve regenerates after reopening");
        ExtrusionObject circular;circular.curve=std::make_shared<TrimmedCurve<3>>(std::make_shared<Circle<3>>(Vec3(),Vec3(1,0,0),Vec3(0,1,0),1),0,kPi/2);circular.curves={circular.curve};
        ExtrusionObject straightPiece;straightPiece.curve=std::make_shared<TrimmedCurve<3>>(std::make_shared<Line<3>>(Vec3(0,2,0),Vec3(0,-1,0)),0,1);straightPiece.curves={straightPiece.curve};
        CurveExtractionParameters rationalJoin;GeometryRef circleRef;circleRef.kind=9;circleRef.index=0;GeometryRef lineRef=circleRef;lineRef.index=1;rationalJoin.source=circleRef;rationalJoin.sources={lineRef,circleRef};
        auto rational=extractCurves3D(rationalJoin,2,{}, {circular,straightPiece},&error);require(error.isEmpty()&&rational.size()==1,"rational arc and reversed line unified");
        for(int i=0;i<=48;++i)require(projectPoint(*rational[0],circular.curve->point(kPi*i/96.)).distance<1e-7,"rational circle geometry preserved");
        ExtrusionObject branchPiece;branchPiece.curve=std::make_shared<TrimmedCurve<3>>(std::make_shared<Line<3>>(Vec3(0,1,0),Vec3(1,0,0)),0,1);branchPiece.curves={branchPiece.curve};
        auto branchJoin=rationalJoin;GeometryRef branchRef=circleRef;branchRef.index=2;branchJoin.sources.append(branchRef);
        require(extractCurves3D(branchJoin,3,{}, {circular,straightPiece,branchPiece},&error).isEmpty()&&error.contains("diramazione"),"branched source network rejected");

        CadViewport joinView;joinView.tessellationQuality_=0;joinView.extrusions_={source};QMainWindow joinWindow;std::exception_ptr joinFailure;ExtrusionObject joinSubmitted;
        QTimer::singleShot(0,&joinWindow,[&]{auto *dialog=joinWindow.findChild<QDialog *>("extractedCurveDialog");try{
            require(dialog,"multi-source dialog open");auto *box=dialog->findChild<QCheckBox *>("extractJoinBox");require(box,"join control available");box->setChecked(true);
            dialog->findChild<QPushButton *>("extractSourceButton")->click();
            auto callback=joinView.refPickFinished_;joinView.cancelReferencePick();callback(true,sideB);require(joinView.referencePicking(),"picker rearmed after source click");
            callback=joinView.refPickFinished_;joinView.cancelReferencePick();callback(true,sideA);
            require(dialog->findChild<QListWidget *>("extractSources")->count()==3,"three source edges visible");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        }catch(...){joinFailure=std::current_exception();if(dialog)dialog->reject();}});
        ExtrusionObject joinInitial;joinInitial.curveExtraction.source=top;
        require(extractedCurveDialog(&joinWindow,&joinView,"Unisci bordi",-1,joinInitial,[&](const ExtrusionObject &d){joinSubmitted=d;return joinView.createBody(d);}),"multi-source dialog applied");if(joinFailure)std::rethrow_exception(joinFailure);
        require(joinSubmitted.curveExtraction.sources.size()==3&&joinView.extrusions_.last().curves.size()==1&&!joinView.referencePicking(),"dialog creates one curve and clears picker");
        // Il dialogo sceglie sorgente e segmento, applica e ripulisce i callback.
        CadViewport dialogView;dialogView.tessellationQuality_=0;dialogView.sketches_=state.sketches;dialogView.extrusions_={source};dialogView.setViewNormal(0);
        QMainWindow window;std::exception_ptr failure;bool applied=false;
        QTimer::singleShot(0,&window,[&]{
            auto *dialog=window.findChild<QDialog*>("extractedCurveDialog");
            try{
                require(dialog,"dialogo estrazione aperto");
                dialog->findChild<QPushButton*>("extractSourceButton")->click();
                auto cb=dialogView.refPickFinished_;cb(true,edge);
                dialog->findChild<QPushButton*>("extractToolButton")->click();
                cb=dialogView.refPickFinished_;cb(true,p.tool);
                require(dialog->findChild<QComboBox*>("extractCutBox")->count()>=1,"incroci disponibili nel dialogo");
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            }catch(...){failure=std::current_exception();if(dialog)dialog->reject();}
        });
        ExtrusionObject empty;empty.curveExtraction.trimmed=true;empty.curveExtraction.projected=true;
        require(extractedCurveDialog(&window,&dialogView,"Estrai",-1,empty,[&](const ExtrusionObject &d){applied=true;return dialogView.createBody(d);}),"dialogo applicato");
        if(failure)std::rethrow_exception(failure);
        require(applied && !dialogView.referencePicking() && !dialogView.refPickFinished_ && dialogView.extrusions_.size()==2,"applicazione e pulizia dialogo");
        const auto preservedView=dialogView.extrusions_[1].curveExtraction.view;
        dialogView.setViewNormal(1);dialogView.regenerateAll();
        require(dialogView.extrusions_[1].curve && dialogView.extrusions_[1].curveExtraction.view.normal[2]==preservedView.normal[2],"rotazione della camera non modifica vista del taglio");
        dialogView.undo();require(dialogView.extrusions_.size()==1,"undo curva estratta");
        dialogView.redo();require(dialogView.extrusions_.size()==2 && dialogView.extrusions_[1].curve,"redo curva estratta");
        QTimer::singleShot(0,&window,[&]{
            auto *dialog=window.findChild<QDialog*>("extractedCurveDialog");
            if(dialog)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
        });
        require(!extractedCurveDialog(&window,&dialogView,"Annulla estrazione",-1,empty,[&](const ExtrusionObject &){return QString();}),"Annulla chiude dialogo estrazione");
        require(!dialogView.referencePicking() && !dialogView.refPickFinished_ && dialogView.extrusions_.size()==2,"annullamento ripulisce callback senza modificare storia");
    }

    static void handleConstraints() {
        using namespace ForgeCad;
        SketchObject original;
        CurveObject spline;
        spline.tool = DrawingTool::Spline;
        spline.controlPoints = {{0,0},{3,1},{6,0}};
        initializeTangentHandles(spline);
        spline.tangentLinked[1] = true;
        original.curves.append(spline);
        for (int k=0;k<3;++k)
            original.geometricConstraints.append(makeConstraint(original,ConstraintType::Fix,{{1,0,k}}));
        require(analyzeSketch(original).fullyDefined(),"maniglie non vincolate escluse dal conteggio");
        const ConstraintRef handle{1,0,handlePoint(1,1)};
        for (bool reverse : {false,true}) {
            auto sketch=original;
            const ConstraintRef axis{2,1,-1};
            const QVector<ConstraintRef> refs=reverse ? QVector<ConstraintRef>{axis,handle} : QVector<ConstraintRef>{handle,axis};
            require(applicableConstraints(sketch,refs).contains(ConstraintType::Angle),"quota angolare maniglia asse in entrambi gli ordini");
            auto c=makeConstraint(sketch,ConstraintType::Angle,refs);
            c.value=-35;
            sketch.geometricConstraints.append(c);
            require(solveSketch(sketch).ok,"angolo aggiunto a schizzo completamente definito");
            require(std::fabs(currentMeasure(sketch,c)+35)<1e-6,"misura angolare maniglia risolta");
            QPointF vertex,d,e;
            require(angleDirections(sketch,c,vertex,d,e),"geometria grafica quota maniglia");
            auto placed=c;
            require(chooseAngleSector(sketch,placed,vertex+d+e),"settore della quota maniglia");
            const auto &curve=sketch.curves[0];
            require(curve.controlPoints==spline.controlPoints,"quota conserva nodi fissi");
            const QPointF in=curve.tangentHandles[1].first-curve.controlPoints[1];
            const QPointF out=curve.tangentHandles[1].second-curve.controlPoints[1];
            require(std::fabs(in.x()*out.y()-in.y()*out.x())<1e-8 && QPointF::dotProduct(in,out)<0,"maniglia opposta ruota e conserva verso");
            require(pointDistance(in,{0,0})>0.1 && pointDistance(out,{0,0})>0.1,"maniglie non collassano");
            DocumentState state;state.sketches.append(sketch);
            QTemporaryDir dir;const QString path=dir.filePath("angle.prt");
            require(saveDocumentFile(path,state).isEmpty(),"salvataggio quota maniglia");
            DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty(),"riapertura quota maniglia");
            require(std::fabs(currentMeasure(loaded.sketches[0],loaded.sketches[0].geometricConstraints.last())+35)<1e-6,"quota maniglia persistente");
        }
        for (int reference=0;reference<3;++reference) {
            auto sketch=original;
            ConstraintRef ref;
            if(reference==0) {
                sketch.segments.append({{-1,-2},{5,4}});
                ref={0,0,-1};
            } else {
                CurveObject curve;
                curve.tool=reference==1 ? DrawingTool::Circle : DrawingTool::Converted;
                if(reference==1) curve.controlPoints={{3,-2},{6,-2}};
                else {curve.degree=1;curve.controlPoints={{0,0},{6,2}};curve.knots={0,0,1,1};}
                recalculateCurve(curve);
                sketch.curves.append(curve);ref={1,1,-1};
            }
            sketch.geometricConstraints.append(makeConstraint(sketch,ConstraintType::Fix,{ref}));
            require(applicableConstraints(sketch,{handle,ref}).contains(ConstraintType::Tangent),"tangenza maniglia con retta cerchio o curva derivata");
            auto tangent=makeConstraint(sketch,ConstraintType::Tangent,{ref,handle});
            sketch.geometricConstraints.append(tangent);
            require(solveSketch(sketch).ok && constraintError(sketch,tangent)<1e-7,"soluzione tangenza maniglia con riferimento fisso");
            require(sketch.curves[0].controlPoints==spline.controlPoints,"tangenza conserva nodi fissi");
        }
    }

    static void smoothZoom(bool render = false) {
        portableZoomTests();
        CadViewport v;
        v.wheelZoom_.setPreferences({});
        v.resize(900,650);
        v.setZoom(20);
        QPointingDevice trackpad(QStringLiteral("Zoom test trackpad"),1234,QInputDevice::DeviceType::TouchPad,
                                 QPointingDevice::PointerType::Finger,QInputDevice::Capability::Position|QInputDevice::Capability::PixelScroll,1,0);
        const auto wheel = [&](int angle, int pixel = 0, Qt::ScrollPhase phase = Qt::NoScrollPhase, bool mousePixels = false,
                               QPointF anchor = QPointF(450,325)) {
            const auto *device = pixel!=0 && !mousePixels ? &trackpad : QPointingDevice::primaryPointingDevice();
            QWheelEvent event(anchor, anchor, QPoint(0,pixel), QPoint(0,angle),
                              Qt::NoButton, Qt::NoModifier, phase, false, Qt::MouseEventNotSynthesized, device);
            v.wheelEvent(&event);
        };
        const auto close = [](double actual, double expected, const char *message) {
            require(std::abs(actual-expected) < 2e-6*std::max(1.0,std::abs(expected)),message);
        };
        const auto drain = [&] {
            for(int i=0;i<2000 && v.wheelZoom_.timer()->isActive();++i) v.advanceWheelZoom(0.016);
            require(!v.wheelZoom_.timer()->isActive() && v.wheelZoom_.pendingPoints()==0,"coda finita senza movimento residuo");
        };
        const double factor=std::exp(-0.03); // passo dimezzato: 20 punti, circa 2.96%
        wheel(120);
        const double duration=v.wheelZoom_.remaining();
        const double sampleTime=duration/10.0;
        const auto advanceInterval = [&](double total) {
            while(total>1e-10) {
                const double dt=std::min(total,0.016);
                v.advanceWheelZoom(dt);
                total-=dt;
            }
        };
        auto *timer=v.wheelZoom_.timer();
        require(timer && timer->isActive(),"rotella avvia filtro continuo");
        close(v.camera_.zoom_,20,"nessun salto sullo scatto");
        close(v.wheelZoom_.pendingPoints(),20,"120 unita Qt normalizzate a 20 punti");
        double previousLog=std::log(20.0);
        for(int i=0;i<5;++i) {
            advanceInterval(sampleTime);
            const double currentLog=std::log(double(v.camera_.zoom_));
            close(previousLog-currentLog,0.003,"stessa quota di zoom per frame, senza accelerazione o frenata");
            previousLog=currentLog;
        }
        close(v.wheelZoom_.pendingPoints(),10,"meta' movimento a meta' durata");
        advanceInterval(sampleTime*1.25);
        close(v.wheelZoom_.pendingPoints(),7.5,"avanzamento proporzionale al tempo");
        close(v.camera_.zoom_,20*std::exp(-0.01875),"scala al 62.5 percento della durata");
        const double pending=v.wheelZoom_.pendingPoints();
        wheel(120);
        close(v.wheelZoom_.pendingPoints(),pending+20,"scatti aggiunti al movimento rimasto");
        drain();
        close(v.camera_.zoom_,20*factor*factor,"due scatti conservano esattamente la quantita' richiesta");

        v.setZoom(20);wheel(120);
        previousLog=std::log(20.0);
        for(int i=0;i<10;++i) {
            advanceInterval(sampleTime);
            const double currentLog=std::log(double(v.camera_.zoom_));
            close(previousLog-currentLog,0.003,"velocita' uniforme anche all'ultimo frame");
            previousLog=currentLog;
        }
        drain();close(v.camera_.zoom_,20*factor,"durata e ampiezza del singolo scatto");
        const QPointF anchor(670,210);
        v.setZoom(20);
        QVector3D fixedPoint,rayDirection;
        v.viewRay(anchor.toPoint(),fixedPoint,rayDirection);
        require((v.projectWorldPoint(fixedPoint)-anchor).manhattanLength()<1e-3,"raggio iniziale al cursore");
        wheel(120,0,Qt::NoScrollPhase,false,anchor);
        for(int i=0;i<10;++i) {
            advanceInterval(sampleTime);
            require((v.projectWorldPoint(fixedPoint)-anchor).manhattanLength()<1e-3,"punto sotto cursore fermo a ogni frame");
        }
        drain();
        wheel(-120000,0,Qt::NoScrollPhase,false,anchor);drain();
        require((v.projectWorldPoint(fixedPoint)-anchor).manhattanLength()<0.02,"ancora corretta anche al limite effettivo");

        // La scala totale resta proporzionale all'input: nessuna soglia minima
        // per evento che amplifichi una rotella ad alta risoluzione.
        v.setZoom(20);
        for(int i=0;i<12;++i) wheel(10);
        drain();
        close(v.camera_.zoom_,20*factor,"dodici frazioni equivalgono a uno scatto");
        for(double dt:{1.0/30.0,1.0/60.0,1.0/144.0}) {
            v.setZoom(20);wheel(120);
            for(int i=0;i<2000 && timer->isActive();++i) v.advanceWheelZoom(dt);
            close(v.camera_.zoom_,20*factor,"scala finale indipendente dalla frequenza dei frame");
        }
        v.setZoom(20);wheel(120);v.advanceWheelZoom(0.016);
        const double reversedFrom=v.camera_.zoom_;
        wheel(-120);v.advanceWheelZoom(0.016);
        require(v.camera_.zoom_>reversedFrom,"inversione risponde senza smaltire vecchia coda");
        drain();close(v.camera_.zoom_,reversedFrom/factor,"inversione dalla scala visibile");

        float low=0,high=0;v.zoomLimits(low,high);
        v.setZoom(low*1.1f);wheel(120000);drain();
        close(v.camera_.zoom_,low,"limite minimo senza accumulo eccessivo");
        wheel(-120000);drain();close(v.camera_.zoom_,high,"limite massimo");
        wheel(-120);require(!timer->isActive(),"nessun timer al limite");

        v.setZoom(20);wheel(120);
        v.wheelZoom_.setPreferences({ForgeCad::ZoomInputMode::DirectTouchpad,1.,duration});
        wheel(120,3,Qt::ScrollUpdate);
        close(v.camera_.zoom_,20*std::exp(-0.0045),"trackpad segue pixel senza scatto angolare");
        require(!timer->isActive() && v.wheelZoom_.pendingPoints()==0,"trackpad elimina la coda della rotella");
        // Riproduzione dell'input fisico letto dal journal della prova utente:
        // dispositivo TouchPad, scatto angolare -120 e pixel -15.
        v.wheelZoom_.setPreferences({ForgeCad::ZoomInputMode::Automatic,1.,duration});
        const double realWheelStart=v.camera_.zoom_;
        wheel(-120,-15,Qt::NoScrollPhase);
        close(v.camera_.zoom_,realWheelStart,"rotella etichettata TouchPad non salta subito alla scala finale");
        require(timer->isActive(),"input reale dell'utente passa dal percorso animato");
        close(v.wheelZoom_.remaining(),duration,"anche la rotella TouchPad rispetta i secondi scelti");
        advanceInterval(duration/2);
        close(v.camera_.zoom_,realWheelStart*std::exp(0.015),"meta' zoom solo a meta' durata sull'input reale");
        drain();close(v.camera_.zoom_,realWheelStart/factor,"scala finale dello scatto reale");
        const double pixelZoom=v.camera_.zoom_;
        wheel(120,0,Qt::ScrollUpdate);
        close(v.camera_.zoom_,pixelZoom,"fase da sola non bypassa il filtro");
        drain();close(v.camera_.zoom_,pixelZoom*factor,"rotella con fase mantiene la normalizzazione");
        const double mousePixels=v.camera_.zoom_;
        wheel(120,20,Qt::ScrollUpdate,true);
        close(v.camera_.zoom_,mousePixels,"pixel forniti dal mouse non saltano il filtro");
        drain();close(v.camera_.zoom_,mousePixels*factor,"mouse usa angolo anche quando il driver fornisce pixel");
        wheel(0);require(!timer->isActive(),"delta nullo non avvia zoom");

        wheel(120);v.setWheelZoomEnabled(false);
        const double disabled=v.camera_.zoom_;wheel(120);
        close(v.camera_.zoom_,disabled,"ruota disabilitata non cambia vista");
        require(!timer->isActive() && v.wheelZoom_.pendingPoints()==0,"disabilitare elimina il residuo");
        v.setWheelZoomEnabled(true);wheel(120);v.zoomOut();
        require(!timer->isActive(),"comando diretto interrompe filtro");
        wheel(120);v.fitAll();const double fitted=v.camera_.zoom_;
        v.advanceWheelZoom(1);
        close(v.camera_.zoom_,fitted,"nessun callback superato sovrascrive Zoom tutto");
        wheel(120);v.setViewPreset(4);require(!timer->isActive(),"cambio vista ferma la ruota");
        wheel(120);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(450,325),QPointF(450,325),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        v.mousePressEvent(&press);require(!timer->isActive() && v.panning_,"pan interrompe il residuo");
        wheel(120);QFocusEvent focus(QEvent::FocusOut);v.focusOutEvent(&focus);
        require(!timer->isActive(),"perdita focus interrompe la ruota");
        if(render) {
            v.show();QEventLoop warmup;QTimer::singleShot(250,&warmup,&QEventLoop::quit);warmup.exec();
            require(v.context() && v.context()->isValid(),"contesto OpenGL reale disponibile");
        }
        v.setZoom(20);
        int frames=0;QVector<double> shown;
        const auto swapped=QObject::connect(&v,&QOpenGLWidget::frameSwapped,[&]{shown.append(v.camera_.zoom_);});
        const auto connection=QObject::connect(timer,&QTimer::timeout,[&]{++frames;});
        wheel(120);QElapsedTimer elapsed;elapsed.start();
        QEventLoop loop;QTimer::singleShot(int(duration*1000)+200,&loop,&QEventLoop::quit);loop.exec();
        QObject::disconnect(connection);QObject::disconnect(swapped);
        if(render) {
            int intermediate=0;double previous=20;
            for(double z:shown) {
                require(z<=previous+1e-5,"fotogrammi OpenGL monotoni");
                if(z<20-1e-4 && z>20*factor+1e-4)++intermediate;
                previous=z;
            }
            require(intermediate>=5,"almeno cinque fotogrammi OpenGL intermedi");
            v.makeCurrent();std::cout<<"OpenGL: "<<reinterpret_cast<const char *>(glGetString(GL_RENDERER))
                <<"; fotogrammi intermedi presentati: "<<intermediate<<std::endl;v.doneCurrent();
        }
        require(frames>=3 && !timer->isActive(),"timer reale produce passi intermedi e si ferma");
        close(v.camera_.zoom_,20*factor,"scala finale con timer reale");
        std::cout<<"PASS zoom uniforme al cursore: 120 Qt = 20 punti, fattore "<<factor
                 <<", "<<duration*1000<<"ms senza easing; osservazione "<<elapsed.elapsed()<<"ms; "<<frames<<" aggiornamenti Qt"<<std::endl;
    }

    static void run(bool render, bool orbitOnly = false) {
        extensionContourPicking();
        trimPartPicking();
        deletionDuringEdgePick();
        using namespace ForgeCad;
        CadViewport v;
        {
            ExpressionSpinBox value;
            value.setKeyboardTracking(false);
            int previews = 0;
            value.onReturn = [&] { ++previews; };
            value.findChild<QLineEdit *>()->setText(QStringLiteral("1/2"));
            require(previews == 0, "nessuna anteprima durante la digitazione");
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(&value, &enter);
            require(previews == 1 && value.value() == 0.5, "Invio conferma il valore e richiede una sola anteprima");
            value.clearFocus();
            require(previews == 1, "cambio di focus senza nuove anteprime");
            value.stepBy(1);
            require(previews == 2, "le frecce aggiornano l'anteprima");
            setDisplayLengthUnit(LengthUnit::Inch, &value);
            value.setRange(0.0, 1000.0);
            value.findChild<QLineEdit *>()->setText(QStringLiteral("2"));
            value.interpretText();
            require(std::fabs(value.value() - 50.8) < 1e-12 && value.suffix().trimmed() == QStringLiteral("in")
                        && formatLength(25.4, 6, true, &value) == QStringLiteral("1 in"),
                    "campi in pollici convertiti internamente in millimetri");
            value.setLengthMeasurement(false);
            value.setSuffix(QStringLiteral(" °"));
            setDisplayLengthUnit(LengthUnit::Foot, &value);
            require(value.suffix().trimmed() == QStringLiteral("°"), "gli angoli non cambiano con l'unita' lineare");
            setDisplayLengthUnit(LengthUnit::Millimeter, &value);
        }
        {
            // Filettatura parametrica: riconoscimento automatico della faccia
            // esterna e aggiunta del profilo elicoidale al cilindro.
            const Kernel::Body cylinder = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 5.0, 10.0);
            Kernel::FaceId side;
            for (Kernel::FaceId face : cylinder.faces())
                if (cylinder.face(face).surface->type() == Kernel::SurfaceType::Cylinder) { side = face; break; }
            require(side.valid(), "faccia cilindrica per il filetto");
            const Kernel::FinId fin = cylinder.loop(cylinder.face(side).loops.front()).first;
            ThreadParameters parameters;
            parameters.standard = 0;
            parameters.designation = QStringLiteral("M10 x 1,5");
            parameters.pitch = 1.5;
            parameters.length = 6.0;
            parameters.face = faceReference(cylinder, side, cylinder.finPoint(fin, 0.5));
            ThreadFaceInfo info;
            QString error;
            require(forgeThreadFaceInfo(cylinder, parameters.face, info, &error) && !info.internal
                        && std::fabs(info.diameter - 10.0) < 1e-9 && std::fabs(info.length - 10.0) < 1e-9,
                    "misure automatiche della faccia da filettare");
            const ForgeBody base = std::make_shared<const Kernel::Body>(cylinder);
            const ForgeBody threaded = forgeThread(base, parameters, &error);
            require(threaded && error.isEmpty() && Kernel::massProperties(*threaded).volume > Kernel::massProperties(cylinder).volume,
                    "filettatura geometrica esterna");
            int sweptFaces = 0;
            for (Kernel::FaceId face : threaded->faces())
                sweptFaces += threaded->face(face).surface->type() == Kernel::SurfaceType::BSpline;
            require(sweptFaces > 0, "filettatura esterna con superfici sweep B-spline, non facce poliedriche");
            const Kernel::Body bore = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(0, 0, -1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 12.0);
            const Kernel::Body ring = Kernel::booleanOperation(cylinder, bore, Kernel::BooleanOperation::Subtract);
            Kernel::FaceId inside;
            for (Kernel::FaceId face : ring.faces()) {
                if (ring.face(face).surface->type() != Kernel::SurfaceType::Cylinder) continue;
                const auto &surface = static_cast<const Kernel::CylindricalSurface &>(*ring.face(face).surface);
                if (std::fabs(surface.radius() - 3.0) < 1e-9) { inside = face; break; }
            }
            require(inside.valid(), "faccia interna per il filetto");
            parameters.pitch = 1.0;
            parameters.length = 2.0;
            const Kernel::FinId insideFin = ring.loop(ring.face(inside).loops.front()).first;
            parameters.face = faceReference(ring, inside, ring.finPoint(insideFin, 0.5));
            require(forgeThreadFaceInfo(ring, parameters.face, info, &error) && info.internal,
                    "riconoscimento automatico del foro interno");
            const ForgeBody threadedHole = forgeThread(std::make_shared<const Kernel::Body>(ring), parameters, &error);
            require(threadedHole && error.isEmpty() && Kernel::massProperties(*threadedHole).volume < Kernel::massProperties(ring).volume,
                    "filettatura geometrica interna");
            DocumentState state;
            ExtrusionObject root;
            root.name = QStringLiteral("Cilindro");
            root.feature = BodyFeature::Primitive;
            root.primitive.kind = PrimitiveKind::Cylinder;
            root.primitive.size[0] = 5.0;
            root.primitive.size[1] = 10.0;
            ExtrusionObject feature;
            feature.name = QStringLiteral("Filettatura 1");
            feature.feature = BodyFeature::Thread;
            feature.firstBody = 0;
            feature.thread = parameters;
            state.extrusions = {root, feature};
            QTemporaryDir dir;
            const QString path = dir.filePath(QStringLiteral("filetto.prt"));
            require(saveDocumentFile(path, state, false).isEmpty(), "salvataggio della filettatura");
            DocumentState loaded;
            require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions.size() == 2
                        && loaded.extrusions.at(1).feature == BodyFeature::Thread
                        && loaded.extrusions.at(1).thread.designation == parameters.designation
                        && std::fabs(loaded.extrusions.at(1).thread.pitch - 1.0) < 1e-12,
                    "lettura della filettatura dal documento");
        }
        {
            CadViewport selection;
            int picks = 0;
            selection.selectionCallback_ = [&](const SceneSelection &) { ++picks; };
            const QPointF a(20, 20), b(100, 100);
            QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, b, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            selection.mousePressEvent(&press);
            require(picks == 0, "selezione differita al rilascio");
            selection.mouseMoveEvent(&move);
            selection.mouseReleaseEvent(&release);
            require(picks == 0, "rotazione senza selezione");
            selection.mousePressEvent(&press);
            QMouseEvent clickRelease(QEvent::MouseButtonRelease, a, a, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            selection.mouseReleaseEvent(&clickRelease);
            require(picks == 1, "clic seleziona una sola volta");
        }
        {
            // Misura: aggancio al punto medio di uno spigolo, risolto sulla curva esatta.
            CadViewport measure;
            measure.resize(800, 600);
            PrimitiveParameters box;
            box.kind = PrimitiveKind::Box;
            box.size[0] = 4.0;
            box.size[1] = 3.0;
            box.size[2] = 2.0;
            require(measure.createPrimitive(box, QStringLiteral("Blocco misura")).isEmpty(), "corpo da misurare");
            measure.fitAll();
            // Il test aggiunge un secondo corpo piu' avanti: il vettore puo'
            // riallocarsi o fare detach, quindi la misura conserva una copia.
            const ExtrusionObject body = measure.extrusions_.last();
            const Kernel::EdgeId edge = body.forgeBody->edges().front();
            const Kernel::Vec3 middle = edgeMidpoint(*body.forgeBody, edge);
            measure.refPickOwner_ = -1;
            measure.refPickRoles_ = DatumRolePoint | DatumRoleCurve | DatumRoleFace | kMeasureSnapRole;
            GeometryRef picked;
            require(measure.pickReference(measure.projectWorldPoint(QVector3D(middle.x(), middle.y(), middle.z())).toPoint(), picked)
                        && picked.kind == kGeometryRefEdgeMidpoint,
                    "aggancio al punto medio dello spigolo");
            MeasureEntity entity;
            require(measureEntity(picked, measure.sketches_, measure.extrusions_, entity, nullptr) && entity.kind == MeasureEntity::Kind::Point,
                    "punto medio misurabile");
            {
                // Scelta del corpo dalla faccia cliccata (finestre con l'elenco dei corpi).
                require(measure.createPrimitive(box, QStringLiteral("Secondo blocco")).isEmpty(), "secondo corpo");
                QDialog dialog;
                auto *combo = new QComboBox(&dialog);
                const QVector<int> candidates = measure.resultBodiesBefore(-1);
                for (int index : candidates) combo->addItem(QString::number(index));
                combo->setCurrentIndex(int(candidates.size()) - 1);
                BodyPicker picker(dialog, &measure, -1, candidates, combo);
                int changes = 0;
                picker.changed = [&] { ++changes; };
                picker.start(true);
                require(picker.active() && picker.waiting(), "scelta del corpo in attesa");
                GeometryRef face;
                face.kind = 5;
                face.index = candidates.first();
                require(picker.handle(true, face) && combo->currentIndex() == 0 && !picker.active() && !picker.waiting() && changes == 1,
                        "la faccia cliccata sceglie il corpo");
                require(!picker.handle(true, face), "clic fuori dalla scelta del corpo");
            }
            const Kernel::Edge &exact = body.forgeBody->edge(Kernel::EdgeId(picked.point.subshape));
            const Kernel::Vec3 expected = 0.5 * (exact.curve->point(exact.range.lo) + exact.curve->point(exact.range.hi));
            require(distance(entity.point, expected) < 1e-12, "punto medio esatto");
        }
        topologyLabels();
        {
            // Estrusione simmetrica e nei due versi: un solo prisma dal piano
            // spostato (volumi e quote esatti); con la fine "fino a" il secondo
            // verso si unisce al primo.
            SketchObject square;
            square.plane = 0;
            const QPointF c[4] = {QPointF(5, 5), QPointF(6, 5), QPointF(6, 6), QPointF(5, 6)};
            for (int k = 0; k < 4; ++k) square.segments.append({c[k], c[(k + 1) % 4]});
            const QVector<SketchObject> sketches{square};
            const auto zRange = [](const Kernel::Body &body, double &lo, double &hi) {
                lo = 1e300, hi = -1e300;
                for (Kernel::VertexId v : body.vertices()) lo = std::min(lo, body.vertex(v).point[2]), hi = std::max(hi, body.vertex(v).point[2]);
            };
            const auto build = [&](ExtrusionObject e, const QVector<ExtrusionObject> &bodies, double volume, double lo, double hi, const char *what) {
                e.feature = BodyFeature::Extrusion;
                e.sketchIndex = 0;
                QString error;
                const ForgeBody body = forgeExtrusionFeature(e, int(bodies.size()), sketches, bodies, &error);
                require(body && error.isEmpty(), what);
                double zlo, zhi;
                zRange(*body, zlo, zhi);
                require(std::fabs(Kernel::massProperties(*body).volume - volume) < 1e-9 && std::fabs(zlo - lo) < 1e-12
                            && std::fabs(zhi - hi) < 1e-12 && body->shells().size() == 1, what);
            };
            ExtrusionObject e;
            e.distance = 2.0;
            e.extrudeSides = 1;
            build(e, {}, 2.0, -1.0, 1.0, "estrusione simmetrica");
            e.extrudeSides = 2;
            e.distance2 = 0.5;
            build(e, {}, 2.5, -0.5, 2.0, "estrusione nei due versi");
            e.distance = -2.0;
            build(e, {}, 2.5, -2.0, 0.5, "estrusione nei due versi, prima distanza negativa");
            ExtrusionObject block;
            block.forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(
                Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 1.0, 1.0, 3.0));
            block.solid = true;
            e.distance = 1.0;
            e.extent = 1;
            e.extentRef.kind = 3;
            e.extentRef.index = 0;
            e.extentRef.point = {1.0, 1.0, 3.0};
            build(e, {block}, 3.5, -0.5, 3.0, "fino a un vertice e secondo verso");
            e.extrudeSides = 1;
            QString error;
            e.feature = BodyFeature::Extrusion;
            require(!forgeExtrusionFeature(e, 1, sketches, {block}, &error) && !error.isEmpty(), "simmetrica solo con la distanza");
            // Formato 21: versi e seconda distanza nel documento.
            DocumentState state;
            state.sketches = sketches;
            ExtrusionObject saved;
            saved.name = QStringLiteral("Due versi");
            saved.feature = BodyFeature::Extrusion;
            saved.sketchIndex = 0;
            saved.extrudeSides = 2;
            saved.distance2 = 0.75;
            state.extrusions = {saved};
            QTemporaryDir sidesDir;
            const QString sidesPath = sidesDir.filePath(QStringLiteral("versi.prt"));
            require(saveDocumentFile(sidesPath, state, false).isEmpty(), "salvataggio dei versi");
            DocumentState sidesLoaded;
            require(loadDocumentFile(sidesPath, sidesLoaded).isEmpty() && sidesLoaded.extrusions.size() == 1
                        && sidesLoaded.extrusions.at(0).extrudeSides == 2 && sidesLoaded.extrusions.at(0).distance2 == 0.75,
                    "lettura dei versi dal documento");
        }
        {
            // La parte cliccata appartiene alla funzione che l'ha creata: la
            // faccia di sopra, accorciata dallo smusso, resta della base; la
            // faccia dello smusso e' dello smusso.
            const Kernel::Body block = Kernel::makeBox(Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 4.0, 3.0, 2.0);
            const Kernel::Body chamfered = Kernel::blendEdges(block, {Kernel::nearestEdge(block, Kernel::Vec3(4, 1.5, 2), 1e-6)}, 0.5, true);
            const std::vector<std::pair<int, ForgeBody>> chain{{0, std::make_shared<const Kernel::Body>(block)},
                                                               {1, std::make_shared<const Kernel::Body>(chamfered)}};
            const auto faceAt = [&](const Kernel::Vec3 &p) {
                for (Kernel::FaceId f : chamfered.faces())
                    if (Kernel::projectPoint(*chamfered.face(f).surface, p).distance < 1e-9
                        && Kernel::classifyPointOnFace(chamfered, f, p, 1e-9) == Kernel::PointLocation::Inside)
                        return f.index;
                return -1;
            };
            const Kernel::Vec3 top(1, 1, 2), bevel(3.75, 1.5, 1.75);
            require(faceAt(top) >= 0 && faceAt(bevel) >= 0, "facce del corpo smussato");
            require(forgeFaceOwner(chamfered, faceAt(top), top, chain) == 0, "faccia della base: funzione base");
            require(forgeFaceOwner(chamfered, faceAt(bevel), bevel, chain) == 1, "faccia dello smusso: lo smusso");
            // Evidenziazione della feature scelta nell'albero: solo la faccia dello smusso.
            BodyDisplay display;
            forgeTessellate(chamfered, 1, display);
            require(display.triangleFaces.size() * 3 == display.vertices.size(), "faccia di ogni triangolo della tassellazione");
            const QVector<int> bevelFaces = forgeFeatureFaces(chamfered, display.faceIds, display.faceLabelPoints, 1, chain);
            require(bevelFaces == QVector<int>{faceAt(bevel)}, "facce create dallo smusso");
            const QVector<int> baseFaces = forgeFeatureFaces(chamfered, display.faceIds, display.faceLabelPoints, 0, chain);
            require(baseFaces.size() == 6 && !baseFaces.contains(faceAt(bevel)), "facce della base dopo lo smusso");
        }
        {
            // Modifica di una funzione intermedia: la storia torna a quel punto.
            // Le funzioni successive spariscono, il corpo che solo loro
            // consumavano (lo strumento della differenza) torna visibile.
            CadViewport rollback;
            PrimitiveParameters base;
            base.size[0] = 4.0; base.size[1] = 3.0; base.size[2] = 2.0;
            PrimitiveParameters tool = base;
            tool.origin[0] = 3.0;
            require(rollback.createPrimitive(base, QStringLiteral("Base")).isEmpty()
                        && rollback.createPrimitive(tool, QStringLiteral("Utensile")).isEmpty()
                        && rollback.createBoolean(::BooleanOperation::Difference, 0, {1}, QStringLiteral("Differenza")).isEmpty(),
                    "storia per il ritorno indietro");
            rollback.requestPreview(rollback.extrusions_.at(0), 0);
            require(rollback.preview_.later == QSet<int>{2} && rollback.preview_.restored == QSet<int>{1},
                    "modifica della base: differenza nascosta, utensile di nuovo visibile");
            rollback.requestPreview(rollback.extrusions_.at(2), 2);
            require(rollback.preview_.later.isEmpty() && rollback.preview_.restored.isEmpty(), "modifica dell'ultima funzione");
            rollback.clearPreview();
            require(rollback.preview_.later.isEmpty(), "chiusura dell'anteprima");
        }
        // La fusione parametrica e' condivisa da estrusione e sweep: il probe
        // conserva il bersaglio realmente intersecato e produce un solo solido.
        ExtrusionObject mergeTarget;
        mergeTarget.forgeBody = std::make_shared<const Kernel::Body>(Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 3.0, 3.0));
        mergeTarget.solid = true;
        ExtrusionObject sweepMerge;
        sweepMerge.feature = BodyFeature::Sweep;
        sweepMerge.mergeOperation = 1;
        sweepMerge.mergeProbe = true;
        sweepMerge.mergeBodies = {0};
        const ForgeBody sweepTool = std::make_shared<const Kernel::Body>(Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(2, 1, 1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 2.0, 1.0, 1.0));
        QString sweepMergeError;
        const ForgeBody sweepUnited = forgeMergeFeatureResult(sweepMerge, sweepTool, 1, {mergeTarget}, &sweepMergeError);
        require(sweepUnited && sweepMergeError.isEmpty() && sweepMerge.mergeBodies == QVector<int>{0}
                    && sweepUnited->shells().size() == 1,
                "unione automatica del risultato sweep");
        require(ForgeCad::hiddenOperands(sweepMerge).contains(0), "bersaglio della sweep unita nascosto");
        CadViewport storyboard;
        PrimitiveParameters box;
        box.size[0] = 4.0;
        box.size[1] = 3.0;
        box.size[2] = 2.0;
        require(storyboard.createPrimitive(box, QStringLiteral("Blocco")).isEmpty(), "radice della storyboard");
        require(storyboard.modelBodies().size() == 1 && storyboard.extrusions().at(0).featureId != 0
                    && storyboard.extrusions().at(0).modelBodyId == storyboard.modelBodies().at(0).id,
                "identita' persistenti di corpo e feature");
        require(storyboard.createScale(0, 1.1, 0, {}, QStringLiteral("Scala 1")).isEmpty()
                    && storyboard.createScale(1, 1.1, 0, {}, QStringLiteral("Scala 2")).isEmpty(),
                "catena di feature nello stesso corpo logico");
        require(storyboard.modelBodies().size() == 1 && !storyboard.extrusions().at(0).visible
                    && !storyboard.extrusions().at(1).visible && storyboard.extrusions().at(2).visible,
                "solo il tip della storyboard e' visibile");
        {
            ExtrusionObject failed = storyboard.extrusions_.at(2);
            failed.featureId = 0;
            failed.forgeBody.reset();
            failed.display = {};
            failed.error = QStringLiteral("errore di rigenerazione simulato");
            storyboard.extrusions_.append(failed);
            ForgeCad::normalizeModelHistory(storyboard.extrusions_, storyboard.modelBodies_);
            require(storyboard.extrusions().at(2).visible && !storyboard.extrusions().at(3).visible
                        && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(2).featureId,
                    "una feature fallita lascia visibile l'ultimo stadio valido");
            // Corretta (riattivata o con un raggio che riesce): torna il tip
            // visibile anche se il suo flag era spento mentre falliva.
            storyboard.extrusions_[3].error.clear();
            storyboard.extrusions_[3].forgeBody = storyboard.extrusions_.at(2).forgeBody;
            ForgeCad::normalizeModelHistory(storyboard.extrusions_, storyboard.modelBodies_);
            require(!storyboard.extrusions().at(2).visible && storyboard.extrusions().at(3).visible
                        && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(3).featureId,
                    "una feature fallita che torna valida diventa il tip visibile");
            storyboard.extrusions_.removeLast();
            ForgeCad::normalizeModelHistory(storyboard.extrusions_, storyboard.modelBodies_);
        }
        const quint64 lastFeature = storyboard.extrusions().at(2).featureId;
        require(storyboard.moveFeature(2, -1).isEmpty() && storyboard.extrusions().at(1).featureId == lastFeature,
                "riordino di due feature compatibili");
        require(storyboard.setFeatureSuppressed(2, true).isEmpty() && storyboard.extrusions().at(2).suppressed
                    && storyboard.extrusions().at(1).visible,
                "soppressione ripristina lo stadio precedente");
        require(storyboard.setFeatureSuppressed(2, false).isEmpty() && storyboard.extrusions().at(2).visible,
                "riattivazione della feature");
        storyboard.deleteFeature(2);
        require(storyboard.extrusions().size() == 2 && storyboard.extrusions().at(1).visible
                    && storyboard.modelBodies().at(0).tipFeatureId == storyboard.extrusions().at(1).featureId,
                "eliminazione del tip ripristina la feature precedente");
        storyboard.undo();
        require(storyboard.extrusions().size() == 3 && storyboard.extrusions().at(2).visible,
                "undo dell'eliminazione nella storyboard");
        {
            // Riferimenti che sopravvivono al riordino. Una faccia si ritrova dalla
            // sua superficie anche quando altre feature ne cambiano i bordi; uno
            // spigolo dalla sua geometria anche quando il suo ID, in un altro stato
            // del corpo, e' un altro spigolo dello stesso tipo.
            CadViewport reorder;
            PrimitiveParameters block;
            block.size[0] = 10.0;
            block.size[1] = 8.0;
            block.size[2] = 6.0;
            require(reorder.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco per il riordino dei raccordi");
            const ForgeBody boxBody = reorder.extrusions().at(0).forgeBody;
            const Kernel::EdgeId vertical = Kernel::nearestEdge(*boxBody, Kernel::Vec3(10, 0, 3), 1e-6);
            const Kernel::EdgeId otherVertical = Kernel::nearestEdge(*boxBody, Kernel::Vec3(0, 0, 3), 1e-6);
            require(vertical.valid() && otherVertical.valid(), "spigoli verticali del blocco");
            EdgePoint misleading = edgeReference(*boxBody, vertical, Kernel::Vec3(10, 0, 3));
            misleading.subshape = otherVertical.index;  // stesso tipo e contesto, altro spigolo
            // Senza nome topologico (documenti fino al formato 48): qui si
            // verifica la scelta tra ID e punto. Con il nome, che non dipende
            // dagli indici, decide il nome (test --genealogy).
            misleading.name.clear();
            require(ForgeCad::resolveEdgeReference(*boxBody, misleading, 1e-3, ForgeCad::ReferenceState::Other) == vertical,
                    "dopo un riordino un ID che non passa per il punto non vale");
            require(ForgeCad::resolveEdgeReference(*boxBody, misleading, 1e-3, ForgeCad::ReferenceState::Same) == otherVertical,
                    "sulla stessa base l'ID vale piu' del punto");
            require(reorder.createBlend(0, {edgeReference(*boxBody, vertical, Kernel::Vec3(10, 0, 3))}, 0.5, false, QStringLiteral("R1")).isEmpty(),
                    "raccordo dello spigolo verticale");
            const ForgeBody filleted = reorder.extrusions().at(1).forgeBody;
            const auto planeAt = [](const Kernel::Body &body, int axis, double value) {
                for (Kernel::FaceId f : body.faces()) {
                    const auto *plane = dynamic_cast<const Kernel::Plane *>(body.face(f).surface.get());
                    if (plane && std::fabs(plane->frame().origin()[axis] - value) < 1e-9 && std::fabs(std::fabs(plane->frame().zDir()[axis]) - 1.0) < 1e-12)
                        return f;
                }
                return Kernel::FaceId();
            };
            const Kernel::FaceId top = planeAt(*filleted, 2, 6.0);
            require(top.valid() && ForgeCad::faceBoundaryEdges(*filleted, top).size() == 5, "faccia superiore con l'arco del raccordo");
            EdgePoint topFace = faceReference(*filleted, top, Kernel::Vec3(5, 4, 6));
            topFace.role = kEdgePointFaceBoundary;
            std::vector<Kernel::EdgeId> onBox;
            require(ForgeCad::resolveBlendEdges(*boxBody, {topFace}, 1e-3, onBox, ForgeCad::ReferenceState::Other) && onBox.size() == 4,
                    "la faccia si ritrova sul blocco con i suoi 4 bordi");
            // R2: i bordi della faccia x = 0 (non toccano R1), poi R2 prima di R1.
            const Kernel::FaceId left = planeAt(*filleted, 0, 0.0);
            EdgePoint leftFace = faceReference(*filleted, left, Kernel::Vec3(0, 4, 3));
            leftFace.role = kEdgePointFaceBoundary;
            require(left.valid() && reorder.createBlend(1, {leftFace}, 1.0, false, QStringLiteral("R2")).isEmpty(), "raccordo dei bordi della faccia x = 0");
            const double inOrder = Kernel::massProperties(*reorder.extrusions().at(2).forgeBody).volume;
            require(reorder.moveFeature(2, -1).isEmpty(), "R2 spostato prima di R1");
            const ExtrusionObject &first = reorder.extrusions().at(1), &second = reorder.extrusions().at(2);
            require(first.name == QStringLiteral("R2") && first.error.isEmpty() && first.forgeBody, "R2 sul blocco dopo il riordino");
            require(second.name == QStringLiteral("R1") && second.error.isEmpty() && second.forgeBody, "R1 dopo R2");
            const double removed = Kernel::massProperties(*first.forgeBody).volume - Kernel::massProperties(*second.forgeBody).volume;
            require(std::fabs(removed - 0.25 * (1.0 - M_PI / 4.0) * 6.0) < 1e-8, "R1 raccorda lo spigolo verticale giusto");
            require(std::fabs(Kernel::massProperties(*second.forgeBody).volume - inOrder) < 1e-9 * inOrder, "stesso risultato nei due ordini");
        }
        {
            // Storia unica: una feature si sposta anche tra feature di altri
            // corpi, mai prima della feature che crea il suo corpo.
            CadViewport history;
            require(history.createPrimitive(box, QStringLiteral("A")).isEmpty() && history.createPrimitive(box, QStringLiteral("B")).isEmpty()
                        && history.createScale(0, 1.1, 0, {}, QStringLiteral("Scala A")).isEmpty(),
                    "due corpi e una feature del primo");
            const quint64 scaleId = history.extrusions().at(2).featureId;
            require(history.moveFeature(2, -1).isEmpty() && history.extrusions().at(1).featureId == scaleId
                        && history.extrusions().at(1).firstBody == 0 && history.extrusions().at(1).visible,
                    "feature spostata prima della feature di un altro corpo");
            require(!history.moveFeature(1, -1).isEmpty() && history.extrusions().at(1).featureId == scaleId,
                    "una feature non va prima della feature che crea il suo corpo");
        }
        // Il proprietario viene ritrovato dall'ID della feature e la
        // sotto-entita' dal suo ID B-rep, anche se indice e punto-cache sono
        // volutamente fuorvianti.
        const int referencedFeature = 1;
        const ForgeBody referencedBody = storyboard.extrusions().at(referencedFeature).forgeBody;
        require(referencedBody && referencedBody->edges().size() > 1, "corpo per i riferimenti topologici");
        const Kernel::EdgeId persistentEdge = referencedBody->edges().front();
        const Kernel::Edge &edgeGeometry = referencedBody->edge(persistentEdge);
        const Kernel::Vec3 edgePoint = edgeGeometry.curve->point(0.5 * (edgeGeometry.range.lo + edgeGeometry.range.hi));
        EdgePoint persistentPoint = edgeReference(*referencedBody, persistentEdge, edgePoint);
        const Kernel::Edge &otherGeometry = referencedBody->edge(referencedBody->edges().back());
        const Kernel::Vec3 wrongPoint = otherGeometry.curve->point(0.5 * (otherGeometry.range.lo + otherGeometry.range.hi));
        persistentPoint.x = wrongPoint.x(); persistentPoint.y = wrongPoint.y(); persistentPoint.z = wrongPoint.z();
        require(resolveEdgeReference(*referencedBody, persistentPoint, 1e-12) == persistentEdge,
                "ID topologico preferito al punto piu' vicino");
        EdgePoint renumberedPoint = edgeReference(*referencedBody, persistentEdge, edgePoint);
        renumberedPoint.subshape = std::numeric_limits<int>::max();
        require(resolveEdgeReference(*referencedBody, renumberedPoint, 1e-12) == persistentEdge,
                "firma geometrico-topologica recupera uno spigolo rinumerato");
        const Kernel::FaceId persistentFace = referencedBody->faces().front();
        const EdgePoint facePoint = faceReference(*referencedBody, persistentFace, edgePoint);
        require(resolveFaceReference(*referencedBody, facePoint, 1e-12) == persistentFace,
                "riferimento persistente a una faccia");
        GeometryRef persistentRef;
        persistentRef.kind = 4;
        persistentRef.index = 0; // indice volutamente errato
        persistentRef.featureId = storyboard.extrusions().at(referencedFeature).featureId;
        persistentRef.point = persistentPoint;
        ResolvedRef resolvedPersistent;
        require(resolveGeometryRef(persistentRef, storyboard.extrusions().size(), {}, storyboard.extrusions(), resolvedPersistent, nullptr)
                    && resolvedPersistent.hasCurve,
                "proprietario del riferimento ritrovato dall'ID della feature");
        GeometryRef deletedOwner = persistentRef;
        deletedOwner.featureId = std::numeric_limits<quint64>::max();
        require(!resolveGeometryRef(deletedOwner, storyboard.extrusions().size(), {}, storyboard.extrusions(), resolvedPersistent, nullptr),
                "un proprietario eliminato non si lega per errore all'indice riutilizzato");
        GeometryRef legacyRef = persistentRef;
        legacyRef.point = {edgePoint.x(), edgePoint.y(), edgePoint.z()};
        storyboard.extrusions_[referencedFeature].extentRef = legacyRef;
        storyboard.extrusions_[referencedFeature].blendEdges = {persistentPoint};
        storyboard.setModelBodyMeshColor(0, QColor(184, 72, 116));
        QTemporaryDir storyboardDir;
        const QString storyboardPath = storyboardDir.filePath(QStringLiteral("storyboard.prt"));
        require(storyboardDir.isValid() && saveDocumentFile(storyboardPath, storyboard.currentDocument(), false).isEmpty(),
                "salvataggio della storyboard");
        DocumentState storyboardLoaded;
        require(loadDocumentFile(storyboardPath, storyboardLoaded).isEmpty() && storyboardLoaded.modelBodies.size() == 1
                    && storyboardLoaded.extrusions.at(1).featureId == lastFeature
                    && storyboardLoaded.modelBodies.first().meshColor == QColor(184, 72, 116),
                "persistenza e lettura della storyboard");
        int loadedVisible = 0;
        quint64 loadedVisibleId = 0;
        for (const ExtrusionObject &feature : storyboardLoaded.extrusions)
            if (feature.visible) ++loadedVisible, loadedVisibleId = feature.featureId;
        require(loadedVisible == 1 && loadedVisibleId == storyboardLoaded.modelBodies.first().tipFeatureId,
                "l'apertura senza cache conserva visibile il tip serializzato");
        require(storyboardLoaded.extrusions.at(referencedFeature).extentRef.featureId == persistentRef.featureId
                    && storyboardLoaded.extrusions.at(referencedFeature).extentRef.point.subshape == persistentEdge.index
                    && storyboardLoaded.extrusions.at(referencedFeature).blendEdges.first().context != -1,
                "migrazione e persistenza dei riferimenti topologici nel formato 20");
        CadViewport booleanStory;
        PrimitiveParameters firstBox, secondBox;
        firstBox.size[0] = firstBox.size[1] = firstBox.size[2] = 2.0;
        secondBox = firstBox;
        secondBox.origin[0] = 1.0;
        require(booleanStory.createPrimitive(firstBox, QStringLiteral("A")).isEmpty()
                    && booleanStory.createPrimitive(secondBox, QStringLiteral("B")).isEmpty()
                    && booleanStory.createBoolean(::BooleanOperation::Union, 0, {1}, QStringLiteral("Unione")).isEmpty(),
                "booleana nella storyboard");
        require(booleanStory.modelBodies().size() == 2 && booleanStory.extrusions().at(2).modelBodyId == booleanStory.extrusions().at(0).modelBodyId
                    && !booleanStory.extrusions().at(1).visible && booleanStory.extrusions().at(2).visible,
                "la booleana appartiene al corpo A e consuma il corpo strumento");
        require(booleanStory.setFeatureSuppressed(2, true).isEmpty() && booleanStory.extrusions().at(0).visible
                    && booleanStory.extrusions().at(1).visible,
                "sopprimere la booleana ripristina entrambi gli operandi");
        require(booleanStory.setFeatureSuppressed(2, false).isEmpty() && !booleanStory.extrusions().at(0).visible
                    && !booleanStory.extrusions().at(1).visible && booleanStory.extrusions().at(2).visible,
                "riattivare la booleana nasconde di nuovo gli operandi");
        CadViewport interleavedStory;
        require(interleavedStory.createPrimitive(firstBox, QStringLiteral("Corpo A")).isEmpty()
                    && interleavedStory.createPrimitive(secondBox, QStringLiteral("Corpo B")).isEmpty()
                    && interleavedStory.createScale(0, 1.1, 0, {}, QStringLiteral("A1")).isEmpty()
                    && interleavedStory.createScale(1, 1.1, 0, {}, QStringLiteral("B1")).isEmpty()
                    && interleavedStory.createScale(2, 1.1, 0, {}, QStringLiteral("A2")).isEmpty(),
                "storyboard con feature di corpi intercalati");
        const quint64 a2Id = interleavedStory.extrusions().at(4).featureId;
        const quint64 b1Id = interleavedStory.extrusions().at(3).featureId;
        require(interleavedStory.moveFeatureTo(4, 2).isEmpty()
                    && interleavedStory.extrusions().at(2).featureId == a2Id
                    && interleavedStory.extrusions().at(4).featureId == b1Id,
                "drag logico tra feature con altri corpi intercalati");
        ForgeCad::HistoryGraphDialog historyGraph;
        historyGraph.setDocument(interleavedStory.currentDocument());
        require(historyGraph.nodeCount() >= interleavedStory.extrusions().size()
                    && historyGraph.edgeCount() == 3 && historyGraph.invalidDependencyCount() == 0,
                "grafo diagnostico della storyboard");
        const double graphZoom = historyGraph.zoomFactor();
        historyGraph.zoomBy(1.25);
        require(historyGraph.zoomFactor() > graphZoom,
                "zoom interattivo del grafo diagnostico");
        historyGraph.zoomBy(0.001);
        require(historyGraph.zoomFactor() >= 0.719,
                "limite leggibile dello zoom del grafo diagnostico");
        historyGraph.show();
        QApplication::processEvents();
        QGraphicsView *graphView = historyGraph.findChild<QGraphicsView *>();
        QGraphicsItem *movableNode = nullptr;
        if (graphView)
            for (QGraphicsItem *item : graphView->scene()->items())
                if ((item->flags() & QGraphicsItem::ItemIsMovable) && item->data(0).toInt() == 1) {
                    movableNode = item;
                    break;
                }
        require(movableNode, "blocchi spostabili nel grafo diagnostico");
        const QPointF movedPosition = movableNode->pos() + QPointF(57.0, 31.0);
        movableNode->setPos(movedPosition);
        QApplication::processEvents();
        require(movableNode->pos() == movedPosition,
                "spostamento di un blocco con aggiornamento dei collegamenti");
        historyGraph.setDocument(interleavedStory.currentDocument());
        QApplication::processEvents();
        bool preservedPosition = false;
        for (QGraphicsItem *item : graphView->scene()->items())
            preservedPosition = preservedPosition
                || ((item->flags() & QGraphicsItem::ItemIsMovable) && item->pos() == movedPosition);
        require(preservedPosition, "posizione manuale conservata dopo la ricostruzione del grafo");
        DocumentState invalidGraph = interleavedStory.currentDocument();
        invalidGraph.extrusions[4].firstBody = 99;
        historyGraph.setDocument(invalidGraph);
        require(historyGraph.invalidDependencyCount() == 1,
                "dipendenza mancante evidenziata dal grafo diagnostico");
        const auto renderedAlpha = [](QWidget &widget) {
            widget.resize(500, 300);
            QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            widget.render(&painter);
            return image.pixelColor(widget.rect().center()).alpha();
        };
        FunctionDialogPanel translucentPanel;
        translucentPanel.setPanelOpacity(25);
        translucentPanel.setBackdropBlur(18);
        const int lowOpacity = renderedAlpha(translucentPanel);
        translucentPanel.setPanelOpacity(100);
        require(lowOpacity > 0 && renderedAlpha(translucentPanel) > lowOpacity,
                "opacita' regolabile dei pannelli funzione");
        require(translucentPanel.backdropBlur() == 18, "sfocatura della scena regolabile sui pannelli funzione");
        translucentPanel.setPanelOpacity(70);
        translucentPanel.show();
        QApplication::processEvents();
        translucentPanel.hide();
        FunctionDialogPanel scrollablePanel;
        QFormLayout *scrollableForm = scrollablePanel.createScrollableForm();
        scrollableForm->addRow(new FeatureOperationDiagram(FeatureOperationDiagram::Extrusion, &scrollablePanel));
        QLabel *longPanelNote = wrappedNote(QStringLiteral(
            "Questo messaggio volutamente lungo verifica che le spiegazioni delle funzioni vadano a capo, "
            "ricevano tutta l'altezza necessaria e non finiscano dietro ai pulsanti o alle righe successive. "
            "Il controllo deve restare leggibile anche quando il pannello viene ridotto alla sua dimensione minima."),
            &scrollablePanel);
        QLabel *rowAfterLongNote = new QLabel(QStringLiteral("Riga successiva"), &scrollablePanel);
        scrollableForm->addRow(longPanelNote);
        scrollableForm->addRow(rowAfterLongNote);
        auto *panelScroll = scrollablePanel.findChild<QScrollArea *>(QStringLiteral("functionDialogScroll"));
        require(panelScroll,
                "contenuto scorrevole dei pannelli funzione");
        scrollablePanel.setFixedSize(400, 280);
        scrollablePanel.show();
        QApplication::processEvents();
        require(longPanelNote->sizePolicy().hasHeightForWidth()
                    && longPanelNote->height() >= 3 * longPanelNote->fontMetrics().height()
                    && longPanelNote->geometry().bottom() < rowAfterLongNote->geometry().top(),
                "note lunghe a capo senza sovrapporre le righe successive");
        require(panelScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded
                    && panelScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
                "pannelli funzione senza tagli orizzontali e con scorrimento verticale");
        scrollablePanel.hide();
        FunctionDialogPanel embeddedPanel(&v);
        embeddedPanel.setWindowTitle(QStringLiteral("Pannello test"));
        embeddedPanel.setSizeIdentity(QStringLiteral("test-panel"));
        require(embeddedPanel.isEmbedded() && !embeddedPanel.isWindow() && embeddedPanel.parentWidget() == &v,
                "pannello funzione sovrapposto nelle coordinate del viewport");
        v.setGlassPanel(123, QRect(10, 20, 300, 220), 16, 14, true);
        require(v.glassCompositor_.glassPanels_.contains(123) && v.glassCompositor_.glassPanels_.value(123).blur == 16
                    && v.glassCompositor_.glassPanels_.value(123).radius == 14,
                "registrazione del pannello per la composizione GPU");
        v.removeGlassPanel(123);
        require(!v.glassCompositor_.glassPanels_.contains(123), "rimozione del pannello dalla composizione GPU");
        embeddedPanel.resize(420, 300);
        embeddedPanel.placeAtLeft();
        const QPoint initialPanelPosition = embeddedPanel.pos();
        const QPoint dragStart = embeddedPanel.mapToGlobal(QPoint(20, 20));
        embeddedPanel.beginEmbeddedMove(dragStart);
        embeddedPanel.updateEmbeddedMove(dragStart + QPoint(40, 30));
        embeddedPanel.endEmbeddedMove();
        require(embeddedPanel.pos() == initialPanelPosition + QPoint(40, 30),
                "trascinamento del vetro aggiorna il ritaglio della scena");
        embeddedPanel.setCornerRadius(18);
        require(embeddedPanel.cornerRadius() == 18 && !embeddedPanel.mask().contains(QPoint(0, 0))
                    && embeddedPanel.mask().contains(embeddedPanel.rect().center()),
                "angoli arrotondati configurabili dei pannelli funzione");
        auto *panelGrip = embeddedPanel.findChild<QSizeGrip *>(QStringLiteral("functionPanelResizeGrip"));
        require(panelGrip && !embeddedPanel.isSizeGripEnabled(),
                "grip interno senza ridimensionamento nativo della finestra niri");
        const QSize viewportBeforeGrip = v.size();
        const QSize panelBeforeGrip = embeddedPanel.size();
        const QPointF gripLocal(10, 10);
        const QPointF gripGlobal(panelGrip->mapToGlobal(gripLocal.toPoint()));
        QMouseEvent gripPress(QEvent::MouseButtonPress, gripLocal, gripGlobal,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent gripMove(QEvent::MouseMove, gripLocal + QPointF(30, 20), gripGlobal + QPointF(30, 20),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent gripRelease(QEvent::MouseButtonRelease, gripLocal + QPointF(30, 20), gripGlobal + QPointF(30, 20),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(panelGrip, &gripPress);
        QApplication::sendEvent(panelGrip, &gripMove);
        QApplication::sendEvent(panelGrip, &gripRelease);
        require(embeddedPanel.size() == panelBeforeGrip + QSize(30, 20) && v.size() == viewportBeforeGrip,
                "il grip ridimensiona soltanto il pannello funzione");
        require(QSettings().value(embeddedPanel.panelSizeSettingsKey()).toSize() == embeddedPanel.size(),
                "ridimensionamento manuale del pannello memorizzato");
        const QPalette panelPalette = embeddedPanel.palette();
        require(std::abs(panelPalette.color(QPalette::WindowText).lightness() - embeddedPanel.panelColor().lightness()) > 100,
                "contrasto del testo sul vetro");
        QComboBox panelCombo(&embeddedPanel);
        panelCombo.addItems({QStringLiteral("Prima voce"), QStringLiteral("Seconda voce")});
        panelCombo.ensurePolished();
        panelCombo.view()->ensurePolished();
        require(std::abs(panelCombo.palette().color(QPalette::Text).lightness()
                         - embeddedPanel.panelColor().lighter(112).lightness()) > 100
                    && std::abs(panelCombo.view()->palette().color(QPalette::Text).lightness()
                                - embeddedPanel.panelColor().darker(125).lightness()) > 100,
                "testo leggibile nel campo e nella tendina dei pannelli funzione");
        const QColor selectionText = panelPalette.color(QPalette::HighlightedText);
        const QColor selectionBackground = panelPalette.color(QPalette::Highlight);
        require(selectionBackground.red() > 230 && selectionBackground.green() > 120
                    && selectionText.lightness() < 80,
                "testo leggibile nella selezione dei menu a discesa");
        require(!ForgeCad::commandIcon(QStringLiteral("panelOpacity")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("panelBlur")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("panelCorners")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("edit")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("visibility")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("rename")).isNull()
                    && !ForgeCad::commandIcon(QStringLiteral("meshColor")).isNull(),
                "icone delle impostazioni dei pannelli");
        FloatingPanel constraintStylePanel(&v, QStringLiteral("Vincoli"),
                                           QStringLiteral("test/constraintPanelStyle"), QSize(380, 360));
        require(constraintStylePanel.isEmbedded()
                    && constraintStylePanel.objectName() == QStringLiteral("functionDialogPanel")
                    && constraintStylePanel.findChild<QSizeGrip *>(QStringLiteral("functionPanelResizeGrip")),
                "pannello Vincoli con lo stesso vetro e grip dei pannelli funzione");
        LoftSelectionDiagram loftDiagram;
        require(renderedAlpha(loftDiagram) == 255, "grafica Loft sempre opaca");
        FeatureOperationDiagram extrusionDiagram(FeatureOperationDiagram::Extrusion);
        FeatureOperationDiagram revolutionDiagram(FeatureOperationDiagram::Revolution);
        require(renderedAlpha(extrusionDiagram) == 255 && renderedAlpha(revolutionDiagram) == 255,
                "grafiche Estrusione e Rivoluzione sempre opache");
        v.resize(900, 650);
        v.setViewNormal(0);
        v.camera_.zoom_ = 20;
        const double ratio = v.axisDisplayLength() / v.camera_.zoom_;
        v.camera_.zoom_ *= 3;
        require(std::fabs(v.axisDisplayLength() / v.camera_.zoom_ - ratio) < 1e-12, "dimensione assi durante zoom");
        v.camera_.zoom_ = 20;
        v.camera_.yaw_ = 0.0f;
        v.camera_.pitch_ = 0.0f;
        v.camera_.roll_ = 0.0f;
        v.lastMousePosition_ = QPoint(450,325);
        v.orbitView(QPoint(100,70));
        require(std::fabs(v.camera_.yaw_-50.0f) < 1e-6f && std::fabs(v.camera_.pitch_-35.0f) < 1e-6f,
                "rotazione classica a mezzo grado per pixel");
        v.lastMousePosition_ += QPoint(100,70);
        v.orbitView(QPoint(-100,-70));
        require(std::fabs(v.camera_.yaw_) < 1e-6f && std::fabs(v.camera_.pitch_) < 1e-6f,
                "gesto inverso recupera orientamento");
        v.orbitView(QPoint(360,360));
        require(std::fabs(v.camera_.yaw_) == 180.0f && std::fabs(v.camera_.pitch_) == 180.0f,
                "orbita libera oltre i poli");
        v.orbitView(QPoint(360,360));
        require(std::fabs(v.camera_.yaw_) < 1e-6f && std::fabs(v.camera_.pitch_) < 1e-6f,
                "giro completo sui due assi");
        v.resize(1200,800);
        v.orbitView(QPoint(100,70));
        require(std::fabs(v.camera_.yaw_-50.0f) < 1e-6f && std::fabs(v.camera_.pitch_-35.0f) < 1e-6f,
                "sensibilita' indipendente dalla dimensione del viewport");
        v.resize(900,650);
        {
            CadViewport pivotView;
            pivotView.resize(900,650);
            pivotView.setViewPreset(0);
            pivotView.camera_.zoom_ = 20;
            ExtrusionObject body;
            body.forgeBody = std::make_shared<const ForgeCad::Kernel::Body>();
            body.display.vertices = {QVector3D(-4,-3,-4), QVector3D(4,-3,-4), QVector3D(0,-3,4)};
            pivotView.extrusions_.append(body);
            const QPoint press(450,325);
            const QVector3D hit = pivotView.orbitPointAt(press);
            require((hit-QVector3D(0,-3,0)).length() < 1e-4f,
                    "pivot sulla superficie sotto il mouse");
            const QPointF fixedPixel = pivotView.projectWorldPoint(hit);
            pivotView.lastMousePosition_ = press;
            pivotView.orbitView(QPoint(80,30));
            require((pivotView.projectWorldPoint(hit)-fixedPixel).manhattanLength() < 1e-3,
                    "pivot resta nello stesso pixel durante orbita");
            pivotView.lastMousePosition_ += QPoint(80,30);
            pivotView.orbitView(QPoint(20,40));
            require((pivotView.camera_.orbitPivot_-hit).length() < 1e-6f
                    && (pivotView.projectWorldPoint(hit)-fixedPixel).manhattanLength() < 1e-3,
                    "pivot stabile per tutto il trascinamento");
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(550,395), QPointF(550,395),
                                Qt::RightButton, Qt::NoButton, Qt::NoModifier);
            pivotView.mouseReleaseEvent(&release);
            require(!pivotView.camera_.orbitPivotValid_, "rilascio libera il pivot");
            const QPoint background(850,40);
            const QVector3D fallback = pivotView.orbitPointAt(background);
            require((pivotView.projectWorldPoint(fallback)-QPointF(background)).manhattanLength() < 1e-3,
                    "pivot sullo sfondo segue il pixel del mouse");
        }
        if (orbitOnly) {
            std::cout << "PASS orbita classica: sensibilita, giri completi e pivot mouse" << std::endl;
            return;
        }
        v.setViewNormal(0);
        v.selectPlane(0);
        const auto dragPlane = [&](bool cancel) {
            const auto corners = v.selection_.kind == SceneObjectKind::Plane ? v.planeCorners(0)
                : v.datumCorners(v.extrusions_.at(v.selection_.index).datumFrame, v.extrusions_.at(v.selection_.index).datum.size);
            const QPoint corner = v.projectWorldPoint(corners.at(2)).toPoint();
            require(v.beginPlaneResize(corner), "maniglia del piano");
            const QPoint end = corner + (corner - v.projectWorldPoint((corners.at(0) + corners.at(2)) * 0.5f).toPoint()) / 2;
            QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            v.mouseMoveEvent(&move);
            v.finishPlaneResize(cancel);
        };
        const auto initial = v.referencePlaneHalf(0);
        dragPlane(true);
        require(v.referencePlaneHalf(0) == initial, "Esc ripristina la dimensione automatica");
        require(v.referencePlaneHalf(0) == v.referencePlaneHalf(1)
                && v.referencePlaneHalf(1) == v.referencePlaneHalf(2),
                "Esc mantiene uguali i tre piani standard");
        dragPlane(false);
        require(v.referencePlaneHalf(0) > initial * 1.4, "ridimensionamento piano standard");
        require(v.referencePlaneHalf(0) == v.referencePlaneHalf(1)
                && v.referencePlaneHalf(1) == v.referencePlaneHalf(2),
                "il ridimensionamento manuale aggiorna tutti i piani standard");
        ExtrusionObject datum;
        datum.name = QStringLiteral("Piano test");
        datum.feature = BodyFeature::DatumPlane;
        GeometryRef ref;
        ref.kind = 1; ref.index = 0;
        datum.datum.refs = {ref}; datum.datum.distance = 2; datum.datum.size = 3;
        require(v.createBody(datum).isEmpty(), "creazione datum");
        const int datumIndex = int(v.extrusions_.size()) - 1;
        v.selectObject(SceneObjectKind::Extrusion, datumIndex);
        dragPlane(false);
        const double resized = v.extrusions_.at(datumIndex).datum.size;
        require(resized > 4.2, "ridimensionamento datum");
        v.undo();
        require(v.extrusions_.at(datumIndex).datum.size == 3, "Undo dimensione datum");
        v.redo();
        require(v.extrusions_.at(datumIndex).datum.size == resized, "Redo dimensione datum");

        // Una curva 3D autonoma (elica/spirale) offre sia la curva sia i suoi
        // estremi come riferimenti grafici del piano normale.
        CadViewport curveReferences;
        curveReferences.resize(800, 600);
        ExtrusionObject pathBody;
        pathBody.name = QStringLiteral("Curva percorso");
        pathBody.feature = BodyFeature::Helix;
        const auto pathLine = std::make_shared<Kernel::Line<3>>(Kernel::Vec3(2, 3, 1), normalized(Kernel::Vec3(1, 0.2, 0.4)));
        pathBody.curve = std::make_shared<Kernel::TrimmedCurve<3>>(pathLine, 0.0, 5.0);
        curveDisplay(*pathBody.curve, 1, pathBody.display);
        curveReferences.extrusions_.append(pathBody);
        curveReferences.refPickOwner_ = -1;
        GeometryRef pickedCurve, pickedEnd;
        const Kernel::Vec3 middle3 = pathBody.curve->point(2.5), end3 = pathBody.curve->point(5.0);
        curveReferences.refPickRoles_ = DatumRoleCurve;
        require(curveReferences.pickReference(curveReferences.projectWorldPoint(QVector3D(middle3.x(), middle3.y(), middle3.z())).toPoint(), pickedCurve)
                    && pickedCurve.kind == 9,
                "curva 3D selezionabile come riferimento del piano normale");
        curveReferences.refPickRoles_ = DatumRolePoint;
        require(curveReferences.pickReference(curveReferences.projectWorldPoint(QVector3D(end3.x(), end3.y(), end3.z())).toPoint(), pickedEnd)
                    && pickedEnd.kind == 10 && pickedEnd.element.point == 1,
                "estremo finale della curva 3D selezionabile graficamente");
        ResolvedRef resolvedEnd;
        require(resolveGeometryRef(pickedEnd, 1, {}, curveReferences.extrusions_, resolvedEnd, nullptr)
                    && resolvedEnd.hasPoint && distance(resolvedEnd.point, end3) < 1e-12,
                "estremo della curva risolto sul dominio esatto");
        DatumParameters normalAtEnd;
        normalAtEnd.mode = 2;
        normalAtEnd.refs = {pickedCurve, pickedEnd};
        SketchFrame normalFrame;
        QString normalError;
        require(computeDatum(normalAtEnd, 1, {}, curveReferences.extrusions_, normalFrame, &normalError),
                "piano normale costruito sull'estremo scelto della curva 3D");
        require(distance(Kernel::Vec3(normalFrame.origin[0], normalFrame.origin[1], normalFrame.origin[2]), end3) < 1e-9,
                "origine del piano normale coincidente con l'estremo della curva");

        // La conversione di un'elica crea un riferimento con molti poli. Il
        // vincolo Fix deve essere analizzato direttamente, senza una matrice
        // densa quadrata che rendeva l'operazione apparentemente infinita.
        Kernel::HelixSpec helixSpec;
        helixSpec.frame = Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0));
        helixSpec.radius = 5.0;
        helixSpec.pitch = 2.0;
        helixSpec.turns = 3.0;
        const auto helixCurve = std::make_shared<Kernel::HelixCurve>(helixSpec);
        SketchObject projectedHelix;
        projectedHelix.plane = 0;
        require(appendProjectedCurve(projectedHelix, helixCurve, helixCurve->domain(), true, true).isEmpty(),
                "conversione rapida di una elica nello schizzo");
        require(projectedHelix.curves.size() == 1 && projectedHelix.curves.first().tool == DrawingTool::Converted,
                "elica convertita come B-spline di riferimento");
        const SketchAnalysis projectedAnalysis = analyzeSketch(projectedHelix);
        require(projectedAnalysis.fullyDefined() && projectedAnalysis.curveDefined.value(0),
                "riferimento convertito fissato senza analisi cubica dei poli");

        Kernel::HelixSpec longHelixSpec = helixSpec;
        longHelixSpec.turns = 1000.0;
        const Kernel::HelixCurve longHelix(longHelixSpec);
        BodyDisplay boundedDisplay;
        curveDisplay(longHelix, 2, boundedDisplay);
        require(boundedDisplay.edges.size() == 1 && boundedDisplay.edges.first().size() == 32769,
                "campionamento grafico delle eliche lunghe limitato");

        const int sketch = v.createSketch(0, QStringLiteral("Test riferimenti"));
        require(sketch >= 0, "creazione schizzo");
        v.selectSketch(sketch);
        v.selectSketchReference(2, 0);
        require(v.sketches_.at(sketch).segments.size() == 1, "riferimento asse X");
        require(v.constraintSelection().size() == 1, "asse scelto per i vincoli");
        v.selectPlane(1);
        require(v.sketches_.at(sketch).segments.size() == 1, "riuso di una retta coincidente");
        v.selectSketchReference(2, 1);
        require(v.sketches_.at(sketch).segments.size() == 2, "riferimento asse Y");
        require(analyzeSketch(v.sketches_.at(sketch)).fullyDefined(), "riferimenti fissi senza liberta' residue");
        v.selectSketchReference(2, 2);
        v.selectPlane(0);
        require(v.sketches_.at(sketch).segments.size() == 2, "riferimenti degeneri non modificano lo schizzo");
        v.undo();
        require(v.sketches_.at(sketch).segments.size() == 1, "Undo riferimento");
        v.redo();
        require(v.sketches_.at(sketch).segments.size() == 2, "Redo riferimento");

        SketchObject work = v.sketches_.at(sketch);
        const int line = int(work.segments.size());
        work.segments.append({QPointF(1, 2), QPointF(4, 3)});
        work.constraints.append(-1); work.segmentLengths.append(0); work.segmentAngles.append(-1);
        const auto fixed = work.segments.at(0);
        work.geometricConstraints.append(makeConstraint(work, ConstraintType::Parallel, {{0, line, -1}, {0, 0, -1}}));
        require(solveSketch(work).ok, "vincolo parallelo all'asse");
        require(work.segments.at(0) == fixed, "il riferimento resta fisso");
        require(std::fabs(work.segments.at(line).second.y() - work.segments.at(line).first.y()) < 1e-8, "segmento parallelo al riferimento");
        {
            // Quota d'angolo: il settore del puntatore decide tra angolo interno e supplementare.
            SketchObject angle;
            angle.segments = {{QPointF(0, 0), QPointF(2, 0)}, {QPointF(0, 0), QPointF(1, std::sqrt(3.0))}};
            angle.constraints = {-1, -1}; angle.segmentLengths = {0, 0}; angle.segmentAngles = {-1, -1};
            SketchConstraint dimension = makeConstraint(angle, ConstraintType::Angle, {{0, 0, -1}, {0, 1, -1}});
            require(std::fabs(std::fabs(dimension.value) - 60.0) < 1e-9, "quota d'angolo iniziale");
            require(chooseAngleSector(angle, dimension, QPointF(1.0, 0.3)) && std::fabs(std::fabs(dimension.value) - 60.0) < 1e-9, "settore interno");
            require(chooseAngleSector(angle, dimension, QPointF(-1.0, 0.5)) && std::fabs(std::fabs(dimension.value) - 120.0) < 1e-9, "settore supplementare");
            require(chooseAngleSector(angle, dimension, QPointF(-1.0, -0.3)) && std::fabs(std::fabs(dimension.value) - 60.0) < 1e-9, "settore opposto al vertice");
            require(chooseAngleSector(angle, dimension, QPointF(0.5, -1.0)) && std::fabs(std::fabs(dimension.value) - 120.0) < 1e-9, "settore supplementare sotto");
            const auto before = angle.segments;
            angle.geometricConstraints.append(dimension);
            require(solveSketch(angle).ok, "quota nel settore supplementare");
            for (int k = 0; k < 2; ++k)
                require(pointDistance(angle.segments.at(k).first, before.at(k).first) < 1e-9 && pointDistance(angle.segments.at(k).second, before.at(k).second) < 1e-9,
                        "il settore non cambia la geometria");
            angle.geometricConstraints.last().value = dimension.value < 0 ? -90.0 : 90.0;
            angle.geometricConstraints.append(makeConstraint(angle, ConstraintType::Fix, {{0, 0, -1}}));
            require(solveSketch(angle).ok, "quota supplementare a 90 gradi");
            const QPointF d = angle.segments.at(0).second - angle.segments.at(0).first, e = angle.segments.at(1).second - angle.segments.at(1).first;
            require(std::fabs(d.x() * e.x() + d.y() * e.y()) < 1e-8, "rette perpendicolari");
        }
        {
            // Misura: distanze minime esatte tra punti, spigoli e facce, angoli, interassi.
            using namespace ForgeCad::Kernel;
            using K = MeasureEntity::Kind;
            const auto box = std::make_shared<const Body>(makeBox(Frame3(), 10.0, 20.0, 30.0));
            const auto holeA = std::make_shared<const Body>(makeCylinder(Frame3(Vec3(50, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 5.0, 10.0));
            const auto holeB = std::make_shared<const Body>(makeCylinder(Frame3(Vec3(80, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), 3.0, 10.0));
            const auto ball = std::make_shared<const Body>(makeSphere(Frame3(Vec3(0, 0, 60), Vec3(0, 0, 1), Vec3(1, 0, 0)), 4.0));
            const auto faceWith = [](const ForgeBody &body, const std::function<bool(const Face &, const Vec3 &)> &test) {
                for (FaceId f : body->faces()) {
                    const Face &face = body->face(f);
                    Vec3 n(0, 0, 0);
                    if (face.surface->type() == SurfaceType::Plane) {
                        const Vec3 z = static_cast<const Plane &>(*face.surface).frame().zDir();
                        n = face.sense ? z : -z;
                    }
                    if (test(face, n)) {
                        MeasureEntity e;
                        e.kind = K::Face;
                        e.body = body;
                        e.face = f;
                        return e;
                    }
                }
                throw std::runtime_error("faccia di prova non trovata");
            };
            const auto planar = [&](const ForgeBody &body, const Vec3 &normal) {
                return faceWith(body, [normal](const Face &, const Vec3 &n) { return dot(n, normal) > 1 - 1e-12; });
            };
            const auto ofType = [&](const ForgeBody &body, SurfaceType type) {
                return faceWith(body, [type](const Face &face, const Vec3 &) { return face.surface->type() == type; });
            };
            const auto edgeThrough = [&](const Vec3 &a, const Vec3 &b) {
                for (EdgeId e : box->edges()) {
                    const Edge &edge = box->edge(e);
                    const Vec3 p = edge.curve->point(edge.range.lo), q = edge.curve->point(edge.range.hi);
                    if ((distance(p, a) < 1e-9 && distance(q, b) < 1e-9) || (distance(p, b) < 1e-9 && distance(q, a) < 1e-9)) {
                        MeasureEntity entity;
                        entity.kind = K::Curve;
                        entity.segments.push_back({edge.curve, edge.range});
                        return entity;
                    }
                }
                throw std::runtime_error("spigolo di prova non trovato");
            };
            const auto pointAt = [](const Vec3 &p) {
                MeasureEntity e;
                e.kind = K::Point;
                e.point = p;
                return e;
            };
            const auto distanceOf = [](const MeasureEntity &a, const MeasureEntity &b) {
                const MeasureReport r = measureEntities({a, b});
                if (!r.ok || !r.hasDistance) throw std::runtime_error(r.error.toStdString());
                return r.distance;
            };
            const MeasureEntity left = planar(box, Vec3(-1, 0, 0)), right = planar(box, Vec3(1, 0, 0)), top = planar(box, Vec3(0, 0, 1));
            require(std::fabs(distanceOf(left, right) - 10.0) < 1e-9, "misura faccia-faccia parallele");
            const MeasureReport facing = measureEntities({left, right});
            require(facing.lines.join(QLatin1Char('\n')).contains(QStringLiteral("Angolo tra le normali: 180°")), "angolo tra le normali opposte");
            const MeasureEntity vertical = edgeThrough(Vec3(0, 0, 0), Vec3(0, 0, 30)), across = edgeThrough(Vec3(0, 20, 30), Vec3(10, 20, 30));
            require(std::fabs(distanceOf(vertical, across) - 20.0) < 1e-9, "misura spigolo-spigolo");
            require(std::fabs(distanceOf(right, vertical) - 10.0) < 1e-9, "misura faccia-spigolo");
            require(std::fabs(distanceOf(pointAt(Vec3(5, 5, 50)), top) - 20.0) < 1e-9, "misura punto-faccia interno");
            require(std::fabs(distanceOf(pointAt(Vec3(20, 30, 40)), top) - std::sqrt(300.0)) < 1e-9, "misura punto-faccia sul bordo");
            require(std::fabs(distanceOf(ofType(ball, SurfaceType::Sphere), top) - 26.0) < 1e-7, "misura sfera-faccia");
            const MeasureEntity cylinderA = ofType(holeA, SurfaceType::Cylinder), cylinderB = ofType(holeB, SurfaceType::Cylinder);
            require(std::fabs(distanceOf(cylinderA, cylinderB) - 22.0) < 1e-7, "misura cilindro-cilindro");
            require(measureEntities({cylinderA, cylinderB}).lines.join(QLatin1Char('\n')).contains(QStringLiteral("interasse: 30")), "interasse dei cilindri");
            const MeasureReport single = measureEntities({top});
            require(single.ok && single.lines.first().contains(QStringLiteral("200")), "area della faccia");
            require(measureEntities({vertical, across}).lines.join(QLatin1Char('\n')).contains(QStringLiteral("Angolo tra le rette: 90°")), "angolo tra spigoli");
        }
        {
            // Sposta / copia entita' dello schizzo: i vincoli verso le entita' ferme spariscono, quelli interni restano.
            SketchObject moving;
            moving.segments = {{QPointF(0, 0), QPointF(4, 0)}, {QPointF(4, 0), QPointF(4, 3)}, {QPointF(10, 0), QPointF(12, 0)}};
            moving.constraints = {-1, -1, -1}; moving.segmentLengths = {0, 0, 0}; moving.segmentAngles = {-1, -1, -1};
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Coincident, {{0, 0, 1}, {0, 1, 0}}));
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Horizontal, {{0, 0, -1}}));
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Parallel, {{0, 0, -1}, {0, 2, -1}}));
            moving.geometricConstraints.append(makeConstraint(moving, ConstraintType::Distance, {{0, 0, -1}}));
            SketchObject moved = moving;
            SketchMove shift;
            shift.translation = QPointF(1, 2);
            QVector<SketchEntity> created;
            require(moveSketchEntities(moved, {{0, 0}}, shift, &created).error.isEmpty() && created.size() == 1, "spostamento di un segmento");
            require(moved.segments.at(0).first == QPointF(1, 2) && moved.segments.at(1) == moving.segments.at(1), "solo il segmento scelto si sposta");
            bool coincident = false, horizontal = false, parallel = false, length = false;
            for (const SketchConstraint &c : moved.geometricConstraints) {
                coincident = coincident || (c.type == ConstraintType::Coincident && (c.first.element == 1 || c.second.element == 1));
                horizontal = horizontal || c.type == ConstraintType::Horizontal;
                parallel = parallel || c.type == ConstraintType::Parallel;
                length = length || c.type == ConstraintType::Distance;
            }
            require(!coincident && horizontal && parallel && length, "vincoli dopo lo spostamento");
            require(solveSketch(moved).ok, "schizzo spostato coerente");
            SketchObject copied = moving;
            SketchMove turn;
            turn.angle = 90.0;
            turn.copy = true;
            require(moveSketchEntities(copied, {{0, 0}, {0, 1}}, turn, &created).error.isEmpty() && created.size() == 2, "copia ruotata");
            require(copied.segments.size() == 5 && pointDistance(copied.segments.at(3).second, QPointF(0, 4)) < 1e-12, "copia ruotata di 90 gradi");
            bool copiedCoincident = false, copiedHorizontal = false;
            for (const SketchConstraint &c : copied.geometricConstraints) {
                copiedCoincident = copiedCoincident || (c.type == ConstraintType::Coincident && c.first.element >= 3 && c.second.element >= 3);
                copiedHorizontal = copiedHorizontal || (c.type == ConstraintType::Horizontal && c.first.element >= 3);
            }
            require(copiedCoincident && !copiedHorizontal, "vincoli copiati con la rotazione");
            require(solveSketch(copied).ok, "schizzo con la copia coerente");
        }
        {
            // Aggancio alla geometria e alla griglia indipendenti.
            const QVector<SketchSegment> segments = {{QPointF(0.1, 0.1), QPointF(1.1, 0.1)}};
            const QPointF raw(0.12, 0.13);
            require(snapSegments(raw, segments, {}, true, true, 0.25, 0.05).point == QPointF(0.1, 0.1), "aggancio alla geometria");
            require(snapSegments(raw, segments, {}, false, true, 0.25, 0.05).point == QPointF(0.0, 0.25), "solo griglia");
            require(snapSegments(raw, segments, {}, false, false, 0.25, 0.05).point == raw, "nessun aggancio");
            require(snapSegments(QPointF(0.6, 0.5), segments, {}, true, false, 0.25, 0.05).point == QPointF(0.6, 0.5), "geometria senza griglia");
        }
        SketchFrame tilted;
        tilted.origin[0] = 2; tilted.normal[0] = std::sqrt(0.5); tilted.normal[2] = std::sqrt(0.5);
        QPointF point, direction;
        require(sketchPlaneReference(work, tilted, point, direction).isEmpty(), "traccia di un piano obliquo");
        const auto world = sketchToWorld(point, work);
        require(std::fabs((world.x() - 2) + world.z()) < 1e-10, "traccia sul piano esatto");
        require(sketchPlaneReference(work, SketchFrame(), point, direction).size() > 0, "piano coincidente rifiutato");

        SketchObject contactTarget;
        contactTarget.name = QStringLiteral("Sezione loft");
        contactTarget.plane = 0;
        SketchObject crossing;
        crossing.name = QStringLiteral("Guida loft");
        crossing.plane = kFacePlane;
        crossing.customFrame = true;
        crossing.frame.normal[0] = 0.0; crossing.frame.normal[1] = 1.0; crossing.frame.normal[2] = 0.0;
        crossing.frame.xAxis[0] = 1.0; crossing.frame.xAxis[1] = 0.0; crossing.frame.xAxis[2] = 0.0;
        crossing.segments.append({QPointF(0, -1), QPointF(0, 1)});
        crossing.constraints.append(-1); crossing.segmentLengths.append(0); crossing.segmentAngles.append(-1);
        require(appendSketchContactReferences(contactTarget, crossing).isEmpty(), "contatto tra schizzi");
        require(contactTarget.segments.size() == 1 && contactTarget.segments.at(0).first == contactTarget.segments.at(0).second,
                "punto di attraversamento come riferimento");
        SketchObject contactLineTarget;
        contactLineTarget.plane = 0;
        SketchObject touching = crossing;
        touching.segments[0] = {QPointF(-1, 0), QPointF(1, 0)};
        require(appendSketchContactReferences(contactLineTarget, touching).isEmpty(), "segmento comune tra piani di schizzo");
        require(contactLineTarget.segments.size() == 1
                && pointDistance(contactLineTarget.segments.at(0).first, contactLineTarget.segments.at(0).second) > 1.0,
                "segmento di contatto come costruzione");
        SketchObject oneContact;
        oneContact.plane = 0;
        touching.segments.append({QPointF(-2, 0), QPointF(2, 0)});
        touching.constraints.append(-1); touching.segmentLengths.append(0); touching.segmentAngles.append(-1);
        require(appendSketchContactReference(oneContact, touching, {0, 1}).isEmpty(), "singolo bordo di schizzo come riferimento");
        require(oneContact.segments.size() == 1, "la scelta grafica non importa tutto lo schizzo");
        contactTarget.segments.append({QPointF(0.4, 0.3), QPointF(1, 0)});
        contactTarget.constraints.append(-1); contactTarget.segmentLengths.append(0); contactTarget.segmentAngles.append(-1);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::Coincident, {{0, 1, 0}, {0, 0, 0}}));
        require(solveSketch(contactTarget).ok, "coincidenza con contatto esterno");
        require(pointDistance(contactTarget.segments.at(1).first, QPointF()) < 1e-8, "nodo sul contatto esterno");
        contactTarget.segments.append({QPointF(-1, 0.5), QPointF(1, 0.5)});
        contactTarget.constraints.append(-1); contactTarget.segmentLengths.append(0); contactTarget.segmentAngles.append(-1);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::PointOnCurve, {{0, 0, 0}, {0, 2, -1}}));
        require(solveSketch(contactTarget).ok, "contatto esterno su segmento");
        require(std::fabs(contactTarget.segments.at(2).first.y()) < 1e-8 && std::fabs(contactTarget.segments.at(2).second.y()) < 1e-8,
                "segmento passa per il contatto esterno");
        CurveObject contactCircle;
        contactCircle.tool = DrawingTool::Circle;
        contactCircle.controlPoints = {QPointF(2, 0), QPointF(3, 0)};
        recalculateCurve(contactCircle);
        contactTarget.curves.append(contactCircle);
        contactTarget.geometricConstraints.append(makeConstraint(contactTarget, ConstraintType::PointOnCurve, {{0, 0, 0}, {1, 0, -1}}));
        require(solveSketch(contactTarget).ok, "contatto esterno su curva");
        require(std::fabs(pointDistance(contactTarget.curves.at(0).controlPoints.at(0), QPointF())
                          - pointDistance(contactTarget.curves.at(0).controlPoints.at(0), contactTarget.curves.at(0).controlPoints.at(1))) < 1e-8,
                "curva passa per il contatto esterno");

        CurveObject linkedSpline;
        linkedSpline.tool = DrawingTool::Spline;
        linkedSpline.controlPoints = {QPointF(0, 0), QPointF(2, 1), QPointF(4, 0)};
        initializeTangentHandles(linkedSpline);
        linkedSpline.tangentLinked[1] = true;
        work.curves.append(linkedSpline);
        SketchObject handleSketch;
        handleSketch.curves.append(linkedSpline);
        SketchConstraint handleLength = makeConstraint(handleSketch, ConstraintType::Distance,
            {{1, 0, 1}, {1, 0, handlePoint(1, 1)}});
        handleLength.value *= 1.4;
        handleSketch.geometricConstraints.append(handleLength);
        require(solveSketch(handleSketch).ok, "quota della maniglia collegata");
        const CurveObject &solvedSpline = handleSketch.curves.first();
        const QPointF hin = solvedSpline.tangentHandles.at(1).first - solvedSpline.controlPoints.at(1);
        const QPointF hout = solvedSpline.tangentHandles.at(1).second - solvedSpline.controlPoints.at(1);
        require(std::fabs(hin.x() * hout.y() - hin.y() * hout.x()) < 1e-8, "tangenza delle maniglie collegate");

        SketchObject multiplePaths;
        multiplePaths.segments = {{QPointF(0, 0), QPointF(0, 3)}, {QPointF(8, 0), QPointF(8, 3)}};
        multiplePaths.constraints = {-1, -1}; multiplePaths.segmentLengths = {0, 0}; multiplePaths.segmentAngles = {-1, -1};
        QVector<SketchPathRef> components = sketchPathComponents(multiplePaths);
        require(components.size() == 2, "percorsi disconnessi nello stesso schizzo");
        std::vector<Kernel::PathSegment> partialPath;
        require(sketchPath(sketchPathSubset(multiplePaths, components.at(1)), partialPath, nullptr) && partialPath.size() == 1,
                "selezione parziale usabile come percorso");

        QVector<SketchObject> loftSections;
        for (int i = 0; i < 3; ++i) {
            SketchObject section;
            section.name = QStringLiteral("Loft UV %1").arg(i + 1);
            section.plane = kFacePlane;
            section.customFrame = true;
            section.frame.origin[2] = 2.0 * i;
            CurveObject circle;
            circle.tool = DrawingTool::Circle;
            circle.controlPoints = {QPointF(), QPointF(i == 1 ? 1.0 : 2.0, 0.0)};
            recalculateCurve(circle);
            section.curves.append(circle);
            loftSections.append(section);
        }
        QString loftError;
        const ForgeBody loftBody = forgeLoft(loftSections, false, &loftError);
        require(loftBody && loftError.isEmpty(), "loft per griglia UV");
        SketchObject mirroredGuides;
        mirroredGuides.name = QStringLiteral("Guide specchiate");
        mirroredGuides.plane = kFacePlane; mirroredGuides.customFrame = true;
        mirroredGuides.frame.normal[0] = 0; mirroredGuides.frame.normal[1] = 1; mirroredGuides.frame.normal[2] = 0;
        mirroredGuides.frame.xAxis[0] = 1; mirroredGuides.frame.xAxis[1] = 0; mirroredGuides.frame.xAxis[2] = 0;
        for (double sign : {-1.0, 1.0}) {
            CurveObject guide; guide.tool = DrawingTool::Spline;
            guide.controlPoints = {QPointF(2 * sign, 0), QPointF(sign, -2), QPointF(2 * sign, -4)};
            initializeTangentHandles(guide); recalculateCurve(guide); mirroredGuides.curves.append(guide);
        }
        QVector<SketchPathRef> guideComponents = sketchPathComponents(mirroredGuides);
        require(guideComponents.size() == 2, "guide specchiate selezionabili separatamente");
        const ForgeBody partialGuidedLoft = forgeLoft(loftSections,
            {sketchPathSubset(mirroredGuides, guideComponents.first())}, false, 0, 0, 1, 1.0, 1.0, 1.0, &loftError);
        require(partialGuidedLoft && loftError.isEmpty(), "loft con una sola guida scelta dallo schizzo specchiato");
        SketchObject missedGuide = sketchPathSubset(mirroredGuides, guideComponents.first());
        missedGuide.name = QStringLiteral("Guida lontana");
        for (CurveObject &curve : missedGuide.curves) {
            for (QPointF &point : curve.controlPoints) point.rx() += 20.0;
            initializeTangentHandles(curve); recalculateCurve(curve);
        }
        require(!forgeLoft(loftSections, {missedGuide}, false, 0, 0, 1, 1.0, 1.0, 1.0, &loftError),
                "guida che non incontra la sezione rifiutata");
        require(loftError.contains(loftSections.first().name) && loftError.contains(QStringLiteral("[[loft-section=0]]")),
                "errore loft con nome e indice della sezione mancata");
        v.preview_.definition.loftSketches = {sketch};
        const QString visibleLoftError = v.preparePreviewError(loftError);
        require(v.previewErrorSketch_ == sketch && !visibleLoftError.contains(QStringLiteral("[[loft-section=")),
                "sezione dell'errore evidenziabile nella vista");
        BodyDisplay loftDisplay;
        forgeTessellate(*loftBody, 0, loftDisplay);
        forgeSurfaceConstructionCurves(*loftBody, loftDisplay, 4);
        require(!loftDisplay.constructionCurves.isEmpty(), "curve UV dell'anteprima loft");
        const Kernel::Body previewBox = Kernel::makeBox(
            Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 4.0, 3.0, 2.0);
        BodyDisplay extrusionDisplay;
        forgeTessellate(previewBox, 0, extrusionDisplay);
        forgeSurfaceConstructionCurves(previewBox, extrusionDisplay, 4, true);
        require(!extrusionDisplay.constructionCurves.isEmpty(), "curve UV anche sulle facce piane dell'anteprima estrusione");
        {
            const Kernel::Body tool = Kernel::makeBox(
                Kernel::Frame3(Kernel::Vec3(3.0, 0.75, 0.5), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 2.0, 1.5, 1.0);
            const Kernel::Body merged = Kernel::booleanOperation(previewBox, tool, Kernel::BooleanOperation::Unite);
            BodyDisplay fullMerged, localExtrusion;
            forgeTessellate(merged, 0, fullMerged);
            forgeExtrusionPreviewDisplay({std::make_shared<const Kernel::Body>(previewBox)}, merged, 0, localExtrusion, 4);
            require(!localExtrusion.vertices.isEmpty() && !localExtrusion.constructionCurves.isEmpty()
                        && localExtrusion.vertices.size() < fullMerged.vertices.size(),
                    "l'anteprima dell'estrusione fusa contiene solo le facce che modificano la base");

            const Kernel::Body cylinder = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 3.0, 4.0);
            const Kernel::Body bore = Kernel::makeCylinder(
                Kernel::Frame3(Kernel::Vec3(0, 0, -1), Kernel::Vec3(0, 0, 1), Kernel::Vec3(1, 0, 0)), 1.0, 6.0);
            const Kernel::Body cut = Kernel::booleanOperation(cylinder, bore, Kernel::BooleanOperation::Subtract);
            BodyDisplay cuttingFaces, retainedFaces;
            forgeExtrusionPreviewDisplay({std::make_shared<const Kernel::Body>(cylinder)}, cut, 0, cuttingFaces, 4, &retainedFaces);
            bool annularCap = false;
            for (Kernel::FaceId face : cut.faces())
                if (cut.face(face).surface->type() == Kernel::SurfaceType::Plane && cut.face(face).loops.size() == 2) annularCap = true;
            require(annularCap && !cuttingFaces.vertices.isEmpty() && !cuttingFaces.constructionCurves.isEmpty()
                        && !retainedFaces.vertices.isEmpty(),
                    "la sottrazione mostra il corpo opaco gia' forato e la sola superficie di taglio trasparente");
        }
        const QVector3D rayOrigin(2.0f, 1.5f, 10.0f), rayDirection(0.0f, 0.0f, -1.0f);
        require(!CadViewport::edgeOccludedByMesh(extrusionDisplay, rayOrigin, rayDirection, QVector3D(0, 0, 2), 1e-5)
                    && CadViewport::edgeOccludedByMesh(extrusionDisplay, rayOrigin, rayDirection, QVector3D(0, 0, 0), 1e-5),
                "i bordi posteriori sono esclusi dalla selezione visibile");
        {
            const QVector<ExportBody> exportBox{{QStringLiteral("Blocco STL"),
                std::make_shared<const Kernel::Body>(previewBox), {}, QColor(80, 160, 220)}};
            StlExportOptions coarse;
            coarse.maxEdgeLength = 3.0; coarse.deflection = 1.0; coarse.angle = 90.0;
            const StlBuildResult coarseStl = buildBinaryStl(exportBox, coarse);
            StlExportOptions fine = coarse;
            fine.maxEdgeLength = 1.0;
            const StlBuildResult fineStl = buildBinaryStl(exportBox, fine);
            require(coarseStl.error.isEmpty() && fineStl.error.isEmpty()
                        && fineStl.triangleCount > coarseStl.triangleCount,
                    "STL: il lato massimo aumenta realmente il numero dei triangoli");
            require(fineStl.data.size() == 84 + 50 * qint64(fineStl.triangleCount),
                    "STL binario: 50 byte per triangolo");
            require(fineStl.preview.vertices.size() / 3 == qint64(fineStl.triangleCount)
                        && !fineStl.preview.edges.isEmpty() && !fineStl.previewLimited,
                    "STL: anteprima con facce e griglia triangolare completa");
            quint32 storedTriangles = 0;
            std::memcpy(&storedTriangles, fineStl.data.constData() + 80, sizeof(storedTriangles));
            require(qFromLittleEndian(storedTriangles) == fineStl.triangleCount,
                    "STL binario: conteggio triangoli nell'intestazione");
            QTemporaryDir stlTemporary;
            const QString stlPath = stlTemporary.filePath(QStringLiteral("mesh.stl"));
            require(stlTemporary.isValid() && saveBinaryStl(stlPath, fineStl.data).isEmpty()
                        && QFileInfo(stlPath).size() == fineStl.data.size(),
                    "scrittura atomica del file STL");
            const ObjBuildResult quadObj = buildQuadObj(exportBox, fine);
            require(quadObj.error.isEmpty() && quadObj.quadCount > 0
                        && quadObj.quadCount * 2 + quadObj.triangleCount <= fineStl.triangleCount,
                    "OBJ quadrangolare: triangoli adiacenti accoppiati");
            quint64 objFaces = 0, objQuads = 0;
            for (const QByteArray &line : quadObj.data.split('\n')) {
                if (!line.startsWith("f ")) continue;
                ++objFaces;
                if (line.count(' ') == 4) ++objQuads;
            }
            require(objFaces == quadObj.quadCount + quadObj.triangleCount && objQuads == quadObj.quadCount,
                    "OBJ quadrangolare: conteggi coerenti con le facce scritte");
            require(!quadObj.preview.vertices.isEmpty()
                        && quadObj.preview.edges.size() == qint64(quadObj.quadCount + quadObj.triangleCount)
                        && !quadObj.previewLimited,
                    "OBJ quadrangolare: anteprima con la stessa griglia quad/triangolo esportata");
            const QString objPath = stlTemporary.filePath(QStringLiteral("mesh.obj"));
            require(saveQuadObj(objPath, quadObj.data).isEmpty() && QFileInfo(objPath).size() == quadObj.data.size(),
                    "scrittura atomica del file OBJ quadrangolare");

            const Kernel::Body sphere = Kernel::makeSphere(Kernel::Frame3(), 5.0);
            const QVector<ExportBody> exportSphere{{QStringLiteral("Sfera STL"),
                std::make_shared<const Kernel::Body>(sphere), {}, QColor()}};
            StlExportOptions radialCoarse;
            radialCoarse.maxEdgeLength = 0.0; radialCoarse.deflection = 0.5; radialCoarse.angle = 90.0;
            StlExportOptions radialFine = radialCoarse;
            radialFine.deflection = 0.05;
            const StlBuildResult coarseSphere = buildBinaryStl(exportSphere, radialCoarse);
            const StlBuildResult fineSphere = buildBinaryStl(exportSphere, radialFine);
            require(coarseSphere.error.isEmpty() && fineSphere.error.isEmpty()
                        && fineSphere.triangleCount > coarseSphere.triangleCount,
                    "STL: lo scarto cordale aumenta l'approssimazione dei raggi");
            const ObjBuildResult sphereObj = buildQuadObj(exportSphere, radialFine);
            require(sphereObj.error.isEmpty() && sphereObj.quadCount > 0,
                    "OBJ quadrangolare anche sulle superfici curve");
        }

        DocumentState state = v.documentState();
        state.sketches[sketch] = work;
        state.extrusions[datumIndex].loftGuides = {sketch};
        state.extrusions[datumIndex].loftStartContinuity = 1;
        state.extrusions[datumIndex].loftEndContinuity = 2;
        state.extrusions[datumIndex].loftGuideInfluence = 0.65;
        state.extrusions[datumIndex].loftStartInfluence = 0.4;
        state.extrusions[datumIndex].loftEndInfluence = 0.8;
        state.extrusions[datumIndex].loftGuideContinuity = 1;
        state.extrusions[datumIndex].pathSketch = sketch;
        state.extrusions[datumIndex].pathSegments = {1};
        SketchPathRef savedGuide{sketch, {1}, {}};
        state.extrusions[datumIndex].loftGuidePaths = {savedGuide};
        state.lengthUnit = LengthUnit::Foot;
        state.lengthUnitSet = true;
        QTemporaryDir tmp;
        require(tmp.isValid(), "directory temporanea");
        const QString path = tmp.filePath(QStringLiteral("refs.prt"));
        require(saveDocumentFile(path, state, false).isEmpty(), "salvataggio riferimenti e datum");
        DocumentState loaded;
        require(loadDocumentFile(path, loaded).isEmpty(), "lettura riferimenti e datum");
        require(loaded.lengthUnitSet && loaded.lengthUnit == LengthUnit::Foot,
                "unita' lineare del documento salvata nel formato 26");
        CadViewport filePreview;
        filePreview.loadPreviewDocument(loaded);
        require(!filePreview.sketches_.isEmpty() && !filePreview.sceneGeometryPoints().isEmpty(),
                "schizzi visibili nell'anteprima della finestra Apri");
        require(loaded.extrusions.at(datumIndex).datum.size == resized, "dimensione datum nel documento");
        require(loaded.sketches.at(sketch).curves.last().tangentLinked.value(1), "collegamento maniglie nel documento");
        require(loaded.extrusions.at(datumIndex).pathSegments == QVector<int>{1}
                && loaded.extrusions.at(datumIndex).loftGuidePaths == QVector<SketchPathRef>{savedGuide},
                "percorsi parziali nel documento");
        const ExtrusionObject &roundTrip = loaded.extrusions.at(datumIndex);
        require(roundTrip.loftGuides == QVector<int>{sketch} && roundTrip.loftStartContinuity == 1 && roundTrip.loftEndContinuity == 2,
                "parametri loft nel documento");
        require(roundTrip.loftGuideInfluence == 0.65 && roundTrip.loftStartInfluence == 0.4 && roundTrip.loftEndInfluence == 0.8,
                "influenze loft nel documento");
        require(roundTrip.loftGuideContinuity == 1, "tangenza delle guide nel documento");
        require(solveSketch(loaded.sketches[sketch]).ok, "vincoli dopo riapertura");

        v.endSketchMode();
        PrimitiveParameters primitive;
        require(v.createPrimitive(primitive, QStringLiteral("Box rendering")).isEmpty(), "primitiva per rendering");
        const int body = int(v.extrusions_.size()) - 1;
        const auto &display = v.extrusions_.at(body).display;
        require(display.rayIndex && !display.faceEdges.isEmpty(), "indici di selezione preparati");
        for (const auto &edges : display.faceEdges)
            for (int e : edges) require(e >= 0 && e < display.edges.size(), "corrispondenza faccia-spigoli");
        v.fitAll();
        const auto &projection = v.projectedBodyEdges(body);
        require(projection.lines.size() == display.edges.size(), "cache proiezione spigoli");
        const auto oldPoint = projection.lines.first().first();
        v.camera_.panX_ += 1;
        require(v.projectedBodyEdges(body).lines.first().first() != oldPoint, "invalidazione cache sul pan");

        // La dimensione maggiore del corpo determina un unico quadrato comune:
        // i tre piani devono restare uguali anche per un corpo molto sottile.
        CadViewport thin;
        for (double &scale : thin.referencePlaneScales_) scale = 1.0;
        PrimitiveParameters plate;
        plate.size[0] = 100.0;
        plate.size[1] = 5.0;
        plate.size[2] = 2.0;
        require(thin.createPrimitive(plate, QStringLiteral("Piastra sottile")).isEmpty(), "primitiva sottile");
        const QSizeF xy = thin.referencePlaneExtents(0);
        const QSizeF xz = thin.referencePlaneExtents(1);
        const QSizeF yz = thin.referencePlaneExtents(2);
        require(xy.width() == xy.height(), "il piano XY e' quadrato");
        require(xy == xz && xz == yz, "i tre piani hanno la stessa dimensione");
        require(xy.width() >= 60.0, "la misura comune segue la dimensione maggiore del corpo");

        // Una seconda lavorazione piu' grande, adiacente a una gia' presente,
        // viene rigenerata dalla base comune in ordine di misura. Copre tutte
        // le combinazioni raccordo/smusso sulla zona d'intersezione.
        for (bool firstChamfer : {false, true})
            for (bool secondChamfer : {false, true}) {
                CadViewport sequential;
                PrimitiveParameters block;
                block.size[0] = 4.0; block.size[1] = 3.0; block.size[2] = 2.0;
                require(sequential.createPrimitive(block, QStringLiteral("Base raccordi consecutivi")).isEmpty(),
                        "base dei raccordi consecutivi");
                const QVector<EdgePoint> vertical{{4.0, 0.0, 1.0}};
                require(sequential.createBlend(0, vertical, 0.3, firstChamfer, QStringLiteral("Prima lavorazione")).isEmpty(),
                        "prima lavorazione piccola");
                const QVector<EdgePoint> topFront{{2.0, 0.0, 2.0}};
                require(sequential.createBlend(1, topFront, 0.55, secondChamfer, QStringLiteral("Seconda lavorazione")).isEmpty(),
                        "seconda lavorazione grande sulla zona gia' raccordata");
                require(sequential.extrusions_.back().forgeBody != nullptr, "rigenerazione delle lavorazioni intersecanti");
            }

        if (render) {
            CadViewport threadView;
            threadView.resize(720, 720);
            PrimitiveParameters shaft;
            shaft.kind = PrimitiveKind::Cylinder;
            shaft.size[0] = 4.0;
            shaft.size[1] = 12.0;
            require(threadView.createPrimitive(shaft, QStringLiteral("Albero")).isEmpty(), "albero per rendering del filetto");
            const ForgeBody shaftBody = threadView.extrusions_.first().forgeBody;
            Kernel::FaceId shaftSide;
            for (Kernel::FaceId face : shaftBody->faces())
                if (shaftBody->face(face).surface->type() == Kernel::SurfaceType::Cylinder) { shaftSide = face; break; }
            require(shaftSide.valid(), "faccia dell'albero da renderizzare");
            ExtrusionObject threadFeature;
            threadFeature.name = QStringLiteral("Filettatura esterna");
            threadFeature.feature = BodyFeature::Thread;
            threadFeature.firstBody = 0;
            threadFeature.thread.standard = 0;
            threadFeature.thread.designation = QStringLiteral("M8 x 1,25");
            threadFeature.thread.pitch = 1.25;
            threadFeature.thread.length = 10.0;
            const Kernel::FinId shaftFin = shaftBody->loop(shaftBody->face(shaftSide).loops.front()).first;
            threadFeature.thread.face = faceReference(*shaftBody, shaftSide, shaftBody->finPoint(shaftFin, 0.5));
            require(threadView.createBody(threadFeature, {0}).isEmpty(), "feature filettatura esterna da renderizzare");
            threadView.setViewPreset(4);
            threadView.show();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            threadView.fitAll();
            threadView.update();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            require(threadView.grabFramebuffer().save(QStringLiteral("/tmp/forgecad-thread.png")), "immagine del filetto esterno");
            threadView.hide();
            v.show();
            for (int i = 0; i < 8; ++i) QApplication::processEvents();
            require(v.isValid(), "contesto OpenGL valido");
            require(v.context() && v.context()->format().profile() == QSurfaceFormat::CoreProfile
                        && v.context()->format().majorVersion() >= 3,
                    "contesto OpenGL 3 Core effettivo");
            DocumentFilePreview openPreview;
            openPreview.resize(360, 420);
            openPreview.setPath(path);
            auto *fileProgress = openPreview.findChild<QProgressBar *>(QStringLiteral("documentPreviewProgress"));
            require(fileProgress && !fileProgress->isHidden(), "barra durante il caricamento dell'anteprima file");
            openPreview.show();
            QEventLoop previewWait;
            QTimer::singleShot(500, &previewWait, &QEventLoop::quit);
            previewWait.exec();
            auto *previewViewport = dynamic_cast<CadViewport *>(
                openPreview.findChild<QOpenGLWidget *>(QStringLiteral("documentPreviewViewport")));
            auto *previewDetails = openPreview.findChild<QLabel *>(QStringLiteral("documentPreviewDetails"));
            require(previewViewport && !previewViewport->sketches_.isEmpty()
                        && previewDetails && previewDetails->text().contains(QStringLiteral("schizzi")),
                    "caricamento asincrono del riquadro nella finestra Apri");
            require(fileProgress->isHidden(), "barra nascosta al termine dell'anteprima file");
            openPreview.grab().save(QStringLiteral("/tmp/forgecad-open-preview.png"));
            openPreview.hide();
            PdfWindow progressWindow;
            progressWindow.show();
            QApplication::processEvents();
            require(progressWindow.openDocumentPath(path), "apertura del documento con avanzamento determinato");
            auto *openProgress = progressWindow.findChild<QProgressDialog *>(QStringLiteral("foregroundProgressDialog"));
            require(openProgress && openProgress->maximum() == 100 && openProgress->value() == 100 && openProgress->isHidden(),
                    "finestra di apertura arrivata al 100% e chiusa al termine");
            progressWindow.close();
            FunctionDialogPanel focusPanel(&v);
            focusPanel.setWindowTitle(QStringLiteral("Test focus pannello"));
            auto *focusSpin = new QDoubleSpinBox(&focusPanel);
            focusPanel.createScrollableForm()->addRow(QStringLiteral("Valore:"), focusSpin);
            focusPanel.show();
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            // Wayland puo' negare l'attivazione della finestra di test quando
            // l'utente sta lavorando in un'altra applicazione. In quel caso il
            // widget resta comunque il destinatario predisposto per il focus.
            require(focusSpin->hasFocus() || focusPanel.focusWidget() == focusSpin,
                    "focus iniziale sul primo dato del pannello");
            require(focusPanel.size().width() <= v.width() - 32 && focusPanel.size().height() <= v.height() - 32,
                    "dimensione automatica limitata al viewport");
            focusPanel.hide();
            require(v.gpuGlassAvailable(), "shader OpenGL per la sfocatura dei pannelli");
            v.setGlassPanel(456, QRect(30, 30, 260, 180), 14, 18, true);
            v.setExportMeshPreview(display, true, false);
            v.update();
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            const QImage screenshot = v.grabFramebuffer();
            require(!screenshot.isNull(), "rendering framebuffer");
            v.clearExportMeshPreview();
            v.removeGlassPanel(456);
            screenshot.save(QStringLiteral("/tmp/forgecad-viewport-test.png"));
            v.makeCurrent();
            while (glGetError() != GL_NO_ERROR) {}
            v.displayCache_.faces(display);
            v.displayCache_.edges(display);
            require(glGetError() == GL_NO_ERROR, "rendering VBO senza errori GL");
            {
                QOpenGLFramebufferObject framebuffer(160, 160, QOpenGLFramebufferObject::CombinedDepthStencil);
                require(framebuffer.isValid(), "framebuffer confronto VBO");
                const auto draw = [&]() {
                    framebuffer.bind();
                    glViewport(0, 0, 160, 160);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
                    QMatrix4x4 projection;
                    projection.ortho(-6.0f, 6.0f, -6.0f, 6.0f, -100.0f, 100.0f);
                    QMatrix4x4 modelView;
                    modelView.rotate(25.0f, 1.0f, 0.0f, 0.0f);
                    modelView.rotate(30.0f, 0.0f, 1.0f, 0.0f);
                    v.displayCache_.setMatrices(projection, modelView);
                    v.displayCache_.setLightingEnabled(false);
                    v.displayCache_.setColor(QVector4D(0.8f, 0.5f, 0.2f, 1.0f));
                    v.displayCache_.faces(display);
                    v.displayCache_.setColor(QVector4D(0.2f, 0.8f, 0.9f, 1.0f));
                    v.displayCache_.edges(display);
                    QVector<QMatrix4x4> instances(2);
                    instances[0].translate(-2.0f, 0.0f, 0.0f);
                    instances[1].translate(2.0f, 0.0f, 0.0f);
                    v.displayCache_.pickingInstancedFaces(display, instances, QVector4D(0.1f, 0.2f, 0.3f, 1.0f));
                    v.overlayRenderer_.setMatrices(projection, modelView);
                    v.overlayRenderer_.drawInstanced(GL_LINES,
                        QVector<QVector3D>{QVector3D(0, 0, 0), QVector3D(1, 0, 0)},
                        QVector4D(1.0f, 1.0f, 0.0f, 1.0f), instances);
                    QMatrix4x4 overlayView;
                    v.overlayRenderer_.setMatrices(projection, overlayView);
                    v.overlayRenderer_.drawWideLineStrip(
                        QVector<QVector3D>{QVector3D(-4, 4, 0), QVector3D(4, 4, 0)},
                        QVector4D(1.0f, 0.0f, 0.0f, 1.0f), 12.0f);
                    glFinish();
                    return framebuffer.toImage();
                };
                const QImage cached = draw();
                require(glGetError() == GL_NO_ERROR, "renderer Core non produce errori GL");
                bool colored = false;
                for (int y = 0; y < cached.height(); ++y)
                    for (int x = 0; x < cached.width(); ++x) colored |= (cached.pixel(x, y) & 0xffffff) != 0;
                require(colored, "confronto rendering non vuoto");
                int redBand = 0;
                for (int y = 0; y < cached.height(); ++y) {
                    const QColor pixel = cached.pixelColor(cached.width() / 2, y);
                    if (pixel.red() > 220 && pixel.green() < 50 && pixel.blue() < 50) ++redBand;
                }
                require(redBand >= 10, "linea evidenziata espansa realmente oltre il limite di glLineWidth");
                framebuffer.release();
            }
            v.displayCache_.clear();
            v.doneCurrent();
            v.close();
        }
        // Offset nello schizzo: catene chiuse verso l'esterno (o l'interno),
        // angoli vivi prolungati o raccordati, archi concentrici, spline
        // approssimate; i vincoli aggiunti sono soddisfatti.
        {
            const auto rectangle = [](double w, double h) {
                SketchObject sketch;
                const QVector<QPointF> c{{0, 0}, {w, 0}, {w, h}, {0, h}};
                for (int k = 0; k < 4; ++k) {
                    sketch.segments.append({c[k], c[(k + 1) % 4]});
                    sketch.constraints.append(-1);
                    sketch.segmentLengths.append(0.0);
                    sketch.segmentAngles.append(-1.0);
                }
                return sketch;
            };
            const auto all = [](const SketchObject &sketch) {
                QVector<ForgeCad::SketchEntity> entities;
                for (int k = 0; k < sketch.segments.size(); ++k) entities.append({0, k});
                for (int k = 0; k < sketch.curves.size(); ++k) entities.append({1, k});
                return entities;
            };
            const auto satisfied = [](const SketchObject &sketch) {
                for (const SketchConstraint &c : sketch.geometricConstraints)
                    if (ForgeCad::constraintError(sketch, c) > 1e-9) return false;
                return true;
            };
            const auto bounds = [](const SketchObject &sketch, int from) {
                QRectF box;
                for (int k = from; k < sketch.segments.size(); ++k) {
                    const QRectF r = QRectF(sketch.segments.at(k).first, sketch.segments.at(k).second).normalized();
                    box = box.isNull() ? r : box.united(r);
                }
                return box;
            };
            SketchObject outward = rectangle(4.0, 3.0);
            ForgeCad::SketchOffset offset;
            offset.distance = 0.5;
            QVector<ForgeCad::SketchEntity> created;
            require(ForgeCad::offsetSketchEntities(outward, all(outward), offset, &created).error.isEmpty() && created.size() == 4,
                    "offset del rettangolo verso l'esterno");
            const QRectF grown = bounds(outward, 4);
            require(std::fabs(grown.left() + 0.5) < 1e-12 && std::fabs(grown.right() - 4.5) < 1e-12 && std::fabs(grown.top() + 0.5) < 1e-12
                        && std::fabs(grown.bottom() - 3.5) < 1e-12 && satisfied(outward),
                    "rettangolo a distanza con gli angoli vivi");
            SketchObject inward = rectangle(4.0, 3.0);
            offset.reverse = true;
            require(ForgeCad::offsetSketchEntities(inward, all(inward), offset).error.isEmpty(), "offset del rettangolo verso l'interno");
            const QRectF shrunk = bounds(inward, 4);
            require(std::fabs(shrunk.left() - 0.5) < 1e-12 && std::fabs(shrunk.right() - 3.5) < 1e-12 && satisfied(inward), "rettangolo ristretto");
            offset.reverse = false;
            offset.roundCorners = true;
            SketchObject rounded = rectangle(4.0, 3.0);
            require(ForgeCad::offsetSketchEntities(rounded, all(rounded), offset, &created).error.isEmpty() && created.size() == 8
                        && rounded.curves.size() == 4 && satisfied(rounded),
                    "offset con gli archi negli angoli");
            offset.roundCorners = false;
            offset.bothSides = true;
            SketchObject both = rectangle(4.0, 3.0);
            require(ForgeCad::offsetSketchEntities(both, all(both), offset, &created).error.isEmpty() && created.size() == 8, "offset dalle due parti");
            offset.bothSides = false;
            // Cerchio, arco aperto e spline.
            SketchObject curves;
            CurveObject circle;
            circle.tool = DrawingTool::Circle;
            circle.controlPoints = {QPointF(0, 0), QPointF(2, 0)};
            curves.curves.append(circle);
            require(ForgeCad::offsetSketchEntities(curves, all(curves), offset, &created).error.isEmpty()
                        && curves.curves.last().tool == DrawingTool::Circle
                        && std::fabs(QLineF(curves.curves.last().controlPoints.at(0), curves.curves.last().controlPoints.at(1)).length() - 2.5) < 1e-12
                        && satisfied(curves),
                    "cerchio a distanza concentrico");
            offset.reverse = true;
            offset.distance = 3.0;
            SketchObject tooFar;
            tooFar.curves.append(circle);
            require(!ForgeCad::offsetSketchEntities(tooFar, all(tooFar), offset).error.isEmpty() && tooFar.curves.size() == 1,
                    "raggio negativo: errore e schizzo invariato");
            offset.reverse = false;
            offset.distance = 0.3;
            SketchObject free;
            CurveObject spline;
            spline.tool = DrawingTool::Nurbs;
            spline.controlPoints = {QPointF(0, 0), QPointF(2, 3), QPointF(5, -1), QPointF(7, 2), QPointF(9, 0)};
            free.curves.append(spline);
            require(ForgeCad::offsetSketchEntities(free, all(free), offset, &created).error.isEmpty() && created.size() == 1
                        && free.curves.last().tool == DrawingTool::Converted,
                    "NURBS a distanza come curva derivata");
            const auto original = ForgeCad::curveGeometry(free.curves.first()).front();
            const auto copy = ForgeCad::curveGeometry(free.curves.last()).front();
            double worst = 0.0;
            for (int k = 0; k <= 50; ++k) {
                const ForgeCad::Kernel::Vec2 p = copy.curve->point(copy.range.lo + copy.range.length() * k / 50.0);
                worst = std::max(worst, std::fabs(ForgeCad::Kernel::projectPoint(*original.curve, p, original.range).distance - 0.3));
            }
            require(worst < 1e-6, "distanza della copia della NURBS");
            // Rettangolo arrotondato (segmenti e archi tangenti): archi concentrici di raggio R - d.
            {
                SketchObject slot;
                const double r = 1.0;
                const auto seg = [&](QPointF a, QPointF b) {
                    slot.segments.append({a, b});
                    slot.constraints.append(-1);
                    slot.segmentLengths.append(0.0);
                    slot.segmentAngles.append(-1.0);
                };
                const auto arc = [&](QPointF c, QPointF a, QPointF b) {
                    CurveObject curve;
                    curve.tool = DrawingTool::Arc;
                    curve.controlPoints = {c, a, b};
                    slot.curves.append(curve);
                };
                seg({r, 0}, {5 - r, 0});
                arc({5 - r, r}, {5 - r, 0}, {5, r});
                seg({5, r}, {5, 3 - r});
                arc({5 - r, 3 - r}, {5, 3 - r}, {5 - r, 3});
                seg({5 - r, 3}, {r, 3});
                arc({r, 3 - r}, {r, 3}, {0, 3 - r});
                seg({0, 3 - r}, {0, r});
                arc({r, r}, {0, r}, {r, 0});
                ForgeCad::SketchOffset in;
                in.distance = 0.4;
                in.reverse = true;
                require(ForgeCad::offsetSketchEntities(slot, all(slot), in, &created).error.isEmpty() && created.size() == 8 && satisfied(slot),
                        "rettangolo arrotondato verso l'interno");
                const CurveObject &inner = slot.curves.at(4);
                require(inner.tool == DrawingTool::Arc && std::fabs(QLineF(inner.controlPoints.at(0), inner.controlPoints.at(1)).length() - 0.6) < 1e-12,
                        "arco concentrico di raggio R - d");
            }
            // Angolo concavo di una catena aperta (L): le copie si accorciano fino al punto comune.
            SketchObject corner;
            for (const SketchSegment &segment : {SketchSegment(QPointF(0, 0), QPointF(4, 0)), SketchSegment(QPointF(4, 0), QPointF(4, 3))}) {
                corner.segments.append(segment);
                corner.constraints.append(-1);
                corner.segmentLengths.append(0.0);
                corner.segmentAngles.append(-1.0);
            }
            offset.distance = 0.5;
            require(ForgeCad::offsetSketchEntities(corner, all(corner), offset).error.isEmpty() && corner.segments.size() == 4
                        && QLineF(corner.segments.at(2).second, QPointF(3.5, 0.5)).length() < 1e-12
                        && QLineF(corner.segments.at(3).first, QPointF(3.5, 0.5)).length() < 1e-12 && satisfied(corner),
                    "angolo concavo: le copie si incontrano");
        }
        offsetFacePicking();
        // Offset di superficie e cucitura: feature parametriche, salvate nel formato 32.
        {
            CadViewport surfaces;
            PrimitiveParameters ball;
            ball.kind = PrimitiveKind::Sphere;
            ball.size[0] = 2.0;
            require(surfaces.createPrimitive(ball, QStringLiteral("Sfera")).isEmpty(), "sfera per l'offset");
            ExtrusionObject offset;
            offset.feature = BodyFeature::SurfaceOffset;
            offset.firstBody = 0;
            offset.distance = 0.5;
            offset.name = QStringLiteral("Offset");
            require(surfaces.createBody(offset).isEmpty(), "offset di tutte le facce");
            const ExtrusionObject &skin = surfaces.extrusions_.at(1);
            require(skin.forgeBody && skin.forgeBody->isSheet() && !skin.solid, "l'offset e' una superficie");
            require(std::fabs(ForgeCad::Kernel::faceArea(*skin.forgeBody, skin.forgeBody->faces().front()) - 4.0 * M_PI * 6.25) < 1e-8,
                    "area della sfera a distanza");
            require(surfaces.extrusions_.at(0).visible, "il corpo di partenza dell'offset resta visibile");
            ExtrusionObject sew;
            sew.feature = BodyFeature::Sew;
            sew.firstBody = 1;
            sew.sewSolid = true;
            sew.name = QStringLiteral("Cucitura");
            require(surfaces.createBody(sew).isEmpty(), "cucitura in un solido");
            const ExtrusionObject &closed = surfaces.extrusions_.at(2);
            require(closed.solid && std::fabs(ForgeCad::Kernel::massProperties(*closed.forgeBody).volume - 4.0 / 3.0 * M_PI * 15.625) < 1e-7,
                    "volume del solido cucito");
            require(!surfaces.extrusions_.at(1).visible, "le superfici cucite si nascondono");
            // Offset di una faccia sola di un cilindro (il fianco) con il riferimento persistente.
            PrimitiveParameters can;
            can.kind = PrimitiveKind::Cylinder;
            can.size[0] = 1.0;
            can.size[1] = 3.0;
            require(surfaces.createPrimitive(can, QStringLiteral("Cilindro")).isEmpty(), "cilindro per l'offset");
            const int cylinder = int(surfaces.extrusions_.size()) - 1;
            const ForgeCad::Kernel::Body &canBody = *surfaces.extrusions_.at(cylinder).forgeBody;
            ForgeCad::Kernel::FaceId side;
            for (ForgeCad::Kernel::FaceId f : canBody.faces())
                if (canBody.face(f).surface->type() == ForgeCad::Kernel::SurfaceType::Cylinder) side = f;
            ExtrusionObject sideOffset;
            sideOffset.feature = BodyFeature::SurfaceOffset;
            sideOffset.firstBody = cylinder;
            sideOffset.distance = -0.25;
            sideOffset.offsetSew = false;
            sideOffset.offsetFaces = {ForgeCad::faceReference(canBody, side, ForgeCad::Kernel::Vec3(1.0, 0.0, 1.5))};
            sideOffset.name = QStringLiteral("Offset fianco");
            require(surfaces.createBody(sideOffset).isEmpty(), "offset del fianco del cilindro");
            const ExtrusionObject &band = surfaces.extrusions_.back();
            require(std::fabs(ForgeCad::Kernel::faceArea(*band.forgeBody, band.forgeBody->faces().front()) - 2.0 * M_PI * 0.75 * 3.0) < 1e-9,
                    "area del fianco a distanza verso l'interno");
            QTemporaryDir directory;
            const QString path = directory.filePath(QStringLiteral("superfici.prt"));
            require(saveDocumentFile(path, surfaces.currentDocument(), false).isEmpty(), "salvataggio di offset e cucitura");
            DocumentState reloaded;
            require(loadDocumentFile(path, reloaded).isEmpty(), "lettura di offset e cucitura");
            require(reloaded.extrusions.at(2).feature == BodyFeature::Sew && reloaded.extrusions.at(2).sewSolid
                        && reloaded.extrusions.back().feature == BodyFeature::SurfaceOffset && reloaded.extrusions.back().offsetFaces.size() == 1
                        && reloaded.extrusions.back().distance == -0.25
                        && !reloaded.extrusions.back().offsetSew && reloaded.extrusions.at(1).offsetSew,
                    "parametri di offset e cucitura nel documento");
            CadViewport again;
            again.loadDocument(reloaded);
            require(again.extrusions_.at(2).solid && again.extrusions_.back().forgeBody, "offset e cucitura rigenerati");
        }
        // Taglio di superfici: il bersaglio puo' essere anche la pelle di un
        // solido; gli utensili possono essere corpi, piani e schizzi.
        {
            using namespace ForgeCad;
            using namespace ForgeCad::Kernel;
            QString error;
            const ForgeBody plate = std::make_shared<const Body>(makePlaneSheet(Frame3(), 10.0));
            const ForgeBody cylinder = std::make_shared<const Body>(makeCylinder(
                Frame3(Vec3(1, 2, -3), Vec3(0, 0, 1), Vec3(1, 0, 0)), 2.5, 6.0));
            const QVector<SheetPiece> cachedPieces = forgeSheetPieces(plate, cylinder, 0, &error);
            require(cachedPieces.size() == 2 && error.isEmpty(),
                    "taglio di una superficie con un corpo");
            require(cachedPieces.at(0).geometry && cachedPieces.at(1).geometry
                        && !cachedPieces.at(0).display.vertices.isEmpty() && !cachedPieces.at(1).display.vertices.isEmpty()
                        && forgeClosestSheetPiece(cachedPieces, cachedPieces.at(1).point) == 1,
                    "parti del taglio riutilizzabili e selezionabili nella vista");
            CadViewport cachedViewport;
            ExtrusionObject cachedPlate, cachedCylinder;
            cachedPlate.name = QStringLiteral("Lastra cache"); cachedPlate.forgeBody = plate;
            cachedCylinder.name = QStringLiteral("Cilindro cache"); cachedCylinder.forgeBody = cylinder; cachedCylinder.solid = true;
            cachedViewport.extrusions_ = {cachedPlate, cachedCylinder};
            ExtrusionObject cachedDefinition;
            cachedDefinition.feature = BodyFeature::SheetTrim;
            cachedDefinition.firstBody = 0;
            cachedDefinition.secondBody = 1;
            cachedDefinition.trimKeep = cachedPieces.at(0).point;
            cachedViewport.resize(800, 600);
            cachedViewport.setViewNormal(0);
            cachedViewport.setTrimPartPickCallback(0, cachedPieces, -1, {}, [](int, int, EdgePoint) {});
            const EdgePoint hoverPoint = cachedPieces.at(1).point;
            cachedViewport.updateHover(cachedViewport.projectWorldPoint(
                QVector3D(float(hoverPoint.x), float(hoverPoint.y), float(hoverPoint.z))).toPoint());
            require(cachedViewport.trimPartHover_ == 1, "evidenziazione della parte sotto il mouse");
            cachedViewport.showTrimPreview(cachedDefinition, cachedPieces.at(0).geometry, cachedPieces.at(0).display);
            const ForgeBody cachedGeometry = cachedPieces.at(0).geometry;
            require(cachedViewport.createSheetTrim(0, 1, 0, cachedPieces.at(0).point, QStringLiteral("Taglio da cache")).isEmpty()
                        && cachedViewport.extrusions_.back().forgeBody == cachedGeometry,
                    "conferma del taglio senza ripetere l'intersezione");

            error.clear();
            const ForgeBody vertical = forgeTrimPlaneTool(
                plate, Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), &error);
            require(vertical && forgeSheetPieces(plate, vertical, 0, &error).size() == 2 && error.isEmpty(),
                    "taglio di una superficie con un piano di lavoro");
            error.clear();
            const ForgeBody mutualTool = std::make_shared<const Body>(
                makePlaneSheet(Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), 10.0));
            error.clear();
            const QVector<SheetPiece> mutualFirstPieces = forgeSheetPieces(plate, mutualTool, 0, &error);
            const QVector<SheetPiece> mutualSecondPieces = forgeSheetPieces(mutualTool, plate, 0, &error);
            int pickedSide = -1, pickedPart = -1;
            cachedViewport.setTrimPartPickCallback(0, mutualFirstPieces, 1, mutualSecondPieces,
                [&](int side, int part, EdgePoint) { pickedSide = side; pickedPart = part; });
            require(cachedViewport.trimPartPickDisplays_.size() == mutualFirstPieces.size() + mutualSecondPieces.size()
                        && cachedViewport.trimPartPickSides_.value(mutualFirstPieces.size()) == 1,
                    "hover simultaneo delle parti di entrambi i corpi");
            cachedViewport.trimPartPickFinished_(1, 0, mutualSecondPieces.at(0).point);
            require(pickedSide == 1 && pickedPart == 0, "il clic conserva corpo e indice della parte illuminata");
            const ForgeBody mutual = forgeTrimBoth(plate, mutualTool, EdgePoint{-5, 0, 0}, EdgePoint{2, 0, 5}, &error);
            if (!mutual || !error.isEmpty()) std::cout << "Mutual trim: " << error.toStdString() << std::endl;
            require(mutual && mutual->isSheet() && Kernel::checkBody(*mutual).empty() && error.isEmpty(),
                    "taglio reciproco conserva una parte di entrambe le superfici");
            CadViewport mutualViewport;
            ExtrusionObject firstSheet, secondSheet;
            firstSheet.name = QStringLiteral("Prima superficie");
            firstSheet.forgeBody = plate;
            firstSheet.visible = true;
            secondSheet.name = QStringLiteral("Seconda superficie");
            secondSheet.forgeBody = mutualTool;
            secondSheet.visible = true;
            mutualViewport.extrusions_.append(firstSheet);
            mutualViewport.extrusions_.append(secondSheet);
            require(mutualViewport.createSheetTrim(0, 1, 0, EdgePoint{-5, 0, 0}, QStringLiteral("Taglio reciproco"),
                                                   -1, true, EdgePoint{2, 0, 5}).isEmpty(),
                    "feature di taglio reciproco");
            require(!mutualViewport.extrusions_.at(0).visible && !mutualViewport.extrusions_.at(1).visible
                        && mutualViewport.extrusions_.back().trimBoth && mutualViewport.extrusions_.back().forgeBody,
                    "il taglio reciproco nasconde entrambi gli originali");
            QTemporaryDir mutualDirectory;
            const QString mutualPath = mutualDirectory.filePath(QStringLiteral("taglio-reciproco.prt"));
            require(saveDocumentFile(mutualPath, mutualViewport.currentDocument(), false).isEmpty(),
                    "salvataggio del taglio reciproco");
            DocumentState mutualDocument;
            require(loadDocumentFile(mutualPath, mutualDocument).isEmpty() && mutualDocument.extrusions.back().trimBoth
                        && std::fabs(mutualDocument.extrusions.back().trimToolKeep.z - 5.0) < 1e-12,
                    "persistenza del taglio reciproco");

            SketchObject line;
            line.name = QStringLiteral("Linea di taglio");
            line.segments.append({QPointF(-20, 0), QPointF(20, 0)});
            line.constraints.append(-1);
            line.segmentLengths.append(0.0);
            line.segmentAngles.append(-1.0);
            error.clear();
            const ForgeBody sketchTool = forgeTrimSketchTool(plate, line, &error);
            require(sketchTool && forgeSheetPieces(plate, sketchTool, 0, &error).size() == 2 && error.isEmpty(),
                    "taglio di una superficie con uno schizzo aperto");

            const ForgeBody box = std::make_shared<const Body>(makeBox(Frame3(), 4.0, 3.0, 2.0));
            error.clear();
            const ForgeBody boxPlane = forgeTrimPlaneTool(
                box, Frame3(Vec3(2, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0)), &error);
            const QVector<SheetPiece> boxPieces = forgeSheetPieces(box, boxPlane, 0, &error);
            require(boxPlane && boxPieces.size() == 2 && error.isEmpty(),
                    "taglio della pelle di un solido con un piano");
        }
        // Superfici rigata, planare, loft e sweep di superfici (formato 24).
        {
            using namespace ForgeCad::Kernel;
            CadViewport surfaces;
            const auto addSegment = [](SketchObject &sketch, QPointF a, QPointF b) {
                sketch.segments.append({a, b});
                sketch.constraints.append(-1);
                sketch.segmentLengths.append(0.0);
                sketch.segmentAngles.append(-1.0);
            };
            const auto circleSketch = [](const QString &name, double z, double r) {
                SketchObject sketch;
                sketch.name = name;
                sketch.plane = kFacePlane;
                sketch.frame.origin[2] = z;
                CurveObject circle;
                circle.tool = DrawingTool::Circle;
                circle.controlPoints = {QPointF(0, 0), QPointF(r, 0)};
                ForgeCad::recalculateCurve(circle, 1);
                sketch.curves.append(circle);
                return sketch;
            };
            const auto area = [](const ExtrusionObject &body) {
                double total = 0.0;
                for (FaceId f : body.forgeBody->faces()) total += faceArea(*body.forgeBody, f);
                return total;
            };
            // 0: rettangolo 4 x 3 con un foro di raggio 0.5 (piano XY); 1, 2: cerchi a z = 0 e 2; 3: percorso lungo Z.
            SketchObject plate;
            plate.name = QStringLiteral("Lastra");
            addSegment(plate, {0, 0}, {4, 0});
            addSegment(plate, {4, 0}, {4, 3});
            addSegment(plate, {4, 3}, {0, 3});
            addSegment(plate, {0, 3}, {0, 0});
            CurveObject hole;
            hole.tool = DrawingTool::Circle;
            hole.controlPoints = {QPointF(2, 1.5), QPointF(2.5, 1.5)};
            ForgeCad::recalculateCurve(hole, 1);
            plate.curves.append(hole);
            surfaces.sketches_.append(plate);
            surfaces.sketches_.append(circleSketch(QStringLiteral("Basso"), 0.0, 1.0));
            surfaces.sketches_.append(circleSketch(QStringLiteral("Alto"), 2.0, 2.0));
            SketchObject path;
            path.name = QStringLiteral("Percorso");
            path.plane = 1;  // XZ: la seconda coordinata e' Z
            addSegment(path, {0, 0}, {0, 3});
            surfaces.sketches_.append(path);

            ExtrusionObject planar;
            planar.feature = BodyFeature::PlanarSurface;
            planar.sketchIndex = 0;
            planar.name = QStringLiteral("Planare");
            require(surfaces.createBody(planar).isEmpty(), "superficie planare da uno schizzo");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - (12.0 - M_PI * 0.25)) < 1e-9,
                    "area della lastra con il foro");

            const double slant = std::sqrt(4.0 + 1.0), frustum = M_PI * 3.0 * slant;
            ExtrusionObject ruled;
            ruled.feature = BodyFeature::Ruled;
            GeometryRef low, high;
            low.kind = high.kind = 7;
            low.index = 1;
            high.index = 2;
            low.element = high.element = ConstraintRef{1, 0, -1};
            ruled.ruledFirst = {low};
            ruled.ruledSecond = {high};
            ruled.name = QStringLiteral("Rigata");
            require(surfaces.createBody(ruled).isEmpty(), "superficie rigata tra due cerchi di schizzi");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - frustum) < 1e-8,
                    "area del tronco di cono rigato");

            ExtrusionObject loft;
            loft.feature = BodyFeature::Loft;
            loft.loftSketches = {1, 2};
            loft.loftRuled = true;
            loft.loftSurface = true;
            loft.name = QStringLiteral("Tubo");
            require(surfaces.createBody(loft).isEmpty(), "loft di superfici");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - frustum) < 1e-8,
                    "loft di superfici: tubo senza coperchi");

            ExtrusionObject sweep;
            sweep.feature = BodyFeature::Sweep;
            sweep.sketchIndex = 1;
            sweep.pathSketch = 3;
            sweep.sweepSurface = true;
            sweep.name = QStringLiteral("Canna");
            require(surfaces.createBody(sweep).isEmpty(), "sweep di superfici");
            require(surfaces.extrusions_.back().forgeBody->isSheet() && std::fabs(area(surfaces.extrusions_.back()) - 2.0 * M_PI * 3.0) < 1e-8,
                    "sweep di superfici: cilindro senza coperchi");

            // Planare dai bordi superiori di un parallelepipedo (riferimenti agli spigoli).
            PrimitiveParameters block;
            block.size[0] = 4.0;
            block.size[1] = 3.0;
            block.size[2] = 2.0;
            require(surfaces.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco per la planare");
            const int blockIndex = int(surfaces.extrusions_.size()) - 1;
            const Body &blockBody = *surfaces.extrusions_.at(blockIndex).forgeBody;
            ExtrusionObject lid;
            lid.feature = BodyFeature::PlanarSurface;
            lid.sketchIndex = -1;
            for (EdgeId e : blockBody.edges()) {
                const Edge &edge = blockBody.edge(e);
                const Vec3 middle = edge.curve->point(0.5 * (edge.range.lo + edge.range.hi));
                if (std::fabs(middle.z() - 2.0) > 1e-9) continue;
                GeometryRef ref;
                ref.kind = 4;
                ref.index = blockIndex;
                ref.featureId = surfaces.extrusions_.at(blockIndex).featureId;
                ref.point = ForgeCad::edgeReference(blockBody, e, middle);
                lid.planarRefs.append(ref);
            }
            require(lid.planarRefs.size() == 4, "quattro bordi superiori");
            lid.name = QStringLiteral("Coperchio");
            require(surfaces.createBody(lid).isEmpty(), "superficie planare dai bordi");
            require(std::fabs(area(surfaces.extrusions_.back()) - 12.0) < 1e-9, "area del coperchio");

            // Undo/Redo della creazione e salvataggio/lettura nel formato 24.
            const int count = int(surfaces.extrusions_.size());
            surfaces.undo();
            require(surfaces.extrusions_.size() == count - 1, "annulla la superficie planare");
            surfaces.redo();
            require(surfaces.extrusions_.size() == count && surfaces.extrusions_.back().forgeBody, "ripete la superficie planare");
            QTemporaryDir directory;
            const QString file = directory.filePath(QStringLiteral("superfici24.prt"));
            require(saveDocumentFile(file, surfaces.currentDocument(), false).isEmpty(), "salvataggio formato 24");
            DocumentState reloaded;
            require(loadDocumentFile(file, reloaded).isEmpty(), "lettura formato 24");
            require(reloaded.extrusions.at(1).ruledFirst.size() == 1 && reloaded.extrusions.at(1).ruledSecond.size() == 1
                        && reloaded.extrusions.at(2).loftSurface && reloaded.extrusions.at(3).sweepSurface
                        && reloaded.extrusions.back().planarRefs.size() == 4,
                    "campi delle superfici nel documento");
            CadViewport again;
            again.loadDocument(reloaded);
            for (int k = 0; k < again.extrusions_.size(); ++k)
                require(again.extrusions_.at(k).forgeBody != nullptr, "superfici rigenerate dopo la lettura");
            require(std::fabs(area(again.extrusions_.at(1)) - frustum) < 1e-8 && std::fabs(area(again.extrusions_.back()) - 12.0) < 1e-9,
                    "aree dopo la lettura");
        }
        // Rivoluzione di un profilo aperto (superficie) e bordo libero intero
        // di una superficie come loop per la planare.
        {
            using namespace ForgeCad::Kernel;
            CadViewport v;
            const auto addSegment = [](SketchObject &sketch, QPointF a, QPointF b) {
                sketch.segments.append({a, b});
                sketch.constraints.append(-1);
                sketch.segmentLengths.append(0.0);
                sketch.segmentAngles.append(-1.0);
            };
            const auto area = [](const ExtrusionObject &body) {
                double total = 0.0;
                for (FaceId f : body.forgeBody->faces()) total += faceArea(*body.forgeBody, f);
                return total;
            };
            SketchObject profile;
            profile.name = QStringLiteral("Profilo");
            profile.plane = 1;
            addSegment(profile, {0, 0}, {0, 4});  // asse
            profile.constructionSegments.append(0);
            addSegment(profile, {1, 0}, {2, 0});  // corona piana
            addSegment(profile, {2, 0}, {2, 3});  // cilindro
            v.sketches_.append(profile);
            require(v.createRevolution(0, 0, 360.0, QStringLiteral("Vaso")).isEmpty(), "rivoluzione di un profilo aperto");
            const ExtrusionObject &vase = v.extrusions_.back();
            require(vase.forgeBody->isSheet() && !vase.solid && std::fabs(area(vase) - 15.0 * M_PI) < 1e-8, "superficie di rivoluzione: area 15 pi");
            require(v.createRevolution(0, 0, 90.0, QStringLiteral("Quarto")).isEmpty(), "rivoluzione parziale di un profilo aperto");
            require(std::fabs(area(v.extrusions_.back()) - 15.0 * M_PI / 4.0) < 1e-8, "superficie di rivoluzione parziale");
            // Rivoluzione con l'asse scelto nella vista (l'asse Z del modello,
            // nel piano XZ dello schizzo) e fusa con i solidi come l'estrusione.
            {
                CadViewport r;
                SketchObject ring;
                ring.name = QStringLiteral("Anello");
                ring.plane = 1;
                addSegment(ring, {1, 0}, {2, 0});
                addSegment(ring, {2, 0}, {2, 3});
                addSegment(ring, {2, 3}, {1, 3});
                addSegment(ring, {1, 3}, {1, 0});
                r.sketches_.append(ring);
                ExtrusionObject first;
                first.name = QStringLiteral("Anello");
                first.feature = BodyFeature::Revolution;
                first.sketchIndex = 0;
                first.plane = 1;
                first.revolveAxis = kRevolveAxisReference;
                first.revolveAxisRef.kind = 2;
                first.revolveAxisRef.index = 2;
                require(r.createBody(first).isEmpty(), "rivoluzione attorno a un asse del modello");
                require(std::fabs(massProperties(*r.extrusions_.back().forgeBody).volume - 9.0 * M_PI) < 1e-8, "volume dell'anello: 9 pi");
                ExtrusionObject outside = first;
                outside.revolveAxisRef.index = 1;  // l'asse Y e' normale al piano dello schizzo
                CadViewport probe;
                probe.sketches_ = r.sketches_;
                require(!probe.createBody(outside).isEmpty() && probe.extrusions_.isEmpty(), "un asse fuori dal piano dello schizzo e' un errore");
                SketchObject flange;
                flange.name = QStringLiteral("Flangia");
                flange.plane = 1;
                addSegment(flange, {0, 4}, {0, 5});
                flange.constructionSegments.append(0);
                addSegment(flange, {1.5, 1}, {3, 1});
                addSegment(flange, {3, 1}, {3, 2});
                addSegment(flange, {3, 2}, {1.5, 2});
                addSegment(flange, {1.5, 2}, {1.5, 1});
                r.sketches_.append(flange);
                ExtrusionObject second;
                second.name = QStringLiteral("Flangia");
                second.feature = BodyFeature::Revolution;
                second.sketchIndex = 1;
                second.plane = 1;
                second.revolveAxis = 0;
                second.mergeOperation = 1;
                second.mergeAuto = true;
                require(r.createBody(second).isEmpty(), "rivoluzione unita al solido toccato");
                const ExtrusionObject &united = r.extrusions_.back();
                require(united.mergeBodies == QVector<int>{0} && !r.extrusions_.first().visible, "il solido unito si nasconde");
                require(std::fabs(massProperties(*united.forgeBody).volume - 14.0 * M_PI) < 1e-8, "volume dell'unione: 14 pi");
                require(ForgeCad::bodyOperands(united).contains(0), "il solido unito e' un operando");
                QTemporaryDir dir;
                const QString path = dir.filePath(QStringLiteral("rivoluzione.prt"));
                require(saveDocumentFile(path, r.currentDocument(), false).isEmpty(), "salvataggio delle rivoluzioni");
                DocumentState loaded;
                require(loadDocumentFile(path, loaded).isEmpty() && loaded.extrusions.size() == 2
                            && loaded.extrusions.first().revolveAxis == kRevolveAxisReference
                            && loaded.extrusions.first().revolveAxisRef.kind == 2 && loaded.extrusions.first().revolveAxisRef.index == 2
                            && loaded.extrusions.back().mergeOperation == 1,
                        "asse della vista e fusione nel documento");
            }
            // Il bordo del quarto e' un solo loop: da uno spigolo qualsiasi tutti gli spigoli di bordo.
            const Body &quarter = *v.extrusions_.back().forgeBody;
            int laminar = 0;
            EdgeId some;
            for (EdgeId e : quarter.edges())
                if (quarter.isLaminar(e)) ++laminar, some = e;
            require(ForgeCad::freeBoundaryLoop(quarter, some).size() == laminar, "loop del bordo libero del quarto");

            // Planare chiusa sul bordo esterno di una lastra scelto da un solo spigolo.
            SketchObject plate;
            plate.name = QStringLiteral("Lastra");
            addSegment(plate, {0, 0}, {4, 0});
            addSegment(plate, {4, 0}, {4, 3});
            addSegment(plate, {4, 3}, {0, 3});
            addSegment(plate, {0, 3}, {0, 0});
            v.sketches_.append(plate);
            ExtrusionObject planarPlate;
            planarPlate.feature = BodyFeature::PlanarSurface;
            planarPlate.sketchIndex = 1;
            planarPlate.name = QStringLiteral("Lastra piana");
            require(v.createBody(planarPlate).isEmpty(), "lastra piana");
            const int plateIndex = int(v.extrusions_.size()) - 1;
            const Body &plateBody = *v.extrusions_.at(plateIndex).forgeBody;
            EdgeId first = plateBody.edges().front();
            const QVector<EdgePoint> loop = ForgeCad::freeBoundaryLoop(plateBody, first);
            require(loop.size() == 4, "bordo libero della lastra: quattro spigoli");
            ExtrusionObject lid;
            lid.feature = BodyFeature::PlanarSurface;
            lid.sketchIndex = -1;
            for (const EdgePoint &point : loop) {
                GeometryRef ref;
                ref.kind = 4;
                ref.index = plateIndex;
                ref.featureId = v.extrusions_.at(plateIndex).featureId;
                ref.point = point;
                lid.planarRefs.append(ref);
            }
            lid.name = QStringLiteral("Chiusura");
            require(v.createBody(lid).isEmpty(), "planare dal loop del bordo libero");
            require(std::fabs(area(v.extrusions_.back()) - 12.0) < 1e-9, "area della chiusura dal loop");
        }
        // Eliminazione di facce (formato 25) e corpi offerti alle booleane.
        {
            using namespace ForgeCad::Kernel;
            // Il pannello non deve isolare/riaccendere i candidati: un corpo
            // nascosto che racchiude quello da modificare deve restare fuori
            // sia dal disegno sia dal picking. Il corpo deriva dalla faccia,
            // quindi nel pannello non compare alcuna combo di selezione.
            {
                QMainWindow window;
                auto *picker = new CadViewport(&window);
                window.setCentralWidget(picker);
                PrimitiveParameters outer;
                outer.size[0] = outer.size[1] = outer.size[2] = 4.0;
                PrimitiveParameters inner;
                inner.origin[0] = inner.origin[1] = inner.origin[2] = 1.0;
                inner.size[0] = inner.size[1] = inner.size[2] = 1.0;
                require(picker->createPrimitive(outer, QStringLiteral("Esterno")).isEmpty()
                            && picker->createPrimitive(inner, QStringLiteral("Interno")).isEmpty(),
                        "corpi per il pannello elimina facce");
                picker->setModelBodyVisible(0, false);
                ExtrusionObject definition;
                definition.feature = BodyFeature::DeleteFace;
                definition.firstBody = 0;
                bool inspected = false, noBodySelector = false, visibilityPreserved = false, ownerFromFace = false;
                QTimer::singleShot(0, &window, [&] {
                    auto *panel = dynamic_cast<FunctionDialogPanel *>(window.findChild<QDialog *>());
                    inspected = panel != nullptr;
                    if (!panel) return;
                    noBodySelector = panel->findChildren<QComboBox *>().isEmpty();
                    visibilityPreserved = !picker->extrusions_.at(0).visible && picker->extrusions_.at(1).visible
                        && picker->pickBodies_.isEmpty() && !picker->referenceBodyEligible(0) && picker->referenceBodyEligible(1);
                    const Body &innerBody = *picker->extrusions_.at(1).forgeBody;
                    const FaceId face = innerBody.faces().front();
                    const FinId fin = innerBody.loop(innerBody.face(face).loops.front()).first;
                    GeometryRef ref;
                    ref.kind = 5;
                    ref.index = 1;
                    ref.point = ForgeCad::faceReference(innerBody, face, innerBody.finPoint(fin, 0.5));
                    picker->refPickFinished_(true, ref);
                    ownerFromFace = picker->refMarks_.size() == 1 && picker->refMarks_.first().index == 1;
                    panel->reject();
                });
                require(!offsetDialog(&window, picker, QStringLiteral("Elimina facce"), -1, definition,
                                      [](const ExtrusionObject &) { return QString(); }),
                        "annullamento del pannello elimina facce");
                require(inspected && noBodySelector && visibilityPreserved && ownerFromFace,
                        "elimina facce conserva la visibilita' e sceglie il corpo dalla faccia");
            }
            CadViewport v;
            PrimitiveParameters block;
            block.size[0] = 4.0;
            block.size[1] = 3.0;
            block.size[2] = 2.0;
            require(v.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco da aprire");
            const Body &solid = *v.extrusions_.at(0).forgeBody;
            ExtrusionObject open;
            open.feature = BodyFeature::DeleteFace;
            open.firstBody = 0;
            for (FaceId f : solid.faces()) {
                const SurfaceProjection top = projectPoint(*solid.face(f).surface, Vec3(2.0, 1.5, 2.0));
                if (distance(solid.face(f).surface->point(top.u, top.v), Vec3(2.0, 1.5, 2.0)) < 1e-9)
                    open.offsetFaces = {ForgeCad::faceReference(solid, f, Vec3(2.0, 1.5, 2.0))};
            }
            require(open.offsetFaces.size() == 1, "faccia superiore del blocco");
            open.name = QStringLiteral("Aperto");
            require(v.createBody(open).isEmpty(), "eliminazione della faccia superiore");
            const ExtrusionObject &sheet = v.extrusions_.back();
            double total = 0.0;
            for (FaceId f : sheet.forgeBody->faces()) total += faceArea(*sheet.forgeBody, f);
            require(sheet.forgeBody->isSheet() && !sheet.solid && sheet.forgeBody->faces().size() == 5 && std::fabs(total - 40.0) < 1e-9,
                    "il solido senza una faccia diventa una superficie di area 40");
            require(sheet.modelBodyId == v.extrusions_.at(0).modelBodyId && sheet.visible && !v.extrusions_.at(0).visible,
                    "l'eliminazione e' una feature dello stesso corpo");
            // Se il risultato ha shell disconnesse, il comando della GUI le
            // espone come corpi logici distinti (non come un solo multi-body).
            {
                CadViewport separated;
                PrimitiveParameters left;
                left.size[0] = left.size[1] = left.size[2] = 1.0;
                PrimitiveParameters right = left;
                right.origin[0] = 3.0;
                require(separated.createPrimitive(left, QStringLiteral("Sinistro")).isEmpty()
                            && separated.createPrimitive(right, QStringLiteral("Destro")).isEmpty(),
                        "scatole disgiunte per elimina facce");
                require(separated.createBoolean(::BooleanOperation::Union, 0, {1}, QStringLiteral("Due componenti")).isEmpty(),
                        "unione disgiunta per elimina facce");
                const Body &joined = *separated.extrusions_.at(2).forgeBody;
                const FaceId removed = joined.faces().front();
                const FinId sample = joined.loop(joined.face(removed).loops.front()).first;
                ExtrusionObject deletion;
                deletion.feature = BodyFeature::DeleteFace;
                deletion.firstBody = 2;
                deletion.offsetFaces = {ForgeCad::faceReference(joined, removed, joined.finPoint(sample, 0.5))};
                deletion.name = QStringLiteral("Separato");
                require(separated.createDeleteFaces(deletion).isEmpty(), "elimina facce separa le componenti");
                require(separated.extrusions_.size() == 5
                            && separated.extrusions_.at(3).modelBodyId != separated.extrusions_.at(4).modelBodyId
                            && separated.extrusions_.at(3).visible && separated.extrusions_.at(4).visible
                            && !separated.extrusions_.at(2).visible,
                        "le due componenti sono due corpi visibili di storyboard");
                require(separated.extrusions_.at(3).deleteComponent == 0 && separated.extrusions_.at(4).deleteComponent == 1
                            && !separated.extrusions_.at(3).display.faceIds.isEmpty()
                            && separated.extrusions_.at(3).display.faceIds.size() == separated.extrusions_.at(3).display.faceLabelPoints.size(),
                        "componenti e ID topologici della vista");
                QTemporaryDir directory;
                const QString file = directory.filePath(QStringLiteral("delete-components.prt"));
                require(saveDocumentFile(file, separated.currentDocument(), true).isEmpty(), "salvataggio elimina facce multi-corpo");
                DocumentState reloaded;
                require(loadDocumentFile(file, reloaded).isEmpty() && reloaded.extrusions.at(3).deleteComponent == 0
                            && reloaded.extrusions.at(4).deleteComponent == 1
                            && reloaded.extrusions.at(3).display.faceIds.size() == reloaded.extrusions.at(3).display.faceLabelPoints.size()
                            && !reloaded.extrusions.at(3).display.faceIds.isEmpty(),
                        "lettura componenti e ID topologici");
            }
            // Guscio del blocco 4 x 3 x 2 aperto in alto, spessore 0.25: cavita' 3.5 x 2.5 x 1.75.
            {
                CadViewport g;
                require(g.createPrimitive(block, QStringLiteral("Blocco")).isEmpty(), "blocco da svuotare");
                ExtrusionObject shell = open;
                shell.feature = BodyFeature::Shell;
                shell.distance = 0.25;
                shell.name = QStringLiteral("Guscio");
                // Anteprima della stessa definizione: OK ne riusa il B-rep, senza ricalcolare.
                g.requestPreview(shell, -1);
                QElapsedTimer previewTimeout;
                previewTimeout.start();
                while (!(g.preview_.valid || !g.preview_.error.isEmpty()) && previewTimeout.elapsed() < 60000) QApplication::processEvents();
                require(g.preview_.valid && g.preview_.geometry, "anteprima del guscio");
                const ForgeBody shellPreview = g.preview_.geometry;
                require(g.createBody(shell).isEmpty(), "guscio del blocco");
                require(g.extrusions_.back().forgeBody == shellPreview && !g.extrusions_.back().display.vertices.isEmpty(),
                        "la conferma del guscio riusa il B-rep e la tassellazione dell'anteprima");
                // Modifica con un altro spessore mentre l'anteprima e' ancora in corso: si aspetta quella.
                ExtrusionObject thicker = g.extrusions_.back();
                thicker.distance = 0.3;
                g.requestPreview(thicker, 1);
                require(g.updateBody(1, thicker).isEmpty() && g.preview_.valid && g.extrusions_.at(1).forgeBody == g.preview_.geometry,
                        "la modifica aspetta l'anteprima in corso e ne riusa il risultato");
                require(std::fabs(massProperties(*g.extrusions_.at(1).forgeBody).volume - (24.0 - 3.4 * 2.4 * 1.7)) < 1e-8, "guscio piu' spesso");
                thicker.distance = 0.25;
                require(g.updateBody(1, thicker).isEmpty(), "spessore di partenza");
                g.clearPreview();
                const ExtrusionObject &hollow = g.extrusions_.back();
                require(hollow.solid && std::fabs(massProperties(*hollow.forgeBody).volume - (24.0 - 3.5 * 2.5 * 1.75)) < 1e-8
                            && hollow.modelBodyId == g.extrusions_.at(0).modelBodyId,
                        "guscio: volume e stesso corpo");
                QTemporaryDir shellDirectory;
                const QString shellFile = shellDirectory.filePath(QStringLiteral("guscio25.prt"));
                require(saveDocumentFile(shellFile, g.currentDocument(), false).isEmpty(), "salvataggio del guscio");
                DocumentState shellReloaded;
                require(loadDocumentFile(shellFile, shellReloaded).isEmpty() && shellReloaded.extrusions.back().feature == BodyFeature::Shell
                            && shellReloaded.extrusions.back().distance == 0.25,
                        "lettura del guscio");
            }
            // Superficie tra curve sul bordo libero della scatola aperta (scelto da un solo spigolo).
            const int openIndex = int(v.extrusions_.size()) - 1;
            EdgeId rim;
            for (EdgeId e : sheet.forgeBody->edges())
                if (sheet.forgeBody->isLaminar(e)) rim = e;
            ExtrusionObject lid;
            lid.feature = BodyFeature::BoundarySurface;
            for (const EdgePoint &point : ForgeCad::freeBoundaryLoop(*sheet.forgeBody, rim)) {
                GeometryRef ref;
                ref.kind = 4;
                ref.index = openIndex;
                ref.featureId = v.extrusions_.at(openIndex).featureId;
                ref.point = point;
                lid.planarRefs.append(ref);
            }
            lid.name = QStringLiteral("Coperchio");
            require(lid.planarRefs.size() == 4 && v.createBody(lid).isEmpty(), "superficie tra curve sul bordo libero");
            double lidArea = 0.0;
            for (FaceId f : v.extrusions_.back().forgeBody->faces()) lidArea += faceArea(*v.extrusions_.back().forgeBody, f);
            require(v.extrusions_.back().forgeBody->isSheet() && std::fabs(lidArea - 12.0) < 1e-8, "superficie tra curve: area del coperchio");
            QTemporaryDir directory;
            const QString file = directory.filePath(QStringLiteral("facce25.prt"));
            require(saveDocumentFile(file, v.currentDocument(), false).isEmpty(), "salvataggio formato 25");
            DocumentState reloaded;
            require(loadDocumentFile(file, reloaded).isEmpty() && reloaded.extrusions.at(1).feature == BodyFeature::DeleteFace
                        && reloaded.extrusions.at(1).offsetFaces.size() == 1 && reloaded.extrusions.back().feature == BodyFeature::BoundarySurface
                        && reloaded.extrusions.back().planarRefs.size() == 4,
                    "lettura dell'eliminazione delle facce e della superficie tra curve");

            // Booleane: solo i corpi che esistono in quel punto della storia.
            CadViewport b;
            PrimitiveParameters second = block;
            second.origin[0] = 2.0;
            require(b.createPrimitive(block, QStringLiteral("A")).isEmpty() && b.createScale(0, 1.0, 0, {}, QStringLiteral("Scala A")).isEmpty()
                        && b.createPrimitive(second, QStringLiteral("B")).isEmpty(),
                    "due corpi, il primo con due stadi");
            require(b.resultBodiesBefore(-1) == QVector<int>({1, 2}), "corpi risultanti: lo stadio finale di A e B");
            require(b.createBoolean(::BooleanOperation::Union, 1, {2}, QStringLiteral("Unione")).isEmpty(), "unione dei due corpi");
            require(b.resultBodiesBefore(-1) == QVector<int>({3}), "dopo l'unione resta un solo corpo");
            require(b.resultBodiesBefore(3) == QVector<int>({1, 2}), "prima dell'unione i due corpi");
        }
        // Revisione delle superfici: riferimenti alle entita' dopo un'eliminazione
        // nello schizzo, feature che si staccano dal corpo di cui erano uno stadio.
        {
            const auto addSegment = [](SketchObject &sketch, QPointF a, QPointF b) {
                sketch.segments.append({a, b});
                sketch.constraints.append(-1);
                sketch.segmentLengths.append(0.0);
                sketch.segmentAngles.append(-1.0);
            };
            const auto minY = [](const ExtrusionObject &body) {
                double y = 1e300;
                for (auto v : body.forgeBody->vertices()) y = std::min(y, body.forgeBody->vertex(v).point.y());
                return y;
            };
            {
                CadViewport v;
                SketchObject s;
                s.name = QStringLiteral("S");
                addSegment(s, {10, 10}, {11, 10});  // 0: estraneo
                addSegment(s, {0, 0}, {4, 0});      // 1
                addSegment(s, {0, 3}, {4, 3});      // 2
                addSegment(s, {0, 6}, {4, 6});      // 3
                v.sketches_.append(s);
                ExtrusionObject r;
                r.feature = BodyFeature::Ruled;
                r.name = QStringLiteral("R");
                GeometryRef a, b;
                a.kind = b.kind = 7;
                a.index = b.index = 0;
                a.element = ConstraintRef{0, 1, -1};
                b.element = ConstraintRef{0, 2, -1};
                r.ruledFirst = {a};
                r.ruledSecond = {b};
                require(v.createBody(r).isEmpty(), "rigata tra due segmenti di uno schizzo");
                v.activeSketch_ = 0;
                v.sketchMode_ = true;
                v.sketchSelections_ = {SketchElementSelection{0, 0}};
                v.deleteSketchElements();
                require(v.hasPendingSketchChanges() && v.extrusions_.back().forgeBody
                            && v.extrusions_.back().error.isEmpty(),
                        "eliminazione estranea rinvia la rigenerazione fino all'uscita dallo schizzo");
                v.endSketchMode();
                const ExtrusionObject &after = v.extrusions_.back();
                require(after.ruledFirst.at(0).element.element == 0 && after.ruledSecond.at(0).element.element == 1 && after.forgeBody
                            && std::fabs(minY(after)) < 1e-12,
                        "la rigata segue le entita' rinumerate");
                v.sketchMode_ = true;
                v.sketchSelections_ = {SketchElementSelection{0, 0}};
                v.deleteSketchElements();
                require(v.extrusions_.back().ruledFirst.at(0).element.element == -1
                            && v.hasPendingSketchChanges() && v.extrusions_.back().error.isEmpty(),
                        "riferimento eliminato subito, errore geometrico differito durante lo schizzo");
                v.endSketchMode();
                require(v.extrusions_.back().ruledFirst.at(0).element.element == -1 && !v.extrusions_.back().error.isEmpty(),
                        "entita' eliminata: la rigata va in errore invece di passare a un'altra");
            }
            {
                CadViewport v;
                SketchObject circle;
                circle.name = QStringLiteral("C");
                circle.plane = kFacePlane;
                CurveObject c;
                c.tool = DrawingTool::Circle;
                c.controlPoints = {QPointF(0, 0), QPointF(0.5, 0)};
                ForgeCad::recalculateCurve(c, 1);
                circle.curves.append(c);
                v.sketches_.append(circle);
                SketchObject path;
                path.name = QStringLiteral("P");
                path.plane = 1;
                addSegment(path, {0, 0}, {0, 3});
                v.sketches_.append(path);
                PrimitiveParameters box;
                box.size[0] = 4;
                box.size[1] = 3;
                box.size[2] = 1;
                box.origin[0] = -2;
                box.origin[1] = -1.5;
                box.origin[2] = 0.7;
                require(v.createPrimitive(box, QStringLiteral("Box")).isEmpty(), "blocco per la sweep fusa");
                ExtrusionObject sw;
                sw.feature = BodyFeature::Sweep;
                sw.sketchIndex = 0;
                sw.pathSketch = 1;
                sw.name = QStringLiteral("Sw");
                sw.mergeOperation = 1;
                sw.mergeAuto = false;
                sw.mergeBodies = {0};
                require(v.createBody(sw).isEmpty() && !v.extrusions_.at(0).visible, "sweep fusa nel blocco");
                ExtrusionObject edited = v.extrusions_.at(1);
                edited.sweepSurface = true;
                edited.mergeOperation = 0;
                edited.mergeBodies.clear();
                require(v.updateBody(1, edited).isEmpty(), "sweep resa superficie");
                require(v.extrusions_.at(0).visible && v.extrusions_.at(1).visible
                            && v.extrusions_.at(0).modelBodyId != v.extrusions_.at(1).modelBodyId,
                        "il blocco torna un corpo visibile, la superficie un corpo a parte");
            }
            {
                CadViewport v;
                for (int k = 0; k < 3; ++k) {
                    SketchObject s;
                    s.name = QStringLiteral("Q%1").arg(k);
                    const double x = 3.0 * k;
                    addSegment(s, {x, 0}, {x + 1, 0});
                    addSegment(s, {x + 1, 0}, {x + 1, 1});
                    addSegment(s, {x + 1, 1}, {x, 1});
                    addSegment(s, {x, 1}, {x, 0});
                    v.sketches_.append(s);
                    ExtrusionObject p;
                    p.feature = BodyFeature::PlanarSurface;
                    p.sketchIndex = k;
                    p.name = QStringLiteral("PL%1").arg(k);
                    require(v.createBody(p).isEmpty(), "planare per la cucitura");
                }
                ExtrusionObject sew;
                sew.feature = BodyFeature::Sew;
                sew.firstBody = 0;
                sew.booleanTools = {1, 2};
                sew.sewSolid = false;
                sew.name = QStringLiteral("Sew");
                require(v.createBody(sew).isEmpty(), "cucitura di tre superfici");
                ExtrusionObject edited = v.extrusions_.at(3);
                edited.firstBody = 1;
                edited.booleanTools = {2};
                require(v.updateBody(3, edited).isEmpty(), "cucitura senza la prima superficie");
                require(v.extrusions_.at(0).visible && !v.extrusions_.at(1).visible
                            && v.extrusions_.at(3).modelBodyId == v.extrusions_.at(1).modelBodyId,
                        "la superficie tolta torna visibile, la cucitura passa al corpo della nuova prima");
            }
        }
        // Calcolo lungo dei corpi: va in un thread, il ciclo di eventi continua
        // (timer e ridisegni) e la finestra di avanzamento compare dopo il
        // ritardo e si chiude alla fine; le eccezioni tornano al chiamante.
        {
            CadViewport responsive;
            responsive.resize(320, 240);
            responsive.show();
            int starts = 0, ends = 0;
            QString shownMessage;
            responsive.setWorkCallback([&](bool begin, const QString &message, bool background) {
                if (background) return;
                (begin ? starts : ends)++;
                if (begin) shownMessage = message;
            });
            int ticks = 0;
            QTimer ticker;
            QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
            ticker.start(20);
            {
                CadViewport::DeferredWork work(&responsive, QStringLiteral("Rigenerazione di prova..."));
                responsive.runWhileResponsive([] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); });
                require(starts == 1 && ends == 0 && shownMessage == QStringLiteral("Rigenerazione di prova..."),
                        "avanzamento mostrato durante un calcolo lungo");
            }
            require(ends == 1, "avanzamento chiuso alla fine del calcolo");
            require(ticks >= 5, "ciclo di eventi attivo durante il calcolo nel thread");
            {
                CadViewport::DeferredWork work(&responsive, QStringLiteral("Breve"));
                responsive.runWhileResponsive([] {});
            }
            require(starts == 1, "nessun avanzamento per i calcoli brevi");
            bool thrown = false;
            try {
                responsive.runWhileResponsive([] { throw std::runtime_error("prova"); });
            } catch (const std::runtime_error &) {
                thrown = true;
            }
            require(thrown, "eccezione del thread riportata al chiamante");
            ticker.stop();
            responsive.close();
        }
        // Regressione del documento Applicatore: la sottrazione dello sweep
        // attraversa piu' volte la cucitura parametrica del cilindro. La mesh
        // non deve riempire il lato opposto del trim ne' scambiare i salti di
        // piu' periodi per lati collassati.
        {
            DocumentState applicator;
            const QString path = exampleDocumentPath("Applicatore.prt");
            require(loadDocumentFile(path, applicator).isEmpty() && !applicator.extrusions.isEmpty(), "lettura di Applicatore.prt");
            const ForgeBody result = applicator.extrusions.back().forgeBody;
            require(bool(result), "B-rep finale di Applicatore nello snapshot");
            Kernel::TessellationOptions options;
            options.deflection = 0.02;
            options.angle = 0.25;
            const Kernel::Tessellation mesh = Kernel::tessellate(*result, options);
            require(mesh.failedFaces == 0, "tassellazione del risultato di Applicatore");
            int sampled = 0;
            for (const Kernel::FaceMesh &face : mesh.faces) {
                const Kernel::Surface &surface = *result->face(face.face).surface;
                for (std::size_t i = 0; i < face.points.size(); ++i) {
                    const Kernel::Vec2 uv = face.parameters[i];
                    require(Kernel::distance(face.points[i], surface.point(uv.x(), uv.y())) <= 1e-5,
                            "Applicatore: vertice della mesh fuori dalla superficie");
                }
                if (surface.type() != Kernel::SurfaceType::Cylinder) continue;
                for (std::size_t i = 0; i < face.triangles.size(); ++i) {
                    const std::array<int, 3> &triangle = face.triangles[i];
                    double longest = 0.0;
                    for (int k = 0; k < 3; ++k)
                        longest = std::max(longest, Kernel::distance(face.points[std::size_t(triangle[k])],
                            face.points[std::size_t(triangle[(k + 1) % 3])]));
                    if (i % 200 != 0 && longest <= 1.0) continue;
                    const int divisions = longest > 1.0 ? 8 : 3;
                    for (int a = 1; a < divisions; ++a)
                        for (int b = 1; a + b < divisions; ++b) {
                            const int c = divisions - a - b;
                            const Kernel::Vec3 point = (a * face.points[std::size_t(triangle[0])]
                                                      + b * face.points[std::size_t(triangle[1])]
                                                      + c * face.points[std::size_t(triangle[2])]) / double(divisions);
                            const Kernel::SurfaceProjection on = Kernel::projectPoint(surface, point);
                            if (Kernel::classifyPointOnFace(*result, face.face, on.point, 1e-6) == Kernel::PointLocation::Outside)
                                throw std::runtime_error("Applicatore: triangolo fuori dal trim della faccia "
                                                         + std::to_string(face.face.index));
                            ++sampled;
                        }
                }
            }
            require(sampled > 0, "facce di Applicatore campionate");
        }
        // Regressione del flacone: il contatto terminale del raccordo cade
        // presso una cucitura della superficie e Newton, con un solo seme,
        // sceglieva il ramo sbagliato della curva chiusa.
        {
            // Snapshot fisso della Differenza 19 prima dei raccordi. La ricerca
            // a ritroso nel documento modificabile sceglieva anche corpi gia'
            // raccordati; conteggi e punti del vecchio esempio non erano stabili.
            QFile fixture(QStringLiteral(FORGECAD_SOURCE_DIR "/kernel/tests/data/flacone_pre_blend.body.qz"));
            require(fixture.open(QIODevice::ReadOnly), "apertura fixture flacone prima dei raccordi");
            const auto bytes = qUncompress(fixture.readAll());
            require(!bytes.isEmpty(), "decompressione fixture flacone");
            const Kernel::Body base = Kernel::readBodyBinary(std::string(bytes.constData(), std::size_t(bytes.size())));
            require(base.faces().size() == 29 && base.edges().size() == 40 && Kernel::checkBody(base).empty(),
                    "topologia della fixture flacone prima dei raccordi");
            const Kernel::EdgeId singular = Kernel::nearestEdge(base, Kernel::Vec3(0.0, 13.6, 105.285), 1e-2);
            require(singular.valid(), "bordo singolare del flacone");
            const Kernel::Body rounded = Kernel::blendEdges(base, {singular}, 0.6, false);
            require(Kernel::checkBody(rounded).empty() && rounded.faces().size() == 32 && rounded.edges().size() == 45,
                    "raccordo da 0,6 mm sulla cucitura del flacone");
            const Kernel::EdgeId threadEnd = Kernel::nearestEdge(base, Kernel::Vec3(0.0, 13.6, 108.034), 1e-2);
            require(threadEnd.valid(), "bordo del fine-filetto del flacone");
            const Kernel::Body joined = Kernel::blendEdges(base, {singular, threadEnd}, 0.3, false);
            require(Kernel::checkBody(joined).empty() && joined.faces().size() == 35 && joined.edges().size() == 50,
                    "raccordo congiunto da 0,3 mm su elica e fine-filetto");
            const Kernel::EdgeId otherThreadEnd = Kernel::nearestEdge(base, Kernel::Vec3(0.0, 13.6, 101.678), 1e-2);
            require(otherThreadEnd.valid(), "secondo bordo del fine-filetto del flacone");
            const Kernel::Body joinedEnds = Kernel::blendEdges(base, {singular, threadEnd, otherThreadEnd}, 0.3, false);
            require(Kernel::checkBody(joinedEnds).empty(), "raccordo congiunto sui due estremi del filetto");
            const auto edgeNear = [&](const Kernel::Vec3 &point, const char *message) {
                const Kernel::EdgeId edge = Kernel::nearestEdge(base, point, 1e-2);
                require(edge.valid(), message);
                return edge;
            };
            const Kernel::EdgeId lowerHelix = edgeNear(Kernel::Vec3(0.0, 13.6, 104.427), "elica inferiore del filetto");
            const Kernel::EdgeId lowerTrimA = edgeNear(Kernel::Vec3(-2.506, 12.987, 107.704), "primo trim inferiore del filetto");
            const Kernel::EdgeId lowerTrimB = edgeNear(Kernel::Vec3(2.528, 12.967, 101.153), "secondo trim inferiore del filetto");
            const Kernel::Body lower = Kernel::blendEdges(base, {lowerHelix, lowerTrimA, lowerTrimB}, 0.3, false);
            require(Kernel::checkBody(lower).empty() && lower.faces().size() == 32 && lower.edges().size() == 47,
                    "raccordo del bordo inferiore attraverso i trim del collo");
            const Kernel::EdgeId upperTrimA = edgeNear(Kernel::Vec3(-2.478, 12.984, 108.560), "primo trim superiore del filetto");
            const Kernel::EdgeId upperTrimB = edgeNear(Kernel::Vec3(2.54383, 12.9486, 102.010), "secondo trim superiore del filetto");
            const Kernel::Body upper = Kernel::blendEdges(base, {singular, upperTrimA, upperTrimB}, 0.3, false);
            require(Kernel::checkBody(upper).empty() && upper.faces().size() == 32 && upper.edges().size() == 47,
                    "raccordo del bordo superiore attraverso i trim del collo");

            // La parete del collo e' un cilindro con piu' loop e bordi,
            // trimmato dalle filettature. Una nuova faccia cilindrica nasce
            // invece con due soli cerchi: la cucitura deve imprimerle i loop
            // liberi dell'apertura prima di tentare di chiudere il solido.
            Kernel::FaceId neck;
            int mostEdges = 0;
            for (Kernel::FaceId f : base.faces()) {
                if (base.face(f).surface->type() != Kernel::SurfaceType::Cylinder) continue;
                int count = 0;
                for (Kernel::LoopId loop : base.face(f).loops) count += int(base.loopFins(loop).size());
                if (count > mostEdges) mostEdges = count, neck = f;
            }
            require(neck.valid() && mostEdges > 2, "parete cilindrica trimmata del collo");
            std::vector<Kernel::FaceId> withoutNeck;
            for (Kernel::FaceId f : base.faces()) if (f != neck) withoutNeck.push_back(f);
            const Kernel::Body openBottle = Kernel::facesAsSheet(base, withoutNeck);
            const auto *cylinder = dynamic_cast<const Kernel::CylindricalSurface *>(base.face(neck).surface.get());
            require(cylinder != nullptr, "supporto cilindrico del collo");
            double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
            for (Kernel::LoopId loop : base.face(neck).loops)
                for (Kernel::FinId fin : base.loopFins(loop))
                    for (Kernel::VertexId vertex : {base.finStart(fin), base.finEnd(fin)}) {
                        const double z = Kernel::dot(base.vertex(vertex).point - cylinder->frame().origin(), cylinder->frame().zDir());
                        lo = std::min(lo, z);
                        hi = std::max(hi, z);
                    }
            const Kernel::Frame3 replacementFrame(cylinder->frame().origin() + lo * cylinder->frame().zDir(),
                                                   cylinder->frame().zDir(), cylinder->frame().yDir());
            const Kernel::Body replacementSolid = Kernel::makeCylinder(replacementFrame, cylinder->radius(), hi - lo);
            std::vector<Kernel::FaceId> replacementFaces;
            for (Kernel::FaceId f : replacementSolid.faces())
                if (replacementSolid.face(f).surface->type() == Kernel::SurfaceType::Cylinder) replacementFaces.push_back(f);
            const Kernel::Body replacement = Kernel::facesAsSheet(replacementSolid, replacementFaces);
            const Kernel::SewResult repaired = Kernel::sewSheets({&openBottle, &replacement}, 1e-5, true);
            require(repaired.closed && repaired.solid && repaired.freeEdges == 0 && Kernel::checkBody(repaired.body).empty(),
                    "ricostruzione del collo cilindrico trimmato e cucitura in solido");
        }
        // L'anteprima del raccordo conserva il B-rep esatto e la
        // tassellazione: OK deve promuoverli senza eseguire di nuovo il kernel.
        {
            CadViewport cachedBlend;
            int foregroundStarts = 0, foregroundEnds = 0, backgroundStarts = 0, backgroundEnds = 0;
            cachedBlend.setWorkCallback([&](bool begin, const QString &, bool background) {
                if (background) (begin ? backgroundStarts : backgroundEnds)++;
                else (begin ? foregroundStarts : foregroundEnds)++;
            });
            PrimitiveParameters cachedBlock;
            cachedBlock.size[0] = 4.0; cachedBlock.size[1] = 3.0; cachedBlock.size[2] = 2.0;
            require(cachedBlend.createPrimitive(cachedBlock, QStringLiteral("Base anteprima raccordo")).isEmpty(),
                    "base dell'anteprima raccordo");
            require(!forgeBlendHasEffect(cachedBlend.extrusions_.first().forgeBody, cachedBlend.extrusions_.first().forgeBody),
                    "un risultato identico non e' un raccordo riuscito");
            const ForgeBody sameTopologyChanged = std::make_shared<const Kernel::Body>(Kernel::makeBox(Kernel::Frame3(), 5.0, 3.0, 2.0));
            require(forgeBlendHasEffect(cachedBlend.extrusions_.first().forgeBody, sameTopologyChanged),
                    "una modifica geometrica resta riconosciuta anche a topologia invariata");
            require(foregroundStarts == foregroundEnds, "notifiche bilanciate per il calcolo sul thread principale");
            CadViewport loadedWithProgress;
            int loadedBodies = -1, totalBodies = -1;
            loadedWithProgress.loadDocument(cachedBlend.currentDocument(), [&](int completed, int total, const QString &) {
                loadedBodies = completed;
                totalBodies = total;
            });
            require(loadedBodies == totalBodies && totalBodies == 1,
                    "avanzamento determinato fino all'ultimo corpo durante l'apertura");

            // Uno snapshot della stessa definizione resta apribile dopo un
            // aggiornamento del kernel: la prima modifica lo rigenerera'.
            QTemporaryDir staleCacheDir;
            const QString staleCachePath = staleCacheDir.filePath(QStringLiteral("cache-precedente.prt"));
            require(saveDocumentFile(staleCachePath, cachedBlend.currentDocument(), true).isEmpty(),
                    "salvataggio del documento con cache 3D");
            QFile staleFile(staleCachePath);
            require(staleFile.open(QIODevice::ReadOnly), "lettura del documento con cache 3D");
            QByteArray staleData = staleFile.readAll();
            staleFile.close();
            QDataStream staleHeader(staleData);
            staleHeader.skipRawData(4);
            quint16 staleVersion = 0;
            quint8 staleCompression = 0;
            quint32 payloadSize = 0;
            staleHeader >> staleVersion >> staleCompression >> payloadSize;
            const qsizetype cacheOffset = 11 + qsizetype(payloadSize);
            QByteArray staleCache = qUncompress(staleData.mid(cacheOffset));
            require(staleVersion >= 14 && staleCompression == 1 && staleCache.size() > 68,
                    "blocco cache 3D presente nel documento");
            staleCache[4] = staleCache.at(4) == '0' ? '1' : '0'; // altera soltanto l'impronta dei sorgenti
            staleData.replace(cacheOffset, staleData.size() - cacheOffset, qCompress(staleCache, 6));
            require(staleFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                        && staleFile.write(staleData) == staleData.size(),
                    "scrittura della cache con impronta precedente");
            staleFile.close();
            DocumentState strictCache, previewCache;
            require(loadDocumentFile(staleCachePath, strictCache).isEmpty()
                        && loadDocumentFile(staleCachePath, previewCache, true).isEmpty(),
                    "lettura della cache precedente nei due modi");
            require(strictCache.extrusions.first().forgeBody && strictCache.extrusions.first().cachedGeometry
                        && previewCache.extrusions.first().forgeBody
                        && !strictCache.extrusions.first().display.vertices.isEmpty(),
                    "snapshot precedente disponibile senza ricalcolo e con mesh salvata");
            const QVector<EdgePoint> cachedEdge{{2.0, 0.0, 2.0}};
            cachedBlend.requestBlendPreview(0, cachedEdge, 0.25, false);
            cachedBlend.startPreviewJob();
            QElapsedTimer previewTimeout;
            previewTimeout.start();
            while (cachedBlend.previewRunning_ && previewTimeout.elapsed() < 10000) QApplication::processEvents();
            require(backgroundStarts == 1 && backgroundEnds == 1,
                    "notifiche bilanciate per il calcolo dell'anteprima in background");
            require(cachedBlend.preview_.valid && cachedBlend.preview_.geometry,
                    "B-rep esatto conservato dall'anteprima raccordo");
            require(!cachedBlend.preview_.display.vertices.isEmpty(),
                    "patch locale presente nell'anteprima raccordo");
            require(cachedBlend.preview_.resultDisplay.vertices.isEmpty() && cachedBlend.preview_.resultDisplay.edges.isEmpty(),
                    "l'anteprima nuova non tassella le facce estranee alla patch");
            require(!cachedBlend.preview_.display.constructionCurves.isEmpty(),
                    "curve U/V presenti nell'anteprima raccordo");
            for (const QVector<QVector3D> &curve : cachedBlend.preview_.display.constructionCurves)
                require(curve.size() >= 2 && (curve.first() - curve.last()).length() > 1e-5f,
                        "le curve U/V del raccordo sono trimmate sulla patch e non sono cerchi completi");
            require(cachedBlend.preview_.replaced.isEmpty(),
                    "la base opaca non viene sostituita dalla patch del raccordo");
            const ForgeBody previewGeometry = cachedBlend.preview_.geometry;
            const int patchTriangles = cachedBlend.preview_.display.vertices.size();
            require(cachedBlend.createBlend(0, cachedEdge, 0.25, false, QStringLiteral("Raccordo da anteprima")).isEmpty(),
                    "conferma dell'anteprima raccordo");
            require(cachedBlend.extrusions_.back().forgeBody == previewGeometry,
                    "la conferma riusa il B-rep dell'anteprima");
            require(cachedBlend.extrusions_.back().display.vertices.size() > patchTriangles,
                    "la conferma completa la tassellazione dell'intero risultato");
            require(forgeBlendHasEffect(cachedBlend.extrusions_.first().forgeBody, cachedBlend.extrusions_.back().forgeBody),
                    "il raccordo confermato modifica realmente la base");

            DocumentState noOpSnapshot = cachedBlend.currentDocument();
            noOpSnapshot.extrusions.back().forgeBody = noOpSnapshot.extrusions.first().forgeBody;
            noOpSnapshot.extrusions.back().display = noOpSnapshot.extrusions.first().display;
            noOpSnapshot.extrusions.back().cachedGeometry = true;
            CadViewport repairedSnapshot;
            repairedSnapshot.loadDocument(noOpSnapshot);
            require(repairedSnapshot.extrusions_.back().error.isEmpty()
                        && forgeBlendHasEffect(repairedSnapshot.extrusions_.first().forgeBody,
                                               repairedSnapshot.extrusions_.back().forgeBody),
                    "una cache no-op del raccordo viene scartata e rigenerata");

            // Modifica senza cambiare valori: l'anteprima mostra la patch con le
            // curve U/V dal body esistente (niente kernel) e la conferma lascia
            // il corpo com'e'.
            const int blendIndex = int(cachedBlend.extrusions_.size()) - 1;
            const ExtrusionObject existing = cachedBlend.extrusions_.at(blendIndex);
            require(cachedBlend.unchangedFeature(existing, blendIndex), "funzione non modificata riconosciuta");
            cachedBlend.requestBlendPreview(existing.firstBody, existing.blendEdges, existing.blendSize, existing.blendChamfer,
                                            blendIndex, existing.chamferSpec);
            cachedBlend.startPreviewJob();
            previewTimeout.restart();
            while (cachedBlend.previewRunning_ && previewTimeout.elapsed() < 10000) QApplication::processEvents();
            require(cachedBlend.preview_.valid && cachedBlend.preview_.geometry == existing.forgeBody,
                    "l'anteprima della modifica riusa il B-rep del corpo");
            require(!cachedBlend.preview_.display.vertices.isEmpty() && !cachedBlend.preview_.display.constructionCurves.isEmpty()
                        && cachedBlend.preview_.display.vertices.size() < existing.display.vertices.size(),
                    "patch in trasparenza con le curve U/V senza ricalcolo");
            require(cachedBlend.updateBody(blendIndex, existing).isEmpty()
                        && cachedBlend.extrusions_.at(blendIndex).forgeBody == existing.forgeBody,
                    "conferma senza modifiche: corpo invariato");
            // La finestra passa la seconda distanza dello smusso anche per un
            // raccordo: per i raccordi non conta.
            ExtrusionObject fromDialog = existing;
            fromDialog.chamferSpec.second = existing.blendSize + 1.0;
            fromDialog.chamferSpec.flip = true;
            require(cachedBlend.unchangedFeature(fromDialog, blendIndex), "la seconda distanza non conta per un raccordo");
            // Gli stessi spigoli scelti di nuovo nella vista (punti diversi).
            require(cachedBlend.beginBlendEdit(blendIndex, existing.blendSize, existing.blendChamfer).isEmpty(),
                    "modifica degli spigoli del raccordo");
            ExtrusionObject repicked = existing;
            repicked.blendEdges = cachedBlend.pickedEdgePoints();
            require(cachedBlend.unchangedFeature(repicked, blendIndex), "gli stessi spigoli scelti di nuovo non richiedono il calcolo");
            // Durante la modifica degli spigoli l'anteprima resta quella del
            // raccordo attuale: togliere o aggiungere spigoli non la ricalcola.
            const QString existingKey = CadViewport::previewKey(existing, blendIndex);
            require(cachedBlend.preview_.key == existingKey, "la modifica degli spigoli mostra il raccordo attuale");
            cachedBlend.pickedEdges_.clear();
            cachedBlend.pickedEdges_.append(0);
            cachedBlend.edgePicked();
            require(cachedBlend.preview_.key == existingKey, "un clic nella modifica degli spigoli non ricalcola");
            cachedBlend.cancelEdgePick();
            ExtrusionObject changed = existing;
            changed.blendSize = 0.3;
            require(!cachedBlend.unchangedFeature(changed, blendIndex), "un valore cambiato richiede il calcolo");
            cachedBlend.clearPreview();
        }
        std::cout << "Viewport: assi, piani, datum, Undo/Redo, riferimenti, vincoli, salvataggio e cache OK" << std::endl;
    }
};
#include "test_blend_history.h"
#include "../forgeCad2026_application_theme.h"
// QA facoltativa con un contesto OpenGL reale: non tocca documenti dell'utente.
static void surfaceToolsVisualQa() {
    using namespace ForgeCad; using namespace Kernel;
    const QString qa="/tmp/forgecad-surface-tools-qa";QDir().mkpath(qa);
    PdfWindow window;window.resize(1600,1000);for(auto *dock:window.findChildren<QDockWidget *>())dock->hide();
    CadViewport *view=nullptr;for(auto *child:window.findChildren<QWidget *>())if(auto *candidate=dynamic_cast<CadViewport *>(child)){view=candidate;break;}
    require(view,"viewport della finestra QA");window.show();QApplication::processEvents();require(view->isValid(),"QA visiva richiede OpenGL valido");
    for(int mode=0;mode<3;++mode){
        DocumentState document;ExtrusionObject source;source.feature=BodyFeature::Imported;source.name="Superficie NURBS";
        const auto base=surface_fixture::patch(true);source.importData=QByteArray::fromStdString(writeStep({{"NURBS",base,false,{0,0,0},nullptr,{}}}));document.extrusions.append(source);
        ExtrusionObject definition;definition.feature=BodyFeature::SurfaceTools;definition.surfaceTool.mode=mode;definition.firstBody=0;
        if(mode==0){const auto wall=makePlaneSheet(Frame3(Vec3(14,5,0),Vec3(1,0,0),Vec3(0,0,1)),8);ExtrusionObject target;target.feature=BodyFeature::Imported;target.name="Piano finale";target.importData=QByteArray::fromStdString(writeStep({{"Limite",wall,false,{0,0,0},nullptr,{}}}));document.extrusions.append(target);}
        if(mode==1){ExtrusionObject curves;curves.feature=BodyFeature::SpatialSketch;curves.name="Curve U/V";curves.spatialSketch.nextId=7;for(int i=0;i<6;++i){SpatialCurve3D entity;entity.id=i+1;entity.points=i<3?QVector<SpatialPoint>{{0.,5.*i,0.},{10.,5.*i,2.*i}}:QVector<SpatialPoint>{{5.*(i-3),0.,0.},{5.*(i-3),10.,2.*(i-3)}};curves.spatialSketch.entities.append(entity);}document.extrusions.append(curves);}
        view->loadDocument(document);
        if(mode==0){const auto shape=view->extrusions()[0].forgeBody;definition.blendEdges={edgeReference(*shape,surface_fixture::edgeNear(*shape,Vec3(10,5,0)),Vec3(10,5,0))};const auto target=view->extrusions()[1].forgeBody;definition.extentRef.kind=5;definition.extentRef.index=1;definition.extentRef.featureId=view->extrusions()[1].featureId;definition.extentRef.point=faceReference(*target,target->faces().front(),Vec3(14,5,0));definition.surfaceTool.maximum=10;}
        if(mode==1){view->setModelBodyVisible(view->modelBodyIndexForFeature(0),false);for(int i=0;i<6;++i){GeometryRef ref;ref.kind=9;ref.index=1;ref.featureId=view->extrusions()[1].featureId;ref.point.subshape=i+1;ref.point.geometry=kSpatialCurveReference;(i<3?definition.ruledFirst:definition.ruledSecond).append(ref);}}
        if(mode==2){definition.surfaceTool.basis=surfaceBasisHash(view->extrusions()[0].forgeBody);definition.surfaceTool.edits={{1,1,0,0,2,1.4}};}
        view->setReferencePlanesVisible(false);view->setAxesVisible(false);view->setGridVisible(false);view->setViewNormal(0);view->setViewPreset(4);view->fitAll();QApplication::processEvents();
        std::exception_ptr failure;bool captured=false;const auto name=QStringList{"estensione","rete","poli"}[mode];
        QTimer::singleShot(1200,&window,[&]{auto *dialog=window.findChild<QDialog *>("surfaceToolDialog");try{require(dialog,"pannello QA aperto");if(!view->previewError().isEmpty()){dialog->grab().save(qa+"/errore-"+name+".png");for(auto *box:dialog->findChildren<QComboBox *>())std::cerr<<box->objectName().toStdString()<<": "<<box->currentText().toStdString()<<"\n";}require(view->previewError().isEmpty(),qPrintable(view->previewError()));dialog->resize(mode==2?620:480,mode==2?720:650);dialog->move(16,180);const QPointF center(view->width()/2.,view->height()/2.);QMouseEvent press(QEvent::MouseButtonPress,center,center,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&press);QMouseEvent move(QEvent::MouseMove,center+QPointF(290,0),center+QPointF(290,0),Qt::NoButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&move);QMouseEvent release(QEvent::MouseButtonRelease,center+QPointF(290,0),center+QPointF(290,0),Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&release);if(mode==2)dialog->findChild<QTableWidget *>("surfacePolesTable")->selectRow(5);QApplication::processEvents();require(dialog->grab().save(qa+"/dialogo-"+name+".png"),"cattura pannello");require(view->grabFramebuffer().save(qa+"/geometria-"+name+".png"),"cattura B-rep e gabbia");require(window.grab().save(qa+"/ambiente-"+name+".png"),"cattura ambiente3D");captured=true;dialog->reject();}catch(...){failure=std::current_exception();if(dialog)dialog->reject();}});
        surfaceToolDialog(&window,view,name,-1,definition,[](const ExtrusionObject &){return QString();});if(failure)std::rethrow_exception(failure);require(captured,"QA visiva completata");
    }
    std::cout<<"PASS QA OpenGL superfici: estensione, rete, tabella poli e gabbia\n";
}

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL); format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile); format.setDepthBufferSize(24); format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--mesh-memory-benchmark"))) {
        try {
            const int at=app.arguments().indexOf(QStringLiteral("--mesh-memory-benchmark"));
            require(at+1<app.arguments().size(),"documento richiesto per la sonda memoria");
            ViewportInteractionTest::meshMemoryBenchmark(app.arguments()[at+1],app.arguments().contains(QStringLiteral("--gl")));
        } catch(const std::exception &error) { std::cerr<<error.what()<<std::endl;return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-mesh-benchmark"))) {
        QTemporaryDir isolatedSettings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,isolatedSettings.path());
        QCoreApplication::setOrganizationName(QStringLiteral("ForgeCADBenchmark"));
        QCoreApplication::setApplicationName(QStringLiteral("DocumentMesh"));
        try {
            if(app.arguments().contains(QStringLiteral("--require-cuda")) && !ForgeCad::cudaTessellationAvailable()) {
                std::cerr<<"CUDA non disponibile: snapshot GPU saltato"<<std::endl;return 77;
            }
            ForgeCad::initializeGeometryAcceleration();
            const int snapshotIndex=app.arguments().indexOf(QStringLiteral("--mesh-snapshot-dir"));
            const auto snapshotDirectory=snapshotIndex<0?QString():app.arguments().value(snapshotIndex+1);
            if(snapshotIndex>=0 && (snapshotDirectory.isEmpty() || snapshotDirectory.startsWith(QStringLiteral("--"))))
                throw std::runtime_error("specificare la directory degli snapshot numerici");
            const int index=app.arguments().indexOf(QStringLiteral("--document-mesh-benchmark"));
            ViewportInteractionTest::documentMeshBenchmark(app.arguments().value(index+1),
                app.arguments().contains(QStringLiteral("--inventory")),
                app.arguments().contains(QStringLiteral("--rebuild")),snapshotDirectory);
        } catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--lazy-mesh"))) {
        try { ViewportInteractionTest::lazyMeshTests(); }
        catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-solver-benchmark"))) {
        try { documentSolverBenchmark(app.arguments().value(app.arguments().indexOf(QStringLiteral("--document-solver-benchmark"))+1),
            app.arguments().contains(QStringLiteral("--admissible-targets"))); }
        catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--solver-partition"))) {
        try { solverPartitionTests(app.arguments().contains(QStringLiteral("--benchmark"))); }
        catch(const std::exception &error) { std::cerr<<error.what()<<std::endl;return 1; }
        return 0;
    }
    ForgeCad::initializeApplicationTheme(app);
    QCoreApplication::setOrganizationName(QStringLiteral("ForgeCADTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Viewport"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    qputenv("FORGECAD_RECOVERY_DIR",settings.filePath(QStringLiteral("recovery")).toUtf8());
    if(app.arguments().contains(QStringLiteral("--recovery-crash-worker")))return ViewportInteractionTest::recoveryCrashWorker(app.arguments().last());
    if(app.arguments().contains(QStringLiteral("--document-recovery"))){
        try{ViewportInteractionTest::documentRecoveryRegressions();}
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--pattern-placements"))) {
        try { patternPlacementTests(); }
        catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-read")) || app.arguments().contains(QStringLiteral("--document-read-memory"))) {
        try {
            if (app.arguments().contains(QStringLiteral("--document-read-memory"))) documentReadMemoryTest();
            else documentReadTests();
        } catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        std::cout << "PASS document read" << std::endl; return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-consultation"))) {
        try { ViewportInteractionTest::documentConsultation(); }
        catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}
        std::cout<<"PASS concurrent consultation and exclusive editing"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-lock-worker"))) {
        const int index=app.arguments().indexOf(QStringLiteral("--document-lock-worker"));
        const QString path=app.arguments().value(index+1);
        if(app.arguments().contains(QStringLiteral("--save")))
            return ForgeCad::saveDocumentFile(path,DocumentState{},false).isEmpty() ? 0 : 2;
        ForgeCad::DocumentFileLock lock(path);
        const QString error=lock.acquire();
        if(!error.isEmpty()) {std::cout<<"LOCKED"<<std::endl;return 2;}
        std::cout<<"READY"<<std::endl;
        std::cin.get();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sweep-references"))) {
        try { ViewportInteractionTest::sweepReferences(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if(app.arguments().contains(QStringLiteral("--drawing-performance"))){
        try{drawingPerformanceTest(app.arguments().value(app.arguments().indexOf(QStringLiteral("--drawing-performance"))+1));}
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;
    }
    if(app.arguments().contains(QStringLiteral("--drawing-section-model"))){
        try{drawingSectionModelTest(app.arguments().value(app.arguments().indexOf(QStringLiteral("--drawing-section-model"))+1));}
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;
    }
    if(app.arguments().contains(QStringLiteral("--surface-tools-visual"))){try{surfaceToolsVisualQa();}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;}
    if(app.arguments().contains(QStringLiteral("--surface-tools"))){try{ViewportInteractionTest::surfaceTools();}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;}
    if(app.arguments().contains(QStringLiteral("--catalog-recovery"))){try{ViewportInteractionTest::catalogRecoveryGui();}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;}
    if(app.arguments().contains(QStringLiteral("--drawing-release"))){try{drawingReleaseTests();}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;}
    if(app.arguments().contains(QStringLiteral("--drawing-restore"))){try{drawingRevisionRestoreTests();}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;}
    if(app.arguments().contains(QStringLiteral("--drawing-multisheet"))){try{multisheetTests();}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}return 0;}
    if (app.arguments().contains(QStringLiteral("--projected-drawings"))) {
        try { projectedDrawingFileTests(); }
        catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
        std::cout<<"PASS projected drawing files and revisions"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--drawing-revisions"))) {
        try { drawingRevisionTests();drawingRevisionUiTests(); }
        catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
        std::cout<<"PASS drawing revisions, recovery and historical GUI"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--linked-drawings"))) {
        try { linkedDrawingTests(); ViewportInteractionTest::linkedDrawingUi(); }
        catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
        std::cout<<"PASS linked lightweight drawings and GUI"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-access"))) {
        try { ViewportInteractionTest::documentAccess(); }
        catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sketch-ghost-render"))) {
        try { ViewportInteractionTest::sketchGhostRender(); }
        catch (const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
        std::cout<<"PASS sketch ghost rendering"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--async-lifetime"))) {
        try { ViewportInteractionTest::asyncLifetimeRegressions(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--mouse-snapshot"))) {
        try { ViewportInteractionTest::mouseSnapshotRegression(app.arguments().value(app.arguments().indexOf(QStringLiteral("--mouse-snapshot")) + 1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--workspace-revisions"))) {
        try { workspaceRevisionTests(); ViewportInteractionTest::workspaceRevisionsUi(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS workspace document revisions and GUI" << std::endl; return 0;
    }
    if (app.arguments().contains(QStringLiteral("--native-revisions"))) {
        try { nativeRevisionTests(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS native document revisions" << std::endl; return 0;
    }
    if (app.arguments().contains(QStringLiteral("--storyboard-scroll"))) {
        try { ViewportInteractionTest::storyboardScroll(); }
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        std::cout<<"storyboard scroll PASS"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sketch-story"))) {
        try { ViewportInteractionTest::sketchStoryRegressions(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--history-position"))) {
        try { ViewportInteractionTest::historyPositionRegressions(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--mesh-export-tests"))) {
        try { ViewportInteractionTest::meshExportAutomatic(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--spatial-sketch"))) {
        try { ViewportInteractionTest::spatialSketch(app.arguments().contains(QStringLiteral("--gl"))); }
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        std::cout<<"spatial sketch PASS"<<std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--curve-extraction"))) {
        try { ViewportInteractionTest::curveExtraction(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "curve extraction PASS" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--handle-constraints"))) {
        try { ViewportInteractionTest::handleConstraints(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "handle constraints PASS" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sketch-reference-workflow"))) {
        try { ViewportInteractionTest::sketchReferenceWorkflow(); ViewportInteractionTest::derivedCurveEntity(); ViewportInteractionTest::associativeOffset(); ViewportInteractionTest::inwardOffsetAndProjection(); }
        catch(const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
        std::cout<<"PASS sketch reference workflow"<<std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-thicken"))) {
        const int argument = int(app.arguments().indexOf(QStringLiteral("--profile-thicken")));
        try {
            ViewportInteractionTest::profileThicken(app.arguments().value(argument + 1), app.arguments().value(argument + 2).toInt(),
                                                    app.arguments().value(argument + 3).toDouble(), app.arguments().value(argument + 4).toInt());
        } catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-projection"))) {
        const int argument = int(app.arguments().indexOf(QStringLiteral("--profile-projection")));
        try { ViewportInteractionTest::profileProjection(app.arguments().value(argument + 1), app.arguments().value(argument + 2), app.arguments().value(argument + 3).toInt()); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-mouse-offset"))) {
        try { ViewportInteractionTest::profileMouseOffset(app.arguments().value(app.arguments().indexOf(QStringLiteral("--profile-mouse-offset"))+1)); }
        catch(const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
        return 0;
    }
    if(app.arguments().contains(QStringLiteral("--sketch-editing"))){
        try{ViewportInteractionTest::sketchInteractionRegressions();}
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if(app.arguments().contains(QStringLiteral("--helix-start-angle"))){
        try{ViewportInteractionTest::helixStartAngleRegressions();}
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if(app.arguments().contains(QStringLiteral("--sketch-menu-cancel"))){
        try{ViewportInteractionTest::sketchMenuCancelRegressions();}
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--dense-fixed-sketch"))) {
        try { ViewportInteractionTest::denseFixedSketchRegressions(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--profile-sketch-interaction"))) {
        try { ViewportInteractionTest::profileSketchInteraction(app.arguments().value(app.arguments().indexOf(QStringLiteral("--profile-sketch-interaction"))+1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--bench-blend-edit"))) {
        const int argument = app.arguments().indexOf(QStringLiteral("--bench-blend-edit"));
        try { ViewportInteractionTest::benchmarkBlendEdit(app.arguments().value(argument + 1), app.arguments().value(argument + 2).toInt()); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-regression"))) {
        try {
            const int at=app.arguments().indexOf(QStringLiteral("--document-regression"));
            require(at+3<app.arguments().size(), "scenario, documento e output JSON richiesti");
            DocumentRegression::run(app.arguments()[at+1],app.arguments()[at+2],app.arguments()[at+3],4);
        }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--applicatore-preview"))) {
        try {
            const int option = app.arguments().indexOf(QStringLiteral("--applicatore-preview"));
            require(option + 1 < app.arguments().size(), "percorso Applicatore richiesto");
            ViewportInteractionTest::applicatorePreview(app.arguments()[option + 1]);
        } catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--display-mesh"))) {
        try { ViewportInteractionTest::displayMeshTests(); }
        catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--blend-preview-reload"))) {
        try { ViewportInteractionTest::blendPreviewReload(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS blend preview reload" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--function-panels-review"))) {
        try { ViewportInteractionTest::functionPanelsReview(); ViewportInteractionTest::extrusionActionPreconditions(); }
        catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--document-units"))) {
        try { ViewportInteractionTest::documentUnitsTests(); }
        catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--history-suppression"))) {
        try { historySuppressionTests(); }
        catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--review-regressions"))) {
        try { ViewportInteractionTest::reviewRegressions(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS review regressions" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--toolbar-restore"))) {
        try {
            {
                PdfWindow first;
                auto *model = first.findChild<QToolBar *>(QStringLiteral("modelingIconBar"));
                auto *sketch = first.findChild<QToolBar *>(QStringLiteral("sketchIconBar"));
                require(model && sketch, "toolbars exist");
                require(model->isHidden() && sketch->isHidden(), "classic toolbars off by default");
                require(first.findChild<QAction *>("ribbonVisibleAction")->isChecked(), "ribbon on by default");
                for(const auto &name:{"modelingIconBar","surfaceIconBar","sketchIconBar"})first.findChild<QAction *>(QString::fromLatin1(name)+"VisibleAction")->setChecked(true);
                model->hide(); sketch->show();
                QSettings().setValue(QStringLiteral("interface/state"), first.saveState());
            }
            PdfWindow reopened;
            require(!reopened.findChild<QToolBar *>(QStringLiteral("modelingIconBar"))->isHidden(), "model toolbar restored after saved sketch mode");
            require(reopened.findChild<QToolBar *>(QStringLiteral("sketchIconBar"))->isHidden(), "sketch toolbar hidden outside sketch");
            require(!reopened.menuBar()->actions().isEmpty(), "menus available at construction");
            // Barra Superfici: i comandi delle superfici come pulsanti diretti.
            auto *surfaces = reopened.findChild<QToolBar *>(QStringLiteral("surfaceIconBar"));
            require(surfaces && !surfaces->isHidden(), "barra Superfici visibile");
            QStringList texts;
            for (QAction *action : surfaces->actions()) texts.append(action->text());
            for (const QString &command : {QStringLiteral("Taglia superficie..."), QStringLiteral("Spessore..."), QStringLiteral("Offset superficie..."),
                                           QStringLiteral("Cuci superfici..."), QStringLiteral("Superficie di riempimento..."), QStringLiteral("Curva proiettata...")})
                require(texts.contains(command), "comando nella barra Superfici");
            // Barra personalizzata: vuota e nascosta, poi i comandi scelti nell'ordine dato.
            auto *custom = reopened.findChild<QToolBar *>(QStringLiteral("customIconBar"));
            require(custom && custom->isHidden() && custom->actions().isEmpty(), "barra personalizzata vuota all'inizio");
            QSettings().setValue(QStringLiteral("toolbar/custom"),
                                 QStringList{QStringLiteral("Funzioni/Superfici/Spessore..."), QStringLiteral("Analisi/Misura..."), QStringLiteral("Comando inesistente")});
            QSettings().setValue("toolbar/visible/customIconBar",true);
            PdfWindow customized;
            auto *chosen = customized.findChild<QToolBar *>(QStringLiteral("customIconBar"));
            require(chosen && !chosen->isHidden() && chosen->actions().size() == 2 && chosen->actions().at(0)->text() == QStringLiteral("Spessore...")
                        && chosen->actions().at(1)->text() == QStringLiteral("Misura..."),
                    "barra personalizzata con i comandi scelti");
            QSettings().remove(QStringLiteral("toolbar/custom"));
            QSettings().setValue(QStringLiteral("toolbar/custom"), QStringList{QStringLiteral("Analisi/Misura...")});
            QSettings().setValue(QStringLiteral("toolbar/customDropdown"), true);
            QSettings().setValue(QStringLiteral("toolbar/locked"), false);
            {
                PdfWindow dropdownWindow;
                auto *bar = dropdownWindow.findChild<QToolBar *>(QStringLiteral("customIconBar"));
                require(bar && bar->actions().size() == 1 && bar->isMovable(), "custom dropdown toolbar movable");
                require(bar->findChild<QToolButton *>(QStringLiteral("customToolbarDropdown")), "custom dropdown created");
                dropdownWindow.addToolBar(Qt::LeftToolBarArea, bar);
                auto *lock = dropdownWindow.findChild<QAction *>(QStringLiteral("lockToolbarsAction"));
                require(lock, "toolbar lock action exists");
                lock->setChecked(true);
                for (auto *toolbar : dropdownWindow.findChildren<QToolBar *>())
                    require(!toolbar->isMovable() && !toolbar->isFloatable(), "all toolbars locked");
            }
            {
                PdfWindow restored;
                auto *bar = restored.findChild<QToolBar *>(QStringLiteral("customIconBar"));
                require(restored.toolBarArea(bar) == Qt::LeftToolBarArea, "custom toolbar position restored");
                require(!bar->isMovable(), "toolbar lock restored");
                restored.findChild<QAction *>(QStringLiteral("lockToolbarsAction"))->setChecked(false);
                require(bar->isMovable() && bar->isFloatable(), "toolbar unlocked again");
            }
            QSettings().remove(QStringLiteral("toolbar/custom"));
            QSettings().remove(QStringLiteral("toolbar/customDropdown"));
            QSettings().remove(QStringLiteral("toolbar/locked"));
            {
                PdfWindow editable;
                auto *model = editable.findChild<QToolBar *>(QStringLiteral("modelingIconBar"));
                auto *edit = editable.findChild<QAction *>(QStringLiteral("modelingIconBarEditorAction"));
                require(model && edit && edit->isEnabled(), "main toolbar editor available when unlocked");
                bool edited = false;
                QTimer::singleShot(0, &editable, [&edited] {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog) return;
                    auto *list = dialog->findChild<QListWidget *>(QStringLiteral("toolbarEditorCommands"));
                    if (list) for (int row = 0; row < list->count(); ++row) {
                        if (list->item(row)->data(Qt::UserRole).toString() != QStringLiteral("command/Funzioni/Superfici/Offset superficie...")) continue;
                        auto *item = list->takeItem(row);
                        item->setCheckState(Qt::Checked);
                        list->insertItem(0, item);
                        edited = true;
                        break;
                    }
                    dialog->accept();
                });
                edit->trigger();
                require(edited && model->actions().first()->text() == QStringLiteral("Offset superficie..."), "main toolbar command reordered through editor");
                editable.findChild<QAction *>(QStringLiteral("lockToolbarsAction"))->setChecked(true);
                require(!edit->isEnabled(), "locking disables main toolbar editor");
            }
            {
                PdfWindow restored;
                auto *model = restored.findChild<QToolBar *>(QStringLiteral("modelingIconBar"));
                require(model->actions().first()->text() == QStringLiteral("Offset superficie..."), "main toolbar button order restored");
            }
            {
                QToolBar bar;
                bar.setObjectName(QStringLiteral("testToolbarEditor"));
                QAction first(QStringLiteral("First")), extra(QStringLiteral("Extra"));
                bar.addAction(&first);
                auto *button = new QToolButton;
                button->setDefaultAction(&extra);
                auto *group = bar.addWidget(button);
                bool locked = false;
                ToolbarEditor editor(&bar, {{QStringLiteral("First"), &first}, {QStringLiteral("Extra"), &extra}}, [&locked] { return locked; });
                require(editor.moveAction(group, &first), "flyout can move before a command");
                require(bar.actions().first() == group && bar.widgetForAction(group) == button, "flyout widget preserved when moving");
                locked = true;
                require(!editor.moveAction(group, nullptr) && bar.actions().first() == group, "lock prevents button reordering");
                locked = false;
                require(editor.moveAction(group, nullptr) && bar.actions().last() == group, "flyout can move to end");
            }
            {
                QToolBar bar;
                bar.setObjectName(QStringLiteral("testToolbarGroups"));
                QAction first(QStringLiteral("First")), second(QStringLiteral("Second"));
                bar.addAction(&first); bar.addAction(&second);
                bool locked = false;
                ToolbarEditor editor(&bar, {{QStringLiteral("First"), &first}, {QStringLiteral("Second"), &second}}, [&locked] { return locked; });
                bool grouped = false;
                const auto stageGroup = [&grouped](bool accept) {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog) return;
                    auto *list = dialog->findChild<QListWidget *>(QStringLiteral("toolbarEditorCommands"));
                    list->selectAll();
                    dialog->findChild<QLineEdit *>(QStringLiteral("toolbarGroupName"))->setText(QStringLiteral("Preferiti"));
                    dialog->findChild<QPushButton *>(QStringLiteral("toolbarGroupCreate"))->click();
                    grouped = list->count() == 1;
                    if (accept) dialog->accept(); else dialog->reject();
                };
                QTimer::singleShot(0, &bar, [stageGroup] { stageGroup(false); });
                editor.edit();
                require(grouped && bar.actions().size() == 2, "cancel grouping leaves toolbar unchanged");
                require(!QSettings().contains(QStringLiteral("toolbar/layout/testToolbarGroups")), "cancel grouping does not save");
                QTimer::singleShot(0, &bar, [stageGroup] { stageGroup(true); });
                editor.edit();
                require(grouped && bar.actions().size() == 1, "two commands occupy one toolbar button");
                auto *button = qobject_cast<QToolButton *>(bar.widgetForAction(bar.actions().first()));
                require(button && button->menu() && button->menu()->actions() == QList<QAction *>{&first, &second}, "group uses original command actions in order");
                int triggered = 0;
                QObject::connect(&first, &QAction::triggered, &bar, [&triggered] { ++triggered; });
                button->click();
                require(triggered == 1, "group primary button executes the original action once");
                first.setEnabled(false);
                require(button->isEnabled(), "group menu remains accessible when another command is enabled");
                button->click();
                require(triggered == 1, "disabled primary command is not executed");
                second.setEnabled(false);
                require(!button->isEnabled(), "group disabled when all its commands are disabled");
                first.setEnabled(true); second.setEnabled(true);
                locked = true;
                editor.edit();
                require(bar.actions().size() == 1, "locked grouping editor cannot modify toolbar");
                QToolBar reopened;
                reopened.setObjectName(bar.objectName());
                reopened.addAction(&first); reopened.addAction(&second);
                ToolbarEditor restored(&reopened, {{QStringLiteral("First"), &first}, {QStringLiteral("Second"), &second}}, [] { return false; });
                require(reopened.actions().size() == 1, "group restored after reopening");
                auto *restoredButton = qobject_cast<QToolButton *>(reopened.widgetForAction(reopened.actions().first()));
                require(restoredButton && restoredButton->menu()->actions().size() == 2 && reopened.actions().first()->text() == QStringLiteral("Preferiti"), "group name and members restored");
                QTimer::singleShot(0, &reopened, [] {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog) return;
                    auto *list = dialog->findChild<QListWidget *>(QStringLiteral("toolbarEditorCommands"));
                    list->setCurrentRow(0);
                    dialog->findChild<QPushButton *>(QStringLiteral("toolbarGroupSeparate"))->click();
                    dialog->accept();
                });
                restored.edit();
                require(reopened.actions() == QList<QAction *>{&first, &second}, "separate group restores individual buttons in order");
            }
            {
                PdfWindow window;
                auto *menu = window.findChild<QMenu *>(QStringLiteral("toolbarsMenu"));
                auto *bar = window.findChild<QToolBar *>(QStringLiteral("modelingIconBar"));
                auto *visible = window.findChild<QAction *>(QStringLiteral("modelingIconBarVisibleAction"));
                require(menu && visible && menu->actions().contains(visible), "toolbar visibility is in unified options group");
                require(menu->actions().contains(window.findChild<QAction *>(QStringLiteral("lockToolbarsAction"))), "toolbar lock is in unified group");
                require(visible->isChecked() && !bar->isHidden(), "model toolbar visible by default");
                visible->setChecked(false);
                require(bar->isHidden(), "toolbar can be hidden");
                bar->setProperty("toolbarModeHidden", true);
                ToolbarEditor::refreshVisibility(bar);
                bar->setProperty("toolbarModeHidden", false);
                ToolbarEditor::refreshVisibility(bar);
                require(bar->isHidden(), "mode changes preserve hidden preference");
                PdfWindow reopened;
                auto *restored = reopened.findChild<QToolBar *>(QStringLiteral("modelingIconBar"));
                auto *toggle = reopened.findChild<QAction *>(QStringLiteral("modelingIconBarVisibleAction"));
                require(restored->isHidden() && !toggle->isChecked(), "hidden toolbar preference restored");
                toggle->setChecked(true);
                require(!restored->isHidden(), "hidden toolbar can be shown from options");
                auto *sketch = reopened.findChild<QToolBar *>(QStringLiteral("sketchIconBar"));
                auto *sketchToggle = reopened.findChild<QAction *>(QStringLiteral("sketchIconBarVisibleAction"));
                sketchToggle->setChecked(false); sketchToggle->setChecked(true);
                require(sketch->isHidden(), "sketch toolbar still waits for sketch mode");
            }
            QSettings().remove(QStringLiteral("toolbar/visible"));
            QSettings().remove(QStringLiteral("toolbar/layout"));
            QSettings().remove(QStringLiteral("toolbar/locked"));
        } catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS toolbar restore" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sketch-comb"))) {
        try { ViewportInteractionTest::sketchCombUi(app.arguments().contains(QStringLiteral("--gl"))); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS sketch comb UI" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--shape-analysis"))) {
        try { ViewportInteractionTest::shapeAnalysisUi(app.arguments().contains(QStringLiteral("--gl"))); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS shape analysis UI" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--thicken"))) {
        try { ViewportInteractionTest::thickenFeature(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--projection-features"))) {
        try { ViewportInteractionTest::projectionFeatures(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--draft"))) {
        try { ViewportInteractionTest::draftFeature(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--workflow-ui"))) {
        try { ViewportInteractionTest::workflowUi(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--topology-labels"))) {
        try { ViewportInteractionTest::topologyLabels(); ViewportInteractionTest::topologyLabelVisibility(); }
        catch (const std::exception &error) { std::cerr << "FAIL topology labels: " << error.what() << std::endl; return 1; }
        std::cout << "PASS topology labels" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--all-history"))) {
        try { allHistoryTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--smooth-zoom"))) {
        try { ViewportInteractionTest::smoothZoom(app.arguments().contains(QStringLiteral("--gl"))); }
        catch (const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sketch-slot-point"))) {
        try { ViewportInteractionTest::sketchSlotPoint(app.arguments().contains(QStringLiteral("--gl"))); }
        catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--history-preview-mesh"))) {
        try { const int index=app.arguments().indexOf(QStringLiteral("--history-preview-mesh"));
            ViewportInteractionTest::historyPreviewMesh(index+1<app.arguments().size() && !app.arguments().at(index+1).startsWith("--")
                ?app.arguments().at(index+1):QString(),app.arguments().contains(QStringLiteral("--gl"))); }
        catch (const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--preview-performance"))) {
        try { ViewportInteractionTest::previewPerformance(app.arguments().contains(QStringLiteral("--gl"))); }
        catch (const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--feature-handles"))) {
        try { ViewportInteractionTest::featureHandles(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--revolution-history"))) {
        try { revolutionHistoryTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--extrusion-history"))) {
        try { extrusionHistoryTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--blend-history"))) {
        try { blendHistoryTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--boolean-history"))) {
        try { booleanHistoryTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS boolean history" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--declared-history-cache"))) {
        try { declaredHistoryCacheTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS declared history cache" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--genealogy")) || app.arguments().contains(QStringLiteral("--history-diagnostics"))) {
        try { ViewportInteractionTest::genealogyReferences(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS genealogy" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--rebuild-all"))) {
        try { ViewportInteractionTest::rebuildAllCommand(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS rebuild all" << std::endl;
        return 0;
    }
    const int rebuildArg = int(app.arguments().indexOf(QStringLiteral("--rebuild-document")));
    if (rebuildArg > 0) {
        try { ViewportInteractionTest::rebuildDocument(app.arguments().mid(rebuildArg + 1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--individual-fill"))) {
        try { ViewportInteractionTest::individualFillControls(); }
        catch (const std::exception &e) { std::cerr<<e.what()<<std::endl; return 1; }
        return 0;
    }
    const int fillArg = int(app.arguments().indexOf(QStringLiteral("--fill-document")));
    if (fillArg > 0) {
        try { ViewportInteractionTest::fillDocument(app.arguments().mid(fillArg + 1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS fill surface" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--drawing-mouse"))) {
        try { drawingMouseProfileTests(app.arguments().value(app.arguments().indexOf(QStringLiteral("--drawing-mouse"))+1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS real Mouse drawing profile and interactive placement" << std::endl;return 0;
    }
    if (app.arguments().contains(QStringLiteral("--extrude-surface"))) {
        try { ViewportInteractionTest::extrusionSurfaceOnly(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS extrusion surface only" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--automatic-snaps"))) {
        try { ViewportInteractionTest::automaticSnapConstraints(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS automatic snap constraints" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--micro-features"))) {
        try { ViewportInteractionTest::microFeatureMarksTest(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS micro features" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--ellipse-trim"))) {
        try { ViewportInteractionTest::ellipseTrim(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        std::cout << "PASS ellipse trim" << std::endl;
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--edge-pick-deletion"))) {
        try { ViewportInteractionTest::deletionDuringEdgePick(); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    const int benchWindow = int(app.arguments().indexOf(QStringLiteral("--bench-window")));
    if (benchWindow > 0) {
        // Le impostazioni dell'utente, copiate: la finestra non tocca le sue.
        QDir().mkpath(settings.path() + QStringLiteral("/ForgeCADTests"));
        QFile::copy(QDir::homePath() + QStringLiteral("/.config/ForgeCAD/ForgeCAD.conf"),
                    settings.path() + QStringLiteral("/ForgeCADTests/Viewport.ini"));
        try { ViewportInteractionTest::benchWindow(app.arguments().mid(benchWindow + 1)); }
        catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
        return 0;
    }
    if (app.arguments().indexOf(QStringLiteral("--time-offset")) > 0) {
        DocumentState state;
        ForgeCad::loadDocumentFile(app.arguments().last(), state);
        CadViewport v;
        v.loadDocument(state);
        for (int i = 0; i < v.extrusions().size(); ++i) {
            const ExtrusionObject &e = v.extrusions().at(i);
            if (e.feature != BodyFeature::SurfaceOffset) continue;
            const ExtrusionObject &base = v.extrusions().at(e.firstBody);
            QElapsedTimer t; t.start();
            QString error;
            auto r = ForgeCad::forgeOffsetFaces(base.forgeBody, e.offsetFaces, e.distance, &error);
            std::cout << e.name.toStdString() << ": facce " << (e.offsetFaces.isEmpty() ? base.forgeBody->faces().size() : std::size_t(e.offsetFaces.size()))
                      << " di " << base.forgeBody->faces().size() << ", " << t.elapsed() << " ms " << error.toStdString() << std::endl;
            // Tutte le facce del corpo.
            t.restart();
            r = ForgeCad::forgeOffsetFaces(base.forgeBody, {}, e.distance, &error);
            std::cout << "  tutte le facce: " << t.elapsed() << " ms " << error.toStdString() << std::endl;
        }
        return 0;
    }
    try {
        const int step = int(app.arguments().indexOf(QStringLiteral("--render-step")));
        const int owner = int(app.arguments().indexOf(QStringLiteral("--pick-owner")));
        if (owner > 0) { ViewportInteractionTest::pickOwner(app.arguments().mid(owner + 1)); return 0; }
        const int load = int(app.arguments().indexOf(QStringLiteral("--bench-load")));
        if (load > 0) { ViewportInteractionTest::benchLoad(app.arguments().mid(load + 1)); return 0; }
        const int bench = int(app.arguments().indexOf(QStringLiteral("--bench-view")));
        if (bench > 0) { ViewportInteractionTest::benchView(app.arguments().mid(bench + 1)); return 0; }
        const int sketchEdit = int(app.arguments().indexOf(QStringLiteral("--render-sketch-context")));
        if(sketchEdit>0){ViewportInteractionTest::renderSketchContext(app.arguments().mid(sketchEdit+1));return 0;}
        const int edit = int(app.arguments().indexOf(QStringLiteral("--render-edit")));
        const int displayStatsArg = int(app.arguments().indexOf(QStringLiteral("--display-stats")));
        if (displayStatsArg > 0) { ViewportInteractionTest::displayStats(app.arguments().value(displayStatsArg + 1)); return 0; }
        const int meshStats = int(app.arguments().indexOf(QStringLiteral("--mesh-stats")));
        const int dumpArg = int(app.arguments().indexOf(QStringLiteral("--dump-document")));
        if (dumpArg > 0) {
            try { ViewportInteractionTest::dumpDocument(app.arguments().value(dumpArg + 1), app.arguments().value(dumpArg + 2, QStringLiteral("-1")).toInt()); }
            catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
            return 0;
        }
        if (meshStats > 0) { ViewportInteractionTest::meshStats(app.arguments().mid(meshStats + 1)); return 0; }
        if (edit > 0) ViewportInteractionTest::renderEdit(app.arguments().mid(edit + 1));
        else if (step > 0) ViewportInteractionTest::renderStep(app.arguments().mid(step + 1));
        else if (app.arguments().contains(QStringLiteral("--extend-contour-pick")))
            ViewportInteractionTest::extensionContourPicking();
        else if (app.arguments().contains(QStringLiteral("--trim-pick")))
            ViewportInteractionTest::trimPartPicking();
        else if (app.arguments().contains(QStringLiteral("--offset-pick")))
            ViewportInteractionTest::offsetFacePicking();
        else if (app.arguments().contains(QStringLiteral("--offset-loft-sides"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--offset-loft-sides"));
            ViewportInteractionTest::offsetLoftSides(app.arguments().value(argument + 1), app.arguments().value(argument + 2, QStringLiteral("1")).toDouble(), app.arguments().contains(QStringLiteral("--rebuild-lofts")),
                app.arguments().contains(QStringLiteral("--save-rebuilt-loft"))
                    ? app.arguments().value(app.arguments().indexOf(QStringLiteral("--save-rebuilt-loft")) + 1) : QString());
        }
        else if (app.arguments().contains(QStringLiteral("--extend-topology-face"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--extend-topology-face"));
            ViewportInteractionTest::extendTopologyFace(app.arguments().value(argument + 1),
                app.arguments().value(argument + 2).toInt(), app.arguments().value(argument + 3).toInt(),
                app.arguments().value(argument + 4, QStringLiteral("1")).toDouble());
        }
        else if (app.arguments().contains(QStringLiteral("--regularize-spline-document"))) {
            ViewportInteractionTest::regularizeSplineDocument(app.arguments().mid(app.arguments().indexOf(QStringLiteral("--regularize-spline-document")) + 1));
        }
        else if (app.arguments().contains(QStringLiteral("--blend-document-faces"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--blend-document-faces"));
            ViewportInteractionTest::blendDocumentFaces(app.arguments().mid(argument + 1));
        }
        else if (app.arguments().contains(QStringLiteral("--repair-extension-contours"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--repair-extension-contours"));
            ViewportInteractionTest::repairExtensionContours(app.arguments().value(argument + 1), app.arguments().value(argument + 2),
                                                            app.arguments().mid(argument + 3));
        }
        else if (app.arguments().contains(QStringLiteral("--audit-sheet-history"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--audit-sheet-history"));
            ViewportInteractionTest::auditSheetHistory(app.arguments().value(argument + 1), app.arguments().value(argument + 2));
        }
        else if (app.arguments().contains(QStringLiteral("--verify-mutual-trim"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--verify-mutual-trim"));
            ViewportInteractionTest::trimTopologyBodies(app.arguments().value(argument + 1),
                app.arguments().value(argument + 2).toInt(), app.arguments().value(argument + 3).toInt(), true);
        }
        else if (app.arguments().contains(QStringLiteral("--trim-topology-bodies"))) {
            const int argument = app.arguments().indexOf(QStringLiteral("--trim-topology-bodies"));
            ViewportInteractionTest::trimTopologyBodies(app.arguments().value(argument + 1),
                app.arguments().value(argument + 2).toInt(), app.arguments().value(argument + 3).toInt());
        }
        else if (app.arguments().contains(QStringLiteral("--offset-lofts")))
            ViewportInteractionTest::offsetLofts(app.arguments().mid(app.arguments().indexOf(QStringLiteral("--offset-lofts")) + 1));
        else if (app.arguments().contains(QStringLiteral("--loft-corner")))
            ViewportInteractionTest::loftCorner(exampleDocumentPath("prova con loft.prt"));
        else if (app.arguments().contains(QStringLiteral("--copy-faces"))) ViewportInteractionTest::copyFaces();
        else if (app.arguments().contains(QStringLiteral("--reference-list-highlight"))) ViewportInteractionTest::referenceListHighlight();
        else if (app.arguments().contains(QStringLiteral("--surface-patterns"))) ViewportInteractionTest::surfacePatterns();
        else if (app.arguments().contains(QStringLiteral("--sheet-dressup"))) ViewportInteractionTest::sheetDressupFeatures();
        else if (app.arguments().contains(QStringLiteral("--midpoint-quadrant"))) ViewportInteractionTest::midpointQuadrantConstraints();
        else if (app.arguments().contains(QStringLiteral("--saved-loft-surface")))
            ViewportInteractionTest::savedLoftSurface(app.arguments().value(app.arguments().indexOf(QStringLiteral("--saved-loft-surface"))+1));
        else if (app.arguments().contains(QStringLiteral("--edge-loft"))) ViewportInteractionTest::edgeLoftWorkflow();
        else if (app.arguments().contains(QStringLiteral("--open-loft-tangency")))
            ViewportInteractionTest::openLoftTangency(app.arguments().value(app.arguments().indexOf(QStringLiteral("--open-loft-tangency"))+1));
        else if (app.arguments().contains(QStringLiteral("--spline-clicks")))
            ViewportInteractionTest::splineSelectionClicks(app.arguments().value(app.arguments().indexOf(QStringLiteral("--spline-clicks"))+1));
        else if (app.arguments().contains(QStringLiteral("--spline-shape"))) ViewportInteractionTest::splineShapeOptions();
        else ViewportInteractionTest::run(app.arguments().contains(QStringLiteral("--gl")),
            app.arguments().contains(QStringLiteral("--orbit-pivot")));
    }
    catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
}
