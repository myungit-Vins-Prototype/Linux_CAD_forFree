// Incluso nella classe amica del viewport: verifica le cache, senza scrivere
// documenti e senza richiedere una finestra OpenGL.
static void lazyMeshTests() {
    using namespace ForgeCad;
    const bool wasSet=qEnvironmentVariableIsSet("FORGECAD_EAGER_MESH");
    const auto saved=qgetenv("FORGECAD_EAGER_MESH");
    struct Restore { bool set;QByteArray value;~Restore(){if(set)qputenv("FORGECAD_EAGER_MESH",value);else qunsetenv("FORGECAD_EAGER_MESH");}} restore{wasSet,saved};
    DocumentState source;
    ExtrusionObject base;base.name="Sfera";base.feature=BodyFeature::Primitive;
    base.primitive.kind=PrimitiveKind::Sphere;base.primitive.size[0]=10;
    source.extrusions.append(base);
    constexpr int count=20;
    for(int i=1;i<count;++i){
        ExtrusionObject step;step.name=QStringLiteral("Scala %1").arg(i);step.feature=BodyFeature::Scale;
        step.firstBody=i-1;step.scaleFactor=1.001;source.extrusions.append(step);
    }
    normalizeModelHistory(source);
    qputenv("FORGECAD_EAGER_MESH","0");
    CadViewport view;view.tessellationQuality_=0;view.loadDocument(source);
    int prepared=0;
    for(const auto &body:view.extrusions_) {
        require(body.forgeBody && body.error.isEmpty(),"mesh differita: tutti gli stadi geometrici validi");
        if(body.display.quality==0)++prepared;
    }
    require(prepared==1 && !view.extrusions_.last().display.vertices.isEmpty(),"mesh differita: preparato solo il risultato visibile");
    const auto tip=view.extrusions_.last().forgeBody;
    const double volume=Kernel::massProperties(*tip).volume;
    require(std::abs(volume-4.0/3.0*Kernel::kPi*1000*std::pow(1.001,3*(count-1)))<1e-6,"volume analitico indipendente dalla mesh");
    const auto old=view.documentState();
    view.setHistoryPosition(5);
    require(view.extrusions_[4].display.quality==0 && view.isBodyDrawn(4),"rollback materializza lo stadio richiesto");
    require(old.extrusions[4].display.quality==-1,"cache differita conserva snapshot precedente");
    view.setHistoryPosition(count);
    view.setTessellationQuality(1);
    require(view.extrusions_.last().display.quality==1 && view.extrusions_[4].display.quality==0,"qualita' aggiornata soltanto dove serve");
    view.setHistoryPosition(5);
    require(view.extrusions_[4].display.quality==1,"rollback rispetta la nuova qualita'");
    view.restoreDocument(old);
    require(view.extrusions_.last().forgeBody==tip && !view.extrusions_.last().display.vertices.isEmpty(),"restore conserva geometria e scena");
    QVector<double> times[2];
    for(int repeat=0;repeat<3;++repeat)for(int pass=0;pass<2;++pass){
        const int eager=(repeat%2)^pass;qputenv("FORGECAD_EAGER_MESH",eager?"1":"0");
        CadViewport work;work.tessellationQuality_=0;QElapsedTimer timer;timer.start();work.loadDocument(source);
        times[eager].append(timer.nsecsElapsed()/1e6);
        require(std::abs(Kernel::massProperties(*work.extrusions_.last().forgeBody).volume-volume)<1e-9,"mesh eager/differita: stessa geometria finale");
        int meshes=0;for(const auto &body:work.extrusions_)if(body.display.quality==0)++meshes;
        require(meshes==(eager?count:1),"numero di mesh richieste coerente");
    }
    for(auto &values:times)std::sort(values.begin(),values.end());
    std::cout<<"mesh history="<<count<<" eager_ms="<<times[1][1]<<" lazy_ms="<<times[0][1]
             <<" speedup="<<times[1][1]/times[0][1]<<" tessellations="<<count<<"->1"<<std::endl;
}
