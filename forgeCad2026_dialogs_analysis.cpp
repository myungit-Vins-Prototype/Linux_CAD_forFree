#include "forgeCad2026_dialogs.h"
#include "cad_drawing_browser.h"
#include "cad_drawing_window.h"
#include "cad_toolbar.h"
#include "cad_curve_extract.h"

#include "kernel/fk_cancel.h"
#include "cad_spatial_sketch.h"
#include "forgeCad2026_viewport.h"
#include "cuda_support.h"
#include "cad_cuda_tessellation.h"
#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_document_io.h"
#include "cad_revision_client.h"
#include "cad_export.h"
#include "cad_expression.h"
#include "cad_datum.h"
#include "cad_features.h"
#include "cad_feature_dependencies.h"
#include "cad_feature_build.h"
#include "cad_import.h"
#include "cad_mass.h"
#include "cad_measure.h"
#include "cad_pattern.h"
#include "cad_extrude.h"
#include "cad_sketch_refs.h"
#include "cad_forge.h"
#include "cad_display_cache.h"
#include "cad_shape_analysis.h"
#include "fk_curve_ops.h"
#include "cad_overlay_renderer.h"
#include <array>
#include <atomic>
#include <set>
#include "fk_classify.h"
#include "fk_surface_algo.h"
#include "fk_curve_algo.h"
#include "fk_topology.h"
#include "fk_micro.h"
#include "cad_history.h"
#include "cad_history_graph.h"
#include "cad_model_history.h"
#include "cad_topology_ref.h"
#include "cad_element_names.h"
#include "cad_icons.h"
#include "cad_kernel.h"
#include "cad_sketch_edit.h"
#include "cad_sketch_offset.h"
#include "cad_snap.h"
#include "fk_parallel.h"

#include "cad_menu.h"
#include "toolbar_editor.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <cstdio>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using ForgeCad::Gui::uiFont;
using ForgeCad::Gui::pointLength;
using ForgeCad::Gui::pointDistance;
using ForgeCad::Gui::kSelectionColor;
using ForgeCad::Gui::kHoverColor;
using ForgeCad::Gui::featureIconName;
using ForgeCad::Gui::wrappedNote;
using ForgeCad::Gui::runUntilApplied;



