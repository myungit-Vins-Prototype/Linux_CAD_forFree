// Incluso dentro ViewportInteractionTest. Nessun include in questo frammento.
static void documentThemeTabsTests() {
    using namespace ForgeCad;
    int checks = 0, failures = 0;
    const QString output = qEnvironmentVariable("FORGECAD_THEME_TABS_OUTPUT");
    if (!output.isEmpty())
        QDir().mkpath(output);
    const bool only2d = QApplication::arguments().contains("--theme-tabs-only-2d");
    initializeApplicationTheme(*qApp);
    const auto check = [&](bool value, const QString &name) {
        ++checks;
        if (!value) {
            ++failures;
            std::cout << "FAIL " << name.toStdString() << std::endl;
        }
    };
    const auto ok = [](const QString &s) {
        if (!s.isEmpty())
            throw std::runtime_error(s.toStdString());
    };
    const auto settle = []() {
        for (int i = 0; i < 8; ++i)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    const auto dominant = [](QWidget *w, QRect crop = QRect()) {
        const auto image =
            (crop.isNull() ? w->grab() : w->grab(crop)).toImage().convertToFormat(QImage::Format_RGB32);
        QHash<QRgb, int> n;
        QRgb best = 0;
        int maximum = 0;
        for (int y = 2; y < image.height() - 2; ++y)
            for (int x = 2; x < image.width() - 2; ++x) {
                const auto p = image.pixel(x, y);
                const int count = ++n[p];
                if (count > maximum) {
                    maximum = count;
                    best = p;
                }
            }
        return QColor(best);
    };
    const auto isDark = [](QColor c) { return c.lightness() < 128; };
    struct Page {
        QMainWindow *window;
        PdfWindow *model = nullptr;
        DrawingWindow *drawing = nullptr;
        FunctionDialogPanel *panel = nullptr;
        QDoubleSpinBox *field = nullptr;
        QCheckBox *box = nullptr;
        QString name;
    };

    DocumentTabsWindow host;
    host.resize(1320, 900);
    host.show();
    QTemporaryDir files;
    if (!files.isValid())
        throw std::runtime_error("temp failed");
    QVector<Page> pages;
    const auto addPanel = [&](Page &p) {
        p.panel = new FunctionDialogPanel(p.window, false);
        p.panel->setSizeIdentity("theme-tabs-probe-" + p.name);
        p.panel->setPanelOpacity(100);
        auto *form = p.panel->createScrollableForm();
        p.field = new QDoubleSpinBox(p.panel);
        p.field->setValue(12.5);
        form->addRow("Valore", p.field);
        p.box = new QCheckBox("Controllo", p.panel);
        p.box->setFocusPolicy(Qt::NoFocus);
        form->addRow(p.box);
        p.panel->show();
    };
    QString source;
    if (!only2d) {
        for (int i = 0; i < 2; ++i) {
            auto *m = host.newModel();
            Page p{m, m, nullptr, nullptr, nullptr, nullptr, QString("model%1").arg(i)};
            ok(m->viewport_->createPrimitive(PrimitiveParameters{}, "Test body"));
            source = files.filePath(QString("model%1.prt").arg(i));
            check(m->saveDocumentPath(source), "save isolated model");
            addPanel(p);
            pages.append(p);
        }
    } else {
        DocumentState empty;
        ExtrusionObject box;
        box.feature = BodyFeature::Primitive;
        empty.extrusions = {box};
        CadViewport builder;
        builder.loadDocument(empty);
        source = files.filePath("empty.prt");
        ok(saveDocumentFile(source, builder.currentDocument()));
    }
    for (int i = 0; i < 2; ++i) {
        auto *d = new DrawingWindow(host.documentTabs());
        ok(d->createFromModel(source));
        host.addDocument(d, true);
        Page p{d, nullptr, d, nullptr, nullptr, nullptr, QString("drawing%1").arg(i)};
        addPanel(p);
        pages.append(p);
    }
    // Preserve explicit user choices independently from the normal theme surfaces.
    const QColor custom("#42662b");
    PdfWindow *customOwner = nullptr;
    BackgroundSettings customBackground;
    for (auto &p : pages)
        if (p.model) {
            customOwner = p.model;
            customOwner->applyTreeBackground(custom);
            customBackground = customOwner->viewport_->background();
            customBackground.startColor = QColor("#214365");
            customBackground.endColor = QColor("#654321");
            customOwner->viewport_->setBackground(customBackground);
            break;
        }
    QMap<QString, QImage> icons;
    const auto verify = [&](bool dark, const QString &phase) {
        check(isDark(QApplication::palette().color(QPalette::Window)) == dark,
              phase + " application palette");
        for (auto &p : pages) {
            // Palette check before reactivation exercises hidden pages as well.
            check(isDark(p.window->palette().color(QPalette::Window)) == dark,
                  phase + " " + p.name + " hidden/window palette");
            if (host.documentTabs()->indexOf(p.window) >= 0)
                host.activateDocument(p.window);
            else
                p.window->show();
            p.panel->show();
            settle();
            const QString prefix = phase + " " + p.name;
            auto bars = p.window->findChildren<QToolBar *>();
            for (auto *bar : bars)
                bar->show();
            settle();
            int barChecks = 0;
            for (auto *bar : bars)
                if (bar->isVisible() && bar->width() > 40 && bar->height() > 10) {
                    const auto color = dominant(bar);
                    check(isDark(color) == dark,
                          prefix + " toolbar raster " + bar->objectName() + "=" + color.name());
                    check(isDark(bar->palette().color(QPalette::Button)) == dark,
                          prefix + " toolbar palette " + bar->objectName());
                    ++barChecks;
                }
            check(barChecks > 0, prefix + " toolbar present");
            check(isDark(p.field->palette().color(QPalette::Base)) == dark, prefix + " field palette");
            const auto fieldColor = dominant(p.field);
            check(isDark(fieldColor) == dark, prefix + " field raster=" + fieldColor.name());
            check(isDark(p.panel->panelColor()) == dark, prefix + " panel color");
            const auto panelColor = dominant(p.panel);
            check(isDark(panelColor) == dark, prefix + " panel raster=" + panelColor.name());
            p.box->setChecked(false);
            const auto off = p.box->grab().toImage();
            p.box->setChecked(true);
            const auto on = p.box->grab().toImage();
            int contrast = 0;
            for (int y = 0; y < off.height(); ++y)
                for (int x = 0; x < off.width(); ++x) {
                    const auto a = off.pixelColor(x, y), b = on.pixelColor(x, y);
                    if (qMax(qMax(qAbs(a.red() - b.red()), qAbs(a.green() - b.green())),
                             qAbs(a.blue() - b.blue())) > 100)
                        ++contrast;
                }
            check(contrast >= 6, prefix + " checkbox raster contrast");
            if (p.model) {
                auto *tree = p.model->modelTree_;
                const auto color = dominant(tree->viewport());
                if (p.model == customOwner) {
                    check(tree->palette().color(QPalette::Base) == custom,
                          prefix + " user tree palette retained");
                    check(color == custom, prefix + " user tree raster retained");
                    check(p.model->viewport_->background().startColor == customBackground.startColor &&
                              p.model->viewport_->background().endColor == customBackground.endColor,
                          prefix + " canvas background retained");
                } else {
                    check(isDark(tree->palette().color(QPalette::Base)) == dark,
                          prefix + " storyboard palette");
                    check(isDark(color) == dark, prefix + " storyboard raster=" + color.name());
                }
            }
            if (p.drawing) {
                QByteArray bytes;
                ok(encodeDrawingSheet(p.drawing->editor()->sheet(), bytes));
                const auto previous = p.window->property("themeProbeDefinition").toByteArray();
                if (previous.isEmpty())
                    p.window->setProperty("themeProbeDefinition", bytes);
                else
                    check(previous == bytes, prefix + " drawing definition retained");
                p.drawing->editor()->fitSheet();
                settle();
                auto *canvas = p.drawing->editor()->canvas();
                const auto point = canvas->mapFromScene(QPointF(20, 20));
                const auto capture = canvas->viewport()->grab().toImage();
                const auto sample = (QPointF(point) * capture.devicePixelRatio()).toPoint();
                check(capture.rect().contains(sample) && capture.pixelColor(sample) == QColor(Qt::white),
                      prefix + " paper raster stays white");
                // Technical ink belongs to the drawing, independent from the interface theme.
                const auto size = p.drawing->editor()->sheet().size;
                QImage sceneImage(1000, 700, QImage::Format_RGB32);
                sceneImage.fill(Qt::white);
                QPainter scenePainter(&sceneImage);
                canvas->scene()->render(&scenePainter, QRectF(0, 0, 1000, 700), QRectF(QPointF(0, 0), size),
                                        Qt::IgnoreAspectRatio);
                scenePainter.end();
                int sceneInk = 0;
                for (int y = 0; y < sceneImage.height(); ++y)
                    for (int x = 0; x < sceneImage.width(); ++x)
                        sceneInk += sceneImage.pixelColor(x, y).lightness() < 128;
                check(sceneInk > 100, prefix + " technical border/title text raster present");
                const auto imageBytes = QByteArray(reinterpret_cast<const char *>(sceneImage.constBits()),
                                                   sceneImage.sizeInBytes());
                const auto previousImage = p.window->property("themeProbePaperRaster").toByteArray();
                if (previousImage.isEmpty())
                    p.window->setProperty("themeProbePaperRaster", imageBytes);
                else
                    check(previousImage == imageBytes,
                          prefix + " technical sheet raster identical across themes");
                // A frame can be queued after Qt's palette propagation. Wait for that frame before sampling
                // the visible canvas.
                const auto canvasInk = [&] {
                    const auto paperRect = canvas->mapFromScene(QRectF(QPointF(0, 0), size))
                                               .boundingRect()
                                               .intersected(canvas->viewport()->rect())
                                               .adjusted(2, 2, -2, -2);
                    const auto actual = canvas->viewport()->grab(paperRect).toImage();
                    int ink = 0;
                    for (int y = 0; y < actual.height(); ++y)
                        for (int x = 0; x < actual.width(); ++x)
                            ink += actual.pixelColor(x, y).lightness() < 128;
                    return ink;
                };
                QElapsedTimer frame;
                frame.start();
                int actualInk = canvasInk();
                while (actualInk < 100 && frame.elapsed() < 1000) {
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                    actualInk = canvasInk();
                }
                check(actualInk >= 100, prefix + " visible technical border/title text raster present");
            }
            auto *menu = p.window->findChild<QMenu *>("applicationThemeMenu");
            check(menu && menu->actions().size() == 3, prefix + " theme menu present");
            if (menu) {
                menu->popup({50, 50});
                settle();
                const auto image = menu->grab().toImage();
                const auto point =
                    QPointF(menu->width() / 2.0, menu->actionGeometry(menu->actions().back()).bottom() + 3) *
                    image.devicePixelRatio();
                const auto color = image.pixelColor(point.toPoint());
                check(color.alpha() > 200, prefix + " menu surface opaque interior");
                check(isDark(color) == dark, prefix + " menu raster=" + color.name());
                check(isDark(menu->palette().color(QPalette::WindowText)) != dark, prefix + " menu palette");
                int selected = 0;
                for (auto *a : menu->actions())
                    selected += a->isChecked();
                check(selected == 1, prefix + " menu single selection");
                menu->hide();
            }
            // Render the actual action icon, whose engine predates the change.
            QAction *iconAction =
                p.window->findChild<QAction *>(p.model ? "documentSaveAction" : "drawingSaveAction");
            check(iconAction && !iconAction->icon().isNull(), prefix + " actual icon present");
            if (iconAction) {
                QImage image(32, 32, QImage::Format_ARGB32);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                iconAction->icon().paint(&painter, QRect(0, 0, 32, 32));
                painter.end();
                int visible = 0;
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 32; ++x)
                        if (image.pixelColor(x, y).alpha() > 128)
                            ++visible;
                check(visible > 30, prefix + " icon raster visible");
                const QString key = p.name + QString(dark ? "-dark" : "-light");
                if (icons.contains(key))
                    check(image == icons[key], prefix + " icon raster stable after reactivation");
                else
                    icons[key] = image;
                const QString other = p.name + QString(dark ? "-light" : "-dark");
                if (icons.contains(other))
                    check(image != icons[other], prefix + " icon raster follows theme");
                if (!output.isEmpty())
                    image.save(output + "/" + phase + "-" + p.name + "-icon.png");
            }
            if (!output.isEmpty())
                p.window->grab().save(output + "/" + phase + "-" + p.name + ".png");
            if (!output.isEmpty())
                p.panel->grab().save(output + "/" + phase + "-" + p.name + "-panel.png");
        }
    };
    // Drive the actual menus, alternating a 3D and 2D origin where possible.
    const auto choose = [&](int index) {
        auto *menu =
            pages[index == 2 ? 0 : (only2d ? 0 : 2)].window->findChild<QMenu *>("applicationThemeMenu");
        check(menu, "origin menu exists");
        if (menu)
            menu->actions()[index]->trigger();
        else
            setApplicationThemeMode(*qApp, index == 2   ? ApplicationThemeMode::Dark
                                           : index == 1 ? ApplicationThemeMode::Light
                                                        : ApplicationThemeMode::Desktop);
        settle();
    };
    choose(1);
    verify(false, "light0");
    choose(2);
    verify(true, "dark1");
    choose(1);
    verify(false, "light2");
    choose(2);
    verify(true, "dark3");
    if (!only2d) {
        auto *m = new PdfWindow;
        Page p{m, m, nullptr, nullptr, nullptr, nullptr, "detached-model"};
        m->show();
        addPanel(p);
        pages.append(p);
    }
    auto *d = new DrawingWindow;
    ok(d->createFromModel(source));
    d->resize(host.size());
    d->show();
    Page dp{d, nullptr, d, nullptr, nullptr, nullptr, "detached-drawing"};
    addPanel(dp);
    pages.append(dp);
    // New documents must inherit the current global style without a palette event.
    if (!only2d) {
        auto *m = host.newModel();
        Page p{m, m, nullptr, nullptr, nullptr, nullptr, "new-model-dark"};
        addPanel(p);
        pages.append(p);
    }
    {
        auto *d = new DrawingWindow(host.documentTabs());
        ok(d->createFromModel(source));
        host.addDocument(d, true);
        Page p{d, nullptr, d, nullptr, nullptr, nullptr, "new-drawing-dark"};
        addPanel(p);
        pages.append(p);
    }
    verify(true, "dark-new");
    choose(0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    for (bool dark : {false, true, false}) {
        check(QMetaObject::invokeMethod(
                  qApp->styleHints(), "colorSchemeChanged", Qt::DirectConnection,
                  Q_ARG(Qt::ColorScheme, dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light)),
              "desktop scheme signal");
        settle();
        verify(dark, dark ? "desktop-dark" : "desktop-light");
    }
#endif
    // Changing sheet rebuilds view menus; the same theme entry must survive exactly once.
    for (auto &p : pages)
        if (p.drawing) {
            ok(p.drawing->addSheet("Secondo foglio", {420, 297}));
            ok(p.drawing->setActiveSheet(0));
            p.window->setProperty("themeProbeDefinition", QByteArray());
            p.window->setProperty("themeProbePaperRaster", QByteArray());
            check(p.window->findChildren<QMenu *>("applicationThemeMenu").size() == 1,
                  "exactly one 2D theme menu after sheet rebuild");
        }
    choose(2);
    verify(true, "sheets-dark");
    // Close the final 3D page: the remaining 2D session still owns theme switching.
    for (int i = pages.size() - 1; i >= 0; --i)
        if (pages[i].model) {
            auto p = pages.takeAt(i);
            delete p.panel;
            const int index = host.documentTabs()->indexOf(p.window);
            if (index >= 0)
                host.documentTabs()->removeTab(index);
            delete p.window;
        }
    choose(1);
    verify(false, "only2d-light");
    choose(2);
    verify(true, "only2d-dark");
    for (auto &p : pages) {
        p.panel->hide();
        delete p.panel;
        p.panel = nullptr;
    }
    // Clean destruction without saving or changing any original CAD.
    for (auto &p : pages) {
        const int index = host.documentTabs()->indexOf(p.window);
        if (index >= 0)
            host.documentTabs()->removeTab(index);
        delete p.window;
        p.window = nullptr;
    }
    require(failures == 0, "tema coerente in tutte le schede 3D/2D e nei pixel raster");
    std::cout << "PASS document theme tabs: " << checks << " checks\n";
}

// Included within ViewportInteractionTest; drives production dialogs.
static void uiResidualThemeTests() {
    // Keep both standard Qt values and every advertised byte format. Some
    // platform MIME providers expose image/color values only through QVariant.
    class ClipboardMimeData final : public QMimeData {
        QStringList originalFormats;
        QMap<QString, QByteArray> originalBytes;

      public:
        explicit ClipboardMimeData(const QMimeData &source) : originalFormats(source.formats()) {
            for (const auto &format : originalFormats) {
                const auto bytes = source.data(format);
                originalBytes.insert(format, bytes);
                setData(format, bytes);
            }
            if (source.hasText())
                setText(source.text());
            if (source.hasHtml())
                setHtml(source.html());
            if (source.hasImage())
                setImageData(source.imageData());
            if (source.hasUrls())
                setUrls(source.urls());
            if (source.hasColor())
                setColorData(source.colorData());
        }
        QStringList formats() const override {
            auto result = originalFormats;
            for (const auto &format : QMimeData::formats())
                if (!result.contains(format))
                    result.append(format);
            return result;
        }
        bool hasFormat(const QString &format) const override { return formats().contains(format); }

      protected:
        QVariant retrieveData(const QString &format, QMetaType preferredType) const override {
            if (preferredType == QMetaType::fromType<QByteArray>() && originalBytes.contains(format))
                return originalBytes.value(format);
            return QMimeData::retrieveData(format, preferredType);
        }
    };
    struct ClipboardGuard {
        QClipboard *clipboard = QApplication::clipboard();
        std::unique_ptr<QMimeData> savedClipboard;
        std::unique_ptr<QMimeData> savedSelection;
        bool selectionSupported = clipboard->supportsSelection();
        static std::unique_ptr<QMimeData> clone(const QMimeData *source) {
            return source ? std::make_unique<ClipboardMimeData>(*source) : nullptr;
        }
        ClipboardGuard() : savedClipboard(clone(clipboard->mimeData())) {
            if (selectionSupported)
                savedSelection = clone(clipboard->mimeData(QClipboard::Selection));
        }
        ~ClipboardGuard() {
            clipboard->setMimeData(savedClipboard.release(), QClipboard::Clipboard);
            if (selectionSupported)
                clipboard->setMimeData(savedSelection.release(), QClipboard::Selection);
            for (int i = 0; i < 8; ++i)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    };
    using namespace ForgeCad;
    int checks = 0, failures = 0;
    QJsonArray samples;
    QTemporaryDir temporaryOutput;
    const QString requestedOutput = qEnvironmentVariable("FORGECAD_UI_RESIDUAL_OUTPUT");
    const QString output = requestedOutput.isEmpty() ? temporaryOutput.path() : requestedOutput;
    QDir().mkpath(output);
    const auto check = [&](bool value, const QString &name) {
        ++checks;
        if (!value) {
            ++failures;
            std::cout << "FAIL " << name.toStdString() << std::endl;
        }
    };
    const auto settle = [] {
        for (int i = 0; i < 8; ++i)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    const auto luminance = [](QColor c) {
        const auto channel = [](double x) {
            return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
        };
        return .2126 * channel(c.redF()) + .7152 * channel(c.greenF()) + .0722 * channel(c.blueF());
    };
    const auto contrast = [&](QColor a, QColor b) {
        const double x = luminance(a), y = luminance(b);
        return (std::max(x, y) + .05) / (std::min(x, y) + .05);
    };
    const auto snapshot = [&](QTextBrowser *browser, const QString &needle, const QString &label) {
        QTextCursor cursor = browser->document()->find(needle);
        check(!cursor.isNull(), label + " text exists");
        const auto format = cursor.charFormat();
        const bool explicitColor = format.hasProperty(QTextFormat::ForegroundBrush);
        const QColor fg =
            explicitColor ? format.foreground().color() : browser->palette().color(QPalette::Text);
        const QColor base = browser->palette().color(QPalette::Base);
        const double ratio = contrast(fg, base);
        samples.append(QJsonObject{{"label", label},
                                   {"needle", needle},
                                   {"explicitForeground", explicitColor},
                                   {"foreground", fg.name()},
                                   {"paletteText", browser->palette().color(QPalette::Text).name()},
                                   {"base", base.name()},
                                   {"contrast", ratio},
                                   {"role", browser->property("functionMessageRole").toString()}});
        check(ratio >= 4.5, label + " readable foreground");
        browser->grab().save(output + "/" + label + ".png");
        return fg;
    };
    initializeApplicationTheme(*qApp);
    setApplicationThemeMode(*qApp, ApplicationThemeMode::Light);
    settle();
    auto *window = new PdfWindow;
    window->resize(1000, 760);
    const QString primitiveError = window->viewport_->createPrimitive(PrimitiveParameters{}, "Synthetic box");
    check(primitiveError.isEmpty(), "synthetic primitive created");
    QAction *section = nullptr;
    for (auto *action : window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("Vista in sezione")) {
            section = action;
            break;
        }
    check(section != nullptr, "section action exists");
    if (section)
        section->setChecked(true);
    settle();
    QDialog *sectionPanel = nullptr;
    for (auto *dialog : window->findChildren<QDialog *>())
        if (dialog->windowTitle() == QStringLiteral("Sezione"))
            sectionPanel = dialog;
    check(sectionPanel != nullptr, "section production dialog exists");
    if (sectionPanel) {
        auto *combo = sectionPanel->findChild<QComboBox *>();
        QLabel *info = nullptr;
        for (auto *label : sectionPanel->findChildren<QLabel *>())
            if (label->wordWrap())
                info = label;
        const QRect naturalGeometry = sectionPanel->geometry();
        const QPoint naturalPosition = sectionPanel->pos();
        combo->setCurrentIndex(3);
        settle();
        if (info)
            check(info->height() >= info->heightForWidth(info->width()),
                  "section full picking instruction fits without reopening");
        window->viewport_->cancelReferencePick();
        settle();
        check(info && !info->text().isEmpty(), "section real instruction exists");
        if (info) {
            samples.append(QJsonObject{{"label", "section-natural-layout"},
                                       {"width", sectionPanel->width()},
                                       {"height", sectionPanel->height()},
                                       {"labelHeight", info->height()},
                                       {"requiredLabelHeight", info->heightForWidth(info->width())}});
            check(info->height() >= info->heightForWidth(info->width()),
                  "section full instruction fits after cancel without reopening");
            check(sectionPanel->width() == naturalGeometry.width(),
                  "section instruction preserves natural width");
            check(sectionPanel->pos() == naturalPosition,
                  "section instruction preserves position");
            sectionPanel->grab().save(output + "/section-natural-layout.png");
            sectionPanel->resize(sectionPanel->size() + QSize(120, 100));
            settle();
            const QRect userGeometry = sectionPanel->geometry();
            const QPoint userPosition = sectionPanel->pos();
            combo->setCurrentIndex(0);
            combo->setCurrentIndex(3);
            window->viewport_->cancelReferencePick();
            settle();
            check(sectionPanel->width() == userGeometry.width() &&
                      sectionPanel->height() >= userGeometry.height(),
                  "section instruction preserves larger user size");
            check(sectionPanel->pos() == userPosition,
                  "section instruction preserves user position");
            check(info->height() >= info->heightForWidth(info->width()),
                  "section full instruction fits after user resize");
            combo->setCurrentIndex(0);
            settle();
            sectionPanel->resize(naturalGeometry.size());
            settle();
            const QSize hiddenSize = sectionPanel->size();
            const QPoint hiddenPosition = sectionPanel->pos();
            sectionPanel->hide();
            combo->setCurrentIndex(3);
            window->viewport_->cancelReferencePick();
            QAction *parameters = nullptr;
            for (auto *action : window->findChildren<QAction *>())
                if (action->text() == QStringLiteral("Parametri della sezione..."))
                    parameters = action;
            check(parameters != nullptr, "section parameters action exists");
            if (parameters)
                parameters->trigger();
            settle();
            check(info->height() >= info->heightForWidth(info->width()),
                  "section hidden instruction fits immediately on reopening");
            check(sectionPanel->width() == hiddenSize.width() && sectionPanel->pos() == hiddenPosition,
                  "section reopening preserves user width and position");
            samples.append(QJsonObject{{"label", "section-reopened-layout"},
                                       {"width", sectionPanel->width()},
                                       {"height", sectionPanel->height()},
                                       {"labelHeight", info->height()},
                                       {"requiredLabelHeight", info->heightForWidth(info->width())}});
            sectionPanel->grab().save(output + "/section-reopened-layout.png");
            const QString text = info->text();
            QColor first;
            int iteration = 0;
            for (auto mode :
                 {ApplicationThemeMode::Light, ApplicationThemeMode::Dark, ApplicationThemeMode::Light}) {
                setApplicationThemeMode(*qApp, mode);
                settle();
                const auto fg = info->palette().color(info->foregroundRole());
                const auto bg = info->palette().color(info->backgroundRole());
                const QString label = QString("section-%1").arg(iteration++);
                samples.append(QJsonObject{{"label", label},
                                           {"foreground", fg.name()},
                                           {"base", bg.name()},
                                           {"contrast", contrast(fg, bg)}});
                check(contrast(fg, bg) >= 4.5, label + " readable instruction");
                check(info->text() == text, label + " instruction retained");
                if (iteration == 1)
                    first = fg;
                if (iteration == 2)
                    check(fg != first, "open section follows dark theme");
                if (iteration == 3)
                    check(fg == first, "open section restores light theme");
                sectionPanel->grab().save(output + "/" + label + ".png");
            }
        }
        sectionPanel->hide();
    }
    // A second, hidden viewport contains a real error fixture; no user document is touched.
    CadViewport reports;
    reports.extrusions_ = window->viewport_->extrusions_;
    ExtrusionObject invalid = reports.extrusions_.first();
    invalid.name = "Invalid synthetic body";
    auto broken = std::make_shared<Kernel::Body>(*invalid.forgeBody);
    broken->face(broken->faces().front()).surface.reset();
    invalid.forgeBody = broken;
    reports.extrusions_.append(invalid);
    check(!reports.massReport(1).ok, "synthetic invalid body triggers real report error");
    int computations = 0;
    reports.setWorkCallback([&](bool start, const QString &, bool) {
        if (start)
            ++computations;
    });
    std::exception_ptr callbackError;
    QTimer::singleShot(0, [&] {
        QDialog *dialog = nullptr;
        for (auto *d : window->findChildren<QDialog *>())
            if (d->windowTitle() == QStringLiteral("Proprieta' di massa"))
                dialog = d;
        try {
            if (!dialog)
                throw std::runtime_error("mass dialog missing");
            settle();
            ClipboardGuard clipboardGuard;
            auto *browser = dialog->findChild<QTextBrowser *>();
            if (!browser)
                throw std::runtime_error("mass browser missing");
            QComboBox *body = nullptr;
            for (auto *box : dialog->findChildren<QComboBox *>())
                if (box->itemText(0) == QStringLiteral("Tutti i solidi visibili"))
                    body = box;
            if (!body)
                throw std::runtime_error("mass body combo missing");
            body->setCurrentIndex(1);
            settle();
            auto *copy = static_cast<QPushButton *>(nullptr);
            for (auto *button : dialog->findChildren<QPushButton *>())
                if (button->text() == "Copia")
                    copy = button;
            if (!copy)
                throw std::runtime_error("copy button missing");
            copy->click();
            const QString plain = qApp->clipboard()->text();
            check(plain.contains("Volume") && plain.contains("Synthetic box"),
                  "copy contains computed report");
            const QString html = browser->toHtml(), text = browser->toPlainText();
            auto *document = browser->document();
            QTextCursor selection = document->find("Volume");
            browser->setTextCursor(selection);
            browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum() / 2);
            const int scroll = browser->verticalScrollBar()->value(), anchor = selection.anchor(),
                      position = selection.position();
            const int before = computations;
            QColor lightLabel, lightHint;
            int iteration = 0;
            for (auto mode :
                 {ApplicationThemeMode::Light, ApplicationThemeMode::Dark, ApplicationThemeMode::Light}) {
                setApplicationThemeMode(*qApp, mode);
                settle();
                const QString label = QString("mass-%1").arg(iteration++);
                const QColor fg = snapshot(browser, "Volume", label + "-label");
                const QColor hint = snapshot(browser, "Calcolo:", label + "-hint");
                if (iteration == 1) {
                    lightLabel = fg;
                    lightHint = hint;
                }
                if (iteration == 2) {
                    check(fg != lightLabel, "open mass label follows dark theme");
                    check(hint != lightHint, "open mass hint follows dark theme");
                }
                if (iteration == 3) {
                    check(fg == lightLabel && hint == lightHint, "open mass restores light theme");
                }
                check(browser->document() == document && browser->toHtml() == html &&
                          browser->toPlainText() == text,
                      label + " document retained");
                check(browser->textCursor().anchor() == anchor &&
                          browser->textCursor().position() == position,
                      label + " selection retained");
                check(browser->verticalScrollBar()->value() == scroll, label + " scroll retained");
                check(computations == before, label + " no mass recomputation");
                copy->click();
                check(qApp->clipboard()->text() == plain, label + " copy retained");
            }
            for (auto type : {QEvent::PaletteChange, QEvent::StyleChange}) {
                QEvent event(type);
                QApplication::sendEvent(dialog, &event);
                settle();
                check(browser->document() == document && browser->toHtml() == html,
                      "explicit event preserves document");
                check(browser->textCursor().anchor() == anchor &&
                          browser->verticalScrollBar()->value() == scroll,
                      "explicit event preserves view state");
                check(computations == before, "explicit event no mass recomputation");
            }
            body->setCurrentIndex(2);
            settle();
            check(browser->toPlainText().contains("Calcolo non riuscito:"), "production error report shown");
            QColor lightError;
            iteration = 0;
            for (auto mode :
                 {ApplicationThemeMode::Light, ApplicationThemeMode::Dark, ApplicationThemeMode::Light}) {
                setApplicationThemeMode(*qApp, mode);
                settle();
                const QColor error =
                    snapshot(browser, "Calcolo non riuscito:", QString("error-%1").arg(iteration++));
                if (iteration == 1)
                    lightError = error;
                if (iteration == 2)
                    check(error != lightError, "open error follows dark theme");
                if (iteration == 3)
                    check(error == lightError, "open error restores light theme");
            }
            body->setCurrentIndex(1);
            settle();
            snapshot(browser, "Volume", "error-to-normal");
            check(browser->toPlainText() == text, "error to normal restores report");
            check(browser->palette().color(QPalette::Text) != lightError,
                  "error to normal clears error foreground");
            copy->click();
            check(qApp->clipboard()->text() == plain, "error to normal restores copy");
        } catch (...) {
            callbackError = std::current_exception();
        }
        if (dialog) {
            dialog->reject();
        } else {
            // A failed lookup must still leave the nested modal loop. Only this
            // synthetic window owns these dialogs; never inspect other windows.
            for (auto *owned : window->findChildren<QDialog *>())
                if (owned->isVisible())
                    owned->reject();
        }
    });
    massPropertiesDialog(window, &reports);
    delete window;
    QFile result(output + "/metrics.json");
    if (!result.open(QIODevice::WriteOnly))
        throw std::runtime_error("metrics output failed");
    result.write(QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}, {"samples", samples}})
                     .toJson());
    std::cout << "UI residual checks " << checks << ", failures " << failures << std::endl;
    if (callbackError)
        std::rethrow_exception(callbackError);
    if (failures)
        throw std::runtime_error("UI residual theme regression");
}
