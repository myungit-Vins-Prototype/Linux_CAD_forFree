static void storyboardPanelSelection() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    QMainWindow window;
    auto *view=new CadViewport(&window);window.setCentralWidget(view);
    auto *dock=new QDockWidget(&window);auto *tree=new StoryboardTree(dock);dock->setWidget(tree);
    window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto *other=new QDockWidget(&window);other->setWidget(new QLabel("Altro"));window.addDockWidget(Qt::RightDockWidgetArea,other);
    installStoryboardSelection(&window,view,tree);
    window.resize(1100,760);window.show();
    SketchObject sketch;sketch.name="Profilo nascosto";sketch.visible=false;sketch.storyId=101;
    sketch.segments={{{0,0},{10,0}},{{10,0},{10,5}}};view->sketches_.append(sketch);
    ExtrusionObject body;body.feature=BodyFeature::Imported;body.featureId=102;body.modelBodyId=1;body.name="Cubo";
    body.forgeBody=std::make_shared<Body>(makeBox(Frame3(),10,10,10));body.visible=true;body.solid=true;
    view->extrusions_.append(body);
    const auto row=[&](const QString &name,int type,int index) {
        auto *item=new QTreeWidgetItem(tree,{name});item->setData(0,Qt::UserRole,type);item->setData(0,Qt::UserRole+1,index);
        item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);item->setCheckState(0,Qt::Unchecked);return item;
    };
    auto *sketchRow=row(sketch.name,kTreeSketch,0);auto *bodyRow=row(body.name,kTreeExtrusion,0);
    int changed=0,clicked=0,edited=0;
    QObject::connect(tree,&QTreeWidget::itemChanged,&window,[&]{++changed;});
    QObject::connect(tree,&QTreeWidget::itemClicked,&window,[&]{++clicked;});
    QObject::connect(tree,&QTreeWidget::itemDoubleClicked,&window,[&]{++edited;});
    const auto pump=[] {for(int i=0;i<4;++i)QApplication::processEvents();};pump();
    const auto click=[&](QTreeWidgetItem *item,bool check=false) {
        tree->scrollToItem(item);pump();const auto rect=tree->visualItemRect(item);
        const QPointF point=check?QPointF(rect.left()+8,rect.center().y()):QPointF(rect.center());
        QMouseEvent press(QEvent::MouseButtonPress,point,tree->viewport()->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(),&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,tree->viewport()->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(),&release);pump();
    };
    FunctionDialogPanel panel(view,false);panel.createScrollableForm()->addRow(new QLabel("Selezione",&panel));
    GeometryRef chosen;int picked=0;
    view->setReferencePickCallback([&](bool ok,GeometryRef ref){if(ok){chosen=ref;++picked;}});
    {
        WindowLock lock(&window,view,&panel);
        require(dock->isEnabled()&&!other->isEnabled(),"solo storyboard resta disponibile durante il pannello");
        require(view->beginReferencePick(DatumRoleCurve,-1).isEmpty(),"picker curve avviato");
        click(sketchRow);
        require(picked==1&&chosen.kind==7&&chosen.index==0&&chosen.element.kind==-1,"clic singolo sceglie schizzo intero nascosto");
        require(!view->sketches_[0].visible&&changed==0&&clicked==0&&edited==0,"scelta non cambia visibilita o apre modifica");
        std::vector<PathSegment> path;QString reason;
        require(geometryRefPath(chosen,-1,view->sketches_,view->extrusions_,path,&reason)&&path.size()==2,"schizzo intero consumabile dai picker di curve");
        view->beginReferencePick(DatumRolePoint,-1);click(sketchRow);
        require(picked==1,"campo punto non riceve schizzo intero");
        QTreeWidgetItem *pointRow=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->text(0).startsWith("Punto di")){pointRow=*it;break;}
        require(pointRow,"entita compatibili dello schizzo espanse");click(pointRow);
        require(picked==2&&chosen.kind==6,"punto di schizzo dalla storyboard");
        view->beginReferencePick(DatumRoleCurve,-1);click(sketchRow,true);
        require(picked==3&&sketchRow->checkState(0)==Qt::Unchecked&&changed==0,"clic sulla spunta seleziona senza cambiare visibilita");
        view->beginReferencePick(DatumRoleCurve,-1);click(bodyRow);
        QTreeWidgetItem *edgeRow=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->text(0).startsWith("Spigolo E")){edgeRow=*it;break;}
        require(edgeRow,"bordi B-rep disponibili sotto il corpo");click(edgeRow);
        require(picked==4&&chosen.kind==4&&geometryRefPath(chosen,-1,view->sketches_,view->extrusions_,path,&reason),"bordo esatto passato al picker");
        view->beginReferencePick(DatumRoleCurve,0);require(view->storyboardReferences(SceneObjectKind::Extrusion,0).isEmpty(),"corpo futuro escluso");
        view->cancelReferencePick();
        QKeyEvent rename(QEvent::KeyPress,Qt::Key_F2,Qt::NoModifier);QApplication::sendEvent(tree,&rename);
        require(edited==0&&changed==0,"modifica impedita durante pannello anche senza picker attivo");
    }
    pump();require(dock->isEnabled()&&other->isEnabled()&&!view->interactionLocked(),"stato normale ripristinato alla chiusura");
    view->setReferencePickCallback({});
    require(view->startEdgePick(false,false,false,true).isEmpty(),"picker raccordo avviato");
    const auto edges=view->storyboardReferences(SceneObjectKind::Extrusion,0);
    GeometryRef edge;for(const auto &ref:edges)if(ref.kind==4){edge=ref;break;}
    require(edge.kind==4&&view->pickStoryboardReference(edge).isEmpty()&&!view->edgePickPoints().isEmpty(),"bordo storyboard nel picker raccordo");
    require(view->pickStoryboardReference(edge).isEmpty()&&view->edgePickPoints().isEmpty(),"secondo clic toglie bordo raccordo");
    GeometryRef face;for(const auto &ref:edges)if(ref.kind==5){face=ref;break;}
    require(face.kind==5&&view->pickStoryboardReference(face).isEmpty()&&!view->edgePickPoints().isEmpty()
        &&view->edgePickPoints().first().role==kEdgePointFaceBoundary,"faccia conserva il ruolo genealogico di selezione del suo bordo");
    require(view->pickStoryboardReference(face).isEmpty()&&view->edgePickPoints().isEmpty(),"secondo clic rimuove bordo della faccia");
    view->endEdgePick();
    int pattern=-1;tree->setSourcePickCallback([&](int index,bool){pattern=index;});click(bodyRow);
    require(pattern==0,"selezione sorgente ripetizione resta funzionante");tree->setSourcePickCallback({});
    pump();click(sketchRow);
    require(clicked>0,"clic normale ripristinato dopo picker");
    std::cout<<"PASS storyboard panel selection\n";
}

