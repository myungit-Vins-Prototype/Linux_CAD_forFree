// Incluso nel friend ViewportInteractionTest: strumenti e riferimenti scelti dall'utente.
    static void sketchToolsReferences(bool gl) {
        using namespace ForgeCad;
        using namespace Kernel;
        const auto near=[](QPointF a,QPointF b){return QLineF(a,b).length()<1e-7;};
        const auto setup=[](CadViewport &v) {
            v.resize(1000,750);v.createSketch(0,QStringLiteral("Schizzo verifica"));
            v.geometrySnap_=v.originSnap_=false;
            v.gridSnap_=true; // la griglia rende esatti i punti interi nonostante il clic al pixel
            v.axesVisible_=false;v.referencePlanesVisible_=false;v.history_.clear();v.camera_.zoom_=18;
        };
        const auto click=[](CadViewport &v,QPointF p) {
            const auto pixel=v.projectWorldPoint(v.mapSketchPoint(p,v.sketches_[v.activeSketch_]));
            QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            v.mousePressEvent(&press);
            QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            v.mouseReleaseEvent(&release);
        };
        {
            CadViewport v;setup(v);v.setDrawingTool(DrawingTool::ThreePointCircle);
            click(v,{5,0});click(v,{0,5});v.cursorSketchPoint_={-5,0};
            CurveObject preview;require(v.previewCurve(preview) && preview.tool==DrawingTool::Circle
                && near(preview.controlPoints[0],{}),"anteprima cerchio3 con circocentro esatto");
            click(v,{-5,0});
            require(v.sketches_[0].curves.size()==1,"tre clic creano un solo cerchio");
            const auto original=v.sketches_[0].curves[0];
            require(original.tool==DrawingTool::Circle && original.controlPoints.size()==2
                && near(original.controlPoints[0],{}) && std::abs(QLineF(original.controlPoints[0],original.controlPoints[1]).length()-5)<1e-7,
                "cerchio3 salvato come cerchio analitico ordinario");
            require(v.sketches_[0].geometricConstraints.isEmpty() && analyzeSketch(v.sketches_[0]).degreesOfFreedom==3,
                "nessun vincolo aggiunto al centro calcolato del cerchio3");
            v.undo();require(v.sketches_[0].curves.isEmpty(),"Undo cerchio3 atomico");
            v.redo();require(v.sketches_[0].curves[0].controlPoints==original.controlPoints,"Redo cerchio3");
            QByteArray bytes;DocumentState loaded;
            require(encodeDocument(v.currentDocument(),bytes,false).isEmpty() && decodeDocument(bytes,loaded).isEmpty()
                && loaded.sketches[0].curves[0].tool==DrawingTool::Circle,"codec cerchio3");
            ExtrusionObject extrusion;extrusion.sketchIndex=0;extrusion.distance=2;
            buildBody(extrusion,0,v.sketches_,{},0);
            require(extrusion.error.isEmpty() && extrusion.forgeBody
                && std::abs(massProperties(*extrusion.forgeBody).volume-50*M_PI)<1e-6,"cerchio3 usabile come profilo esatto");
            if(gl) {
                v.show();QApplication::processEvents();require(v.isValid(),"contesto OpenGL per strumenti schizzo");
                v.setDrawingTool(DrawingTool::ThreePointArc);v.curveControlPoints_={{-6,-3},{6,-3}};
                v.cursorSketchPoint_={0,-6};v.lastMousePosition_={500,550};
                v.update();QApplication::processEvents();
                require(v.grabFramebuffer().save("/tmp/sketch-tools-arc-three-points.png"),"cattura anteprima arco3");v.hide();
            }
        }
        {
            CadViewport v;setup(v);v.geometrySnap_=true;
            auto &s=v.sketches_[0];
            for(const QPointF &p:{QPointF(5,0),QPointF(0,5),QPointF(-5,0)}) {
                CurveObject point;point.tool=DrawingTool::Point;point.controlPoints={p};recalculateCurve(point);
                s.curves.append(point);
                s.geometricConstraints.append(makeConstraint(s,ConstraintType::Fix,{{1,int(s.curves.size())-1,0}}));
            }
            v.setDrawingTool(DrawingTool::ThreePointCircle);click(v,{5,0});click(v,{0,5});click(v,{-5,0});
            require(v.sketches_[0].curves.size()==4 && v.sketches_[0].geometricConstraints.size()==6,
                "cerchio3 conserva i tre punti notevoli scelti senza vincolare il centro");
            require(solveSketch(v.sketches_[0]).ok && analyzeSketch(v.sketches_[0]).fullyDefined(),
                "cerchio3 definito dai tre punti sorgenti");
        }
        {
            CadViewport v;setup(v);v.setDrawingTool(DrawingTool::ThreePointCircle);
            click(v,{1,1});click(v,{3,1});click(v,{5,1});
            require(v.sketches_[0].curves.isEmpty() && !v.canUndo() && v.curveControlPoints_.size()==2,
                "punti allineati rifiutati senza geometria o Undo");
            click(v,{3,3});require(v.sketches_[0].curves.size()==1,"terzo punto sostituibile dopo errore");
            v.setDrawingTool(DrawingTool::ThreePointArc);v.curveControlPoints_={{-5,0},{5,0}};v.cursorSketchPoint_={0,5};
            require(!v.previewMeasures().isEmpty() && !v.previewMeasures().join(' ').contains("R ="),"badge arco3 senza raggio");
        }
        {
            CadViewport v;setup(v);v.setDrawingTool(DrawingTool::Line);
            require(v.drawingTool_==DrawingTool::Polyline,"vecchio comando Linea sostituito dalla Polilinea");
            click(v,{-4,-3});click(v,{2,-3});click(v,{2,4});
            require(v.sketches_[0].segments.size()==2 && v.hasPendingPoint_,"polilinea continua dopo ogni segmento");
            int joins=0;for(const auto &c:v.sketches_[0].geometricConstraints)joins+=c.type==ConstraintType::Coincident;
            require(joins==1,"un solo collegamento fra i segmenti della polilinea");
        }
        {
            CadViewport v;setup(v);v.geometrySnap_=v.originSnap_=true;
            ExtrusionObject cylinder;cylinder.feature=BodyFeature::Imported;cylinder.name="Cilindro";cylinder.featureId=801;
            cylinder.forgeBody=std::make_shared<const Body>(makeCylinder(Frame3(),5,3));
            v.endSketchMode();v.sketches_.clear();v.extrusions_={cylinder};
            FaceHit face;
            for (const auto id:cylinder.forgeBody->faces()) {
                const auto &surface=*cylinder.forgeBody->face(id).surface;
                if(surface.type()!=SurfaceType::Plane)continue;
                const auto &frame=static_cast<const Plane &>(surface).frame();
                if(frame.zDir().z()<0.9)continue;
                face.face=id.index;face.planar=true;
                for(int i=0;i<3;++i){face.point[i]=frame.origin()[i];face.normal[i]=frame.zDir()[i];}
                break;
            }
            require(face.planar && v.createFaceSketch(0,face,"Schizzo su faccia circolare")>=0,"schizzo sulla faccia circolare reale");
            v.geometrySnap_=v.originSnap_=true;v.gridSnap_=false;
            v.snapPoint({0,0});require(v.lastSnapNote_.contains("Origine") && !v.lastSnapExternal_,"badge sceglie origine davanti al centro del bordo circolare");
            auto &s=v.sketches_[v.activeSketch_];s.segments={{{0,0},{2,1}}};
            v.recordPointCoincidences(s,{0,0,0},{0,0});
            require(s.geometricConstraints.size()==1 && s.geometricConstraints[0].second.kind==2 && s.linkedReferences.isEmpty(),
                "origine: un solo vincolo e nessun riferimento al centro del cerchio");
            v.endSketchMode();v.removeBodies({0});
            require(v.sketches_[0].supportError.contains("Cilindro") && v.sketches_[0].supportError.contains("Faccia F"),
                "eliminazione dell'appoggio identifica funzione e faccia");
        }
        {
            CadViewport v;setup(v);v.geometrySnap_=true;
            auto &s=v.sketches_[0];s.segments={{{2,3},{4,5}},{{2,3},{7,4}},{{2,3},{1,6}}};
            CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{2,3},{5,3}};recalculateCurve(circle);s.curves={circle};
            v.recordPointCoincidences(s,{0,2,0},{2,3});
            require(s.geometricConstraints.size()==1 && s.geometricConstraints[0].second==ConstraintRef{0,0,0},
                "estremi e centro sovrapposti producono un solo aggancio locale");
        }
        {
            CadViewport v;setup(v);SketchObject source;source.storyId=811;source.name="Profilo circolare";
            CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{8,6},{11,6}};recalculateCurve(circle);
            source.curves={circle,circle};v.sketches_.prepend(source);v.activeSketch_=1;
            v.sketches_[1].storyId=812;v.sketchReferenceCacheValid_=false;v.geometrySnap_=true;v.originSnap_=false;
            v.snapPoint({8,6});
            require(v.lastSnapNote_.contains("Centro") && v.lastSnapNote_.contains("Profilo circolare")
                && v.lastSnapNote_.contains("Curva 1"),"badge identifica centro, sorgente ed entita associativa");
            auto &s=v.sketches_[1];s.segments={{{8,6},{10,8}}};v.recordPointCoincidences(s,{0,0,0},{8,6});
            require(s.linkedReferences.size()==1 && s.geometricConstraints.size()==1,"una sola sorgente circolare sovrapposta viene collegata");
            require(describeConstraint(s,s.geometricConstraints[0]).contains("Profilo circolare"),"nome sorgente anche nell'elenco vincoli");
        }
        {
            CadViewport v;v.resize(800,600);SketchObject s;s.storyId=823;s.name="Schizzo due sorgenti";
            for(int i=0;i<2;++i) {
                ExtrusionObject body;body.feature=BodyFeature::Imported;body.featureId=821+i;
                body.name=i?"Base da mantenere":"Base da cancellare";
                body.forgeBody=std::make_shared<const Body>(makeBox(Frame3(Vec3(10*i,0,0),Vec3(1,0,0),Vec3(0,1,0)),3,4,5));
                body.importData=QByteArray::fromStdString(writeStep({{"Base",*body.forgeBody,false,{0,0,0},nullptr,{}}}));v.extrusions_.append(body);
                GeometryRef ref;ref.kind=3;ref.index=i;ref.featureId=body.featureId;
                ref.point=vertexReference(*body.forgeBody,body.forgeBody->vertices().front());
                QVector<SketchEntity> created;
                require(appendLinkedSketchReference(s,ref,{},v.extrusions_,false,true,100,&created).isEmpty(),"due sorgenti associative distinte");
                const QPointF p=s.segments[created[0].index].first;const int local=s.segments.size();
                s.segments.append({p,p+QPointF(2,1)});
                s.geometricConstraints.append(makeConstraint(s,ConstraintType::Coincident,{{0,local,0},{0,created[0].index,0}}));
            }
            v.sketches_={s};const auto old=v.sketches_[0];v.recordUndo();v.removeBodies({0});
            const auto &broken=v.sketches_[0];
            require(broken.referenceError.contains("Base da cancellare") && broken.referenceError.contains("Vincoli: 1")
                && !broken.referenceError.contains("Base da mantenere"),"eliminazione segnala sorgente esatta e solo vincolo rotto");
            require(describeConstraint(broken,broken.geometricConstraints[0]).contains("ROTTO")
                && !describeConstraint(broken,broken.geometricConstraints[1]).contains("ROTTO"),"il vincolo sano resta riconoscibile");
            require(old.linkedReferences[0]->error.isEmpty(),"diagnostica non modifica istantanea Undo");
            QByteArray bytes;DocumentState loaded;
            require(encodeDocument(v.currentDocument(),bytes,false).isEmpty() && decodeDocument(bytes,loaded).isEmpty(),"salvataggio schizzo con riferimento rotto");
            require(loaded.sketches[0].linkedReferences[0]->sourceLabel.contains("Base da cancellare"),"nome della sorgente eliminata persistente nel codec68");
            refreshLinkedSketchReferences(loaded.sketches[0],loaded.sketches,loaded.extrusions);
            require(loaded.sketches[0].referenceError.contains("Base da cancellare"),"riapertura conserva nome della sorgente mancante");
            v.undo();require(v.extrusions_.size()==2 && v.sketches_[0].referenceError.isEmpty(),"Undo ripristina sorgente e vincoli");
            v.redo();require(v.sketches_[0].referenceError.contains("Base da cancellare"),"Redo mantiene diagnostica identificabile");
            auto allBroken=old;refreshLinkedSketchReferences(allBroken,{},{});
            require(allBroken.referenceError.contains("Base da cancellare") && allBroken.referenceError.contains("Base da mantenere"),
                "piu sorgenti rotte vengono tutte elencate");
        }
        {
            QSettings().setValue("toolbar/custom",QStringList{"Schizzo/Strumento geometrico/Linea","Schizzo/Strumento geometrico/Polilinea"});
            const QJsonArray ribbon{QJsonObject{{"id","sketch"},{"groups",QJsonArray{QJsonObject{{"name","Disegna"},
                {"items",QJsonArray{"Schizzo/Strumento geometrico/Linea"}}}}}}};
            QSettings().setValue("ribbon/layout",QJsonDocument(ribbon).toJson(QJsonDocument::Compact));
            PdfWindow window;bool circle3=false,line=false;
            for (auto *action:window.findChildren<QAction *>()) {
                circle3=circle3 || action->text()==QStringLiteral("Cerchio per tre punti");
                line=line || action->text()==QStringLiteral("Linea");
            }
            require(circle3 && !line,"menu aggiornato: cerchio3 disponibile e comando Linea rimosso");
            require(QSettings().value("toolbar/custom").toStringList()==QStringList{"Schizzo/Strumento geometrico/Polilinea"},
                "barra personalizzata migra Linea a Polilinea senza duplicati");
            require(QSettings().value("ribbon/layout").toByteArray().contains("Polilinea"),"ribbon personalizzata conserva il comando sostituito");
            window.documentModified_=false;
        }
        std::cout<<"PASS sketch tools, unique point attachments, circular face origin, source labels, broken references, codec and Undo/Redo"<<std::endl;
    }
