// Incluso nel friend ViewportInteractionTest: bordi di corpi filettati.
    static void threadEdges(bool gl) {
        using namespace ForgeCad;
        using namespace Kernel;
        const Body cylinder=makeCylinder(Frame3(),5,10);
        const Body box=makeBox(Frame3(Vec3(-10,-7,-3),Vec3(0,0,1),Vec3(1,0,0)),20,14,3);
        auto base=std::make_shared<const Body>(booleanOperation(cylinder,box,Kernel::BooleanOperation::Unite));
        FaceId side;
        for(auto face:base->faces())if(base->face(face).surface->type()==SurfaceType::Cylinder){side=face;break;}
        require(side.valid(),"fixture cilindro e base rettangolare");
        ThreadParameters parameters;parameters.pitch=1.5;parameters.length=3;
        const auto fin=base->loop(base->face(side).loops.front()).first;
        parameters.face=faceReference(*base,side,base->finPoint(fin,.5));
        QString error;const auto threaded=forgeThread(base,parameters,&error);
        require(threaded && error.isEmpty(),"filettatura reale sulla base rettangolare");
        const auto original=writeBodyBinary(*threaded);
        Gui::SceneRenderer renderer;
        BodyDisplay display;
        QVector<int> straight;
        for(int quality=0;quality<3;++quality) {
            forgeTessellate(*threaded,quality,display);
            const auto hidden=renderer.threadSeamEdges(display,threaded);
            require(!hidden.isEmpty(),"cuciture lisce del filetto restano nascoste");
            straight.clear();
            for(int i=0;i<display.edges.size();++i) {
                const auto &edge=threaded->edge(EdgeId(display.edgeIds[i]));
                const auto middle=edge.curve->point((edge.range.lo+edge.range.hi)*.5);
                if(edge.curve->type()==CurveType::Line && (std::abs(middle.x())>7 || std::abs(middle.y())>6)) {
                    straight.append(i);
                    require(!hidden.contains(i),"bordi rettilinei della base conservati dopo filettatura");
                }
                const auto a=edge.curve->point(edge.range.lo), b=edge.curve->point(edge.range.hi);
                if(norm(b-a)>1e-7 && norm(cross(middle-a,b-a))/norm(b-a)>1e-4)
                    require(!hidden.contains(i),"profili curvi del filetto conservati");
            }
            require(straight.size()==12,"tutti i dodici bordi del parallelepipedo presenti");
            const auto *storage=renderer.threadSeamEdges(display,threaded).constData();
            require(renderer.threadSeamEdges(display,threaded).constData()==storage,"classificazione riusata senza ricalcolo");
            auto legacy=display;legacy.edgeIds.clear();
            require(renderer.threadSeamEdges(legacy,threaded).isEmpty() && renderer.threadSeamEdges(display,{}).isEmpty(),
                    "senza topologia o identificativi si conservano tutti i bordi");
        }
        require(writeBodyBinary(*threaded)==original,"il filtro non modifica la geometria esatta");
        // Le facce B-spline possono avere spigoli veri: il tipo da solo non basta.
        Body sharp=box;
        for(auto face:sharp.faces()) {
            const auto plane=sharp.face(face).surface;
            sharp.face(face).surface=std::make_shared<BSplineSurface>(1,1,
                std::vector<double>{-30,-30,30,30},std::vector<double>{-30,-30,30,30},2,2,
                std::vector<Vec3>{plane->point(-30,-30),plane->point(-30,30),plane->point(30,-30),plane->point(30,30)});
        }
        const auto sharpBody=std::make_shared<const Body>(sharp);BodyDisplay sharpDisplay;forgeTessellate(sharp,1,sharpDisplay);
        require(renderer.threadSeamEdges(sharpDisplay,sharpBody).isEmpty(),"spigoli vivi tra facce B-spline conservati");
        const auto sheet=std::make_shared<const Body>(surface_fixture::patch());BodyDisplay sheetDisplay;forgeTessellate(*sheet,1,sheetDisplay);
        require(renderer.threadSeamEdges(sheetDisplay,sheet).isEmpty(),"bordi liberi delle superfici conservati");
        auto restored=std::make_shared<const Body>(readBodyBinary(original));
        // Una nuova voce puo' riallocare il vettore della cache: confronta
        // copie dei risultati, non riferimenti ottenuti in due chiamate.
        const auto savedSeams=renderer.threadSeamEdges(display,threaded);
        const auto restoredSeams=renderer.threadSeamEdges(display,restored);
        require(restoredSeams==savedSeams,
                "classificazione valida anche sulla geometria riaperta da cache");
        auto changed=display;std::reverse(changed.edges.begin(),changed.edges.end());std::reverse(changed.edgeIds.begin(),changed.edgeIds.end());
        const auto reordered=renderer.threadSeamEdges(changed,threaded);
        for(int i:renderer.threadSeamEdges(display,threaded))require(reordered.contains(display.edges.size()-1-i),
                "cache invalidata quando cambiano ordine e identificativi delle polilinee");
        if(gl) {
            CadViewport view;view.resize(720,720);view.show();QApplication::processEvents();view.makeCurrent();
            QOpenGLFramebufferObject fbo(QSize(900,900),QOpenGLFramebufferObject::CombinedDepthStencil);
            require(fbo.isValid(),"framebuffer per regressione bordi filettatura");
            QMatrix4x4 projection;projection.ortho(-18,18,-18,18,-100,100);
            QMatrix4x4 model;model.rotate(55,1,0,0);model.rotate(25,0,0,1);
            view.overlayRenderer_.setMatrices(projection,model);view.displayCache_.setMatrices(projection,model);
            auto lines=display;lines.vertices.clear();lines.normals.clear();
            const auto capture=[&](bool preview,bool filtered,int mode) {
                fbo.bind();glViewport(0,0,900,900);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
                if(preview) {
                    Gui::SceneRenderer::PreviewStyle style;style.thread=true;style.displayMode=mode;
                    if(filtered)style.threadGeometry=threaded;
                    view.sceneRenderer_.drawFeaturePreview(view.displayCache_,view.overlayRenderer_,lines,{},style);
                } else {
                    Gui::SceneRenderer::BodyEdgeStyle style;style.thread=true;style.displayMode=mode;
                    if(filtered)style.threadGeometry=threaded;
                    view.sceneRenderer_.drawBodyEdges(view.displayCache_,view.overlayRenderer_,lines,style);
                }
                glFinish();const auto result=fbo.toImage();fbo.release();return result;
            };
            const auto all=capture(false,false,2), filtered=capture(false,true,2), preview=capture(true,true,2);
            const auto whiteAt=[&](const QImage &img,int index) {
                const auto &line=display.edges[index];const auto world=(line.first()+line.last())*.5f;
                const auto p=projection*model*QVector4D(world,1);
                const int x=qRound((p.x()/p.w()+1)*450),y=qRound((1-p.y()/p.w())*450);
                for(int a=-2;a<=2;++a)for(int b=-2;b<=2;++b)if(img.rect().contains(x+a,y+b)) {
                    const auto c=img.pixelColor(x+a,y+b);if(c.red()>100 && c.green()>100 && c.blue()>100)return true;
                }
                return false;
            };
            for(int i:straight)require(whiteAt(all,i) && whiteAt(filtered,i) && whiteAt(preview,i),
                    "rendering corpo e anteprima conservano i bordi rettilinei dopo filettatura");
            int suppressed=0;
            for(int i:view.sceneRenderer_.threadSeamEdges(display,threaded))
                if(whiteAt(all,i) && !whiteAt(filtered,i))++suppressed;
            require(suppressed>0 && all!=filtered,"cuciture effettivamente assenti dal framebuffer ombreggiato");
            require(capture(false,true,0)==capture(false,false,0),"wireframe conserva tutte le cuciture per ispezione");
            const auto directory=qEnvironmentVariable("FORGECAD_THREAD_EDGE_QA_DIR");
            if(!directory.isEmpty()){QDir().mkpath(directory);all.save(directory+"/all.png");filtered.save(directory+"/filtered.png");preview.save(directory+"/preview.png");}
            require(glGetError()==GL_NO_ERROR,"disegno bordi senza errori OpenGL");
            // Le cache dei corpi non piu' disegnati si liberano a fine frame.
            view.sceneRenderer_.render(view.size(),[]{});
            require(view.sceneRenderer_.threadEdgeCache_.empty(),"nessuna cache trattenuta da un frame senza corpi filettati");
            view.doneCurrent();view.hide();
        }
        std::cout<<"PASS thread edges: corpo originale, cuciture, spigoli vivi, cache e anteprima"<<std::endl;
    }
