// Incluso dentro ViewportInteractionTest; sessione isolata dal runner.
static void documentTabsTests() {
    using namespace ForgeCad;
    const auto ok=[](const QString &error) { if(!error.isEmpty())throw std::runtime_error(error.toStdString()); };
    const auto pump=[](const std::function<bool()> &finished) {
        QElapsedTimer timer;timer.start();
        while(!finished()&&timer.elapsed()<8000){QApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(1);}
        require(finished(),"completamento asincrono delle schede");
    };
    const auto drawingSheetBytes=[&](const DrawingSheet &sheet){QByteArray bytes;ok(encodeDrawingSheet(sheet,bytes));return bytes;};
    QTemporaryDir temporary;require(temporary.isValid(),"directory delle schede");
    QSettings().setValue("interface/documentTabsBottom",false);
    DocumentTabsWindow host;auto *tabs=host.documentTabs();
    auto *first=host.newModel();host.show();QApplication::processEvents();
    require(tabs->count()==1&&!first->isWindow()&&first->window()==&host,"3D incorporato nella sola finestra applicativa");
    ok(first->viewport_->createPrimitive(PrimitiveParameters{},"Corpo A"));
    require(first->saveDocumentPath(temporary.filePath("A.prt")),"salva primo modello");
    const auto body=first->viewport_->currentDocument().extrusions.front().forgeBody;
    first->newDrawing();
    auto *drawing=dynamic_cast<DrawingWindow *>(tabs->currentWidget());
    require(drawing&&tabs->count()==2&&!drawing->isWindow()&&drawing->window()==&host,"nuova tavola nella stessa finestra");
    require(drawing->document().sheet.drawing.model.documentId==first->viewport_->documentVersion().documentId,"tavola collegata al modello di partenza");
    DrawingView view;view.kind=DrawingViewKind::ProjectedEdges;view.scale=1;
    ok(drawing->editor()->insertView(view,{view.id,"Vista test",{100,100},{first->viewport_->currentDocument().extrusions.front().featureId}}));
    ok(drawing->savePath(temporary.filePath("A.drw")));
    auto *editor=drawing->editor();const auto sheetBytes=drawingSheetBytes(editor->sheet());
    const auto firstPath=first->documentFilePath();
    require(host.openPath(firstPath)&&tabs->count()==2&&tabs->currentWidget()==first,"riapertura modello attiva la scheda esistente");
    require(host.openPath(drawing->filePath())&&tabs->count()==2&&tabs->currentWidget()==drawing,"riapertura tavola senza duplicati");
    // Due modelli e una tavola hanno viewport, Undo e comandi indipendenti.
    auto secondState=first->viewport_->currentDocument();forkDocumentIdentity(secondState);
    secondState.lengthUnit=LengthUnit::Inch;secondState.lengthUnitSet=true;
    ok(saveDocumentFile(temporary.filePath("B.prt"),secondState));
    require(host.openPath(temporary.filePath("B.prt"))&&tabs->count()==3,"apertura secondo modello conserva i documenti");
    auto *second=dynamic_cast<PdfWindow *>(tabs->currentWidget());require(second&&second!=first,"viewport separati");
    for(int i=0;i<20;++i){host.activateDocument(first);host.activateDocument(drawing);host.activateDocument(second);QApplication::processEvents();}
    require(first->viewport_->currentDocument().extrusions.front().forgeBody==body,"cambio scheda riusa il B-rep");
    require(drawing->editor()==editor&&drawingSheetBytes(editor->sheet())==sheetBytes,"cambio scheda conserva editor e quote senza rigenerare");
    require(displayLengthUnit(first)==LengthUnit::Millimeter&&displayLengthUnit(second)==LengthUnit::Inch,"unita' indipendenti nella stessa finestra");
    host.activateDocument(first);ok(first->viewport_->createPrimitive(PrimitiveParameters{},"Corpo Undo"));
    first->viewport_->undo();require(first->viewport_->currentDocument().extrusions.size()==1,"Undo del solo modello attivo");
    first->viewport_->redo();require(first->viewport_->currentDocument().extrusions.size()==2&&second->viewport_->currentDocument().extrusions.size()==1,"Redo indipendente dagli altri documenti");
    {
        host.activateDocument(second);
        FunctionDialogPanel command(second,true);command.show();QApplication::processEvents();
        require(second->viewport_->isAncestorOf(&command)&&!first->viewport_->isAncestorOf(&command),"pannello nel viewport del secondo modello");
        require(second->viewport_->interactionLocked()&&!first->viewport_->interactionLocked(),"blocco del comando limitato al documento proprietario");
        require(first->findChild<QAction *>("documentSaveAction")->isEnabled(),"comandi degli altri documenti disponibili");
        command.hide();
    }
    host.activateDocument(first);
    // La scorciatoia deve salvare solo la pagina con il focus.
    first->viewport_->setFocus();QApplication::processEvents();
    host.activateWindow();QApplication::setActiveWindow(&host);
    QTest::keyClick(host.windowHandle(),Qt::Key_S,Qt::ControlModifier);
    require(!first->documentModified_,"Ctrl+S salva il modello attivo anche con altre schede aperte");
    host.setTabsAtBottom(true);require(tabs->tabPosition()==QTabWidget::South,"schede spostate in basso");
    {DocumentTabsWindow next;require(next.documentTabs()->tabPosition()==QTabWidget::South,"posizione delle schede persistente");}
    host.activateDocument(drawing);ok(drawing->addSheet("Secondo foglio",{420,297}));
    require(drawing->findChild<QAction *>("documentTabsTopAction"),"preferenza disponibile dopo la ricreazione dell'editor 2D");
    drawing->findChild<QAction *>("documentTabsTopAction")->trigger();require(tabs->tabPosition()==QTabWidget::North,"posizione cambiata dal menu 2D");
    ok(drawing->setActiveSheet(0));require(drawing->sheetCount()==2,"schede dei fogli distinte da quelle dei documenti");
    tabs->tabBar()->moveTab(tabs->indexOf(second),0);require(tabs->widget(0)==second,"schede riordinabili");
    // Un pannello flottante viene nascosto e ripristinato con il suo documento.
    auto *panel=new QDialog(first,Qt::Tool);panel->setModal(false);host.activateDocument(first);panel->show();QApplication::processEvents();
    host.activateDocument(second);require(!panel->isVisible(),"pannello inattivo nascosto");host.activateDocument(first);require(panel->isVisible(),"pannello ripristinato al cambio scheda");delete panel;
    // Annulla nella tavola: nessun modello precedentemente approvato viene chiuso.
    QTimer responses;responses.setInterval(5);QList<QMessageBox::StandardButton> answers{QMessageBox::Discard,QMessageBox::Cancel};
    QObject::connect(&responses,&QTimer::timeout,&host,[&]{if(auto *box=dynamic_cast<QMessageBox *>(QApplication::activeModalWidget())){
        if(!box->property("answeredInTabsTest").toBool()){box->setProperty("answeredInTabsTest",true);box->button(answers.isEmpty()?QMessageBox::Discard:answers.takeFirst())->click();}
    }});responses.start();
    host.activateDocument(first);ok(first->viewport_->createPrimitive(PrimitiveParameters{},"Modifica non salvata"));
    host.close();require(tabs->count()==3&&host.isVisible()&&!host.closingAll()&&answers.isEmpty(),"Annulla chiusura applicazione conserva tutte le schede");
    // La X di una tavola modificata deve a sua volta rispettare Annulla.
    answers={QMessageBox::Cancel};host.closeDocument(drawing);
    require(tabs->count()==3&&drawing->isModified()&&answers.isEmpty(),"Annulla chiusura della singola scheda");
    // Primo modello ancora sporco: salva prima delle prove di rilascio del lock.
    require(first->saveDocumentPath(firstPath),"salva dopo Annulla senza perdita di modifiche");
    responses.stop();
    // Chiusura singola 3D: arresto asincrono e rilascio del lock, altri editor intatti.
    QPointer<PdfWindow> secondGuard=second;host.closeDocument(second);
    pump([&]{return tabs->count()==2&&secondGuard.isNull();});
    require(tabs->indexOf(first)>=0&&tabs->indexOf(drawing)>=0,"chiusura di un modello conserva la tavola");
    // Chiudere il modello sorgente lascia aperta e utilizzabile la tavola salvata.
    QPointer<PdfWindow> firstGuard=first;host.closeDocument(first);pump([&]{return tabs->count()==1&&firstGuard.isNull();});
    require(tabs->currentWidget()==drawing&&drawing->editor(),"tavola sopravvive alla chiusura del modello");
    ok(drawing->savePath(drawing->filePath()));
    host.activateDocument(drawing);
    // Prova anche il salvataggio da tastiera del 2D, senza ambiguita' con il 3D.
    ok(drawing->renameSheet(1,"Rinominato"));drawing->editor()->setFocus();QApplication::processEvents();
    host.activateWindow();QApplication::setActiveWindow(&host);
    QTest::keyClick(host.windowHandle(),Qt::Key_S,Qt::ControlModifier);
    require(!drawing->isModified(),"Ctrl+S salva la tavola attiva dopo il cambio foglio");
    if(QApplication::arguments().contains("--tabs-screenshot"))host.grab().save(QApplication::arguments().value(QApplication::arguments().indexOf("--tabs-screenshot")+1));
    host.close();pump([&]{return tabs->count()==0&&!host.isVisible();});
    std::cout<<"PASS document tabs: 3D/2D, reuse, state/Undo, shortcuts, position, sheets, floating panels, cancel, asynchronous close\n";
}