void measureDialog(QMainWindow *window, CadViewport *viewport) {
    FunctionDialogPanel dialog(window);
    dialog.setWindowTitle(QStringLiteral("Misura"));
    dialog.setModal(false);
    auto *form = dialog.createScrollableForm();
    form->addRow(wrappedNote(QStringLiteral("Clicca nella vista uno o due oggetti: vertici, punti medi e centri degli spigoli, spigoli, "
                                            "facce, piani e assi. Il clic su un oggetto scelto lo toglie; Esc nella vista azzera."), &dialog));
    auto *first = new QLabel(&dialog), *second = new QLabel(&dialog);
    for (QLabel *label : {first, second}) {
        label->setWordWrap(true);
        label->setMaximumWidth(360);
    }
    form->addRow(QStringLiteral("A:"), first);
    form->addRow(QStringLiteral("B:"), second);
    auto *results = new QLabel(&dialog);
    results->setWordWrap(true);
    results->setMinimumWidth(300);
    results->setMaximumWidth(420);
    results->setTextInteractionFlags(Qt::TextSelectableByMouse);
    results->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    form->addRow(results);
    auto *buttons = new QDialogButtonBox(&dialog);
    QPushButton *clearButton = buttons->addButton(QStringLiteral("Azzera"), QDialogButtonBox::ResetRole);
    QPushButton *copyButton = buttons->addButton(QStringLiteral("Copia"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    form->addRow(buttons);

    QVector<GeometryRef> refs;
    QString report;
    const auto same = [](const GeometryRef &a, const GeometryRef &b) {
        if (a.kind != b.kind || a.index != b.index || !(a.element == b.element)) return false;
        if (a.kind == 3 || a.kind == 4 || a.kind == 5 || a.kind == ForgeCad::kGeometryRefEdgeMidpoint || a.kind == ForgeCad::kGeometryRefEdgeCenter)
            return a.point.subshape == b.point.subshape;
        return true;
    };
    const auto refresh = [&] {
        viewport->setReferenceMarks(refs);
        QVector<ForgeCad::MeasureEntity> entities;
        QStringList names;
        QString error;
        for (const GeometryRef &ref : refs) {
            ForgeCad::MeasureEntity entity;
            QString reason;
            if (!ForgeCad::measureEntity(ref, viewport->sketches(), viewport->extrusions(), entity, &reason)) {
                error = reason;
                names << QStringLiteral("(non valido)");
                continue;
            }
            names << entity.name;
            entities.append(entity);
        }
        first->setText(names.value(0, QStringLiteral("(clicca nella vista)")));
        second->setText(refs.size() >= 1 ? names.value(1, QStringLiteral("(facoltativo)")) : QStringLiteral("-"));
        viewport->setMeasureOverlay(false);
        report.clear();
        if (!error.isEmpty()) {
            results->setStyleSheet(QStringLiteral("color: #ff7a6a;"));
            results->setText(error);
            return;
        }
        results->setStyleSheet(QString());
        if (entities.isEmpty()) {
            results->clear();
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const ForgeCad::MeasureReport measure = ForgeCad::measureEntities(entities);
        QApplication::restoreOverrideCursor();
        if (!measure.ok) {
            results->setStyleSheet(QStringLiteral("color: #ff7a6a;"));
            results->setText(measure.error);
            return;
        }
        report = measure.lines.join(QLatin1Char('\n'));
        results->setText(report);
        if (measure.hasDistance) {
            const auto q = [](const ForgeCad::Kernel::Vec3 &p) { return QVector3D(float(p.x()), float(p.y()), float(p.z())); };
            viewport->setMeasureOverlay(true, q(measure.from), q(measure.to), measure.label);
        }
    };
    const auto pick = [&] {
        const QString error = viewport->beginReferencePick(ForgeCad::DatumRolePoint | ForgeCad::DatumRoleLine | ForgeCad::DatumRoleCurve
                                                               | ForgeCad::DatumRolePlane | ForgeCad::DatumRoleFace | ForgeCad::kMeasureSnapRole,
                                                           -1);
        if (!error.isEmpty()) results->setText(error);
    };
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        if (picked) {
            int existing = -1;
            for (int k = 0; k < refs.size(); ++k)
                if (same(refs.at(k), ref)) existing = k;
            if (existing >= 0) refs.remove(existing);
            else if (refs.size() >= 2) refs = {ref};
            else refs.append(ref);
        } else {
            refs.clear();
        }
        refresh();
        pick();
    });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [&] {
        refs.clear();
        refresh();
        pick();
    });
    QObject::connect(copyButton, &QPushButton::clicked, &dialog, [&] {
        if (!report.isEmpty()) QGuiApplication::clipboard()->setText(first->text() + QStringLiteral("\n") + second->text() + QStringLiteral("\n") + report);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Un oggetto selezionato prima del comando e' gia' la prima entita'? No: la
    // selezione della vista non dice quale spigolo o faccia; si parte da zero.
    {
        const WindowLock lock(window, viewport, &dialog);
        dialog.show();
        refresh();
        pick();
        QEventLoop loop;
        QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->setReferenceMarks({});
    viewport->setMeasureOverlay(false);
}

QVector<GeometryRef> microFeatureMarks(const ExtrusionObject &body, int index, QStringList &lines) {
    QVector<GeometryRef> marks;
    const ForgeCad::Kernel::Body &brep = *body.forgeBody;
    for (const auto &feature : ForgeCad::Kernel::findMicroFeatures(brep)) {
        GeometryRef ref;
        ref.index = index;
        ref.featureId = body.featureId;
        if (feature.kind == ForgeCad::Kernel::MicroFeature::Kind::ThinFace) {
            // La faccia si segna con i suoi bordi (i segni di faccia valgono
            // solo per le facce piane e di rivoluzione).
            for (const ForgeCad::Kernel::EdgeId edge : ForgeCad::faceBoundaryEdges(brep, ForgeCad::Kernel::FaceId(feature.index))) {
                GeometryRef border = ref;
                border.kind = 4;
                const auto &geometry = brep.edge(edge);
                border.point = ForgeCad::edgeReference(brep, edge, geometry.curve->point(0.5 * (geometry.range.lo + geometry.range.hi)));
                marks.append(border);
            }
            lines << QStringLiteral("Faccia sottile F%1: larghezza circa %2 mm").arg(feature.index).arg(feature.measure, 0, 'g', 3);
            continue;
        } else {
            ref.kind = 4;
            ref.point = ForgeCad::edgeReference(brep, ForgeCad::Kernel::EdgeId(feature.index), feature.location);
            if (feature.kind == ForgeCad::Kernel::MicroFeature::Kind::ShortEdge)
                lines << QStringLiteral("Spigolo corto E%1: %2 mm").arg(feature.index).arg(feature.measure, 0, 'g', 3);
            else
                lines << QStringLiteral("Spigolo quasi tangente E%1: %2 gradi tra le facce").arg(feature.index)
                             .arg(feature.measure * 180.0 / M_PI, 0, 'g', 3);
        }
        marks.append(ref);
    }
    return marks;
}

void shapeAnalysisDialog(QMainWindow *window, CadViewport *viewport) {
    QDialog dialog(window);
    dialog.setWindowTitle(QStringLiteral("Curvatura, zebra e continuità"));
    dialog.resize(660, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *body = new QComboBox(&dialog);
    body->setObjectName(QStringLiteral("shapeAnalysisBody"));
    const auto &objects = viewport->extrusions();
    QVector<int> candidates;
    for (int index : viewport->resultBodiesBefore(-1))
        if (objects[index].visible) {
            candidates.append(index);
            body->addItem(logicalBodyLabel(viewport,index),index);
        }
    // Le curve 3D sono riferimenti indipendenti, non stadi intermedi di un corpo.
    for (int i=0; i<objects.size(); ++i)
        if (objects[i].visible && !objects[i].modelBodyId && objects[i].curve)
            body->addItem(QStringLiteral("Curva 3D: ")+objects[i].name,i);
    const auto &sketches = viewport->sketches();
    for (int i=0; i<sketches.size(); ++i)
        if (sketches[i].visible) body->addItem(QStringLiteral("Schizzo: ") + sketches[i].name, -2-i);
    if (!body->count()) {
        QMessageBox::information(window, dialog.windowTitle(), QStringLiteral("Occorre un corpo, una superficie, una curva 3D o uno schizzo visibile."));
        return;
    }
    const auto selection = viewport->selection();
    if (selection.kind == SceneObjectKind::Extrusion) {
        const int mapped = resultBodyForPick(viewport,candidates,selection.index);
        const int row = body->findData(mapped>=0?mapped:selection.index); if (row>=0) body->setCurrentIndex(row);
    }
    if (selection.kind == SceneObjectKind::Sketch) {
        const int row = body->findData(-2-selection.index); if (row>=0) body->setCurrentIndex(row);
    }
    auto *face = new QComboBox(&dialog);
    face->setObjectName(QStringLiteral("shapeAnalysisFace"));
    auto *pickFace = new QPushButton(QStringLiteral("Scegli faccia nella vista"), &dialog);
    pickFace->setObjectName(QStringLiteral("shapeAnalysisPickFace")); pickFace->setCheckable(true);
    auto fillFaces = [&] {
        const QSignalBlocker blocker(face);
        face->clear(); face->addItem(QStringLiteral("Tutte le facce"),-1);
        const int index=body->currentData().toInt();
        const bool hasFaces=index>=0 && objects[index].forgeBody;
        if(hasFaces) for(auto id:objects[index].forgeBody->faces()) face->addItem(QStringLiteral("Faccia F%1").arg(id.index),id.index);
        face->setEnabled(hasFaces); pickFace->setEnabled(hasFaces);
    };
    fillFaces();
    auto *mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("shapeAnalysisMode"));
    mode->addItems({QStringLiteral("Zebra — riflessi"), QStringLiteral("Curvatura — intensità"),
        QStringLiteral("Forma locale — sella / piatta / doppia curvatura"), QStringLiteral("Giunzioni — semaforo"),
        QStringLiteral("Curve e bordi — pettine"), QStringLiteral("Superfici — griglia U/V e pettini")});
    if (body->currentData().toInt()<0 || objects[body->currentData().toInt()].curve) mode->setCurrentIndex(4);
    auto spin = [&dialog](double lo, double hi, double value, int decimals) {
        auto *s = new QDoubleSpinBox(&dialog); s->setDecimals(decimals); s->setRange(lo,hi); s->setValue(value); return s;
    };
    auto *frequency = spin(1,100,12,0), *angle = spin(0,180,0,0);
    auto *limit = spin(.000001,1000000,.1,6), *gain = spin(.000001,1000000,100,6);
    auto *autoGain = new QCheckBox(QStringLiteral("Adatta i denti alla dimensione della curva"), &dialog);
    autoGain->setChecked(true);
    auto *uLines = spin(0,40,6,0), *vLines = spin(0,40,6,0), *isoSamples = spin(4,200,32,0);
    auto *normalOnly = new QCheckBox(QStringLiteral("Misura quanto piega la superficie"), &dialog);
    normalOnly->setChecked(true);
    auto *through = new QCheckBox(QStringLiteral("Mostra anche le parti nascoste dalla superficie"), &dialog);
    through->setChecked(true);
    through->setObjectName(QStringLiteral("shapeCombThrough"));
    uLines->setObjectName(QStringLiteral("shapeULines")); vLines->setObjectName(QStringLiteral("shapeVLines"));
    isoSamples->setObjectName(QStringLiteral("shapeIsoSamples"));
    normalOnly->setObjectName(QStringLiteral("shapeNormalCurvature"));
    auto *angleTol = spin(.001,45,.1,3), *curvTol = spin(.000001,100, .001,6);
    form->addRow(QStringLiteral("Corpo / riferimento"),body);
    form->addRow(QStringLiteral("Faccia da analizzare"),face); form->addRow(pickFace);
    form->addRow(QStringLiteral("Lettura"),mode);
    form->addRow(QStringLiteral("Densità zebra"),frequency); form->addRow(QStringLiteral("Direzione zebra (°)"),angle);
    form->addRow(QStringLiteral("Rosso da |k| (mm⁻¹)"),limit); form->addRow(QStringLiteral("Scala pettine (mm²)"),gain);
    form->addRow(QStringLiteral("Scala automatica pettine"),autoGain);
    form->addRow(QStringLiteral("Linee U costante (ciano)"),uLines);
    form->addRow(QStringLiteral("Linee V costante (magenta)"),vLines);
    form->addRow(QStringLiteral("Denti per curva U/V (circa)"),isoSamples);
    form->addRow(QStringLiteral("Curvatura normale"),normalOnly);
    form->addRow(QStringLiteral("Pettini sempre visibili"),through);
    form->addRow(QStringLiteral("Soglia tangenza (°)"),angleTol); form->addRow(QStringLiteral("Soglia curvatura (mm⁻¹)"),curvTol);
    layout->addLayout(form);
    auto *help = new QLabel(&dialog); help->setWordWrap(true); layout->addWidget(help);
    auto *report = new QTextBrowser(&dialog); layout->addWidget(report,1);
    auto *apply = new QPushButton(QStringLiteral("Aggiorna analisi"), &dialog); layout->addWidget(apply);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const QStringList guides = {
        QStringLiteral("Ruota la vista e la direzione delle strisce. Una rottura del riflesso suggerisce un salto di tangenza; un cambio brusco di andamento suggerisce un cambio di curvatura. Zebra qualitative sulla tessellazione: non identificano da sole i flessi."),
        QStringLiteral("Blu = curvatura zero, verde = metà scala, rosso = limite o superiore; grigio = non valutabile. Si mostra max(|k1|,|k2|), in mm⁻¹: 0,1 corrisponde a un raggio di 10 mm. Non indica il segno. Le fasce triangolari dipendono dal campionamento."),
        QStringLiteral("Blu = sella (curvature di segno opposto); arancio = doppia curvatura dello stesso segno; verde = almeno una curvatura quasi nulla entro soglia. Un cilindro è verde senza essere piano. Il confine tra colori non è una certificazione di flesso; grigio = non valutabile."),
        QStringLiteral("Rosso = salto di tangenza o posizione; arancio = salto di curvatura; verde = regolare entro soglia nei campioni; grigio = bordo libero o non valutabile. Controlla 31 punti per bordo condiviso nello stesso corpo: non confronta corpi separati né nodi interni alle facce. Non certifica G1/G2 sull'intero bordo."),
        QStringLiteral("Denti blu: più lunghi dove la curva piega di più, disegnati verso l’esterno della piega. Inviluppo arancio: andamento del pettine. Su una curva piana, un flesso regolare passa per curvatura zero e cambia lato; in 3D non esiste un lato firmato unico. Pettine sulle curve dello schizzo, sui bordi del corpo o sulla curva 3D, campionato per tratti: non certifica assenza di salti."),
        QStringLiteral("Ciano: U costante; magenta: V costante. Griglia sulla superficie e pettini lungo le sue curve U/V. Con Curvatura normale si misura la piega della superficie in quella direzione; disattivandola si misura la curvatura completa della curva U/V, che può piegare anche su un piano. Denti più lunghi = piega più stretta; scala comune alle due famiglie. Le direzioni U/V non sono necessariamente quelle di curvatura massima e minima. Ritaglio grafico sui contorni e sui fori, approssimato dalla tessellazione; analisi campionata.")};
    auto refresh = [&] {
        viewport->clearShapeAnalysis();
        const int m=mode->currentIndex();
        help->setText(guides[m]);
        frequency->setEnabled(m==0); angle->setEnabled(m==0); limit->setEnabled(m==1);
        const bool comb = m==4 || m==5;
        autoGain->setEnabled(comb); gain->setEnabled(comb && !autoGain->isChecked()); angleTol->setEnabled(m==3); curvTol->setEnabled(m==2 || m==3);
        form->setRowVisible(frequency,m==0); form->setRowVisible(angle,m==0); form->setRowVisible(limit,m==1);
        form->setRowVisible(autoGain,comb); form->setRowVisible(gain,comb); form->setRowVisible(through,comb);
        form->setRowVisible(angleTol,m==3); form->setRowVisible(curvTol,m==2 || m==3);
        form->setRowVisible(uLines,m==5); form->setRowVisible(vLines,m==5);
        form->setRowVisible(isoSamples,m==5); form->setRowVisible(normalOnly,m==5);
        const int i=body->currentData().toInt();
        if (m!=4 && (i<0 || !objects[i].forgeBody)) { report->setPlainText(QStringLiteral("Per uno schizzo o una curva 3D scegli Pettine.")); return; }
        try {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const double scale=comb?(autoGain->isChecked()?0:gain->value()):limit->value(), at=angleTol->value(), kt=curvTol->value();
            const int faceId=face->currentData().toInt();
            ForgeCad::SurfaceCombOptions isoOptions; isoOptions.faceId=faceId;
            isoOptions.uLines=int(uLines->value()); isoOptions.vLines=int(vLines->value());
            isoOptions.samples=int(isoSamples->value()); isoOptions.normalOnly=normalOnly->isChecked();
            ForgeCad::ShapeAnalysis result;
            report->setPlainText(QStringLiteral("Calcolo in corso…"));
            viewport->calculateShapeAnalysis([&] {
                if(i>=0) result=m==5 ? ForgeCad::analyzeSurfaceComb(objects[i],scale,isoOptions) : ForgeCad::analyzeShape(objects[i],m,scale,at,kt,faceId);
                else {
                    const auto &sketch=sketches[-2-i];
                    ForgeCad::Kernel::Frame3 frame; double height;
                    ForgeCad::forgeSketchFrame(sketch,0,frame,height);
                    std::vector<ForgeCad::ForgeCurve> curves;
                    for(const auto &segment:ForgeCad::forgeSketchSegments(sketch))
                        curves.push_back(std::make_shared<ForgeCad::Kernel::TrimmedCurve<3>>(
                            ForgeCad::Kernel::embedCurve(segment.curve,frame),segment.range.lo,segment.range.hi));
                    double sketchScale=scale;
                    if(scale==0) {
                        ForgeCad::Kernel::Vec3 lo,hi; bool first=true; double maxK=0;
                        for(const auto &curve:curves) {
                            const auto breaks=curve->breakpoints(curve->domain());
                            for(std::size_t j=1;j<breaks.size();++j) for(int k=0;k<=24;++k) {
                                ForgeCad::Kernel::Vec3 p,c;
                                if(!ForgeCad::curveCurvature(*curve,breaks[j-1]+(breaks[j]-breaks[j-1])*(k+.001)/24.002,p,c)) continue;
                                if(first) { lo=hi=p; first=false; }
                                for(int axis=0;axis<3;++axis) { lo[axis]=std::min(lo[axis],p[axis]); hi[axis]=std::max(hi[axis],p[axis]); }
                                maxK=std::max(maxK,ForgeCad::Kernel::norm(c));
                            }
                        }
                        sketchScale=maxK>0?.15*ForgeCad::Kernel::distance(lo,hi)/maxK:1;
                    }
                    for(const auto &geometry:curves) {
                        ExtrusionObject curve; curve.curve=geometry;
                        const auto part=ForgeCad::analyzeShape(curve,4,sketchScale,at,kt);
                        result.lines+=part.lines; result.report+=part.report;
                    }
                }
            });
            viewport->shapeAnalysis_=std::move(result);
            viewport->shapeAnalysisFace_=faceId;
            if(i>=0 && faceId>=0) viewport->shapeAnalysisFaceDisplay_=ForgeCad::analysisFaceDisplay(objects[i].display,faceId);
            viewport->shapeAnalysis_.report.prepend(QStringLiteral("%1 — %2").arg(body->currentText(),face->currentText()));
            QApplication::restoreOverrideCursor();
            viewport->shapeAnalysisBody_=i; viewport->shapeAnalysisMode_=m; viewport->shapeAnalysisThrough_=through->isChecked();
            viewport->zebraFrequency_=float(frequency->value()); viewport->zebraAngle_=float(angle->value()*M_PI/180);
            report->setPlainText(viewport->shapeAnalysis_.report.join(QLatin1Char('\n')));
            viewport->update();
        } catch(const std::exception &e) { QApplication::restoreOverrideCursor(); report->setPlainText(QString::fromUtf8(e.what())); }
    };
    QObject::connect(autoGain,&QCheckBox::toggled,&dialog,[&] { gain->setEnabled((mode->currentIndex()==4 || mode->currentIndex()==5) && !autoGain->isChecked()); });
    QObject::connect(through,&QCheckBox::toggled,&dialog,[&](bool checked) { viewport->shapeAnalysisThrough_=checked; viewport->update(); });
    QObject::connect(apply,&QPushButton::clicked,&dialog,refresh);
    QObject::connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,refresh);
    QObject::connect(face,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,refresh);
    QObject::connect(body,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&] {
        viewport->cancelReferencePick(); pickFace->setChecked(false); fillFaces(); refresh();
    });
    QObject::connect(pickFace,&QPushButton::clicked,&dialog,[&](bool checked) {
        if(!checked) { viewport->cancelReferencePick(); return; }
        const QString error=viewport->beginReferencePick(ForgeCad::DatumRoleFace,-1);
        if(!error.isEmpty()) { pickFace->setChecked(false); report->setPlainText(error); }
        else report->setPlainText(QStringLiteral("Clicca una faccia del corpo nella vista. Esc termina la scelta."));
    });
    viewport->setReferencePickCallback([&](bool picked, GeometryRef ref) {
        pickFace->setChecked(false); viewport->cancelReferencePick();
        if(!picked) return;
        // Gli ID di faccia devono riferirsi proprio alla geometria risultante.
        if(ref.kind!=5 || !candidates.contains(ref.index)) {
            report->setPlainText(QStringLiteral("Scegli una faccia di un corpo risultante visibile.")); return;
        }
        const int row=body->findData(ref.index);
        if(row<0) return;
        { const QSignalBlocker blocker(body); body->setCurrentIndex(row); }
        fillFaces();
        const int faceRow=face->findData(ref.point.subshape);
        if(faceRow<1) { report->setPlainText(QStringLiteral("Faccia non disponibile sul corpo risultante.")); refresh(); return; }
        face->setCurrentIndex(faceRow);
    });
    QObject::connect(frequency,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&] { if(mode->currentIndex()==0) refresh(); });
    QObject::connect(angle,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&] { if(mode->currentIndex()==0) refresh(); });
    refresh();
    runModeless(window,viewport,dialog);
    viewport->cancelReferencePick();
    viewport->setReferencePickCallback(nullptr);
    viewport->clearShapeAnalysis();
}

