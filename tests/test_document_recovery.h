// Incluso nel friend ViewportInteractionTest.
    static DocumentState recoveryFixture() {
        using namespace ForgeCad;
        DocumentState state;SketchObject sketch;
        sketch.segments={{{0,0},{3,0}},{{3,0},{3,4}},{{3,4},{0,4}},{{0,4},{0,0}}};state.sketches={sketch};
        ExtrusionObject body;body.sketchIndex=0;body.distance=5;state.extrusions={body};return state;
    }
    static int recoveryCrashWorker(const QString &directory) {
        using namespace ForgeCad;
        auto state=recoveryFixture();DocumentRecovery recovery([&](DocumentState &out,QString &path){out=state;path=directory+"/original.prt";return true;},nullptr,directory,10,40);
        int writes=0;QObject::connect(&recovery,&DocumentRecovery::checkpointFinished,&recovery,[&](quint64,bool written,const QString &error){
            if(!error.isEmpty()){std::cerr<<error.toStdString()<<std::endl;QCoreApplication::exit(1);return;}
            if(written&&++writes==1){state.extrusions[0].distance=7;recovery.changed();}
            else if(writes==2)std::cout<<"READY "<<state.version.documentId.toString().toStdString()<<std::endl;
        });recovery.changed();return QCoreApplication::exec();
    }
    static void documentRecoveryRegressions() {
        using namespace ForgeCad;
        const auto wait=[](const std::function<bool()> &done,int timeout=12000){
            QElapsedTimer elapsed;elapsed.start();
            while(!done()&&elapsed.elapsed()<timeout){
                QApplication::processEvents();
                // Il test pompa gli eventi senza exec(): consegna anche le
                // distruzioni rinviate che il normale loop Qt esegue da solo.
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                QThread::msleep(1);
            }
            require(done(),"timeout recupero asincrono");
        };
        const auto scan=[&](DocumentRecovery &recovery){
            QVector<RecoveryEntry> result;QString error;bool done=false;
            auto connection=QObject::connect(&recovery,&DocumentRecovery::entriesFound,&recovery,[&](const auto &entries,const auto &failure){result=entries;error=failure;done=true;});
            recovery.scan();wait([&]{return done;});QObject::disconnect(connection);require(error.isEmpty(),qPrintable(error));return result;
        };
        const auto stop=[&](DocumentRecovery &recovery){bool done=false;
            auto connection=QObject::connect(&recovery,&DocumentRecovery::stopped,&recovery,[&]{done=true;});recovery.shutdown();wait([&]{return done;});QObject::disconnect(connection);
        };
        const auto read=[&](DocumentRecovery &recovery,const RecoveryEntry &entry){
            DocumentState result;QString error;bool done=false;
            auto connection=QObject::connect(&recovery,&DocumentRecovery::restored,&recovery,[&](const auto &state,const auto &,const auto &failure){result=state;error=failure;done=true;});
            recovery.restore(entry);wait([&]{return done;});QObject::disconnect(connection);require(error.isEmpty(),qPrintable(error));return result;
        };
        QTemporaryDir directory;require(directory.isValid(),"cartella recovery isolata");
        auto state=recoveryFixture();buildBody(state.extrusions[0],0,state.sketches,{},0);
        require(state.extrusions[0].forgeBody&&!state.extrusions[0].display.vertices.isEmpty(),"fixture con cache geometrica e mesh reali");
        state.pendingSketchChanges={0};int captures=0,finished=0,writes=0;QString failure;
        bool ready=true;
        auto live=std::make_unique<DocumentRecovery>([&](DocumentState &out,QString &path){
            require(QThread::currentThread()==qApp->thread(),"acquisizione snapshot soltanto sul thread GUI");
            if(!ready)return false;
            ++captures;out=state;path=directory.filePath("original.prt");return true;
        },nullptr,directory.path(),30,140);
        QObject::connect(live.get(),&DocumentRecovery::checkpointFinished,live.get(),[&](quint64,bool written,const QString &error){++finished;writes+=written;failure=error;});
        for(int i=0;i<200;++i)live->changed();
        wait([&]{return finished==1;});
        require(failure.isEmpty()&&captures==1&&writes==1,"raffica accorpata in un solo snapshot/scrittura");
        auto reader=std::make_unique<DocumentRecovery>([](DocumentState &,QString &){return false;},nullptr,directory.path());
        require(scan(*reader).isEmpty(),"sessione ancora aperta non proposta per il recupero");
        live->changed();wait([&]{return finished==2;});require(writes==1,"definizione identica non riscritta");
        ready=false;live->changed();QElapsedTimer pause;pause.start();while(pause.elapsed()<180){QApplication::processEvents();QThread::msleep(1);}
        require(captures==2&&!live->busy(),"nessuno snapshot durante un gesto/calcolo non stabile");ready=true;
        state.extrusions[0].distance=6;wait([&]{return finished==3;});require(writes==2,"snapshot rinviato fino allo stato stabile");
        state.extrusions[0].distance=7;live->changed();wait([&]{return finished==4;});require(writes==3,"terza versione aggiornata");
        const auto activeFolders=QDir(directory.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);require(activeFolders.size()==1,"una sessione attiva");
        const auto blockedSlot=directory.filePath(activeFolders[0]+"/snapshot-0.fcr");
        require(QFile::rename(blockedSlot,blockedSlot+".keep")&&QDir().mkdir(blockedSlot),"errore disco controllato sul prossimo slot");
        state.extrusions[0].distance=8;live->changed();wait([&]{return finished==5;});
        require(!failure.isEmpty()&&writes==3,"errore di commit segnalato senza perdere ultimo snapshot valido");
        require(QDir().rmdir(blockedSlot)&&QFile::rename(blockedSlot+".keep",blockedSlot),"ripristino fixture errore disco");
        live.reset();QVector<RecoveryEntry> entries;
        wait([&]{entries=scan(*reader);return entries.size()==1;});
        require(entries[0].sequence==3,"ultima versione recuperabile");
        const auto session=entries[0].directory;
        require(QDir(session).entryList({"*.fcr"},QDir::Files).size()==2,"due soli slot per documento");
        auto restored=read(*reader,entries[0]);require(restored.extrusions[0].distance==7&&restored.sketches.size()==1,"definizioni complete recuperate");
        require(!restored.extrusions[0].forgeBody&&restored.extrusions[0].display.vertices.isEmpty(),"nessuna cache B-rep o mesh nel backup");
        {QFile corrupt(session+"/snapshot-1.fcr");require(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate),"corruzione controllata dell'ultimo slot");corrupt.write("interrupted");}
        entries=scan(*reader);require(entries.size()==1&&entries[0].sequence==2,"fallback alla copia precedente se ultima corrotta");
        restored=read(*reader,entries[0]);require(restored.extrusions[0].distance==6,"fallback recupera parametri esatti");
        // Massima attesa sotto modifiche continue; nessuna serializzazione idle.
        {
            QTemporaryDir folder;int captured=0,done=0;
            DocumentRecovery r([&](DocumentState &out,QString &){++captured;out=recoveryFixture();return true;},nullptr,folder.path(),90,140);
            QObject::connect(&r,&DocumentRecovery::checkpointFinished,&r,[&](quint64,bool,const QString &error){require(error.isEmpty(),qPrintable(error));++done;});
            QTimer changes;changes.setInterval(12);QObject::connect(&changes,&QTimer::timeout,&r,[&]{r.changed();});changes.start();r.changed();
            wait([&]{return done>0;},2000);changes.stop();require(captured>0,"limite massimo attesa non annullato dalle modifiche continue");
            wait([&]{return !r.pending()&&!r.busy();});const auto before=captured;
            pause.restart();while(pause.elapsed()<200){QApplication::processEvents();QThread::msleep(1);}require(captured==before,"nessun lavoro sul documento fermo");
            bool cleared=false;QObject::connect(&r,&DocumentRecovery::cleared,&r,[&](quint64,const QString &error){require(error.isEmpty(),qPrintable(error));cleared=true;});r.reset();wait([&]{return cleared;});
            stop(r);
        }
        // Payload importato: la compressione lavora mentre il timer UI continua.
        {
            QTemporaryDir folder;auto big=recoveryFixture();ExtrusionObject imported;imported.feature=BodyFeature::Imported;
            imported.importData=QByteArray(32*1024*1024,'x');big.extrusions.append(imported);int ticks=0;bool done=false;QString error;
            DocumentRecovery r([&](DocumentState &out,QString &){out=big;return true;},nullptr,folder.path(),1,20);
            QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,&r,[&]{if(r.busy())++ticks;});heartbeat.start();
            QObject::connect(&r,&DocumentRecovery::checkpointFinished,&r,[&](quint64,bool,const QString &failure){error=failure;done=true;});QElapsedTimer cost;cost.start();r.changed();wait([&]{return done;},15000);
            require(error.isEmpty()&&ticks>3,"serializzazione/compressione importato non bloccano il timer UI");
            const auto folders=QDir(folder.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);require(folders.size()==1,"sessione del grande importato");
            QFile slot(folder.filePath(folders[0]+"/snapshot-1.fcr"));
            require(slot.exists()&&slot.size()>0,"checkpoint importato scritto su disco");
            // Il livello veloce di zlib e zlib-ng ha rapporti diversi: qui
            // chiediamo almeno 32:1 sui dati ripetuti, non una dimensione esatta.
            // Il round-trip sotto verifica separatamente tutti i byte importati.
            require(slot.size()<imported.importData.size()/32,"compressione sostanziale dei dati importati ripetuti");
            std::cout<<"RECOVERY 32MiB dati ripetuti: "<<cost.elapsed()<<"ms, UI timer "<<ticks<<", "<<slot.size()<<" byte"<<std::endl;
            QTemporaryDir clone;const auto cloneSession=clone.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));require(QDir().mkpath(cloneSession)&&QFile::copy(slot.fileName(),cloneSession+"/snapshot-1.fcr"),"copia isolata del checkpoint importato");
            DocumentRecovery importedReader([](DocumentState &,QString &){return false;},nullptr,clone.path());
            const auto importedEntries=scan(importedReader);require(importedEntries.size()==1,"checkpoint importato leggibile");
            require(read(importedReader,importedEntries[0]).extrusions.last().importData==imported.importData,"importData conservato byte per byte senza dipendere dal file sorgente");
            // Reset durante lavoro e nuova modifica non fanno riapparire copie vecchie.
            done=false;r.changed();wait([&]{return r.busy();});bool cleared=false;
            QObject::connect(&r,&DocumentRecovery::cleared,&r,[&](quint64,const QString &failure){require(failure.isEmpty(),qPrintable(failure));cleared=true;});r.reset();wait([&]{return cleared;});
            require(QDir(folder.filePath(folders[0])).entryList({"*.fcr"},QDir::Files).isEmpty(),"Salva/abbandono elimina anche un backup in corso");
            stop(r);stop(importedReader);
        }
        // SIGKILL di un processo separato con due checkpoint e lock vivo.
        QTemporaryDir crashFolder;QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--recovery-crash-worker",crashFolder.path()});
        require(child.waitForStarted(),"avvio processo da interrompere");QByteArray output;
        wait([&]{output+=child.readAllStandardOutput();return output.contains("READY");});child.kill();require(child.waitForFinished(),"terminazione forzata del solo processo test");
        DocumentRecovery crashed([](DocumentState &,QString &){return false;},nullptr,crashFolder.path());auto crashedEntries=scan(crashed);
        require(crashedEntries.size()==1&&crashedEntries[0].sequence==2,"lock stale rimosso e ultima copia proposta dopo SIGKILL");
        const auto crashedState=read(crashed,crashedEntries[0]);require(crashedState.extrusions[0].distance==7,"stato esatto dopo processo interrotto");
        // File originale intatto, nuova identita' e rigenerazione nella vera UI.
        const auto oldRoot=qgetenv("FORGECAD_RECOVERY_DIR");qputenv("FORGECAD_RECOVERY_DIR",crashFolder.path().toUtf8());
        PdfWindow window;window.show();bool dialogSeen=false,rendered=true;QTimer choose;choose.setInterval(10);
        QObject::connect(&choose,&QTimer::timeout,&window,[&]{auto *dialog=window.findChild<QDialog*>("documentRecoveryDialog");if(!dialog||!dialog->isVisible())return;
            dialogSeen=true;choose.stop();if(QApplication::arguments().contains(QStringLiteral("--render")))rendered=dialog->grab().save("/tmp/document-recovery/recovery-dialog.png");dialog->findChild<QPushButton*>("restoreRecoveryButton")->click();});choose.start();window.showRecoveryDocuments();
        PdfWindow *recovered=nullptr;wait([&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *candidate=dynamic_cast<PdfWindow*>(widget))if(candidate!=&window&&candidate->documentModified_){recovered=candidate;return true;}return false;});
        require(dialogSeen&&rendered&&recovered->documentPath_.isEmpty()&&recovered->viewport_->documentVersion().documentId!=crashedState.version.documentId,"recupero dalla UI come copia nuova non salvata");
        require(!QFileInfo::exists(crashFolder.filePath("original.prt")),"recupero non scrive il file originale");
        require(recovered->viewport_->extrusions()[0].forgeBody&&std::abs(Kernel::massProperties(*recovered->viewport_->extrusions()[0].forgeBody).volume-84)<1e-6,"rigenerazione del modello recuperato senza cache");
        require(scan(crashed).size()==1,"backup originale conservato finche' la copia recuperata non viene salvata");
        const auto saved=crashFolder.filePath("recovered.prt");require(recovered->saveDocumentPath(saved),"salvataggio esplicito della copia recuperata");
        wait([&]{return scan(crashed).isEmpty();});
        auto *guarded=recovered;QPointer<PdfWindow> closed=guarded;guarded->close();wait([&]{return closed.isNull();});
        window.close();wait([&]{return window.recoveryCloseReady_;});qputenv("FORGECAD_RECOVERY_DIR",oldRoot);
        {
            PdfWindow dirty;dirty.viewport_->restoreDocument(recoveryFixture());dirty.viewport_->pointDragActive_=true;
            int checkpoints=0;QObject::connect(dirty.recovery_,&DocumentRecovery::checkpointFinished,&dirty,[&](quint64,bool,const QString &error){require(error.isEmpty(),qPrintable(error));++checkpoints;});
            pause.restart();while(pause.elapsed()<1700){QApplication::processEvents();QThread::msleep(1);}
            require(checkpoints==0&&dirty.recovery_->pending(),"vero drag viewport rinvia lo snapshot");
            dirty.viewport_->pointDragActive_=false;wait([&]{return checkpoints>0;});
            auto cancel=std::make_unique<QTimer>();cancel->setInterval(10);
            QObject::connect(cancel.get(),&QTimer::timeout,&dirty,[&]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){box->button(QMessageBox::Cancel)->click();cancel->stop();}});cancel->start();
            require(!dirty.close()&&!dirty.recoveryClosing_,"Annulla chiusura conserva documento e backup");
            auto discard=std::make_unique<QTimer>();discard->setInterval(10);
            QObject::connect(discard.get(),&QTimer::timeout,&dirty,[&]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){box->button(QMessageBox::Discard)->click();discard->stop();}});discard->start();dirty.close();wait([&]{return dirty.recoveryCloseReady_;});
            const auto sessions=QDir(dirty.recovery_->directory()).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot);
            for(const auto &session:sessions)require(QDir(session.absoluteFilePath()).entryList({"*.fcr"},QDir::Files).isEmpty(),"Scarta esplicito elimina copie senza scrivere l'originale");
            PdfWindow readonly;readonly.viewport_->setDocumentReadOnly(true);readonly.viewport_->documentChanged();require(!readonly.recovery_->pending(),"nessun backup per revisioni in sola lettura");readonly.close();wait([&]{return readonly.recoveryCloseReady_;});
        }
        std::cout<<"PASS recovery: debounce/max attesa, idle, thread UI, due slot/fallback, importati, reset in corsa, SIGKILL, lock, UI copia/Salva/rigenerazione"<<std::endl;
    }
