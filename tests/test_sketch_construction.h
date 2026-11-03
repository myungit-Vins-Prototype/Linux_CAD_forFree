static void sketchConstructionVisibility(bool gl) {
    using namespace ForgeCad; using namespace Kernel;
    SketchObject sketch; sketch.name="Profilo con costruzione"; sketch.storyId=9901;
    sketch.segments={{{-8,-4},{8,-4}},{{-8,6},{8,6}}};sketch.constructionSegments={1};
    CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{0,12},{3,12}};circle.construction=true;recalculateCurve(circle);sketch.curves={circle};
    CadViewport view;view.resize(800,650);view.sketches_={sketch};view.setGridVisible(false);view.setAxesVisible(false);for(int i=0;i<3;++i)view.setPlaneVisible(i,false);
    view.camera_.setFrame({1,0,0},{0,0,1});view.camera_.zoom_=30;
    const auto linePixel=[&]{return view.projectWorldPoint(view.mapSketchPoint({0,6},sketch)).toPoint();};
    require(view.sketchGeometryPoints(view.sketches_[0]).size()==2,"costruzione esclusa dai limiti fuori modifica");
    require(view.sketchScreenPolylines(view.sketches_[0]).size()==1,"costruzione esclusa da selezione rettangolare");
    require(view.pickSceneObject(linePixel(),false).kind!=SceneObjectKind::Sketch,"clic sulla sola costruzione non seleziona lo schizzo");
    view.setReferencePickCallback([](bool,GeometryRef){});
    require(view.beginReferencePick(DatumRoleCurve,-1).isEmpty(),"picker curve attivo");GeometryRef picked;
    require(!view.pickReference(linePixel(),picked),"costruzione non candidata ai picker curve");
    const auto candidates=view.storyboardReferences(SceneObjectKind::Sketch,0);
    require(!candidates.isEmpty(), "candidati storyboard verificati con picker effettivamente collegato");
    for(const auto &r:candidates)require(r.element.kind==-1||r.element!=ConstraintRef{0,1,-1},"storyboard esclude costruzione");
    view.cancelReferencePick();
    QImage inactive,normal,active,shown;
    if(gl){view.show();QElapsedTimer elapsed,stable;elapsed.start();stable.start();QSize previous=view.size();
        do{QApplication::processEvents();QThread::msleep(2);if(view.size()!=previous){previous=view.size();stable.restart();}}while(elapsed.elapsed()<2000&&(elapsed.elapsed()<150||stable.elapsed()<100));
        require(stable.elapsed()>=100&&view.isValid(),"GL e dimensioni native stabili");inactive=view.grabFramebuffer();auto saved=view.sketches_[0];view.sketches_[0].segments.removeLast();view.sketches_[0].curves.clear();view.sketches_[0].constructionSegments.clear();view.update();QApplication::processEvents();normal=view.grabFramebuffer();require(inactive==normal,"render fuori modifica identico senza costruzione");view.sketches_[0]=saved;}
    const auto documentBefore = view.currentDocument();
    int documentChanges = 0;view.setDocumentChangedCallback([&]{++documentChanges;});
    require(!view.sketchConstructionVisible(), "costruzione fuori modifica nascosta per default");
    view.updateSceneBounds();
    view.setSketchConstructionVisible(true);
    require(view.sketchConstructionVisible()&&view.sceneBoundsDirty_, "opzione aggiorna la geometria da inquadrare");
    require(view.sketchGeometryPoints(view.sketches_[0]).size()>2&&view.sketchScreenPolylines(view.sketches_[0]).size()==3, "opzione mostra linee e curve fuori modifica");
    require(view.pickSceneObject(linePixel(),false).kind==SceneObjectKind::Sketch, "costruzione visibile selezionabile");
    require(view.beginReferencePick(DatumRoleCurve,-1).isEmpty()&&view.pickReference(linePixel(),picked)&&picked.kind==7&&picked.element==ConstraintRef{0,1,-1}, "costruzione visibile utilizzabile come riferimento");
    bool constructionCandidate=false;for(const auto &r:view.storyboardReferences(SceneObjectKind::Sketch,0))if(r.element==ConstraintRef{0,1,-1})constructionCandidate=true;
    require(constructionCandidate, "storyboard coerente con l'opzione visiva");view.cancelReferencePick();
    auto hidden=std::make_shared<SketchLinkedReference>();hidden->entities={{0,1,-1}};view.sketches_[0].linkedReferences={hidden};
    require(!view.isSketchEntityDisplayed(view.sketches_[0],0,1), "cache automatiche nascoste anche con l'opzione attiva");view.sketches_[0].linkedReferences.clear();
    if(gl){view.update();QApplication::processEvents();shown=view.grabFramebuffer();require(shown!=inactive,"render costruzione visibile fuori modifica");}
    view.setSketchConstructionVisible(false);
    require(view.sketchGeometryPoints(view.sketches_[0]).size()==2&&view.sketchScreenPolylines(view.sketches_[0]).size()==1, "disattivazione nasconde subito costruzione e picking");
    require(!view.canUndo()&&documentChanges==0, "opzione senza Undo o documento modificato");
    QByteArray beforeBytes,afterBytes;require(encodeDocument(documentBefore,beforeBytes,false).isEmpty()&&encodeDocument(view.currentDocument(),afterBytes,false).isEmpty()&&beforeBytes==afterBytes, "opzione non modifica il documento salvato");
    view.activeSketch_=0;view.sketchMode_=true;
    require(view.sketchGeometryPoints(view.sketches_[0]).size()>2&&view.sketchScreenPolylines(view.sketches_[0]).size()==3,"costruzione visibile durante modifica");
    if(gl){view.update();QApplication::processEvents();active=view.grabFramebuffer();require(active!=inactive,"render costruzione durante modifica");}
    view.sketchMode_=false;
    view.setSketchConstructionVisible(true);
    auto last=sketch;last.storyId=9902;last.customFrame=true;last.frame.origin[2]=10;
    QVector<SketchObject> sections{sketch,last};
    const auto check=[&](ExtrusionObject loft){QString error;auto body=forgeReferencedLoft(loft,-1,sections,{},&error);require(body&&checkBody(*body).empty(),qPrintable(error));for(auto v:body->vertices())require(std::abs(body->vertex(v).point.y()+4)<1e-8,"loft non usa costruzione");};
    ExtrusionObject loft;loft.feature=BodyFeature::Loft;loft.loftSurface=true;loft.loftSketches={0,1};check(loft);
    loft.loftSketches.clear();for(int i:{0,1}){GeometryRef r;r.kind=7;r.index=i;r.element={-1,-1,-1};loft.loftSections.append(r);}check(loft);
    for(ConstraintRef entity:QVector<ConstraintRef>{{0,1,-1},{1,0,-1}}){auto invalid=loft;invalid.loftSections[0].element=entity;QString error;require(!forgeReferencedLoft(invalid,-1,sections,{},&error)&&error.contains("costruzione")&&loftFailure(error).section==0,"entita di costruzione rifiutata come sezione nominata");}
    if(gl){const auto path=QStringLiteral("/tmp/forgecad-construction-option/construction-")+QGuiApplication::platformName();inactive.save(path+"-outside-hidden.png");shown.save(path+"-outside-shown.png");active.save(path+"-editing.png");}
    const auto setting=QStringLiteral("view/sketchConstruction");const auto previous=QSettings().value(setting);QSettings().remove(setting);
    {
        PdfWindow window;auto *action=window.findChild<QAction*>("sketchConstructionVisibilityAction");
        require(action&&action->isCheckable()&&!action->isChecked()&&!window.viewport_->sketchConstructionVisible(), "opzione presente nel menu e spenta per default");
        const bool dirty=window.documentModified_;action->trigger();
        require(action->isChecked()&&window.viewport_->sketchConstructionVisible()&&QSettings().value(setting).toBool(), "comando menu aggiorna vista e preferenza");
        require(window.documentModified_==dirty&&!window.viewport_->canUndo(), "comando menu non sporca il documento");
        PdfWindow reopened;auto *saved=reopened.findChild<QAction*>("sketchConstructionVisibilityAction");
        require(saved&&saved->isChecked()&&reopened.viewport_->sketchConstructionVisible(), "nuova finestra ripristina preferenza attiva");
        saved->trigger();require(!reopened.viewport_->sketchConstructionVisible()&&!QSettings().value(setting).toBool(), "comando menu salva disattivazione");
        PdfWindow hiddenAgain;require(!hiddenAgain.viewport_->sketchConstructionVisible(), "nuova finestra ripristina preferenza disattivata");
    }
    if(previous.isValid())QSettings().setValue(setting,previous);else QSettings().remove(setting);
    std::cout<<"PASS costruzione: menu persistente, on/off, rendering, bounds, picking, storyboard, cache nascoste, documento e loft invariati"<<std::endl;
}

