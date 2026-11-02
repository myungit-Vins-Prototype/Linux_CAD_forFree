static void projectedCurveReferences() {
    using namespace ForgeCad;using namespace Kernel;
    const auto nurbs=[](QVector<QPointF> points){CurveObject c;c.tool=DrawingTool::Nurbs;c.degree=2;c.knots={0,0,0,1,1,1};c.controlPoints=points;return c;};
    const auto entity=[](int sketch,int index){GeometryRef r;r.kind=7;r.index=sketch;r.element={1,index,-1};return r;};
    QVector<SketchObject> sketches(3);sketches[0].storyId=1001;sketches[1].storyId=1002;sketches[2].storyId=1003;
    sketches[0].name="Input A";sketches[1].name="Input B";sketches[2].name="Riferimento esatto";
    sketches[0].plane=0;sketches[1].plane=1;sketches[2].plane=2;
    sketches[0].curves={nurbs({{0,0},{0,2},{4,3}}),nurbs({{0,5},{0,7},{4,8}})};sketches[1].curves={nurbs({{0,1},{1,4},{4,2}})};
    ExtrusionObject body;body.feature=BodyFeature::ProjectedCurve;body.projectionMode=1;body.featureId=2001;body.name="Curva implicita";
    body.ruledFirst={entity(0,0),entity(0,1)};body.ruledSecond={entity(1,0)};
    const auto rebuild=[&](ExtrusionObject &feature){feature.curves=forgeSketchProjectedCurves(feature,0,sketches,{},&feature.error,&feature.notice,&feature.curveReferenceKeys);feature.curve=feature.curves.value(0);require(feature.error.isEmpty() && feature.curve,"fixture curva3D implicita");};
    rebuild(body);QVector<ExtrusionObject> bodies{body};
    require(isImplicitSketchProjection(*body.curve) && body.curveReferenceKeys.size()==2,"geometria implicita e genealogia");
    GeometryRef source;source.kind=9;source.index=0;source.featureId=body.featureId;setCurveReferenceComponent(source,body,0);
    auto &target=sketches[2];
    require(appendLinkedSketchReference(target,source,sketches,bodies,false,true,100,nullptr,true).isEmpty(),"conversione associativa esatta");
    require(target.curves.size()==1 && target.curves[0].degree==0 && target.curves[0].referenceCurve && target.curves[0].knots.isEmpty(),"cache procedurale distinta da NURBS");
    GeometryRef hidden=source;setCurveReferenceComponent(hidden,body,1);
    require(appendLinkedSketchReference(target,hidden,sketches,bodies,false,true,100,nullptr,false).isEmpty(),"cache automatica di un altro ramo");
    require(target.curves.size()==2 && isHiddenSketchReference(target,1,1) && !isHiddenSketchReference(target,1,0),"cache nascosta interposta prima del punto vincolato");
    const auto &original=target.curves[0];const auto domain=original.referenceCurve->domain();
    for(int k=0;k<=40;++k) {
        const double t=domain.lo+domain.length()*k/40;const auto expected=sketchAxes(target).toLocal(body.curve->point(t));
        require(distance(original.referenceCurve->point(t),Vec2(expected.x(),expected.y()))<1e-11,"riferimento sul nuovo piano senza fit");
    }
    auto clicked=source;clicked.point.x+=1;clicked.point.y+=2;
    require(appendLinkedSketchReference(target,clicked,sketches,bodies,false,true).isEmpty() && target.curves.size()==2,"stesso ramo cliccato altrove non duplicato");
    const auto midpoint=domain.lo+.35*domain.length();const auto before=target.curves[0].referenceCurve->point(midpoint);
    const auto oldEndpoints=target.curves[0].controlPoints;
    SketchObject undoCopy=target;
    sketches[0].curves[0].controlPoints[1].ry()+=.7;rebuild(bodies[0]);
    require(refreshLinkedSketchReferences(target,sketches,bodies).isEmpty(),"refresh da primo input");
    require(target.curves[0].controlPoints==oldEndpoints && distance(target.curves[0].referenceCurve->point(midpoint),before)>.05,"forma interna aggiornata con estremi invariati");
    require(distance(undoCopy.curves[0].referenceCurve->point(midpoint),before)<1e-12,"snapshot Undo immutabile");
    const auto afterFirst=target.curves[0].referenceCurve->point(midpoint);
    sketches[1].curves[0].controlPoints[1].ry()+=.8;rebuild(bodies[0]);
    require(refreshLinkedSketchReferences(target,sketches,bodies).isEmpty() && distance(target.curves[0].referenceCurve->point(midpoint),afterFirst)>.05,"refresh indipendente da secondo input");

    CurveObject point;point.tool=DrawingTool::Point;const auto p=target.curves[0].referenceCurve->point(midpoint);point.controlPoints={QPointF(p.x()+.02,p.y()-.02)};target.curves.append(point);
    target.geometricConstraints.append(makeConstraint(target,ConstraintType::PointOnCurve,{{1,2,0},{1,0,-1}}));
    require(solveSketch(target).ok && constraintError(target,target.geometricConstraints.last())<1e-8,"PointOnCurve sul riferimento esatto");
    target.geometricConstraints.append(makeConstraint(target,ConstraintType::Distance,{{1,2,0},{2,0,0}}));
    require(solveSketch(target).ok && constraintError(target,target.geometricConstraints.last())<1e-8,"quota insieme a PointOnCurve");

    const auto savedConstraints=target.geometricConstraints;
    const auto bothConstraints=[&](const SketchObject &sketch){
        require(sketch.geometricConstraints.size()==2,"PointOnCurve e quota non vengono eliminati dalla migrazione");
        for(int i=0;i<2;++i) {
            const auto &expected=savedConstraints[i],&actual=sketch.geometricConstraints[i];
            require(actual.type==expected.type && actual.first==expected.first && actual.second==expected.second
                && actual.third==expected.third && actual.value==expected.value,"tipi, riferimenti e quota persistono con la cache nascosta interposta");
        }
        require(sketch.geometricConstraints[0].type==ConstraintType::PointOnCurve && sketch.geometricConstraints[1].type==ConstraintType::Distance
            && sketch.geometricConstraints[0].first==ConstraintRef{1,2,0} && sketch.geometricConstraints[0].second==ConstraintRef{1,0,-1},"due vincoli sul punto2 e riferimento esplicito0");
    };
    bothConstraints(target);
    for(bool cache:{false,true}) {
        DocumentState document;document.sketches=sketches;document.extrusions=bodies;QByteArray bytes;
        require(encodeDocument(document,bytes,cache).isEmpty(),"salvataggio cache procedurale formato60");
        DocumentState restored;require(decodeDocument(bytes,restored).isEmpty(),"riapertura riferimento implicito");
        bothConstraints(restored.sketches[2]);
        require(!solveSketch(restored.sketches[2]).ok,"vincoli conservati ma cache non risolta resta in errore");
        require(restored.sketches[2].curves[0].degree==0 && !restored.sketches[2].curves[0].referenceCurve
            && curveGeometry(restored.sketches[2].curves[0]).empty(),"cache transiente non diventa geometria di ripiego");
        auto &f=restored.extrusions[0];f.curves=forgeSketchProjectedCurves(f,0,restored.sketches,{},&f.error,&f.notice,&f.curveReferenceKeys);f.curve=f.curves.value(0);
        require(f.error.isEmpty() && refreshLinkedSketchReferences(restored.sketches[2],restored.sketches,restored.extrusions).isEmpty(),"ricostruzione esatta da GeometryRef persistito");
        bothConstraints(restored.sketches[2]);
        require(solveSketch(restored.sketches[2]).ok,"quote e vincoli dopo riapertura");
        restored.extrusions.clear();
        require(!refreshLinkedSketchReferences(restored.sketches[2],restored.sketches,restored.extrusions).isEmpty(),"sorgente mancante dopo riapertura");
        bothConstraints(restored.sketches[2]);
        require(!solveSketch(restored.sketches[2]).ok && curveGeometry(restored.sketches[2].curves[0]).empty(),"sorgente mancante conserva vincoli senza geometria di ripiego");
    }

    for(bool unlinked:{false,true}) {
        DocumentState invalid;invalid.sketches=sketches;invalid.extrusions=bodies;
        if(unlinked)invalid.sketches[2].linkedReferences.clear();else invalid.sketches[2].curves[0].knots={0};
        QByteArray bytes;require(encodeDocument(invalid,bytes,false).isEmpty(),"fixture marker procedurale malformato");
        DocumentState rejected;require(!decodeDocument(bytes,rejected).isEmpty(),"marker senza link o con nodi rifiutato dal decoder");
    }

    SketchObject contact;contact.plane=kFacePlane;contact.frame.origin[0]=1;contact.frame.normal[0]=1;contact.frame.normal[2]=0;contact.frame.xAxis[0]=0;contact.frame.xAxis[1]=1;
    require(appendLinkedSketchReference(contact,source,sketches,bodies,true,true).isEmpty() && !contact.segments.isEmpty(),"contatto isolato esatto col piano");
    CurveObject piercePoint;piercePoint.tool=DrawingTool::Point;piercePoint.controlPoints={contact.segments[0].first+QPointF(.1,.1)};contact.curves={piercePoint};
    SketchConstraint pierce;pierce.type=ConstraintType::Pierce;pierce.first={1,0,0};
    require(refreshCurvePierce(contact,pierce,source,sketches,bodies).isEmpty(),"Pierce su curva implicita");
    contact.geometricConstraints={pierce};require(solveSketch(contact).ok && constraintError(contact,contact.geometricConstraints[0])<1e-8,"punto risolto su Pierce esatto");
    auto suppressed=bodies;suppressed[0].suppressed=true;
    require(!refreshCurvePierce(contact,pierce,source,sketches,suppressed).isEmpty(),"Pierce da sorgente soppressa rifiutato");
    suppressed[0].suppressed=false;suppressed[0].error=QStringLiteral("errore sorgente");
    require(!refreshCurvePierce(contact,pierce,source,sketches,suppressed).isEmpty(),"Pierce da sorgente in errore rifiutato");
    auto absent=contact;absent.frame.origin[0]=20;
    require(!refreshCurvePierce(absent,pierce,source,sketches,bodies).isEmpty(),"Pierce senza soluzione rifiutato");
    auto coplanar=target;GeometryRef linked;linked.kind=7;linked.index=2;linked.featureId=target.storyId;linked.element={1,0,-1};
    require(!appendLinkedSketchReference(coplanar,linked,sketches,bodies,true,true).isEmpty(),"contatto complanare senza punto arbitrario");
    SketchObject further;further.plane=0;
    require(appendLinkedSketchReference(further,linked,sketches,bodies,false,true).isEmpty() && further.curves[0].referenceCurve,"riferimento da un altro schizzo resta esatto");
    auto invalid=contact;invalid.frame.normal[0]=invalid.frame.normal[1]=invalid.frame.normal[2]=0;invalid.segments.clear();invalid.curves.clear();
    require(!appendLinkedSketchReference(invalid,source,sketches,bodies,false,true).isEmpty() && invalid.curves.isEmpty(),"piano degenere rifiutato atomicamente");
    auto unavailable=bodies;unavailable[0].curveReferenceKeys.clear();
    auto missing=target;require(!refreshLinkedSketchReferences(missing,sketches,unavailable).isEmpty() && !missing.curves[0].referenceCurve && curveGeometry(missing.curves[0]).empty(),"ramo scomparso senza riaggancio o geometria obsoleta");

    QVector<SketchObject> lines(2);lines[0].storyId=11;lines[1].storyId=12;lines[1].plane=1;
    lines[0].segments={{{0,0},{4,0}},{{0,10},{4,10}}};lines[1].segments={{{0,0},{4,4}}};
    auto refLine=[](int s,int i){GeometryRef r;r.kind=7;r.index=s;r.element={0,i,-1};return r;};
    auto ordered=body;ordered.ruledFirst={refLine(0,0),refLine(0,1)};ordered.ruledSecond={refLine(1,0)};
    ordered.curves=forgeSketchProjectedCurves(ordered,0,lines,{},&ordered.error,nullptr,&ordered.curveReferenceKeys);ordered.curve=ordered.curves.value(0);
    GeometryRef branch;branch.kind=9;setCurveReferenceComponent(branch,ordered,0);
    std::reverse(ordered.ruledFirst.begin(),ordered.ruledFirst.end());
    ordered.curves=forgeSketchProjectedCurves(ordered,0,lines,{},&ordered.error,nullptr,&ordered.curveReferenceKeys);ordered.curve=ordered.curves.value(0);
    const auto resolved=bodyCurveForReference(ordered,branch);require(resolved && std::abs(resolved->point(resolved->domain().lo).y())<1e-12,"ramo genealogico conserva identita' dopo reorder input");
    std::cout<<"PASS riferimenti impliciti: cache esatta, altro piano, input interni, PointOnCurve/quote/Pierce, codec, snapshot e genealogia rami\n";
}
