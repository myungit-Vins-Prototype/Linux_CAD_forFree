#pragma once

static void documentLifetimeTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto document=[] {
        DocumentState state;
        ExtrusionObject box; box.feature=BodyFeature::Primitive; box.name=QStringLiteral("Corpo durata");
        state.extrusions.append(box);
        SketchObject sketch; sketch.name=QStringLiteral("Schizzo durata");
        sketch.segments={{{-10,-10},{10,-10}}};
        state.sketches.append(sketch);
        return state;
    };
    const auto released=[](const auto &weak) {
        QElapsedTimer deadline;deadline.start();
        while(!weak.expired()&&deadline.elapsed()<3000) {
            QApplication::processEvents(QEventLoop::AllEvents,10);
            QThread::msleep(1);
        }
        return weak.expired();
    };
    // Due documenti reali: sostituire il primo non modifica o libera il secondo.
    CadViewport first,other;
    first.resize(900,680);other.resize(900,680);
    first.loadDocument(document());other.loadDocument(document());
    QThreadPool::globalInstance()->waitForDone();
    const auto otherBody=other.extrusions_[0].forgeBody;
    const auto otherBytes=writeBodyBinary(*otherBody);
    std::weak_ptr<const Body> previous=first.extrusions_[0].forgeBody;
    std::weak_ptr<const Surface> previousSurface=first.extrusions_[0].forgeBody->face(first.extrusions_[0].forgeBody->faces().front()).surface;

    // Il normale clic/rilascio di un estremo crea lo snapshot di drag,
    // anche quando non si e' ancora spostata la geometria.
    first.sketchMode_=true;first.activeSketch_=0;first.drawingTool_=DrawingTool::Select;
    first.geometrySnap_=false;first.gridSnap_=false;first.camera_.zoom_=40;
    first.setViewFrame(QVector3D(1,0,0),QVector3D(0,0,1));
    const auto pixel=first.projectWorldPoint(first.mapSketchPoint({-10,-10},first.sketches_[0]));
    QMouseEvent press(QEvent::MouseButtonPress,pixel,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    first.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,pixel,pixel,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    first.mouseReleaseEvent(&release);
    require(!first.dragSnapshot_.extrusions.isEmpty(),"clic reale acquisisce snapshot di trascinamento");
    first.endSketchMode();
    first.loadDocument(document());
    require(released(previous)&&previousSurface.expired(),"cambio documento libera corpo e superfici dello snapshot precedente");
    require(first.dragSnapshot_.extrusions.isEmpty()&&first.planeResizeSnapshot_.extrusions.isEmpty(),"nessuno snapshot transitorio del vecchio file");
    require(other.extrusions_[0].forgeBody==otherBody&&writeBodyBinary(*otherBody)==otherBytes,"secondo viewport conserva il proprio B-rep esatto");

    // Definizioni dei pannelli: contengono anche il risultato precedente.
    // Chiudere anteprima o maniglia deve rinunciare a quel proprietario.
    for(int tool=0;tool<2;++tool) {
        ExtrusionObject definition;definition.feature=BodyFeature::Primitive;
        definition.forgeBody=forgePrimitive(definition.primitive,nullptr);
        std::weak_ptr<const Body> previewBody=definition.forgeBody;
        if(tool==0)first.requestPreview(definition);
        else first.setFeatureHandle(definition,[](double){});
        definition.forgeBody.reset();
        if(tool==0)first.clearPreview();else first.clearFeatureHandle();
        require(released(previewBody),"chiusura pannello libera il corpo nella definizione transitoria");
    }

    // La sostituzione non e' un Undo: mentre il documento rimane aperto,
    // la sua cronologia deve continuare a tenere vivi i risultati necessari.
    previous=first.extrusions_[0].forgeBody;
    require(first.createPrimitive(PrimitiveParameters{},QStringLiteral("Secondo corpo")).isEmpty(),"modifica documento attivo");
    first.undo();require(first.extrusions_.size()==1&&!previous.expired(),"Undo mantiene la geometria necessaria");
    first.redo();require(first.extrusions_.size()==2,"Redo ancora disponibile");
    first.loadPreviewDocument({});
    require(released(previous)&&!first.history_.canUndo()&&!first.history_.canRedo(),"anteprima nuova scarta anche la vecchia cronologia");

    // Il viewport nascosto non riceve paintGL: il cambio deve liberare
    // subito anche le mesh CPU/GL, senza aspettare un nuovo fotogramma.
    if (QApplication::arguments().contains(QStringLiteral("--gl"))) {
        first.loadDocument(document());first.show();
        QElapsedTimer paint;paint.start();
        while(first.displayCache_.statistics().entries==0&&paint.elapsed()<3000) {
            QApplication::processEvents(QEventLoop::AllEvents,20);
            QThread::msleep(1);
        }
        require(first.isValid()&&first.displayCache_.statistics().entries>0,"contesto nativo e cache GL popolata");
        previous=first.extrusions_[0].forgeBody;
        first.hide();first.loadDocument({});
        require(first.displayCache_.statistics().entries==0&&released(previous),"viewport nascosto libera geometria e cache GL senza un paint");
    }

    // Una coda sospesa non deve possedere tutti i B-rep del documento.
    auto *pool=QThreadPool::globalInstance();pool->waitForDone();
    const int previousThreads=pool->maxThreadCount();pool->setMaxThreadCount(1);
    QSemaphore entered,unblock;
    pool->start([&]{entered.release();unblock.acquire();});entered.acquire();
    struct ReleasePool {
        QThreadPool *pool;QSemaphore &unblock;int threads;
        ~ReleasePool(){unblock.release();pool->waitForDone();pool->setMaxThreadCount(threads);}
    } releasePool{pool,unblock,previousThreads};
    first.loadDocument(document());
    previous=first.extrusions_[0].forgeBody;
    const auto cancellation=first.pickCacheCancellation_;
    first.loadDocument({});
    const bool cancelled=cancellation&&cancellation->load(std::memory_order_relaxed);
    const bool gone=released(previous);
    require(cancelled&&gone,"nuovo file cancella la coda senza trattenere i corpi vecchi");
    require(other.extrusions_[0].forgeBody==otherBody,"cancellazione confinata al viewport sostituito");
    std::cout<<"PASS durata documenti: drag reale, pannelli, Undo/Redo, preview e selezione in coda, secondo viewport"<<std::endl;
}
