static void filletRefinements() {
  using namespace ForgeCad;
  struct SettingsGuard {
    QMap<QString, QVariant> values;
    ~SettingsGuard() {
      QSettings settings;
      for (auto i = values.begin(); i != values.end(); ++i)
        if (i.value().isValid())
          settings.setValue(i.key(), i.value());
        else
          settings.remove(i.key());
    }
    void preserve(const QString &key) {
      values.insert(key, QSettings().value(key));
    }
  } settings;
  for (const QString &key : {QStringLiteral("view/functionPanelDockSide"),
                             QStringLiteral("view/functionPanelFullHeight")})
    settings.preserve(key);
  QSettings().setValue("view/functionPanelDockSide", 0);
  QSettings().setValue("view/functionPanelFullHeight", false);
  QMainWindow window;
  CadViewport view(&window);
  view.setMinimumSize(0, 0);
  window.setCentralWidget(&view);
  window.resize(1300, 940);
  window.show();
  QApplication::processEvents();
  const QString token = QStringLiteral("fillet-refinements-") +
                        QUuid::createUuid().toString(QUuid::WithoutBraces);
  {
    FunctionDialogPanel panel(&view, false);
    panel.setSizeIdentity(token + "-a");
    settings.preserve(panel.panelSizeSettingsKey());
    panel.createScrollableForm()->addRow(new QLineEdit(&panel));
    panel.show();
    QApplication::processEvents();
    require(panel.contentsMargins().top() == 36,
            "intestazione compatta condivisa di 36 px");
    auto *menu = panel.findChild<QToolButton *>("functionPanelDockMenu");
    require(menu && menu->isVisible(),
            "menu aggancio raggiungibile nell'intestazione");
    panel.findChild<QAction *>("functionPanelDockRight")->trigger();
    require(panel.dockSide() == 2 &&
                panel.x() + panel.width() == view.width() - 8,
            "aggancio a destra");
    panel.setFullHeight(true);
    require(panel.y() == 8 && panel.height() == view.height() - 16,
            "intera altezza disponibile");
    window.resize(1100, 740);
    QApplication::processEvents();
    require(panel.x() + panel.width() == view.width() - 8 &&
                panel.height() == view.height() - 16,
            "aggancio segue il ridimensionamento della finestra");
    FunctionDialogPanel next(&view, false);
    next.setSizeIdentity(token + "-b");
    settings.preserve(next.panelSizeSettingsKey());
    next.createScrollableForm()->addRow(new QLineEdit(&next));
    next.show();
    QApplication::processEvents();
    require(next.dockSide() == 2 && next.fullHeight() &&
                next.x() + next.width() == view.width() - 8,
            "nuovi pannelli ereditano aggancio e altezza");
    next.hide();
    QPoint start = panel.mapToGlobal(QPoint(20, 18));
    panel.beginEmbeddedMove(start);
    panel.updateEmbeddedMove(start - QPoint(150, 0));
    panel.endEmbeddedMove();
    require(panel.dockSide() == 0, "trascinamento sgancia il pannello");
    start = panel.mapToGlobal(QPoint(20, 18));
    panel.beginEmbeddedMove(start);
    panel.updateEmbeddedMove(start + QPoint(8 - panel.x(), 0));
    panel.endEmbeddedMove();
    require(panel.dockSide() == 1 && panel.x() == 8,
            "trascinamento riaggancia a sinistra");
    panel.setFullHeight(false);
    panel.setDockSide(0);
    panel.hide();
  }
  window.resize(1300, 940);
  QApplication::processEvents();
  PrimitiveParameters primitive;
  primitive.size[0] = 20;
  primitive.size[1] = 16;
  primitive.size[2] = 30;
  require(view.createPrimitive(primitive, "Base percorsi").isEmpty(),
          "base percorsi");
  const auto base = view.extrusions().first().forgeBody;
  const auto a = nearestEdge(*base, {0, 0, 15}, 1e-8),
             b = nearestEdge(*base, {20, 16, 15}, 1e-8);
  const QVector<EdgePoint> refs{edgeReference(*base, a, {0, 0, 15}),
                                edgeReference(*base, b, {20, 16, 15})};
  ChamferSpec spec;
  spec.fillet.mode = 2;
  spec.fillet.propagate = false;
  spec.fillet.radii = {1, 2, 1};
  spec.fillet.paths = {{{0}, {1, 2, 1}, true, false},
                       {{1}, {3, 3.5, 4, 3.5, 3}, true, false}};
  view.fitAll();
  bool checked = false;
  QString applyError;
  QTimer timer;
  timer.setInterval(20);
  QObject::connect(&timer, &QTimer::timeout, &window, [&] {
    auto *tabs = window.findChild<QTabWidget *>("filletTabs");
    if (!tabs || !tabs->isVisible())
      return;
    timer.stop();
    window.findChild<QCheckBox *>("filletAutomaticPreview")->setChecked(false);
    auto *list = window.findChild<QListWidget *>("filletEdges2");
    auto *count = window.findChild<QSpinBox *>("filletPointCount");
    const auto radius = [&] {
      return qobject_cast<QDoubleSpinBox *>(
          window.findChild<QTableWidget *>("filletRadiusTable")
              ->cellWidget(0, 2));
    };
    checked = list && list->count() == 2 && count->value() == 3 &&
              std::abs(radius()->value() - 1) < 1e-8;
    radius()->setValue(1.8);
    list->setCurrentRow(1);
    checked = checked && list->currentRow() == 1 && count->value() == 5 &&
              std::abs(radius()->value() - 3) < 1e-8;
    radius()->setValue(4);
    list->setCurrentRow(0);
    checked = checked && count->value() == 3 &&
              std::abs(radius()->value() - 1.8) < 1e-8;
    list->setCurrentRow(1);
    checked = checked && list->currentRow() == 1 && count->value() == 5 &&
              std::abs(radius()->value() - 4) < 1e-8;
    checked = checked && view.filletRadiusHandles_.size() == 5;
    if (!view.filletRadiusHandles_.isEmpty()) {
      const auto h = view.filletRadiusHandles_.first();
      const auto tip =
          view.projectWorldPoint(h.point + h.direction * float(h.radius));
      const auto delta = view.projectWorldPoint(h.point + h.direction * .5f) -
                         view.projectWorldPoint(h.point);
      QMouseEvent press(QEvent::MouseButtonPress, tip,
                        view.mapToGlobal(tip.toPoint()), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(&view, &press);
      const bool grabbed = view.filletRadiusDrag_ >= 0;
      checked = checked && grabbed;
      if (grabbed) {
        QMouseEvent move(QEvent::MouseMove, tip + delta,
                         view.mapToGlobal((tip + delta).toPoint()),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, tip + delta,
                            view.mapToGlobal((tip + delta).toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &move);
        QApplication::sendEvent(&view, &release);
      }
      checked = checked && std::abs(radius()->value() - 4.5) < 2e-5;
    }
    const auto changed = view.edgePickChanged_;
    changed(0, {refs[1], refs[0]});
    checked = checked && list->currentRow() == 0 && count->value() == 5 &&
              std::abs(radius()->value() - 4.5) < 2e-5;
    list->setCurrentRow(1);
    checked = checked && count->value() == 3 &&
              std::abs(radius()->value() - 1.8) < 1e-8;
    list->setCurrentRow(0);
    changed(0, refs);
    checked = checked && list->currentRow() == 1 && count->value() == 5 &&
              std::abs(radius()->value() - 4.5) < 2e-5;
    auto *illustration = window.findChild<QWidget *>("filletIllustration2");
    if (illustration) {
      const auto image = illustration->grab().toImage();
      const auto color = image.pixelColor(15, 40);
      checked =
          checked &&
          (QApplication::palette().color(QPalette::Window).lightness() < 128 ||
           color == QColor(Qt::white));
    }
    tabs->parentWidget()
        ->findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Ok)
        ->click();
  });
  timer.start();
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &window, [&] {
    for (auto *d : window.findChildren<QDialog *>())
      if (d->isVisible())
        d->reject();
  });
  watchdog.start(5000);
  const auto result =
      filletDialog(&window, &view, "Raggio variabile", 0, -1, refs, 4, spec,
                   [&](int body, const auto &selected, double radius,
                       bool chamfer, const auto &parameters) {
                     applyError =
                         view.createBlend(body, selected, radius, chamfer,
                                          "Due percorsi", parameters);
                     return applyError;
                   });
  timer.stop();
  watchdog.stop();
  if (!applyError.isEmpty())
    std::cerr << applyError.toStdString() << std::endl;
  require(result.applied && checked, "tabelle indipendenti selezionabili e "
                                     "raggio trascinato nel pannello reale");
  require(view.filletRadiusHandles_.isEmpty() && !view.filletRadiusChanged_,
          "marker e callback rimossi alla chiusura");
  const auto &built = view.extrusions().last();
  require(checkBody(*built.forgeBody, {true, true}).empty(),
          "geometria valida con raggi diversi sui due percorsi");
  const auto &paths = built.chamferSpec.fillet.paths;
  require(paths.size() == 2 && paths[0].radii.size() == 3 &&
              paths[1].radii.size() == 5 &&
              std::abs(paths[0].radii[0] - 1.8) < 1e-8 &&
              std::abs(paths[1].radii[0] - 4.5) < 2e-5,
          "tabelle e modifica grafica nel modello");
  const double volume = massProperties(*built.forgeBody).volume;
  for (bool cache : {false, true}) {
    QByteArray bytes;
    DocumentState restored;
    require(encodeDocument(view.currentDocument(), bytes, cache).isEmpty() &&
                decodeDocument(bytes, restored).isEmpty(),
            "salvataggio formato 59 dei percorsi");
    const auto &saved = restored.extrusions.last().chamferSpec.fillet.paths;
    require(saved.size() == 2 && saved[0].edges == paths[0].edges &&
                saved[1].radii == paths[1].radii,
            "associazioni e raggi conservati");
    CadViewport reopened;
    reopened.loadDocument(restored);
    require(
        reopened.extrusions().last().forgeBody &&
            std::abs(
                massProperties(*reopened.extrusions().last().forgeBody).volume -
                volume) < 1e-5,
        "rigenerazione dei percorsi dopo riapertura");
  }
  view.undo();
  view.redo();
  require(view.extrusions().last().chamferSpec.fillet.paths.size() == 2,
          "undo redo dei percorsi");
  QThreadPool::globalInstance()->waitForDone();
  QApplication::processEvents();
  std::cout << "PASS pannelli compatti, aggancio persistente, percorsi "
               "indipendenti, trascinamento e salvataggio"
            << std::endl;
}
