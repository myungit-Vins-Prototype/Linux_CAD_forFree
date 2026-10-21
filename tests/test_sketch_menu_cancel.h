// Incluso nel friend ViewportInteractionTest: menu reale e veri eventi Esc.
    static void sketchMenuCancelRegressions() {
        using namespace ForgeCad;
        const bool native=QGuiApplication::platformName()!=QStringLiteral("offscreen");
        QVector<CurveObject> curves;
        for(auto tool:{DrawingTool::Circle,DrawingTool::Arc,DrawingTool::Ellipse,DrawingTool::Spline,DrawingTool::Nurbs}){
            CurveObject curve;curve.tool=tool;
            if(tool==DrawingTool::Circle)curve.controlPoints={{5,3},{8,3}};
            else if(tool==DrawingTool::Arc||tool==DrawingTool::Ellipse)curve.controlPoints={{5,3},{8,3},{5,5}};
            else curve.controlPoints={{0,0},{4,2},{7,-1},{10,3}};
            recalculateCurve(curve,0);curves.append(curve);
        }
        int cases=0;
        for(const auto &curve:curves){
            QVector<QPointF> points{curve.controlPoints[0]};
            if(curve.tool==DrawingTool::Spline){
                points.append(curve.tangentHandles[0].second);
                // Un tratto lontano dai controlli: l'azione Elimina e' assente.
                CadViewport picker;picker.resize(900,650);SketchObject s;s.curves={curve};picker.sketches_={s};picker.selectSketch(0);
                bool found=false;for(const auto &p:curve.samples){int c=-1,k=-1;EditablePointKind kind;
                    if(!picker.findCurveEditPoint(p,c,k,kind)){points.append(p);found=true;break;}}
                require(found,"fixture menu sul tratto della spline senza punto selezionato");
            }
            for(const auto &point:points)for(bool escape:{true,false})for(bool mouseRelease:{true,false}){
                CadViewport v;v.resize(900,650);SketchObject s;s.curves={curve};v.sketches_={s};v.selectSketch(0);v.history_.clear();
                if(native){v.show();v.activateWindow();QApplication::processEvents();}
                const auto before=v.sketches_[0].curves[0];
                const auto at=v.projectWorldPoint(v.mapSketchPoint(point,s)).toPoint();bool opened=false;
                QTimer::singleShot(native?30:0,&v,[&]{
                    auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
                    if(!menu)for(auto *candidate:v.findChildren<QMenu*>())if(candidate->isVisible()){menu=candidate;break;}
                    if(!menu)return;
                    opened=true;
                    if(escape){QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(menu,&key);}
                    else menu->close();
                });
                if(mouseRelease){
                    v.contextPending_=true;v.rightDragged_=false;v.lastMousePosition_=at;
                    QMouseEvent release(QEvent::MouseButtonRelease,at,v.mapToGlobal(at),Qt::RightButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
                }else{
                    QContextMenuEvent context(QContextMenuEvent::Keyboard,at,v.mapToGlobal(at));v.contextMenuEvent(&context);
                }
                require(opened,"menu reale aperto prima di annullare");
                const auto &after=v.sketches_[0].curves[0];
                require(after.controlPoints==before.controlPoints&&after.tangentHandles==before.tangentHandles&&after.tangentLinked==before.tangentLinked,"Esc/chiusura menu non modifica nodi o maniglie");
                require(!v.canUndo()&&!v.hasPendingSketchChanges()&&v.sketchMode_,"annullamento menu senza Undo o modifiche pendenti e schizzo ancora aperto");
                QKeyEvent exit(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);v.keyPressEvent(&exit);
                require(!v.sketchMode_,"Esc successivo fuori dal menu esce dallo schizzo");++cases;
            }
        }
        // Una scelta effettiva continua a funzionare e resta annullabile.
        {
            CadViewport v;v.resize(900,650);SketchObject s;s.curves={curves[3]};v.sketches_={s};v.selectSketch(0);v.history_.clear();
            if(native){v.show();v.activateWindow();QApplication::processEvents();}
            const auto before=v.sketches_[0].curves[0];bool selected=false;
            QTimer::singleShot(native?30:0,&v,[&]{auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                for(auto *action:menu->actions())if(action->text().contains(QStringLiteral("le maniglie"))){menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);selected=true;return;}
                menu->close();});
            const auto at=v.projectWorldPoint(v.mapSketchPoint(before.controlPoints[0],s)).toPoint();QContextMenuEvent context(QContextMenuEvent::Keyboard,at,v.mapToGlobal(at));v.contextMenuEvent(&context);
            require(selected&&v.sketches_[0].curves[0].tangentLinked[0]!=before.tangentLinked[0]&&v.canUndo(),"comando maniglie effettivamente scelto resta attivo");
            v.undo();require(v.sketches_[0].curves[0].tangentLinked==before.tangentLinked,"Undo comando maniglie del menu");
        }
        std::cout<<"PASS sketch menu cancel: "<<cases<<" casi Esc/chiusura, rilascio mouse/menu tastiera e comando valido con Undo"<<std::endl;
    }
