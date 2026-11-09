static void surfacePolesMultiple(bool gl=false) {
    using namespace ForgeCad;using namespace Kernel;
    QMainWindow window;auto *view=new CadViewport(&window);window.setCentralWidget(view);
    view->tessellationQuality_=0;
    if(gl){window.resize(1500,1000);window.show();QApplication::processEvents();}
    ExtrusionObject imported;imported.feature=BodyFeature::Imported;imported.name="Tre facce NURBS";
    imported.importData=QByteArray::fromStdString(writeStep({{"Strip",surface_fixture::strip(),false,{0,0,0},nullptr,{}}}));
    DocumentState document;document.extrusions={imported};view->loadDocument(document);
    require(view->extrusions_[0].forgeBody && view->extrusions_[0].forgeBody->faces().size()==3,"sorgente multifaccia");
    const auto source=view->extrusions_[0].forgeBody;const auto before=writeBodyBinary(*source);
    view->setReferencePlanesVisible(false);view->setAxesVisible(false);view->setGridVisible(false);view->setViewNormal(0);view->setViewPreset(4);view->fitAll();
    ExtrusionObject definition;definition.feature=BodyFeature::SurfaceTools;definition.surfaceTool.mode=2;definition.firstBody=0;
    std::exception_ptr failure;
    QTimer::singleShot(0,&window,[&] {
        auto *dialog=window.findChild<QDialog *>("surfaceToolDialog");
        try {
            require(dialog,"pannello multifaccia aperto");
            auto *list=dialog->findChild<QListWidget *>("surfacePolesFaces");
            auto *table=dialog->findChild<QTableView *>("surfacePolesTable");
            auto *model=static_cast<SurfacePolesModel *>(table->model());
            require(list&&list->count()==3&&model->rowCount()==48&&model->columnCount()==7,"tutte le facce disponibili senza Copia facce");
            require(view->surfacePoleGrids_.size()==3&&view->surfacePoleOverlay_.size()==48,"tre gabbie simultanee");
            require(model->index(16,0).data().toString()!=model->index(0,0).data().toString(),"faccia riconoscibile in tabella");
            list->item(2)->setCheckState(Qt::Unchecked);
            require(model->rowCount()==32&&view->surfacePoleGrids_.size()==2,"deselezione rimuove la sola griglia");
            auto *interaction=static_cast<SurfacePolesInteraction *>(dialog->findChild<QObject *>("surfacePolesInteraction"));
            require(interaction,"gestore interazione poli disponibile");
            const auto mouse=[&](QObject *target,QEvent::Type type,QPointF point,Qt::KeyboardModifiers mods=Qt::NoModifier,Qt::MouseButtons buttons=Qt::LeftButton) {
                const auto global=view->mapToGlobal(point.toPoint());
                QMouseEvent e(type,point,global,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonRelease?Qt::NoButton:buttons,mods);QApplication::sendEvent(target,&e);
            };
            QRectF box;bool firstPoint=true;
            for(int i=0;i<view->surfacePoleOverlay_.size();++i){const auto point=view->surfacePoleScreen(i);if(firstPoint){box=QRectF(point,QSizeF(.001,.001));firstPoint=false;}else box=box.united(QRectF(point,QSizeF(.001,.001)));}
            box=box.adjusted(-16,-16,16,16);
            const auto rectangle=[&](Qt::KeyboardModifiers mods,bool cancel=false) {
                mouse(view,QEvent::MouseButtonPress,box.topLeft(),mods);require(interaction->interacting(),"riquadro attivo");
                mouse(view,QEvent::MouseMove,box.bottomRight(),mods);
                if(cancel){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(dialog,&escape);}
                else mouse(view,QEvent::MouseButtonRelease,box.bottomRight(),mods);
                require(!interaction->interacting(),"riquadro termina senza bloccare il mouse");
            };
            // Selezionare piu' nodi non intercetta la normale rotazione.
            auto *boxButton=dialog->findChild<QPushButton *>("surfacePolesBoxSelection");
            require(boxButton && !boxButton->isChecked(),"riquadro esplicito disattivato all'apertura");
            mouse(view,QEvent::MouseButtonPress,box.topLeft());
            require(!interaction->interacting(),"clic sullo sfondo disponibile per la rotazione");
            mouse(view,QEvent::MouseButtonRelease,box.topLeft());
            boxButton->click();
            rectangle(Qt::NoModifier);require(table->selectionModel()->selectedRows().size()==32,"riquadro seleziona i nodi di entrambe le facce");
            rectangle(Qt::ControlModifier);require(table->selectionModel()->selectedRows().isEmpty(),"Ctrl e riquadro tolgono i nodi");
            rectangle(Qt::ShiftModifier);require(table->selectionModel()->selectedRows().size()==32,"Maiusc e riquadro aggiungono i nodi");
            rectangle(Qt::NoModifier,true);require(table->selectionModel()->selectedRows().size()==32,"Esc del riquadro conserva la selezione iniziale");
            boxButton->click();
            rectangle(Qt::AltModifier);
            require(table->selectionModel()->selectedRows().size()==32,"Alt attiva il riquadro senza cambiare modalita'");
            if(gl) {
                const auto camera=view->camera_.yaw_;
                const QPointF background(view->width()-20,view->height()-20);
                mouse(view,QEvent::MouseButtonPress,background);
                require(!interaction->interacting(),"sfondo senza maniglie lascia passare il gesto di rotazione");
                mouse(view,QEvent::MouseMove,background+QPointF(-30,-10));
                mouse(view,QEvent::MouseButtonRelease,background+QPointF(-30,-10));
                require(view->camera_.yaw_!=camera && table->selectionModel()->selectedRows().size()==32,"rotazione con selezione multipla conservata");
            }
            table->selectRow(5);auto point=view->surfacePoleScreen(5);
            mouse(view,QEvent::MouseButtonPress,point);require(interaction->interacting(),"trascinamento avviato");
            auto *refit=dialog->findChild<QCheckBox *>("surfacePolesRefitEnabled");
            dialog->findChild<QSpinBox *>("surfacePolesRefitU")->setValue(6);dialog->findChild<QSpinBox *>("surfacePolesRefitV")->setValue(8);
            refit->setChecked(true);
            require(!interaction->interacting()&&model->rowCount()==96,"spunta approssimazione interrompe cattura e rigenera entrambe le griglie");
            const int dragRow=2*8+3;table->selectRow(dragRow);point=view->surfacePoleScreen(dragRow);
            auto numbers=model->values(dragRow);mouse(view,QEvent::MouseButtonPress,point);mouse(view,QEvent::MouseMove,point+QPointF(10,5));
            const auto generation=view->preview_.generation;
            mouse(refit,QEvent::MouseButtonRelease,point+QPointF(10,5));
            require(!interaction->interacting()&&view->preview_.generation>generation,"rilascio su altro widget riattiva anteprima dopo approssimazione");
            table->selectRow(dragRow);point=view->surfacePoleScreen(dragRow);numbers=model->values(dragRow);
            mouse(view,QEvent::MouseButtonPress,point);mouse(view,QEvent::MouseMove,point+QPointF(10,5));
            QEvent ungrab(QEvent::UngrabMouse);QApplication::sendEvent(view,&ungrab);
            require(!interaction->interacting()&&model->values(dragRow)==numbers,"perdita cattura annulla spostamento senza blocco");
            mouse(view,QEvent::MouseButtonPress,point);mouse(view,QEvent::MouseMove,point+QPointF(8,4));
            mouse(view,QEvent::MouseMove,point+QPointF(8,4),Qt::NoModifier,Qt::NoButton);
            require(!interaction->interacting(),"movimento senza pulsante recupera rilascio mancante");
            mouse(view,QEvent::MouseButtonPress,point);
            mouse(refit,QEvent::MouseButtonPress,point);
            require(!interaction->interacting(),"nuovo clic sul pannello recupera una cattura rimasta aperta");
            refit->setChecked(false);require(model->rowCount()==32&&!interaction->interacting(),"ritorno a griglie originali senza cattura residua");
            table->clearSelection();
            // La selezione dalla vista continua e alterna lo stato della faccia.
            auto *pick=dialog->findChild<QPushButton *>("surfacePolesPickFaces");pick->click();
            GeometryRef picked;picked.kind=5;picked.index=0;picked.featureId=view->extrusions_[0].featureId;
            picked.point=faceReference(*source,source->faces()[2],source->face(source->faces()[2]).surface->point(.5,.5));
            view->refPickFinished_(true,picked);
            require(list->item(2)->checkState()==Qt::Checked&&model->rowCount()==48,"picking aggiunge una faccia");
            view->refPickFinished_(true,picked);
            require(list->item(2)->checkState()==Qt::Unchecked&&model->rowCount()==32,"secondo clic toglie la faccia");pick->click();
            const int rowA=5,rowB=16+5;
            const auto zColumn=model->coordinateColumn()+2;
            model->setData(model->index(rowA,zColumn),QLocale().toString(.2,'g',17));
            model->setData(model->index(rowB,zColumn),QLocale().toString(.3,'g',17));
            list->item(1)->setCheckState(Qt::Unchecked);list->item(1)->setCheckState(Qt::Checked);
            require(std::fabs(model->values(rowB)[2]-.3)<1e-12,"modifiche conservate cambiando le facce selezionate");
            table->selectRow(rowA);table->selectionModel()->select(model->index(rowB,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
            require(view->surfacePoleSelection_.size()==2,"selezione di gruppo tra facce");
            const auto first=model->values(rowA),second=model->values(rowB);
            if(gl) {
                const auto head=view->surfacePoleAxisScreen(2),end=head+(head-view->surfacePoleGizmoCenter())*.1;
                const auto send=[&](QEvent::Type type,QPointF p){QMouseEvent e(type,p,p,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&e);};
                send(QEvent::MouseButtonPress,head);send(QEvent::MouseMove,end);
                require(std::fabs((model->values(rowA)[2]-first[2])-(model->values(rowB)[2]-second[2]))<1e-10,"gizmo sposta insieme poli di due facce");
                QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);
                require(std::fabs(model->values(rowA)[2]-first[2])<1e-12&&std::fabs(model->values(rowB)[2]-second[2])<1e-12,"Esc ripristina entrambe le griglie");
            }
            auto *ok=dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok);
            QElapsedTimer wait;wait.start();while(!ok->isEnabled()&&wait.elapsed()<5000)QApplication::processEvents();
            require(ok->isEnabled(),"due facce modificabili nello stesso comando");
            if(gl){
                dialog->resize(620,950);dialog->move(8,20);
                if(auto *panel=dynamic_cast<FunctionDialogPanel *>(dialog))panel->setPanelOpacity(100);
                for(auto *area:dialog->findChildren<QScrollArea *>())area->verticalScrollBar()->setValue(260);
                const QPointF center(view->width()/2.,view->height()/2.);
                const auto pan=[&](QEvent::Type type,QPointF p){QMouseEvent e(type,p,p,type==QEvent::MouseMove?Qt::NoButton:Qt::MiddleButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&e);};
                pan(QEvent::MouseButtonPress,center);pan(QEvent::MouseMove,center+QPointF(330,-70));pan(QEvent::MouseButtonRelease,center+QPointF(330,-70));
                view->repaint();QApplication::processEvents();view->repaint();window.grab().save("/tmp/forgecad-poles-multiface/window.png");view->grabFramebuffer().save("/tmp/forgecad-poles-multiface/cages.png");}
            ok->click();
        }catch(...){failure=std::current_exception();if(dialog)dialog->reject();}
    });
    const bool accepted=surfaceToolDialog(&window,view,"Poli NURBS multifaccia",-1,definition,[&](const auto &d){return view->createBody(d);});
    if(failure)std::rethrow_exception(failure);
    require(accepted&&view->extrusions_.size()==2,"una sola funzione per due facce");
    const auto result=view->extrusions_.back();const auto faces=result.forgeBody->faces();
    require(result.surfaceTool.poleFaces.size()==2,"selezione persistita");
    require(checkBody(*result.forgeBody,{true,true}).empty(),"B-rep multifaccia valido");
    require(result.forgeBody->face(faces[0]).surface->point(.5,.5).z()>0 && result.forgeBody->face(faces[1]).surface->point(.5,.5).z()>0,"entrambe le facce deformate");
    require(result.forgeBody->face(faces[2]).surface==source->face(source->faces()[2]).surface,"faccia esclusa conserva il supporto");
    require(writeBodyBinary(*source)==before,"sorgente immutabile");
    require(bodyOperands(result)==QVector<int>{0}&&result.modelBodyId==view->extrusions_[0].modelBodyId,"dipendenza e corpo logico conservati");
    const auto changed=writeBodyBinary(*result.forgeBody);view->undo();require(view->extrusions_.size()==1,"Undo multifaccia unico");
    view->redo();require(writeBodyBinary(*view->extrusions_.back().forgeBody)==changed,"Redo multifaccia esatto");
    const auto key=CadViewport::previewKey(result,1);auto other=result;other.surfaceTool.poleFaces[1].edits[0].dz+=.1;
    require(CadViewport::previewKey(other,1)!=key,"modifica seconda faccia invalida anteprima");
    QByteArray encoded;clientOk(encodeDocument(view->documentState(),encoded));DocumentState decoded;clientOk(decodeDocument(encoded,decoded));
    require(decoded.extrusions.back().surfaceTool.poleFaces.size()==2,"codec schema11 conserva tutte le facce");
    auto broken=QJsonDocument::fromJson(surfaceToolsExtras(view->extrusions_)).object();auto records=broken["features"].toArray();auto record=records.last().toObject();auto entries=record["poleFaces"].toArray();entries.append(entries[0]);record["poleFaces"]=entries;records[records.size()-1]=record;broken["features"]=records;
    auto retained=view->extrusions_;require(!applySurfaceToolsExtras(QJsonDocument(broken).toJson(),retained).isEmpty(),"codec rifiuta facce duplicate");
    for(auto &body:decoded.extrusions){body.forgeBody.reset();body.cachedGeometry=false;body.display={};}
    CadViewport rebuilt;rebuilt.tessellationQuality_=0;rebuilt.loadDocument(decoded);
    require(rebuilt.extrusions_.back().forgeBody&&rebuilt.extrusions_.back().error.isEmpty(),"multifaccia rigenera senza cache");
    QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<QDialog *>("surfaceToolDialog");try{
        require(dialog->findChild<QTableView *>("surfacePolesTable")->model()->rowCount()==32,"riapertura ripristina due griglie");
        const auto *list=dialog->findChild<QListWidget *>("surfacePolesFaces");require(list->item(2)->checkState()==Qt::Unchecked,"faccia esclusa conservata alla riapertura");
    }catch(...){failure=std::current_exception();}dialog->reject();});
    surfaceToolDialog(&window,view,"Poli NURBS multifaccia",1,result,[](const auto &){return QString();});
    if(failure)std::rethrow_exception(failure);
    // Ricostruzione pura su due facce: spinner comuni, scarto per faccia e
    // salvataggio senza alcuno spostamento manuale (compatibilita' schema10).
    view->undo();
    QTimer::singleShot(0,&window,[&] {auto *dialog=window.findChild<QDialog *>("surfaceToolDialog");try {
        auto *list=dialog->findChild<QListWidget *>("surfacePolesFaces");list->item(2)->setCheckState(Qt::Unchecked);
        dialog->findChild<QSpinBox *>("surfacePolesRefitU")->setValue(6);
        dialog->findChild<QSpinBox *>("surfacePolesRefitV")->setValue(8);
        dialog->findChild<QCheckBox *>("surfacePolesRefitEnabled")->setChecked(true);
        auto *table=dialog->findChild<QTableView *>("surfacePolesTable");
        require(table->model()->rowCount()==96,"spinner cambiano entrambe le griglie");
        const auto text=dialog->findChild<QLabel *>("surfacePolesGridInfo")->text();
        require(text.contains(QStringLiteral("F%1").arg(source->faces()[0].index))&&text.contains(QStringLiteral("F%1").arg(source->faces()[1].index))&&text.contains("scarto"),"scostamento indicato per faccia");
        auto *ok=dialog->findChild<QDialogButtonBox *>("surfaceToolButtons")->button(QDialogButtonBox::Ok);
        QElapsedTimer wait;wait.start();while(!ok->isEnabled()&&wait.elapsed()<5000)QApplication::processEvents();require(ok->isEnabled(),"ricostruzione pura multifaccia confermabile");ok->click();
    }catch(...){failure=std::current_exception();if(dialog)dialog->reject();}});
    require(surfaceToolDialog(&window,view,"Ricostruzione multifaccia",-1,definition,[&](const auto &d){return view->createBody(d);}),"ricostruzione applicata");
    if(failure)std::rethrow_exception(failure);
    encoded.clear();clientOk(encodeDocument(view->documentState(),encoded));decoded={};clientOk(decodeDocument(encoded,decoded));
    require(decoded.extrusions.back().surfaceTool.poleFaces[0].edits.isEmpty()&&decoded.extrusions.back().surfaceTool.refitU==6,"codec conserva ricostruzione senza modifiche");
    std::cout<<"PASS poli multifaccia: selezione, gabbie, gruppo, codec, Undo/Redo, riapertura e rigenerazione senza cache"<<std::endl;
}
