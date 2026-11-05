static void loftPointsAndPlanes() {
    using namespace ForgeCad;using namespace Kernel;
    DocumentState initial;initial.sketches.resize(2);
    auto &circle=initial.sketches[0];circle.name="Sezione";
    CurveObject ring;ring.tool=DrawingTool::Circle;ring.controlPoints={{0,0},{5,0}};circle.curves={ring};
    auto &point=initial.sketches[1];point.name="Punta";point.plane=3;point.frame.origin[2]=10;
    CurveObject vertex;vertex.tool=DrawingTool::Point;vertex.controlPoints={{0,0}};point.curves={vertex};
    CadViewport view;view.loadDocument(initial);
    GeometryRef section;section.kind=7;section.index=0;section.element={-1,-1,-1};
    GeometryRef tip;tip.kind=6;tip.index=1;tip.element={1,0,0};
    ExtrusionObject definition;definition.feature=BodyFeature::Loft;definition.operation=-1;definition.loftSections={section,tip};
    const auto requireOk=[&](const QString &error){if(!error.isEmpty())throw std::runtime_error(error.toStdString());};
    auto sections=resolveLoftSections(definition,-1,view.sketches(),view.extrusions());
    require(sections.size()==2 && sections[1].point && distance(*sections[1].point,Vec3(0,0,10))<1e-12,"punto esatto dalla curva dello schizzo");
    GeometryRef entire=tip;entire.kind=7;entire.element={-1,-1,-1};
    definition.loftSections[1]=entire;
    require(resolveLoftSections(definition,-1,view.sketches(),view.extrusions())[1].point.has_value(),"schizzo con un solo punto come sezione");
    definition.loftSections[1]=tip;
    ExtrusionObject datum;datum.feature=BodyFeature::DatumPlane;GeometryRef xy;xy.kind=1;xy.index=0;datum.datum.refs={xy};datum.datum.distance=10;
    requireOk(view.createBody(datum));
    GeometryRef plane;plane.kind=8;plane.index=0;plane.featureId=view.extrusions_[0].featureId;
    definition.loftEndContinuity=4;definition.loftEndFaces={plane};
    requireOk(view.createBody(definition));
    const auto good=[&]{require(view.extrusions_.size()==2 && view.extrusions_.last().error.isEmpty() && view.extrusions_.last().forgeBody,"loft con punta e piano disponibile");};good();
    require(bodyOperands(view.extrusions_.last()).contains(0) && sketchesOf(view.extrusions_.last()).contains(1),"dipendenze punto e piano");
    const auto saved=view.currentDocument();QTemporaryDir dir;
    requireOk(saveDocumentFile(dir.filePath("punti.prt"),saved,false));DocumentState loaded;
    requireOk(loadDocumentFile(dir.filePath("punti.prt"),loaded));
    require(loaded.extrusions.last().loftEndContinuity==4 && loaded.extrusions.last().loftSections[1].kind==6,"codec 66 conserva punto e piano");
    view.loadDocument(loaded);good();
    auto moved=view.extrusions_[0];moved.datum.distance=12;
    requireOk(view.updateBody(0,moved));
    require(!view.extrusions_.last().error.isEmpty(),"piano spostato fuori sezione segnala incompatibilita");
    view.undo();good();view.redo();require(!view.extrusions_.last().error.isEmpty(),"Redo conserva diagnosi");view.undo();good();
    view.recordUndo();view.sketches_[1].curves[0].controlPoints[0]={1,0};view.regenerateDependents(1);
    good();sections=resolveLoftSections(view.extrusions_.last(),1,view.sketches(),view.extrusions());
    require(distance(*sections[1].point,Vec3(1,0,10))<1e-12,"loft segue la modifica del punto");view.undo();good();
    // Stessi controlli della UI, con selezione di un piano principale e datum.
    ExtrusionObject controlsDefinition;
    QDialog dialog;dialog.setObjectName("loftDialog");auto *form=new QFormLayout(&dialog);
    {
        SurfaceEndControls controls(dialog,*form,&view,-1,controlsDefinition);
        auto *mode=dialog.findChild<QComboBox *>("surfaceEndContinuity");require(mode && mode->count()==5,"modo G1 piano disponibile");
        mode->setCurrentIndex(4);auto *pick=dialog.findChild<QPushButton *>("surfaceEndFacesPick");pick->click();
        require(view.referencePicking() && view.refPickRoles_==DatumRolePlane,"picker piano attivo");
        view.refPickFinished_(true,plane);require(controlsDefinition.loftEndFaces.size()==1,"piano selezionato");
        view.refPickFinished_(true,xy);require(controlsDefinition.loftEndFaces.size()==1 && controlsDefinition.loftEndFaces[0].kind==1,"sostituzione con piano principale");
        view.refPickFinished_(true,xy);require(controlsDefinition.loftEndFaces.isEmpty(),"secondo clic rimuove il piano");
    }
    view.setReferencePickCallback([](bool,GeometryRef){});
    view.beginReferencePick(DatumRolePoint|DatumRoleCurve,1);
    const auto candidates=view.storyboardReferences(SceneObjectKind::Sketch,1);
    bool found=false;for(const auto &ref:candidates)if(ref.kind==6 && ref.element==tip.element)found=true;
    require(found,"punto selezionabile dalla storyboard");view.cancelReferencePick();view.setReferencePickCallback({});
    QByteArray previous;auto legacy=saved;legacy.extrusions.last().loftEndContinuity=0;legacy.extrusions.last().loftEndFaces.clear();
    requireOk(encodeDocumentDefinition65ForHash(legacy,previous));DocumentState legacyRead;requireOk(decodeDocument(previous,legacyRead));
    QByteArray refused;require(!encodeDocumentDefinition65ForHash(saved,refused).isEmpty(),"piano G1 non retrocesso silenziosamente al formato 65");
    {
        QMainWindow window;auto *viewport=new CadViewport(&window);window.setCentralWidget(viewport);
        viewport->loadDocument(saved);window.resize(1100,800);window.show();viewport->fitAll();
        QString uiError;bool exercised=false;int ticks=0;
        QTimer timer;timer.setInterval(100);
        QObject::connect(&timer,&QTimer::timeout,&window,[&] {
            auto *panel=window.findChild<QDialog *>("loftDialog");
            if(!panel || !panel->isVisible())return;
            if(++ticks>50){uiError="timeout pannello loft";panel->reject();return;}
            if(exercised)return;
            exercised=true;
            try {
                panel->findChild<QPushButton *>("loftPickSections")->click();
                require((viewport->refPickRoles_&DatumRolePoint)!=0,"pannello accetta punti");
                viewport->refPickFinished_(true,tip);viewport->refPickFinished_(true,tip);
                auto *end=panel->findChild<QComboBox *>("surfaceEndContinuity");end->setCurrentIndex(4);
                panel->findChild<QPushButton *>("surfaceEndFacesPick")->click();
                viewport->refPickFinished_(true,plane);viewport->refPickFinished_(true,plane);
                panel->findChild<QCheckBox *>("loftCorrespondenceEnabled")->setChecked(true);
                auto *sectionBox=panel->findChild<QComboBox *>("loftCorrespondenceSection");
                require(sectionBox->count()==2,"corrispondenze disponibili con punto");sectionBox->setCurrentIndex(1);
                require(!panel->findChild<QDoubleSpinBox *>("loftCorrespondencePosition")->isEnabled(),"punto non trascinabile lungo un profilo inesistente");
            }catch(const std::exception &e){uiError=QString::fromUtf8(e.what());}
            QTimer::singleShot(300,panel,[&,panel]{
                const auto path=qEnvironmentVariable("FORGECAD_LOFT_POINT_QA_IMAGE");
                if(!path.isEmpty())window.grab().save(path);
                panel->reject();
            });
        });timer.start();
        loftDialog(&window,viewport,"Loft punto/piano",1,saved.extrusions.last(),[](const auto &){return QString();});
        timer.stop();require(exercised,"pannello loft esercitato");requireOk(uiError);
    }
    std::cout<<"PASS punti e piani loft: associazione, rigenerazione, Undo/Redo, codec, picker e storyboard"<<std::endl;
}
