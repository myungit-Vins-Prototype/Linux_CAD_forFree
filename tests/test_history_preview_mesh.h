// Sonda di storia/anteprima sul documento originale, senza salvataggi CAD.
static void historyPreviewMesh(const QString &path, bool gl) {
    using namespace ForgeCad;
    QFile input(path);QByteArray original;
    DocumentState state;
    if(!path.isEmpty()) {
        require(input.open(QIODevice::ReadOnly),"lettura documento anteprima storia");
        original=input.readAll();input.close();
        require(decodeDocument(original,state,true).isEmpty(),"decode anteprima storia");
    } else {
        SketchObject sketch;sketch.segments={{{0,0},{8,0}},{{8,0},{8,6}},{{8,6},{0,6}},{{0,6},{0,0}}};
        state.sketches.append(sketch);
        ExtrusionObject sides;sides.distance=5;sides.sketchIndex=0;sides.extrudeSurface=true;state.extrusions.append(sides);
        ExtrusionObject cap;cap.feature=BodyFeature::PlanarSurface;cap.sketchIndex=0;state.extrusions.append(cap);
        ExtrusionObject sew;sew.feature=BodyFeature::Sew;sew.firstBody=0;sew.booleanTools={1};state.extrusions.append(sew);
        ExtrusionObject next;next.feature=BodyFeature::Scale;next.firstBody=2;next.scaleFactor=1.01;state.extrusions.append(next);
        normalizeModelHistory(state);
    }
    if(qEnvironmentVariableIsSet("FORGECAD_HISTORY_PREVIEW_REBUILD"))
        for(auto &body:state.extrusions){body.cachedGeometry=false;body.forgeBody.reset();body.display={};}
    CadViewport view;view.resize(1000,760);view.tessellationQuality_=1;view.loadDocument(state);
    int index=-1;
    for(int i=0;i<view.extrusions_.size();++i)
        if(view.extrusions_[i].feature==BodyFeature::PlanarSurface)index=i;
    require(index>=0,"superficie planare presente");
    const auto definition=view.extrusions_.at(index);
    const auto unchanged=view.documentState();
    if(gl){view.setViewPreset(4);view.show();QApplication::processEvents();view.fitView(view.sceneGeometryPoints());QApplication::processEvents();}
    const auto record=[&](const char *phase) {
        QJsonArray bodies;
        for(int i=0;i<view.extrusions_.size();++i) {
            const auto &b=view.extrusions_.at(i);const auto &d=b.display;
            QVector3D lo,hi;if(!d.vertices.isEmpty()){lo=hi=d.vertices.first();for(const auto &p:d.vertices)for(int k=0;k<3;++k){lo[k]=qMin(lo[k],p[k]);hi[k]=qMax(hi[k],p[k]);}}
            bodies.append(QJsonObject{{"index",i},{"name",b.name},{"drawn",view.isBodyDrawn(i)},
                {"restored",view.preview_.restored.contains(i)},{"later",view.preview_.later.contains(i)},
                {"geometry",bool(b.forgeBody)},{"quality",d.quality},{"vertices",double(d.vertices.size())},
                {"normals",double(d.normals.size())},{"edges",d.edges.size()},
                {"lo",QJsonArray{lo.x(),lo.y(),lo.z()}},{"hi",QJsonArray{hi.x(),hi.y(),hi.z()}}});
        }
        std::cout<<QJsonDocument(QJsonObject{{"phase",phase},{"bodies",bodies},{"preview_vertices",double(view.preview_.display.vertices.size())},
            {"preview_valid",view.preview_.valid},{"preview_error",view.preview_.error}}).toJson(QJsonDocument::Compact).constData()<<std::endl;
    };
    const auto capture=[&](const QString &name) {
        if(!gl)return QImage();
        const auto directory=qEnvironmentVariable("FORGECAD_HISTORY_PREVIEW_QA_DIR");
        const auto image=view.grabFramebuffer();
        if(!directory.isEmpty())require(image.save(directory+"/"+name+".png"),"salvataggio immagine anteprima storia");
        view.makeCurrent();std::cerr<<"GL "<<name.toStdString()<<" error="<<view.context()->functions()->glGetError()<<std::endl;view.doneCurrent();
        return image;
    };
    record("loaded");capture("loaded");
    for(int repeat=0;repeat<3;++repeat) {
        view.setReferenceMarks(definition.planarRefs);view.requestPreview(definition,index);
        QElapsedTimer wait;wait.start();
        while((!view.preview_.valid || view.previewRunning_) && view.preview_.error.isEmpty() && wait.elapsed()<20000)QApplication::processEvents();
        require(view.preview_.valid && !view.previewRunning_,qPrintable("anteprima storia: "+view.preview_.error));
        require(view.preview_.geometry==view.extrusions_.at(index).forgeBody,"editing invariato riusa il B-rep");
        require(!view.preview_.display.vertices.isEmpty(),"anteprima contiene la superficie");
        require(!view.preview_.restored.isEmpty(),"operandi nascosti ripristinati nella storia");
        const auto geometry=view.preview_.geometry;
        record("preview-before-paint");const auto first=capture(QString("preview-%1").arg(repeat));record("preview-after-paint");
        if(gl) {
            view.makeCurrent();view.displayCache_.clear();view.doneCurrent();
            const auto fresh=capture(QString("preview-cleared-%1").arg(repeat));
            require(!first.isNull() && first.size()==fresh.size(),"fotogrammi di anteprima disponibili");
            int changed=0,maximum=0;
            for(int y=0;y<first.height();++y)for(int x=0;x<first.width();++x) {
                const auto a=first.pixelColor(x,y),b=fresh.pixelColor(x,y);
                const int delta=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
                maximum=std::max(maximum,delta);changed+=delta>2;
            }
            std::cout<<"FRAME_COMPARE repeat="<<repeat<<" changed="<<changed<<" maximum="<<maximum<<std::endl;
            require(changed<=32,"prima anteprima uguale a cache GL ricreata: nessun corpo assente o triangolo spurio");
            require(view.preview_.geometry==geometry,"ricreazione cache GL non modifica la geometria");
        }
        view.clearPreview();view.setReferenceMarks({});capture(QString("closed-%1").arg(repeat));
    }
    for(int i=0;i<view.extrusions_.size();++i)
        require(view.extrusions_.at(i).forgeBody==unchanged.extrusions.at(i).forgeBody,"nessun B-rep modificato dalle riaperture");
    if(!path.isEmpty())require(input.open(QIODevice::ReadOnly)&&input.readAll()==original,"originale anteprima storia invariato");
    std::cout<<"PASS riaperture anteprima storia"<<std::endl;
}
