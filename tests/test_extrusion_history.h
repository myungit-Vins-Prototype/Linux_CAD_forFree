#ifndef FORGECAD_TEST_EXTRUSION_HISTORY_H
#define FORGECAD_TEST_EXTRUSION_HISTORY_H

static void extrusionHistoryTest() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto rectangle = [](double x, double y, double w, double h) {
        SketchObject sketch; sketch.plane=0;
        const QPointF points[]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
        for(int i=0;i<4;++i)sketch.segments.append({points[i],points[(i+1)%4]});
        return sketch;
    };
    const auto imported = [](const Body &body) {
        ExtrusionObject root;root.feature=BodyFeature::Imported;root.featureId=97000;
        ExchangeBody exchange;exchange.name="limite";exchange.body=body;
        root.importData=QByteArray::fromStdString(writeStep({exchange}));root.importSource=QStringLiteral("fixture.step");
        return root;
    };
    QTemporaryDir directory;
    require(directory.isValid(),"directory temporanea per estrusioni dichiarate");
    for(int mode=0;mode<12;++mode) {
        DocumentState definition;
        SketchObject sketch=rectangle(0,0,4,4);
        ExtrusionObject feature;feature.feature=BodyFeature::Extrusion;feature.featureId=97001;
        feature.sketchIndex=0;feature.distance=2;
        if(mode==1)feature.distance=-2;
        if(mode==2)feature.extrudeSides=1;
        if(mode==3||mode==4){feature.extrudeSides=2;feature.distance2=0.75;if(mode==4)feature.distance=-2;}
        if(mode==5){CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{2,2},{3,2}};sketch.curves={circle};}
        if(mode==6||mode==7){const auto second=rectangle(8,0,2,2);sketch.segments+=second.segments;}
        if(mode==7){feature.extrudeSurface=true;sketch.segments.append({{12,0},{13,1}});sketch.segments.append({{13,1},{14,1}});}
        if(mode==8){sketch.segments={{{0,0},{2,0}},{{2,3},{2,0}}};feature.distance=-2;}
        if(mode==9){sketch.segments.clear();CurveObject ellipse;ellipse.tool=DrawingTool::Ellipse;ellipse.controlPoints={{0,0},{3,0},{0,2}};sketch.curves={ellipse};}
        if(mode==10||mode==11) {
            ExtrusionObject root;root.feature=BodyFeature::Primitive;root.featureId=97000;
            root.primitive.size[0]=root.primitive.size[1]=4;root.primitive.size[2]=2;
            definition.extrusions.append(root);
            sketch=rectangle(1,1,2,2);feature.mergeBodies={0};feature.mergeOperation=mode==10?1:2;
            sketch.customFrame=true;
            if(mode==10){sketch.frame.origin[2]=1;feature.distance=3;}
            else {sketch.frame.origin[2]=-1;feature.distance=4;}
        }
        definition.sketches={sketch};definition.extrusions.append(feature);
        CadViewport view;view.loadDocument(definition);
        const auto original=view.currentDocument();const int index=original.extrusions.size()-1;
        const auto body=original.extrusions[index].forgeBody;
        require(body&&original.extrusions[index].error.isEmpty(),"estrusione della matrice costruita");
        const auto bytes=writeBodyBinary(*body);const auto names=elementNamesOf(*body);
        require(bool(names),"nomi dell'estrusione disponibili");
        const auto verify=[&](const DocumentState &state) {
            const auto shape=state.extrusions[index].forgeBody;
            require(shape&&state.extrusions[index].error.isEmpty()&&writeBodyBinary(*shape)==bytes,"geometria estrusione persistente");
            const auto history=featureHistoryOf(*shape);require(bool(history),"storia dell'estrusione disponibile");
            for(FaceId face:shape->faces())require(history->isDeclared(EntityKind::Face,face.index),"tutte le facce dell'estrusione dichiarate");
            const auto metadata=declaredHistorySnapshot(state.extrusions[index],index,state.extrusions);
            require(metadata.history&&metadata.labels,"etichette emesse presenti nella cache del registro");
            bool side=false;
            for(const auto &cell:*metadata.labels)for(const auto &label:cell)side=side||label.rfind("side.",0)==0;
            require(side,"fianchi riferiti ai tratti dello schizzo");
            const auto next=elementNamesOf(*shape);
            require(next&&next->faces==names->faces&&next->edges==names->edges&&next->vertices==names->vertices,
                "tutti i nomi dell'estrusione conservati dopo riapertura e rigenerazione");
        };
        verify(original);
        const auto history=featureHistoryOf(*body);
        view.setHistoryPosition(index);view.setHistoryPosition(index+1);
        require(view.extrusions()[index].forgeBody==body&&featureHistoryOf(*body)==history,"rollback dell'estrusione senza ricalcolo");
        const QString path=directory.filePath(QStringLiteral("estrusione-%1.prt").arg(mode));
        require(saveDocumentFile(path,original,true).isEmpty(),"salvataggio etichette emesse");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty()&&loaded.extrusions[index].cachedGeometry,"estrusione ripristinata dalla cache 7");
        CadViewport reopened;reopened.loadDocument(loaded);verify(reopened.currentDocument());
        require(reopened.rebuildAllFeatures()==0,"rigenerazione estrusione riaperta");verify(reopened.currentDocument());
        if(mode<10) {
            auto changed=original;changed.extrusions[index].distance*=1.5;
            for(auto &f:changed.extrusions)f.cachedGeometry=false;
            CadViewport longer;longer.loadDocument(changed);
            const auto next=elementNamesOf(*longer.extrusions()[index].forgeBody);
            require(next&&next->faces==names->faces&&next->edges==names->edges&&next->vertices==names->vertices,
                "nomi indipendenti dalla distanza di estrusione");
        }
        if(mode==0) {
            QString error;auto pending=original;
            pending.extrusions[index].forgeBody=forgeExtrusion(sketch,2,&error);
            require(pending.extrusions[index].forgeBody&&!featureHistoryOf(*pending.extrusions[index].forgeBody),"estrusione preparata senza calcolo dei nomi");
            require(saveDocumentFile(path,pending,true).isEmpty()&&!featureHistoryOf(*pending.extrusions[index].forgeBody),"salvataggio senza inferenza");
            require(loadDocumentFile(path,loaded).isEmpty(),"rilettura etichette preparate");
            CadViewport prepared;prepared.loadDocument(loaded);verify(prepared.currentDocument());
            for(FaceId f:body->faces())require(!entityEvolutionText(*body,EntityKind::Face,f.index).contains(QStringLiteral("nessuna provenienza")),
                "diagnostica della sorgente nota dello schizzo");
        }
    }
    // Limiti su una faccia obliqua e su una faccia curva, anche con secondo verso.
    for(bool curved:{false,true}) {
        DocumentState definition;SketchObject sketch=rectangle(-0.5,-0.5,1,1);
        const Frame3 plane(Vec3(-5,-5,3),normalized(Vec3(-0.2,0,1)),Vec3(1,0,0.2));
        const Body tool=curved?makeCylinder(Frame3(Vec3(0,-5,3),Vec3(0,1,0),Vec3(1,0,0)),2,10):makeBox(plane,20,20,20);
        const Vec3 point=curved?Vec3(0,0,1):plane.toGlobal(Vec3(5,5,0));
        FaceId limit;
        for(FaceId face:tool.faces())if(projectPoint(*tool.face(face).surface,point).distance<1e-8
            &&classifyPointOnFace(tool,face,point,1e-8)==PointLocation::Inside)limit=face;
        require(limit.valid(),"faccia limite della fixture");
        ExtrusionObject feature;feature.feature=BodyFeature::Extrusion;feature.featureId=97001;feature.sketchIndex=0;
        feature.extent=3;feature.extentRef.kind=5;feature.extentRef.index=0;feature.extentRef.point=faceReference(tool,limit,point);
        if(curved){feature.extrudeSides=2;feature.distance2=0.75;}
        definition.sketches={sketch};definition.extrusions={imported(tool),feature};
        CadViewport view;view.loadDocument(definition);const auto original=view.currentDocument();
        const auto shape=original.extrusions[1].forgeBody;
        require(shape&&original.extrusions[1].error.isEmpty(),"estrusione fino alla faccia costruita");
        const auto history=featureHistoryOf(*shape);require(history&&history->operandCount()==1,"faccia limite tra gli operandi della genealogia");
        for(FaceId face:shape->faces())require(history->isDeclared(EntityKind::Face,face.index),"facce limitate dichiarate");
        const FaceId actual=resolveFaceReference(*original.extrusions[0].forgeBody,feature.extentRef.point,1e-5,ReferenceState::Other);
        require(actual.valid(),"faccia limite risolta sul corpo importato");
        const auto sources=history->generated(EntityKind::Face,EntityKind::Face,0,actual.index);
        require(!sources.empty(),"coperchio generato dalla faccia limite effettiva");
        const auto names=elementNamesOf(*shape);const auto bytes=writeBodyBinary(*shape);
        const QString path=directory.filePath(curved?QStringLiteral("curva.prt"):QStringLiteral("obliqua.prt"));
        require(saveDocumentFile(path,original,true).isEmpty(),"salvataggio limite dichiarato");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty()&&loaded.extrusions[1].cachedGeometry,"ripristino limite dalla cache");
        CadViewport reopened;reopened.loadDocument(loaded);
        require(reopened.rebuildAllFeatures()==0,"rigenerazione limite dichiarato");
        const auto other=reopened.extrusions()[1].forgeBody;const auto next=elementNamesOf(*other);
        require(writeBodyBinary(*other)==bytes&&next&&next->faces==names->faces&&next->edges==names->edges&&next->vertices==names->vertices,
            "nomi e geometria del limite stabili");
    }
    // Etichette corrotte nella cache: rifiuto atomico, definizione intatta.
    for(int corruption=0;corruption<5;++corruption) {
        DocumentState state;state.sketches={rectangle(0,0,2,2)};
        ExtrusionObject feature;feature.feature=BodyFeature::Extrusion;feature.featureId=97001;feature.sketchIndex=0;feature.distance=2;
        state.extrusions={feature};
        QString error;
        const auto fresh=forgeExtrusion(state.sketches[0],2,&error);
        require(bool(fresh),"estrusione per fixture etichette corrotte");
        feature.forgeBody=std::make_shared<const Body>(*fresh);
        auto history=makeHistory({},*feature.forgeBody);
        const auto faces=feature.forgeBody->faces();
        for(FaceId face:faces)if(corruption!=4||face!=faces.back())history.declareOrigins(EntityKind::Face,face.index,{});
        FaceAliases labels(std::size_t(history.resultSize(EntityKind::Face)));
        labels[std::size_t(faces.front().index)]={"side.L0"};
        if(corruption==0){labels.resize(labels.size()+2);labels.back()={"invalid"};}
        if(corruption==1)labels[std::size_t(faces.front().index)]={"side.L0","side.L0"};
        if(corruption==2)labels[std::size_t(faces.front().index)]={""};
        if(corruption==3)labels[std::size_t(faces.front().index)]={std::string(4097,'x')};
        if(corruption==4)labels[std::size_t(faces.back().index)]={"cap.end"};
        registerDeclaredHistory(feature.forgeBody,{},std::move(history),std::move(labels));
        ExtrusionObject root;root.feature=BodyFeature::Primitive;root.featureId=97000;
        root.forgeBody=std::make_shared<const Body>(makeBox(Frame3(),1,1,1));
        state.extrusions={root,feature};
        const QString path=directory.filePath(QStringLiteral("etichette-corrotte-%1.prt").arg(corruption));
        require(saveDocumentFile(path,state,true).isEmpty(),"salvataggio fixture corrotta");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty(),"definizione leggibile con etichette corrotte");
        for(const auto &body:loaded.extrusions)require(!body.cachedGeometry&&!body.forgeBody,"cache etichette incoerenti rifiutata atomicamente");
    }
}

#endif
