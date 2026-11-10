static void surfacePolesSoft(bool gl=false) {
    using namespace ForgeCad;using namespace Kernel;
    {
        auto strip=std::make_shared<const Body>(surface_fixture::strip());
        const auto ids=strip->faces();QVector<FaceId> faces(ids.cbegin(),ids.cend());
        SurfaceToolParameters p;p.mode=2;p.poleSoft=true;p.poleContinuity=2;
        SurfacePolesSession session;SurfacePolesModel model;
        clientOk(session.load(strip,faces,p,model));
        clientOk(model.translatePoles({13},{0,0,.1},1,1));session.setDrivers({13});clientOk(session.constrain(model,p));
        ExtrusionObject source;source.forgeBody=strip;source.feature=BodyFeature::Imported;
        ExtrusionObject edited;edited.feature=BodyFeature::SurfaceTools;edited.firstBody=0;edited.surfaceTool=p;
        buildSurfaceTool(edited,1,{}, {source});
        require(edited.error.isEmpty()&&edited.forgeBody,"influenza e vincoli dei giunti compatibili");
        require(checkSurfacePoleContinuity(*strip,*edited.forgeBody,strip->faces(),1e-6).angle<1e-9,"G1 conservata con influenza attiva");
        p.poleContinuity=0;clientOk(session.load(strip,{faces[0]},p,model));
        clientOk(model.translatePoles({5},{0,0,.1},1,1));session.setDrivers({5});clientOk(session.constrain(model,p));
        require(!model.automaticRows().isEmpty(),"influenza funziona senza G0/G1");
        clientOk(session.store(model,p));require(p.poleFaces[0].edits.size()>1,"vicini persistiti anche senza vincoli dei giunti");
    }
    QMainWindow window;auto *view=new CadViewport(&window);window.setCentralWidget(view);window.resize(1500,1000);
    view->tessellationQuality_=0;if(gl){window.show();QApplication::processEvents();}
    ExtrusionObject imported;imported.feature=BodyFeature::Imported;imported.name="Faccia isolata";
    imported.importData=QByteArray::fromStdString(writeStep({{"Patch",surface_fixture::patch(),false,{0,0,0},nullptr,{}}}));
    DocumentState doc;doc.extrusions={imported};view->loadDocument(doc);view->setViewPreset(4);view->fitAll();
    ExtrusionObject definition;definition.feature=BodyFeature::SurfaceTools;definition.firstBody=0;
    definition.surfaceTool.mode=2;definition.surfaceTool.refitU=8;definition.surfaceTool.refitV=108;
    definition.surfaceTool.refitTolerance=.01;definition.surfaceTool.poleContinuity=2;
    constexpr int driver=48,nearby=49,distant=68;
    std::array<double,4> driverResult,neighborResult;
    std::exception_ptr failure;
    QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog *>("surfaceToolDialog");try{
        auto *table=dialog->findChild<QTableView *>("surfacePolesTable");auto *model=static_cast<SurfacePolesModel *>(table->model());
        auto *soft=dialog->findChild<QCheckBox *>("surfacePolesSoft");auto *u=dialog->findChild<QSpinBox *>("surfacePolesRadiusU"),*v=dialog->findChild<QSpinBox *>("surfacePolesRadiusV");
        require(model->rowCount()==864 && soft && !soft->isChecked(),"griglia8x108 e opzione morbida separata dal G1");
        require(dialog->findChild<QPlainTextEdit *>("surfacePolesContinuityInfo")->toPlainText().contains("tra facce"),"distinzione del vincolo su faccia isolata");
        soft->setChecked(true);u->setValue(3);v->setValue(6);
        auto *interaction=static_cast<SurfacePolesInteraction *>(dialog->findChild<QObject *>("surfacePolesInteraction"));
        const auto wait=[&](int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms)QApplication::processEvents();};
        const auto send=[&](QEvent::Type type,QPointF point){QMouseEvent e(type,point,view->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&e);};
        const auto baseline=model->values(driver),oldNeighbor=model->values(nearby),oldDistant=model->values(distant);
        table->selectRow(driver);QApplication::processEvents();
        for(int pass=0;pass<2;++pass){
            const auto point=view->surfacePoleAxisScreen(2);
            send(QEvent::MouseButtonPress,point);require(interaction->interacting()&&interaction->draggedRows().contains(driver),"trascinamento del polo selezionato");
            send(QEvent::MouseMove,point+QPointF(0,-12));
            const auto moved=model->values(driver),neighbor=model->values(nearby);
            require(std::abs(moved[2]-baseline[2])>1e-5 && std::abs(neighbor[2]-oldNeighbor[2])>1e-5,"il vicino non selezionato segue sulla stessa faccia");
            require(std::abs(neighbor[2]-oldNeighbor[2])<std::abs(moved[2]-baseline[2]),"influenza decrescente");
            require(model->values(distant)==oldDistant && model->automaticRows().contains(nearby),"campo locale e vicini automatici");
            require(!view->surfacePoleFollowers_.isEmpty(),"compensativi verdi nella vista");
            send(QEvent::MouseMove,point+QPointF(0,-12));require(model->values(nearby)==neighbor,"nessun accumulo nello stesso gesto");
            if(pass==0){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(dialog,&escape);
                require(!interaction->interacting()&&model->values(driver)==baseline&&model->values(nearby)==oldNeighbor&&model->automaticRows().isEmpty(),"Esc ripristina anche il campo morbido");}
            else {send(QEvent::MouseButtonRelease,point+QPointF(0,-12));require(!interaction->interacting(),"rilascio termina il gesto morbido");}
        }
        auto *ok=dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok);wait(120);
        QElapsedTimer ready;ready.start();while((!ok->isEnabled()||view->previewPending())&&ready.elapsed()<10000)QApplication::processEvents();
        require(ok->isEnabled()&&view->previewError().isEmpty(),"anteprima morbida valida");
        driverResult=model->values(driver);neighborResult=model->values(nearby);
        soft->setChecked(false);require(model->values(nearby)==neighborResult,"disattivare conserva la forma risolta");
        soft->setChecked(true);u->setValue(4);require(model->values(nearby)==neighborResult,"cambiare raggio conserva la forma");
        wait(120);ready.restart();while((!ok->isEnabled()||view->previewPending())&&ready.elapsed()<10000)QApplication::processEvents();
        require(ok->isEnabled(),"conferma dopo modifica delle impostazioni");
        if(gl){window.grab().save("/tmp/forgecad-poles-soft/fixture-window.png");view->grabFramebuffer().save("/tmp/forgecad-poles-soft/fixture-cage.png");}
        ok->click();
    }catch(...){failure=std::current_exception();dialog->reject();}});
    require(surfaceToolDialog(&window,view,"Trascinamento morbido",-1,definition,[&](const auto &d){return view->createBody(d);})||bool(failure),"conferma funzione morbida");if(failure)std::rethrow_exception(failure);
    const auto result=view->extrusions_.back();auto disabled=result;disabled.surfaceTool.poleSoft=false;require(CadViewport::previewKey(disabled,1)!=CadViewport::previewKey(result,1),"influenza nella chiave di anteprima");require(result.surfaceTool.poleSoft&&result.surfaceTool.poleRadiusU==4&&result.surfaceTool.poleRadiusV==6,"impostazioni salvate");
    const auto support=std::dynamic_pointer_cast<const BSplineSurface>(result.forgeBody->face(result.forgeBody->faces()[0]).surface);
    require(distance(support->pole(0,49),{neighborResult[0],neighborResult[1],neighborResult[2]})<1e-10,"vicino salvato nella geometria");
    require(checkBody(*result.forgeBody,{true,true}).empty(),"B-rep morbido valido");view->undo();require(view->extrusions_.size()==1,"Undo");view->redo();require(view->extrusions_.size()==2,"Redo");
    QByteArray bytes;clientOk(encodeDocument(view->documentState(),bytes));DocumentState decoded;clientOk(decodeDocument(bytes,decoded));require(decoded.extrusions.back().surfaceTool.poleSoft,"codec conserva impostazione");
    auto extras=QJsonDocument::fromJson(surfaceToolsExtras(view->extrusions_)).object();require(extras["schema"].toInt()==15,"schema15 influenza");extras["schema"]=14;auto invalid=view->extrusions_;
    require(!applySurfaceToolsExtras(QJsonDocument(extras).toJson(),invalid).isEmpty(),"rifiuto influenza su vecchio schema");
    decoded.extrusions.back().forgeBody.reset();decoded.extrusions.back().cachedGeometry=false;decoded.extrusions.back().display={};CadViewport rebuilt;rebuilt.loadDocument(decoded);
    require(rebuilt.extrusions_.back().error.isEmpty()&&rebuilt.extrusions_.back().forgeBody,"rigenerazione senza cache del risultato");
    QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog *>("surfaceToolDialog");try{auto *model=static_cast<SurfacePolesModel *>(dialog->findChild<QTableView *>("surfacePolesTable")->model());require(dialog->findChild<QCheckBox *>("surfacePolesSoft")->isChecked()&&model->values(nearby)==neighborResult,"riapertura conserva opzione e poli risolti");}catch(...){failure=std::current_exception();}dialog->reject();});
    surfaceToolDialog(&window,view,"Riapertura morbida",1,result,[](const auto&){return QString();});if(failure)std::rethrow_exception(failure);
}