void microFeaturesDialog(QWidget *parent, CadViewport *viewport) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    int index = -1;
    const SceneSelection selection = viewport->selection();
    if (selection.kind == SceneObjectKind::Extrusion && selection.index >= 0 && selection.index < bodies.size()
        && bodies.at(selection.index).forgeBody)
        index = selection.index;
    for (int k = bodies.size() - 1; k >= 0 && index < 0; --k)
        if (bodies.at(k).visible && bodies.at(k).forgeBody) index = k;
    if (index < 0) {
        QMessageBox::information(parent, QStringLiteral("Micro-geometrie"), QStringLiteral("Nella scena non ci sono corpi."));
        return;
    }
    const ExtrusionObject &body = bodies.at(index);
    QStringList lines;
    QVector<GeometryRef> marks;
    try {
        marks = microFeatureMarks(body, index, lines);
    } catch (const std::exception &failure) {
        QMessageBox::warning(parent, QStringLiteral("Micro-geometrie"), QString::fromUtf8(failure.what()));
        return;
    }
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("Micro-geometrie di %1").arg(body.name));
    if (lines.isEmpty()) {
        box.setText(QStringLiteral("Nessuna faccia sottile (sotto 0,05 mm), nessuno spigolo corto (sotto 0,05 mm) "
                                   "e nessuno spigolo quasi tangente (tra 0,06 e 10 gradi)."));
    } else {
        box.setText(QStringLiteral("%1 micro-geometrie, segnate nella vista. Di solito sono rumore di una feature a monte "
                                   "(loft, offset, estensioni) e fanno fallire raccordi e offset: conviene correggerle li'.")
                        .arg(lines.size()));
        box.setDetailedText(lines.join(QLatin1Char('\n')));
    }
    viewport->setReferenceMarks(marks);
    box.exec();
    viewport->setReferenceMarks({});
}

