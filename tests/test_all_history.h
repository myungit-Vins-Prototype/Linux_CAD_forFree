#ifndef FORGECAD_TEST_ALL_HISTORY_H
#define FORGECAD_TEST_ALL_HISTORY_H

// Il lettore binario ricostruisce le primitive geometriche. Nella
// filettatura un coefficiente quasi nullo cambia gia' nella versione precedente:
// confronta anche topologia e geometria, lasciando esatta la prova di rigenerazione.
static bool equivalentThreadCache(const ForgeCad::Kernel::Body &a,const ForgeCad::Kernel::Body &b) {
    using namespace ForgeCad::Kernel;
    if(a.faces()!=b.faces()||a.edges()!=b.edges()||a.vertices()!=b.vertices()||a.fins()!=b.fins()||a.loops()!=b.loops())return false;
    constexpr double tolerance=1e-10;
    for(VertexId v:a.vertices())if(distance(a.vertex(v).point,b.vertex(v).point)>tolerance)return false;
    for(EdgeId e:a.edges()) {
        const auto &x=a.edge(e),&y=b.edge(e);
        if(x.range.lo!=y.range.lo||x.range.hi!=y.range.hi||a.edgeStart(e)!=b.edgeStart(e)||a.edgeEnd(e)!=b.edgeEnd(e)||x.curve->type()!=y.curve->type())return false;
        for(int i=0;i<=8;++i){double t=x.range.lo+x.range.length()*i/8;if(distance(x.curve->point(t),y.curve->point(t))>tolerance)return false;}
    }
    for(FinId f:a.fins()) {const auto &x=a.fin(f),&y=b.fin(f);if(x.edge!=y.edge||x.loop!=y.loop||x.sense!=y.sense||bool(x.pcurve)!=bool(y.pcurve))return false;}
    for(FaceId f:a.faces()) {
        const auto &x=a.face(f),&y=b.face(f);if(x.loops!=y.loops||x.sense!=y.sense||x.surface->type()!=y.surface->type())return false;
        Vec3 point;Vec2 uv;if(!faceInteriorPoint(a,f,point,&uv))return false;
        if(distance(x.surface->point(uv.x(),uv.y()),y.surface->point(uv.x(),uv.y()))>tolerance
           ||distance(normalAt(*x.surface,uv.x(),uv.y()),normalAt(*y.surface,uv.x(),uv.y()))>tolerance)return false;
    }
    return true;
}

