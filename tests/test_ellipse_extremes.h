// Incluso nel friend ViewportInteractionTest: contratto geometrico e interazione reale.
    static void ellipseExtremes(bool gl) {
        using namespace ForgeCad;
        const auto near=[](QPointF a,QPointF b,double tolerance=1e-7) { return pointDistance(a,b)<tolerance; };
        const auto check=[&](const CurveObject &curve) {
            require(curve.tool==DrawingTool::EllipseExtremes && curve.controlPoints.size()==4,"ellisse: quattro estremi reali");
            const auto &p=curve.controlPoints;
            const QPointF center=(p[0]+p[1])/2, a=(p[0]-p[1])/2, b=(p[2]-p[3])/2;
            require(near(center,(p[2]+p[3])/2),"ellisse: centro comune derivato dalle coppie opposte");
            require(pointLength(a)>1e-7 && pointLength(b)>1e-7
                && std::abs(QPointF::dotProduct(a,b))<1e-7*pointLength(a)*pointLength(b),"ellisse: assi non nulli e perpendicolari");
            const auto geometry=curveGeometry(curve);
            require(geometry.size()==1 && geometry[0].curve->type()==Kernel::CurveType::Ellipse,"ellisse: una curva analitica esatta");
            const auto *exact=dynamic_cast<const Kernel::Ellipse<2>*>(geometry[0].curve.get());
            require(exact && near({exact->center().x(),exact->center().y()},center),"ellisse: centro esatto coerente");
            for(const auto &endpoint:p) {
                const QPointF d=endpoint-center;
                const double x=(d.x()*exact->xAxis().x()+d.y()*exact->xAxis().y())/exact->xRadius();
                const double y=(d.x()*exact->yAxis().x()+d.y()*exact->yAxis().y())/exact->yRadius();
                const auto onCurve=exact->point(std::atan2(y,x));
                require(near(endpoint,{onCurve.x(),onCurve.y()}),"ellisse: ciascuno dei quattro estremi appartiene alla curva");
            }
            for(int i=0;i<25;++i) {
                const auto q=geometry[0].curve->point(2*M_PI*i/25);
                const QPointF d=QPointF(q.x(),q.y())-center;
                const double x=QPointF::dotProduct(d,a)/QPointF::dotProduct(a,a);
                const double y=QPointF::dotProduct(d,b)/QPointF::dotProduct(b,b);
                require(std::abs(x*x+y*y-1)<1e-9,"ellisse: equazione analitica indipendente dai campioni di display");
            }
        };
        const QPointF center(14,11);
        for(const QPointF axis:{QPointF(1,0),QPointF(0.6,0.8),QPointF(-0.8,0.6)}) {
            const QPointF perpendicular(-axis.y(),axis.x());
            for(double b:{2.0,9.0,-2.0,-9.0}) {
                auto curve=ellipseFromExtremes(center-4*axis,center+4*axis,center+1.7*axis+b*perpendicular);
                recalculateCurve(curve); require(curve.numericallyValid,"ellisse: creazione valida anche ruotata e con secondo asse maggiore");check(curve);
                require(near(curve.controlPoints[0],center-4*axis) && near(curve.controlPoints[1],center+4*axis)
                    && near(curve.controlPoints[2],center+b*perpendicular) && near(curve.controlPoints[3],center-b*perpendicular),"ellisse: terzo clic proiettato conservando il lato scelto");
                SketchObject sketch;sketch.curves={curve};
                require(analyzeSketch(sketch).degreesOfFreedom==5,"ellisse: otto coordinate meno tre equazioni implicite");
                require(solveSketch(sketch).ok,"ellisse: risoluzione senza vincoli espliciti");check(sketch.curves[0]);
                for(int k=0;k<4;++k) {
                    auto moved=curve;const auto previous=moved.controlPoints[k];
                    moved.controlPoints[k]+=QPointF(0.4,-0.7);
                    const auto requested=moved.controlPoints[k];normalizeEllipseExtremes(moved,k,previous);
                    require(near(moved.controlPoints[k],requested),"ellisse: normalizzazione conserva l'estremo trascinato");check(moved);
                    auto targeted=sketch;const QPointF destination=curve.controlPoints[k]+QPointF(0.5,0.3);
                    require(solveSketch(targeted,{{{1,0,k},destination}}).ok && near(targeted.curves[0].controlPoints[k],destination),"ellisse: solver muove ognuno dei quattro estremi");check(targeted.curves[0]);
                }
            }
        }
        const auto base=ellipseFromExtremes({10,11},{18,11},{14,14});
        for(int k=0;k<4;++k) {
            SketchObject sketch;sketch.curves={base};const ConstraintRef point{1,0,k},origin{2,0,-1};
            QPointF value;require(refPoint(sketch,point,value) && near(value,base.controlPoints[k]),"ellisse: ogni estremo e un riferimento distinto");
            const auto dimensions=applicableConstraints(sketch,{point,origin});
            require(dimensions.contains(ConstraintType::HorizontalDistance) && dimensions.contains(ConstraintType::VerticalDistance)
                && dimensions.contains(ConstraintType::Coincident),"ellisse: ogni estremo offre quote X/Y e coincidenza");
            require(applicableConstraints(sketch,{point}).contains(ConstraintType::Fix),"ellisse: ogni estremo offre Fisso");
            auto x=makeConstraint(sketch,ConstraintType::HorizontalDistance,{point,origin});
            auto y=makeConstraint(sketch,ConstraintType::VerticalDistance,{point,origin});x.value+=0.6;y.value+=0.4;
            sketch.geometricConstraints={x,y};require(solveSketch(sketch).ok,"ellisse: quote guidanti X/Y su ogni estremo");check(sketch.curves[0]);
            require(std::abs(currentMeasure(sketch,x)-x.value)<1e-7 && std::abs(currentMeasure(sketch,y)-y.value)<1e-7,"ellisse: entrambe le quote aggiornano il punto");
            auto fixed=sketch;fixed.geometricConstraints={makeConstraint(fixed,ConstraintType::Fix,{point})};
            require(!solveSketch(fixed,{{point,value+QPointF(3,2)}}).ok,"ellisse: Fisso impedisce un bersaglio incompatibile");
            sketch.curves={base};sketch.segments={{{base.controlPoints[k]+QPointF(1,2)},{35,25}}};
            sketch.geometricConstraints={makeConstraint(sketch,ConstraintType::Coincident,{point,{0,0,0}})};
            require(solveSketch(sketch).ok && near(sketch.curves[0].controlPoints[k],sketch.segments[0].first),"ellisse: coincidenza effettiva su ogni estremo");check(sketch.curves[0]);
            CurveObject p;p.tool=DrawingTool::Point;p.controlPoints={base.controlPoints[k]};sketch.curves={base,p};sketch.geometricConstraints.clear();
            require(applicableConstraints(sketch,{{1,1,0},{1,0,-1}}).contains(ConstraintType::PointOnCurve),"ellisse: disponibile Punto sulla curva");
            const auto on=makeConstraint(sketch,ConstraintType::PointOnCurve,{{1,1,0},{1,0,-1}});
            require(constraintError(sketch,on)<1e-7,"ellisse: ciascun estremo soddisfa Punto sulla curva");
        }
        SketchObject concentric;concentric.curves={base};CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={center,center+QPointF(2,0)};concentric.curves.append(circle);
        require(applicableConstraints(concentric,{{1,0,-1},{1,1,-1}}).contains(ConstraintType::Concentric),"ellisse: concentrico disponibile");
        auto aligned=makeConstraint(concentric,ConstraintType::Concentric,{{1,0,-1},{1,1,-1}});
        require(constraintError(concentric,aligned)<1e-9,"ellisse: concentrico usa il centro derivato, distinto dal primo estremo");
        concentric.curves[1].controlPoints[0]+=QPointF(2,3);concentric.curves[1].controlPoints[1]+=QPointF(2,3);
        concentric.geometricConstraints={aligned,makeConstraint(concentric,ConstraintType::Fix,{{1,1,-1}})};
        require(solveSketch(concentric).ok,"ellisse: concentricita sposta il centro derivato");check(concentric.curves[0]);
        require(near((concentric.curves[0].controlPoints[0]+concentric.curves[0].controlPoints[1])/2,center+QPointF(2,3)),"ellisse: centro derivato risolto correttamente");

        {
            // Il taglio conserva una meta esatta dell'ellisse ruotata, con il secondo asse maggiore.
            const QPointF c(7,-3),a(0.6,0.8),b(-0.8,0.6);
            auto ellipse=ellipseFromExtremes(c-2*a,c+2*a,c+5*b);ellipse.construction=true;
            SketchObject cut;cut.curves={ellipse};const auto original=curveGeometry(ellipse).front();
            const auto point=[&](double t) {const auto p=original.curve->point(t);return QPointF(p.x(),p.y());};
            const QPointF diameter=2*(point(0)-c);cut.segments={{{c-diameter},{c+diameter}}};
            require(trimPreview(cut,{1,0},point(M_PI/2)).size()>2,"ellisse: anteprima Taglia sul nuovo strumento");
            const auto result=trimSketchEntity(cut,{1,0},point(M_PI/2));
            require(result.error.isEmpty() && cut.curves.size()==1 && cut.curves[0].tool==DrawingTool::Nurbs
                && cut.curves[0].construction && !cut.curves[0].weights.isEmpty(),"ellisse: Taglia produce un arco NURBS razionale di costruzione");
            const auto kept=curveGeometry(cut.curves[0]);require(kept.size()==1,"ellisse: arco tagliato esatto valido");
            require(Kernel::distance(kept[0].start(),original.curve->point(M_PI))<1e-7
                && Kernel::distance(kept[0].end(),original.curve->point(2*M_PI))<1e-7,"ellisse: taglio conserva gli estremi corretti della meta opposta al clic");
            for(int i=0;i<=20;++i) {
                const auto p=kept[0].curve->point(kept[0].range.lo+kept[0].range.length()*i/20.0);
                const QPointF d=QPointF(p.x(),p.y())-c;const double x=QPointF::dotProduct(d,a)/2,y=QPointF::dotProduct(d,b)/5;
                require(std::abs(x*x+y*y-1)<1e-10,"ellisse: arco NURBS tagliato sulla conica originale");
            }
            Kernel::ProfileLoop half;half.segments=kept;
            const auto start=kept[0].start(),end=kept[0].end();
            half.segments.push_back({std::make_shared<Kernel::Line<2>>(end,start-end),{0,Kernel::distance(start,end)}});
            require(std::abs(std::abs(Kernel::signedArea(half))-5*M_PI)<1e-8,"ellisse: area esatta della meta tagliata chiusa dal diametro");
        }

        const auto setup=[](CadViewport &v) {v.resize(1000,750);v.createSketch(0,QStringLiteral("Ellisse dagli estremi"));v.geometrySnap_=v.gridSnap_=v.originSnap_=false;v.history_.clear();};
        const auto click=[](CadViewport &v,QPointF p) {
            const auto pixel=v.projectWorldPoint(v.mapSketchPoint(p,v.sketches_[0]));
            const auto actual=v.screenToSketchPoint(pixel.toPoint());
            QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
            return actual;
        };
        CadViewport view;setup(view);view.setDrawingTool(DrawingTool::EllipseExtremes);
        const auto firstClick=click(view,{10,11});click(view,{10,11});
        require(view.curveControlPoints_.size()==1 && !view.canUndo(),"ellisse: estremi coincidenti non avanzano e non creano Undo");
        const auto secondClick=click(view,{18,11});view.cursorSketchPoint_={16,8};CurveObject preview;
        require(view.previewCurve(preview) && preview.controlPoints.size()==4,"ellisse: anteprima a quattro estremi dopo due clic");check(preview);
        require(!view.previewMeasures().isEmpty(),"ellisse: misure disponibili durante l'anteprima");
        click(view,{16,11});require(view.curveControlPoints_.size()==2 && !view.canUndo(),"ellisse: terzo clic sull'asse non crea curva degenerata");
        const auto thirdClick=click(view,{16,8});require(view.sketches_[0].curves.size()==1 && view.curveControlPoints_.isEmpty(),"ellisse: tre clic confermano un'unica curva");
        const auto created=view.sketches_[0].curves[0];check(created);
        // Gli eventi del viewport usano pixel interi: il bersaglio geometrico e la loro intersezione sul piano.
        const QPointF clickCenter=(firstClick+secondClick)/2,clickAxis=secondClick-firstClick;
        const QPointF normal(-clickAxis.y(),clickAxis.x());
        const QPointF expectedB0=clickCenter+QPointF::dotProduct(thirdClick-clickCenter,normal)/QPointF::dotProduct(normal,normal)*normal;
        require(near(created.controlPoints[0],firstClick,1e-5) && near(created.controlPoints[1],secondClick,1e-5)
            && near(created.controlPoints[2],expectedB0,1e-5) && near(created.controlPoints[3],2*clickCenter-expectedB0,1e-5),"ellisse: terzo clic negativo e centro conseguente");
        view.undo();require(view.sketches_[0].curves.isEmpty(),"ellisse: Undo della creazione atomico");view.redo();
        require(view.sketches_[0].curves.size()==1 && view.sketches_[0].curves[0].controlPoints==created.controlPoints,"ellisse: Redo conserva i quattro estremi");
        view.setDrawingTool(DrawingTool::Select);
        for(int k=0;k<4;++k) {
            const auto p=view.sketches_[0].curves[0].controlPoints[k];ConstraintRef ref;
            require(view.dimensionRefAt(p,ref) && ref==ConstraintRef{1,0,k},"ellisse: picking quota restituisce ogni estremo");
            require(view.snapCandidates(view.sketches_[0]).contains(p),"ellisse: tutti gli estremi disponibili per snap");
            view.geometrySnap_=true;require(near(view.snapPoint(p+QPointF(0.01,0.01)),p),"ellisse: snap reale a ogni estremo");view.geometrySnap_=false;
            int curve=-1,control=-1;EditablePointKind kind=EditablePointKind::Control;
            require(view.findCurveEditPoint(p,curve,control,kind) && curve==0 && control==k,"ellisse: tutte le maniglie sono trascinabili");
            const auto pixel=view.projectWorldPoint(view.mapSketchPoint(p,view.sketches_[0]));
            const auto target=view.projectWorldPoint(view.mapSketchPoint(p+QPointF(0.7,0.4),view.sketches_[0]));
            QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);view.mousePressEvent(&press);
            QMouseEvent move(QEvent::MouseMove,target,target,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);view.mouseMoveEvent(&move);
            QMouseEvent release(QEvent::MouseButtonRelease,target,target,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);view.mouseReleaseEvent(&release);
            require(pointDistance(p,view.sketches_[0].curves[0].controlPoints[k])>0.5,"ellisse: movimento mouse reale di ogni estremo");check(view.sketches_[0].curves[0]);
            view.undo();require(view.sketches_[0].curves[0].controlPoints==created.controlPoints,"ellisse: Undo trascinamento conserva la definizione");
            view.redo();check(view.sketches_[0].curves[0]);view.undo();
        }
        view.setDrawingTool(DrawingTool::EllipseExtremes);click(view,{1,2});click(view,{7,2});view.cursorSketchPoint_={3,5};
        require(view.previewCurve(preview),"ellisse: anteprima nuova costruzione");QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);view.keyPressEvent(&escape);
        require(view.curveControlPoints_.isEmpty() && view.sketches_[0].curves.size()==1 && !view.previewCurve(preview),"ellisse: Esc annulla anteprima e conserva la curva confermata");

        DocumentState state;SketchObject persisted;persisted.curves={base};
        for(int k=0;k<4;++k) {
            const ConstraintRef point{1,0,k};
            persisted.geometricConstraints.append(makeConstraint(persisted,ConstraintType::Fix,{point}));
            persisted.geometricConstraints.append(makeConstraint(persisted,ConstraintType::HorizontalDistance,{point,{2,0,-1}}));
            persisted.geometricConstraints.append(makeConstraint(persisted,ConstraintType::VerticalDistance,{point,{2,0,-1}}));
        }
        CurveObject legacy;legacy.tool=DrawingTool::Ellipse;legacy.controlPoints={{-10,-7},{-5,-7},{-10,-5}};persisted.curves.append(legacy);state.sketches={persisted};
        QTemporaryDir directory;const auto path=directory.filePath(QStringLiteral("ellisse-estremi.prt"));DocumentState restored;
        require(saveDocumentFile(path,state,false).isEmpty() && loadDocumentFile(path,restored).isEmpty(),"ellisse: salvataggio e riapertura .prt");
        require(restored.sketches.size()==1 && restored.sketches[0].curves[0].controlPoints==base.controlPoints
            && restored.sketches[0].curves[1].tool==DrawingTool::Ellipse && restored.sketches[0].curves[1].controlPoints==legacy.controlPoints,"ellisse: roundtrip conserva definizione nuova e legacy");
        require(restored.sketches[0].geometricConstraints.size()==12 && analyzeSketch(restored.sketches[0]).degreesOfFreedom==5,"ellisse: tutti i vincoli dei quattro estremi persistono, legacy libera");
        for(int i=0;i<12;++i)require(restored.sketches[0].geometricConstraints[i].first==ConstraintRef{1,0,i/3}
            && constraintError(restored.sketches[0],restored.sketches[0].geometricConstraints[i])<1e-7,"ellisse: riferimenti dei quattro estremi preservati");
        QByteArray data;require(encodeDocument(state,data,false).isEmpty() && qFromBigEndian<quint16>(data.constData()+4)==70,"ellisse: nuovo formato nativo 70");
        QByteArray historicalHash="immutato";
        require(!encodeDocumentDefinition66ForHash(state,historicalHash).isEmpty() && historicalHash=="immutato","ellisse: hash formato 66 rifiuta la nuova semantica senza alterare il risultato");
        auto oldHeader=data;qToBigEndian<quint16>(69,oldHeader.data()+4);const auto identity=restored.version.documentId;
        require(!decodeDocument(oldHeader,restored).isEmpty() && restored.version.documentId==identity,"ellisse: enum nuova rifiutata in formato precedente senza mutare il documento");
        state.sketches[0].curves={legacy};state.sketches[0].geometricConstraints.clear();require(encodeDocument(state,data,false).isEmpty(),"ellisse: fixture legacy");
        qToBigEndian<quint16>(69,data.data()+4);require(decodeDocument(data,restored).isEmpty()
            && restored.sketches[0].curves[0].controlPoints==legacy.controlPoints,"ellisse: lettura formato 69 conserva il centro storico");
        for(double accuracy:{Kernel::kLinearResolution,1e-8}) {
            DocumentState historical;historical.calculationAccuracy=accuracy;
            historical.version.checkinId=QUuid::createUuid();historical.lengthUnit=LengthUnit::Inch;
            historical.orientation.right[0]=0;historical.orientation.right[1]=1;
            historical.orientation.up[0]=-1;historical.orientation.up[1]=0;
            SketchObject sketch;sketch.name=QStringLiteral("Ellisse storica");sketch.visible=false;sketch.curves={legacy};historical.sketches={sketch};
            ExtrusionObject box;box.feature=BodyFeature::Primitive;box.featureId=90701;
            box.primitive.kind=PrimitiveKind::Box;box.primitive.size[0]=10;box.primitive.size[1]=6;box.primitive.size[2]=4;historical.extrusions={box};
            normalizeModelHistory(historical);require(historical.modelBodies.size()==1,"ellisse: fixture impronta69 con corpo normalizzato");
            auto normalized=historical;normalized.version.checkinId={};normalized.baseCheckinId={};
            normalized.orientation=AxesOrientation{};normalized.lengthUnit=LengthUnit::Millimeter;
            for(auto &feature:normalized.extrusions)feature.forgeBody.reset();
            QByteArray definition69;require(encodeDocumentDefinition69ForHash(normalized,definition69).isEmpty()
                && qFromBigEndian<quint16>(definition69.constData()+4)==69,"ellisse: impronta storica serializzata nel formato69 effettivo");
            const auto hash69=QCryptographicHash::hash(definition69,QCryptographicHash::Sha256);bool matches=false;
            require(drawingModelDefinitionMatches(historical,hash69,matches).isEmpty() && matches,"ellisse: tavola69 riconosce la stessa geometria anche con precisione personalizzata");
            for(int presentation=0;presentation<3;++presentation) {
                auto changed=historical;
                if(presentation==0)changed.sketches[0].visible=!changed.sketches[0].visible;
                if(presentation==1)changed.extrusions[0].visible=!changed.extrusions[0].visible;
                if(presentation==2)changed.modelBodies[0].visible=!changed.modelBodies[0].visible;
                require(drawingModelDefinitionMatches(changed,hash69,matches).isEmpty() && matches,"ellisse: impronta69 riconosce i cambi di sola visibilita");
            }
            auto canonical69=normalized;
            for(auto &item:canonical69.sketches)item.visible=true;
            for(auto &item:canonical69.extrusions)item.visible=true;
            for(auto &item:canonical69.modelBodies)item.visible=true;
            QByteArray canonicalBytes;require(encodeDocumentDefinition69ForHash(canonical69,canonicalBytes).isEmpty(),"ellisse: fixture impronta69 con visibilita canonica");
            auto hidden=historical;hidden.extrusions[0].visible=false;hidden.modelBodies[0].visible=false;
            require(drawingModelDefinitionMatches(hidden,QCryptographicHash::hash(canonicalBytes,QCryptographicHash::Sha256),matches).isEmpty()
                && matches,"ellisse: impronta69 canonica riconosce piu cambi simultanei di visibilita");
            auto changed=historical;changed.sketches[0].curves[0].controlPoints[1]+=QPointF(1,0);
            require(drawingModelDefinitionMatches(changed,hash69,matches).isEmpty() && !matches,"ellisse: impronta69 rileva la modifica di un semiasse");
            changed=historical;changed.calculationAccuracy*=0.1;
            require(drawingModelDefinitionMatches(changed,hash69,matches).isEmpty() && !matches,"ellisse: impronta69 rileva la modifica della precisione");
            for(bool nested:{false,true}) {
                changed=historical;
                if(nested) {
                    auto section=std::make_shared<SketchPierceData>();section->curves={base};
                    SketchConstraint pierce;pierce.type=ConstraintType::Pierce;pierce.first={1,0,1};pierce.pierce=section;
                    changed.sketches[0].geometricConstraints.append(pierce);
                } else changed.sketches[0].curves.append(base);
                QByteArray untouched="immutato";
                require(!encodeDocumentDefinition69ForHash(changed,untouched).isEmpty() && untouched=="immutato","ellisse: codec69 rifiuta la nuova ellisse anche nelle sezioni Pierce senza mutare l'output");
                require(drawingModelDefinitionMatches(changed,hash69,matches).isEmpty() && !matches,"ellisse: nessun falso match69 dopo aggiunta della nuova ellisse");
            }
        }
        for(const auto &points:QVector<QVector<QPointF>>{{{1,1},{1,1},{1,3},{1,-1}},{{1,1},{5,1},{3,1},{3,1}},{{1,1},{5,1},{3,std::numeric_limits<double>::infinity()},{3,-2}}}) {
            CurveObject invalid;invalid.tool=DrawingTool::EllipseExtremes;invalid.controlPoints=points;recalculateCurve(invalid);
            require(!invalid.numericallyValid && curveGeometry(invalid).empty(),"ellisse: assi nulli e numeri non finiti non producono geometria");
        }
        PdfWindow window;window.viewport_->createSketch(0,QStringLiteral("Comando ellisse"));bool actionFound=false;
        for(auto *action:window.findChildren<QAction *>())if(action->text().contains(QStringLiteral("Ellisse"),Qt::CaseInsensitive)
            && action->text().contains(QStringLiteral("estrem"),Qt::CaseInsensitive)) {
            actionFound=true;require(!action->icon().isNull(),"ellisse: icona del nuovo comando");action->trigger();
            require(window.viewport_->drawingTool_==DrawingTool::EllipseExtremes,"ellisse: comando UI collegato allo strumento");
        }
        require(actionFound,"ellisse: comando dagli estremi presente nell'interfaccia");
        if(gl) {view.show();QApplication::processEvents();require(view.isValid(),"ellisse: contesto OpenGL nativo valido");view.hide();}
        std::cout<<"PASS ellipse extremes: exact geometry, 5 DOF, all four references, constraints, mouse, snap, Undo/Redo, codec70/legacy and command"<<std::endl;
    }
