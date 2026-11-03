#include <QStyleHints>

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
            ~RestorePalette() {
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
        QWidget owner;
        FunctionDialogPanel panel(&owner, false);
        panel.setSizeIdentity(token + "-style");
        auto *fields = panel.createScrollableForm();
        auto *amount = new QDoubleSpinBox(&panel);
        auto *disabled = new QDoubleSpinBox(&panel); disabled->setEnabled(false);
        auto *enabled = new QCheckBox(QStringLiteral("Opzione"), &panel);
        fields->addRow(QStringLiteral("Valore"), amount); fields->addRow(disabled); fields->addRow(enabled);
        auto *actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &panel);
        fields->addRow(actions);
        panel.setPanelOpacity(100);
        owner.show(); panel.show(); QApplication::processEvents();
        for (bool dark : {true, false, true}) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            // Segnale simulato: non cambia le preferenze del desktop. Una
            // setPalette manuale sarebbe corretta dal controller del tema.
            require(QMetaObject::invokeMethod(qApp->styleHints(), "colorSchemeChanged", Qt::DirectConnection,
                    Q_ARG(Qt::ColorScheme, dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light)),
                    "segnale del tema disponibile");
#else
            QApplication::setPalette(QPalette(QColor(dark ? "#111820" : "#f0f0f0")));
#endif
            for (int i=0; i<3; ++i) QApplication::processEvents();
            require((panel.panelColor().lightness() < 128) == dark
                    && (amount->palette().color(QPalette::Base).lightness() < 128) == dark,
                    "pannello ed editor aperti seguono entrambe le transizioni del tema");
            require(std::abs(amount->palette().color(QPalette::Text).lightness()
                             - amount->palette().color(QPalette::Base).lightness()) > 100,
                    "testo dei campi leggibile nei due temi");
            require(disabled->palette().color(QPalette::Disabled, QPalette::Text)
                    != amount->palette().color(QPalette::Active, QPalette::Text), "campi disabilitati distinguibili");
        }
        enabled->click(); require(enabled->isChecked(), "spunte funzionanti nel contenitore a schede");
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
    QByteArray bytes;require(encodeDocument(viewport.currentDocument(),bytes,false).isEmpty(),"codifica verso formato61");
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
    QTimer::singleShot(0,&window,[&] {
        auto *dialog=window.findChild<QDialog *>("loftDialog");require(dialog,"riapertura pannello loft");
        require(dialog->findChild<QPushButton *>("surfaceStartReverse")->isChecked()
            &&!dialog->findChild<QPushButton *>("surfaceEndReverse")->isChecked(),"riapertura pannello conserva lo stato dei pulsanti");
        dialog->findChild<QPushButton *>("surfaceStartReverse")->click();
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
    });
    require(!loftDialog(&window,&viewport,QStringLiteral("Modifica loft"),0,viewport.extrusions()[0],[](const auto &){return QString();})
        &&viewport.extrusions()[0].loftStartReverse,"Annulla non cambia il verso memorizzato");
    require(!timedOut, "pannelli completati senza intervento del watchdog");
}