static void loftFailureDiagnostics() {
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    QVector<SketchObject> sketches;
    const auto profile=[](QString name,double z) {
        SketchObject s;s.name=name;s.customFrame=true;s.frame.origin[2]=z;s.segments={{{0,0},{10,0}}};return s;
    };
    sketches.append(profile("Sezione base",0));sketches.append(profile("Sezione finale",10));
    QVector<ExtrusionObject> bodies;
    for(int i=0;i<2;++i){ExtrusionObject b;b.feature=BodyFeature::ExtractedCurve;b.name=i?"Guida lontana":"Guida corretta";b.featureId=i+10;
        // Percorso 3D limitato, come nelle estrazioni reali.
        b.curve=std::make_shared<BSplineCurve<3>>(1,std::vector<double>{0,0,1,1},std::vector<Vec3>{Vec3(i?20:0,0,0),Vec3(i?20:0,0,10)});b.curves={b.curve};bodies.append(b);}
    ExtrusionObject d;d.feature=BodyFeature::Loft;d.loftSurface=true;
    for(int i=0;i<2;++i){GeometryRef ref;ref.kind=7;ref.index=i;ref.element={-1,-1,-1};d.loftSections.append(ref);}
    for(int i=0;i<2;++i){GeometryRef ref;ref.kind=9;ref.index=i;ref.featureId=bodies[i].featureId;d.loftEdgeGuides.append(ref);}
    d.loftGuideOrder={1,0};
    QString error;require(!forgeReferencedLoft(d,-1,sketches,bodies,&error),"guida distante fallisce realmente nel kernel");
    const auto failure=loftFailure(error);
    require(failure.guide==0&&failure.section==0&&error.contains("Guida lontana")&&error.contains("Sezione base"),"identita guida e sezione rispettano ordine kernel");
    require(!visibleLoftError(error).contains("[[loft-"),"marker interni assenti dal messaggio visibile");
    const auto paths=loftDiagnosticPaths(failure,d,-1,sketches,bodies);
    require(paths.size()==2&&paths[0].guide&&!paths[1].guide&&paths[0].path[0].curve->point(0).x()==20,"diagnostica esatta sulla guida riordinata e sulla sola sezione coinvolta");
    CadViewport view;view.sketches_=sketches;view.extrusions_=bodies;view.preview_.definition=d;
    require(!view.preparePreviewError(error).contains("[[loft-")&&view.previewLoftErrors_.size()==2,"anteprima prepara due overlay");
    require(view.previewLoftErrors_[0].color.red()>view.previewLoftErrors_[0].color.green()
        &&view.previewLoftErrors_[0].color!=view.previewLoftErrors_[1].color,"guida rossa e sezione di colore distinto");
    view.resize(1000,750);view.ensureVisibleBodyDisplays();view.fitAll();view.show();
    for(int i=0;i<4;++i)QApplication::processEvents();
    QImage image(view.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    {QPainter painter(&image);view.drawLoftDiagnostics(painter);}require(!image.isNull(),"etichette e frecce renderizzabili senza cambiare modello");
    int red=0,orange=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
        const auto pixel=image.pixelColor(x,y);
        if(pixel.alpha()>100 && pixel.red()>220 && pixel.green()<100)++red;
        if(pixel.alpha()>100 && pixel.red()>220 && pixel.green()>130 && pixel.green()<210 && pixel.blue()<90)++orange;
    }
    require(red>10&&orange>10,"entrambi gli overlay ed etichette hanno pixel dei colori previsti");
    const auto platform=QGuiApplication::platformName();
    image.save(QStringLiteral("/tmp/forgecad-storyboard-pick/loft-diagnostics-overlay-%1.png").arg(platform));
    if(view.isValid())view.grabFramebuffer().save(QStringLiteral("/tmp/forgecad-storyboard-pick/loft-diagnostics-view-%1.png").arg(platform));
    view.preparePreviewError({});require(view.previewLoftErrors_.isEmpty(),"diagnostica eliminata quando errore risolto");
    view.preparePreviewError(error);view.clearPreview();require(view.previewLoftErrors_.isEmpty(),"diagnostica eliminata alla chiusura");
    std::cout<<"PASS loft failure diagnostics\n";
}

