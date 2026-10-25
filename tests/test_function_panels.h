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
    require(!timedOut, "pannelli completati senza intervento del watchdog");
}
