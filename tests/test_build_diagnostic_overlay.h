static void buildDiagnosticOverlay() {
    CadViewport view;
    view.resize(640, 480);
    const auto render = [&](const QColor &background) {
        QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(background);
        {
            QPainter painter(&image);
            view.drawBuildDiagnostic(painter);
        }
        return image;
    };
    const auto isRed = [](const QColor &color) {
        return color.red() > 225 && color.green() > 80 && color.green() < 145 && color.blue() < 135;
    };
    const SceneSelection selection{SceneObjectKind::Extrusion, 3};
    view.selection_ = selection;
    const QVector3D point(0, 1, 0);
    const QVector3D segmentFrom(-1, -1, 0);
    const QVector3D segmentTo(1, -1, 0);
    const QPoint marker = view.projectWorldPoint(point).toPoint();
    const QPoint line = view.projectWorldPoint((segmentFrom + segmentTo) / 2).toPoint();
    const QString error = QStringLiteral("Profili che si intersecano");
    const QString context = QStringLiteral("Controlla i due bordi evidenziati.");
    QVector3D outsideLeft, outsideRight, rayDirection;
    view.camera_.viewRay(QPoint(-160, view.height() / 2), view.size(), 10.0, outsideLeft, rayDirection);
    view.camera_.viewRay(QPoint(view.width() + 160, view.height() / 2), view.size(), 10.0, outsideRight, rayDirection);
    require(view.projectWorldPoint(outsideLeft).x() < 0
            && view.projectWorldPoint(outsideRight).x() >= view.width(), "estremi diagnostici fuori dalla vista");

    for (const QColor &background : {QColor(243, 244, 246), QColor(24, 26, 30)}) {
        const QImage empty = render(background);
        view.showBuildDiagnostic(error, {{{point}, {segmentFrom, segmentTo}}, context});
        const QImage localized = render(background);
        require(isRed(localized.pixelColor(marker)), "punto diagnostico nella posizione proiettata");
        require(isRed(localized.pixelColor(line)), "tratto diagnostico nel mezzo del segmento");
        require(view.selection() == selection && view.projectWorldPoint(point).toPoint() == marker,
                "diagnostica conserva la selezione e la proiezione");

        view.showBuildDiagnostic(error, {{}, context});
        const QImage global = render(background);
        require(global.pixelColor(marker) == background && global.pixelColor(line) == background,
                "diagnostica globale non disegna marker residui");
        require(global != empty, "diagnostica globale visibile senza geometria");

        QRect textBox;
        for (int y = 0; y < global.height(); ++y)
            for (int x = 0; x < global.width(); ++x)
                if (global.pixelColor(x, y) == QColor(31, 19, 26))
                    textBox |= QRect(x, y, 1, 1);
        require(textBox.isValid(), "identifica il riquadro del messaggio disegnato");
        const auto hasLeader = [&](const QImage &image, const QPointF &target) {
            const QLineF ray(QRectF(textBox).center(), target);
            for (double fraction : {0.6, 0.85}) {
                const QPoint probe = ray.pointAt(fraction).toPoint();
                bool found = false;
                for (int y = probe.y() - 2; y <= probe.y() + 2; ++y)
                    for (int x = probe.x() - 2; x <= probe.x() + 2; ++x) {
                        const auto c = image.pixelColor(x, y);
                        found |= c.red() > 185 && c.red() - c.green() > 35 && c.green() > 125
                              && image.pixel(x, y) != global.pixel(x, y);
                    }
                if (!found) return false;
            }
            return true;
        };
        require(hasLeader(localized, marker) && hasLeader(localized, line),
                "il riquadro ha un segmento verso il punto e uno verso il bordo");
        view.showBuildDiagnostic(error, {{{point}, {outsideRight}}, context});
        const auto mixed = render(background);
        require(hasLeader(mixed, marker), "il punto visibile resta collegato accanto a un errore fuori campo");

        const auto redAtRightMargin = [&](const QImage &image, bool skipCenterLine = false) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                if (skipCenterLine && std::abs(y - image.height() / 2) <= 3) continue;
                for (int x = image.width() - 41; x < image.width() - 20; ++x)
                    count += isRed(image.pixelColor(x, y)) && !isRed(global.pixelColor(x, y));
            }
            return count;
        };
        view.showBuildDiagnostic(error, {{{outsideRight}}, context});
        const QImage outside = render(background);
        require(redAtRightMargin(mixed) > 10, "la freccia resta visibile anche con un altro errore in campo");
        require(redAtRightMargin(outside) > 10, "geometria fuori vista segnalata al margine destro");
        require(outside.pixelColor(view.width() / 2, view.height() / 2) == background,
                "la freccia resta al margine");
        require(view.projectWorldPoint(point).toPoint() == marker && view.selection() == selection,
                "la freccia conserva vista e selezione");

        view.showBuildDiagnostic(error, {{{outsideLeft, outsideRight}}, context});
        const QImage crossing = render(background);
        require(isRed(crossing.pixelColor(view.width() / 2, view.height() / 2)),
                "segmento visibile con entrambi gli estremi fuori dalla vista");
        require(hasLeader(crossing, QPointF(view.width() / 2, view.height() / 2)),
                "il richiamo termina sul tratto visibile anche con entrambi gli estremi fuori campo");
        require(redAtRightMargin(crossing, true) == 0, "il segmento visibile non riceve la freccia");
        const QVector3D nonFinite(std::numeric_limits<float>::quiet_NaN(), 0, 0);
        view.showBuildDiagnostic(error, {{{nonFinite}, {nonFinite, nonFinite}}, context});
        require(render(background) == global, "proiezioni non finite ignorate senza freccia");
        view.showBuildDiagnostic(error, {});
        require(render(background) != global, "il contesto cambia il testo visibile");
        view.clearBuildDiagnostic();
        require(render(background) == empty, "reset rimuove geometria e testo");
    }
    QObject panel, otherPanel;
    const auto revision = view.buildDiagnosticRevision();
    view.showBuildDiagnostic(error, {{{point}}, context});
    view.previewErrorSketch_ = 0;
    view.showPanelBuildFailure(&panel, error, revision);
    require(view.buildDiagnostic_.paths.size() == 1 && view.previewErrorSketch_ == 0,
            "il pannello conserva la segnalazione appena prodotta per lo stesso errore");
    view.clearPanelBuildFailure(&otherPanel);
    require(view.buildDiagnosticError_ == error, "un altro pannello non cancella la segnalazione");
    view.clearPanelBuildFailure(&panel);
    require(view.buildDiagnosticError_.isEmpty() && view.previewErrorSketch_ == -1,
            "la chiusura del pannello rimuove segnalazione e marker");
    view.showBuildDiagnostic(error, {{{point}}, context});
    view.showPanelBuildFailure(&panel, error, view.buildDiagnosticRevision());
    require(view.buildDiagnosticError_ == error && view.buildDiagnostic_.paths.isEmpty(),
            "una validazione non eredita la posizione di un errore precedente con lo stesso testo");
    view.showBuildDiagnostic(error, {{{point}}, context});
    view.clearPanelBuildFailure(&panel);
    require(view.buildDiagnostic_.paths.size() == 1,
            "la chiusura del pannello conserva la nuova segnalazione indipendente");
    std::cout << "PASS build diagnostic overlay\n";
}
