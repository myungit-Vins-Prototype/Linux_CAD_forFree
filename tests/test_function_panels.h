#include <QStyleHints>
#include <QStyleOptionButton>

static void functionPanelsReview() {
    using namespace ForgeCad;
    // Un dialogo inatteso non deve lasciare il runner nel suo event loop.
    bool timedOut = false;
    QTimer watchdog;
    watchdog.setInterval(5000);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        timedOut = true;
        for (auto *top : QApplication::topLevelWidgets()) {
            const auto dialogs = top->findChildren<QDialog *>();
            for (auto *opened : dialogs) if (opened->isVisible()) opened->reject();
            if (auto *opened = qobject_cast<QDialog *>(top); opened && opened->isVisible()) opened->reject();
        }
    });
    watchdog.start();
    // Chiavi temporanee univoche: nessuna preferenza utente viene sovrascritta.
    const QString token = QStringLiteral("review-panels-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    struct SettingsCleanup {
        QStringList keys;
        ~SettingsCleanup() { QSettings settings; for (const auto &key : keys) settings.remove(key); }
    } cleanup;
    const auto key = [&](const QString &id) { const auto result = QStringLiteral("view/functionPanelSizes/v2/") + token + id; cleanup.keys.append(result); return result; };
    // Il cambio tema deve aggiornare anche un pannello gia' aperto e i suoi
    // editor; la nuova presentazione non deve duplicare contenuti o bloccare
    // conferma/annullamento quando lo spazio disponibile diminuisce.
    {
        struct RestorePalette {
            QPalette saved = QApplication::palette();
            ApplicationThemeMode mode = applicationThemeMode();
            ~RestorePalette() {
                setApplicationThemeMode(*qApp, mode);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
                QMetaObject::invokeMethod(qApp->styleHints(), "colorSchemeChanged", Qt::DirectConnection,
                                         Q_ARG(Qt::ColorScheme, scheme));
#endif
                QApplication::setPalette(saved);
                for (int i=0; i<3; ++i) QApplication::processEvents();
            }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            Qt::ColorScheme scheme = qApp->styleHints()->colorScheme();
#endif
        } restore;
        PdfWindow owner;
        auto *themeMenu=owner.findChild<QMenu *>(QStringLiteral("applicationThemeMenu"));
        require(themeMenu && themeMenu->actions().size()==3, "menu Visualizza/Tema nella finestra reale");
        FunctionDialogPanel panel(&owner, false);
        panel.setSizeIdentity(token + "-style");
        auto *fields = panel.createScrollableForm();
        auto *amount = new QDoubleSpinBox(&panel);
        auto *disabled = new QDoubleSpinBox(&panel); disabled->setEnabled(false);
        auto *enabled = new QCheckBox(QStringLiteral("Opzione"), &panel);
        enabled->setFocusPolicy(Qt::NoFocus);
        auto *faces = new QListWidget(&panel); faces->setMaximumHeight(90);
        auto *face = new QListWidgetItem(QStringLiteral("F0"), faces); face->setCheckState(Qt::Unchecked);
        fields->addRow(QStringLiteral("Facce"), faces);
        fields->addRow(QStringLiteral("Valore"), amount); fields->addRow(disabled); fields->addRow(enabled);
        QVector<FunctionMessage *> messages;
        for (auto role : {FunctionMessageRole::Information, FunctionMessageRole::Success, FunctionMessageRole::Error, FunctionMessageRole::Hint}) {
            auto *message = new FunctionMessage(QStringLiteral("Stato della funzione"), &panel);
            setFunctionMessageRole(message,role);
            fields->addRow(message); messages.append(message);
        }
        const auto luminance = [](const QColor &c) {
            const auto linear = [](double v) {return v <= .04045 ? v/12.92 : std::pow((v+.055)/1.055,2.4);};
            return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());
        };
        auto *actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &panel);
        fields->addRow(actions);
        panel.setPanelOpacity(100);
        owner.show(); panel.show(); QApplication::processEvents();
        for (bool dark : {true, false, true}) {
            themeMenu->actions()[dark ? 2 : 1]->trigger();
            for (int i=0; i<3; ++i) QApplication::processEvents();
            require((panel.panelColor().lightness() < 128) == dark
                    && (amount->palette().color(QPalette::Base).lightness() < 128) == dark,
                    "pannello ed editor aperti seguono entrambe le transizioni del tema");
            require(std::abs(amount->palette().color(QPalette::Text).lightness()
                             - amount->palette().color(QPalette::Base).lightness()) > 100,
                    "testo dei campi leggibile nei due temi");
            require(disabled->palette().color(QPalette::Disabled, QPalette::Text)
                    != amount->palette().color(QPalette::Active, QPalette::Text), "campi disabilitati distinguibili");
            require(faces->palette().color(QPalette::Base) == amount->palette().color(QPalette::Base), "elenchi con lo stesso fondo dei campi");
            for (auto *message : messages) {
                const double foreground=luminance(message->palette().color(QPalette::Text));
                const double background=luminance(panel.panelColor());
                require((std::max(foreground,background)+.05)/(std::min(foreground,background)+.05) >= 4.5,
                        "messaggi semantici leggibili e aggiornati al cambio tema");
            }
            // Confronta pixel realmente disegnati, non la sola palette: il bug
            // produceva nero su nero negli elenchi e bianco su bianco nei box.
            const auto readableCheck = [&](auto capture, auto setState) {
                setState(Qt::Unchecked); const auto empty = capture().convertToFormat(QImage::Format_RGB32);
                setState(Qt::Checked); const auto checked = capture().convertToFormat(QImage::Format_RGB32);
                int contrasting = 0;
                for (int y=0; y<empty.height(); ++y) for (int x=0; x<empty.width(); ++x) {
                    const auto a=empty.pixelColor(x,y), b=checked.pixelColor(x,y);
                    if (std::max({std::abs(a.red()-b.red()), std::abs(a.green()-b.green()), std::abs(a.blue()-b.blue())}) > 100) ++contrasting;
                }
                return contrasting >= 6;
            };
            QStyleOptionButton option; option.initFrom(enabled); option.rect=enabled->rect();
            const auto indicator=enabled->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, enabled);
            require(readableCheck([&]{return enabled->grab(indicator).toImage();}, [&](Qt::CheckState state){enabled->setCheckState(state);}), "spunta normale contrastata nei due temi");
            auto row=faces->visualItemRect(face); row.setWidth(faces->style()->pixelMetric(QStyle::PM_IndicatorWidth)+8);
            require(readableCheck([&]{return faces->viewport()->grab(row).toImage();}, [&](Qt::CheckState state){face->setCheckState(state);}), "spunta elenco contrastata nei due temi");
            const auto qa=qEnvironmentVariable("FORGECAD_PANEL_REVIEW_OUTPUT");
            if (!qa.isEmpty()) {
                QDir().mkpath(qa);
                require(panel.grab().save(qa + (dark ? "/checkbox-scuro.png" : "/checkbox-chiaro.png")), "cattura pannello e checkbox");
                themeMenu->ensurePolished(); themeMenu->adjustSize();
                require(themeMenu->grab().save(qa + (dark ? "/menu-scuro.png" : "/menu-chiaro.png")), "cattura menu reale del tema");
            }
        }
        enabled->setChecked(false); enabled->click(); require(enabled->isChecked(), "spunte funzionanti nel contenitore a schede");
        panel.resize(380, 260); QApplication::processEvents();
        require(panel.rect().contains(QRect(actions->mapTo(&panel, QPoint()), actions->size())),
                "azioni raggiungibili nel nuovo layout compatto");
        panel.hide();
    }
    const QString firstKey = key("-extend"), secondKey = key("-network"), thirdKey = key("-poles");
    QSettings settings;
    settings.setValue(firstKey, QSize(430, 320)); settings.setValue(secondKey, QSize(510, 380)); settings.setValue(thirdKey, QSize(600, 440));
    for (const auto &entry : QVector<QPair<QString, QSize>>{{"-extend",{430,320}},{"-network",{510,380}},{"-poles",{600,440}},{"-extend",{430,320}}}) {
        FunctionDialogPanel panel(nullptr, false); panel.setObjectName("surfaceToolDialog");
        panel.setSizeIdentity(token + entry.first); panel.setWindowTitle(QStringLiteral("Titolo variabile, creazione/modifica"));
        panel.createScrollableForm()->addRow(new QLineEdit(&panel)); panel.show(); QApplication::processEvents();
        require(panel.size() == entry.second, "dimensione indipendente dalla classe, objectName e titolo; riapertura stabile");
        panel.hide();
    }
    const auto legacy = token + "-legacy";
    const auto legacyKey = QStringLiteral("view/functionPanelSizes/") + legacy; cleanup.keys.append(legacyKey);
    settings.setValue(legacyKey, QSize(455,335)); const auto migratedKey = key("-migrated");
    { FunctionDialogPanel panel(nullptr, false); panel.setSizeIdentity(token + "-migrated", {legacy}); panel.show(); QApplication::processEvents();
      require(panel.size()==QSize(455,335) && settings.value(migratedKey).toSize()==QSize(455,335), "migrazione alias esplicito senza eliminare preferenza legacy"); }
    const auto otherLegacy = token + "-other"; const auto otherLegacyKey=QStringLiteral("view/functionPanelSizes/")+otherLegacy; cleanup.keys.append(otherLegacyKey);
    settings.setValue(otherLegacyKey,QSize(500,400));const auto conflictKey=key("-conflict");
    { FunctionDialogPanel panel(nullptr,false);panel.setSizeIdentity(token+"-conflict",{legacy,otherLegacy});panel.show();QApplication::processEvents();
      require(!settings.contains(conflictKey),"preferenze legacy discordanti non migrate arbitrariamente"); }

    QString message = QStringLiteral("Errore: è impossibile completare l’operazione.\n") + QString(1800, QChar('X'));
    for (int i=0;i<80;++i) message += QStringLiteral("\nRiga %1: dettaglio completo, accenti àèìòù.").arg(i);
    FunctionDialogPanel dialog(nullptr,false);dialog.setSizeIdentity(token+"-diagnostic");auto *form=dialog.createScrollableForm();
    for(int i=0;i<12;++i)form->addRow(QString::number(i),new QLineEdit(&dialog));
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    bool diagnosticOk=false;
    QTimer::singleShot(0,&dialog,[&]{
        dialog.resize(380,260);QApplication::processEvents();const auto before=dialog.size();
        buttons->button(QDialogButtonBox::Ok)->click();QApplication::processEvents();
        auto *text=dialog.findChild<QPlainTextEdit *>("functionApplyError");
        if(text){
            auto *scroll=dialog.findChild<QScrollArea *>("functionDialogScroll");scroll->ensureWidgetVisible(text);QApplication::processEvents();
            const auto complete=text->toPlainText();text->selectAll();
            diagnosticOk=text->isReadOnly()&&text->lineWrapMode()==QPlainTextEdit::NoWrap&&complete.startsWith(message)
                &&text->horizontalScrollBar()->maximum()>0&&text->verticalScrollBar()->maximum()>0
                &&text->textCursor().selectedText().replace(QChar::ParagraphSeparator,'\n')==complete
                &&dialog.size()==before&&buttons->isVisibleTo(&dialog)
                &&dialog.rect().contains(QRect(buttons->mapTo(&dialog,QPoint()),buttons->size()));
            if(QGuiApplication::platformName()==QStringLiteral("offscreen")){text->copy();diagnosticOk=diagnosticOk&&QApplication::clipboard()->text()==complete;}
            text->setPlainText(QStringLiteral("Errore breve"));QApplication::processEvents();diagnosticOk=diagnosticOk&&dialog.size()==before;
        }
        dialog.reject();
    });
    require(!ForgeCad::Gui::runUntilApplied(dialog,form,buttons,[&]{return message;})&&diagnosticOk,
            "errore completo selezionabile/copiabile, scorrimento H/V e pulsanti accessibili senza ridimensionare");

    QMainWindow window;CadViewport viewport(&window);viewport.setMinimumSize(0,0);window.setCentralWidget(&viewport);window.resize(960,720);window.show();QApplication::processEvents();
    FunctionDialogPanel embedded(&viewport,false);embedded.setSizeIdentity(token+"-poles");embedded.createScrollableForm()->addRow(new QLineEdit(&embedded));embedded.show();QApplication::processEvents();
    const auto preferred=embedded.size();window.resize(520,400);QApplication::processEvents();
    require(embedded.size().width()<preferred.width()&&settings.value(thirdKey).toSize()==QSize(600,440),"viewport piccolo non sovrascrive dimensione preferita");
    window.resize(960,720);QApplication::processEvents();require(embedded.size()==preferred,"ritorno alla dimensione preferita quando il viewport cresce");embedded.hide();
    // Estensione e poli richiedono una superficie valida prima di mostrare il pannello.
    DocumentState document;
    ExtrusionObject source;
    source.feature = BodyFeature::Imported;
    source.name = QStringLiteral("Superficie NURBS di prova");
    source.importData = QByteArray::fromStdString(Kernel::writeStep({{"NURBS", surface_fixture::patch(), false, {0,0,0}, nullptr, {}}}));
    document.extrusions.append(source);
    viewport.loadDocument(document);
    require(!viewport.extrusions().isEmpty() && viewport.extrusions().front().forgeBody
            && !surfaceBasisHash(viewport.extrusions().front().forgeBody).isEmpty(), "fixture NURBS disponibile per i pannelli reali");
    for(int mode=0;mode<3;++mode){
        ExtrusionObject definition;definition.feature=BodyFeature::SurfaceTools;definition.surfaceTool.mode=mode;definition.firstBody=0;bool checked=false;
        QTimer closePanel;
        QObject::connect(&closePanel,&QTimer::timeout,&window,[&]{
            auto *opened=window.findChild<QDialog *>("surfaceToolDialog");auto *panel=dynamic_cast<FunctionDialogPanel *>(opened);
            if(panel && panel->isVisible()){
                const QStringList ids{"surface-extend-to","surface-network","surface-poles"};
                checked=panel->panelSizeSettingsKey()==QStringLiteral("view/functionPanelSizes/v2/")+ids[mode];
                closePanel.stop();panel->reject();
            }
        });
        closePanel.start(10);
        require(!surfaceToolDialog(&window,&viewport,"Titolo identico",-1,definition,[](const auto &){return QString();})&&checked&&!timedOut,"pannelli reali estensione/rete/poli usano identita semantiche distinte senza timeout");
    }

    // Modifica reale di un piano gia definito: rimozione, riselezione e rollback.
    DocumentState pointsDocument;
    SketchObject points;points.name=QStringLiteral("Punti per il piano");
    points.segments={{{0,0},{10,0}},{{10,0},{0,10}},{{2,2},{0,20}}};
    pointsDocument.sketches.append(points);viewport.loadDocument(pointsDocument);
    const auto pointRef=[](int segment,int endpoint) {
        GeometryRef ref;ref.kind=6;ref.index=0;ref.element={0,segment,endpoint};return ref;
    };
    ExtrusionObject plane;plane.feature=BodyFeature::DatumPlane;plane.name=QStringLiteral("Piano tre punti");
    plane.datum.mode=1;plane.datum.refs={pointRef(0,0),pointRef(0,1),pointRef(1,1)};
    require(viewport.createBody(plane).isEmpty(),"piano per tre punti esistente prima della modifica");
    const auto original=viewport.extrusions().front();
    const auto sameRef=[](const GeometryRef &a,const GeometryRef &b) {
        return a.kind==b.kind&&a.index==b.index&&a.element==b.element&&a.featureId==b.featureId;
    };
    const auto runPlaneEdit=[&](bool accept) {
        bool visited=false,applied=false,valid=false;
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=window.findChild<QDialog *>("datumPlaneDialog");
            require(dialog && dialog->isVisible(),"pannello reale modifica piano aperto");visited=true;
            auto *buttons=dialog->findChild<QDialogButtonBox *>();
            auto *status=dialog->findChild<QPlainTextEdit *>("datumStatus");
            require(viewport.datumPreviewValid_ && viewport.refMarks_.size()==3,"anteprima iniziale e tre punti evidenziati");
            for(int slot=1;slot<=3;++slot) {
                auto *clear=dialog->findChild<QToolButton *>(QStringLiteral("datumReferenceClear%1").arg(slot));
                require(clear && clear->isEnabled(),"punto esistente rimovibile");clear->click();
                require(!clear->isEnabled() && !viewport.datumPreviewValid_ && viewport.refMarks_.size()==3-slot,"rimuovi toglie riferimento, marchio e anteprima");
            }
            buttons->button(QDialogButtonBox::Ok)->click();
            require(!applied && dialog->isVisible() && viewport.referencePicking(),"OK incompleto avvia scelta senza applicare dati vecchi");
            // Consegna dal picker: la callback di produzione deve aggiornare gli slot.
            for(const auto &ref:QVector<GeometryRef>{pointRef(2,0),pointRef(0,1),pointRef(1,1)}) {
                require(bool(viewport.refPickFinished_),"callback picker disponibile");
                auto callback=viewport.refPickFinished_;callback(true,ref);
            }
            require(viewport.datumPreviewValid_ && viewport.refMarks_.size()==3,"tre punti riselezionati ripristinano anteprima");
            auto *mode=dialog->findChild<QComboBox *>("datumMode");
            mode->setCurrentIndex(0);mode->setCurrentIndex(1);
            require(!viewport.datumPreviewValid_ && viewport.refMarks_.isEmpty(),"cambio tipo non ripristina punti rimossi");
            for(const auto &ref:QVector<GeometryRef>{pointRef(2,0),pointRef(0,1),pointRef(1,1)}){auto callback=viewport.refPickFinished_;callback(true,ref);}
            require(status && viewport.datumPreviewValid_,"anteprima valida dopo cambio tipo e completamento");
            const auto qa=qEnvironmentVariable("FORGECAD_UI_PANEL_QA");
            if(!qa.isEmpty())require(window.grab().save(qa+QStringLiteral("/piano-tre-punti.png")),"cattura piano");
            buttons->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();
        });
        const bool result=datumDialog(&window,&viewport,QStringLiteral("Modifica piano per tre punti"),0,original,[&](const auto &definition) {
            applied=true;valid=definition.datum.refs.size()==3 && sameRef(definition.datum.refs[0],pointRef(2,0))
                && sameRef(definition.datum.refs[1],original.datum.refs[1]) && sameRef(definition.datum.refs[2],original.datum.refs[2]);
            return viewport.updateBody(0,definition);
        });
        require(visited && result==accept && applied==accept && (!accept||valid),"modifica usa solo i nuovi riferimenti confermati");
        require(!viewport.referencePicking() && !viewport.datumPreviewValid_ && viewport.refMarks_.isEmpty(),"chiusura pulisce picker, anteprima e riferimenti");
    };
    runPlaneEdit(false);
    require(sameRef(viewport.extrusions()[0].datum.refs[0],original.datum.refs[0]),"Annulla conserva il piano originale");
    runPlaneEdit(true);
    require(std::abs(viewport.extrusions()[0].datumFrame.origin[0]-4)<1e-9
        &&std::abs(viewport.extrusions()[0].datumFrame.origin[1]-4)<1e-9,"Conferma rigenera il piano sui nuovi punti");
    viewport.undo();require(sameRef(viewport.extrusions()[0].datum.refs[0],original.datum.refs[0]),"Undo ripristina i punti originali");
    viewport.redo();require(sameRef(viewport.extrusions()[0].datum.refs[0],pointRef(2,0)),"Redo ripristina il punto sostituito");
    QTemporaryDir savedPlane;const auto planePath=savedPlane.filePath(QStringLiteral("piano.prt"));
    require(saveDocumentFile(planePath,viewport.currentDocument(),false).isEmpty(),"salvataggio piano modificato");
    DocumentState decodedPlane;require(loadDocumentFile(planePath,decodedPlane).isEmpty()
        && sameRef(decodedPlane.extrusions[0].datum.refs[0],pointRef(2,0)),"riapertura conserva il nuovo punto");

    DocumentState loftDocument;
    for(double z:{0.,5.}) {
        SketchObject section;section.name=QStringLiteral("Profilo %1").arg(z);section.customFrame=true;
        section.frame.origin[2]=z;
        CurveObject profile;profile.tool=DrawingTool::Nurbs;profile.degree=2;
        profile.controlPoints={{0,0},{2,2},{4,0}};profile.knots={0,0,0,1,1,1};
        section.curves.append(profile);loftDocument.sketches.append(section);
    }
    viewport.loadDocument(loftDocument);
    ExtrusionObject loft;loft.feature=BodyFeature::Loft;loft.loftSurface=true;loft.loftSketches={0,1};
    loft.loftStartContinuity=loft.loftEndContinuity=1;
    require(viewport.createBody(loft).isEmpty(),"loft di superficie per i controlli di verso");
    const auto defaultLoft=viewport.extrusions()[0];
    auto reverseKey=defaultLoft;reverseKey.loftStartReverse=true;
    require(CadViewport::previewKey(defaultLoft,0)!=CadViewport::previewKey(reverseKey,0),"verso iniziale invalida la cache anteprima");
    reverseKey=defaultLoft;reverseKey.loftEndReverse=true;
    require(CadViewport::previewKey(defaultLoft,0)!=CadViewport::previewKey(reverseKey,0),"verso finale invalida la cache anteprima");
    bool toggled=false;
    QTimer::singleShot(0,&window,[&] {
        auto *dialog=window.findChild<QDialog *>("loftDialog");require(dialog,"pannello loft reale");
        auto *start=dialog->findChild<QPushButton *>("surfaceStartReverse");
        auto *end=dialog->findChild<QPushButton *>("surfaceEndReverse");
        require(start&&end&&start->isEnabled()&&end->isEnabled()&&!start->isChecked()&&!end->isChecked(),"due pulsanti G1 indipendenti");
        start->click();require(start->isChecked()&&!end->isChecked(),"inversione iniziale indipendente");
        end->click();end->click();toggled=true;
        const auto qa=qEnvironmentVariable("FORGECAD_UI_PANEL_QA");
        if(!qa.isEmpty()) {
            auto *scroll=dialog->findChild<QScrollArea *>("functionDialogScroll");
            scroll->ensureWidgetVisible(start);QApplication::processEvents();
            require(window.grab().save(qa+QStringLiteral("/loft-inverti-normale.png")),"cattura controllo verso");
        }
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    require(loftDialog(&window,&viewport,QStringLiteral("Modifica loft"),0,defaultLoft,[&](const auto &definition) {
        return viewport.updateBody(0,definition);
    })&&toggled,"Conferma loft con normale iniziale invertita");
    require(viewport.extrusions()[0].loftStartReverse&&!viewport.extrusions()[0].loftEndReverse,"verso salvato nella feature");
    viewport.undo();require(!viewport.extrusions()[0].loftStartReverse,"Undo del verso G1");
    viewport.redo();require(viewport.extrusions()[0].loftStartReverse,"Redo del verso G1");
    QByteArray bytes;require(encodeDocument(viewport.currentDocument(),bytes,false).isEmpty(),"codifica verso formato corrente");
    DocumentState reopened;require(decodeDocument(bytes,reopened).isEmpty()&&reopened.extrusions[0].loftStartReverse
        &&!reopened.extrusions[0].loftEndReverse,"riapertura conserva versi indipendenti");
    auto before=viewport.currentDocument();before.extrusions[0].loftStartReverse=false;
    auto normalized=before;normalized.version.checkinId={};normalized.baseCheckinId={};normalized.orientation=AxesOrientation{};normalized.lengthUnit=LengthUnit::Millimeter;
    QByteArray oldBytes;require(encodeDocumentDefinition60ForHash(normalized,oldBytes).isEmpty(),"fixture reale formato60");
    DocumentState legacyLoft;require(decodeDocument(oldBytes,legacyLoft).isEmpty()&&!legacyLoft.extrusions[0].loftStartReverse&&!legacyLoft.extrusions[0].loftEndReverse,"file60 conserva il verso automatico precedente");
    const auto oldHash=QCryptographicHash::hash(oldBytes,QCryptographicHash::Sha256);bool matches=false;
    require(drawingModelDefinitionMatches(before,oldHash,matches).isEmpty()&&matches,"tavole con firma60 restano compatibili");
    require(drawingModelDefinitionMatches(viewport.currentDocument(),oldHash,matches).isEmpty()&&!matches,"inversione G1 invalida la vecchia firma della tavola");
    require(!encodeDocumentDefinition53ForHash(viewport.currentDocument(),oldBytes).isEmpty(),"hash storico non ignora nuovi versi");
    auto normalized61=normalized;normalized61.extrusions[0].loftStartReverse=true;
    require(encodeDocumentDefinition61ForHash(normalized61,oldBytes).isEmpty(),"fixture reale formato61");
    const auto hash61=QCryptographicHash::hash(oldBytes,QCryptographicHash::Sha256);
    require(drawingModelDefinitionMatches(viewport.currentDocument(),hash61,matches).isEmpty()&&matches,"tavole con firma61 e verso G1 restano compatibili");
    auto crossHashState=viewport.currentDocument();crossHashState.extrusions[0].loftGuideCrossConditions.resize(1);
    crossHashState.extrusions[0].loftGuideCrossConditions[0].mode=1;
    require(!encodeDocumentDefinition61ForHash(crossHashState,oldBytes).isEmpty(),"firma61 non ignora nuove condizioni delle guide");
    QTimer::singleShot(0,&window,[&] {
        auto *dialog=window.findChild<QDialog *>("loftDialog");require(dialog,"riapertura pannello loft");
        require(dialog->findChild<QPushButton *>("surfaceStartReverse")->isChecked()
            &&!dialog->findChild<QPushButton *>("surfaceEndReverse")->isChecked(),"riapertura pannello conserva lo stato dei pulsanti");
        dialog->findChild<QPushButton *>("surfaceStartReverse")->click();
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
    });
    require(!loftDialog(&window,&viewport,QStringLiteral("Modifica loft"),0,viewport.extrusions()[0],[](const auto &){return QString();})
        &&viewport.extrusions()[0].loftStartReverse,"Annulla non cambia il verso memorizzato");
    {
        QMainWindow owner; CadViewport view;
        for (auto unit : {LengthUnit::Millimeter,LengthUnit::Centimeter,LengthUnit::Meter,LengthUnit::Inch,LengthUnit::Foot}) {
            view.setLengthUnit(unit);
            ExtrusionObject fill;fill.feature=BodyFeature::FillSurface;
            QTimer::singleShot(0,&owner,[&] {
                auto *dialog=owner.findChild<QDialog *>("fillDialog");require(dialog,"pannello riempimento aperto");
                for (const auto &name : {"fillFaceInfluence","fillGuideWeight","fillShapeWeight"}) {
                    auto *spin=dynamic_cast<ExpressionSpinBox *>(dialog->findChild<QDoubleSpinBox *>(name));
                    require(spin && !spin->isLengthMeasurement(),"coefficiente riempimento adimensionale");
                    require(std::abs(spin->valueFromText(QStringLiteral("1"))-1.0)<1e-12,"coefficiente non convertito dalle unita");
                }
                dialog->reject();
            });
            require(!fillDialog(&owner,&view,QStringLiteral("Riempimento"),-1,fill,[](const auto &){return QString();}),"annullamento riempimento senza modifiche");
        }
    }
    previewConsistencyReview();
    functionPanelsLifecycleReview();
    require(!timedOut, "pannelli completati senza intervento del watchdog");
}

static void previewConsistencyReview() {
    QMainWindow owner; CadViewport view;
    DocumentState doc;
    for (double z : {0.,3.}) {
        SketchObject sketch;sketch.name=QStringLiteral("Profilo %1").arg(z);sketch.customFrame=true;sketch.frame.origin[2]=z;
        sketch.segments={{{2,0},{4,0}},{{4,0},{4,2}},{{4,2},{2,2}},{{2,2},{2,0}}};doc.sketches.append(sketch);
    }
    ExtrusionObject box;box.feature=BodyFeature::Primitive;doc.extrusions={box,box};view.loadDocument(doc);
    ExtrusionObject ext;ext.feature=BodyFeature::Extrusion;ext.sketchIndex=0;ext.distance=5;ext.mergeOperation=0;
    QTimer poll;poll.setInterval(10);bool checked=false;
    QObject::connect(&poll,&QTimer::timeout,&owner,[&] {
        if (!view.preview_.valid) return;
        poll.stop();
        auto *combo=owner.findChild<QComboBox *>("extrusionExtent");require(combo,"limite estrusione presente");
        QWidget *ancestor=combo->parentWidget();
        while(ancestor && !dynamic_cast<QDialog *>(ancestor)) ancestor=ancestor->parentWidget();
        auto *panel=dynamic_cast<QDialog *>(ancestor);
        require(panel,"dialogo estrusione presente");
        auto *label=panel->findChild<QPlainTextEdit *>("functionPreviewMessage");
        auto changed=ext;changed.distance=7;view.requestPreview(changed,-1);
        combo->setCurrentIndex(3);
        require(!view.preview_.valid && view.preview_.key.isEmpty() && !view.preview_.geometry,"limite mancante invalida subito forma e chiave");
        require(label->toPlainText().contains(QStringLiteral("seleziona")),"anteprima attende riferimento");
        QTimer::singleShot(150,panel,[&,panel,label] {
            require(!view.preview_.valid && view.preview_.key.isEmpty(),"worker precedente non ripubblica forma superata");
            require(label->toPlainText().contains(QStringLiteral("seleziona")),"stato attesa resta coerente");
            checked=true;panel->reject();
        });
    });
    poll.start();extrusionDialog(&owner,&view,QStringLiteral("Estrusione"),-1,ext,[](const auto &){return QString();});poll.stop();
    require(checked,"regressione estrusione completata");
    QObject::disconnect(&poll,nullptr,&owner,nullptr);
    ExtrusionObject loft;loft.feature=BodyFeature::Loft;loft.loftSketches={0};
    checked=false;
    QTimer::singleShot(0,&owner,[&] {
        auto *panel=owner.findChild<QDialog *>("loftDialog");require(panel,"dialogo loft presente");
        auto *label=panel->findChild<QPlainTextEdit *>("functionPreviewMessage");
        auto *sections=panel->findChild<QListWidget *>("loftSections");
        require(!view.preview_.valid && view.preview_.key.isEmpty() && label->toPlainText().contains(QStringLiteral("almeno due")),"una sezione non mostra calcolo inesistente");
        require(sections && sections->count()==2,"due sezioni disponibili");sections->item(1)->setCheckState(Qt::Checked);
        QObject::connect(&poll,&QTimer::timeout,panel,[&,panel,label,sections] {
            if(!view.preview_.valid)return;
            poll.stop();sections->item(1)->setCheckState(Qt::Unchecked);
            require(!view.preview_.valid && view.preview_.key.isEmpty() && label->toPlainText().contains(QStringLiteral("almeno due")),"loft pronta torna in attesa quando manca una sezione");
            checked=true;panel->reject();
        });poll.start();
    });
    loftDialog(&owner,&view,QStringLiteral("Loft"),-1,loft,[](const auto &){return QString();});poll.stop();
    require(checked,"regressione loft completata");
    for(int mode:{3,4})for(bool copy:{false,true})for(bool multiple:{false,true}) {
        ExtrusionObject map;map.feature=BodyFeature::SurfaceTools;map.firstBody=0;map.surfaceTool.mode=mode;map.surfaceTool.mappingCopy=copy;
        if(multiple)for(int i:{0,1}){GeometryRef ref;ref.kind=11;ref.index=i;map.surfaceTool.sources.append(ref);}
        view.requestPreview(map,-1);
        require(view.preview_.replaced==ForgeCad::hiddenOperands(map),"mapping anteprima e risultato conservano le stesse sorgenti");
        view.clearPreview();
    }
}

static void functionPanelsLifecycleReview() {
 const auto check = [](bool value, const char *name) { require(value, name); };
 QMainWindow a,b;auto *va=new CadViewport(&a);a.setCentralWidget(va);auto *vb=new CadViewport(&b);b.setCentralWidget(vb);
 DocumentState doc;ExtrusionObject box;box.feature=BodyFeature::Primitive;box.name="Box";doc.extrusions.append(box);va->loadDocument(doc);vb->loadDocument(doc);
 {
 FunctionDialogPanel panel(&a,false);panel.setObjectName("loftDialog");auto *form=panel.createScrollableForm();ExtrusionObject loft;SurfaceEndControls controls(panel,*form,va,-1,loft);
 auto *mode=panel.findChild<QComboBox*>("surfaceStartContinuity");auto *pick=panel.findChild<QPushButton*>("surfaceStartFacesPick");
 mode->setCurrentIndex(3);pick->click();check(va->referencePicking(),"G1 faces picking");mode->setCurrentIndex(0);check(!va->referencePicking()&&!pick->isEnabled()&&!pick->isChecked(),"G0 cancels and unchecks picker");
 mode->setCurrentIndex(4);pick->click();check(va->referencePicking(),"plane picking");mode->setCurrentIndex(3);check(!va->referencePicking()&&!pick->isChecked(),"plane to faces cancels stale role");pick->click();check(va->referencePicking(),"new face picker can start");controls.stop();
 }
 {
 FunctionDialogPanel panel(&a,false);GeometryReferenceList list(panel,va,"multiple");list.setSelectionMode(QAbstractItemView::ExtendedSelection);
 GeometryRef x;x.kind=5;x.index=0;x.point.subshape=0;GeometryRef y=x;y.point.subshape=1;GeometryRef z=x;z.point.subshape=2;
 list.setReferences({x,y,z});list.setCurrentRow(1);list.item(0)->setSelected(true);list.setReferences({x,y,z});check(list.selectedItems().size()==2&&list.currentRow()==1,"multiselection refresh");
 list.setReferences({z,x,y});check(list.selectedItems().size()==2&&list.item(1)->isSelected()&&list.item(2)->isSelected()&&list.currentRow()==2,"selection follows reordered identities");
 list.setCurrentRow(0,QItemSelectionModel::NoUpdate);list.setReferences({y,z,x});check(list.currentRow()==1&&!list.currentItem()->isSelected()&&list.selectedItems().size()==2,"current stays separate from selection");
 list.setReferences({z,x});check(list.currentRow()==0&&list.selectedItems().size()==1&&list.item(1)->isSelected(),"removed references are discarded");
 x.point.x=37;list.setReferences({z,x});check(list.selectedItems().size()==1&&list.item(1)->isSelected(),"same face identity survives changed pick point");
 }
 {
 ExtrusionObject fill;fill.feature=BodyFeature::Pattern;fill.firstBody=0;
 QTimer::singleShot(0,&a,[&]{auto *pick=a.findChild<QPushButton*>("fillPatternBoundaryPick");check(bool(pick),"fill picker exists");if(!pick)return;pick->click();check(va->referencePicking(),"fill boundary picking");for(auto *d:a.findChildren<QDialog*>())if(d->findChild<QPushButton*>("fillPatternBoundaryPick"))d->reject();});
 check(!fillPatternDialog(&a,va,"Fill",-1,fill,[](const auto&){return QString();}),"fill cancel result");check(!va->referencePicking()&&!va->interactionLocked(),"fill cancel clears picker and lock");
 }
 for (int mode : {0,3}) {
 ExtrusionObject mapping;mapping.feature=BodyFeature::SurfaceTools;mapping.firstBody=0;mapping.surfaceTool.mode=mode;
 QTimer::singleShot(0,&a,[&]{auto *pick=a.findChild<QPushButton*>(mode==3?"mappingFacePick":"mappingSourceFacePick");check(bool(pick),"mapping picker exists");if(!pick)return;pick->click();check(va->referencePicking(),"mapping face picking");for(auto *d:a.findChildren<QDialog*>())if(d->findChild<QPushButton*>(mode==3?"mappingFacePick":"mappingSourceFacePick")) {QObject::connect(d,&QDialog::finished,d,[&]{check(!va->referencePicking(),"mapping picker cleared synchronously at finished");});d->reject();}});
 check(!surfaceMappingDialog(&a,va,"Mapping",-1,mapping,[](const auto&){return QString();}),"mapping cancel result");check(!va->referencePicking()&&!va->interactionLocked(),"mapping cancel clears picker and lock");
 }
 {
 FunctionDialogPanel pa(&a,false);bool rejected=false;
 QTimer::singleShot(0,&a,[&]{
 FunctionDialogPanel pb(&b,false);auto *form=pb.createScrollableForm();PreviewScope preview(vb,pb,form,-1);preview.request(box);vb->beginReferencePick(ForgeCad::DatumRoleFace,-1);vb->setReferencePickCallback([](bool,GeometryRef){});
 check(!runModeless(&b,vb,pb),"cross workspace runner rejected");rejected=true;check(!vb->referencePicking()&&!vb->interactionLocked()&&!vb->previewPending()&&pb.isHidden(),"second runner leaves no picker lock or panel");check(va->interactionLocked()&&!pa.isHidden(),"first runner remains active");check(!b.statusBar()->currentMessage().isEmpty(),"second workspace explains restriction");pa.reject();
 });
 check(!runModeless(&a,va,pa)&&rejected&&!va->interactionLocked(),"first runner closes and unlocks immediately");
 FunctionDialogPanel pb(&b,false);QTimer::singleShot(0,&pb,&QDialog::reject);check(!runModeless(&b,vb,pb)&&!vb->interactionLocked(),"second workspace opens after first closes");
 }
 {
 ForgeCad::DocumentTabsWindow host;auto *da=new QMainWindow;auto *db=new QMainWindow;
 da->setProperty("documentWorkspace",true);db->setProperty("documentWorkspace",true);
 auto *vda=new CadViewport(da);auto *vdb=new CadViewport(db);da->setCentralWidget(vda);db->setCentralWidget(vdb);
 host.addDocument(da,false);host.addDocument(db,false);host.activateDocument(da);FunctionDialogPanel pa(da,false);
 QTimer::singleShot(0,&host,[&]{host.activateDocument(db);FunctionDialogPanel pb(db,false);check(!runModeless(db,vdb,pb),"real tabs second runner rejected");host.activateDocument(da);pa.reject();});
 check(!runModeless(da,vda,pa)&&!vda->interactionLocked(),"real tabs original runner unlocked");
 }
 {
 FunctionDialogPanel pa(&a,false);
 QTimer::singleShot(0,&a,[&]{FunctionDialogPanel nested(&a,false);QTimer::singleShot(0,&nested,&QDialog::reject);check(!runModeless(&a,va,nested)&&va->interactionLocked(),"same workspace nesting preserves outer lock");pa.reject();});
 check(!runModeless(&a,va,pa)&&!va->interactionLocked(),"same workspace nested runner restores lock");
 }
}

static void previewPickerContextReview() {
    QMainWindow owner; CadViewport view;
    DocumentState state; SketchObject boundary;
    boundary.segments={{{0,0},{20,0}},{{20,0},{20,12}},{{20,12},{0,12}},{{0,12},{0,0}}};state.sketches={boundary};
    ExtrusionObject seed;seed.feature=BodyFeature::Primitive;seed.primitive.origin[0]=-50;state.extrusions={seed};view.loadDocument(state);
    const auto original=view.extrusions()[0].forgeBody;
    GeometryRef contour;contour.kind=7;contour.index=0;ExtrusionObject fill;fill.feature=BodyFeature::Pattern;fill.firstBody=0;
    fill.pattern.kind=3;fill.pattern.refs={contour};fill.pattern.spacing=4;fill.pattern.spacing2=4;
    QTimer poll;poll.setInterval(20);QElapsedTimer elapsed;elapsed.start();bool checked=false;
    QObject::connect(&poll,&QTimer::timeout,&owner,[&] {
        auto *source=owner.findChild<QComboBox*>("fillPatternSource");if(!source)return;
        QDialog *panel=nullptr;for(auto *d:owner.findChildren<QDialog*>())if(d->findChild<QComboBox*>("fillPatternSource"))panel=d;
        if(!view.preview_.valid){if(elapsed.elapsed()>15000){poll.stop();panel->reject();}return;}
        poll.stop();require(!view.isBodyVisibleInScene(0),"anteprima pronta sostituisce sorgente");
        const auto geometry=view.preview_.geometry;const auto key=view.preview_.key;
        QPushButton *pick=nullptr;for(auto *b:source->parentWidget()->findChildren<QPushButton*>())if(b->text()==QStringLiteral("Dalla vista"))pick=b;
        require(pick,"picker sorgente presente");pick->click();
        require(view.referencePicking()&&view.refPickOwner_==-1&&view.isBodyVisibleInScene(0),"creazione mostra input selezionabile durante picker");
        GeometryRef hit;require(view.pickReference(view.projectWorldPoint(QVector3D(-49.5,.5,.5)).toPoint(),hit)&&hit.index==0,"raggio sceglie sorgente visibile");
        require(view.preview_.valid&&view.preview_.geometry==geometry&&view.preview_.key==key,"picker conserva anteprima pronta");
        pick->click();require(!view.referencePicking()&&!view.isBodyVisibleInScene(0),"uscita picker ripristina anteprima");
        auto *edges=panel->findChild<QPushButton*>("fillPatternBoundaryPick");edges->click();
        require(view.referencePicking()&&view.isBodyVisibleInScene(0),"picker contorni mostra scena input");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);
        require(!view.referencePicking(),"Esc termina picker contorni");checked=true;panel->reject();
    });poll.start();fillPatternDialog(&owner,&view,"Fill picker",-1,fill,[](const auto&){return QString();});poll.stop();
    require(checked&&!view.referencePicking()&&!view.interactionLocked()&&view.preview_.key.isEmpty(),"annulla ripulisce picker e anteprima");
    require(view.extrusions().size()==1&&view.extrusions()[0].forgeBody==original&&view.isBodyVisibleInScene(0),"annulla conserva modello e visibilita'");
    // La storia futura rimane esclusa anche con una lista di candidati esplicita.
    DocumentState history;history.extrusions={seed,seed};view.loadDocument(history);view.setHistoryPosition(1);
    view.setPickBodies({0,1});view.beginReferencePick(ForgeCad::DatumRoleFace,-1);
    require(view.isBodyVisibleInScene(0)&&!view.isBodyVisibleInScene(1),"rollback non mostra candidati futuri");view.cancelReferencePick();view.setPickBodies({});
    view.setHistoryPosition(2);view.beginReferencePick(ForgeCad::DatumRoleFace,1);
    require(view.isBodyVisibleInScene(0)&&!view.isBodyVisibleInScene(1),"modifica usa scena precedente alla feature");view.cancelReferencePick();
    view.extrusions_[0].visible=false;view.setPickBodies({0});view.beginReferencePick(ForgeCad::DatumRoleFace,-1);
    require(view.referenceBodyEligible(0)&&view.isBodyVisibleInScene(0),"operando consumato esplicito resta visibile e selezionabile");view.cancelReferencePick();view.setPickBodies({});
    ExtrusionObject curve;curve.feature=BodyFeature::Helix;
    auto line=std::make_shared<ForgeCad::Kernel::Line<3>>(ForgeCad::Kernel::Vec3(),ForgeCad::Kernel::Vec3(0,0,1));
    curve.curve=std::make_shared<ForgeCad::Kernel::TrimmedCurve<3>>(line,0,3);view.extrusions_.append(curve);view.setHistoryPosition(3);
    view.beginReferencePick(ForgeCad::DatumRolePoint,-1);require(view.isBodyDrawn(2)&&view.isBodyVisibleInScene(2),"curva 3D visibile durante scelta punto");view.cancelReferencePick();
}

