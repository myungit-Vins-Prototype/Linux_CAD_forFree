#ifndef FORGECAD_TEST_REVOLUTION_HISTORY_H
#define FORGECAD_TEST_REVOLUTION_HISTORY_H

static void revolutionHistoryTest() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto rectangle=[](double x,double y,double w,double h) {
        SketchObject sketch;sketch.plane=1;
        sketch.segments.append({{0,-2},{0,-1}});sketch.constructionSegments={0};
        const QPointF p[]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
        for(int i=0;i<4;++i)sketch.segments.append({p[i],p[(i+1)%4]});
        return sketch;
    };
    QTemporaryDir directory;require(directory.isValid(),"directory rivoluzioni dichiarate");
    for(int mode=0;mode<20;++mode) {
        DocumentState definition;
        auto sketch=rectangle(1,0,4,4);
        ExtrusionObject feature;feature.feature=BodyFeature::Revolution;feature.featureId=98001;
        feature.sketchIndex=0;feature.plane=1;feature.revolveAxis=0;
        feature.revolveAngle=mode==0?360:mode==2?-90:mode==3?270:mode==4?-270:90;
        if(mode==5||mode==14){sketch=rectangle(0,0,4,4);feature.revolveAngle=mode==5?180:-180;}
        if(mode==6){const auto hole=rectangle(2,1,1,1);for(int i=1;i<hole.segments.size();++i)sketch.segments.append(hole.segments[i]);}
        if(mode==7){sketch.segments.resize(1);CurveObject circle;circle.tool=DrawingTool::Circle;circle.controlPoints={{4,0},{5,0}};sketch.curves={circle};}
        if(mode==8){const auto other=rectangle(7,0,1,2);for(int i=1;i<other.segments.size();++i)sketch.segments.append(other.segments[i]);}
        if(mode==9||mode==15){sketch.segments.resize(3);feature.revolveAngle=mode==9?-270:360;}
        if(mode==10){feature.revolveAxis=kRevolveAxisReference;feature.revolveAxisRef.kind=2;feature.revolveAxisRef.index=2;}
        if(mode==11||mode==12) {
            definition.sketches.append(rectangle(1,0,3,4));
            ExtrusionObject root=feature;root.featureId=98000;root.revolveAngle=360;
            definition.extrusions.append(root);
            sketch=rectangle(2,1,mode==11?3:1,2);feature.sketchIndex=1;feature.mergeBodies={0};
            feature.mergeOperation=mode==11?1:2;
        }
        if(mode==13)sketch=rectangle(-5,0,4,4);
        if(mode>=16) {
            sketch.segments.resize(1);
            CurveObject spline; spline.tool=DrawingTool::Spline;
            spline.controlPoints={{2,0},{3,-1},{5,0},{4,2},{2,1},{2,0}};
            shapeSpline(spline,{true,false,false});recalculateCurve(spline);
            sketch.curves={spline};
            feature.revolveAngle=mode==16?90:mode==17?-90:mode==18?270:360;
        }
        definition.sketches.append(sketch);definition.extrusions.append(feature);
        CadViewport view;view.loadDocument(definition);
        const auto original=view.currentDocument();const int index=original.extrusions.size()-1;
        const auto body=original.extrusions[index].forgeBody;
        if(!body)std::cerr<<"revolution mode "<<mode<<": "<<original.extrusions[index].error.toStdString()<<std::endl;
        require(body&&original.extrusions[index].error.isEmpty(),"rivoluzione della matrice costruita");
        if(mode>=16)require(checkBody(*body).empty(),"spline chiusa: corpo rivoluzione valido");
        if(mode==19) {
            require(body->counts().faces==2,"giro completo spline con cuciture esplicite");
            const auto &face=body->face(body->faces().front());
            const auto &surface=static_cast<const RevolutionSurface &>(*face.surface);
            auto meridian=surface.meridian();
            while(meridian->type()==CurveType::Trimmed)
                meridian=static_cast<const TrimmedCurve<3>&>(*meridian).basis();
            Body::BuildFace legacy;
            legacy.surface=std::make_shared<RevolutionSurface>(meridian,surface.axisPoint(),surface.axisDirection());
            legacy.sense=face.sense;
            auto old=original;
            old.extrusions[index].forgeBody=std::make_shared<Body>(Body::build({}, {}, {legacy}));
            old.extrusions[index].cachedGeometry=true;
            QByteArray encoded;
            require(encodeDocument(old,encoded,true).isEmpty(),"serializzazione cache rivoluzione senza cuciture");
            DocumentState decoded;
            require(decodeDocument(encoded,decoded,true).isEmpty(),"lettura cache storica rivoluzione completa");
            CadViewport migrated;migrated.loadDocument(decoded);
            const auto &fixed=migrated.extrusions()[index];
            require(fixed.forgeBody && fixed.forgeBody->counts().faces==2 && checkBody(*fixed.forgeBody).empty(),
                    "vecchia cache a singola faccia rigenerata automaticamente");
            require(std::abs(massProperties(*fixed.forgeBody).volume-massProperties(*body).volume)<1e-8,
                    "migrazione conserva il volume esatto");
        }
        const auto bytes=writeBodyBinary(*body);const auto names=elementNamesOf(*body);
        require(bool(names),"nomi rivoluzione disponibili");
        const auto verify=[&](const DocumentState &state) {
            const auto shape=state.extrusions[index].forgeBody;
            require(shape&&writeBodyBinary(*shape)==bytes,"geometria rivoluzione conservata");
            const auto history=featureHistoryOf(*shape);require(bool(history),"genealogia rivoluzione disponibile");
            const auto metadata=declaredHistorySnapshot(state.extrusions[index],index,state.extrusions);
            require(metadata.history&&metadata.labels,"etichette rivoluzione persistenti");
            bool side=false,start=false,end=false;
            for(FaceId face:shape->faces()) {
                require(history->isDeclared(EntityKind::Face,face.index),"ogni faccia della rivoluzione dichiarata");
                // Gli alias sono sparsi: le facce ereditate possono non avere una cella.
                if(std::size_t(face.index)>=metadata.labels->size())continue;
                for(const auto &label:(*metadata.labels)[std::size_t(face.index)]) {
                    side=side||label.rfind("side.",0)==0;
                    start=start||label=="cap.start";end=end||label=="cap.end";
                    require(label!="side.L0","asse di costruzione escluso dalle sorgenti");
                }
            }
            require(side,"fianchi associati ai tratti dello schizzo");
            if(mode==0||mode==9||mode==15||mode==19)require(!start&&!end,"giro completo e lamina senza coperchi");
            else if(mode<11||mode==13||mode==14||mode>=16)require(start&&end,"coperchi iniziale e finale dichiarati");
            const auto next=elementNamesOf(*shape);
            require(next&&next->faces==names->faces&&next->edges==names->edges&&next->vertices==names->vertices,
                "nomi di tutte le entita' conservati dopo riapertura e rigenerazione");
        };
        verify(original);
        if(mode==5||mode==14) {
            const auto metadata=declaredHistorySnapshot(original.extrusions[index],index,original.extrusions);bool both=false;
            for(const auto &cell:*metadata.labels)both=both||(std::find(cell.begin(),cell.end(),"cap.start")!=cell.end()&&std::find(cell.begin(),cell.end(),"cap.end")!=cell.end());
            require(both,"il coperchio del mezzo giro sull'asse conserva entrambe le origini");
        }
        const auto history=featureHistoryOf(*body);
        view.setHistoryPosition(index);view.setHistoryPosition(index+1);
        require(view.extrusions()[index].forgeBody==body&&featureHistoryOf(*body)==history,"rollback rivoluzione senza ricalcolo");
        const QString path=directory.filePath(QStringLiteral("rivoluzione-%1.prt").arg(mode));
        require(saveDocumentFile(path,original,true).isEmpty(),"salvataggio rivoluzione con etichette");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty()&&loaded.extrusions[index].cachedGeometry,"rivoluzione ripristinata dalla cache 7");
        CadViewport reopened;reopened.loadDocument(loaded);verify(reopened.currentDocument());
        require(reopened.rebuildAllFeatures()==0,"rigenerazione rivoluzione riaperta");verify(reopened.currentDocument());
        if(mode!=0&&mode!=5&&mode!=11&&mode!=12&&mode!=14&&mode!=15&&mode!=19) {
            auto changed=original;changed.extrusions[index].revolveAngle*=0.8;
            for(auto &f:changed.extrusions)f.cachedGeometry=false;
            CadViewport shorter;shorter.loadDocument(changed);
            require(shorter.extrusions()[index].forgeBody&&shorter.extrusions()[index].error.isEmpty(),"rivoluzione al cambio angolo costruita");
            const auto next=elementNamesOf(*shorter.extrusions()[index].forgeBody);
            const auto sorted=[](auto cells){std::sort(cells.begin(),cells.end());return cells;};
            require(next&&sorted(next->faces)==sorted(names->faces)&&sorted(next->edges)==sorted(names->edges)&&sorted(next->vertices)==sorted(names->vertices),
                "nomi indipendenti dall'angolo con la stessa topologia");
        }
        if(mode==1) {
            QString error;auto pending=original;
            pending.extrusions[index].forgeBody=forgeRevolution(sketch,0,90,&error);
            require(pending.extrusions[index].forgeBody&&!featureHistoryOf(*pending.extrusions[index].forgeBody),"rivoluzione preparata senza nomi");
            require(saveDocumentFile(path,pending,true).isEmpty()&&!featureHistoryOf(*pending.extrusions[index].forgeBody),"salvataggio rivoluzione senza inferenza");
            require(loadDocumentFile(path,loaded).isEmpty(),"rilettura etichette rivoluzione preparata");
            CadViewport prepared;prepared.loadDocument(loaded);verify(prepared.currentDocument());
            for(FaceId f:body->faces())require(!entityEvolutionText(*body,EntityKind::Face,f.index).contains(QStringLiteral("nessuna provenienza")),"diagnostica origine nota dello schizzo");
        }
    }
}
#endif
