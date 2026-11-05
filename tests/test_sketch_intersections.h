// Incluso nel friend ViewportInteractionTest: regressioni dello snap e del commit.
static void intersectionSnaps() {
    using namespace ForgeCad;
    const auto check = [](bool ok, const char *text) { if (!ok) throw std::runtime_error(text); };
    const auto near = [](QPointF a, QPointF b) { return std::hypot(a.x()-b.x(),a.y()-b.y()) < 1e-7; };
    SketchObject crossing;
    crossing.segments = {{{0,3},{8,3}},{{2,0},{2,9}}};
    auto hit = snapIntersection({2.04,3.03}, crossing, {}, .1);
    check(hit.found && near(hit.point,{2,3}), "intersezione retta/retta esatta");
    check(!snapIntersection({2.04,3.03},crossing,{},.1,{{0,1,-1}}).found, "esclusione entita trascinata");
    SketchObject disjoint; disjoint.segments={{{0,3},{1,3}},{{2,0},{2,9}}};
    check(!snapIntersection({2,3},disjoint,{},.1).found,"nessuno snap sui prolungamenti");
    disjoint.segments={{{0,3},{8,3}},{{1,3},{9,3}}};
    check(!snapIntersection({2,3},disjoint,{},.1).found,"sovrapposizioni non sono punti isolati");
    CurveObject circle; circle.tool=DrawingTool::Circle; circle.controlPoints={{0,0},{5,0}};
    recalculateCurve(circle);
    SketchObject conic; conic.curves={circle}; conic.segments={{{-6,3},{6,3}}};
    check(near(snapIntersection({4.02,3.02},conic,{},.1).point,{4,3}),"retta/cerchio ramo destro");
    check(near(snapIntersection({-4.02,3.02},conic,{},.1).point,{-4,3}),"retta/cerchio ramo sinistro");
    auto other=circle;other.controlPoints={{6,0},{11,0}};recalculateCurve(other);
    conic.segments.clear();conic.curves.append(other);
    check(near(snapIntersection({3.02,4.02},conic,{},.1).point,{3,4}),"cerchio/cerchio");
    CurveObject arc;arc.tool=DrawingTool::Arc;arc.controlPoints={{0,0},{5,0},{0,5}};recalculateCurve(arc);
    conic.curves={arc};conic.segments={{{-6,3},{6,3}}};
    check(!snapIntersection({-4,3},conic,{},.1).found,"nessuna intersezione fuori dall'arco");
    check(snapIntersection({4,3},conic,{},.1).found,"intersezione nel dominio dell'arco");
    CurveObject spline;spline.tool=DrawingTool::Spline;spline.controlPoints={{0,0},{2,3},{5,0}};recalculateCurve(spline);
    conic.curves={spline};conic.segments={{{2,-1},{2,5}}};
    check(near(snapIntersection({2.02,3.02},conic,{},.1).point,{2,3}),"intersezione spline esatta");

    // Due sorgenti esterne: hover senza mutazioni, commit, refresh e codec.
    QVector<SketchObject> sources(2);
    sources[0].storyId=101;sources[0].name="Orizzontale";sources[0].segments={crossing.segments[0]};
    sources[1].storyId=102;sources[1].name="Verticale";sources[1].segments={crossing.segments[1]};
    SketchObject cache;
    for(int i=0;i<2;++i) {
        GeometryRef ref;ref.kind=7;ref.index=i;ref.featureId=sources[i].storyId;ref.element={0,0,-1};
        check(appendLinkedSketchReference(cache,ref,sources,{}).isEmpty(),"cache esterna");
    }
    SketchObject target;target.storyId=103;target.name="Intersezione";
    hit=snapIntersection({2.04,3.03},target,cache,.1);
    check(hit.found && hit.firstExternal && hit.secondExternal && target.linkedReferences.empty(),"hover transitorio");
    CurveObject point;point.tool=DrawingTool::Point;point.controlPoints={hit.point};recalculateCurve(point);
    target.curves={point};
    check(constrainIntersection(target,{1,0,0},hit,cache,sources,{}),"commit due sorgenti");
    check(target.linkedReferences.size()==2 && target.geometricConstraints.size()==2,"due link e due vincoli");
    check(constrainIntersection(target,{1,0,0},hit,cache,sources,{}) && target.geometricConstraints.size()==2,"commit idempotente");
    sources[0].segments[0]={QPointF(0,4),QPointF(8,4)};
    sources[1].segments[0]={QPointF(3,0),QPointF(3,9)};
    check(refreshLinkedSketchReferences(target,sources,{}).isEmpty() && solveSketch(target).ok,"solver intersezione associativa");
    check(near(target.curves[0].controlPoints[0],{3,4}),"il punto segue entrambe le sorgenti");
    DocumentState doc;doc.sketches=sources;doc.sketches.append(target);
    QTemporaryDir dir;check(dir.isValid(),"directory temporanea");
    check(saveDocumentFile(dir.path()+"/intersection.prt",doc,false).isEmpty(),"salvataggio intersezione");
    DocumentState loaded;check(loadDocumentFile(dir.path()+"/intersection.prt",loaded).isEmpty(),"riapertura intersezione");
    check(loaded.sketches[2].linkedReferences.size()==2 && solveSketch(loaded.sketches[2]).ok
          && near(loaded.sketches[2].curves[0].controlPoints[0],{3,4}),"riferimenti e vincoli persistiti");
    History history;history.record(loaded);
    removeSketchEntities(loaded.sketches[2],{},QSet<int>{0});
    pruneUnusedSketchReferences(loaded);
    check(loaded.sketches[2].linkedReferences.empty(),"eliminazione parte rimuove entrambi i link inutilizzati");
    check(history.undo(loaded) && loaded.sketches[2].linkedReferences.size()==2,"Undo ripristina i riferimenti");
    check(history.redo(loaded) && loaded.sketches[2].linkedReferences.empty(),"Redo pulisce i riferimenti");

    CadViewport view;view.resize(900,700);view.sketches_={crossing};view.activeSketch_=0;view.sketchMode_=true;
    view.axesVisible_=false;view.originSnap_=false;
    for(auto &visible:view.planeVisible_)visible=false;
    view.gridSnap_=false;view.geometrySnap_=true;view.drawingTool_=DrawingTool::Point;
    check(near(view.snapPoint({2.04,3.03}),{2,3}) && view.lastSnapKind_==SnapKind::Intersection,"snap viewport etichettato");
    check(view.snapLabelAt(view.lastSnapKind_,view.lastSnapPoint_).contains("Intersezione"),"etichetta intersezione");
    check(!view.history_.canUndo(),"hover non genera Undo");
    view.geometrySnap_=false;
    check(near(view.snapPoint({2.04,3.03}),{2.04,3.03}) && view.lastSnapKind_==SnapKind::None,"snap disattivabile");
    view.geometrySnap_=true;view.snapPoint({2.04,3.03});
    view.curveControlPoints_={view.lastSnapPoint_};view.finalizePrimitive();
    check(view.sketches_[0].curves.size()==1 && view.sketches_[0].geometricConstraints.size()==2,"creazione punto vincola due entita");
    view.undo();check(view.sketches_[0].curves.empty(),"Undo creazione");
    view.redo();check(view.sketches_[0].curves.size()==1 && view.sketches_[0].geometricConstraints.size()==2,"Redo creazione");
    view.pointDragPosition_={8,3};
    view.dragSnapPoint({2.04,3.03});
    check(view.lastSnapKind_!=SnapKind::Intersection,"trascinamento esclude la propria geometria dalla coppia");
    std::cout << "PASS intersection snaps, solver, external references, codec, cleanup and Undo/Redo" << std::endl;
}
