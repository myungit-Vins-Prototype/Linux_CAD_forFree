// Sonda esplicita: ciclo bassa/massima/bassa, nessun salvataggio del modello.
// Il trim e' diagnostico e viene eseguito solo con --memory-trim.
static void meshMemoryBenchmark(const QString &path,bool gl) {
    using namespace ForgeCad;
    const bool trim=QApplication::arguments().contains(QStringLiteral("--memory-trim"));
    CadViewport view;view.resize(640,480);view.tessellationQuality_=0;
    const auto snapshot=[&](const QString &stage) {
        QJsonObject row{{"stage",stage},{"quality",view.tessellationQuality_},{"features",view.extrusions_.size()},
                        {"frames",double(view.renderedFrameSerial_)}};
        std::set<const void *> arrays;quint64 bytes=0,triangles=0;
        const auto account=[&](auto &&visit,const BodyDisplay &d) -> void {
            const auto add=[&](const auto &a) {
                if(!a.isEmpty() && arrays.insert(a.constData()).second)bytes+=quint64(a.capacity())*sizeof(*a.constData());
            };
            add(d.vertices);add(d.normals);add(d.edges);for(const auto &e:d.edges)add(e);
            add(d.edgeIds);add(d.faceIds);add(d.triangleFaces);add(d.faceLabelPoints);
            add(d.faceEdges);for(const auto &e:d.faceEdges)add(e);add(d.instanceTransforms);
            if(d.instancedBase)visit(visit,*d.instancedBase);
        };
        for(const auto &body:view.extrusions_) {account(account,body.display);triangles+=body.display.vertices.size()/3;}
        const auto cache=view.displayCache_.statistics();
        row["mesh_storage_bytes"]=double(bytes);row["triangles"]=double(triangles);
        row["render_cache_entries"]=double(cache.entries);row["render_cache_source_bytes"]=double(cache.sourceStorageBytes);
        row["render_cache_ready_buffer_bytes"]=double(cache.readyBufferBytes);
        QFile status(QStringLiteral("/proc/self/smaps_rollup"));
        if(status.open(QIODevice::ReadOnly))for(const auto &line:status.readAll().split('\n')) {
            const auto fields=line.simplified().split(' ');
            if(fields.size()>=2 && (fields[0]=="Rss:" || fields[0]=="Private_Dirty:" || fields[0]=="Private_Clean:"))
                row[QString::fromLatin1(fields[0])]=double(fields[1].toULongLong()*1024);
        }
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2,33)
        const auto allocator=mallinfo2();row["heap_arena_bytes"]=double(allocator.arena);
        row["heap_used_bytes"]=double(allocator.uordblks);row["heap_free_bytes"]=double(allocator.fordblks);
        row["heap_mapped_bytes"]=double(allocator.hblkhd);
#endif
#endif
        std::cout<<QJsonDocument(row).toJson(QJsonDocument::Compact).constData()<<std::endl;
    };
    const auto paint=[&] {
        if(!gl)return;
        const auto before=view.renderedFrameSerial_;view.update();
        QElapsedTimer deadline;deadline.start();
        while(view.renderedFrameSerial_==before && deadline.elapsed()<5000) {
            QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
        }
        require(view.renderedFrameSerial_>before,"la sonda attende un nuovo fotogramma reale");
        view.makeCurrent();QOpenGLContext::currentContext()->functions()->glFinish();view.doneCurrent();
    };
    snapshot(QStringLiteral("empty"));
    DocumentState source;const auto error=loadDocumentFile(path,source);
    require(error.isEmpty(),qPrintable(error));snapshot(QStringLiteral("read"));
    const int limit=qEnvironmentVariableIntValue("FORGECAD_MEMORY_BODY_LIMIT");
    if(limit>0 && limit<source.extrusions.size()) {source.extrusions.resize(limit);source.modelBodies.clear();}
    view.loadDocument(std::move(source));snapshot(QStringLiteral("low-loaded"));
    std::vector<ForgeBody> geometry;for(const auto &body:view.extrusions_)geometry.push_back(body.forgeBody);
    const auto signature=[&] {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for(const auto &body:view.extrusions_) {
            const auto &d=body.display;
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(d.vertices.constData()),d.vertices.size()*sizeof(QVector3D)));
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(d.normals.constData()),d.normals.size()*sizeof(QVector3D)));
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(d.triangleFaces.constData()),d.triangleFaces.size()*sizeof(int)));
        }
        return hash.result();
    };
    const auto low=signature();
    if(gl) {view.show();QApplication::processEvents();for(int i=0;i<4;++i)paint();}
    snapshot(QStringLiteral("low-painted"));
    view.setTessellationQuality(2);snapshot(QStringLiteral("high-updated"));paint();snapshot(QStringLiteral("high-frame1"));
    for(int i=0;i<4;++i)paint();
    snapshot(QStringLiteral("high-frame5"));
    view.setTessellationQuality(0);snapshot(QStringLiteral("low-updated"));paint();snapshot(QStringLiteral("low-frame1"));
    for(int i=0;i<4;++i)paint();
    snapshot(QStringLiteral("low-frame5"));
    require(low==signature(),"ciclo qualita': mesh bassa riprodotta esattamente");
    for(int i=0;i<view.extrusions_.size();++i)require(geometry[std::size_t(i)]==view.extrusions_[i].forgeBody,"B-rep non sostituito dal cambio qualita'");
#if defined(__GLIBC__)
    if(trim){malloc_trim(0);snapshot(QStringLiteral("low-trimmed"));}
#endif
    if(gl){view.makeCurrent();view.displayCache_.clear();QOpenGLContext::currentContext()->functions()->glFinish();view.doneCurrent();}
    snapshot(QStringLiteral("low-cache-cleared"));
#if defined(__GLIBC__)
    if(trim){malloc_trim(0);snapshot(QStringLiteral("low-cleared-trimmed"));}
#endif
    view.hide();std::cerr<<"PASS ciclo qualita': geometria e mesh bassa invariate\n";
}
