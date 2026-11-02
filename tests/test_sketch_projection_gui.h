static void sketchProjection() {
    using namespace ForgeCad;
    using namespace Kernel;
    const auto reference = [](int sketch, int entity) {GeometryRef ref; ref.kind=7; ref.index=sketch; ref.element={1,entity,-1}; return ref;};
    const auto spline = [](const QVector<QPointF> &points) {
        CurveObject curve; curve.tool=DrawingTool::Nurbs; curve.degree=2;
        curve.controlPoints=points; curve.knots={0,0,0,1,1,1}; return curve;
    };
    DocumentState state;
    SketchObject first, second;
    first.name=QStringLiteral("Prima curva e curva esclusa"); first.plane=0;
    second.name=QStringLiteral("Seconda curva e curva esclusa"); second.plane=1;
    first.curves={spline({{0,0},{0,2},{4,3}}),spline({{20,20},{22,20},{25,25}})};
    second.curves={spline({{0,1},{1,4},{4,2}}),spline({{40,40},{42,40},{45,45}})};
    state.sketches={first,second};
    CadViewport view; view.loadDocument(state);
    ExtrusionObject feature;
    feature.name=QStringLiteral("Doppia proiezione"); feature.feature=BodyFeature::ProjectedCurve; feature.projectionMode=1;
    feature.ruledFirst={reference(0,0)}; feature.ruledSecond={reference(1,0)};
    require(sketchesOf(feature)==QVector<int>({0,1}) && bodyOperands(feature).isEmpty(), "dipendenze dei due schizzi, nessun corpo consumato");
    ExtrusionObject other=feature; other.ruledSecond[0].element.element=1;
    require(CadViewport::previewKey(feature,0)!=CadViewport::previewKey(other,0), "la curva scelta invalida la chiave anteprima");
    other.projectionMode=0;
    require(CadViewport::previewKey(feature,0)!=CadViewport::previewKey(other,0), "la modalita' invalida la chiave anteprima");
    view.requestPreview(feature,-1);
    QElapsedTimer wait; wait.start();
    while (!view.preview_.valid && view.preview_.error.isEmpty() && wait.elapsed()<10000) {QApplication::processEvents(); QThread::msleep(1);}
    require(view.preview_.valid && !view.preview_.display.edges.isEmpty() && view.preview_.error.isEmpty(), "anteprima asincrona senza corpo B-rep");
    require(view.createBody(feature).isEmpty(), "creazione curva da due schizzi");
    view.clearPreview();
    require(view.extrusions_.size()==1 && view.extrusions_[0].curves.size()==1 && !view.extrusions_[0].display.edges.isEmpty(), "una componente visibile, solo entita' selezionate");
    require(isImplicitSketchProjection(*view.extrusions_[0].curve), "intersezione implicita senza fit");
    const auto endpoint = [&] {const auto &curve=view.extrusions_[0].curve; return curve->point(curve->domain().hi);};
    const auto before=endpoint();
    require(before.x()<5 && before.y()<5 && before.z()<5, "le curve grigie non entrano nel risultato");
    const auto featureId=view.extrusions_[0].featureId;
    view.undo(); require(view.extrusions_.isEmpty(), "Undo unico della creazione");
    view.redo(); require(view.extrusions_.size()==1 && view.extrusions_[0].featureId==featureId && distance(endpoint(),before)<1e-11, "Redo conserva identita' e geometria");

    for (bool cache : {false,true}) {
        QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes,cache).isEmpty(), "codec nativo salva input anche con curva implicita");
        DocumentState decoded; require(decodeDocument(bytes,decoded).isEmpty(), "codec nativo rilegge doppia proiezione");
        require(decoded.extrusions[0].projectionMode==1 && decoded.extrusions[0].ruledFirst[0].element.element==0
            && decoded.extrusions[0].ruledSecond[0].index==1, "riferimenti precisi persistenti, formato invariato");
        CadViewport reopened; reopened.loadDocument(decoded);
        require(reopened.extrusions_[0].error.isEmpty() && reopened.extrusions_[0].curves.size()==1, "rigenerazione dopo riapertura");
        const auto &curve=reopened.extrusions_[0].curve;
        require(distance(curve->point(curve->domain().hi),before)<1e-11, "stessa curva dopo riapertura");
    }
    // Modificare ciascuno dei due input rigenera la stessa feature.
    view.sketches_[0].curves[0].controlPoints[2].ry()+=1;
    view.regenerateDependents(0);
    require(view.extrusions_[0].error.isEmpty() && distance(endpoint(),before)>.1, "modifica primo schizzo rigenera risultato");
    const auto firstChanged=endpoint();
    view.sketches_[1].curves[0].controlPoints[2].ry()+=2;
    view.regenerateDependents(1);
    require(view.extrusions_[0].error.isEmpty() && distance(endpoint(),firstChanged)>.1, "modifica secondo schizzo rigenera risultato");
    auto missing=view.extrusions_[0]; missing.ruledSecond[0].element.element=999;
    buildGeometry(missing,0,view.sketches_,view.extrusions_);
    require(!missing.error.isEmpty() && !missing.curve && missing.curves.isEmpty(), "riferimento mancante non riagganciato per vicinanza");
    auto parallel=view.sketches_; parallel[1].plane=0;
    auto invalid=feature; buildGeometry(invalid,0,parallel,{});
    require(!invalid.error.isEmpty() && !invalid.curve, "piani paralleli: errore leggibile senza risultato parziale");
    QTemporaryDir directory;
    require(!exportBodies(directory.filePath("implicit.step"),view.exportableBodies(),ExportFormat::StepAP242).isEmpty(), "STEP non approssima nascostamente curva implicita");
    SketchObject converted;
    require(!appendProjectedCurve(converted,view.extrusions_[0].curve,view.extrusions_[0].curve->domain(),false,true).isEmpty(), "conversione in schizzo non introduce fit");
    ExtrusionObject sweep; sweep.feature=BodyFeature::Sweep; sweep.sketchIndex=0; sweep.sweepPath=1; sweep.firstBody=0;
    buildGeometry(sweep,1,view.sketches_,view.extrusions_);
    require(!sweep.error.isEmpty() && !sweep.forgeBody, "sweep implicito non introduce fit o cache non serializzabile");

    QMainWindow window; auto *dialogView=new CadViewport(&window); window.setCentralWidget(dialogView);
    dialogView->loadDocument(state);
    bool rows=false, callback=false;
    QTimer watchdog; watchdog.setInterval(10000);
    QObject::connect(&watchdog,&QTimer::timeout,&window,[&] {for(auto *dialog:window.findChildren<QDialog *>()) dialog->reject();});
    watchdog.start();
    QTimer::singleShot(0,&window,[&] {
        for(auto *dialog:window.findChildren<QDialog *>()) if(dialog->objectName()==QStringLiteral("projectionDialog")) {
            const auto *mode=dialog->findChild<QComboBox *>(QStringLiteral("projectionMode"));
            auto *one=dialog->findChild<QListWidget *>(QStringLiteral("projectionEntities0"));
            auto *two=dialog->findChild<QListWidget *>(QStringLiteral("projectionEntities1"));
            rows=mode && mode->currentIndex()==1 && one && two && one->count()==2 && two->count()==2
                && one->item(0)->checkState()==Qt::Checked && one->item(1)->checkState()==Qt::Unchecked
                && two->item(0)->checkState()==Qt::Checked && two->item(1)->checkState()==Qt::Unchecked;
            auto *buttons=dialog->findChild<QDialogButtonBox *>(); if(buttons) buttons->button(QDialogButtonBox::Ok)->click();
        }
    });
    require(projectionDialog(&window,dialogView,QStringLiteral("Test due schizzi"),-1,feature,[&](const ExtrusionObject &d) {
        callback=d.projectionMode==1 && d.firstBody==-1 && d.sketchIndex==-1 && d.ruledFirst.size()==1 && d.ruledSecond.size()==1
            && d.ruledFirst[0].element.element==0 && d.ruledSecond[0].element.element==0;
        return QString();
    }), "conferma pannello doppia proiezione senza solidi");
    require(rows && callback && !dialogView->referencePicking() && !dialogView->interactionLocked(), "curve individuali, conferma e ripristino pannello");
    std::cout<<"PASS GUI doppia proiezione: selezione precisa, anteprima, Undo/Redo, dipendenze, due rigenerazioni, codec cache/no-cache, errori e guardie senza fit"<<std::endl;
}
