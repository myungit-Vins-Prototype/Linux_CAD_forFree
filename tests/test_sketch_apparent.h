// Contorno apparente associativo, incluso nel friend ViewportInteractionTest.
static void sketchApparent(bool gl) {
    using namespace ForgeCad;
    using namespace Kernel;
    const auto capsule = [](double radius, const Frame3 &frame = Frame3()) {
        ProfileRegion region;
        region.outer.segments = {
            {std::make_shared<Line<2>>(Vec2(-10,-radius),Vec2(1,0)),{0,20}},
            {std::make_shared<Circle<2>>(Vec2(10,0),Vec2(1,0),Vec2(0,1),radius),{-kHalfPi,kHalfPi}},
            {std::make_shared<Line<2>>(Vec2(10,radius),Vec2(-1,0)),{0,20}},
            {std::make_shared<Circle<2>>(Vec2(-10,0),Vec2(1,0),Vec2(0,1),radius),{kHalfPi,3*kHalfPi}}
        };
        return makeExtrusion(frame,region,12);
    };
    const auto checkContours = [](const Body &body, Vec3 direction) {
        int count=0;
        for(auto face:body.faces()) for(const auto &span:faceApparentContours(body,face,direction)) {
            ++count;require(span.curve && span.range.isFinite() && span.range.length()>0,"contorno esatto non degenere");
            for(double f:{.1,.37,.73,.9}) {
                const auto p=span.curve->point(span.range.lo+f*span.range.length());
                const auto projection=projectPoint(*body.face(face).surface,p);
                require(projection.distance<1e-7 && classifyPointOnFace(body,face,p,1e-7)!=PointLocation::Outside,
                        "contorno sulla superficie esatta e dentro il ritaglio B-rep");
                const auto n=body.face(face).surface->normal(projection.u,projection.v);
                require(std::abs(dot(n,normalized(direction)))<1e-10,"normale del contorno ortogonale alla proiezione");
            }
        }
        return count;
    };
    const Body rounded=capsule(4);
    require(!nearestEdge(rounded,{-14,0,6},1e-8).valid(),"il bordo indicato nella foto non e' uno spigolo reale");
    require(checkContours(rounded,{0,-1,0})==2,"capsula: solo i due contorni esterni, nessuna generatrice fuori dalle mezze facce");
    require(checkContours(makeCylinder(Frame3(),4,12),{0,1,.3})==2,"contorni cilindrici anche in proiezione inclinata");
    require(checkContours(makeCone(Frame3(),6,2,12),{0,1,.2})==2,"generatrici esatte del cono inclinato");
    require(checkContours(makeSphere(Frame3(),5),{1,2,3})>0,"contorno sferico esatto");
    require(checkContours(makeTorus(Frame3(),8,2),{0,1,0})>0,"meridiani toroidali esatti nella vista laterale");
    const Frame3 placed({7,-3,2},{1,2,3},{0,1,0});
    require(checkContours(capsule(4,placed),placed.yDir())==2,"contorni invarianti per posizione e orientamento del pezzo");
    for(auto face:rounded.faces()) {
        const auto a=faceApparentContours(rounded,face,{0,1,0}),b=faceApparentContours(rounded,face,{0,-1,0});
        require(a.size()==b.size(),"inversione della normale conserva i rami");
        for(std::size_t i=0;i<a.size();++i)require(a[i].side==b[i].side
            && distance(a[i].curve->point(a[i].range.lo),b[i].curve->point(b[i].range.lo))<1e-10,"identita' del ramo indipendente dal verso della normale");
    }
    CadViewport view;view.resize(1200,850);
    DocumentState document;ExtrusionObject source;source.feature=BodyFeature::Imported;source.name="Parte raggiata";
    source.importData=QByteArray::fromStdString(writeStep({ExchangeBody{"Parte raggiata",rounded}}));
    source.featureId=901;source.forgeBody=std::make_shared<const Body>(rounded);source.cachedGeometry=true;document.extrusions={source};
    view.loadDocument(document);require(view.createSketch(1,"Profilo da riferire")>=0,"schizzo sul piano XZ");
    view.geometrySnap_=true;view.gridSnap_=false;view.originSnap_=true;view.axesVisible_=false;view.referencePlanesVisible_=false;
    view.camera_.zoom_=24;view.setViewNormal(1);view.history_.clear();
    const auto pixel=[&](QPointF p){return view.projectWorldPoint(view.mapActiveSketchPoint(p)).toPoint();};
    const auto move=[&](QPointF p){const auto s=pixel(p);QMouseEvent e(QEvent::MouseMove,QPointF(s),QPointF(s),Qt::NoButton,Qt::NoButton,Qt::NoModifier);view.mouseMoveEvent(&e);};
    const auto click=[&](QPointF p){const auto s=pixel(p);QMouseEvent press(QEvent::MouseButtonPress,QPointF(s),QPointF(s),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);view.mousePressEvent(&press);QMouseEvent release(QEvent::MouseButtonRelease,QPointF(s),QPointF(s),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);view.mouseReleaseEvent(&release);};
    const auto &cache=view.sketchReferenceCache();
    int apparent=0;for(const auto &link:cache.linkedReferences)if(link && isSketchApparentReference(link->source))++apparent;
    require(apparent==2 && view.activeSketchObject()->linkedReferences.isEmpty(),"contorni in cache transitoria, senza modificare lo schizzo");
    view.setDrawingTool(DrawingTool::Polyline);move({-14,6});
    require(view.lastSnapKind_!=SnapKind::None && view.snapLabelAt(view.lastSnapKind_,view.lastSnapPoint_).contains("Contorno apparente"),"badge distingue il bordo apparente da quello reale");
    require(!view.canUndo() && view.activeSketchObject()->linkedReferences.isEmpty(),"hover di disegno non crea riferimenti o Undo");
    click({-14,6});click({-18,9});
    const auto drawn=*view.activeSketchObject();
    require(drawn.linkedReferences.size()==1 && isSketchApparentReference(drawn.linkedReferences.first()->source),"linea agganciata a una sola faccia/ramo apparente");
    require(drawn.geometricConstraints.size()==1,"aggancio unico, senza vincoli doppi");
    require(std::abs(drawn.segments.first().first.x()+14)<1e-9,"aggancio sulla proiezione esatta della parte raggiata");
    view.undo();require(view.activeSketchObject()->segments.isEmpty() && view.activeSketchObject()->linkedReferences.isEmpty(),"Undo rimuove linea e riferimento nello stesso passo");
    view.redo();require(view.activeSketchObject()->linkedReferences.size()==1,"Redo ripristina il contorno vivo");
    view.setDrawingTool(DrawingTool::Dimension);view.sketchSelections_.clear();view.selectedPoints_={{0,0,1}};move({-14,3});
    require(isSketchApparentReference(view.sketchReferenceHoverSource_) && !view.sketchReferenceHoverContact_
        && view.sketchReferenceHoverLabels_.join(' ').contains("Contorno apparente"),"Quota propone il contorno e la faccia sorgente");
    if(gl) {
        view.ensureBodyDisplay(0);view.show();QApplication::processEvents();require(view.isValid(),"OpenGL per il contorno apparente");
        // La prima esposizione puo' precedere la pubblicazione del display.
        for(int i=0;i<3;++i){view.grabFramebuffer();QApplication::processEvents();}
        view.setAttribute(Qt::WA_UnderMouse,true);move({-14,3});const auto frame=view.grabFramebuffer();
        const auto labels=view.sketchReferenceHoverLabels_;view.sketchReferenceHoverLabels_.clear();const auto without=view.grabFramebuffer();view.sketchReferenceHoverLabels_=labels;
        const auto greenPixels=[](const QImage &image){int count=0;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);
                if(c.green()>c.red()+60 && c.green()>c.blue()+20)++count;}return count;};
        require(!frame.isNull() && greenPixels(frame)>greenPixels(without)+30,"testo del badge del contorno effettivamente disegnato");
        const auto qa=qEnvironmentVariable("FORGECAD_APPARENT_QA");
        if(!qa.isEmpty()){QDir().mkpath(qa);require(frame.save(qa+"/contorno-quota.png"),"cattura contorno quotabile");}
    }
    click({-14,3});require(view.constraintSelection().size()==2 && view.dimensionPreviewValid_,"Quota usa il bordo immaginario insieme al punto locale");
    require(view.addConstraint(ConstraintType::Distance,4).isEmpty(),"quota associativa al contorno applicabile");
    require(view.activeSketchObject()->linkedReferences.size()==1,"disegno e quota riusano la stessa sorgente");
    QByteArray bytes;DocumentState restored;
    require(encodeDocument(view.currentDocument(),bytes).isEmpty() && decodeDocument(bytes,restored).isEmpty(),"codec PRT69 conserva faccia e ramo del contorno");
    require(encodeDocumentDefinition66ForHash(view.currentDocument(),bytes).contains("69"),"il codec legacy non confonde il contorno con una sezione");
    CadViewport reopened;reopened.loadDocument(restored);
    require(reopened.sketches_.size()==1 && reopened.sketches_.first().referenceError.isEmpty()
        && isSketchApparentReference(reopened.sketches_.first().linkedReferences.first()->source),"riapertura rigenera il riferimento senza perdere i vincoli");
    auto sketch=*view.activeSketchObject();auto bodies=view.extrusions_;
    const auto old=sketch.linkedReferences.first()->entities.first();const auto oldX=sketch.segments.at(old.element).first.x();
    bodies[0].forgeBody=std::make_shared<const Body>(capsule(6));
    const auto error=refreshLinkedSketchReferences(sketch,view.sketches_,bodies);
    if(!error.isEmpty())std::cerr<<error.toStdString()<<'\n';
    require(error.isEmpty() && std::abs(sketch.segments.at(old.element).first.x()-oldX+2)<1e-8,"il contorno segue il cambiamento del raggio della faccia");
    require(sketch.geometricConstraints.size()==view.activeSketchObject()->geometricConstraints.size(),"refresh conserva quote e vincoli locali");
    auto translated=*view.activeSketchObject();translated.customFrame=true;
    translated.frame.origin[1]=100;translated.frame.normal[0]=0;translated.frame.normal[1]=-1;translated.frame.normal[2]=0;
    translated.frame.xAxis[0]=1;translated.frame.xAxis[1]=translated.frame.xAxis[2]=0;
    require(refreshLinkedSketchReferences(translated,view.sketches_,view.extrusions_).isEmpty(),"proiezione disponibile anche se il piano non attraversa la superficie");
    const auto projected=translated.linkedReferences.first()->entities.first();
    require(std::abs(translated.segments.at(projected.element).first.x()+14)<1e-8,"la proiezione sul piano esterno conserva il bordo estremo, senza sezionare il corpo");
    view.sketches_[view.activeSketch_]=translated;view.sketchReferenceCacheValid_=false;view.setViewNormal(1);move({-14,3});
    require(isSketchApparentReference(view.sketchReferenceHoverSource_) && !view.sketchReferenceHoverContact_
        && view.sketchReferenceHoverError_.isEmpty(),"Quota prende il bordo proiettato anche sul piano esterno al corpo");
    bodies.clear();const auto broken=refreshLinkedSketchReferences(sketch,view.sketches_,bodies);
    require(broken.contains("Contorno apparente") && broken.contains("Parte raggiata") && broken.contains("Vincoli"),"riferimento eliminato identifica sorgente e vincoli da riparare");
    view.setDrawingTool(DrawingTool::Select);move({-14,3});
    require(view.sketchReferenceHoverLabels_.isEmpty() && view.sketchReferenceHoverSource_.kind<0,"nessuna proposta al passaggio in Selezione");
    std::cout<<"PASS contorni apparenti esatti, facce trimmate, snap/Quota, riferimento unico, refresh raggio, codec e Undo/Redo\n";
}