static void mirrorDeleteOnce() {
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    for(bool linked:{false,true})for(bool child:{false,true}) {
        DocumentState initial;ExtrusionObject source;source.name="Base";source.feature=BodyFeature::Primitive;
        source.primitive.kind=PrimitiveKind::Box;source.primitive.origin[0]=3;
        source.primitive.size[0]=source.primitive.size[1]=source.primitive.size[2]=2;initial.extrusions={source};
        CadViewport view;view.loadDocument(initial);
        ExtrusionObject mirror;mirror.feature=BodyFeature::Pattern;mirror.name="Specchio da eliminare";mirror.firstBody=0;mirror.pattern.kind=2;
        GeometryRef plane;plane.kind=1;plane.index=2;mirror.pattern.refs={plane};
        require(view.createBody(mirror,{0}).isEmpty()&&view.extrusions_.size()==3,"fixture specchio con due risultati");
        if(linked) {
            SketchObject sketch;sketch.name="Schizzo collegato";
            GeometryRef vertex;vertex.kind=3;vertex.index=2;vertex.featureId=view.extrusions_[2].featureId;
            vertex.point=vertexReference(*view.extrusions_[2].forgeBody,view.extrusions_[2].forgeBody->vertices().front());
            require(appendLinkedSketchReference(sketch,vertex,{},view.extrusions_).isEmpty(),"riferimento associativo al risultato dello specchio");
            view.sketches_.append(sketch);view.documentChanged();
        }
        const auto before=view.currentDocument();
        view.selectObject(SceneObjectKind::Extrusion,child?2:1);view.deleteSelection();
        require(view.extrusions_.size()==1&&view.extrusions_[0].feature==BodyFeature::Primitive,"un solo Elimina rimuove madre e risultati anche con schizzi collegati");
        require(view.extrusions_[0].error.isEmpty()&&view.isBodyDrawn(0),"base ripristinata senza specchio orfano arancione");
        if(linked)require(!view.sketches_.last().linkedReferences.first()->error.isEmpty(),"schizzo conserva errore esplicito del riferimento rimosso");
        view.undo();require(view.extrusions_.size()==before.extrusions.size()&&view.extrusions_[1].featureId==before.extrusions[1].featureId,"Undo unico ripristina gruppo con identita");
        view.redo();require(view.extrusions_.size()==1,"Redo elimina gruppo in una sola operazione");
    }
    std::cout<<"PASS mirror deletion once with linked sketches\n";
}