void massPropertiesDialog(QWidget *parent, CadViewport *viewport) {
    const QVector<ExtrusionObject> &bodies = viewport->extrusions();
    QVector<int> candidates;
    for (int index = 0; index < bodies.size(); ++index)
        if (bodies.at(index).curve || bodies.at(index).forgeBody) candidates.append(index);
    if (candidates.isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("Proprieta' di massa"), QStringLiteral("Nella scena non ci sono corpi."));
        return;
    }
    FunctionDialogPanel dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Proprieta' di massa"));
    dialog.resize(620, 640);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *bodyBox = new QComboBox(&dialog);
    bodyBox->addItem(QStringLiteral("Tutti i solidi visibili"));
    for (int index : candidates)
        bodyBox->addItem(bodies.at(index).visible ? bodies.at(index).name : bodies.at(index).name + QStringLiteral(" (nascosto)"));
    const SceneSelection selection = viewport->selection();
    if (selection.kind == SceneObjectKind::Extrusion && candidates.contains(selection.index)) bodyBox->setCurrentIndex(int(candidates.indexOf(selection.index)) + 1);
    struct Material {
        const char *name;
        double density;  // g/cm^3
    };
    static const Material materials[] = {
        {"Acciaio", 7.85}, {"Acciaio inox AISI 304", 8.00}, {"Ghisa", 7.20}, {"Alluminio", 2.70}, {"Ottone", 8.50}, {"Rame", 8.96},
        {"Titanio", 4.51}, {"PP (polipropilene)", 0.905}, {"PE-HD", 0.955}, {"PE-LD", 0.92}, {"PET", 1.38}, {"PETG", 1.27},
        {"ABS", 1.05}, {"PC (policarbonato)", 1.20}, {"PA6", 1.13}, {"POM", 1.41}, {"PVC rigido", 1.40}, {"PS", 1.05}, {"PMMA", 1.18},
        {"Acqua", 1.00}};
    auto *materialBox = new QComboBox(&dialog);
    for (const Material &m : materials) materialBox->addItem(QString::fromUtf8(m.name));
    materialBox->addItem(QStringLiteral("Personalizzato"));
    auto *densityBox = new ForgeCad::ExpressionSpinBox(&dialog);
    densityBox->setDecimals(5);
    densityBox->setRange(1e-6, 100.0);
    densityBox->setSuffix(QStringLiteral(" g/cm³"));
    QSettings settings;
    materialBox->setCurrentIndex(qBound(0, settings.value(QStringLiteral("mass/material"), 0).toInt(), materialBox->count() - 1));
    densityBox->setValue(settings.value(QStringLiteral("mass/density"), materials[0].density).toDouble());
    form->addRow(QStringLiteral("Corpo:"), bodyBox);
    form->addRow(QStringLiteral("Materiale:"), materialBox);
    form->addRow(QStringLiteral("Densita':"), densityBox);
    layout->addLayout(form);
    auto *text = new QTextBrowser(&dialog);
    text->setOpenLinks(false);
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *copy = buttons->addButton(QStringLiteral("Copia"), QDialogButtonBox::ActionRole);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    // Proprieta' (densita' 1) per corpo, calcolate una volta sola.
    QMap<int, ForgeCad::MassReport> cache;
    const auto reportFor = [&](int index) {
        if (!cache.contains(index)) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            cache.insert(index, viewport->massReport(index));
            QApplication::restoreOverrideCursor();
        }
        return cache.value(index);
    };
    QString plain;
    const auto show = [&] {
        const double rho = densityBox->value() * 1e-3;  // g/mm^3
        ForgeCad::MassReport r;
        QString name;
        if (bodyBox->currentIndex() == 0) {
            QVector<ForgeCad::MassReport> parts;
            for (int index : candidates) {
                const ExtrusionObject &body = bodies.at(index);
                if (!body.visible || !body.solid || body.curve) continue;
                parts.append(reportFor(index));
            }
            if (parts.isEmpty()) {
                text->setHtml(QStringLiteral("<p>Nessun solido visibile.</p>"));
                plain.clear();
                return;
            }
            r = ForgeCad::combineMass(parts);
            name = QStringLiteral("Tutti i solidi visibili (%1; i volumi che si sovrappongono si contano due volte)").arg(parts.size());
        } else {
            const int index = candidates.at(bodyBox->currentIndex() - 1);
            r = reportFor(index);
            name = bodies.at(index).name;
        }
        const auto num = [](double v, int digits = 6) { return QString::number(v, 'g', digits); };
        const QString linearUnit = ForgeCad::lengthUnitSymbol(ForgeCad::displayLengthUnit());
        const double lengthScale = ForgeCad::millimetersPerUnit(ForgeCad::displayLengthUnit());
        const double inertiaScale = lengthScale * lengthScale;
        QStringList lines;
        QString html = QStringLiteral("<h3>%1</h3>").arg(name.toHtmlEscaped());
        const auto row = [&](const QString &label, const QString &value) {
            html += QStringLiteral("<tr><td style='padding-right:14px;color:#9fb3c8'>%1</td><td>%2</td></tr>").arg(label, value);
            lines.append(label + QStringLiteral("\t") + QString(value).remove(QRegularExpression(QStringLiteral("<[^>]*>"))));
        };
        if (!r.ok) {
            text->setHtml(html + QStringLiteral("<p style='color:#ff7b72'>Calcolo non riuscito: %1</p>").arg(r.error.toHtmlEscaped()));
            plain.clear();
            return;
        }
        html += QStringLiteral("<table>");
        if (r.kind == ForgeCad::MassReport::Kind::Curve) {
            row(QStringLiteral("Lunghezza"), ForgeCad::formatLength(r.length));
            row(QStringLiteral("Baricentro della curva"), QStringLiteral("X %1, Y %2, Z %3")
                .arg(ForgeCad::formatLength(r.centroid[0]), ForgeCad::formatLength(r.centroid[1]), ForgeCad::formatLength(r.centroid[2])));
        } else if (r.kind == ForgeCad::MassReport::Kind::Sheet) {
            row(QStringLiteral("Area della superficie"), ForgeCad::formatArea(r.area));
            if (r.hasCentroid)
                row(QStringLiteral("Baricentro dell'area"), QStringLiteral("X %1, Y %2, Z %3")
                    .arg(ForgeCad::formatLength(r.centroid[0]), ForgeCad::formatLength(r.centroid[1]), ForgeCad::formatLength(r.centroid[2])));
        } else {
            const double mass = rho * r.volume;
            row(QStringLiteral("Volume"), ForgeCad::formatVolume(r.volume));
            row(QStringLiteral("Area della superficie"), ForgeCad::formatArea(r.area));
            row(QStringLiteral("Densita'"), num(densityBox->value()) + QStringLiteral(" g/cm³"));
            row(QStringLiteral("Massa"), QStringLiteral("<b>%1 g</b> (%2 kg)").arg(num(mass, 9), num(mass * 1e-3, 9)));
            if (r.hasCentroid) {
                row(QStringLiteral("Baricentro"), QStringLiteral("X %1, Y %2, Z %3")
                    .arg(ForgeCad::formatLength(r.centroid[0]), ForgeCad::formatLength(r.centroid[1]), ForgeCad::formatLength(r.centroid[2])));
                // Momenti (Ixx = \\int (y^2 + z^2) dm) e prodotti d'inerzia (Pxy = \\int x y dm), in g mm^2.
                const auto tensorRows = [&](const double t[3][3], const QString &where) {
                    row(QStringLiteral("Momenti d'inerzia %1").arg(where),
                        QStringLiteral("Ixx %1, Iyy %2, Izz %3 g·%4²")
                            .arg(num(rho * t[0][0] / inertiaScale, 9), num(rho * t[1][1] / inertiaScale, 9),
                                 num(rho * t[2][2] / inertiaScale, 9), linearUnit));
                    row(QStringLiteral("Prodotti d'inerzia %1").arg(where),
                        QStringLiteral("Pxy %1, Pxz %2, Pyz %3 g·%4²")
                            .arg(num(-rho * t[0][1] / inertiaScale, 9), num(-rho * t[0][2] / inertiaScale, 9),
                                 num(-rho * t[1][2] / inertiaScale, 9), linearUnit));
                };
                tensorRows(r.inertia, QStringLiteral("al baricentro"));
                double moments[3], axes[3][3];
                ForgeCad::principalMoments(r.inertia, moments, axes);
                for (int k = 0; k < 3; ++k)
                    row(QStringLiteral("Momento principale I%1").arg(k + 1),
                        QStringLiteral("%1 g·%2² lungo (%3, %4, %5), raggio d'inerzia %6")
                            .arg(num(rho * moments[k] / inertiaScale, 9), linearUnit, num(axes[0][k], 6),
                                 num(axes[1][k], 6), num(axes[2][k], 6),
                                 ForgeCad::formatLength(r.volume > 0.0 ? std::sqrt(std::max(0.0, moments[k]) / r.volume) : 0.0, 6)));
                const double origin[3] = {0.0, 0.0, 0.0};
                double about[3][3];
                ForgeCad::inertiaAbout(r, origin, about);
                tensorRows(about, QStringLiteral("all'origine"));
            }
        }
        html += QStringLiteral("</table><p style='color:#8aa0b4'>Calcolo: %1.<br>Unita': %2, g. Prodotti d'inerzia con il segno positivo (Pxy = ∫ x y dm); "
                               "il tensore d'inerzia ha fuori diagonale -Pxy.</p>")
                    .arg(r.method.toHtmlEscaped(), linearUnit);
        text->setHtml(html);
        plain = name + QStringLiteral("\n") + lines.join(QLatin1Char('\n'));
    };
    QObject::connect(bodyBox, &QComboBox::currentIndexChanged, &dialog, show);
    QObject::connect(materialBox, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        if (index >= 0 && index < int(std::size(materials))) densityBox->setValue(materials[index].density);
        QSettings().setValue(QStringLiteral("mass/material"), index);
    });
    QObject::connect(densityBox, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        QSettings().setValue(QStringLiteral("mass/density"), value);
        show();
    });
    QObject::connect(copy, &QPushButton::clicked, &dialog, [&] { QGuiApplication::clipboard()->setText(plain); });
    show();
    dialog.exec();
}
