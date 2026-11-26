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
    {
        PdfWindow window;
        auto &view = *window.viewport_;
        view.resize(800, 600);
        view.camera_.autoFit_ = false;
        view.camera_.zoom_ = 20;
        PrimitiveParameters box;
        box.size[0] = box.size[1] = box.size[2] = 4;
        require(view.createPrimitive(box, QStringLiteral("Sezione ruotata")).isEmpty(), "solido per la rotazione della sezione");
        auto *action = window.findChild<QAction *>("sectionViewAction");
        auto *reset = window.findChild<QPushButton *>("sectionResetOrientation");
        auto *offset = window.findChild<QDoubleSpinBox *>("sectionOffset");
        auto *visible = window.findChild<QCheckBox *>("sectionPlaneVisible");
        auto *inclination = window.findChild<QDoubleSpinBox *>("sectionInclination");
        auto *flip = window.findChild<QCheckBox *>("sectionFlipSide");
        require(action && reset && offset && visible && inclination && flip, "comandi Sezione 3D");
        action->setChecked(true);
        offset->setValue(.75);
        require(view.sectionEnabled(), "sezione attiva");
        const QString zeroTilt = inclination->text();
        require(inclination->value() == 0. && view.section_.inclination == zeroTilt, "inclinazione iniziale a zero");

        const auto drag = [&](QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, modifiers);
            QApplication::sendEvent(&view, &press);
            require(view.sectionDrag_ != CadViewport::SectionHandle::None, "il puntatore afferra il manipolatore");
            const bool rotating = view.sectionRotationDragging();
            const QString before = inclination->text();
            QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, modifiers);
            QApplication::sendEvent(&view, &move);
            if (modifiers.testFlag(Qt::ShiftModifier)) require(view.sectionSnap_.has_value(), "Shift mostra il punto agganciato");
            require((inclination->text() != before) == rotating && view.section_.inclination == inclination->text(),
                    "pannello e viewport aggiornano l'angolo durante la rotazione");
            QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, modifiers);
            QApplication::sendEvent(&view, &release);
            require(view.sectionDrag_ == CadViewport::SectionHandle::None && !view.sectionSnap_, "rilascio del manipolatore");
        };
        const QVector3D initial = view.section_.normal;
        for (const auto handle : {CadViewport::SectionHandle::RotateU, CadViewport::SectionHandle::RotateV}) {
            const bool faceOn = handle == CadViewport::SectionHandle::RotateU;
            view.setViewFrame(QVector3D(0, 1, 0), faceOn ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1));
            const auto arc = view.sectionRotationArc(handle);
            int start = 4;
            for (; start < 30 && view.sectionHandleAt(arc[start].toPoint()) != handle; ++start) {}
            require(start < 30, "anello selezionabile sia di fronte sia di taglio");
            const auto pivot = view.sectionCenter(), axis = view.sectionRotationAxis(handle);
            drag(arc[start], arc[start + 10]);
            const auto normal = view.section_.normal;
            const float cosine = std::clamp(QVector3D::dotProduct(initial, normal), -1.f, 1.f);
            require(cosine < .996f, "l'anello cambia la normale del piano");
            const QString tilt = inclination->text();
            const double degrees = inclination->value();
            require(tilt.endsWith(QChar(u'°')) && std::abs(degrees - std::acos(cosine) * 180.0 / std::acos(-1.0)) < .001,
                    "i gradi riportano l'inclinazione rispetto al piano iniziale");
            flip->setChecked(!flip->isChecked());
            require(inclination->text() == tilt && view.section_.inclination == tilt &&
                        (view.section_.normal + normal).length() < 1e-5,
                    "invertire il lato non cambia l'inclinazione");
            flip->setChecked(!flip->isChecked());
            require(std::abs(QVector3D::dotProduct(axis, normal)) < 1e-5f, "la rotazione conserva l'asse afferrato");
            require(std::abs(QVector3D::dotProduct(pivot - view.section_.point, normal)) < 1e-5f,
                    "il piano ruota attorno al punto afferrato");
            require(std::abs(offset->value() - .75) < 1e-6 &&
                        view.sectionRemoves(pivot + normal) && !view.sectionRemoves(pivot - normal),
                    "l'offset conserva la quota e il taglio segue la normale ruotata");

            if (!faceOn) {
                const auto arrow = view.sectionHandleShape().boundingRect().center();
                const auto alongNormal = view.projectWorldPoint(view.sectionCenter() + normal) -
                                         view.projectWorldPoint(view.sectionCenter());
                require(view.sectionHandleAt(arrow.toPoint()) == CadViewport::SectionHandle::Translate,
                        "la freccia resta selezionabile con la sezione inclinata");
                drag(arrow, arrow + alongNormal);
                require(std::abs(offset->value() - .75) > .1 &&
                            (view.section_.normal - normal).length() < 1e-5,
                        "la freccia trasla anche dopo la rotazione");
                offset->setValue(.75);
            }
            const auto editPivot = view.sectionCenter();
            inclination->setValue(23.75);
            const auto edited = view.section_.normal;
            const double expectedCosine = std::cos(23.75 * std::acos(-1.0) / 180.);
            require(std::abs(QVector3D::dotProduct(edited, initial) - expectedCosine) < 1e-5 &&
                        view.section_.inclination == inclination->text(),
                    "il campo numerico ruota il piano e aggiorna il valore nel viewport");
            require(std::abs(QVector3D::dotProduct(edited, axis)) < 1e-5 &&
                        std::abs(QVector3D::dotProduct(editPivot - view.section_.point, edited)) < 1e-5,
                    "l'input numerico conserva direzione e perno");
            reset->click();
            require((view.section_.normal - initial).length() < 1e-5 && std::abs(offset->value() - .75) < 1e-6,
                    "ripristina orientamento mantenendo la posizione numerica");
            require(inclination->text() == zeroTilt && view.section_.inclination == zeroTilt, "ripristino a zero gradi");
        }

        view.setViewFrame(QVector3D(1, 0, 0), QVector3D(0, 0, 1));
        flip->setChecked(false);
        const QPoint facePixel = view.projectWorldPoint({1.25f, 1.5f, 4.f}).toPoint();
        offset->setValue(2.);
        require(!view.sectionSnapAt(facePixel), "lo snap esclude le facce dietro la campitura");
        offset->setValue(5.);
        const QVector3D vertex(0, 0, 4);
        const auto vertexSnap = view.sectionSnapAt(view.projectWorldPoint(vertex).toPoint());
        require(vertexSnap && (*vertexSnap - vertex).length() < 1e-5f, "aggancio al vertice visibile davanti a quello nascosto");
        const auto edgeSnap = view.sectionSnapAt(view.projectWorldPoint({1, 0, 4}).toPoint());
        require(edgeSnap && std::abs(edgeSnap->y()) < 1e-5f && std::abs(edgeSnap->z() - 4) < 1e-5f,
                "aggancio allo spigolo nella geometria esatta");
        drag(view.sectionHandleShape().boundingRect().center(), facePixel, Qt::ShiftModifier);
        require(std::abs(offset->value() - 4.) < 1e-5 && inclination->value() == 0.,
                "lo snap fa passare il piano per la faccia mantenendo l'inclinazione");

        view.setViewFrame(QVector3D(1, -1, 0), QVector3D(1, 1, 1));
        QImage gizmo(view.size(), QImage::Format_ARGB32_Premultiplied);
        gizmo.fill(Qt::transparent);
        {
            QPainter painter(&gizmo);
            view.drawSectionHandle(painter);
        }
        QImage withoutAngle(view.size(), QImage::Format_ARGB32_Premultiplied);
        withoutAngle.fill(Qt::transparent);
        view.section_.inclination.clear();
        {
            QPainter painter(&withoutAngle);
            view.drawSectionHandle(painter);
        }
        view.section_.inclination = zeroTilt;
        require(gizmo != withoutAngle, "il valore angolare è disegnato accanto al manipolatore");
        bool red = false, green = false;
        for (int y = 0; y < gizmo.height() && !(red && green); ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(gizmo.constScanLine(y));
            for (int x = 0; x < gizmo.width(); ++x) {
                red |= row[x] == qRgb(255, 138, 130);
                green |= row[x] == qRgb(130, 232, 189);
            }
        }
        require(red && green, "entrambi gli anelli sono disegnati nel viewport");
        visible->setChecked(false);
        require(view.sectionEnabled() && !view.sectionHandlesAvailable(), "mostra piano nasconde il manipolatore e conserva il taglio");
        action->setChecked(false);
        require(!view.sectionEnabled(), "spegnere la sezione arresta il taglio");
    }
    if (!openGl) {
        std::cout << "PASS GUI review: palette, tasto pan e manipolatore di sezione; OpenGL non richiesto" << std::endl;
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
