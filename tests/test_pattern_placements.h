#pragma once
#include "../cad_pattern.h"
#include "../cad_document_io.h"
#include <limits>

namespace {
void patternPlacementTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto grid = [](int first, int second = 1) {
        PatternParameters p; p.kind=0; p.count=first; p.count2=second; p.spacing=2; p.spacing2=3;
        GeometryRef x; x.kind=2; x.index=0;
        GeometryRef y=x; y.index=1; p.refs={x,y}; return p;
    };
    const auto reject = [](const PatternParameters &p, const QString &expected) {
        std::vector<Transform3> placements; QString error;
        require(!patternPlacements(p,0,{}, {},placements,&error) && error==expected, "conteggi non validi rifiutati con errore coerente");
        require(placements.empty() && placements.capacity()==0, "rifiuto prima di allocare o generare trasformazioni");
        placements.resize(400); const auto capacity=placements.capacity();
        require(!patternPlacements(p,0,{}, {},placements,nullptr) && placements.empty()
                && placements.capacity()==capacity, "output precedente svuotato senza nuove allocazioni su errore");
    };
    const auto tooMany = QStringLiteral("Troppe istanze (al massimo 500).");
    const auto tooFew = QStringLiteral("Servono almeno due istanze.");
    // La regressione della review: il codice precedente genera prima 249.999
    // trasformazioni, poi restituisce false lasciandole nell'output.
    reject(grid(500,500),tooMany);
    for (const auto &p : {grid(502),grid(2,251),grid(65536,65536),grid(std::numeric_limits<int>::max(),2),
                         grid(std::numeric_limits<int>::max(),std::numeric_limits<int>::max())}) reject(p,tooMany);
    for (const auto &p : {grid(1),grid(0,500),grid(500,0),grid(-1),grid(1,-1),
                         grid(std::numeric_limits<int>::min(),std::numeric_limits<int>::min())}) reject(p,tooFew);
    auto missing=grid(500,500); missing.refs.clear(); reject(missing,tooMany);
    // Conserva il contratto esistente: massimo 500 trasformazioni oltre
    // all'originale, quindi 501 posizioni totali sono ancora ammesse.
    for (const auto &p : {grid(2),grid(500),grid(501),grid(5,100),grid(3,167)}) {
        std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error), "ripetizione sul limite accettata");
        require(placements.size()==std::size_t(p.count)*std::size_t(p.count2)-1, "numero di copie e originale invariati");
        std::size_t next=0;
        for (int j=0;j<p.count2;++j) for (int i=0;i<p.count;++i) if (i||j)
            require(distance(placements[next++].applyToPoint({1,2,3}), {1+2.*i,2+3.*j,3})<1e-12,
                    "ordine e coordinate della griglia invariati");
    }
    {
        auto p=grid(3,2); p.spacing=-2; p.flip2=true;
        std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error) && placements.size()==5
                && distance(placements.back().applyToPoint({}),{-4,-3,0})<1e-12, "passo negativo e secondo verso conservati");
        p.spacing=0;
        require(!patternPlacements(p,0,{}, {},placements,&error) && placements.empty(), "passo nullo rifiutato senza output residuo");
        p.spacing=2; p.refs[1]=p.refs[0];
        require(!patternPlacements(p,0,{}, {},placements,&error) && placements.empty(), "direzioni parallele rifiutate senza output residuo");
    }
    PatternParameters circular; circular.kind=1; circular.refs={grid(2).refs[0]}; circular.refs[0].index=2;
    circular.count2=std::numeric_limits<int>::max(); // inutilizzato per la ripetizione circolare
    for (int count : {2,500,501}) {
        auto p=circular; p.count=count; std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error) && placements.size()==std::size_t(count-1), "limite circolare preservato");
    }
    for (int count : {502,std::numeric_limits<int>::max()}) { auto p=circular; p.count=count; reject(p,tooMany); }
    for (int count : {1,0,-1,std::numeric_limits<int>::min()}) { auto p=circular; p.count=count; reject(p,tooFew); }
    {
        auto p=circular; p.count=4; std::vector<Transform3> placements; QString error;
        require(patternPlacements(p,0,{}, {},placements,&error) && placements.size()==3
                && distance(placements[0].applyToPoint({1,0,0}),{0,1,0})<1e-12
                && distance(placements[2].applyToPoint({1,0,0}),{0,-1,0})<1e-12, "giro completo senza duplicare l'originale");
        p.count=3; p.angle=180;
        require(patternPlacements(p,0,{}, {},placements,&error)
                && distance(placements.back().applyToPoint({1,0,0}),{-1,0,0})<1e-12, "settore include l'estremo");
        p.spread=false; p.angle=45; p.flip=true;
        require(patternPlacements(p,0,{}, {},placements,&error)
                && distance(placements.back().applyToPoint({1,0,0}),{0,-1,0})<1e-12, "passo angolare e verso conservati");
        p.angle=0;
        require(!patternPlacements(p,0,{}, {},placements,&error) && placements.empty(), "angolo nullo svuota output");
    }
    {
        auto mirror=circular; mirror.kind=2; mirror.count=std::numeric_limits<int>::min();
        mirror.refs[0].kind=1; mirror.refs[0].index=2;
        std::vector<Transform3> placements; QString error;
        require(patternPlacements(mirror,0,{}, {},placements,&error) && placements.size()==1
                && distance(placements[0].applyToPoint({2,3,4}),{-2,3,4})<1e-12,
                "simmetria non usa i conteggi delle ripetizioni");
    }
    {
        DocumentState source; ExtrusionObject base; base.feature=BodyFeature::Primitive;
        ExtrusionObject pattern; pattern.feature=BodyFeature::Pattern; pattern.firstBody=0;
        pattern.pattern=grid(std::numeric_limits<int>::max(),std::numeric_limits<int>::max());
        source.extrusions={base,pattern}; QByteArray bytes;
        require(encodeDocument(source,bytes,false).isEmpty(), "fixture con conteggi estremi codificata");
        DocumentState restored;
        require(decodeDocument(bytes,restored).isEmpty() && restored.extrusions.size()==2, "fixture con conteggi estremi riletta");
        reject(restored.extrusions[1].pattern,tooMany);
    }
    std::cout << "PASS pattern placements: limiti, overflow, output, griglie, rotazioni e simmetria" << std::endl;
}
}

