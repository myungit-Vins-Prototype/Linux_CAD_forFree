// Incluso nella classe friend ViewportInteractionTest.
static void loftCorrespondenceControls(bool native = false) {
    using namespace ForgeCad; using namespace ForgeCad::Kernel;
    const auto check=[](bool ok,const char *text){if(!ok)throw std::runtime_error(text);};
    DocumentState doc;
    for(int i=0;i<3;++i){
        SketchObject s;s.name=QStringLiteral("Profilo %1").arg(i+1);s.storyId=501+i;s.beforeFeatureId=601;s.storyOrder=i;
        s.customFrame=true;s.frame.origin[2]=i*10;
        CurveObject c;c.tool=DrawingTool::Circle;c.controlPoints={{0,0},{5.0+i,0}};recalculateCurve(c);
        s.curves={c};doc.sketches.append(s);
    }
    ExtrusionObject loft;loft.name="Loft controllata";loft.feature=BodyFeature::Loft;loft.loftSurface=true;loft.featureId=601;
    for(int i=0;i<3;++i){GeometryRef r;r.kind=7;r.index=i;r.element={-1,-1,-1};loft.loftSections.append(r);}
    QString error;loft.forgeBody=forgeReferencedLoft(loft,0,doc.sketches,{},&error);
    if(!loft.forgeBody)throw std::runtime_error(error.toStdString());
    doc.extrusions={loft};normalizeModelHistory(doc);
    CadViewport view;view.resize(1050,780);view.loadDocument(doc);
    if(native){view.show();QElapsedTimer timer;timer.start();while(timer.elapsed()<500)QApplication::processEvents();}
    LoftCorrespondenceEditor editor(&view,0,nullptr);
    editor.synchronize(loft,{11,22,33});
    auto *enable=editor.findChild<QCheckBox*>("loftCorrespondenceEnabled");
    auto *section=editor.findChild<QComboBox*>("loftCorrespondenceSection");
    auto *connector=editor.findChild<QComboBox*>("loftCorrespondenceConnector");
    auto *position=editor.findChild<QDoubleSpinBox*>("loftCorrespondencePosition");
    auto *add=editor.findChild<QPushButton*>("loftCorrespondenceAdd");
    auto *reverse=editor.findChild<QPushButton*>("loftCorrespondenceReverse");
    enable->setChecked(true);check(section->count()==3&&connector->count()==1,"controlli iniziali per tre sezioni chiuse");
    auto edited=loft;editor.copyTo(edited,{11,22,33});
    const double original=edited.loftCorrespondences[1].positions[0];
    section->setCurrentIndex(1);position->setValue(25.0);
    add->click();check(connector->count()==2,"aggiunta di una colonna per tutte le sezioni");
    editor.copyTo(edited,{11,22,33});
    check(std::abs(edited.loftCorrespondences[1].positions[0]-.25)<1e-9,"punto manuale sulla seconda sezione");
    check(edited.loftCorrespondences[0].positions.size()==2&&edited.loftCorrespondences[2].positions.size()==2,"collegamenti completi");
    check(CadViewport::previewKey(edited,0)!=CadViewport::previewKey(loft,0),"anteprima invalida dopo cambio corrispondenza");
    auto reversed=edited;reverse->click();editor.copyTo(reversed,{11,22,33});
    check(reversed.loftCorrespondences[1].reverse,"verso invertito della sola sezione selezionata");reverse->click();
    auto reordered=edited;std::swap(reordered.loftSections[0],reordered.loftSections[2]);
    editor.synchronize(reordered,{33,22,11});editor.copyTo(reordered,{33,22,11});
    check(reordered.loftCorrespondences[1].positions==edited.loftCorrespondences[1].positions
        &&reordered.loftCorrespondences[0].positions==edited.loftCorrespondences[2].positions,"riordino mantiene i dati sulla sezione giusta");
    editor.synchronize(edited,{11,22,33});
    // Una modifica numerica e una costruzione kernel usano gli stessi profili esatti.
    const auto shape=forgeReferencedLoft(edited,0,view.sketches(),{},&error);
    if(!shape)throw std::runtime_error(error.toStdString());
    check(checkBody(*shape).empty(),"loft con controlli produce un B-rep valido");
    const auto applyError=view.updateBody(0,edited);
    if(!applyError.isEmpty())throw std::runtime_error(applyError.toStdString());
    view.undo();check(view.extrusions()[0].loftCorrespondences.isEmpty(),"Undo ripristina allineamento automatico");
    view.redo();check(view.extrusions()[0].loftCorrespondences[1].positions==edited.loftCorrespondences[1].positions,"Redo ripristina controlli");
    QByteArray bytes;DocumentState loaded;
    check(encodeDocument(view.documentState(),bytes,false).isEmpty()&&decodeDocument(bytes,loaded).isEmpty(),"codec64 corrispondenze");
    check(loaded.extrusions[0].loftCorrespondences[1].positions==edited.loftCorrespondences[1].positions,"posizioni double conservate");
    check(!encodeDocumentDefinition63ForHash(view.documentState(),bytes).isEmpty(),"hash63 non perde silenziosamente controlli64");
    auto invalid=view.documentState();invalid.extrusions[0].loftCorrespondences[1].positions[0]=std::numeric_limits<double>::quiet_NaN();
    check(!encodeDocument(invalid,bytes,false).isEmpty(),"salvataggio rifiuta controlli non finiti");
    check(encodeDocumentDefinition63ForHash(doc,bytes).isEmpty()&&decodeDocument(bytes,loaded).isEmpty()
          &&loaded.extrusions[0].loftCorrespondences.isEmpty(),"documenti63 conservano allineamento automatico");
    // Picking: parte da un punto proiettato e termina sulla curva, senza usare XYZ della mesh.
    const auto sections=resolveLoftSections(edited,0,view.sketches(),{});
    section->setCurrentIndex(1);connector->setCurrentIndex(0);
    const auto pixel=[&](double fraction){const auto p=loftSectionPoint(sections[1],fraction);return view.loftControlScreen(QVector3D(p.x(),p.y(),p.z()));};
    const QPointF from=pixel(.25),to=pixel(.32);
    QMouseEvent press(QEvent::MouseButtonPress,from,from,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&view,&press);
    QMouseEvent move(QEvent::MouseMove,to,to,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&view,&move);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);
    editor.copyTo(edited,{11,22,33});check(std::abs(edited.loftCorrespondences[1].positions[0]-.25)<1e-8,"Esc annulla solo il trascinamento");
    QApplication::sendEvent(&view,&press);QApplication::sendEvent(&view,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,to,to,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&release);
    editor.copyTo(edited,{11,22,33});
    check(std::abs(edited.loftCorrespondences[1].positions[0]-.32)<1e-4,"trascinamento vincolato alla sezione esatta");
    if(native){QElapsedTimer timer;timer.start();while(timer.elapsed()<200)QApplication::processEvents();
        check(view.grab().save("/tmp/forgecad-loft-correspondence-native.png"),"immagine dei collegamenti nel viewport");}
    editor.findChild<QPushButton*>("loftCorrespondenceReset")->click();editor.copyTo(edited,{11,22,33});
    check(edited.loftCorrespondences.isEmpty(),"ripristino automatico elimina i controlli manuali");editor.stop();
    auto united=view.extrusions()[0];united.loftMergeTangentFaces=true;
    check(CadViewport::previewKey(united,0)!=CadViewport::previewKey(view.extrusions()[0],0),"opzione unione invalida preview");
    check(view.updateBody(0,united).isEmpty(),"unione facce applicata");
    view.undo();check(!view.extrusions()[0].loftMergeTangentFaces,"Undo unione facce");
    view.redo();check(view.extrusions()[0].loftMergeTangentFaces,"Redo unione facce");
    check(encodeDocument(view.documentState(),bytes,false).isEmpty()&&decodeDocument(bytes,loaded).isEmpty()
          &&loaded.extrusions[0].loftMergeTangentFaces,"codec65 conserva unione facce");
    check(!encodeDocumentDefinition64ForHash(view.documentState(),bytes).isEmpty(),"codec64 non perde opzione65");
    check(encodeDocumentDefinition64ForHash(doc,bytes).isEmpty()&&decodeDocument(bytes,loaded).isEmpty()
          &&!loaded.extrusions[0].loftMergeTangentFaces,"legacy64 conserva separazione facce");
    // Riapertura e Annulla del vero pannello non modificano la feature.
    QMainWindow window;std::exception_ptr failure;
    if(native){window.resize(1450,1000);window.setCentralWidget(&view);window.show();view.fitAll();}
    const auto saved=view.extrusions()[0];
    QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog*>("loftDialog");try{
        check(dialog,"pannello loft aperto");
        auto *merge=dialog->findChild<QCheckBox*>("loftMergeTangentFaces");
        check(merge&&merge->isChecked(),"pannello ripristina unione facce");merge->setChecked(false);
        auto *toggle=dialog->findChild<QCheckBox*>("loftCorrespondenceEnabled");
        check(toggle&&toggle->isChecked(),"riapertura conserva controlli attivi");
        dialog->findChild<QDoubleSpinBox*>("loftCorrespondencePosition")->setValue(12.0);
        if(native){QElapsedTimer timer;timer.start();while(timer.elapsed()<500)QApplication::processEvents();
            check(window.grab().save("/tmp/forgecad-loft-correspondence-panel.png"),"pannello con controlli e overlay");}
    }catch(...){failure=std::current_exception();}if(dialog)dialog->reject();});
    loftDialog(&window,&view,QStringLiteral("Corrispondenze"),0,saved,[](const ExtrusionObject&){return QString();});
    if(native){window.takeCentralWidget();view.setParent(nullptr);}
    if(failure)std::rethrow_exception(failure);
    check(view.extrusions()[0].loftCorrespondences[1].positions==saved.loftCorrespondences[1].positions,"Annulla non modifica il documento");
    check(view.extrusions()[0].loftMergeTangentFaces,"Annulla conserva unione facce precedente");
    std::cout<<"PASS corrispondenze: kernel, UI, riordino, codec, Undo/Redo, drag, annulla"<<std::endl;
    (void)original;
}
