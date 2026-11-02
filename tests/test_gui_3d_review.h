// Incluso nel friend ViewportInteractionTest: regressioni GUI della review.
static void gui3dReviewRegressions(bool openGl) {
    // Il contratto della selezione e' la leggibilita', indipendentemente
    // dall'accento grafico. Esercita entrambe le palette dell'applicazione.
    for (bool dark : {false, true}) {
        QWidget owner;
        QPalette palette = owner.palette();
        palette.setColor(QPalette::Window, QColor(dark ? "#182330" : "#f4f8fc"));
        owner.setPalette(palette);
        FunctionDialogPanel panel(&owner, false);
        panel.setPanelOpacity(100);
        panel.ensurePolished();
        QComboBox field(&panel);
        field.addItem(QStringLiteral("Voce"));
        field.ensurePolished();
        const auto colors = panel.palette();
        for (auto group : {QPalette::Active, QPalette::Inactive}) {
            require(std::abs(colors.color(group, QPalette::Highlight).lightness()
                             - colors.color(group, QPalette::HighlightedText).lightness()) > 100,
                    "contrasto della selezione nei temi chiaro e scuro");
            require(std::abs(field.palette().color(group, QPalette::Base).lightness()
                             - field.palette().color(group, QPalette::Text).lightness()) > 100,
                    "contrasto dei campi nei temi chiaro e scuro");
        }
    }
    {
        CadViewport view;
        view.resize(800, 600);
        view.camera_.zoom_ = 20;
        QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&view, &override);
        require(override.isAccepted(), "il tasto pan prevale sulle scorciatoie della finestra");
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&view, &down);
        const QPointF from(300, 250), to(360, 280);
        QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
        QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &move);
        require(view.panning_ && std::abs(view.camera_.panX_ - 2) < 1e-6
                    && std::abs(view.camera_.panY_ + 1) < 1e-6,
                "tasto pan e trascinamento aggiornano la camera nelle unita' della vista");
        QKeyEvent repeatedUp(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QString(), true);
        QKeyEvent repeatedDown(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QString(), true);
        QApplication::sendEvent(&view, &repeatedUp);
        QApplication::sendEvent(&view, &repeatedDown);
        require(view.panKeyHeld_ && view.panning_ && view.cursor().shape() == Qt::ClosedHandCursor,
                "ripetizione del tasto conserva il trascinamento e la mano chiusa");
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&view, &up);
        require(!view.panKeyHeld_ && view.panning_, "rilascio del tasto conserva il pan fino al rilascio del mouse");
        QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &release);
        require(!view.panning_ && !view.panKeyHeld_ && view.cursor().shape() != Qt::ClosedHandCursor,
                "rilascio del mouse termina il pan");
        QApplication::sendEvent(&view, &down);
        QApplication::sendEvent(&view, &press);
        QFocusEvent lost(QEvent::FocusOut);
        QApplication::sendEvent(&view, &lost);
        require(!view.panKeyHeld_ && !view.panning_, "perdita del focus annulla i tasti trattenuti");
    }
    if (!openGl) {
        std::cout << "PASS GUI review: palette e tasto pan; OpenGL non richiesto" << std::endl;
        return;
    }
    require(QGuiApplication::platformName() != QStringLiteral("offscreen"), "la prova GL richiede una sessione grafica");
    QWidget firstWindow, secondWindow;
    firstWindow.resize(800, 600); secondWindow.resize(800, 600);
    auto *view = new CadViewport(&firstWindow);
    view->resize(800, 600);
    view->setReferencePlanesVisible(false);
    view->setAxesVisible(false);
    view->setGridVisible(false);
    PrimitiveParameters box; box.size[0] = box.size[1] = box.size[2] = 4;
    require(view->createPrimitive(box, QStringLiteral("Contesto GL")).isEmpty(), "corpo per la ricreazione GL");
    view->setViewPreset(4);
    view->fitAll();
    view->setGlassPanel(0x1234, QRect(20, 20, 200, 160), 14, 16, true);
    firstWindow.show(); view->show();
    const auto capture = [&] {
        view->update();
        for (int i = 0; i < 4; ++i) QApplication::processEvents();
        require(view->isValid(), "contesto OpenGL valido");
        const auto image = view->grabFramebuffer();
        require(!image.isNull() && view->gpuGlassAvailable()
                    && view->glassCompositor_.glassSource_ && view->glassCompositor_.glassSource_->isValid()
                    && view->glassCompositor_.glassPing_ && view->glassCompositor_.glassPing_->isValid()
                    && view->glassCompositor_.glassBlur_ && view->glassCompositor_.glassBlur_->isValid(),
                "shader e framebuffer del vetro creati nel contesto corrente");
        require(view->gpuPickBody(QPoint(view->width() / 2, view->height() / 2)) == 0,
                "picking GPU del corpo dopo il rendering");
        view->makeCurrent();
        require(view->glGetError() == GL_NO_ERROR, "rendering e picking senza errori GL");
        view->doneCurrent();
        return image;
    };
    const auto initial = capture();
    const auto qaDirectory = qEnvironmentVariable("FORGECAD_GUI_REVIEW_QA_DIR");
    if (!qaDirectory.isEmpty())
        require(QDir().mkpath(qaDirectory) && initial.save(QDir(qaDirectory).filePath(QStringLiteral("initial.png"))),
                "cattura iniziale della review GUI");
    view->makeCurrent();
    std::cout << "GL " << reinterpret_cast<const char *>(view->glGetString(GL_RENDERER))
              << " | " << reinterpret_cast<const char *>(view->glGetString(GL_VERSION)) << std::endl;
    view->doneCurrent();
    const QPoint center(view->width() / 2, view->height() / 2);
    const quint64 frameBeforeChanges = view->renderedFrameSerial();
    view->setModelBodyVisible(view->modelBodyIndexForFeature(0), false);
    require(view->gpuPickBody(center) == -1 && view->pickSceneObject(center, false).kind == SceneObjectKind::None,
            "corpo nascosto non selezionabile anche prima del repaint");
    view->setModelBodyVisible(view->modelBodyIndexForFeature(0), true);
    require(view->gpuPickBody(center) == 0, "corpo nuovamente visibile selezionabile prima del repaint");
    view->pickBodies_ = {1};
    require(view->gpuPickBody(center) == -1, "filtro di selezione aggiornato prima del repaint");
    view->pickBodies_.clear();
    require(view->gpuPickBody(center) == 0, "rimozione del filtro di selezione prima del repaint");
    const QPointF panEnd = QPointF(center) + QPointF(2 * view->width(), 0);
    QMouseEvent panPress(QEvent::MouseButtonPress, center, center, Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent panMove(QEvent::MouseMove, panEnd, panEnd, Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QMouseEvent panRelease(QEvent::MouseButtonRelease, panEnd, panEnd, Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    const double originalPan = view->camera_.panX_;
    QApplication::sendEvent(view, &panPress);
    QApplication::sendEvent(view, &panMove);
    QApplication::sendEvent(view, &panRelease);
    require(view->gpuPickBody(center) == -1 && view->pickSceneObject(center, false).kind == SceneObjectKind::None,
            "camera spostata non seleziona la vecchia proiezione prima del repaint");
    view->camera_.panX_ = originalPan;
    const auto originalDisplay = view->extrusions_[0].display;
    auto &display = view->extrusions_[0].display;
    display.instancedBase = std::make_shared<BodyDisplay>(originalDisplay);
    display.instanceTransforms = {QMatrix4x4()};
    require(view->gpuPickBody(center) == 0, "picking di un'istanza senza repaint");
    display.instanceTransforms[0].translate(100, 0, 0);
    require(view->gpuPickBody(center) == -1, "trasformazione dell'istanza aggiornata prima del repaint");
    display = originalDisplay;
    require(view->renderedFrameSerial() == frameBeforeChanges, "regressioni picking esercitate senza un nuovo frame");
    capture();
    for (int move = 0; move < 2; ++move) {
        QPointer<QOpenGLContext> oldContext(view->context());
        bool destroyed = false, resourcesCleared = false;
        const auto connection = QObject::connect(oldContext, &QOpenGLContext::aboutToBeDestroyed, &firstWindow, [&] {
            destroyed = true;
            resourcesCleared = !view->gpuGlassAvailable() && !view->glassCompositor_.glassShader_
                && !view->glassCompositor_.glassSource_ && !view->glassCompositor_.glassPing_
                && !view->glassCompositor_.glassBlur_;
        }, Qt::DirectConnection);
        QWidget *destination = move == 0 ? &secondWindow : &firstWindow;
        view->setParent(destination);
        view->resize(800, 600); destination->show(); view->show();
        const auto recreated = capture();
        if (!qaDirectory.isEmpty())
            require(recreated.save(QDir(qaDirectory).filePath(QStringLiteral("recreated-%1.png").arg(move + 1))),
                    "cattura dopo la ricreazione del contesto GL");
        require(destroyed && oldContext.isNull() && resourcesCleared,
                "distruzione del vecchio contesto libera tutte le risorse del vetro");
        require(recreated.size() == initial.size(), "dimensione del framebuffer conservata dopo il cambio finestra");
        require(view->glassCompositor_.glassPanels_.contains(0x1234), "pannelli conservati dopo la ricreazione GL");
        QObject::disconnect(connection);
    }
    delete view;
    std::cout << "PASS GUI review: palette, pan, picking prima del repaint e due ricreazioni reali del contesto GL" << std::endl;
}