// Strumento dello sweep ripetuto attorno a Y, mantenendo il bersaglio fermo.
static void sweepFeaturePatternTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    for (int operation : {1, 2}) {
        DocumentState state;
        ExchangeBody stock; stock.body = makeCylinder(Frame3({0,0,0}, {0,1,0}, {1,0,0}), 8, 6);
        ExtrusionObject base; base.feature = BodyFeature::Imported;
        base.importData = QByteArray::fromStdString(writeStep({stock}));
        SketchObject profile; profile.plane = kFacePlane;
        profile.frame.normal[1] = 1; profile.frame.normal[2] = 0;
        const double start = operation == 1 ? 5 : -1, end = operation == 1 ? 9 : 7;
        profile.frame.origin[1] = start;
        CurveObject circle; circle.tool = DrawingTool::Circle;
        circle.controlPoints = {QPointF(4,0), QPointF(4.5,0)}; profile.curves = {circle};
        SketchObject path; path.segments = {{QPointF(4,start), QPointF(4,end)}};
        state.sketches = {profile, path};
        ExtrusionObject sweep; sweep.feature = BodyFeature::Sweep; sweep.name = QStringLiteral("Sweep da ripetere");
        sweep.sketchIndex = 0; sweep.pathSketch = 1;
        sweep.mergeOperation = operation; sweep.mergeAuto = false; sweep.mergeBodies = {0};
        state.extrusions = {base, sweep};
        CadViewport view; view.loadDocument(state);
        require(view.extrusions()[1].error.isEmpty() && view.extrusions()[1].forgeBody,
                "fixture sweep associato al cilindro");
        require(patternFeatureSupported(view.extrusions()[1]) && !patternFeatureSupported(view.extrusions()[0]),
                "stesso criterio per sweep e corpo senza funzione associata");
        auto unsupported = view.extrusions()[1]; unsupported.sweepSurface = true;
        require(!patternFeatureSupported(unsupported), "sweep di superficie non offre fusione di funzione");
        unsupported.sweepSurface = false; unsupported.mergeOperation = 0;
        require(!patternFeatureSupported(unsupported), "sweep autonomo resta ripetibile come corpo");
        const auto original = writeBodyBinary(*view.extrusions()[1].forgeBody);
        const auto volume = [&](int copies) {
            return M_PI * (64 * 6 + (operation == 1 ? copies * .25 * 3 : -copies * .25 * 6));
        };
        const auto check = [&](int copies) {
            const auto tips = view.resultBodiesBefore(-1);
            require(tips.size() == 1, "ripetere lo sweep mantiene il corpo di base unico");
            const auto &result = view.extrusions()[tips.first()];
            require(result.forgeBody && result.error.isEmpty() && checkBody(*result.forgeBody).empty(),
                    "topologia della ripetizione sweep");
            require(std::abs(massProperties(*result.forgeBody).volume - volume(copies)) < 1e-7,
                    "volume analitico: bersaglio fermo e sole istanze dello strumento");
            require(writeBodyBinary(*view.extrusions()[1].forgeBody) == original, "sweep sorgente immutato");
        };
        require(std::abs(massProperties(*view.extrusions()[1].forgeBody).volume - volume(1)) < 1e-7,
                "volume analitico dello sweep iniziale");
        ExtrusionObject repeated; repeated.feature = BodyFeature::Pattern; repeated.firstBody = 1;
        repeated.pattern.kind = 1; repeated.pattern.featureOnly = true; repeated.pattern.count = 4;
        repeated.pattern.excluded = {2}; GeometryRef y; y.kind = 2; y.index = 1; repeated.pattern.refs = {y};
        QMainWindow window; std::exception_ptr failure;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>();
            try {
                auto *what = dialog ? dialog->findChild<QComboBox *>("patternWhat") : nullptr;
                auto *body = dialog ? dialog->findChild<QComboBox *>("patternBody") : nullptr;
                require(what && what->isEnabled() && what->currentIndex() == 1,
                        "pannello permette di scegliere la funzione sweep");
                require(body && body->currentText().contains(sweep.name) && body->currentData().toInt() == 1,
                        "pannello identifica la feature oltre al corpo logico");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                require(dialog->result() == QDialog::Accepted, "pannello crea sweep circolare su Y");
            } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
        });
        patternDialog(&window, &view, "Ripetizione sweep", -1, repeated,
            [&](const ExtrusionObject &d) { return view.createBody(d, {d.firstBody}); });
        if (failure) std::rethrow_exception(failure);
        check(3);
        auto changed = view.extrusions()[2]; changed.pattern.excluded = {0,2};
        require(view.updateBody(2, changed).isEmpty(), "esclusione originale dello sweep"); check(2);
        changed = view.extrusions()[2]; changed.pattern.excluded.clear();
        require(view.updateBody(2, changed).isEmpty(), "ripristino occorrenze sweep"); check(4);
        view.undo(); check(2); view.redo(); check(4);
        for (bool cache : {false, true}) {
            QByteArray bytes; DocumentState decoded;
            require(encodeDocument(view.currentDocument(), bytes, cache).isEmpty()
                    && decodeDocument(bytes, decoded).isEmpty(), "codec ripetizione sweep");
            view.loadDocument(decoded); require(view.rebuildAllFeatures() == 0, "rigenera ripetizione sweep"); check(4);
        }
        // Una lavorazione successiva deve sopravvivere alla ripetizione
        // della funzione storica selezionata, in entrambe le modalita'.
        auto centerProfile = profile; centerProfile.curves[0].controlPoints = {QPointF(0,0), QPointF(.25,0)};
        auto centerPath = path; centerPath.segments = {{QPointF(0,start), QPointF(0,end)}};
        state.sketches += QVector<SketchObject>{centerProfile, centerPath};
        auto later = sweep; later.sketchIndex = 2; later.pathSketch = 3; later.mergeBodies = {1}; later.name = "Lavorazione successiva";
        state.extrusions.append(later); view.loadDocument(state);
        const auto laterBytes = writeBodyBinary(*view.extrusions()[2].forgeBody);
        for (int kind : {0,1}) {
            auto historical = repeated; historical.secondBody = 2; historical.pattern.kind = kind;
            historical.pattern.excluded.clear(); historical.pattern.count = kind == 0 ? 2 : 4;
            historical.pattern.refs[0].index = kind == 0 ? 0 : 1;
            historical.pattern.spacing = -4;
            // Lineare: la copia cade al centro dove esiste la lavorazione piu' piccola.
            buildGeometry(historical, 3, view.sketches(), view.extrusions());
            require(historical.forgeBody && historical.error.isEmpty(), "ripetizione di funzione storica con bersaglio successivo");
            const double expected = kind == 0 ? volume(2) : volume(4) + M_PI*.0625*(operation == 1 ? 3 : -6);
            require(std::abs(massProperties(*historical.forgeBody).volume - expected) < 1e-6,
                    "le lavorazioni successive sopravvivono e non vengono ripetute");
            require(bodyOperands(historical).contains(1) && bodyOperands(historical).contains(2), "dipendenze sorgente e destinazione");
            require(writeBodyBinary(*view.extrusions()[2].forgeBody) == laterBytes, "bersaglio successivo immutato");
        }
        {
            auto historical=repeated; historical.secondBody=2; historical.pattern.excluded.clear();
            require(view.createBody(historical).isEmpty() && view.resultBodiesBefore(-1)==QVector<int>{3},
                    "creazione storica sostituisce il risultato successivo nello stesso corpo");
            const double expected=volume(4)+M_PI*.0625*(operation==1?3:-6);
            require(std::abs(massProperties(*view.extrusions()[3].forgeBody).volume-expected)<1e-6,"volume storico creato");
            view.undo();require(view.extrusions().size()==3,"undo ripetizione storica");view.redo();
            auto invalid=view.extrusions()[3];invalid.pattern.excluded={0};
            require(!view.updateBody(3,invalid).isEmpty(),"originale storico non escludibile senza annullare i passi successivi");
            for(bool cache:{true,false}) {
                QByteArray bytes;DocumentState restored;
                require(encodeDocument(view.currentDocument(),bytes,cache).isEmpty()&&decodeDocument(bytes,restored).isEmpty(),"codec sorgente storica e bersaglio");
                view.loadDocument(restored);
                require(view.extrusions()[3].firstBody==1&&view.extrusions()[3].secondBody==2&&view.rebuildAllFeatures()==0,
                        "rigenerazione delle dipendenze storiche dopo riapertura");
                require(std::abs(massProperties(*view.extrusions()[3].forgeBody).volume-expected)<1e-6,"volume storico riletto");
            }
            view.loadDocument(state);
        }
        // Stato nuovo: nessuna sorgente implicita, poi selezione della funzione storica.
        {
            QMainWindow chooser; ExtrusionObject empty; empty.feature = BodyFeature::Pattern;
            std::exception_ptr error;
            QTimer::singleShot(0, &chooser, [&] {
                auto *dialog = chooser.findChild<QDialog *>();
                try {
                    auto *body = dialog->findChild<QComboBox *>("patternBody");
                    auto *what = dialog->findChild<QComboBox *>("patternWhat");
                    require(body->currentIndex() == -1 && !dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
                            "nessuna ultima funzione preimpostata");
                    what->setCurrentIndex(1);
                    require(what->currentIndex() == 1, "si puo' scegliere modalita funzione prima della sorgente");
                    require(body->findData(1) >= 0, "funzione storica disponibile nel pannello");
                    body->setCurrentIndex(body->findData(1));
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                } catch (...) { error = std::current_exception(); dialog->reject(); }
            });
            int selected = -1, target = -1;
            patternDialog(&chooser, &view, "Scelta sorgente", -1, empty, [&](const ExtrusionObject &d) {
                selected = d.firstBody; target = d.secondBody; return QString();
            });
            if (error) std::rethrow_exception(error);
            require(selected == 1 && target == 2, "pannello distingue sorgente storica e risultato attuale");
        }
        {
            view.setHistoryPosition(2);
            QMainWindow chooser; ExtrusionObject empty;empty.feature=BodyFeature::Pattern;std::exception_ptr failure;
            QTimer::singleShot(0,&chooser,[&]{auto *dialog=chooser.findChild<QDialog *>();
                try {auto *body=dialog->findChild<QComboBox *>("patternBody");
                    require(body->findData(1)>=0&&body->findData(2)<0,"sorgenti successive al cursore storyboard escluse");
                }catch(...){failure=std::current_exception();}dialog->reject();});
            patternDialog(&chooser,&view,"Sorgenti prima del cursore",-1,empty,[](const ExtrusionObject &){return QString();});
            if(failure)std::rethrow_exception(failure);
            view.setHistoryPosition(3);
        }
        // La faccia visibile appartiene allo sweep storico, non all'ultimo.
        {
            const Body &current = *view.extrusions()[2].forgeBody;
            bool found = false;
            for (FaceId f : current.faces()) if (current.face(f).surface->type() == SurfaceType::Cylinder
                && std::fabs(static_cast<const CylindricalSurface &>(*current.face(f).surface).radius()-.5) < 1e-8) {
                Vec3 point; require(faceInteriorPoint(current,f,point), "punto interno sulla faccia della funzione");
                GeometryRef ref; ref.kind = 5; ref.index = 2; ref.point = faceReference(current,f,point);
                require(view.sourceFeatureForFace(ref) == 1, "viewport risale alla funzione della faccia scelta"); found = true;
            }
            require(found, "faccia storica ancora visibile dopo la lavorazione successiva");
        }
        {
            QMainWindow chooser; chooser.resize(900,650);
            // Il viewport deve essere figlio della stessa finestra: solo cosi'
            // showEvent del pannello incorporato riproduce il blocco reale.
            auto *attached = new CadViewport(&chooser); chooser.setCentralWidget(attached);
            attached->loadDocument(state);
            auto *dock = new QDockWidget(&chooser); dock->setObjectName("modelDock");
            auto *tree = new StoryboardTree(dock); dock->setWidget(tree); chooser.addDockWidget(Qt::LeftDockWidgetArea,dock);
            auto *item = new QTreeWidgetItem(tree, {"Sweep storico"}); item->setData(0,Qt::UserRole,kTreeExtrusion); item->setData(0,Qt::UserRole+1,1);
            item->setFlags(item->flags()|Qt::ItemIsUserCheckable); item->setCheckState(0,Qt::Checked);
            chooser.show();
            ExtrusionObject empty; empty.feature = BodyFeature::Pattern;
            int selected=-1, target=-1; std::exception_ptr error;
            QTimer::singleShot(80,&chooser,[&] {
                auto *dialog=chooser.findChild<QDialog *>();
                try {
                    require(dock->isEnabled() && tree->isEnabled() && !tree->historyRail()->isEnabled(), "storyboard incorporata selezionabile dopo showEvent");
                    auto *sourceStep=dialog->findChild<QPushButton *>("patternSourceStep");
                    auto *refStep=dialog->findChild<QPushButton *>("patternReferenceStep");
                    auto *parametersStep=dialog->findChild<QPushButton *>("patternParametersStep");
                    auto *count=dialog->findChild<QSpinBox *>("patternCount");
                    auto *hint=dialog->findChild<QLabel *>("patternStepHint");
                    require(sourceStep&&sourceStep->isChecked()&&hint&&hint->text().contains("Passaggio 1")
                            &&hint->text().contains("storyboard")&&count&&!count->isEnabled(),"passaggio sorgente visibile, parametri in attesa");
                    QPointF point=tree->visualItemRect(item).center();
                    QMouseEvent click(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(tree->viewport(),&click);
                    auto *body=dialog->findChild<QComboBox *>("patternBody");
                    require(body->currentData().toInt()==1 && dialog->findChild<QComboBox *>("patternWhat")->currentIndex()==1,
                            "clic storyboard seleziona la funzione storica");
                    require(parametersStep->isChecked()&&count->isEnabled()&&hint->text().contains("Passaggio 3")
                            &&hint->text().contains(sweep.name),"scelta aggiorna passaggio e riepilogo della sorgente");
                    refStep->click();
                    require(refStep->isChecked()&&!count->isEnabled()&&attached->referencePicking()
                            &&hint->text().contains("Passaggio 2")&&dock->isEnabled(),"passaggio riferimento esplicito con storyboard disponibile");
                    parametersStep->click();
                    require(parametersStep->isChecked()&&count->isEnabled()&&!attached->referencePicking(),"ritorno ai parametri mantiene asse e sorgente");
                    require(item->checkState(0)==Qt::Checked && attached->extrusions().size()==3, "scelta senza cambiare visibilita o storia");
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                } catch (...) {error=std::current_exception();dialog->reject();}
            });
            patternDialog(&chooser,attached,"Scelta storyboard",-1,empty,[&](const ExtrusionObject &d){selected=d.firstBody;target=d.secondBody;return QString();});
            if(error)std::rethrow_exception(error);
            require(selected==1&&target==2&&dock->isEnabled()&&tree->historyRail()->isEnabled()&&!attached->interactionLocked(),"scelta storyboard e ripristino controlli");
        }
        {
            DocumentState single;single.extrusions={base};CadViewport plain;plain.loadDocument(single);
            QMainWindow chooser;ExtrusionObject empty;empty.feature=BodyFeature::Pattern;empty.firstBody=0;std::exception_ptr failure;
            QTimer::singleShot(0,&chooser,[&]{auto *dialog=chooser.findChild<QDialog *>();
                try{auto *what=dialog->findChild<QComboBox *>("patternWhat");
                    require(what->isEnabled(),"modo funzione disponibile anche partendo da corpo senza funzione ripetibile");
                    what->setCurrentIndex(1);
                    require(what->currentIndex()==1&&!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
                            "modo funzione rimane attivo in attesa della scelta nella viewport");
                }catch(...){failure=std::current_exception();}dialog->reject();});
            patternDialog(&chooser,&plain,"Scelta modo funzione",-1,empty,[](const ExtrusionObject &){return QString();});
            if(failure)std::rethrow_exception(failure);
        }
        // Non mascherare l'errore specifico della ricostruzione dello strumento.
        auto broken = view.extrusions(); broken[1].sketchIndex = -1;
        auto badPattern = repeated;
        buildGeometry(badPattern, 2, view.sketches(), broken);
        require(!badPattern.forgeBody && badPattern.error.contains(QStringLiteral("profilo")),
                "errore del profilo sweep conservato");
    }
    std::cout << "PASS sweep feature pattern: Y, volumi analitici, pannello, esclusioni, codec, Undo/Redo" << std::endl;
}