static void sketchReferenceUsage() {
    using namespace ForgeCad;
    DocumentState doc;SketchObject source;source.storyId=9911;source.name="Sorgente";source.segments={{{20,5},{40,5}},{{30,8},{40,8}},{{30,12},{40,12}},{{30,16},{40,16}}};
    SketchObject target;target.storyId=9912;target.storyOrder=1;target.name="Destinazione";doc.sketches={source,target};
    const auto append=[&](int entity,bool visible){GeometryRef r;r.kind=7;r.index=0;r.featureId=9911;r.element={0,entity,-1};require(appendLinkedSketchReference(doc.sketches[1],r,doc.sketches,{},false,true,100,nullptr,visible).isEmpty(),"fixture cache");};
    append(0,false);doc.sketches[1].segments.append({{50,0},{50,10}});append(1,false);append(2,true);append(3,false);
    SketchConstraint c;c.type=ConstraintType::Coincident;c.first={0,1,0};c.second={0,2,0};doc.sketches[1].geometricConstraints={c};
    ExtrusionObject consumer;GeometryRef ref;ref.kind=7;ref.index=1;ref.featureId=9912;ref.element={0,4,-1};consumer.loftSections={ref};doc.extrusions={consumer};const auto before=doc;
    auto maps=pruneUnusedSketchReferences(1,doc.sketches,doc.extrusions);
    require(maps.changed()&&doc.sketches[1].linkedReferences.size()==3,"elimina solo orfano intercalato");
    require(doc.sketches[1].geometricConstraints.size()==1&&doc.sketches[1].geometricConstraints[0].first.element==0&&doc.sketches[1].geometricConstraints[0].second.element==1,"vincolo conservato e rimappato");
    require(doc.extrusions[0].loftSections[0].element.element==3&&before.extrusions[0].loftSections[0].element.element==4&&before.sketches[1].segments.size()==5,"consumatore rimappato e snapshot immutabile");
    auto released=doc;released.sketches[1].geometricConstraints.clear();pruneUnusedSketchReferences(released);require(released.sketches[1].linkedReferences.size()==2,"ultimo vincolo rimosso, conversione e uso a valle conservati");
    for(bool cache:{false,true}){QByteArray bytes;require(encodeDocument(doc,bytes,cache).isEmpty(),"salvataggio cache usate");DocumentState read;require(decodeDocument(bytes,read).isEmpty()&&read.sketches[1].linkedReferences.size()==3,"riapertura cache usate");}
    // I riferimenti nei dati complessi Offset/Pattern contano come consumatori.
    for(auto type:{ConstraintType::Offset,ConstraintType::Pattern}){auto nested=before;nested.extrusions.clear();nested.sketches[1].geometricConstraints.clear();SketchConstraint use;use.type=type;if(type==ConstraintType::Offset){use.offset.sources={{0,0,-1}};use.offset.copies={{0,1,-1}};}else{use.pattern.sources={{0,1,-1}};use.pattern.copies={{0,1,-1}};use.pattern.direction={0,0,-1};}nested.sketches[1].geometricConstraints={use};pruneUnusedSketchReferences(nested);require(nested.sketches[1].geometricConstraints.size()==1&&nested.sketches[1].linkedReferences.size()==2,"riferimenti annidati conservati");}
    CadViewport view;view.resize(1000,750);view.sketches_={source,target};view.activeSketch_=1;view.sketchMode_=true;view.geometrySnap_=true;view.gridSnap_=view.originSnap_=view.axesVisible_=false;
    for(int i=0;i<3;++i)view.setPlaneVisible(i,false);
    view.camera_.setFrame({1,0,0},{0,0,1});view.camera_.zoom_=50;
    int changes=0;view.setDocumentChangedCallback([&]{++changes;});
    const auto click=[&](QPointF point,Qt::KeyboardModifiers modifiers=Qt::NoModifier){const auto pixel=view.projectWorldPoint(view.mapSketchPoint(point,view.sketches_[1]));QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,modifiers);view.mousePressEvent(&press);QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,modifiers);view.mouseReleaseEvent(&release);};
    const auto escape=[&]{QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);view.keyPressEvent(&key);};
    view.setDrawingTool(DrawingTool::Select);click({20,5},Qt::ControlModifier);
    require(!view.selectedPoints_.isEmpty()&&!view.canUndo()&&changes==0,"clic esterno e selezione senza Undo o documento modificato");
    require(view.currentDocument().sketches[1].linkedReferences.isEmpty(),"selezione transitoria esclusa da persistenza e snapshot");
    escape();require(view.sketches_[1].linkedReferences.isEmpty()&&!view.canUndo()&&changes==0,"Escape rimuove cache transitoria");
    view.setDrawingTool(DrawingTool::Line);click({20,5});click({20,15});
    require(view.sketches_[1].linkedReferences.size()==1&&view.currentDocument().sketches[1].linkedReferences.size()==1&&view.canUndo(),"solo riferimento effettivamente usato dalla linea confermata");
    const auto linked=view.currentDocument();view.setDrawingTool(DrawingTool::Select);view.sketchSelections_={{0,0}};view.deleteSketchElements();
    require(view.sketches_[1].segments.isEmpty()&&view.sketches_[1].linkedReferences.isEmpty(),"eliminazione ultima parte libera cache");
    view.undo();require(view.sketches_[1].linkedReferences.size()==1&&view.sketches_[1].segments.size()==linked.sketches[1].segments.size(),"Undo ripristina parte e collegamento");view.redo();require(view.sketches_[1].segments.isEmpty()&&view.sketches_[1].linkedReferences.isEmpty(),"Redo rimuove parte e collegamento");
    view.undo();view.history_.clear();view.sketchSelections_.clear();view.selectedPoints_.clear();
    view.setDrawingTool(DrawingTool::Dimension);const int used=view.sketches_[1].linkedReferences.size();click({30,8});escape();
    require(view.sketches_[1].linkedReferences.size()==used&&!view.canUndo(),"quota annullata non conserva riferimenti aggiuntivi");
    std::cout<<"PASS uso riferimenti: clic temporanei, nessun falso Undo/dirty, orfani, vincoli, consumatori, Offset/Pattern, codec ed eliminazione Undo/Redo"<<std::endl;
}
