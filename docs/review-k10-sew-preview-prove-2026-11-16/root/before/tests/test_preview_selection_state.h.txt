// Included inside ViewportInteractionTest: the existing friend grants access
// to preview generation, cancellation token and reference-pick callback.
static void previewSelectionMutationReview() {
    int passed = 0;
    QStringList failures;
    const auto check = [&](bool condition, const char *message) {
        std::cout << (condition ? "PASS " : "FAIL ") << message << std::endl;
        if (condition)
            ++passed;
        else
            failures.append(QString::fromUtf8(message));
    };
    const auto faceRef = [](CadViewport &v, int i) {
        const auto b = v.extrusions()[i].forgeBody;
        auto face = b->faces().front();
        for (auto f : b->faces())
            if (b->face(f).surface->type() == ForgeCad::Kernel::SurfaceType::Cylinder)
                face = f;
        GeometryRef r;
        r.kind = 5;
        r.index = i;
        r.point = ForgeCad::faceReference(*b, face, b->face(face).surface->point(0, 0));
        return r;
    };
    const auto run = [&](int mode, bool pending) {
        std::cout << "CASE " << mode << " pending=" << pending << std::endl;
        QMainWindow owner;
        CadViewport v;
        DocumentState doc;
        ExtrusionObject carrier;
        carrier.feature = BodyFeature::Primitive;
        carrier.name = "Carrier";
        carrier.primitive.size[0] = 20;
        carrier.primitive.size[1] = 12;
        carrier.primitive.size[2] = 4;
        ExtrusionObject seed;
        seed.feature = BodyFeature::Primitive;
        seed.name = "Seed";
        seed.primitive.origin[0] = -50;
        if (mode == 1) {
            carrier.primitive.kind = PrimitiveKind::Cylinder;
            carrier.primitive.size[0] = 10;
            carrier.primitive.size[1] = 20;
            seed.primitive.origin[0] = -2;
            seed.primitive.origin[1] = -1;
            seed.primitive.size[0] = 4;
            seed.primitive.size[1] = 2;
        }
        doc.extrusions = {carrier, seed};
        SketchObject sketch;
        sketch.name = "Contour";
        sketch.segments = {{{0, 0}, {20, 0}}, {{20, 0}, {20, 12}}, {{20, 12}, {0, 12}}, {{0, 12}, {0, 0}}};
        doc.sketches = {sketch};
        v.loadDocument(doc);
        auto ref = faceRef(v, 0);
        GeometryRef plane;
        plane.kind = 1;
        plane.index = 0;
        GeometryRef contour;
        contour.kind = 7;
        contour.index = 0;
        ExtrusionObject d;
        d.operation = -1;
        d.firstBody = mode == 0 ? 0 : 1;
        if (mode == 2) {
            d.feature = BodyFeature::Pattern;
            d.pattern.kind = 3;
            d.pattern.spacing = 4;
            d.pattern.spacing2 = 4;
            d.pattern.angle = 0;
            d.pattern.refs = {contour};
        } else {
            d.feature = BodyFeature::SurfaceTools;
            d.surfaceTool.mode = mode == 0 ? 3 : 4;
            d.surfaceTool.targets = {ref, plane};
            if (mode == 0)
                d.surfaceTool.sources = {ref};
            else {
                GeometryRef source;
                source.kind = 11;
                source.index = 1;
                d.surfaceTool.sources = {source};
                d.surfaceTool.mappingU = ForgeCad::Kernel::kHalfPi;
                d.surfaceTool.mappingV = 10;
            }
        }
        const char *buttonName = mode == 0   ? "mappingFacePick"
                                 : mode == 1 ? "mappingFacePick"
                                             : "fillPatternBoundaryPick";
        const char *listName = mode == 0   ? "mappingSourceFaces"
                               : mode == 1 ? "mappingTargetFaces"
                                           : "fillPatternBoundaries";
        QTimer poll, watch;
        poll.setInterval(10);
        watch.setSingleShot(true);
        int phase = 0;
        bool complete = false;
        QPushButton *pick = nullptr;
        QPlainTextEdit *label = nullptr;
        QListWidget *list = nullptr;
        QString oldKey;
        quint64 oldGeneration = 0;
        std::shared_ptr<std::atomic_bool> cancellation;
        const auto close = [&] {
            for (auto *x : owner.findChildren<QDialog *>())
                x->reject();
        };
        QObject::connect(&watch, &QTimer::timeout, &owner, [&] {
            std::cout << "TIMEOUT phase=" << phase << " error=" << v.preview_.error.toStdString()
                      << std::endl;
            check(false, "case completed before watchdog");
            close();
        });
        QObject::connect(&poll, &QTimer::timeout, &owner, [&] {
            if (phase == 0) {
                pick = owner.findChild<QPushButton *>(buttonName);
                label = owner.findChild<QPlainTextEdit *>("functionPreviewMessage");
                list = owner.findChild<QListWidget *>(listName);
                if (!pick || !label || !list)
                    return;
                if (mode == 1) {
                    auto *method = owner.findChild<QComboBox *>("mappingMethod");
                    method->setCurrentIndex(0);
                }
                phase = 1;
            }
            if (phase == 1) {
                if (!v.preview_.valid)
                    return;
                if (pending) {
                    auto *number = mode < 2 ? owner.findChild<QDoubleSpinBox *>("mappingX")
                                            : owner.findChildren<QDoubleSpinBox *>().value(0);
                    if (!number) {
                        check(false, "pending fixture numeric control exists");
                        close();
                        return;
                    }
                    number->setValue(number->value() + .125);
                    v.startPreviewJob();
                    if (!v.previewRunning_ || !v.previewCancellation_ || v.previewCancellation_->load()) {
                        check(false, "pending fixture starts uncancelled worker");
                        close();
                        return;
                    }
                }
                check(pending ? v.previewRunning_ : bool(v.preview_.geometry),
                      pending ? "real worker running before mutation"
                              : "real ready geometry before mutation");
                oldKey = v.preview_.key;
                oldGeneration = v.preview_.generation;
                cancellation = v.previewCancellation_;
                pick->click();
                check(v.preview_.key == oldKey && v.preview_.generation == oldGeneration,
                      "opening picker preserves unchanged preview policy");
                auto callback = v.refPickFinished_;
                v.cancelReferencePick();
                callback(true, ref);
                check(v.referencePicking(), "continuous picking remains active after mutation");
                check(v.preview_.key.isEmpty() && !v.preview_.valid && !v.preview_.geometry &&
                          !v.previewPending(),
                      "mutated selection invalidates key and geometry");
                check(v.preview_.generation > oldGeneration, "mutation increments generation");
                check(!label->toPlainText().contains("pronta") && label->toPlainText().contains("Termina"),
                      "mutated selection reports waiting");
                if (pending)
                    check(cancellation && cancellation->load(), "mutation cancels real worker token");
                if (mode < 2)
                    check(list->count() == 0, "mapping removes its only selected face");
                else
                    check(list->count() == 1, "fill replaces sketch contour with picked face");
                if (mode < 2) {
                    auto *result = owner.findChild<QPlainTextEdit *>("mappingResult");
                    check(result && result->toPlainText().isEmpty(), "mapping clears previous notice");
                }
                phase = 2;
                QTimer::singleShot(180, &owner, [&] {
                    check(v.preview_.key.isEmpty() && !v.preview_.valid && !v.preview_.geometry,
                          "cancelled completion cannot publish obsolete preview");
                    if (mode < 2) {
                        auto callback = v.refPickFinished_;
                        v.cancelReferencePick();
                        callback(true, ref);
                        pick->click();
                    } else {
                        auto callback = v.refPickFinished_;
                        v.cancelReferencePick();
                        callback(true, contour);
                    }
                    check(!v.referencePicking() && !v.preview_.key.isEmpty(),
                          "finishing selection restarts request");
                    phase = 3;
                });
                return;
            }
            if (phase == 3 && v.preview_.valid) {
                check(bool(v.preview_.geometry) && label->toPlainText().contains("pronta"),
                      "preview recovers after selection");
                complete = true;
                phase = 4;
                close();
            }
        });
        watch.start(20000);
        poll.start();
        if (mode == 2)
            fillPatternDialog(&owner, &v, "Fill audit", -1, d, [](const auto &) { return QString(); });
        else
            surfaceMappingDialog(&owner, &v, "Mapping audit", -1, d, [](const auto &) { return QString(); });
        poll.stop();
        watch.stop();
        check(complete, "case reaches recovered preview");
        check(!v.referencePicking() && !v.interactionLocked() && v.preview_.key.isEmpty() &&
                  !v.preview_.geometry && !v.refPickFinished_,
              "cancel cleans picker lock callback preview");
    };
    for (int mode : {0, 1, 2})
        for (bool pending : {false, true})
            run(mode, pending);
    std::cout << "PRE7 RESULT passed=" << passed << " failed=" << failures.size() << std::endl;
    // Throw only after every dialog has left its event loop and cleaned up.
    if (!failures.isEmpty())
        throw std::runtime_error(failures.join(QStringLiteral("; ")).toStdString());
}
