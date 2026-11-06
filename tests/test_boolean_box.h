// Incluso nel runner come membro di ViewportInteractionTest.
static void booleanBoxPicking() {
    using namespace ForgeCad;
    using namespace Kernel;
    DocumentState document;
    for (int i = 0; i < 4; ++i) {
        ExtrusionObject body;
        body.feature = BodyFeature::Primitive;
        body.name = QStringLiteral("Blocco %1").arg(i + 1);
        body.primitive.origin[0] = i * 5;
        body.primitive.size[0] = body.primitive.size[1] = body.primitive.size[2] = 2;
        document.extrusions.append(body);
    }
    const auto bounds = [](CadViewport &view, int index) {
        view.ensureBodyDisplay(index);
        QPolygonF projected;
        for (const auto &p : view.extrusions()[index].display.vertices) projected.append(view.projectWorldPoint(p));
        return projected.boundingRect();
    };
    const auto gesture = [](CadViewport &view, QPointF from, QPointF to, bool drag) {
        QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        view.mousePressEvent(&press);
        if (drag) {
            QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            view.mouseMoveEvent(&move);
        }
        QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        view.mouseReleaseEvent(&release);
    };
    // Prova del viewport senza pannello: filtri storici, selezione attraverso
    // l'interno di una faccia e ripristino del gesto dopo Esc.
    {
        CadViewport view; view.resize(1000, 700); view.loadDocument(document);
        view.setViewNormal(0); view.fitAll();
        view.setPickBodies({1, 2, 3});
        clientOk(view.beginReferencePick(DatumRoleFace, 3));
        QVector<int> picked;
        view.setBodyBoxPickCallback([&](const QVector<int> &values) { picked = values; });
        const auto all = bounds(view, 0).united(bounds(view, 3)).adjusted(-5, -5, 5, 5);
        gesture(view, all.topLeft(), all.bottomRight(), true);
        require(picked == QVector<int>({1, 2}), "rettangolo limita la selezione agli operandi prima della feature");
        const auto center = bounds(view, 1).center();
        const QRectF inside(center - QPointF(6, 6), QSizeF(12, 12));
        gesture(view, inside.topLeft(), inside.bottomRight(), true);
        require(picked.isEmpty(), "finestra non seleziona un corpo incluso solo parzialmente");
        gesture(view, inside.topRight(), inside.bottomLeft(), true);
        require(picked == QVector<int>{1}, "attraversamento anche dentro una faccia senza toccarne i bordi");
        view.extrusions_[2].suppressed = true;
        gesture(view, all.topLeft(), all.bottomRight(), true);
        require(picked == QVector<int>{1}, "corpo soppresso escluso");
        view.cancelReferencePick(); view.setBodyBoxPickCallback({}); view.setPickBodies({});
        require(!view.boxArmed_ && !view.boxSelecting_, "fine selezione ripulisce il rettangolo");
    }
    for (int operation : {0, 1, 2}) {
        PdfWindow window;
        auto *view = window.viewport_;
        window.resize(1300, 900); window.show();
        view->loadDocument(document); view->setViewNormal(0); view->fitAll();
        const auto sceneSelection = view->selection();
        const auto camera = view->camera_.basisMatrix();
        ExtrusionObject initial; initial.operation = operation; initial.firstBody = operation == 0 ? -1 : 0; initial.secondBody = 1;
        ExtrusionObject submitted;
        std::exception_ptr failure;
        bool inspected = false;
        QTimer driver; driver.setInterval(10);
        QObject::connect(&driver, &QTimer::timeout, &window, [&] {
            auto *panel = window.findChild<QDialog *>("booleanDialog");
            if (!panel || !panel->isVisible()) return;
            driver.stop();
            QTimer::singleShot(10000, panel, [panel] { if (panel->isVisible()) panel->reject(); });
            try {
                auto *tools = panel->findChild<QListWidget *>("booleanTools");
                const auto selected = [&] {
                    QVector<int> values;
                    for (int i = 0; i < tools->count(); ++i) {
                        auto *item = tools->item(i);
                        if ((item->flags() & Qt::ItemIsEnabled) && item->checkState() == Qt::Checked)
                            values.append(item->data(Qt::UserRole).toInt());
                    }
                    return values;
                };
                require(view->referencePicking() && bool(view->bodyBoxPickFinished_), "strumenti: rettangolo attivo anche senza preselezionare A");
                require(panel->findChild<QPushButton *>("booleanPickTools")->text() == QStringLiteral("Termina selezione"),
                    "la scelta attiva mostra il comando per terminarla");
                const auto all = bounds(*view, 0).united(bounds(*view, 3)).adjusted(-5, -5, 5, 5);
                gesture(*view, all.topLeft(), all.bottomRight(), true);
                require(selected() == QVector<int>({1, 2, 3}), "tre strumenti insieme, corpo A escluso");
                gesture(*view, all.topLeft(), all.bottomRight(), true);
                require(selected() == QVector<int>({1, 2, 3}), "rettangolo ripetuto mantiene gli strumenti gia spuntati");
                require(view->camera_.basisMatrix() == camera && view->selection() == sceneSelection
                    && view->extrusions().size() == 4, "rettangolo non ruota la vista e non modifica scena o storia");
                require(view->refMarks_.size() == 4, "evidenze di A e strumenti selezionati");
                const auto center = bounds(*view, 2).center();
                gesture(*view, center, center, false);
                require(selected() == QVector<int>({1, 3}), "clic singolo continua a togliere uno strumento");
                gesture(*view, center, center, false);
                require(selected() == QVector<int>({1, 2, 3}), "clic singolo aggiunge lo strumento");
                panel->findChild<QPushButton *>("booleanPickFirst")->click();
                require(view->referencePicking() && !view->bodyBoxPickFinished_, "A mantiene la scelta singola");
                panel->findChild<QPushButton *>("booleanPickTools")->click();
                require(view->bodyBoxPickFinished_ && panel->findChild<QPushButton *>("booleanPickTools")->isChecked(),
                    "passaggio esplicito da A agli strumenti abilita il rettangolo");
                // Esc durante il trascinamento non lascia il box armato.
                QMouseEvent press(QEvent::MouseButtonPress, all.topLeft(), all.topLeft(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                view->mousePressEvent(&press);
                QMouseEvent move(QEvent::MouseMove, all.bottomRight(), all.bottomRight(), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                view->mouseMoveEvent(&move);
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                view->keyPressEvent(&escape);
                require(!view->referencePicking() && !view->boxSelecting_ && !view->bodyBoxPickFinished_
                    && selected() == QVector<int>({1, 2, 3}), "Esc termina il riquadro e conserva le scelte");
                inspected = true;
                if (operation == 0) panel->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                else panel->reject();
            } catch (...) { failure = std::current_exception(); panel->reject(); }
        });
        driver.start();
        const bool applied = booleanDialog(&window, view, QStringLiteral("Test booleana rettangolo"), -1, initial,
            [&](const ExtrusionObject &d) { submitted = d; return view->createBody(d); });
        if (failure) std::rethrow_exception(failure);
        require(inspected && applied == (operation == 0), "pannello verificato con Unione, Intersezione e Differenza");
        require(!view->referencePicking() && !view->bodyBoxPickFinished_ && view->refMarks_.isEmpty()
            && view->pickBodies_.isEmpty(), "chiusura pulisce callback, evidenze e filtro corpi");
        if (operation == 0) {
            require(submitted.firstBody == 0 && submitted.secondBody == 1 && submitted.booleanTools == QVector<int>({2, 3}),
                "conferma conserva tutti gli strumenti selezionati");
            require(view->extrusions().size() == 5 && std::fabs(massProperties(*view->extrusions().last().forgeBody).volume - 32) < 1e-8,
                "unione dei quattro corpi valida");
            view->undo(); require(view->extrusions().size() == 4, "Undo unico per la booleana");
            view->redo(); require(view->extrusions().size() == 5, "Redo booleana con piu strumenti");
            QByteArray bytes; DocumentState decoded;
            clientOk(encodeDocument(view->currentDocument(), bytes)); clientOk(decodeDocument(bytes, decoded));
            require(decoded.extrusions.last().booleanTools == QVector<int>({2, 3}), "strumenti persistenti");
        } else require(view->extrusions().size() == 4, "Annulla conserva i corpi originali");
    }
    std::cout << "PASS booleane: rettangolo cumulativo, clic, filtri, Esc/Annulla, Undo/Redo e codec\n";
}
