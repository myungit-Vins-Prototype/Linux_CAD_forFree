// Selezione esplicita separata dagli agganci automatici: hover in sola lettura.
static void sketchReferenceHover(bool gl) {
    using namespace ForgeCad;
    using namespace Kernel;
    const auto load = [&](CadViewport &view, const Body &shape) {
        DocumentState document;
        ExtrusionObject body; body.feature = BodyFeature::Imported; body.name = "Corpo di riferimento";
        body.featureId = 901; body.forgeBody = std::make_shared<const Body>(shape); body.cachedGeometry = true;
        document.extrusions = {body};
        view.resize(1100,800); view.loadDocument(document);
        require(view.createSketch(0,"Profilo da vincolare") >= 0,"schizzo dopo corpo sorgente");
        view.sketches_[view.activeSketch_].segments = {{{-6,1},{-2,4}}};
        view.sketches_[view.activeSketch_].storyId = 902;
        view.setDrawingTool(DrawingTool::Select);
        view.geometrySnap_ = false; // scelta manuale indipendente dagli snap
        view.originSnap_ = true; view.gridSnap_ = false; view.axesVisible_ = false; view.referencePlanesVisible_ = false;
        view.camera_.zoom_ = 32; view.setViewNormal(0); view.history_.clear();
        view.ensureBodyDisplay(0);
    };
    const auto pixel = [](CadViewport &view, Vec3 p) { return view.projectWorldPoint({float(p.x()),float(p.y()),float(p.z())}).toPoint(); };
    const auto move = [](CadViewport &view, QPoint p, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(QEvent::MouseMove,QPointF(p),QPointF(p),Qt::NoButton,Qt::NoButton,modifiers); view.mouseMoveEvent(&event);
    };
    const auto click = [](CadViewport &view, QPoint p, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(p),QPointF(p),Qt::LeftButton,Qt::LeftButton,modifiers); view.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(p),QPointF(p),Qt::LeftButton,Qt::NoButton,modifiers); view.mouseReleaseEvent(&release);
    };
    const auto definition = [](CadViewport &view) { QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes,false).isEmpty(),"istantanea definizione"); return bytes; };
    {
        CadViewport view;
        const double t = std::sqrt(.5);
        load(view, makeBox(Frame3({10,-5,-3},{0,-t,t},{1,0,0}),8,10,6));
        view.sketchSelections_ = {{0,0}};
        const auto position = pixel(view,{10,-2,0});
        const auto before = definition(view);
        move(view,position);
        require(view.sketchReferenceHoverSource_.kind < 0 && view.sketchReferenceHoverLabels_.isEmpty()
                && view.sketchReferenceHoverKey_.isEmpty(), "Selezione non propone ne' calcola riferimenti esterni");
        view.setDrawingTool(DrawingTool::Dimension); move(view,position);
        require(view.sketchReferenceHoverSource_.kind == 4,"hover sul bordo del corpo");
        require(view.sketchReferenceHoverTarget_.kind == 0 && view.sketchReferenceHoverTarget_.point < 0,
                "il contatto puntuale non sostituisce la selezione del bordo intero");
        const auto labels = view.sketchReferenceHoverLabels_.join(' ');
        require(labels.contains("Proiezione") && labels.contains("Corpo di riferimento") && labels.contains("Spigolo")
                && labels.contains(constraintName(ConstraintType::Parallel)),"badge: tipo, bordo, sorgente e vincoli compatibili");
        for (int i=0;i<5;++i) move(view,position);
        require(definition(view)==before && view.sketches_[view.activeSketch_].linkedReferences.isEmpty()
                && !view.canUndo(),"hover non crea cache persistenti, vincoli, modifiche o Undo");
        view.setDrawingTool(DrawingTool::Select); view.sketchSelections_={{0,0}};
        click(view,position);
        auto refs = view.constraintSelection();
        require(refs.size()==2 && refs[0].element==0 && refs[1].point<0,"clic esterno conserva l'elemento locale gia scelto");
        require(view.sketches_[view.activeSketch_].linkedReferences.size()==1
                && view.sketches_[view.activeSketch_].geometricConstraints.isEmpty(),"clic seleziona un solo riferimento e nessun vincolo");
        require(applicableConstraints(*view.activeSketchObject(),refs).contains(ConstraintType::Parallel),"vincolo suggerito realmente disponibile nel pannello");
        require(view.addConstraint(ConstraintType::Parallel).isEmpty(),"parallelismo al bordo associativo applicabile");
        require(view.activeSketchObject()->geometricConstraints.size()==1 && view.activeSketchObject()->linkedReferences.size()==1,
                "viene creato soltanto il vincolo scelto");
        QByteArray bytes;DocumentState decoded;
        require(encodeDocument(view.currentDocument(),bytes,false).isEmpty() && decodeDocument(bytes,decoded).isEmpty(),"salvataggio riferimento scelto");
        view.undo();require(view.activeSketchObject()->geometricConstraints.isEmpty(),"Undo del solo vincolo");
        view.redo();require(view.activeSketchObject()->geometricConstraints.size()==1,"Redo conserva il riferimento");
        view.selectedPoints_.clear();view.sketchSelections_={{0,0}};
        click(view,position);
        require(view.activeSketchObject()->linkedReferences.size()==1,"riferimento gia usato riutilizzato senza duplicarlo");
    }
    {
        CadViewport view; load(view,makeBox(Frame3({10,-5,-3},{0,0,1},{1,0,0}),8,10,6));
        view.setDrawingTool(DrawingTool::Dimension);
        view.selectedPoints_={{0,0,0}}; view.setViewPreset(4);
        const auto position = pixel(view,{10,0,0}); const auto before = definition(view);
        move(view,position);
        require(view.sketchReferenceHoverSource_.kind==5 && view.sketchReferenceHoverContact_,"faccia: proposta la sezione col piano di schizzo");
        const auto labels = view.sketchReferenceHoverLabels_.join(' ');
        if(!view.sketchReferenceHoverError_.isEmpty())std::cerr<<view.sketchReferenceHoverError_.toStdString()<<std::endl;
        require(view.sketchReferenceHoverError_.isEmpty() && labels.contains("Faccia")
                && labels.contains(constraintName(ConstraintType::PointOnCurve)),"faccia identificata e punto su sezione proposto");
        require(definition(view)==before,"hover faccia non modifica lo schizzo");
        if(gl) {
            view.show();QApplication::processEvents();require(view.isValid(),"contesto OpenGL per suggerimento associativo");
            view.setAttribute(Qt::WA_UnderMouse,true);move(view,position);
            const auto image=view.grabFramebuffer();const auto lines=view.sketchReferenceHoverLabels_;
            view.sketchReferenceHoverLabels_.clear();const auto without=view.grabFramebuffer();view.sketchReferenceHoverLabels_=lines;
            require(!image.isNull() && image!=without,"badge associativo realmente disegnato");
            const QString qa=qEnvironmentVariable("FORGECAD_SKETCH_REFERENCE_QA");
            if(!qa.isEmpty()){QDir().mkpath(qa);require(image.save(qa+"/faccia-vincoli-proposti.png"),"cattura badge faccia");}
        }
        view.setDrawingTool(DrawingTool::Select); view.selectedPoints_={{0,0,0}};
        move(view,position);
        require(view.sketchReferenceHoverSource_.kind<0 && view.sketchReferenceHoverLabels_.isEmpty(),"nessun suggerimento dopo la fine del comando");
        click(view,position);
        require(view.constraintSelection().size()==2 && view.activeSketchObject()->geometricConstraints.isEmpty(),"faccia selezionata senza assegnare vincoli automatici");
        require(view.addConstraint(ConstraintType::PointOnCurve).isEmpty(),"punto vincolabile alla sezione della faccia");
        require(view.activeSketchObject()->linkedReferences.size()==1 && view.activeSketchObject()->geometricConstraints.size()==1,
                "un solo riferimento di faccia e un solo vincolo");
        view.sketchSelections_.clear();view.selectedPoints_={{0,0,1}};
        view.setDrawingTool(DrawingTool::Dimension);click(view,position);
        require(view.constraintSelection().size()==2 && view.dimensionPreviewValid_,"Quota usa la faccia esplicitamente selezionata");
        require(view.activeSketchObject()->linkedReferences.size()==1,"Quota riutilizza la stessa sorgente associativa");
    }
    {
        CadViewport view;load(view,makeCylinder(Frame3({12,0,-3},{0,0,1},{1,0,0}),4,6));
        view.setDrawingTool(DrawingTool::Dimension); view.selectedPoints_={{0,0,0}};
        const auto position = pixel(view,{12,0,0});
        move(view,position,Qt::ControlModifier);
        require(view.sketchReferenceHoverTarget_.kind==1 && view.sketchReferenceHoverTarget_.point==0,
                "Ctrl propone il centro del cerchio anche senza un bordo sotto il mouse");
        require(view.sketchReferenceHoverLabels_.join(' ').contains(constraintName(ConstraintType::Coincident)),"coincidenza col centro proposta esplicitamente");
        view.setDrawingTool(DrawingTool::Select); view.selectedPoints_={{0,0,0}};
        click(view,position,Qt::ControlModifier);
        require(view.constraintSelection().size()==2 && view.activeSketchObject()->linkedReferences.size()==1,
                "Ctrl sceglie il centro associativo mantenendo il punto locale");
    }
    {
        CadViewport view;load(view,makeCylinder(Frame3({12,0,-3},{0,0,1},{1,0,0}),4,6));
        view.geometrySnap_=true;
        const auto position=pixel(view,{16,0,0});
        for(auto tool:{DrawingTool::Polyline,DrawingTool::Spline,DrawingTool::Nurbs,DrawingTool::Circle,DrawingTool::ThreePointCircle}) {
            view.setDrawingTool(tool);move(view,position);
            require(view.lastSnapKind_!=SnapKind::None && view.snapLabelAt(view.lastSnapKind_,view.lastSnapPoint_).contains("Corpo di riferimento"),
                    "comandi di disegno conservano il badge con sorgente associativa");
        }
        view.setDrawingTool(DrawingTool::Select);move(view,position);
        require(view.lastSnapKind_==SnapKind::None && view.sketchReferenceHoverLabels_.isEmpty()
                && view.activeSketchObject()->linkedReferences.isEmpty(),"fine comando elimina la proposta senza creare riferimenti");
    }
    std::cout<<"PASS selezione bordi/facce, hover associativo in sola lettura, suggerimenti coerenti, vincolo unico e Undo/Redo\n";
}
