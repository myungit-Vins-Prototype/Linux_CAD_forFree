// Incluso nel friend ViewportInteractionTest: esercita i veri gestori mouse.
    static void sketchInteractionRegressions() {
        using namespace ForgeCad;
        const auto near=[](QPointF a,QPointF b){return pointDistance(a,b)<1e-7;};
        const auto setup=[](CadViewport &v,const SketchObject &s){
            if(v.sketchMode_)v.endSketchMode();
            v.resize(1000,700);v.sketches_={s};v.selectSketch(0);
            v.geometrySnap_=v.gridSnap_=v.originSnap_=false;v.setDrawingTool(DrawingTool::Select);v.history_.clear();
        };
        const auto mouse=[](CadViewport &v,QPointF p,int phase){
            const QPointF pixel=v.projectWorldPoint(v.mapSketchPoint(p,v.sketches_[0]));
            QMouseEvent event(phase==0?QEvent::MouseButtonPress:phase==2?QEvent::MouseButtonRelease:QEvent::MouseMove,
                pixel,pixel,phase==1?Qt::NoButton:Qt::LeftButton,phase==2?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
            if(phase==0)v.mousePressEvent(&event);else if(phase==1)v.mouseMoveEvent(&event);else v.mouseReleaseEvent(&event);
        };
        // Un cursore fuori dalla retta ammessa non blocca il grado di liberta' X.
        {
            SketchObject s;s.segments={{{0,0},{10,0}}};
            s.geometricConstraints={makeConstraint(s,ConstraintType::Fix,{{0,0,0}}),makeConstraint(s,ConstraintType::Horizontal,{{0,0,-1}})};
            CadViewport v;setup(v,s);mouse(v,{10,0},0);QElapsedTimer timer;timer.start();
            for(int i=1;i<=60;++i){mouse(v,{10+.1*i,2+std::sin(.1*i)},1);const auto &line=v.sketches_[0].segments[0];require(near(line.first,{0,0})&&std::abs(line.second.y())<1e-8,"drag orizzontale conserva Fix e orizzontalita'");}
            require(v.sketches_[0].segments[0].second.x()>15.5,"drag segue la componente consentita del cursore");
            require(near(v.pointDragPosition_,v.sketches_[0].segments[0].second),"tracking della posizione effettivamente risolta");
            require(timer.elapsed()<3000,"60 eventi di trascinamento vincolato senza stalli");mouse(v,{16,2},2);
            const auto moved=v.sketches_[0].segments[0].second;v.undo();require(near(v.sketches_[0].segments[0].second,{10,0}),"Undo atomico del drag parzialmente vincolato");v.redo();require(near(v.sketches_[0].segments[0].second,moved),"Redo del drag proiettato");
        }
        // Traiettoria curva: il punto resta a distanza10 dal punto fissato.
        {
            SketchObject s;s.segments={{{0,0},{10,0}}};
            s.geometricConstraints={makeConstraint(s,ConstraintType::Fix,{{0,0,0}}),makeConstraint(s,ConstraintType::Distance,{{0,0,0},{0,0,1}})};
            CadViewport v;setup(v,s);mouse(v,{10,0},0);mouse(v,{6,8.7},1);
            require(v.sketches_[0].segments[0].second.y()>7,"punto segue la circonferenza dei gradi di liberta'");
            require(std::abs(pointLength(v.sketches_[0].segments[0].second)-10)<1e-7,"quota di distanza invariata durante drag");mouse(v,{6,8.7},2);
            s.geometricConstraints={makeConstraint(s,ConstraintType::Fix,{{0,0,-1}})};setup(v,s);mouse(v,{10,0},0);mouse(v,{13,3},1);mouse(v,{13,3},2);
            require(near(v.sketches_[0].segments[0].second,{10,0})&&!v.canUndo()&&!v.hasPendingSketchChanges(),"entita' fissa non si sposta e non produce Undo vuoti");
        }
        {
            SketchObject s;s.segments={{{0,0},{10,0}}};CadViewport v;setup(v,s);mouse(v,{5,0},0);require(v.bodyDragSegment_==0,"presa del corpo del segmento");mouse(v,{15,0},1);mouse(v,{15,0},2);
            require(std::abs(v.sketches_[0].segments[0].first.x()-10)<.05&&std::abs(v.sketches_[0].segments[0].second.x()-20)<.05,"traslazione pari alla lunghezza non muove due volte un estremo");
        }
        // Cerchio: solo centro, quadranti in hover, niente punto del raggio.
        CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{0,0},{5,0}};recalculateCurve(circle);
        {
            SketchObject s;s.curves={circle};CadViewport v;setup(v,s);
            require(CadViewport::curveEditMarkers(circle,false)==QVector<QPointF>{{0,0}},"un solo marcatore permanente del cerchio");
            const auto hover=CadViewport::curveEditMarkers(circle,true);require(hover.size()==5&&hover.contains({0,5})&&hover.contains({-5,0}),"quattro quadranti solo in hover");
            require(!CadViewport::isPickableCurvePoint(circle,1),"il raggio interno non e' un punto selezionabile");
            require(v.snapCandidates(s)==QVector<QPointF>{{0,0}},"nessun falso estremo del cerchio fra gli snap");
            int c=-1,k=-1;EditablePointKind kind;
            require(!v.findCurveEditPoint({5,0},c,k,kind),"circonferenza non intercettata da un controllo nascosto");
            mouse(v,{0,5},0);require(v.circleRadiusDrag_==0,"clic sulla circonferenza avvia resize raggio");mouse(v,{0,8},1);
            require(near(v.sketches_[0].curves[0].controlPoints[0],{0,0})&&std::abs(pointLength(v.sketches_[0].curves[0].controlPoints[1])-8)<.05,"resize del cerchio con centro fermo");
            mouse(v,{0,8},2);const auto resized=v.sketches_[0].curves[0].controlPoints;v.undo();require(v.sketches_[0].curves[0].controlPoints==circle.controlPoints,"Undo resize cerchio");v.redo();require(v.sketches_[0].curves[0].controlPoints==resized,"Redo resize cerchio");
            setup(v,s);mouse(v,{0,0},0);mouse(v,{3,4},1);mouse(v,{3,4},2);const auto &moved=v.sketches_[0].curves[0];
            require(pointLength(moved.controlPoints[0])>4.8&&std::abs(pointDistance(moved.controlPoints[0],moved.controlPoints[1])-5)<1e-7,"drag centro trasla il cerchio conservando raggio");
            s.geometricConstraints={makeConstraint(s,ConstraintType::Radius,{{1,0,-1}})};setup(v,s);mouse(v,{0,5},0);require(v.circleRadiusDrag_==0,"presa reale anche del cerchio quotato");mouse(v,{0,8},1);mouse(v,{0,8},2);
            require(near(v.sketches_[0].curves[0].controlPoints[0],{0,0})&&std::abs(pointLength(v.sketches_[0].curves[0].controlPoints[1])-5)<1e-7,"raggio quotato non aggirato muovendo il centro");
            require(!v.canUndo(),"resize bloccato non produce Undo vuoto");
        }
        // Spline chiusa: stessa traslazione dei nodi e delle quattro maniglie.
        for(bool linked:{false,true}){
            CurveObject curve;curve.tool=DrawingTool::Spline;curve.controlPoints={{0,0},{6,0},{6,6},{0,0}};recalculateCurve(curve);const int last=3;
            curve.tangentLinked[0]=curve.tangentLinked[last]=linked;
            // Cuspide iniziale conservata trascinando il nodo, senza imporre C1.
            const auto initial=curve;SketchObject s;s.curves={curve};CadViewport v;setup(v,s);
            mouse(v,{0,0},0);mouse(v,{-2,2},1);mouse(v,{-2,2},2);const auto moved=v.sketches_[0].curves[0];const auto delta=moved.controlPoints[0]-initial.controlPoints[0];
            require(pointLength(delta)>1&&near(moved.controlPoints[0],moved.controlPoints[last]),"nodo di chiusura unico durante drag");
            for(int i:{0,last})require(near(moved.tangentHandles[i].first,initial.tangentHandles[i].first+delta)&&near(moved.tangentHandles[i].second,initial.tangentHandles[i].second+delta),"quattro maniglie seguono il nodo di cucitura");
            v.undo();require(v.sketches_[0].curves[0].controlPoints==initial.controlPoints&&v.sketches_[0].curves[0].tangentHandles==initial.tangentHandles,"Undo nodo chiuso e maniglie");
            // Entrambe le maniglie visibili: muovere l'una ruota anche l'altra
            // se collegate, conservandone la lunghezza; svincolo rispettato.
            for(int index:{0,last}){
                auto copy=initial;const int side=index==0?1:0;const auto other=index==0?initial.tangentHandles[last].first:initial.tangentHandles[0].second;
                CadViewport::moveSplineHandle(copy,index,side,{2,3});const auto opposite=index==0?copy.tangentHandles[last].first:copy.tangentHandles[0].second;
                if(linked){require(std::abs(2*opposite.y()-3*opposite.x())<1e-7&&QPointF::dotProduct(opposite,{2,3})<0,"maniglie visibili della cucitura ruotano insieme");require(std::abs(pointLength(opposite)-pointLength(other))<1e-7,"lunghezza della maniglia accoppiata conservata");}
                else require(opposite==other,"maniglie esplicitamente svincolate restano indipendenti");
            }
            if(linked){
                setup(v,s);const auto start=initial.tangentHandles[0].second;mouse(v,start,0);require(v.draggingControlPoint_&&v.draggingPointKind_==EditablePointKind::TangentOut,"presa reale della maniglia visibile alla chiusura");mouse(v,{2,3},1);mouse(v,{2,3},2);
                const auto dragged=v.sketches_[0].curves[0];const auto a=dragged.tangentHandles[0].second-dragged.controlPoints[0],b=dragged.tangentHandles[last].first-dragged.controlPoints[last];require(std::abs(a.x()*b.y()-a.y()*b.x())<1e-7&&QPointF::dotProduct(a,b)<0,"drag mouse accoppia le maniglie della cucitura");v.undo();require(v.sketches_[0].curves[0].tangentHandles==initial.tangentHandles,"Undo entrambe le maniglie della cucitura");v.redo();require(v.sketches_[0].curves[0].tangentHandles==dragged.tangentHandles,"Redo entrambe le maniglie della cucitura");
            }
            // Il solver conserva chiusura e tangente dopo un target numerico.
            CadViewport::moveSplineHandle(curve,0,1,{2,3});s.curves={curve};const auto r=solveSketch(s,{{{1,0,0},{-1,1}}});
            require(r.ok&&near(s.curves[0].controlPoints[0],s.curves[0].controlPoints[last]),"chiusura mantenuta dal solver anche cambiando un riferimento");
        }
        // La feature conserva il vecchio B-rep fino all'uscita, anche tra piu'
        // operazioni e Undo/Redo. Lo stato transitorio viaggia nella cronologia.
        {
            SketchObject s;s.segments={{{0,0},{5,0}},{{5,0},{5,4}},{{5,4},{0,4}},{{0,4},{0,0}}};CadViewport v;setup(v,s);
            ExtrusionObject body;body.sketchIndex=0;body.distance=3;v.extrusions_={body};buildBody(v.extrusions_[0],0,v.sketches_,v.extrusions_,0);require(bool(v.extrusions_[0].forgeBody),"feature iniziale per test differimento");
            const auto oldBody=v.extrusions_[0].forgeBody;
            v.recordUndo();for(auto &line:v.sketches_[0].segments){line.first.rx()*=1.5;line.second.rx()*=1.5;}v.sketchEdited();
            require(v.extrusions_[0].forgeBody==oldBody&&v.hasPendingSketchChanges(),"prima modifica dello schizzo non rigenera feature");
            v.recordUndo();for(auto &line:v.sketches_[0].segments){line.first.rx()*=4./3;line.second.rx()*=4./3;}v.sketchEdited();
            require(v.extrusions_[0].forgeBody==oldBody,"seconda modifica non rigenera feature");v.undo();require(v.extrusions_[0].forgeBody==oldBody&&v.hasPendingSketchChanges(),"Undo in schizzo resta differito");v.redo();require(v.extrusions_[0].forgeBody==oldBody,"Redo in schizzo resta differito");
            QTemporaryDir folder;const auto path=folder.filePath("pending.prt");require(saveDocumentFile(path,v.currentDocument(),true).isEmpty(),"Salva durante editing senza uscire");DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty(),"riapertura del file salvato durante editing");
            require(!loaded.extrusions[0].forgeBody&&!loaded.extrusions[0].cachedGeometry,"nessuna cache obsoleta nel file salvato");CadViewport reopened;reopened.loadDocument(loaded);require(reopened.extrusions_[0].forgeBody&&std::abs(Kernel::massProperties(*reopened.extrusions_[0].forgeBody).volume-120)<1e-6,"rigenerazione corretta al caricamento del file salvato in editing");
            require(v.sketchMode_&&v.extrusions_[0].forgeBody==oldBody,"Salva non chiude lo schizzo ne' rigenera il modello aperto");
            v.endSketchMode();const auto result=v.extrusions_[0].forgeBody;require(result&&result!=oldBody&&!v.hasPendingSketchChanges(),"rigenerazione all'uscita dallo schizzo");require(std::abs(Kernel::massProperties(*result).volume-120)<1e-6,"modello finale segue lo schizzo modificato");
            v.endSketchMode();require(v.extrusions_[0].forgeBody==result,"nessuna seconda rigenerazione senza nuove modifiche");v.undo();require(std::abs(Kernel::massProperties(*v.extrusions_[0].forgeBody).volume-90)<1e-6,"Undo fuori schizzo rigenera l'istantanea differita");v.redo();require(std::abs(Kernel::massProperties(*v.extrusions_[0].forgeBody).volume-120)<1e-6,"Redo fuori schizzo coerente");
        }
        if(QApplication::arguments().contains(QStringLiteral("--render"))){
            SketchObject s;s.curves={circle};CadViewport v;setup(v,s);v.show();QApplication::processEvents();
            require(v.isValid(),"contesto GL nativo per verifica dei cerchi");
            require(v.grabFramebuffer().save("/tmp/sketch-four-fixes/circle-center.png"),"immagine cerchio solo centro");
            v.sketchHover_={1,0};v.update();QApplication::processEvents();
            require(v.grabFramebuffer().save("/tmp/sketch-four-fixes/circle-hover.png"),"immagine quadranti in hover");
            v.hide();
        }
        std::cout<<"PASS sketch DOF drag, circle center/radius/hover, closed spline seam and deferred rebuild/save/Undo/Redo"<<std::endl;
    }
