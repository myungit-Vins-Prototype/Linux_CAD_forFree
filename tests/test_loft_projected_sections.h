static void loftProjectedSections() {
    using namespace ForgeCad;using namespace Kernel;
    const auto nurbs=[](QVector<QPointF> points){CurveObject c;c.tool=DrawingTool::Nurbs;c.degree=2;c.knots={0,0,0,1,1,1};c.controlPoints=points;return c;};
    const auto entity=[](int sketch,int index){GeometryRef r;r.kind=7;r.index=sketch;r.element={1,index,-1};return r;};
    DocumentState initial;initial.sketches.resize(2);auto &sketches=initial.sketches;
    sketches[0].name="Input A";sketches[1].name="Input B";sketches[0].storyId=1001;sketches[1].storyId=1002;sketches[1].plane=1;
    sketches[0].curves={nurbs({{0,0},{0,2},{4,3}}),nurbs({{0,5},{0,7},{4,8}})};
    sketches[1].curves={nurbs({{0,1},{1,4},{4,2}})};
    const auto ref=[](int body,quint64 id){GeometryRef r;r.kind=9;r.index=body;r.featureId=id;return r;};
    for(int i=0;i<2;++i){ExtrusionObject p;p.feature=BodyFeature::ProjectedCurve;p.projectionMode=1;p.featureId=2001+i;p.name=QStringLiteral("Proiezione %1").arg(i+1);p.ruledFirst={entity(0,i)};p.ruledSecond={entity(1,0)};initial.extrusions.append(p);}
    for(int i=0;i<2;++i){ExtrusionObject e;e.feature=BodyFeature::ExtractedCurve;e.featureId=2003+i;e.name=QStringLiteral("Estrazione %1").arg(i+1);e.curveExtraction.source=ref(i,2001+i);initial.extrusions.append(e);}
    for(bool extracted:{false,true}) {
        CadViewport view;view.loadDocument(initial);
        for(const auto &b:view.extrusions_)require(b.error.isEmpty()&&b.curve&&isImplicitSketchProjection(*b.curve),"fixture proiezioni ed estrazioni implicite ricostruite");
        const auto original=view.extrusions_[0].curve;
        ExtrusionObject loft;loft.feature=BodyFeature::Loft;loft.loftSurface=true;loft.name="Loft da sezioni implicite";
        loft.loftSections={ref(extracted?2:0,extracted?2003:2001),ref(extracted?3:1,extracted?2004:2002)};
        require(view.createBody(loft).isEmpty()&&view.extrusions_.last().forgeBody,"creazione loft da proiezioni/estrazioni");
        const auto validate=[&](CadViewport &v){
            const auto &body=v.extrusions_.last();require(body.feature==BodyFeature::Loft&&body.error.isEmpty()&&body.forgeBody&&checkBody(*body.forgeBody).empty(),"B-rep loft valida");
            for(const auto &r:body.loftSections){std::vector<PathSegment> path;QString error;require(geometryRefPath(r,v.extrusions_.size()-1,v.sketches_,v.extrusions_,path,&error),"riferimento sezione associativo risolto");
                for(const auto &piece:path)for(int i=0;i<=31;++i){const auto p=piece.curve->point(piece.range.lo+piece.range.length()*i/31.);double best=1e100;
                    for(auto face:body.forgeBody->faces())best=std::min(best,projectPoint(*body.forgeBody->face(face).surface,p).distance);
                    require(best<4e-9,"sezione associativa conservata entro budget");}}
        };
        validate(view);require(view.extrusions_[0].curve==original&&isImplicitSketchProjection(*original),"sorgente esatta non sostituita dal loft");
        view.undo();require(view.extrusions_.size()==4,"Undo creazione loft");view.redo();validate(view);
        const auto before=view.extrusions_.last().forgeBody;view.recordUndo();view.sketches_[0].curves[0].controlPoints[1].ry()+=.3;view.regenerateDependents(0);view.documentChanged();
        validate(view);require(view.extrusions_.last().forgeBody!=before,"modifica input rigenera estrazioni e loft");
        view.undo();validate(view);view.redo();validate(view);
        for(bool cache:{false,true}){QByteArray bytes;require(encodeDocument(view.currentDocument(),bytes,cache).isEmpty(),"salvataggio loft con sezioni implicite");DocumentState decoded;require(decodeDocument(bytes,decoded).isEmpty(),"lettura loft con sezioni implicite");CadViewport restored;restored.loadDocument(decoded);validate(restored);
            require(restored.rebuildAllFeatures()==0,"rigenerazione completa dopo lettura");validate(restored);}
    }
    std::cout<<"PASS loft da proiezioni/estrazioni: sorgenti esatte, forma, dipendenze, Undo/Redo, cache e rigenerazione\n";
}
