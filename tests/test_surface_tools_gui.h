static void surfaceTools() {
    using namespace ForgeCad;
    using namespace Kernel;
    ExtrusionObject iconFeature; iconFeature.feature=BodyFeature::SurfaceTools; iconFeature.operation=-1;
    QVector<QImage> surfaceIcons;
    for(int mode=0;mode<3;++mode) {
        iconFeature.surfaceTool.mode=mode;
        const auto icon=commandIcon(ForgeCad::Gui::featureIconName(iconFeature));
        require(!icon.isNull(),"icona della funzione superficie disponibile");
        surfaceIcons.append(icon.pixmap(32).toImage());
    }
    require(surfaceIcons[0]!=surfaceIcons[1] && surfaceIcons[1]!=surfaceIcons[2] && surfaceIcons[0]!=surfaceIcons[2],"icone superfici distinte");
    require(surfaceIcons[1]!=commandIcon("loft").pixmap(32).toImage() && surfaceIcons[2]!=commandIcon("spline").pixmap(32).toImage(),"rete e poli hanno icone proprie");
    const auto imported = [](const Body &shape, const QString &name) {
        ExtrusionObject b;
        b.feature = BodyFeature::Imported;
        b.name = name;
        b.importData = QByteArray::fromStdString(writeStep({{name.toStdString(), shape, false, {0, 0, 0}, nullptr, {}}}));
        return b;
    };
    const auto wall = [](double x) { return makePlaneSheet(Frame3(Vec3(x, 5, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)), 20); };
    DocumentState document;
    document.extrusions = {imported(surface_fixture::patch(true), "NURBS con foro"), imported(wall(14), "Riferimento finale")};
    ExtrusionObject grid;
    grid.feature = BodyFeature::SpatialSketch;
    grid.name = "Curve della rete";
    grid.spatialSketch.nextId = 7;
    for (int i = 0; i < 3; ++i) {
        SpatialCurve3D e;
        e.id = i + 1;
        e.points = {{0., 5. * i, 0.}, {10., 5. * i, 1. * i}};
        grid.spatialSketch.entities.append(e);
    }
    for (int j = 0; j < 3; ++j) {
        SpatialCurve3D e;
        e.id = j + 4;
        e.points = {{5. * j, 0., 0.}, {5. * j, 10., 1. * j}};
        grid.spatialSketch.entities.append(e);
    }
    document.extrusions.append(grid);
    CadViewport view;
    view.tessellationQuality_ = 0;
    view.loadDocument(document);
    require(view.extrusions_[0].forgeBody && !surfaceBasisHash(view.extrusions_[0].forgeBody).isEmpty(), "sorgente STEP mantiene NURBS");
    ExtrusionObject network;
    network.feature = BodyFeature::SurfaceTools;
    network.surfaceTool.mode = 1;
    network.name = "Rete 3 x 3";
    for (int i = 0; i < 6; ++i) {
        GeometryRef r;
        r.kind = 9;
        r.index = 2;
        r.featureId = view.extrusions_[2].featureId;
        r.point.subshape = i + 1;
        r.point.geometry = kSpatialCurveReference;
        (i < 3 ? network.ruledFirst : network.ruledSecond).append(r);
    }
    clientOk(view.createBody(network));
    require(view.extrusions_[3].forgeBody->faces().size() == 4, "rete creata con quattro facce");
    require(bodyOperands(view.extrusions_[3]) == QVector<int>{2}, "dipendenza della rete sulle curve spaziali");
    ExtrusionObject extension;
    extension.feature = BodyFeature::SurfaceTools;
    extension.surfaceTool.mode = 0;
    extension.firstBody = 0;
    extension.name = "Estesa fino al piano";
    const auto base = view.extrusions_[0].forgeBody;
    const auto edge = surface_fixture::edgeNear(*base, Vec3(10, 5, 0));
    extension.blendEdges = {edgeReference(*base, edge, Vec3(10, 5, 0))};
    extension.extentRef.kind = 5;
    extension.extentRef.index = 1;
    extension.extentRef.featureId = view.extrusions_[1].featureId;
    const auto target = view.extrusions_[1].forgeBody;
    extension.extentRef.point = faceReference(*target, target->faces().front(), Vec3(14, 5, 0));
    extension.surfaceTool.maximum = 10;
    clientOk(view.createBody(extension));
    require(view.extrusions_[4].forgeBody && bodyOperands(view.extrusions_[4]) == QVector<int>({0, 1}),
            "estensione associativa con base e riferimento");
    require(view.extrusions_[4].modelBodyId == view.extrusions_[0].modelBodyId, "estensione continua lo stesso corpo logico");
    ExtrusionObject poles;
    poles.feature = BodyFeature::SurfaceTools;
    poles.surfaceTool.mode = 2;
    poles.firstBody = 4;
    poles.name = "Poli e pesi";
    poles.surfaceTool.basis = surfaceBasisHash(view.extrusions_[4].forgeBody);
    poles.surfaceTool.edits = {{1, 1, 0, 0, .2, 1.2}};
    clientOk(view.createBody(poles));
    require(view.extrusions_[5].forgeBody && view.extrusions_[5].modelBodyId == view.extrusions_[4].modelBodyId,
            "poli come stadio del corpo");
    const auto changed = writeBodyBinary(*view.extrusions_[5].forgeBody);
    view.undo();
    require(view.extrusions_.size() == 5, "un solo Undo per modifica poli");
    view.redo();
    require(writeBodyBinary(*view.extrusions_[5].forgeBody) == changed, "Redo esatto dei poli");
    auto keyChanged = poles;
    keyChanged.surfaceTool.edits[0].weightFactor = 1.3;
    require(CadViewport::previewKey(poles, 5) != CadViewport::previewKey(keyChanged, 5), "peso invalida anteprima");
    keyChanged = poles;
    keyChanged.surfaceTool.tolerance = 1e-5;
    require(CadViewport::previewKey(poles, 5) != CadViewport::previewKey(keyChanged, 5), "tolleranza invalida anteprima");
    auto invalid = view.extrusions_[5];
    invalid.surfaceTool.edits[0].weightFactor = -1;
    require(!view.updateBody(5, invalid).isEmpty() && writeBodyBinary(*view.extrusions_[5].forgeBody) == changed,
            "peso invalido rifiutato atomicamente");
    invalid = view.extrusions_[5];
    invalid.surfaceTool.basis = QByteArray(32, 'x');
    require(!view.updateBody(5, invalid).isEmpty(), "griglia diversa richiede riselezione esplicita");
    require(!view.moveFeatureTo(3, 0).isEmpty(), "riordino non precede dipendenze della rete");
    clientOk(view.setFeatureSuppressed(5, true));
    require(view.extrusions_[5].suppressed && view.modelBodyTip(view.modelBodyIndexForFeature(4)) == 4,
            "soppressione espone stadio precedente");
    view.undo();
    require(!view.extrusions_[5].suppressed, "Undo della soppressione");
    auto shifted = imported(wall(16), "Riferimento finale");
    clientOk(view.updateBody(1, shifted));
    require(view.extrusions_[4].forgeBody && view.extrusions_[5].forgeBody, "rigenerazione dei poli dopo modifica del riferimento");
    require(view.extrusions_[5].surfaceTool.edits[0].dz == .2, "delta dei poli conservato durante rigenerazione");
    view.undo();
    document = view.currentDocument();
    QByteArray encoded;
    clientOk(encodeDocument(document, encoded, true));
    DocumentState restored;
    clientOk(decodeDocument(encoded, restored));
    require(restored.extrusions[5].surfaceTool.basis == document.extrusions[5].surfaceTool.basis &&
                restored.extrusions[5].surfaceTool.edits[0].weightFactor == 1.2,
            "parametri poli e pesi persistono");
    require(restored.extrusions[4].surfaceTool.maximum == 10 && restored.extrusions[3].surfaceTool.mode == 1,
            "parametri estensione e rete persistono");
    for (auto &b : restored.extrusions)
        b.cachedGeometry = false;
    CadViewport rebuilt;
    rebuilt.tessellationQuality_ = 0;
    rebuilt.loadDocument(restored);
    for (const auto &b : rebuilt.extrusions_)
        require(b.error.isEmpty() && hasGeometry(b), "tutte le feature rigenerano senza cache");
    require(checkBody(*rebuilt.extrusions_[5].forgeBody).empty(), "risultato rigenerato valido");
    QDataStream header(encoded);
    header.skipRawData(7);
    quint32 size = 0;
    header >> size;
    const auto payload = testDocumentUncompress(encoded,11, size);
    const auto tag = QByteArray::fromHex("53524631");
    const int offset = payload.lastIndexOf(tag);
    require(offset > 0, "blocco estensioni presente");
    const auto extras = QJsonDocument::fromJson(surfaceToolsExtras(document.extrusions)).object();
    const auto malformed = [&](const QJsonObject &root) {
        QByteArray suffix;
        QDataStream out(&suffix, QIODevice::WriteOnly);
        out << quint32(0x53524631) << QJsonDocument(root).toJson(QJsonDocument::Compact);
        return nativeFixture(payload.left(offset) + suffix, kDocumentFormatVersion);
    };
    auto bad = extras;
    auto records = bad["features"].toArray();
    auto record = records.last().toObject();
    auto edits = record["edits"].toArray();
    edits.append(edits.first());
    record["edits"] = edits;
    records[records.size() - 1] = record;
    bad["features"] = records;
    const auto retained = restored.version.documentId;
    require(!decodeDocument(malformed(bad), restored).isEmpty() && restored.version.documentId == retained,
            "poli duplicati rifiutati senza stato parziale");
    bad = extras;
    records = bad["features"].toArray();
    records.append(records.first());
    bad["features"] = records;
    require(!decodeDocument(malformed(bad), restored).isEmpty(), "estensioni duplicate rifiutate");
    require(!decodeDocument(nativeFixture(payload.left(offset), kDocumentFormatVersion), restored).isEmpty(), "tipo nuovo senza estensioni rifiutato");
    require(!decodeDocument(nativeFixture(payload, 52), restored).isEmpty(), "declassamento del formato rifiutato");
    QByteArray retainedBytes = "intatto";
    auto invalidDocument = document;
    invalidDocument.extrusions[5].surfaceTool.edits[0].weightFactor = 0;
    require(!encodeDocument(invalidDocument, retainedBytes).isEmpty() && retainedBytes == "intatto",
            "codifica invalida non produce output parziale");
    const auto qa = QStringLiteral("/tmp/forgecad-surface-tools-qa");
    QDir().mkpath(qa);
    QMainWindow window;
    window.resize(1200, 900);
    view.resize(1000, 700);
    view.setViewNormal(0);
    view.fitAll();
    std::exception_ptr failure;
    ExtrusionObject submitted;
    // Tabella XYZ/peso, gabbia, mouse e annullamento sono esercitati sul vero pannello.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "pannello poli aperto");
            auto *table = dialog->findChild<QTableWidget *>("surfacePolesTable");
            require(table && table->rowCount() > 0 && view.surfacePoleOverlay_.size() == table->rowCount(), "gabbia e tabella concordano");
            require(!dialog->findChild<QListWidget *>("surfaceToolEdge")->isVisible(), "controlli degli altri modi nascosti");
            const int row = table->rowCount() / 2;
            const auto start = view.surfacePoleScreen(row);
            const double x = QLocale().toDouble(table->item(row, 2)->text());
            QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&view, &press);
            const auto dragGeneration=view.preview_.generation;
            QMouseEvent move(QEvent::MouseMove, start + QPointF(14, 5), start + QPointF(14, 5), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&view, &move);
            QElapsedTimer duringDrag;duringDrag.start();
            while(duringDrag.elapsed()<120)QApplication::processEvents();
            require(view.preview_.generation==dragGeneration,"nessuna ricostruzione dell'anteprima durante il trascinamento");
            require(!dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->isEnabled(),"conferma sospesa durante il trascinamento");
            QMouseEvent release(QEvent::MouseButtonRelease, start + QPointF(14, 5), start + QPointF(14, 5), Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(&view, &release);
            require(QLocale().toDouble(table->item(row, 2)->text()) != x, "trascinamento modifica coordinate");
            require(view.preview_.generation>dragGeneration,"anteprima richiesta al rilascio");
            const auto point=[&](int r) {return QVector3D(QLocale().toDouble(table->item(r,2)->text()),QLocale().toDouble(table->item(r,3)->text()),QLocale().toDouble(table->item(r,4)->text()));};
            const auto send=[&](QEvent::Type type,QPointF p,Qt::KeyboardModifiers mods=Qt::NoModifier) {
                QMouseEvent e(type,p,p,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,mods);QApplication::sendEvent(&view,&e);
            };
            const int other=row+1,untouched=row+2;
            send(QEvent::MouseButtonPress,view.surfacePoleScreen(other),Qt::ControlModifier);
            send(QEvent::MouseButtonRelease,view.surfacePoleScreen(other),Qt::ControlModifier);
            require(view.surfacePoleSelection_.size()==2,"Ctrl aggiunge un polo senza trascinare");
            send(QEvent::MouseButtonPress,view.surfacePoleScreen(other),Qt::ControlModifier);
            send(QEvent::MouseButtonRelease,view.surfacePoleScreen(other),Qt::ControlModifier);
            require(view.surfacePoleSelection_.size()==1,"Ctrl toglie un polo");
            send(QEvent::MouseButtonPress,view.surfacePoleScreen(other),Qt::ShiftModifier);
            send(QEvent::MouseButtonRelease,view.surfacePoleScreen(other),Qt::ShiftModifier);
            require(view.surfacePoleSelection_.size()==2,"Maiusc aggiunge alla selezione");
            auto orientation=dialog->findChild<QComboBox *>("surfacePolesOrientation");
            require(orientation && !view.surfacePoleLocal_,"assi globali predefiniti");
            for(int axis=0;axis<3;++axis) {
                const auto before=point(row),second=point(other),fixed=point(untouched);
                const auto head=view.surfacePoleAxisScreen(axis),center=view.surfacePoleGizmoCenter();
                require(view.surfacePoleHandleAt(head)==axis,"tutti e tre gli assi sono selezionabili anche lungo la vista");
                const auto end=head+(head-center)*.3;
                send(QEvent::MouseButtonPress,head);send(QEvent::MouseMove,end);send(QEvent::MouseButtonRelease,end);
                const auto delta=point(row)-before;
                require(delta.length()>1e-5 && ((point(other)-second)-delta).length()<1e-5,"gli assi spostano tutto il gruppo dello stesso delta");
                for(int k=0;k<3;++k)if(k!=axis)require(std::fabs(delta[k])<1e-5,"vincolo all'asse globale esatto");
                require((point(untouched)-fixed).length()<1e-7,"poli non selezionati fermi");
            }
            table->item(other,4)->setText(QLocale().toString(double(point(other).z())+2.,'g',17));
            orientation->setCurrentIndex(1);
            const auto local=view.surfacePoleAxes();
            require(view.surfacePoleLocal_ && std::fabs(QVector3D::dotProduct(local[0],local[2]))<1e-6 &&
                QVector3D::dotProduct(QVector3D::crossProduct(local[0],local[1]),local[2])>.999,"assi locali ortonormali e destrorsi");
            require(std::fabs(local[2].z())<.99999,"normale locale segue la superficie deformata");
            const auto localBefore=point(row),localSecond=point(other);
            const auto normalHead=view.surfacePoleAxisScreen(2),normalEnd=normalHead+(normalHead-view.surfacePoleGizmoCenter())*.25;
            send(QEvent::MouseButtonPress,normalHead);send(QEvent::MouseMove,normalEnd);send(QEvent::MouseButtonRelease,normalEnd);
            const auto localDelta=point(row)-localBefore;
            require(localDelta.length()>1e-5 && QVector3D::crossProduct(localDelta,local[2]).length()<1e-5 &&
                ((point(other)-localSecond)-localDelta).length()<1e-5,"gruppo vincolato alla normale locale congelata durante il gesto");
            const auto cancelBefore=point(row),cancelOther=point(other);
            const auto cancelHead=view.surfacePoleAxisScreen(0),cancelEnd=cancelHead+QPointF(15,8);
            send(QEvent::MouseButtonPress,cancelHead);send(QEvent::MouseMove,cancelEnd);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);
            require((point(row)-cancelBefore).length()<1e-7 && (point(other)-cancelOther).length()<1e-7,"Esc ripristina l'intero gruppo");
            table->item(row, 5)->setText("0");
            QElapsedTimer validation;
            validation.start();
            while (validation.elapsed() < 2000 && dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->isEnabled())
                QApplication::processEvents();
            require(!dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->isEnabled(),
                    "peso zero disabilita conferma");
            table->item(row, 5)->setText(QLocale().toString(1.4));
            validation.restart();
            while (validation.elapsed() < 2000 && !dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->isEnabled())
                QApplication::processEvents();
            dialog->grab().save(qa + "/poli-pesi.png");
            dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    const bool polesAccepted = surfaceToolDialog(&window, &view, "Modifica poli NURBS", 5, view.extrusions_[5],
                              [&](const ExtrusionObject &d) {
                                  submitted = d;
                                  return QString();
                              });
    if (failure)
        std::rethrow_exception(failure);
    require(polesAccepted, "pannello poli confermato");
    require(submitted.surfaceTool.edits.size()>=2 && view.surfacePoleOverlay_.isEmpty() && view.surfacePoleSelection_.isEmpty(), "parametri acquisiti e gabbia rimossa");
    require(writeBodyBinary(*view.extrusions_[5].forgeBody) == changed, "anteprima sola non modifica modello");
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Cancel)->click();
    });
    require(!surfaceToolDialog(&window, &view, "Annulla poli", 5, view.extrusions_[5],
                               [&](const ExtrusionObject &) {
                                   throw std::runtime_error("annulla non applica");
                                   return QString();
                               }),
            "annullamento pannello senza applicazione");
    // Picking ordinato nelle due famiglie del pannello rete.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "pannello rete aperto");
            auto *u = dialog->findChild<QListWidget *>("surfaceNetworkU"), *v = dialog->findChild<QListWidget *>("surfaceNetworkV");
            require(u && v && u->count() == 3 && v->count() == 3, "famiglie mostrate in ordine");
            dialog->grab().save(qa + "/rete-curve.png");
            dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, &view, "Rete di curve", 3, view.extrusions_[3],
                              [&](const ExtrusionObject &d) {
                                  require(d.ruledFirst.size() == 3 && d.ruledSecond.size() == 3, "due famiglie inviate");
                                  return QString();
                              }),
            "pannello rete confermato");
    if (failure)
        std::rethrow_exception(failure);
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "pannello estensione aperto");
            require(dialog->findChild<QListWidget *>("surfaceToolEdge")->count() == 1, "mostra solo il bordo salvato");
            dialog->findChild<QPushButton *>("surfaceToolPickTarget")->click();
            auto callback = view.refPickFinished_;
            require(bool(callback), "picker riferimento attivo");
            callback(true, view.extrusions_[4].extentRef);
            require(dialog->findChild<QListWidget *>("surfaceToolTargets")->count() == 0, "secondo clic toglie il target salvato");
            callback(true, view.extrusions_[4].extentRef);
            dialog->grab().save(qa + "/estendi-riferimento.png");
            dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok)->click();
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, &view, "Estendi fino a riferimento", 4, view.extrusions_[4],
                              [&](const ExtrusionObject &d) {
                                  require(d.extentRef.kind == 5 && d.extentRef.index == 1, "faccia scelta conserva destinazione");
                                  require(resolveEdgeReference(*view.extrusions_[d.firstBody].forgeBody,d.blendEdges.front(),1e-5)==edge,"pannello mantiene il bordo esatto anche con alias comuni");
                                  return view.updateBody(4,d);
                              }),
            "pannello estensione confermato");
    if (failure)
        std::rethrow_exception(failure);
    // Nuovo comando: niente bordo implicito, picking del bordo seguito dal target.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog, "nuova estensione aperta");
            auto *list = dialog->findChild<QListWidget *>("surfaceToolEdge");
            auto *targetBox = dialog->findChild<QComboBox *>("surfaceToolTarget");
            auto *targetPick = dialog->findChild<QPushButton *>("surfaceToolPickTarget");
            auto *ok = dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok);
            require(list->count() == 0 && targetBox->currentIndex() == -1, "nuovo flusso vuoto anche con definizione precompilata");
            require(!ok->isEnabled() && !targetPick->isEnabled(), "target e conferma attendono il bordo");
            require(view.referencePicking() && view.refPickRoles_ == (DatumRoleCurve | DatumRoleLine), "picking bordo attivo automaticamente");
            auto callback = view.refPickFinished_;
            callback(true, view.extrusions_[4].extentRef);
            require(list->count() == 0 && view.referencePicking(), "una faccia non diventa un bordo");
            GeometryRef boundary;
            boundary.kind = 4;
            boundary.index = 0;
            boundary.featureId = view.extrusions_[0].featureId;
            boundary.point = edgeReference(*base, edge, Vec3(10, 5, 0));
            callback(true, boundary);
            require(list->count() == 1 && targetPick->isEnabled() && !ok->isEnabled(), "bordo scelto, target ancora richiesto");
            require(view.referencePicking() && view.refPickRoles_ == (DatumRolePlane | DatumRoleFace), "passaggio automatico al target");
            callback(false, {});
            require(!view.referencePicking() && list->count() == 1 && !ok->isEnabled(), "Esc conserva il bordo senza applicare");
            targetPick->click();
            callback(true, view.extrusions_[4].extentRef);
            GeometryRef plane; plane.kind = 1; plane.index = 0;
            callback(true, plane);
            auto *selected = dialog->findChild<QListWidget *>("surfaceToolTargets");
            require(selected->count() == 2 && view.referencePicking(), "target multipli senza riavviare il picker");
            callback(true, plane);
            require(selected->count() == 1, "secondo clic toglie solo il target indicato");
            callback(true, plane);
            selected->setCurrentRow(1);
            dialog->findChild<QPushButton *>("surfaceToolRemoveTarget")->click();
            require(selected->count() == 1, "rimozione dalla lista dei target");
            callback(false, {});
            require(!view.referencePicking() && selected->count() == 1, "Esc conserva tutti i target scelti");
            dialog->findChild<QPushButton *>("surfaceToolPickEdge")->click();
            callback(true, boundary);
            require(targetBox->currentIndex() == -1 && !ok->isEnabled(), "cambio bordo richiede nuovamente il target");
            callback(true, view.extrusions_[4].extentRef);
            // Il pulsante richiama refresh sincrono prima di applicare.
            QTimer::singleShot(100, dialog, [ok] { ok->click(); });
        } catch (...) {
            failure = std::current_exception();
            if (dialog)
                dialog->reject();
        }
    });
    require(surfaceToolDialog(&window, &view, "Nuova estensione nella vista", -1, view.extrusions_[4],
                              [&](const ExtrusionObject &d) {
                                  require(d.firstBody == 0 && d.blendEdges.size() == 1, "corpo dedotto dal bordo selezionato");
                                  require(resolveEdgeReference(*base, d.blendEdges.front(), 1e-5) == edge, "bordo grafico esatto");
                                  require(d.extentRef.kind == 5 && d.extentRef.index == 1, "target grafico esatto");
                                  return QString();
                              }), "nuova estensione confermata dopo entrambe le selezioni");
    if (failure)
        std::rethrow_exception(failure);
    require(!view.referencePicking() && view.pickBodies_.isEmpty() && !view.refPickFinished_, "picking ripulito alla chiusura");
    // Due facce di corpi distinti formano insieme il riferimento spezzato.
    const std::vector<Vec2> corners{{14, -1}, {16, 5}, {14, 11}};
    std::vector<ProfileSegment> sides;
    for (int i = 1; i < int(corners.size()); ++i)
        sides.push_back({std::make_shared<Line<2>>(corners[i - 1], corners[i] - corners[i - 1]), {0, distance(corners[i - 1], corners[i])}});
    const auto profile = buildProfile(sides, 1e-9);
    const auto folded = makeSheetExtrusion(Frame3(Vec3(0, 0, -2), Vec3(0, 0, 1), Vec3(1, 0, 0)), profile.chains, 4);
    DocumentState multi;
    multi.extrusions = {imported(surface_fixture::patch(), "Base"), imported(facesAsSheet(folded, {folded.faces()[0]}), "Target A"),
                         imported(facesAsSheet(folded, {folded.faces()[1]}), "Target B")};
    CadViewport multiView;
    multiView.tessellationQuality_ = 0;
    multiView.loadDocument(multi);
    auto combined = extension;
    combined.firstBody = 0;
    combined.surfaceTool.maximum = 100;
    const auto multiBase = multiView.extrusions_[0].forgeBody;
    combined.blendEdges = {edgeReference(*multiBase, surface_fixture::edgeNear(*multiBase, Vec3(10, 5, 0)), Vec3(10, 5, 0))};
    for (int i : {1, 2}) {
        GeometryRef ref;
        ref.kind = 5; ref.index = i; ref.featureId = multiView.extrusions_[i].featureId;
        const auto shape = multiView.extrusions_[i].forgeBody;
        Vec3 p; require(faceInteriorPoint(*shape, shape->faces().front(), p), "punto interno target");
        ref.point = faceReference(*shape, shape->faces().front(), p);
        combined.surfaceTool.targets.append(ref);
    }
    combined.extentRef = combined.surfaceTool.targets.front();
    clientOk(multiView.createBody(combined));
    require(bodyOperands(multiView.extrusions_[3]) == QVector<int>({0, 1, 2}), "dipendenze di tutti i target");
    auto incomplete = multiView.extrusions_[3];
    incomplete.surfaceTool.targets.removeLast();
    require(CadViewport::previewKey(incomplete, 3) != CadViewport::previewKey(multiView.extrusions_[3], 3), "anteprima invalida al cambiare dei target");
    const auto retainedMulti = writeBodyBinary(*multiView.extrusions_[3].forgeBody);
    require(!multiView.updateBody(3, incomplete).isEmpty() && writeBodyBinary(*multiView.extrusions_[3].forgeBody) == retainedMulti,
            "target incompleto rifiutato senza perdere il risultato");
    require(!multiView.moveFeatureTo(3, 1).isEmpty(), "estensione non precede nessuno dei target");
    require(!multiView.moveFeatureTo(3, 2).isEmpty(), "estensione non precede il secondo target anche se il primo la precede");
    multi = multiView.currentDocument();
    QByteArray multiBytes;
    clientOk(encodeDocument(multi, multiBytes, false));
    DocumentState multiRestored;
    clientOk(decodeDocument(multiBytes, multiRestored));
    require(multiRestored.extrusions[3].surfaceTool.targets.size() == 2 &&
            multiRestored.extrusions[3].surfaceTool.targets[1].point.name == multi.extrusions[3].surfaceTool.targets[1].point.name,
            "persistono tutti i target e i nomi topologici");
    CadViewport multiRebuilt;
    multiRebuilt.tessellationQuality_ = 0;
    multiRebuilt.loadDocument(multiRestored);
    require(multiRebuilt.extrusions_[3].error.isEmpty() && multiRebuilt.extrusions_[3].forgeBody &&
            checkBody(*multiRebuilt.extrusions_[3].forgeBody, {true, true}).empty(), "riferimento composto rigenerato senza cache");
    auto malformedMulti = multi.extrusions;
    malformedMulti[3].surfaceTool.targets.append(malformedMulti[3].surfaceTool.targets.front());
    const auto beforeMalformed = multiRestored.extrusions[3].surfaceTool.targets.size();
    require(!applySurfaceToolsExtras(surfaceToolsExtras(malformedMulti), multiRestored.extrusions).isEmpty() &&
            multiRestored.extrusions[3].surfaceTool.targets.size() == beforeMalformed, "duplicati rifiutati atomicamente dal codec");
    QTemporaryDir dir;
    auto saved = document;
    const auto path = dir.filePath("superfici.prt");
    clientOk(saveDocumentFile(path, saved));
    clientOk(checkoutWorkspaceDocument(path, saved, "qa"));
    RevisionRecord revision;
    clientOk(checkinWorkspaceDocument(path, saved, {}, "Superfici", revision));
    DocumentState historical;
    WorkspacePaths workspace;
    clientOk(locateDocumentWorkspace(path, workspace));
    RevisionStore store(workspace.revisions);
    clientOk(readNativeRevision(store, revision.version, historical));
    require(historical.extrusions[5].surfaceTool.edits[0].weightFactor == 1.2 && historical.extrusions[3].ruledFirst.size() == 3,
            "revisioni immutabili conservano nuove funzioni");
    PdfWindow shell;
    shell.viewport_->loadDocument(historical);
    shell.refreshDocumentEditAccess();
    for (const char *name : {"surfaceExtendToAction", "surfaceNetworkAction", "surfacePolesAction"}) {
        const auto *action = shell.findChild<QAction *>(name);
        require(action && !action->isEnabled(), "nuovi comandi protetti in consultazione");
    }
    const int count = shell.viewport_->extrusions_.size();
    require(!shell.viewport_->createBody(poles).isEmpty() && shell.viewport_->extrusions_.size() == count,
            "API mutazione protetta readonly");
    individualFillControls();
    std::cout << "PASS superfici: rete/estensione/poli, dipendenze, rigenerazione, Undo/Redo, UI/mouse, formato53, errori atomici, "
                 "revisioni e sola lettura\n";
}
