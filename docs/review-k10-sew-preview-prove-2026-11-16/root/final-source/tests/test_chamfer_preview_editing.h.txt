// Included inside ViewportInteractionTest; no changes to user QSettings.
static void chamferPreviewEditingReview() {

    int passed = 0, failed = 0;
    const auto check = [&](bool value, const char *message) {
        std::cout << (value ? "PASS " : "FAIL ") << message << std::endl;
        (value ? passed : failed)++;
    };
    const auto key = [](QWidget *widget, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                        const QString &text = QString()) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
        QApplication::sendEvent(widget, &press);
        QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
        QApplication::sendEvent(widget, &release);
    };
    const auto type = [&](QDoubleSpinBox *spin, const QString &text, int ending) {
        spin->setFocus();
        key(spin, Qt::Key_A, Qt::ControlModifier);
        for (QChar c : text)
            key(spin, c.unicode(), Qt::NoModifier, QString(c));
        key(spin, ending);
    };
    for (int mode : {0, 1, 2})
        for (bool pending : {false, true}) {
            std::cout << "CASE mode=" << mode << " pending=" << pending << std::endl;
            QMainWindow owner;
            CadViewport v;
            DocumentState doc;
            ExtrusionObject b;
            b.feature = BodyFeature::Primitive;
            b.name = "Box";
            b.primitive.size[0] = 10;
            b.primitive.size[1] = 8;
            b.primitive.size[2] = 6;
            doc.extrusions = {b};
            v.loadDocument(doc);
            auto shape = v.extrusions()[0].forgeBody;
            auto edge = shape->edges().front();
            auto range = shape->edge(edge).range;
            auto point = shape->edge(edge).curve->point((range.lo + range.hi) * .5);
            auto ref = ForgeCad::edgeReference(*shape, edge, point);
            ChamferSpec spec;
            spec.mode = mode;
            spec.second = mode == 2 ? 45 : .75;
            QTimer poll, watch;
            poll.setInterval(10);
            watch.setSingleShot(true);
            int phase = 0, edited = 0, applyCalls = 0;
            bool complete = false, committed = false;
            QDoubleSpinBox *spin = nullptr;
            QPlainTextEdit *label = nullptr;
            QString keyBefore;
            quint64 generationBefore = 0;
            std::shared_ptr<std::atomic_bool> token;
            const double wanted = mode == 2 ? 30 : 1.25;
            const auto previewValue = [&] {
                return mode == 0 ? v.preview_.definition.blendSize : v.preview_.definition.chamferSpec.second;
            };
            const auto close = [&] {
                for (auto *d : owner.findChildren<QDialog *>())
                    d->reject();
            };
            QObject::connect(&watch, &QTimer::timeout, &owner, [&] {
                std::cout << "TIMEOUT phase=" << phase << " error=" << v.preview_.error.toStdString()
                          << std::endl;
                check(false, "case bounded by watchdog");
                close();
            });
            QObject::connect(&poll, &QTimer::timeout, &owner, [&] {
                if (phase == 0 && v.preview_.valid) {
                    auto boxes = owner.findChildren<QDoubleSpinBox *>();
                    spin = boxes.value(mode == 0 ? 0 : 1);
                    label = owner.findChild<QPlainTextEdit *>("functionPreviewMessage");
                    if (!spin || !label) {
                        check(false, "actual chamfer controls exist");
                        close();
                        return;
                    }
                    QObject::connect(spin, &QDoubleSpinBox::editingFinished, &owner, [&] { edited++; });
                    auto keyBeforeEnter = v.preview_.key;
                    type(spin, mode == 2 ? "40" : "0.6", Qt::Key_Return);
                    check(previewValue() == (mode == 2 ? 40 : .6) && v.preview_.key != keyBeforeEnter,
                          "Enter refreshes typed field without closing dialog");
                    phase = 10;
                    return;
                }
                if (phase == 10 && v.preview_.valid) {
                    const double before = spin->value();
                    auto keyBeforeArrow = v.preview_.key;
                    key(spin, Qt::Key_Up);
                    check(spin->value() > before && previewValue() == spin->value() &&
                              v.preview_.key != keyBeforeArrow,
                          "Up arrow preserves immediate preview update");
                    key(spin, Qt::Key_Down);
                    check(std::fabs(spin->value() - before) < 1e-12 && previewValue() == spin->value(),
                          "Down arrow restores field and preview definition");
                    phase = 11;
                    return;
                }
                if (phase == 11 && v.preview_.valid) {
                    if (pending) {
                        type(spin, mode == 2 ? "41" : "0.65", Qt::Key_Return);
                        v.startPreviewJob();
                        check(v.previewRunning_ && v.previewCancellation_ && !v.previewCancellation_->load(),
                              "real uncancelled worker before Tab mutation");
                        token = v.previewCancellation_;
                    }
                    keyBefore = v.preview_.key;
                    generationBefore = v.preview_.generation;
                    type(spin, QString::number(wanted), Qt::Key_Tab);
                    check(spin->value() == wanted && edited > 0,
                          "typed value committed by real Tab focus lifecycle");
                    check(v.preview_.key != keyBefore && v.preview_.generation > generationBefore,
                          "Tab refreshes key and generation");
                    check(!v.preview_.valid && !v.preview_.geometry, "old geometry invalidated after Tab");
                    check(!label->toPlainText().contains("pronta"),
                          "label stops declaring old preview ready");
                    if (pending)
                        check(token && token->load(), "Tab mutation cancels previous real worker");
                    phase = 1;
                    return;
                }
                if (phase == 1 && v.preview_.valid) {
                    std::cout << "VISIBLE ui=" << spin->value() << " preview=" << previewValue()
                              << " label=" << label->toPlainText().toStdString() << std::endl;
                    check(previewValue() == wanted, "ready preview matches field after Tab");
                    check(label->toPlainText().contains("pronta"), "new result becomes ready");
                    complete = true;
                    phase = 2;
                    auto *buttons = owner.findChild<QDialogButtonBox *>();
                    buttons->button(QDialogButtonBox::Ok)->click();
                }
            });
            watch.start(15000);
            poll.start();
            auto result = blendDialog(
                &owner, &v, "Chamfer regression", 0, -1, {ref}, .5, true, false, spec,
                [&](int base, const auto &edges, double size, bool chamfer, const auto &parameters) {
                    ++applyCalls;
                    const auto oldGeometry = v.preview_.geometry;
                    const bool matches = previewValue() == wanted;
                    const double shownVolume =
                        oldGeometry ? ForgeCad::Kernel::massProperties(*oldGeometry).volume : 0.;
                    auto error = v.createBlend(base, edges, size, chamfer, "Committed", parameters);
                    check(error.isEmpty() && v.extrusions().size() == 2, "real confirmation creates chamfer");
                    if (error.isEmpty() && v.extrusions().size() == 2) {
                        const auto &actual = v.extrusions().back();
                        check((mode == 0 ? actual.blendSize : actual.chamferSpec.second) == wanted,
                              "confirmation uses updated numeric value");
                        const double actualVolume =
                            ForgeCad::Kernel::massProperties(*actual.forgeBody).volume;
                        std::cout << "COMMIT matches_shown=" << matches << " shown_volume=" << shownVolume
                                  << " actual_volume=" << actualVolume
                                  << " pointer_same=" << (actual.forgeBody == oldGeometry) << std::endl;
                        check(matches && std::fabs(shownVolume - actualVolume) < 1e-8,
                              "confirmed geometry matches updated preview volume");
                        committed = true;
                    }
                    return error;
                },
                false);
            watch.stop();
            poll.stop();
            check(result.applied && complete && committed && applyCalls == 1,
                  "single confirmation applies once and returns");
            check(!v.preview_.valid && v.preview_.key.isEmpty() && !v.previewCallback_ &&
                      !v.interactionLocked(),
                  "dialog cleanup releases preview callback and lock");
        }
    std::cout << "PRE8 RESULT passed=" << passed << " failed=" << failed << std::endl;
    if (failed)
        throw std::runtime_error("Chamfer preview editing regression failed");
}