static void allHistoryTest() {
    using namespace ForgeCad;using namespace ForgeCad::Kernel;
    QTemporaryDir directory;require(directory.isValid(),"directory genealogia completa");
    const auto rectangle=[](double r,double z=0) {
        SketchObject s;s.plane=0;s.customFrame=true;s.frame.origin[2]=z;
        const QPointF p[]={{-r,-r},{r,-r},{r,r},{-r,r}};
        for(int i=0;i<4;++i)s.segments.append({p[i],p[(i+1)%4]});
        return s;
    };
    const auto circle=[](double r,double z=0) {
        SketchObject s;s.plane=0;s.customFrame=true;s.frame.origin[2]=z;
        CurveObject c;c.tool=DrawingTool::Circle;c.controlPoints={{0,0},{r,0}};recalculateCurve(c,0);s.curves={c};return s;
    };
    const auto sketchRef=[](int sketch,int kind,int index) {GeometryRef r;r.kind=7;r.index=sketch;r.element={kind,index,-1};return r;};
    const auto primitive=[] {ExtrusionObject b;b.feature=BodyFeature::Primitive;b.featureId=99000;b.primitive.size[0]=8;b.primitive.size[1]=6;b.primitive.size[2]=4;return b;};
    for(int mode=0;mode<33;++mode)try {
        DocumentState definition;ExtrusionObject feature;feature.featureId=99001;
        const auto box=makeBox(Frame3(),8,6,4);
        FaceId top,right;for(FaceId f:box.faces()) {
            const auto &plane=static_cast<const Plane&>(*box.face(f).surface);
            Vec3 n=box.face(f).sense?plane.frame().zDir():-plane.frame().zDir();
            if(n.z()>0.99)top=f;
            if(n.x()>0.99)right=f;
        }
        const auto topRef=faceReference(box,top,Vec3(4,3,4));const auto rightRef=faceReference(box,right,Vec3(8,3,2));
        if(mode<5){feature=primitive();feature.primitive.kind=PrimitiveKind(mode);if(mode==3)feature.primitive.size[1]=2;if(mode==4){feature.primitive.size[0]=4;feature.primitive.size[1]=1;}}
        else if(mode==5){definition.sketches={rectangle(2)};feature.feature=BodyFeature::PlanarSurface;feature.sketchIndex=0;}
        else if(mode==6||mode==7) {
            SketchObject path;path.plane=1;path.segments={{{0,0},{0,3}}};definition.sketches={circle(0.5),path};
            feature.feature=BodyFeature::Sweep;feature.sketchIndex=0;feature.pathSketch=1;feature.sweepSurface=mode==7;
        } else if(mode>=8&&mode<=11) {
            definition.sketches={circle(1),circle(1.5,3)};
            if(mode==11){feature.feature=BodyFeature::Ruled;feature.ruledFirst={sketchRef(0,1,0)};feature.ruledSecond={sketchRef(1,1,0)};}
            else {feature.feature=BodyFeature::Loft;feature.loftSketches={0,1};feature.loftRuled=mode==9;feature.loftSurface=mode==10;}
        } else if(mode==12||mode==13) {
            definition.sketches={rectangle(2)};feature.feature=mode==12?BodyFeature::BoundarySurface:BodyFeature::FillSurface;
            for(int i=0;i<4;++i)feature.planarRefs.append(sketchRef(0,0,i));
            feature.fillContinuity=0;
        } else if(mode>=14&&mode<=21) {
            definition.extrusions={primitive()};feature.firstBody=0;
            if(mode==14){feature.feature=BodyFeature::Scale;feature.scaleFactor=1.25;}
            if(mode==15){feature.feature=BodyFeature::Transform;feature.move.translation[0]=10;feature.move.angle=20;}
            if(mode==16||mode==17){feature.feature=BodyFeature::Pattern;feature.pattern.kind=mode==16?0:2;feature.pattern.count=3;feature.pattern.spacing=12;GeometryRef ref;ref.kind=mode==16?2:1;ref.index=mode==16?0:2;feature.pattern.refs={ref};feature.pattern.keepOriginal=mode==16;}
            if(mode==18){feature.feature=BodyFeature::Draft;feature.draftNeutral.kind=1;feature.draftNeutral.index=0;feature.draftAngle=5;feature.offsetFaces={rightRef};}
            if(mode==19){feature.feature=BodyFeature::Thicken;feature.distance=0.5;feature.thickenSide=2;feature.offsetFaces={topRef};}
            if(mode==20){feature.feature=BodyFeature::Shell;feature.distance=0.5;feature.offsetFaces={topRef};}
            if(mode==21){feature.feature=BodyFeature::DeleteFace;feature.offsetFaces={topRef};}
        } else if(mode>=22&&mode<=27) {
            definition.sketches={rectangle(2)};ExtrusionObject root;root.feature=BodyFeature::PlanarSurface;root.featureId=99000;root.sketchIndex=0;definition.extrusions={root};feature.firstBody=0;
            if(mode==22) {
                feature.feature=BodyFeature::SheetExtend;feature.blendSize=1;
                QString error;const auto source=forgePlanarSketch(definition.sketches[0],&error);require(bool(source),"fixture estensione");
                const auto edge=nearestEdge(*source,Vec3(0,-2,0),1e-7);feature.blendEdges={edgeReference(*source,edge,Vec3(0,-2,0))};
            }
            if(mode==23){feature.feature=BodyFeature::SheetTrim;feature.trimPlane=2;feature.trimKeep={1,0,0};}
            if(mode==24){definition.sketches.append(rectangle(1));feature.feature=BodyFeature::SheetTrim;feature.sketchIndex=1;feature.trimProject=true;}
            if(mode==25||mode==27) {
                auto other=rectangle(2,mode==25?3:0);if(mode==27){other.customFrame=false;other.plane=2;}
                definition.sketches.append(other);auto second=root;second.featureId=99002;second.sketchIndex=1;definition.extrusions.append(second);
                if(mode==25){feature.feature=BodyFeature::Sew;feature.sewSolid=false;feature.booleanTools={1};}
                else {feature.feature=BodyFeature::SheetTrim;feature.secondBody=1;feature.trimBoth=true;feature.trimKeep={1,0,0};feature.trimToolKeep={0,0,1};}
            }
            if(mode==26){feature.feature=BodyFeature::Pattern;feature.pattern.count=3;feature.pattern.spacing=8;GeometryRef axis;axis.kind=2;axis.index=0;feature.pattern.refs={axis};}
        } else if(mode==28||mode==29) {
            auto a=primitive(),b=primitive();b.featureId=99002;
            b.primitive.origin[0]=6;b.primitive.size[0]=4;
            ExtrusionObject united;united.featureId=99003;united.operation=mode==28?0:2;united.firstBody=0;united.secondBody=1;
            definition.extrusions={a,b,united};feature.feature=BodyFeature::Pattern;feature.firstBody=2;feature.pattern.featureOnly=true;
            feature.pattern.count=2;feature.pattern.spacing=15;GeometryRef axis;axis.kind=2;axis.index=0;feature.pattern.refs={axis};
        } else if(mode==30) {
            auto root=primitive();root.primitive.kind=PrimitiveKind::Cylinder;root.primitive.size[0]=4;root.primitive.size[1]=5;definition.extrusions={root};
            const auto cylinder=makeCylinder(Frame3(),4,5);FaceId side;for(FaceId f:cylinder.faces())if(cylinder.face(f).surface->type()==SurfaceType::Cylinder)side=f;
            feature.feature=BodyFeature::Thread;feature.firstBody=0;feature.thread.face=faceReference(cylinder,side,Vec3(4,0,2));feature.thread.pitch=1;feature.thread.length=1;
        } else {
            feature.feature=BodyFeature::Imported;ExchangeBody source;source.name="fixture";source.body=box;
            feature.importData=QByteArray::fromStdString(writeStep({source}));feature.importSource=QStringLiteral("fixture.step");
        }
        if (feature.feature == BodyFeature::Pattern) feature.patternComponent = 0;
        definition.extrusions.append(feature);
        ForgeBody preparedImport;QVector<ImportedPart> preparedParts;QString preparedPath;
        if(mode==32) {
            const auto path=directory.filePath(QStringLiteral("preparata.step"));QFile file(path);require(file.open(QIODevice::WriteOnly),"file STEP temporaneo");file.write(feature.importData);file.close();
            QVector<ImportedPart> parts;require(importCadFile(path,parts).isEmpty()&&parts.size()==1,"importazione interattiva preparata");
            preparedParts=parts;preparedPath=path;
            preparedImport=parts.front().body;definition.extrusions.last().forgeBody=preparedImport;definition.extrusions.last().importData=parts.front().data;definition.extrusions.last().cachedGeometry=true;
        }
        CadViewport view;
        if(preparedImport)require(view.importParts(preparedParts,preparedPath).isEmpty(),"inserimento interattivo delle parti");
        else view.loadDocument(definition);
        const auto original=view.currentDocument();const int index=original.extrusions.size()-1;
        const auto body=original.extrusions[index].forgeBody;
        if(!body)throw std::runtime_error(original.extrusions[index].error.toStdString());
        if(preparedImport)require(body==preparedImport,"corpo preparato importato senza ricostruirlo");
        const auto names=elementNamesOf(*body);require(bool(names),"nomi feature migrata");const auto bytes=writeBodyBinary(*body);
        bool fromCache=false;
        const auto verify=[&](const DocumentState &state) {
            const auto shape=state.extrusions[index].forgeBody;require(shape&&state.extrusions[index].error.isEmpty()&&(writeBodyBinary(*shape)==bytes||(mode==30&&fromCache&&equivalentThreadCache(*body,*shape))),"B-rep conservato");
            const auto history=featureHistoryOf(*shape);require(bool(history),"genealogia disponibile");
            for(FaceId f:shape->faces())require(history->isDeclared(EntityKind::Face,f.index),"ogni faccia migrata ha origine dichiarata");
            const auto metadata=declaredHistorySnapshot(state.extrusions[index],index,state.extrusions);require(bool(metadata.history),"dichiarazioni persistenti");
            const auto next=elementNamesOf(*shape);require(next&&next->faces==names->faces&&next->edges==names->edges&&next->vertices==names->vertices,"nomi di tutte le entita' persistenti");
        };
        verify(original);const auto history=featureHistoryOf(*body);view.setHistoryPosition(index);view.setHistoryPosition(index+1);
        require(view.extrusions()[index].forgeBody==body&&featureHistoryOf(*body)==history,"navigazione senza rigenerare corpo o storia");
        const auto path=directory.filePath(QStringLiteral("famiglia-%1.prt").arg(mode));require(saveDocumentFile(path,original,true).isEmpty(),"salvataggio famiglia");
        DocumentState loaded;require(loadDocumentFile(path,loaded).isEmpty()&&loaded.extrusions[index].cachedGeometry,"cache 7 della famiglia");
        CadViewport reopened;reopened.loadDocument(loaded);fromCache=true;verify(reopened.currentDocument());fromCache=false;require(reopened.rebuildAllFeatures()==0,"rigenerazione famiglia");verify(reopened.currentDocument());
        if(mode==32) {
            require(view.importParts(preparedParts,preparedPath).isEmpty(),"secondo inserimento delle stesse parti");
            const auto doubled=view.currentDocument();const auto copy=doubled.extrusions.back().forgeBody;
            const auto copiedNames=elementNamesOf(*copy);
            require(doubled.extrusions.size()==2&&copy!=body&&copiedNames&&copiedNames->faces!=names->faces,"importazioni distinte con corpi e nomi propri");
            require(saveDocumentFile(path,doubled,true).isEmpty()&&loadDocumentFile(path,loaded).isEmpty(),"cache delle importazioni duplicate");
            CadViewport copies;copies.loadDocument(loaded);require(copies.rebuildAllFeatures()==0,"rigenerazione importazioni duplicate");
            require(elementNamesOf(*copies.extrusions()[0].forgeBody)->faces==names->faces&&elementNamesOf(*copies.extrusions()[1].forgeBody)->faces==copiedNames->faces,"nomi distinti persistenti dopo rigenerazione");
        }
    } catch(const std::exception &e){throw std::runtime_error("famiglia "+std::to_string(mode)+": "+e.what());}
}
#endif
