// Incluso nel friend ViewportInteractionTest.
    static void helixStartAngleRegressions() {
        using namespace ForgeCad;
        using namespace ForgeCad::Kernel;
        int cases=0;
        for(int plane=0;plane<5;++plane){
            SketchObject sketch;sketch.plane=plane%3;
            if(plane>=3){
                sketch.customFrame=true;
                sketch.frame.origin[0]=11;sketch.frame.origin[1]=-7;sketch.frame.origin[2]=4;
                const Vec3 normal=plane==3?normalized(Vec3(1,2,3)):Vec3(0,0,-1);
                const Vec3 x=plane==3?normalized(Vec3(2,-1,0)):Vec3(-1,0,0);
                for(int k=0;k<3;++k){sketch.frame.normal[k]=normal[k];sketch.frame.xAxis[k]=x[k];}
            }
            const auto axes=sketchAxes(sketch);
            const QPointF center(2,-3);constexpr double radius=5;
            for(auto tool:{DrawingTool::Circle,DrawingTool::Arc})for(double ray:{-113.,37.,180.}){
                CurveObject circle;circle.tool=tool;circle.controlPoints={center,center+radius*QPointF(std::cos(ray*kPi/180),std::sin(ray*kPi/180))};
                if(tool==DrawingTool::Arc)circle.controlPoints.append(center+QPointF(0,radius));
                recalculateCurve(circle);sketch.curves={circle};
                HelixBase base;QString error;
                require(helixBaseFromSketch(sketch,0,base,&error),"base elica da cerchio/arco orientato");
                require(distance(base.xRef,axes.xDir())<1e-12,"zero angolare allineato a X dello schizzo");
                for(double angle:{-90.,0.,90.,180.,270.,360.})for(bool reverse:{false,true})for(bool left:{false,true})for(bool spiral:{false,true}){
                    HelixParameters p;p.startAngle=angle;p.reverse=reverse;p.leftHanded=left;p.spiral=spiral;
                    p.pitch=2;p.turns=3;p.height=9;p.mode=cases%3;p.taper=4;
                    const auto curve=helixCurve(p,base,&error);require(bool(curve),"costruzione elica/spirale con riferimento agli assi");
                    const auto expected=sketchToWorld(center+radius*QPointF(std::cos(angle*kPi/180),std::sin(angle*kPi/180)),sketch);
                    require(distance(curve->point(curve->domain().lo),expected)<1e-9,"quadrante iniziale indipendente da punto nascosto, piano, verso e inversione asse");
                    double pitch,turns,height;helixDimensions(p,pitch,turns,height);
                    const auto displacement=curve->point(curve->domain().hi)-base.origin;
                    require(std::abs(dot(displacement,axes.zDir())-(reverse?-height:height))<1e-8,"altezza e direzione asse conservate");
                    ++cases;
                }
            }
        }
        // Le basi da corpo mantengono il riferimento modello e il contratto
        // precedente dell'inversione dell'asse.
        for(int source:{1,2}){
            HelixBase base;base.axis={0,0,1};base.xRef={1,0,0};base.radius=5;base.length=7;
            HelixParameters p;p.source=source;p.startAngle=90;p.reverse=true;QString error;
            const auto curve=helixCurve(p,base,&error);
            require(curve&&distance(curve->point(0),Vec3(0,-5,source==2?7:0))<1e-9,"basi da corpo conservano il riferimento esistente");
        }
        // Percorso reale della feature, codec e rigenerazione: il punto del
        // raggio puo' ruotare senza ruotare l'elica associata.
        SketchObject sketch;CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{2,3},{5,7}};recalculateCurve(circle);sketch.curves={circle};
        ExtrusionObject body;body.feature=BodyFeature::Helix;body.sketchIndex=0;body.helix.startAngle=90;body.helix.turns=2;
        buildBody(body,0,{sketch},{},0);
        require(body.curve&&distance(body.curve->point(0),Vec3(2,8,0))<1e-9,"feature elica a90 sul quadrante superiore");
        const auto original=body.curve;
        sketch.curves[0].controlPoints[1]={-2,6};recalculateCurve(sketch.curves[0]);
        buildBody(body,0,{sketch},{},0);require(bool(body.curve),"rigenerazione dopo rotazione punto interno del raggio");
        for(double t:{0.,.3,2.,5.})require(distance(body.curve->point(t),original->point(t))<1e-9,"intera elica invariata ruotando il punto nascosto");
        DocumentState state;state.sketches={sketch};state.extrusions={body};QTemporaryDir folder;
        const auto path=folder.filePath("helix-axes.prt");require(saveDocumentFile(path,state,false).isEmpty(),"salvataggio elica riferita agli assi");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty(),"riapertura elica riferita agli assi");
        CadViewport view;view.loadDocument(loaded);
        require(view.extrusions_[0].curve&&distance(view.extrusions_[0].curve->point(0),Vec3(2,8,0))<1e-9,"rigenerazione da file conserva quadrante e angolo");
        if(QApplication::arguments().contains(QStringLiteral("--render"))){
            view.resize(900,650);view.selectSketch(0);view.endSketchMode();view.setViewNormal(0);view.fitAll();view.show();QApplication::processEvents();
            require(view.isValid()&&view.grabFramebuffer().save("/tmp/helix-start-angle/helix90.png"),"rendering nativo elica90");view.hide();
        }
        std::cout<<"PASS helix start angle: "<<cases<<" combinazioni, rigenerazione e Salva/riapertura"<<std::endl;
    }