// Risultati applicativi, persistenza e indipendenza delle singole copie.
void patternResultTests() {
    sweepFeaturePatternTests();
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    for (bool sheet : {false, true}) {
        ExtrusionObject source;
        source.feature = BodyFeature::Primitive;
        source.primitive.kind = PrimitiveKind::Box;
        source.primitive.size[0] = 2; source.primitive.size[1] = 3; source.primitive.size[2] = 4;
        if (sheet) {
            source.feature = BodyFeature::Imported;
            ExchangeBody exchange; exchange.body = makePlaneSheet(Frame3({3,0,0},{0,0,1},{1,0,0}), 1);
            source.importData = QByteArray::fromStdString(writeStep({exchange}));
        }
        DocumentState initial; initial.extrusions = {source};
        CadViewport view; view.loadDocument(initial);
        const auto original = writeBodyBinary(*view.extrusions()[0].forgeBody);
        ExtrusionObject repeated; repeated.feature = BodyFeature::Pattern;
        repeated.firstBody = 0; repeated.name = "Ripetizione";
        repeated.pattern.count = 4; repeated.pattern.spacing = 5;
        GeometryRef axis; axis.kind = 2; axis.index = 0; repeated.pattern.refs = {axis};
        const auto check = [&](int count) {
            const auto tips = view.resultBodiesBefore(-1);
            require(tips.size() == count, "ogni componente e' un corpo indipendente");
            QSet<quint64> ids;
            double total = 0;
            for (int i : tips) {
                const auto &part = view.extrusions()[i];
                require(part.error.isEmpty() && part.forgeBody && part.solid != sheet, "tipo e geometria dei risultati");
                require(checkBody(*part.forgeBody).empty(), "topologia valida dei risultati separati");
                ids.insert(part.modelBodyId);
                total += sheet ? faceArea(*part.forgeBody, part.forgeBody->faces().front()) : massProperties(*part.forgeBody).volume;
            }
            require(ids.size() == count, "identita' logiche distinte");
            require(std::abs(total - count * (sheet ? 4.0 : 24.0)) < 1e-8, "area o volume delle copie disgiunte");
            require(writeBodyBinary(*view.extrusions()[0].forgeBody) == original, "originale immutato");
        };
        require(view.createBody(repeated, {0}).isEmpty(), "creazione ripetizione separata"); check(4);
        require(view.setFeatureSuppressed(1, true).isEmpty(), "sopprime tutta la ripetizione");
        require(view.resultBodiesBefore(-1).size() == 1, "soppressione ripristina solo il corpo sorgente");
        require(view.setFeatureSuppressed(1, false).isEmpty(), "ripristina tutta la ripetizione"); check(4);
        const auto oldOccurrence = view.extrusions()[3].modelBodyId;
        const auto oldGeometry = writeBodyBinary(*view.extrusions()[3].forgeBody);
        auto changed = view.extrusions()[1]; changed.pattern.excluded = {1,3};
        require(view.updateBody(1, changed).isEmpty(), "esclude occorrenze"); check(2);
        const auto surviving = view.extrusions()[view.resultBodiesBefore(-1).last()];
        require(surviving.modelBodyId == oldOccurrence && writeBodyBinary(*surviving.forgeBody) == oldGeometry, "esclusione non rinumera un'altra occorrenza");
        view.undo(); check(4); view.redo(); check(2);
        changed = view.extrusions()[1]; changed.pattern.spacing = 9;
        require(view.updateBody(1, changed).isEmpty(), "cambia passo conservando esclusioni"); check(2);
        require(view.extrusions()[1].pattern.excluded == QVector<int>({1,3}), "numeri esclusi conservati");
        QByteArray bytes;
        require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "salva componenti ed esclusioni");
        DocumentState restored; require(decodeDocument(bytes, restored).isEmpty(), "legge componenti ed esclusioni");
        view.loadDocument(restored); check(2);
        require(view.rebuildAllFeatures() == 0, "rigenera componenti ed esclusioni"); check(2);
        changed = view.extrusions()[1]; changed.pattern.excluded.clear(); changed.pattern.count = 5;
        require(view.updateBody(1, changed).isEmpty(), "ripristina copie e aumenta conteggio"); check(5);
        // Modificare un risultato derivato apre e aggiorna la stessa ripetizione.
        changed = view.extrusions()[1]; changed.pattern.count = 3;
        require(view.updateBody(view.resultBodiesBefore(-1).last(), changed).isEmpty(), "modifica attraverso un risultato derivato"); check(3);
        // Escludere tutto e' un errore transazionale: il documento resta valido.
        changed = view.extrusions()[1]; changed.pattern.excluded = {0,1,2};
        require(!view.updateBody(1, changed).isEmpty(), "tutte le occorrenze escluse rifiutate"); check(3);
        // L'elenco reale del pannello permette esclusione e ripristino.
        {
            CadViewport gui; gui.loadDocument(initial);
            QMainWindow window;
            std::exception_ptr failure;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<QDialog *>();
                try {
                    auto *list = dialog ? dialog->findChild<QListWidget *>("patternOccurrences") : nullptr;
                    require(list && list->count() == 4, "elenco occorrenze nel pannello");
                    list->item(1)->setCheckState(Qt::Unchecked);
                    // refresh ricrea la lista, leggere nuovamente l'elemento.
                    require(list->item(1)->checkState() == Qt::Unchecked, "spunta esclusione aggiornata");
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    require(dialog->result() == QDialog::Accepted, "pannello applica esclusione");
                } catch (...) { failure = std::current_exception(); if (dialog) dialog->reject(); }
            });
            patternDialog(&window, &gui, "Ripetizione", -1, repeated,
                [&](const ExtrusionObject &definition) { return gui.createBody(definition, {definition.firstBody}); });
            if (failure) std::rethrow_exception(failure);
            require(gui.resultBodiesBefore(-1).size() == 3 && gui.extrusions()[1].pattern.excluded == QVector<int>{1}, "esclusione dal pannello costruisce tre corpi");
            // Anche le ripetizioni aggregate dei vecchi documenti si separano
            // quando viene richiesta la rigenerazione completa.
            auto legacy = initial; legacy.extrusions.append(repeated);
            gui.loadDocument(legacy);
            require(gui.rebuildAllFeatures() == 0 && gui.resultBodiesBefore(-1).size() == 4, "rigenera ripetizioni storiche in corpi separati");
        }
        // Occorrenze incluse anche in circolare, griglia e specchio.
        for (int kind : {0,1,2}) {
            CadViewport other; other.loadDocument(initial);
            auto def = repeated; def.pattern.kind = kind;
            def.pattern.count = 4; def.pattern.excluded = {1};
            if (kind == 0) {
                def.pattern.count2 = 2; def.pattern.spacing2 = 8;
                GeometryRef y = axis; y.index = 1; def.pattern.refs.append(y);
            } else if (kind == 1) {
                def.pattern.refs[0].index = 2;
                // Sposta la base solida lontano dall'asse, evitando contatti.
                if (!sheet) { auto far = initial; far.extrusions[0].primitive.origin[0] = 6; other.loadDocument(far); }
            } else { def.pattern.refs[0].kind = 1; def.pattern.refs[0].index = 2; }
            require(other.createBody(def, {0}).isEmpty(), "esclusione nei tre tipi di ripetizione");
            require(other.resultBodiesBefore(-1).size() == (kind == 0 ? 7 : kind == 1 ? 3 : 1), "numero copie dopo esclusione per tipo");
        }
    }
    // Una ripetizione storica puo' alimentare altre feature come aggregato.
    // La sua separazione visiva non deve dimezzare l'operando della scala.
    {
        DocumentState legacy;
        ExtrusionObject base; base.feature=BodyFeature::Primitive;
        base.primitive.kind=PrimitiveKind::Box;
        base.primitive.size[0]=2; base.primitive.size[1]=3; base.primitive.size[2]=4;
        base.primitive.origin[0]=5; base.name="Base legacy";
        ExtrusionObject mirror; mirror.feature=BodyFeature::Pattern; mirror.firstBody=0;
        mirror.pattern.kind=2; mirror.name="Specchio legacy";
        GeometryRef plane; plane.kind=1; plane.index=2; mirror.pattern.refs={plane};
        ExtrusionObject scale; scale.feature=BodyFeature::Scale; scale.firstBody=1;
        scale.scaleFactor=2; scale.name="Scala aggregato";
        legacy.extrusions={base,mirror,scale};
        CadViewport view; view.loadDocument(legacy);
        const auto verify=[&] {
            int root=-1,scaled=-1; QVector<int> children;
            const auto &bodies=view.extrusions();
            for(int i=0;i<bodies.size();++i) {
                if(bodies[i].name==mirror.name) root=i;
                if(bodies[i].name==scale.name) scaled=i;
            }
            require(root>=0 && scaled>root,"madre e scala storiche conservate");
            for(int i=0;i<bodies.size();++i)
                if(bodies[i].feature==BodyFeature::Pattern && bodies[i].patternComponent>0 && bodies[i].firstBody==root) children.append(i);
            require(children.size()==2 && children[0]==root+1 && children[1]==root+2 && scaled>children.last(),
                    "componenti legacy subito dopo la madre e prima dei consumatori");
            require(bodies[root].patternComponent==-1 && bodies[scaled].firstBody==root,
                    "consumatore storico mantiene il riferimento all'aggregato");
            require(bodies[scaled].error.isEmpty() && bodies[scaled].forgeBody
                    && std::abs(massProperties(*bodies[scaled].forgeBody).volume-384)<1e-8,
                    "scala storica riceve entrambe le copie");
            view.setStoryboardPosition(view.storyboard().size());
            require(view.resultBodiesBefore(-1)==QVector<int>{scaled},"scala aggregata consuma tutti i risultati separati");
            view.setStoryboardPosition(view.storyboardIndex(false,root)+1);
            require(view.resultBodiesBefore(-1)==children,"rollback dopo la madre include tutti i risultati");
            for (int child : children)
                require(view.extrusions()[child].visible && !view.extrusions()[child].display.vertices.isEmpty(),
                        "risultati visibili e mesh pronte anche dopo rigenerazioni ripetute");
            require(bodies[children[0]].modelBodyId!=bodies[children[1]].modelBodyId,"identita' separate nel rollback");
            view.setStoryboardPosition(view.storyboard().size());
        };
        require(view.rebuildAllFeatures()==0,"rigenerazione storia legacy con consumatori"); verify();
        require(view.rebuildAllFeatures()==0,"seconda rigenerazione senza duplicazioni"); verify();
        QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes).isEmpty(),"salva aggregato e risultati legacy");
        DocumentState decoded; require(decodeDocument(bytes,decoded).isEmpty(),"legge aggregato e risultati legacy");
        view.loadDocument(decoded); verify();
        require(view.rebuildAllFeatures()==0,"rigenera dopo riapertura legacy"); verify();
        view.undo(); verify(); view.redo(); verify();
        int root=-1, child=-1;
        for(int i=0;i<view.extrusions().size();++i) {
            if(view.extrusions()[i].name==mirror.name) root=i;
            if(view.extrusions()[i].patternComponent==2) child=i;
        }
        const auto survivingId=view.extrusions()[child].modelBodyId;
        auto changed=view.extrusions()[root]; changed.pattern.excluded={0};
        require(view.updateBody(root,changed).isEmpty(),"esclude originale legacy senza cambiare consumatori");
        view.setStoryboardPosition(view.storyboardIndex(false,root)+1);
        auto tips=view.resultBodiesBefore(-1);
        require(tips.size()==1 && view.extrusions()[tips.first()].modelBodyId==survivingId,
                "originale escluso: una sola copia, nessun aggregato duplicato, ID superstite stabile");
        require(!view.extrusions()[root].visible,"aggregato legacy nascosto anche se escluso il primo risultato");
        view.setStoryboardPosition(view.storyboard().size());
        changed=view.extrusions()[root]; changed.pattern.excluded.clear();
        require(view.updateBody(root,changed).isEmpty(),"ripristina originale legacy"); verify();
    }
    // Solidi collegati si uniscono; le shell delle cavita' restano nel solido.
    QString error;
    auto outer = std::make_shared<const Body>(makeBox(Frame3(), 6,6,6));
    auto inner = std::make_shared<const Body>(makeBox(Frame3({1,1,1},{0,0,1},{1,0,0}), 2,2,2));
    auto hollow = forgeBoolean(outer,inner,::BooleanOperation::Difference,&error);
    require(hollow && error.isEmpty(), "solido con cavita' di riferimento");
    auto all = forgePattern(hollow,{Transform3::translation({10,0,0})},true,&error);
    auto separated = forgePatternComponents(all,&error);
    require(error.isEmpty() && separated.size() == 2, "cavita' non esposte come corpi");
    for (const auto &part : separated)
        require(!part->isSheet() && checkBody(*part).empty() && std::abs(massProperties(*part).volume - 208) < 1e-8, "volume netto delle cavita' conservato");
    all = forgePattern(outer,{Transform3::translation({6,0,0})},true,&error);
    require(forgePatternComponents(all,&error).size() == 1, "solidi con faccia comune restano uniti");
    std::cout << "PASS pattern results: corpi solidi/superfici, esclusioni, storia, cavita' e persistenza" << std::endl;
}
