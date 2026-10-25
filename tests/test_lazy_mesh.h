// Incluso nella classe amica del viewport: verifica le cache, senza scrivere
// documenti e senza richiedere un contesto OpenGL valido.
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
    // Una curva osservabile permette di verificare sovrapposizione reale dei
    // worker, richieste duplicate e snapshot, senza asserzioni sui tempi.
    struct Observation {
        std::mutex mutex;
        std::condition_variable ready;
        int started=0, active=0, overlap=0;
        bool waitForGui=false, released=false;
        std::atomic<int> calls{0};
    };
    struct ObservedCurve final : Kernel::Curve<3> {
        std::shared_ptr<Observation> observation;
        mutable std::atomic<bool> first{true};
        double offset;
        bool fail;
        ObservedCurve(std::shared_ptr<Observation> state,double x,bool failure=false)
            : observation(std::move(state)),offset(x),fail(failure) {}
        Kernel::CurveType type() const override { return Kernel::CurveType::Other; }
        Kernel::Interval domain() const override { return {0,1}; }
        void evaluate(double t,int order,Kernel::Vec3 *out) const override {
            ++observation->calls;
            if (first.exchange(false)) {
                std::unique_lock<std::mutex> lock(observation->mutex);
                ++observation->started;
                ++observation->active;
                observation->overlap=std::max(observation->overlap,observation->active);
                observation->ready.notify_all();
                if (Kernel::threadCount(0)>1 && observation->started==1)
                    observation->ready.wait_for(lock,std::chrono::seconds(1),[&]{return observation->started>1;});
                if (observation->waitForGui)
                    observation->ready.wait_for(lock,std::chrono::seconds(2),[&]{return observation->released;});
                --observation->active;
            }
            if (fail) throw std::runtime_error("curva diagnostica");
            out[0]=Kernel::Vec3(offset+t,0,0);
            for (int i=1;i<=order;++i) out[i]=i==1 ? Kernel::Vec3(1,0,0) : Kernel::Vec3();
        }
    };
    auto observation=std::make_shared<Observation>();
    CadViewport batch;batch.tessellationQuality_=0;
    const int parallelCount=std::max(40,int(Kernel::threadCount(0))*4+1);
    for (int i=0;i<=parallelCount;++i) {
        ExtrusionObject curve;curve.feature=BodyFeature::Helix;curve.featureId=i+1;
        curve.curve=std::make_shared<ObservedCurve>(observation,i);
        curve.visible=i<parallelCount;batch.extrusions_.append(curve);
    }
    const auto untouched=batch.documentState();
    QVector<int> requested;
    for (int i=parallelCount-1;i>=0;--i) requested.append(i);
    requested << 0 << 0 << -1 << parallelCount+9;
    batch.ensureBodyDisplays(requested);
    require(Kernel::threadCount(0)==1 || observation->overlap>1,"mesh batch: worker realmente sovrapposti");
    require(observation->calls==parallelCount*201,"mesh batch: ogni richiesta calcolata una sola volta");
    for (int i=0;i<parallelCount;++i) {
        require(batch.extrusions_[i].display.quality==0 && batch.extrusions_[i].display.edges.size()==1,"mesh batch: tutte le curve richieste pronte");
        require(batch.extrusions_[i].display.edges[0][0]==QVector3D(i,0,0),"mesh batch: risultati pubblicati sull'indice corretto");
        require(untouched.extrusions[i].display.quality==-1 && untouched.extrusions[i].display.edges.isEmpty(),"mesh batch: snapshot immutabile");
    }
    require(batch.extrusions_.last().display.quality==-1,"mesh batch: curva nascosta non richiesta");
    batch.ensureVisibleBodyDisplays();
    require(observation->calls==parallelCount*201,"mesh batch: cache gia pronte riusate");
    const auto previousQuality=batch.documentState();
    batch.tessellationQuality_=1;batch.ensureVisibleBodyDisplays();
    require(observation->calls==parallelCount*(201+401),"mesh batch: nuova qualita' calcolata su piu' gruppi");
    for (int i=0;i<parallelCount;++i)
        require(batch.extrusions_[i].display.quality==1 && previousQuality.extrusions[i].display.quality==0,"mesh batch: sostituzione a gruppi conserva la vecchia istantanea");
    batch.tessellationQuality_=0;
    // Una cancellazione o un errore non pubblicano risultati parziali del gruppo.
    for (auto &curve:batch.extrusions_) curve.display={};
    bool cancelled=false;
    try {
        Kernel::CancellationScope scope(std::make_shared<std::atomic<bool>>(true));
        batch.ensureVisibleBodyDisplays();
    } catch (const Kernel::OperationCancelled &) { cancelled=true; }
    require(cancelled,"mesh batch: cancellazione propagata");
    for (const auto &curve:batch.extrusions_) require(curve.display.quality==-1,"mesh batch: cancellazione senza pubblicazione parziale");
    batch.extrusions_[0].curve=std::make_shared<ObservedCurve>(observation,0,true);
    bool failed=false;
    try { batch.ensureVisibleBodyDisplays(); } catch (const std::runtime_error &) { failed=true; }
    require(failed,"mesh batch: errore worker propagato");
    for (const auto &curve:batch.extrusions_) require(curve.display.quality==-1,"mesh batch: errore senza pubblicazione parziale");
    // Il worker mantiene la GUI attiva: un cambio qualita' rende superato il
    // gruppo in corso e un paint/richiesta rientrante non lo avvia di nuovo.
    auto liveObservation=std::make_shared<Observation>();
    liveObservation->waitForGui=true;
    CadViewport live;live.tessellationQuality_=0;
    for (int i=0;i<parallelCount;++i) {
        ExtrusionObject curve;curve.feature=BodyFeature::Helix;curve.featureId=i+1;
        curve.curve=std::make_shared<ObservedCurve>(liveObservation,i);live.extrusions_.append(curve);
    }
    live.setAttribute(Qt::WA_DontShowOnScreen);live.show();
    bool guiTick=false,protectedWorker=false;
    QTimer::singleShot(0,&live,[&] {
        guiTick=true;protectedWorker=live.responsiveWork_>0;
        live.ensureVisibleBodyDisplays();
        live.tessellationQuality_=1;
        std::lock_guard<std::mutex> lock(liveObservation->mutex);
        liveObservation->released=true;liveObservation->ready.notify_all();
    });
    live.ensureVisibleBodyDisplays();live.hide();
    require(guiTick && protectedWorker,"mesh batch: GUI attiva e rientro protetto durante il worker");
    for (const auto &curve:live.extrusions_) require(curve.display.quality==-1,"mesh batch: risultato di qualita' superata scartato");
    require(liveObservation->calls==parallelCount*201,"mesh batch: nessun lavoro duplicato durante il rientro");
    live.ensureVisibleBodyDisplays();
    for (const auto &curve:live.extrusions_) require(curve.display.quality==1,"mesh batch: successiva richiesta usa la nuova qualita'");
    // Un assieme con molti corpi indipendenti esercita anche la costruzione
    // e mesh nello stesso worker, conservando differiti i risultati nascosti.
    DocumentState assembly;
    for (int i=0;i<parallelCount;++i) {auto body=base;body.primitive.size[0]=1.0+i*.01;assembly.extrusions.append(body);}
    normalizeModelHistory(assembly);
    assembly.modelBodies.last().visible=false;assembly.extrusions.last().visible=false;
    CadViewport assembled;assembled.tessellationQuality_=0;assembled.loadDocument(assembly);
    require(assembled.extrusions_.last().forgeBody && assembled.extrusions_.last().display.quality==-1,"mesh pipeline: geometria nascosta pronta senza mesh");
    qputenv("FORGECAD_EAGER_MESH","1");
    CadViewport reference;reference.tessellationQuality_=0;reference.loadDocument(assembly);
    qputenv("FORGECAD_EAGER_MESH","0");
    for (int i=0;i<parallelCount;++i) {
        const auto &a=assembled.extrusions_[i],&b=reference.extrusions_[i];
        require(Kernel::writeBodyBinary(*a.forgeBody)==Kernel::writeBodyBinary(*b.forgeBody),"mesh pipeline: stessa geometria del riferimento");
        if (i<parallelCount-1) require(a.display.vertices==b.display.vertices && a.display.normals==b.display.normals,"mesh pipeline: stesse mesh dei corpi richiesti");
    }
    DocumentState repeated;
    auto sphere=base;sphere.primitive.size[0]=2;repeated.extrusions.append(sphere);
    ExtrusionObject pattern;pattern.feature=BodyFeature::Pattern;pattern.firstBody=0;
    pattern.pattern.count=3;pattern.pattern.spacing=10;
    GeometryRef axis;axis.kind=2;axis.index=0;pattern.pattern.refs={axis};
    repeated.extrusions.append(pattern);normalizeModelHistory(repeated);
    CadViewport instances;instances.tessellationQuality_=0;instances.loadDocument(repeated);
    for (auto &body:instances.extrusions_) body.display={};
    const auto beforeInstances=instances.documentState();
    instances.ensureBodyDisplays({1,1});
    require(instances.extrusions_[0].display.quality==0,"mesh batch: base della ripetizione preparata anche se non richiesta");
    const auto &display=instances.extrusions_[1].display;
    require(display.instancedBase && display.instanceTransforms.size()==3,"mesh batch: istanze configurate dopo la base");
    require(display.instancedBase->vertices.constData()==instances.extrusions_[0].display.vertices.constData(),"mesh batch: istanze condividono il buffer della base");
    require(beforeInstances.extrusions[0].display.quality==-1 && beforeInstances.extrusions[1].display.quality==-1,"mesh batch: dipendenze non mutano snapshot");
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