static void mappingLinkedPickerReview() {
    QMainWindow owner;CadViewport view;DocumentState state;ExtrusionObject box;box.feature=BodyFeature::Primitive;box.primitive.size[0]=4;box.primitive.size[1]=2;state.extrusions={box};view.loadDocument(state);
    const auto body=view.extrusions()[0].forgeBody;const auto face=body->faces()[0];ForgeCad::Kernel::Vec3 point;ForgeCad::Kernel::Vec2 anchor;
    require(ForgeCad::Kernel::faceInteriorPoint(*body,face,point,&anchor),"punto faccia per mappa");GeometryRef ref;ref.kind=5;ref.index=0;ref.point.subshape=face.index;ref.point.geometry=int(body->face(face).surface->type());ref.point.x=point.x();ref.point.y=point.y();ref.point.z=point.z();GeometryRef plane;plane.kind=1;plane.index=0;
    ExtrusionObject flat;flat.feature=BodyFeature::SurfaceTools;flat.firstBody=0;flat.surfaceTool.mode=3;flat.surfaceTool.mappingQuilt=true;flat.surfaceTool.targets={ref,plane};flat.surfaceTool.sources={ref};flat.surfaceTool.mappingU=anchor.x();flat.surfaceTool.mappingV=anchor.y();
    require(view.createBody(flat).isEmpty()&&view.createBody(box).isEmpty(),"mappa associata reale e sorgente wrap");
    ExtrusionObject wrap;wrap.feature=BodyFeature::SurfaceTools;wrap.firstBody=2;wrap.surfaceTool.mode=4;wrap.surfaceTool.mappingQuilt=true;wrap.surfaceTool.targets={ref,plane};
    for(bool source:{false,true}) {
        int applied=0;bool checked=false;QTimer::singleShot(0,&owner,[&] {
            auto *target=owner.findChild<QPushButton*>("mappingFacePick");auto *pick=owner.findChild<QPushButton*>(source?"mappingSourceFacePick":"mappingFacePick");auto *planes=owner.findChild<QComboBox*>("mappingPlane");
            QDialog *panel=nullptr;for(auto *d:owner.findChildren<QDialog*>())if(d->findChild<QComboBox*>("mappingPlane"))panel=d;
            require(target&&pick&&planes&&panel,"controlli mapping presenti");pick->click();require(view.referencePicking(),"picker mapping avviato");
            int linked=-1;for(int i=0;i<planes->count();++i)if(planes->itemText(i).startsWith(QStringLiteral("Mappa di")))linked=i;
            require(linked>=0,"mappa associata disponibile");planes->setCurrentIndex(linked);
            require(!target->isEnabled()&&view.referencePicking()==source,"collegamento termina solo selezione destinazione");
            if(source){require(pick->isEnabled(),"sorgente ancora applicabile");QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);require(!view.referencePicking(),"Esc termina sorgente conservata");panel->reject();}
            else panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            checked=true;
        });
        surfaceMappingDialog(&owner,&view,"Mapping linked",-1,wrap,[&](const auto&){++applied;return QString();});
        require(checked&&applied==(source?0:1),"mapping collegato confermabile senza picker obsoleto");
        require(!view.referencePicking()&&!view.interactionLocked()&&view.preview_.key.isEmpty()&&view.extrusions().size()==3,"mapping ripulisce stato senza mutare modello");
    }
}
