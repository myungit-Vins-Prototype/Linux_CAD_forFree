// Incluso nel friend ViewportInteractionTest: regressioni dei riferimenti vivi.
    static void linkedSketchReferences() {
        using namespace ForgeCad;
        using namespace Kernel;
        const auto near = [](QPointF a, QPointF b) { return pointDistance(a,b) < 1e-7; };
        const auto line = [](SketchObject &s, QPointF a, QPointF b) {
            s.segments.append({a,b}); s.constraints.append(-1); s.segmentLengths.append(0); s.segmentAngles.append(-1);
        };
        const auto noFix = [](const SketchObject &s) {
            for (const auto &c : s.geometricConstraints) if (c.type == ConstraintType::Fix) return false;
            return true;
        };
        SketchObject source; source.name="Sorgente"; source.storyId=101; line(source,{-5,4},{5,4});
        SketchObject target; target.name="Associativo"; target.storyId=102; target.storyOrder=1; line(target,{0,4},{6,4});
        QVector<SketchObject> sketches{source,target};
        GeometryRef ref; ref.kind=7; ref.index=0; ref.featureId=101; ref.element={0,0,-1};
        require(appendLinkedSketchReference(target,ref,sketches,{}).isEmpty(),"riferimento vivo a segmento di altro schizzo");
        require(target.linkedReferences.size()==1 && target.segments.size()==2 && noFix(target),"cache associativa senza Fix");
        target.geometricConstraints={makeConstraint(target,ConstraintType::Midpoint,{{0,0,0},{0,1,-1}}),
                                    makeConstraint(target,ConstraintType::Horizontal,{{0,0,-1}}),
                                    makeConstraint(target,ConstraintType::Distance,{{0,0,-1}})};
        require(solveSketch(target).ok && analyzeSketch(target).fullyDefined(),"cache esterna esclusa dai gradi di liberta'");
        const auto snapshot=target;
        sketches[0].segments[0]={{-3,7},{7,7}};
        require(refreshLinkedSketchReferences(target,sketches,{}).isEmpty() && solveSketch(target).ok,"aggiornamento e soluzione da schizzo sorgente");
        require(near(target.segments[0].first,{2,7}) && near(target.segments[0].second,{8,7}),"punto medio e quota seguono la sorgente");
        require(snapshot.segments[1].first==QPointF(-5,4) && snapshot.linkedReferences!=target.linkedReferences,"istantanea Undo e metadata non mutati");
        require(!applicableConstraints(target,{{0,1,-1}}).contains(ConstraintType::Fix),"riferimento esterno non fissabile o quotabile da solo");
        require(appendLinkedSketchReference(target,ref,sketches,{}).isEmpty() && target.linkedReferences.size()==1,"riuso per identita' della sorgente");
        sketches[1]=target;
        require(storySketchDependencyError(sketches,{},{{true,0},{true,1}}).isEmpty(),"ordine associativo valido");
        require(!storySketchDependencyError(sketches,{},{{true,1},{true,0}}).isEmpty(),"rifiuto ciclo tramite riordino della storia");
        DocumentState state; state.sketches=sketches; QByteArray bytes;
        require(encodeDocument(state,bytes,false).isEmpty(),"salvataggio formato associativo");
        DocumentState loaded; require(decodeDocument(bytes,loaded).isEmpty(),"riapertura riferimenti associativi");
        require(loaded.sketches[1].linkedReferences.size()==1 && loaded.sketches[1].linkedReferences[0]->source.featureId==101,
                "identita' della sorgente persistente dopo riapertura");
        loaded.sketches[0].segments[0]={{-1,9},{9,9}};
        require(refreshLinkedSketchReferences(loaded.sketches[1],loaded.sketches,{}).isEmpty() && solveSketch(loaded.sketches[1]).ok
                && near(loaded.sketches[1].segments[0].first,{4,9}),"associativita' conservata dopo riapertura");
        QByteArray legacy; require(!encodeDocumentDefinition53ForHash(state,legacy).isEmpty(),"hash storico non perde i collegamenti");
        auto missing=target;
        require(!refreshLinkedSketchReferences(missing,{},{}).isEmpty() && !solveSketch(missing).ok,"sorgente eliminata non ignorata dal solver");
        require(refreshLinkedSketchReferences(missing,sketches,{}).isEmpty() && solveSketch(missing).ok,"riferimento recuperabile ripristinando la sorgente");
        auto remapped=target; remapConstraints(remapped,{-1,0},{});
        require(remapped.linkedReferences.size()==1 && remapped.linkedReferences[0]->entities[0].element==0,"rinumerazione cache dopo eliminazione entita'");
        auto deleted=target; remapConstraints(deleted,{0,-1},{});
        require(deleted.linkedReferences.isEmpty(),"eliminazione esplicita di un riferimento senza metadata orfani");
        // Curva e quadranti: il centro/raggio corrente vengono dal sorgente.
        CurveObject circle; circle.tool=DrawingTool::Circle; circle.controlPoints={{20,10},{25,10}}; recalculateCurve(circle);
        sketches[0].curves={circle}; ref.element={1,0,-1};
        SketchObject curved; line(curved,{20,15},{30,15});
        require(appendLinkedSketchReference(curved,ref,sketches,{}).isEmpty() && curved.curves[0].tool==DrawingTool::Circle,"riferimento circolare esatto");
        auto quadrant=makeConstraint(curved,ConstraintType::Quadrant,{{0,0,0},{1,0,-1}}); quadrant.value=1;
        curved.geometricConstraints={quadrant}; sketches[0].curves[0].controlPoints[1]={28,10}; recalculateCurve(sketches[0].curves[0]);
        require(refreshLinkedSketchReferences(curved,sketches,{}).isEmpty() && solveSketch(curved).ok
                && near(curved.segments[0].first,{20,18}) && noFix(curved),"quadrante segue il nuovo raggio senza Fix");
        // Origine e assi del modello anche su un piano di schizzo traslato.
        SketchObject frame; frame.customFrame=true; frame.frame.origin[0]=3;
        GeometryRef origin; origin.kind=0;
        require(appendLinkedSketchReference(frame,origin,{},{}).isEmpty() && near(frame.segments[0].first,{-3,0}),"origine modello proiettata nel piano corrente");
        frame.frame.origin[0]=5; require(refreshLinkedSketchReferences(frame,{},{}).isEmpty() && near(frame.segments[0].first,{-5,0}),"origine segue il piano dello schizzo");
        GeometryRef axis; axis.kind=2; axis.index=1;
        require(appendLinkedSketchReference(frame,axis,{},{}).isEmpty() && noFix(frame),"asse modello associativo");
        GeometryRef plane; plane.kind=1; plane.index=2;
        require(appendLinkedSketchReference(frame,plane,{},{}).isEmpty(),"traccia piano modello associativa");
        // Datum mobile e riferimenti topologici a spigolo/vertice.
        const auto wall=[](double x) { return makePlaneSheet(Frame3(Vec3(x,0,0),Vec3(1,0,0),Vec3(0,1,0)),5); };
        ExtrusionObject body; body.feature=BodyFeature::Imported; body.featureId=200; body.name="Parete";
        body.forgeBody=std::make_shared<const Body>(wall(8));
        QVector<ExtrusionObject> bodies{body};
        EdgeId crossing;
        for (auto edge : body.forgeBody->edges()) {
            const auto &e=body.forgeBody->edge(edge); const auto a=e.curve->point(e.range.lo), b=e.curve->point(e.range.hi);
            if (a.z()*b.z()<0 && std::abs(a.y()-b.y())<1e-8) { crossing=edge; break; }
        }
        require(crossing.valid(),"fixture spigolo attraversante il piano");
        ref={};ref.kind=4;ref.index=0;ref.featureId=200;
        const auto &edge=body.forgeBody->edge(crossing);ref.point=edgeReference(*body.forgeBody,crossing,edge.curve->point(edge.range.lo));
        SketchObject pierce; require(appendLinkedSketchReference(pierce,ref,{},bodies,true).isEmpty(),"Pierce esatto su spigolo");
        const auto pierceY=pierce.segments[0].first.y();require(near(pierce.segments[0].first,{8,pierceY}) && pierce.segments[0].first==pierce.segments[0].second,"Pierce crea solo punto di contatto associativo");
        GeometryRef vertex;vertex.kind=3;vertex.index=0;vertex.featureId=200;vertex.point=vertexReference(*body.forgeBody,body.forgeBody->vertices().front());
        require(appendLinkedSketchReference(pierce,vertex,{},bodies).isEmpty(),"vertice topologico associativo");
        bodies[0].forgeBody=std::make_shared<const Body>(wall(12));
        require(refreshLinkedSketchReferences(pierce,{},bodies).isEmpty() && std::abs(pierce.segments[0].first.x()-12)<1e-8
                && std::abs(pierce.segments[1].first.x()-12)<1e-8 && noFix(pierce),"Pierce e vertice seguono il corpo modificato");
        ExtrusionObject datum;datum.feature=BodyFeature::DatumPlane;datum.featureId=201;datum.datumValid=true;
        datum.datumFrame.origin[0]=2;datum.datumFrame.normal[0]=1;datum.datumFrame.normal[2]=0;datum.datumFrame.xAxis[0]=0;datum.datumFrame.xAxis[1]=1;
        bodies.append(datum);GeometryRef datumRef;datumRef.kind=8;datumRef.index=1;datumRef.featureId=201;
        SketchObject onDatum;require(appendLinkedSketchReference(onDatum,datumRef,{},bodies).isEmpty(),"traccia datum associativa");
        bodies[1].datumFrame.origin[0]=6;require(refreshLinkedSketchReferences(onDatum,{},bodies).isEmpty()
            && std::abs(onDatum.segments[0].first.x()-6)<1e-8,"riferimento al piano segue lo spostamento datum");
        GeometryRef sectionRef;sectionRef.kind=11;sectionRef.index=0;sectionRef.featureId=200;
        bodies[0].forgeBody=std::make_shared<const Body>(makeBox(Frame3(),10,12,5));
        SketchObject section;section.customFrame=true;section.frame.origin[2]=2;
        require(appendLinkedSketchReference(section,sectionRef,{},bodies,true).isEmpty() && section.segments.size()==4 && noFix(section),
                "sezione corpo associativa con contorno deduplicato");
        bodies[0].forgeBody=std::make_shared<const Body>(makeBox(Frame3(),15,12,5));
        require(refreshLinkedSketchReferences(section,{},bodies).isEmpty(),"sezione segue il corpo modificato");
        double maxX=0;for(const auto &segment:section.segments)maxX=std::max({maxX,segment.first.x(),segment.second.x()});
        require(std::abs(maxX-15)<1e-8,"dimensione della sezione associativa aggiornata");
        // Snap in hover senza mutazioni; creazione con il mouse e Undo/Redo.
        CadViewport view;view.resize(1000,750);
        SketchObject first;first.name="Base";first.storyId=301;line(first,{20,5},{40,5});
        SketchObject second;second.name="Nuovo";second.storyId=302;second.storyOrder=1;
        view.sketches_={first,second};view.activeSketch_=1;view.sketchMode_=true;
        view.geometrySnap_=true;view.gridSnap_=view.originSnap_=view.axesVisible_=false;
        view.camera_.setFrame(QVector3D(1,0,0),QVector3D(0,0,1));view.camera_.zoom_=50;
        const auto end=view.snapPoint({20.01,5.01},false);
        require(near(end,{20,5}) && view.sketches_[1].linkedReferences.isEmpty() && !view.canUndo(),"hover snap estremo esterno senza modifiche documento");
        require(near(view.snapPoint({30.01,5.01},false),{30,5}),"snap punto medio esterno");
        require(near(view.snapPoint({27,5.01},false),{27,5}),"snap su bordo esterno");
        view.setDrawingTool(DrawingTool::Line);
        const auto click=[&](QPointF p, Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
            const auto pixel=view.projectWorldPoint(view.mapSketchPoint(p,view.sketches_[1]));
            QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,modifiers);view.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,modifiers);view.mouseReleaseEvent(&release);
        };
        click({20,5});click({20,15});
        require(view.sketches_[1].linkedReferences.size()==1 && noFix(view.sketches_[1]),"disegno mouse aggancia direttamente il modello senza Fix");
        bool coincident=false;for (const auto &c:view.sketches_[1].geometricConstraints)coincident=coincident||c.type==ConstraintType::Coincident;
        require(coincident,"snap mouse crea coincidenza associativa");
        const auto created=view.sketches_[1];view.undo();require(view.sketches_[1].segments.isEmpty()&&view.sketches_[1].linkedReferences.isEmpty(),"Undo atomico geometria e riferimento");
        view.redo();require(view.sketches_[1].linkedReferences.size()==1,"Redo collegamento");
        view.sketches_[0].segments[0]={{22,8},{42,8}};view.regenerateDependents(0);
        require(near(view.sketches_[1].segments[0].first,{22,8}),"rigenerazione viewport propaga lo spostamento sorgente");
        view.setDrawingTool(DrawingTool::Select);click({42,8},Qt::ControlModifier);
        require(!view.selectedPoints_.isEmpty(),"Ctrl clic diretto su punto di geometria esterna");
        ref={};ref.kind=7;ref.index=1;ref.featureId=302;ref.element={0,0,-1};
        require(!view.allowedSketchReference(ref),"autoreferenza rifiutata");
        view.activeSketch_=0;require(!view.allowedSketchReference(ref),"riferimento a schizzo futuro rifiutato");
        // Quota tramite clic diretto su un estremo esterno non ancora importato.
        CadViewport dims; dims.resize(1000,750);
        SketchObject dimensioned; dimensioned.name="Quota"; dimensioned.storyId=303; dimensioned.storyOrder=1;
        line(dimensioned,{15,25},{22,25}); dims.sketches_={first,dimensioned}; dims.activeSketch_=1; dims.sketchMode_=true;
        dims.geometrySnap_=true; dims.gridSnap_=dims.originSnap_=dims.axesVisible_=false;
        dims.camera_.setFrame(QVector3D(1,0,0),QVector3D(0,0,1));dims.camera_.zoom_=20;
        dims.setDrawingTool(DrawingTool::Dimension);
        const auto dimClick=[&](QPointF p) {
            const auto pixel=dims.projectWorldPoint(dims.mapSketchPoint(p,dims.sketches_[1]));
            QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);dims.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);dims.mouseReleaseEvent(&release);
        };
        dimClick({15,25});dimClick({40,5});
        require(dims.sketches_[1].linkedReferences.size()==1 && dims.dimensionPreviewValid_,"quota sceglie direttamente estremo del modello");
        QTimer::singleShot(0,&dims,[] { if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            if(auto *buttons=dialog->findChild<QDialogButtonBox *>()) buttons->button(QDialogButtonBox::Ok)->click(); });
        dimClick({45,35});
        bool dimension=false; for(const auto &c:dims.sketches_[1].geometricConstraints) dimension=dimension||isDimension(c.type);
        require(dimension && noFix(dims.sketches_[1]),"quota associativa confermata con il mouse");
        // Inferenze esterne e snap del trascinamento.
        CadViewport inference;inference.resize(1000,750);inference.sketches_={first,second};
        inference.sketches_[0].segments[0]={{20,10},{40,20}};
        inference.activeSketch_=1;inference.sketchMode_=true;inference.geometrySnap_=true;
        inference.gridSnap_=inference.axesVisible_=inference.originSnap_=false;
        inference.camera_.setFrame(QVector3D(1,0,0),QVector3D(0,0,1));inference.camera_.zoom_=20;
        inference.hasPendingPoint_=true;inference.pendingPoint_={0,-10};
        const auto inferred=inference.inferLinePoint({20,0.1});
        require(inferred.constraint==4 && inferred.reference<=-2,"inferenza parallelo a bordo esterno senza importazione preventiva");
        inference.pointDragPosition_={0,0};
        require(near(inference.dragSnapPoint({30.01,15.01}),{30,15}),"trascinamento agganciato al punto medio esterno");
        // La modifica del corpo deve aggiornare anche le lavorazioni dipendenti.
        CadViewport cascade;
        ExtrusionObject base;base.feature=BodyFeature::Imported;base.featureId=600;base.name="Corpo sorgente";
        base.forgeBody=std::make_shared<const Body>(wall(2));
        base.importData=QByteArray::fromStdString(writeStep({{"Sorgente",*base.forgeBody,false,{0,0,0},nullptr,{}}}));
        ExtrusionObject dependent;dependent.feature=BodyFeature::Extrusion;dependent.featureId=601;
        dependent.name="Superficie dipendente";dependent.sketchIndex=0;dependent.extrudeSurface=true;dependent.distance=3;
        cascade.extrusions_={base,dependent};
        GeometryRef anchor;anchor.kind=3;anchor.index=0;anchor.featureId=600;
        anchor.point=vertexReference(*base.forgeBody,base.forgeBody->vertices().front());
        SketchObject profile;profile.storyId=602;profile.name="Profilo associativo";profile.beforeFeatureId=601;
        const QPointF initialAnchor(anchor.point.x,anchor.point.y);
        line(profile,initialAnchor,initialAnchor+QPointF(6,0));
        require(appendLinkedSketchReference(profile,anchor,{},cascade.extrusions_).isEmpty(),"ancoraggio profilo a vertice del corpo");
        profile.geometricConstraints={makeConstraint(profile,ConstraintType::Coincident,{{0,0,0},{0,1,0}}),
                                     makeConstraint(profile,ConstraintType::Horizontal,{{0,0,-1}}),
                                     makeConstraint(profile,ConstraintType::Distance,{{0,0,-1}})};
        cascade.sketches_={profile};cascade.regenerateDependents(0);
        require(cascade.extrusions_[1].forgeBody && cascade.extrusions_[1].error.isEmpty(),"lavorazione generata da profilo con riferimento esterno");
        const auto previous=cascade.extrusions_[1].forgeBody;const auto savedLink=cascade.sketches_[0].linkedReferences[0];
        cascade.recordUndo();cascade.extrusions_[0].forgeBody=std::make_shared<const Body>(wall(4));
        cascade.extrusions_[0].importData=QByteArray::fromStdString(writeStep({{"Sorgente",*cascade.extrusions_[0].forgeBody,false,{0,0,0},nullptr,{}}}));
        cascade.regenerateAfter(0);
        require(near(cascade.sketches_[0].segments[0].first,initialAnchor+QPointF(2,0))
            && cascade.extrusions_[1].forgeBody && cascade.extrusions_[1].forgeBody!=previous && cascade.extrusions_[1].error.isEmpty(),
            "modifica corpo rigenera riferimento, profilo e lavorazione a cascata");
        require(savedLink!=cascade.sketches_[0].linkedReferences[0],"cache precedente della cascata immutabile");
        cascade.undo();require(near(cascade.sketches_[0].segments[0].first,initialAnchor),"Undo cascata ripristina sorgente e profilo");
        cascade.redo();require(near(cascade.sketches_[0].segments[0].first,initialAnchor+QPointF(2,0)),"Redo cascata associativa");
        cascade.removeBodies({0});
        require(!cascade.sketches_[0].referenceError.isEmpty() && !cascade.extrusions_.back().forgeBody
            && !cascade.extrusions_.back().error.isEmpty(),"sorgente eliminata blocca le lavorazioni dipendenti");
        // Un vertice coincidente con l'origine conserva l'identita' del modello.
        CadViewport atOrigin;atOrigin.sketches_={first,second};atOrigin.activeSketch_=1;atOrigin.sketchMode_=true;
        atOrigin.sketches_[0].segments[0]={{0,0},{10,0}};atOrigin.geometrySnap_=atOrigin.originSnap_=true;atOrigin.axesVisible_=false;
        line(atOrigin.sketches_[1],{0,0},{0,5});
        atOrigin.recordPointCoincidences(atOrigin.sketches_[1],{0,0,0},{0,0});
        require(atOrigin.sketches_[1].linkedReferences.size()==1,"vertice sull'origine resta associato al modello");
        for(const auto &c:atOrigin.sketches_[1].geometricConstraints) require(c.second.kind!=2,"nessun doppio blocco involontario sull'origine");
        atOrigin.sketches_[0].segments[0]={{2,3},{12,3}};atOrigin.regenerateDependents(0);
        require(near(atOrigin.sketches_[1].segments[0].first,{2,3}),"vertice inizialmente all'origine segue lo spostamento della parte");
        std::cout<<"PASS linked sketch references (solver, quote, snap, Pierce, datum, storia, IO, mouse, Undo)"<<std::endl;
    }

    static void linkedReferenceVisibility(bool gl) {
        using namespace ForgeCad;
        using namespace Kernel;
        CadViewport view; view.resize(640,480);view.setGridVisible(false);view.setAxesVisible(false);
        for (int i=0;i<3;++i) view.setPlaneVisible(i,false);
        SketchObject source;source.name="Geometria originale";source.storyId=700;
        source.segments={{{-4,2},{-1,4}}};source.constraints={-1};source.segmentLengths={0};source.segmentAngles={-1};
        CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{3,2},{5,2}};recalculateCurve(circle);source.curves={circle};
        SketchObject original;original.name="Schizzo";original.storyId=701;original.storyOrder=1;
        original.segments={{{0,-2},{4,-2}}};original.constraints={-1};original.segmentLengths={0};original.segmentAngles={-1};
        view.sketches_={source,original};view.activeSketch_=1;view.sketchMode_=true;
        view.camera_.setFrame(QVector3D(1,0,0),QVector3D(0,0,1));view.camera_.zoom_=14;
        ExtrusionObject body;body.feature=BodyFeature::Imported;body.featureId=702;body.name="Parte";body.visible=false;
        body.forgeBody=std::make_shared<const Body>(makeBox(Frame3(),8,6,3));view.extrusions_={body};
        auto linked=original;
        const auto append=[&](GeometryRef ref) {
            const auto error=appendLinkedSketchReference(linked,ref,view.sketches_,view.extrusions_);
            require(error.isEmpty(),qPrintable(error));
        };
        GeometryRef edge;edge.kind=4;edge.index=0;edge.featureId=702;
        for (auto id:body.forgeBody->edges()) {
            const auto &e=body.forgeBody->edge(id);const auto a=e.curve->point(e.range.lo),b=e.curve->point(e.range.hi);
            if(std::abs(a.z()-b.z())<1e-9){edge.point=edgeReference(*body.forgeBody,id,a);break;}
        }
        append(edge);
        GeometryRef ref;ref.kind=7;ref.index=0;ref.featureId=700;ref.element={0,0,-1};append(ref);
        ref.element={1,0,-1};append(ref);
        ref={};ref.kind=2;ref.index=0;append(ref);
        ref={};ref.kind=1;ref.index=2;append(ref);
        ref={};ref.kind=0;append(ref);
        require(linked.linkedReferences.size()>=5,"riferimenti a bordo, curve, piani, assi e origine per verifica grafica");
        const auto bounds=view.sketchGeometryPoints(original);
        require(view.sketchGeometryPoints(linked)==bounds,"riferimenti interni non alterano inquadratura e limiti dello schizzo");
        const auto blank=[] { QImage image(640,480,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);return image; };
        QImage empty=blank(),highlights=blank();
        {
            QPainter painter(&highlights);
            for(const auto &link:linked.linkedReferences)for(const auto &entity:link->entities)
                view.highlightSketchElement(painter,linked,{entity.kind,entity.element},Qt::yellow);
        }
        require(highlights==empty,"selezione e hover dei riferimenti non disegnano copie visibili");
        const auto dimensions=[&](const SketchObject &sketch) {
            view.sketches_[1]=sketch;auto image=blank();QPainter painter(&image);view.drawSketchDimensions(painter);painter.end();return image;
        };
        require(dimensions(original)==dimensions(linked),"nessuna etichetta o tratto di quota ausiliario sulle cache");
        view.sketches_[1]=linked;view.analysisDirty_=true;view.geometrySnap_=true;view.gridSnap_=false;
        require(pointDistance(view.snapPoint({-4.01,2.01},false),{-4,2})<1e-7,"snap ancora disponibile sulla geometria con cache invisibile");
        require(solveSketch(view.sketches_[1]).ok,"riferimenti invisibili ancora validi per il solver");
        auto explicitGeometry=original;ref.kind=7;ref.index=0;ref.featureId=700;ref.element={0,0,-1};
        require(appendLinkedSketchReference(explicitGeometry,ref,view.sketches_,view.extrusions_,false,false).isEmpty(),"conversione esplicita in geometria del profilo");
        QImage explicitHighlight=blank();{
            QPainter painter(&explicitHighlight);view.highlightSketchElement(painter,explicitGeometry,{0,1},Qt::yellow);
        }
        require(explicitHighlight!=empty,"geometria del profilo esplicitamente convertita resta visibile");
        auto manual=original;manual.constructionSegments={0};QImage manualHighlight=blank();{
            QPainter painter(&manualHighlight);view.highlightSketchElement(painter,manual,{0,0},Qt::yellow);
        }
        require(manualHighlight!=empty,"linee di costruzione disegnate dall'utente restano visibili");
        if(gl) {
            view.sketches_[1]=original;view.analysisDirty_=true;view.geometrySnap_=false;view.lastSnapKind_=SnapKind::None;
            view.drawingTool_=DrawingTool::Select;view.sketchSelections_.clear();view.selectedPoints_.clear();view.selectedConstraints_.clear();
            view.sketchHover_={};view.constraintHover_=-1;view.lastMousePosition_={-1000,-1000};
            view.show();for(int i=0;i<8;++i)QApplication::processEvents();require(view.isValid(),"contesto OpenGL per visibilita' riferimenti");
            const auto capture=[&](const SketchObject &sketch) {
                view.sketches_[1]=sketch;view.analysisDirty_=true;view.update();QApplication::processEvents();
                const auto frame=view.grabFramebuffer();require(!frame.isNull(),"framebuffer riferimenti disponibile");return frame;
            };
            const auto baseline=capture(original),withReferences=capture(linked);
            require(baseline==withReferences,"framebuffer invariato aggiungendo riferimenti associativi nello schizzo attivo");
            for(const auto &link:linked.linkedReferences)for(const auto &entity:link->entities)view.sketchSelections_.append({entity.kind,entity.element});
            view.sketchHover_=view.sketchSelections_.first();
            require(capture(linked)==baseline,"nessun tratto ausiliario neppure con riferimenti selezionati");
            view.sketchSelections_.clear();view.sketchHover_={};
            view.sketchMode_=false;view.previewErrorSketch_=1;
            const auto outside=capture(original),outsideReferences=capture(linked);
            require(outside==outsideReferences,"riferimenti invisibili fuori dallo schizzo e nelle evidenziazioni d'errore");
            view.previewErrorSketch_=-1;
            // Nascondi la sorgente per distinguere la copia di profilo dalla stessa retta gia' visibile.
            view.sketches_[0].visible=false;
            const auto clean=capture(original),converted=capture(explicitGeometry);
            require(clean!=converted,"conversione esplicita del profilo visibile nel framebuffer");
            const auto path=qEnvironmentVariable("FORGECAD_REFERENCE_QA_DIR");
            if(!path.isEmpty()){
                QDir().mkpath(path);require(baseline.save(path+"/schizzo.png") && withReferences.save(path+"/con-riferimenti.png")
                    && converted.save(path+"/profilo-convertito.png"),"salvataggio immagini QA riferimenti");
            }
            view.close();
        }
        std::cout<<"PASS linked reference visibility"<<(gl?" OpenGL":" raster")<<std::endl;
    }
