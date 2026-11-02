// Incluso nel friend ViewportInteractionTest. Solo B-rep sintetiche; nessun GL.
static void nextBodyUnionRegressions() {
    using namespace ForgeCad;
    using namespace Kernel;
    const auto rectangle=[](double x,double y,double width,double height) {
        SketchObject sketch;const QPointF p[]={{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
        for(int i=0;i<4;++i)sketch.segments.append({p[i],p[(i+1)%4]});
        return sketch;
    };
    const auto target=[](const Body &shape,int id) {
        ExtrusionObject body;body.feature=BodyFeature::Imported;body.featureId=body.modelBodyId=quint64(id);
        body.name=QStringLiteral("Destinatario %1").arg(id);body.forgeBody=std::make_shared<const Body>(shape);body.solid=true;
        return body;
    };
    const auto box=[&](double x,double y,double z,double w,double h,double d,int id) {
        return target(makeBox(Frame3(Vec3(x,y,z),Vec3(0,0,1),Vec3(1,0,0)),w,h,d),id);
    };
    quint64 featureId=72000;
    const auto build=[&](const SketchObject &sketch,const QVector<ExtrusionObject> &sources,QVector<int> merges,
                         bool automatic,QVector<int> limits=QVector<int>{},double direction=1.0,int operation=1) {
        ExtrusionObject feature;feature.feature=BodyFeature::Extrusion;feature.featureId=featureId++;
        feature.sketchIndex=0;feature.extent=4;feature.distance=direction;feature.booleanTools=std::move(limits);
        feature.mergeOperation=merges.isEmpty()?0:operation;feature.mergeAuto=automatic;feature.mergeProbe=automatic;
        feature.mergeBodies=std::move(merges);buildGeometry(feature,sources.size(),{sketch},sources);return feature;
    };
    const auto valid=[&](const ExtrusionObject &feature,double expected,int shells) {
        require(feature.forgeBody && feature.error.isEmpty(),qPrintable(QStringLiteral("Unione prossimo corpo: ")+feature.error));
        require(checkBody(*feature.forgeBody).empty(),"l'unione limitata deve essere una B-rep valida");
        require(!feature.forgeBody->isSheet() && int(feature.forgeBody->shells().size())==shells,
                "numero atteso di componenti solide dopo la fusione");
        require(std::abs(massProperties(*feature.forgeBody).volume-expected)<1e-7,
                qPrintable(QStringLiteral("Volume unione: atteso %1, ottenuto %2").arg(expected,0,'g',17).arg(massProperties(*feature.forgeBody).volume,0,'g',17)));
    };
    const auto genealogy=[&](const ExtrusionObject &feature,const QVector<ExtrusionObject> &sources) {
        const auto declared=declaredHistorySnapshot(feature,sources.size(),sources);
        const auto history=featureHistoryOf(*feature.forgeBody);
        require(declared.history && history && history->operandCount()==sources.size(),
                "la storia dichiarata si ricompone sui corpi limite/destinatari effettivi");
        for(FaceId face:feature.forgeBody->faces())
            require(declared.history->isDeclared(EntityKind::Face,face.index),"facce dell'unione con provenienza dichiarata");
        for(int source:feature.mergeBodies) {
            bool found=false;
            for(FaceId face:feature.forgeBody->faces())for(const auto &origin:history->origins(EntityKind::Face,face.index))
                found=found || (origin.operand==source && origin.kind==EntityKind::Face);
            require(found,"ogni destinatario conservato contribuisce alla genealogia delle facce");
        }
    };
    const auto profile=rectangle(0,0,2,2);
    const auto above=box(-1,-1,4,4,4,2,71001);
    for(bool automatic:{false,true}) {
        const auto united=build(profile,{above},{0},automatic);valid(united,48,1);genealogy(united,{above});
        require(united.mergeBodies==QVector<int>({0}) && united.booleanTools==QVector<int>({0}),
                "unione manuale e automatica conservano destinatario e limite");
    }
    // Un limite curvo conserva il carrier cilindrico esatto anche dopo la saldatura.
    const auto small=rectangle(-.5,-.5,1,1);
    const auto cylinder=target(makeCylinder(Frame3(Vec3(0,-5,3),Vec3(0,1,0),Vec3(1,0,0)),2,10),71002);
    const auto stopped=build(small,{cylinder},{},false);
    require(stopped.forgeBody && stopped.error.isEmpty(),"fixture estrusione limitata da cilindro");
    const double curvedVolume=massProperties(*stopped.forgeBody).volume+massProperties(*cylinder.forgeBody).volume;
    for(bool automatic:{false,true}) {
        const auto united=build(small,{cylinder},{0},automatic);valid(united,curvedVolume,1);genealogy(united,{cylinder});
        bool exactCylinder=false;
        for(FaceId face:united.forgeBody->faces())exactCylinder=exactCylinder || united.forgeBody->face(face).surface->type()==SurfaceType::Cylinder;
        require(exactCylinder,"la fusione non sostituisce la superficie cilindrica con un fit");
    }
    // La prova da intersezione vuota non trasforma un destinatario laterale in contatto.
    const auto lateral=box(5,-1,1,2,4,2,71003);
    const QVector<ExtrusionObject> separate{above,lateral};
    const auto manualSeparate=build(profile,separate,{0,1},false);valid(manualSeparate,64,2);genealogy(manualSeparate,separate);
    const auto automaticSeparate=build(profile,separate,{0,1},true);valid(automaticSeparate,48,1);
    require(automaticSeparate.mergeBodies==QVector<int>({0}) && automaticSeparate.booleanTools==QVector<int>({0,1}),
            "la ricerca automatica esclude il destinatario separato mantenendolo tra i limiti dipendenti");
    // Due limiti coprono parti diverse del profilo. Entrambi diventano destinatari.
    const auto left=box(-1,-1,4,2,4,2,71004),right=box(1,-1,4,2,4,2,71005);
    for(bool automatic:{false,true}) {
        const QVector<ExtrusionObject> sources{left,right};const auto united=build(profile,sources,{0,1},automatic);
        valid(united,48,1);genealogy(united,sources);
        require(united.mergeBodies==QVector<int>({0,1}),"entrambi i limiti del profilo partecipano alla fusione");
    }
    // I destinatari si sovrappongono: la loro aggregazione deve usare la booleana generale.
    const auto overlappingLeft=box(-1,-1,4,3,4,2,71006),overlappingRight=box(0,-1,4,3,4,2,71007);
    for(bool automatic:{false,true}) {
        const QVector<ExtrusionObject> sources{overlappingLeft,overlappingRight};const auto united=build(profile,sources,{0,1},automatic);
        valid(united,48,1);genealogy(united,sources); // 24+24-16 dei target, piu' 16 di estrusione.
    }
    // Verso negativo: il target sopra la base e' disgiunto per semispazio, quello sotto per differenza.
    const auto below=box(-1,-1,-6,4,4,2,71008),atBase=box(-1,-1,0,4,4,2,71009);
    for(bool automatic:{false,true}) {
        const QVector<ExtrusionObject> sources{below,atBase};const auto united=build(profile,sources,{0,1},automatic,{},-1);
        valid(united,80,1);genealogy(united,sources);
    }
    // Un destinatario escluso dai limiti puo' penetrare il prisma: nessuna prova gli si estende.
    const auto penetrating=box(1,0,1,2,1,2,71010);
    const auto ordinary=build(profile,{above,penetrating},{0,1},false,{0});valid(ordinary,50,1);genealogy(ordinary,{above,penetrating});
    // Il wrapper pubblico non riceve mai prove, neppure per una definizione extent4.
    ExtrusionObject wrapper;wrapper.feature=BodyFeature::Extrusion;wrapper.extent=4;wrapper.mergeOperation=1;
    wrapper.mergeAuto=false;wrapper.mergeBodies={0};QString wrapperError;
    const auto overlappingPrism=forgeExtrusion(profile,7,&wrapperError);
    require(overlappingPrism && wrapperError.isEmpty(),"fixture prisma sovrapposto per wrapper pubblico");
    const auto wrapperShape=forgeMergeFeatureResult(wrapper,overlappingPrism,1,{above},&wrapperError);
    require(wrapperShape && wrapperError.isEmpty() && checkBody(*wrapperShape).empty()
            && std::abs(massProperties(*wrapperShape).volume-52)<1e-7,"wrapper generico conserva la vera unione di volumi sovrapposti");

    // Fascia 0 < hi <= resolution: l'ostacolo viene ignorato come limite, ma NON certificato disgiunto.
    // Il confronto e' con la booleana ordinaria del wrapper, senza attribuire significato a microvolumi.
    // L'assenza di attestazione hi>0 e' inoltre verificata nella review del ramo DisjointLimits.
    const double bandHeight=.5*kLinearResolution;
    const auto band=box(-1,-1,-2,4,4,2+bandHeight,71011);
    double hi=-1;for(VertexId vertex:band.forgeBody->vertices())hi=std::max(hi,band.forgeBody->vertex(vertex).point.z());
    require(hi>0 && hi<=kLinearResolution,"fixture nella fascia positiva esclusa dalla prova di semispazio");
    const QVector<ExtrusionObject> bandSources{above,band};const auto unmerged=build(profile,bandSources,{},false);
    valid(unmerged,16,1);
    wrapper.mergeBodies={1};wrapperError.clear();
    const auto bandExpected=forgeMergeFeatureResult(wrapper,unmerged.forgeBody,2,bandSources,&wrapperError);
    require(bandExpected && wrapperError.isEmpty(),qPrintable(QStringLiteral("Oracle ordinario della fascia: ")+wrapperError));
    for(bool automatic:{false,true}) {
        const auto actual=build(profile,bandSources,{1},automatic);
        require(actual.forgeBody && actual.error.isEmpty() && checkBody(*actual.forgeBody).empty(),
                qPrintable(QStringLiteral("Fascia positiva e fusione ordinaria: ")+actual.error));
        require(actual.mergeBodies==QVector<int>({1}) && actual.forgeBody->shells().size()==bandExpected->shells().size()
                && std::abs(massProperties(*actual.forgeBody).volume-massProperties(*bandExpected).volume)<1e-7,
                "fascia positiva: stesso risultato della booleana ordinaria senza prova");
    }
    std::cout<<"PASS unione prossimo corpo: auto/manuale, carrier curvo, limiti/destinatari multipli, semispazio, intersezione vuota, sovrapposizioni, genealogia e fascia positiva"<<std::endl;
}
