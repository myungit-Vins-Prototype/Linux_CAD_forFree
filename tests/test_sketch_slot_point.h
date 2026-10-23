// Incluso nel friend ViewportInteractionTest: strumenti reali, geometria e formato.
    static void sketchSlotPoint(bool gl) {
        using namespace ForgeCad;
        const auto click=[](CadViewport &v,QPointF p) {
            const auto pixel=v.projectWorldPoint(v.mapSketchPoint(p,v.sketches_[0]));
            QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            v.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            v.mouseReleaseEvent(&release);
        };
        const auto setup=[](CadViewport &v) {
            v.resize(1000,750);v.createSketch(0,QStringLiteral("Asola e punto"));
            v.geometrySnap_=v.gridSnap_=v.originSnap_=false;v.history_.clear();
        };
        for(const QPointF &b:{QPointF(10,0),QPointF(0,10),QPointF(6,8),QPointF(-6,-8)}) {
            const QPointF n(-b.y()/10,b.x()/10);
            CurveObject slot;slot.tool=DrawingTool::Slot;slot.controlPoints={{0,0},b,2*n};
            recalculateCurve(slot);const auto parts=curveGeometry(slot);
            require(slot.numericallyValid && parts.size()==4,"anteprima asola esatta con quattro tratti");
            QVector<SketchSegment> lines;QVector<CurveObject> arcs;
            require(slotParts(slot.controlPoints,lines,arcs),"scomposizione asola");
            SketchObject profile;profile.segments=lines;profile.curves=arcs;
            ExtrusionObject extrusion;extrusion.sketchIndex=0;extrusion.distance=3;
            QVector<ExtrusionObject> bodies;buildBody(extrusion,0,{profile},bodies,0);
            require(extrusion.forgeBody && std::abs(Kernel::massProperties(*extrusion.forgeBody).volume-3*(40+4*M_PI))<1e-5,
                    "volume asola ruotata con semicirconferenze esatte");
        }
        CadViewport view;setup(view);view.setDrawingTool(DrawingTool::Slot);
        click(view,{0,0});click(view,{0,0});
        require(view.curveControlPoints_.size()==1 && !view.canUndo(),"centri coincidenti non creano geometria o Undo");
        click(view,{10,0});view.cursorSketchPoint_={10,2};CurveObject preview;
        require(view.previewCurve(preview) && preview.tool==DrawingTool::Slot && !view.previewMeasures().isEmpty(),"anteprima live e misure asola");
        click(view,{5,0});require(view.curveControlPoints_.size()==2 && !view.canUndo(),"larghezza zero resta in attesa senza Undo");
        click(view,{10,2});
        require(view.sketches_[0].segments.size()==2 && view.sketches_[0].curves.size()==2,"tre clic producono due segmenti e due archi");
        const auto original=view.sketches_[0];
        for(const auto &c:original.geometricConstraints)require(constraintError(original,c)<1e-7,"vincoli asola inizialmente soddisfatti");
        require(analyzeSketch(original).degreesOfFreedom==5,"asola con cinque gradi di liberta");
        view.undo();require(view.sketches_[0].segments.isEmpty() && view.sketches_[0].curves.isEmpty(),"Undo asola atomico");
        view.redo();require(view.sketches_[0].segments.size()==2 && view.sketches_[0].curves.size()==2,"Redo asola atomico");
        auto changed=view.sketches_[0];
        auto radius=makeConstraint(changed,ConstraintType::Radius,{{1,0,-1}});radius.value=3;
        changed.geometricConstraints.append(radius);
        require(solveSketch(changed).ok,"larghezza asola modificabile con quota raggio");
        for(const auto &c:changed.geometricConstraints)require(constraintError(changed,c)<1e-6,"tangenze e chiusura conservate cambiando larghezza");
        view.setDrawingTool(DrawingTool::Point);click(view,{20,12});
        const int point=view.sketches_[0].curves.size()-1;
        require(point==2 && view.sketches_[0].curves[point].tool==DrawingTool::Point,"punto autonomo inserito con un clic");
        const auto position=view.sketches_[0].curves[point].controlPoints[0];
        require(curveGeometry(view.sketches_[0].curves[point]).empty(),"punto escluso dal profilo");
        require(view.findSketchElement(position).index==point && view.snapCandidates(view.sketches_[0]).contains(position),"punto selezionabile e disponibile per snap");
        view.geometrySnap_=true;
        require(pointDistance(view.snapPoint(position+QPointF(0.01,0.01)),position)<1e-7,"snap reale al punto autonomo");
        view.geometrySnap_=false;
        view.sketchSelections_={{1,point}};require(view.constraintSelection()==QVector<ConstraintRef>{{1,point,0}},"selezione punto offre vincoli di punto");
        SketchObject justPoint;justPoint.curves={view.sketches_[0].curves[point]};
        require(analyzeSketch(justPoint).degreesOfFreedom==2,"punto libero con due gradi di liberta");
        auto horizontal=makeConstraint(justPoint,ConstraintType::HorizontalDistance,{{1,0,0},{2,0,-1}});horizontal.value=7;
        auto vertical=makeConstraint(justPoint,ConstraintType::VerticalDistance,{{1,0,0},{2,0,-1}});vertical.value=9;
        justPoint.geometricConstraints={horizontal,vertical};require(solveSketch(justPoint).ok && analyzeSketch(justPoint).fullyDefined(),"punto quotabile sui due assi");
        view.setDrawingTool(DrawingTool::Select);view.sketchSelections_.clear();
        const auto pixel=view.projectWorldPoint(view.mapSketchPoint(position,view.sketches_[0]));
        QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);view.mousePressEvent(&press);
        const auto movedPixel=view.projectWorldPoint(view.mapSketchPoint(position+QPointF(2,3),view.sketches_[0]));
        QMouseEvent move(QEvent::MouseMove,movedPixel,movedPixel,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);view.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,movedPixel,movedPixel,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);view.mouseReleaseEvent(&release);
        require(pointDistance(position,view.sketches_[0].curves[point].controlPoints[0])>2,"punto trascinabile con mouse");
        view.undo();require(view.sketches_[0].curves[point].controlPoints[0]==position,"Undo spostamento punto");view.redo();
        QByteArray data;require(encodeDocument(view.currentDocument(),data).isEmpty(),"salvataggio asola e punto");DocumentState restored;
        require(decodeDocument(data,restored).isEmpty() && restored.sketches[0].curves[point].tool==DrawingTool::Point,"roundtrip asola e punto formato52");
        auto oldHeader=data;qToBigEndian<quint16>(51,oldHeader.data()+4);
        const auto preserved=restored.sketches[0].curves.size();
        require(!decodeDocument(oldHeader,restored).isEmpty() && restored.sketches[0].curves.size()==preserved,"punto dichiarato in formato51 rifiutato senza mutazioni");
        view.setDrawingTool(DrawingTool::Slot);click(view,{-5,-5});
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);view.keyPressEvent(&escape);
        require(view.curveControlPoints_.isEmpty() && view.sketches_[0].curves.size()==3,"Esc annulla soltanto l'asola in corso");
        PdfWindow window;window.viewport_->createSketch(0,QStringLiteral("Comandi"));bool slotAction=false,pointAction=false;
        for(auto *action:window.findChildren<QAction *>()) {
            if(action->text()==QStringLiteral("Asola")){slotAction=!action->icon().isNull();action->trigger();require(window.viewport_->drawingTool_==DrawingTool::Slot,"azione Asola collegata");}
            if(action->text()==QStringLiteral("Punto")){pointAction=!action->icon().isNull();action->trigger();require(window.viewport_->drawingTool_==DrawingTool::Point,"azione Punto collegata");}
        }
        require(slotAction&&pointAction,"comandi e icone presenti nell'interfaccia");
        if(gl) {
            view.setDrawingTool(DrawingTool::Select);view.fitView(view.sceneGeometryPoints(false,false));view.show();QApplication::processEvents();
            require(view.isValid() && view.grabFramebuffer().save("/tmp/forgecad-slot-point-20261023/sketch.png"),"cattura nativa asola e punto");view.hide();
        }
        std::cout<<"PASS slot/point: exact geometry, mouse, constraints, Undo/Redo, format and commands"<<std::endl;
    }
