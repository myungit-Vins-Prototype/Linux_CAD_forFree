// Incluso nel friend ViewportInteractionTest; geometria analitica B-rep,
// storia e persistenza. Le prove non richiedono un contesto OpenGL.
static void extrusionNextBodyTests() {
    using namespace ForgeCad;
    using namespace Kernel;
    const auto rectangle = [](double x, double y, double width, double height) {
        SketchObject sketch;
        const QPointF points[] = {{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
        for (int i = 0; i < 4; ++i) sketch.segments.append({points[i],points[(i+1)%4]});
        return sketch;
    };
    const auto target = [](const Body &shape, int id) {
        ExtrusionObject body; body.feature = BodyFeature::Imported;
        body.featureId = body.modelBodyId = quint64(id); body.name = QStringLiteral("Limite %1").arg(id);
        body.forgeBody = std::make_shared<const Body>(shape); body.solid = !shape.isSheet();
        return body;
    };
    const auto build = [&](const SketchObject &sketch, const QVector<ExtrusionObject> &targets, double direction = 1.0) {
        ExtrusionObject feature; feature.feature = BodyFeature::Extrusion; feature.featureId = 10000;
        feature.sketchIndex = 0; feature.extent = 4; feature.distance = direction;
        buildGeometry(feature, targets.size(), {sketch}, targets);
        return feature;
    };
    const auto volume = [](const ExtrusionObject &feature, double expected) {
        require(feature.forgeBody && feature.error.isEmpty(), qPrintable(QStringLiteral("Volume atteso %1: %2").arg(expected).arg(feature.error)));
        const double actual = massProperties(*feature.forgeBody).volume;
        require(std::abs(actual - expected) < 1e-7,
                qPrintable(QStringLiteral("Volume B-rep fino alle prime superfici: atteso %1, ottenuto %2").arg(expected,0,'g',17).arg(actual,0,'g',17)));
    };
    const auto zBounds = [](const ForgeBody &body) {
        double lo = 1e100, hi = -1e100;
        for (auto vertex : body->vertices()) { const double z = body->vertex(vertex).point.z(); lo = std::min(lo,z); hi = std::max(hi,z); }
        return QPair<double,double>{lo,hi};
    };
    const auto sketch = rectangle(0,0,2,2);
    const auto above = target(makeBox(Frame3(Vec3(-1,-1,4),Vec3(0,0,1),Vec3(1,0,0)),4,4,2), 1001);
    const auto below = target(makeBox(Frame3(Vec3(-1,-1,-7),Vec3(0,0,1),Vec3(1,0,0)),4,4,2), 1002);
    const auto disjoint = target(makeBox(Frame3(Vec3(5,-1,1),Vec3(0,0,1),Vec3(1,0,0)),2,4,2), 1003);
    const auto farther = target(makeBox(Frame3(Vec3(-1,-1,9),Vec3(0,0,1),Vec3(1,0,0)),4,4,2), 1004);
    const QVector<ExtrusionObject> targets{above,below,disjoint,farther};
    auto positive = build(sketch, targets);
    volume(positive,16);
    require(positive.booleanTools == QVector<int>({0,1,2,3}) && bodyOperands(positive) == positive.booleanTools,
            "anche candidati disgiunti e dietro restano dipendenze per i successivi spostamenti");
    const auto forward = zBounds(positive.forgeBody);
    require(forward.first == 0 && std::abs(forward.second-4) < 1e-12, "il prossimo corpo e' scelto nel verso positivo");
    auto negative = build(sketch, targets, -1);
    volume(negative,20);
    const auto backward = zBounds(negative.forgeBody);
    require(std::abs(backward.first+5) < 1e-12 && backward.second == 0, "il verso invertito cerca il corpo dietro lo schizzo");
    require(hiddenOperands(positive).isEmpty(), "i limiti non sono utensili consumati dalla funzione");
    const auto history = featureHistoryOf(*positive.forgeBody);
    require(history && history->operandCount() == 4, "genealogia di tutti i corpi limite");
    for (FaceId face : positive.forgeBody->faces()) require(history->isDeclared(EntityKind::Face, face.index), "tutte le facce limitate hanno una provenienza dichiarata");

    const auto small = rectangle(-0.5,-0.5,1,1);
    const Frame3 slope(Vec3(-5,-5,3), normalized(Vec3(-0.2,0,1)), Vec3(1,0,0.2));
    volume(build(small,{target(makeBox(slope,20,20,20),1100)}),4);
    const auto cylinder = target(makeCylinder(Frame3(Vec3(0,-5,3),Vec3(0,1,0),Vec3(1,0,0)),2,10),1101);
    const auto curved = build(small,{cylinder});
    const double radialIntegral = 0.5*std::sqrt(3.75) + 4*std::asin(0.25);
    volume(curved,3-radialIntegral);
    bool cylindricalCap = false;
    for (FaceId face : curved.forgeBody->faces()) cylindricalCap = cylindricalCap || curved.forgeBody->face(face).surface->type() == SurfaceType::Cylinder;
    require(cylindricalCap, "il limite cilindrico conserva la superficie analitica");
    const auto spherical = build(small,{target(makeSphere(Frame3(Vec3(0,0,3),Vec3(0,0,1),Vec3(1,0,0)),2),1102)});
    require(spherical.forgeBody && spherical.error.isEmpty(), qPrintable(QStringLiteral("Limite sferico: ") + spherical.error));
    bool sphericalCap = false;
    for (FaceId face : spherical.forgeBody->faces()) sphericalCap = sphericalCap || spherical.forgeBody->face(face).surface->type() == SurfaceType::Sphere;
    require(sphericalCap, "il limite sferico conserva la superficie analitica");
    const auto conical = build(small,{target(makeCone(Frame3(Vec3(0,-5,3),Vec3(0,1,0),Vec3(1,0,0)),2,1,10),1103)});
    require(conical.forgeBody && conical.error.isEmpty(), qPrintable(QStringLiteral("Limite conico: ") + conical.error));
    bool conicalCap = false;
    for (FaceId face : conical.forgeBody->faces()) conicalCap = conicalCap || conical.forgeBody->face(face).surface->type() == SurfaceType::Cone;
    double coneHit = 0;
    require(conicalCap && firstRayHit(*conical.forgeBody,Vec3(0,0,0.1),Vec3(0,0,1),kLinearResolution,coneHit)
        && std::abs(coneHit-1.4) < 1e-7, "il limite conico si ferma alla prima superficie esatta");
    // Un corpo con tetto bilineare z=4+0.2xy, chiuso da quattro pareti
    // piane e da un coperchio a z=8. Nessuna approssimazione poligonale
    // della faccia libera: lo zero medio di xy da' volume analitico 4.
    {
        const std::vector<Vec3> points{{-1,-1,4.2},{1,-1,3.8},{1,1,4.2},{-1,1,3.8},
                                       {-1,-1,8},{1,-1,8},{1,1,8},{-1,1,8}};
        const int endpoints[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        std::vector<Body::BuildEdge> edges;
        for (const auto &pair : endpoints) { Body::BuildEdge edge; edge.start=pair[0]; edge.end=pair[1]; edges.push_back(edge); }
        std::vector<Body::BuildFace> faces(6);
        faces[0].loops = {{{3,false,{}},{2,false,{}},{1,false,{}},{0,false,{}}}};
        faces[1].loops = {{{4,true,{}},{5,true,{}},{6,true,{}},{7,true,{}}}};
        faces[2].loops = {{{0,true,{}},{9,true,{}},{4,false,{}},{8,false,{}}}};
        faces[3].loops = {{{1,true,{}},{10,true,{}},{5,false,{}},{9,false,{}}}};
        faces[4].loops = {{{2,true,{}},{11,true,{}},{6,false,{}},{10,false,{}}}};
        faces[5].loops = {{{3,true,{}},{8,true,{}},{7,false,{}},{11,false,{}}}};
        Body freeform = Body::build(points,edges,faces);
        assignPolyhedralGeometry(freeform);
        const FaceId bottom = freeform.faces().front();
        freeform.face(bottom).surface = std::make_shared<BSplineSurface>(1,1,
            std::vector<double>{-1,-1,1,1}, std::vector<double>{-1,-1,1,1}, 2,2,
            std::vector<Vec3>{{-1,-1,4.2},{-1,1,3.8},{1,-1,3.8},{1,1,4.2}});
        freeform.face(bottom).sense = false;
        for (FinId fin : freeform.fins()) freeform.fin(fin).pcurve.reset();
        require(computePCurves(freeform) == 0, "SP-curve esatte del limite bilineare");
        const auto limited = build(small,{target(freeform,1104)});
        volume(limited,4);
        bool splineCap = false;
        for (FaceId face : limited.forgeBody->faces()) splineCap = splineCap || limited.forgeBody->face(face).surface->type() == SurfaceType::BSpline;
        require(splineCap, "la superficie libera resta B-spline nel risultato limitato");
    }
    // Due corpi coprono parti diverse dello stesso profilo sullo stesso piano.
    const auto left = target(makeBox(Frame3(Vec3(-1,-1,4),Vec3(0,0,1),Vec3(1,0,0)),2,4,2),1200);
    const auto right = target(makeBox(Frame3(Vec3(1,-1,4),Vec3(0,0,1),Vec3(1,0,0)),2,4,2),1201);
    volume(build(sketch,{left,right}),16);
    const auto inside = target(makeBox(Frame3(Vec3(0.5,0.5,2),Vec3(0,0,1),Vec3(1,0,0)),1,1,1),1250);
    const auto secondInside = target(makeBox(Frame3(Vec3(0.75,0.75,1),Vec3(0,0,1),Vec3(1,0,0)),0.5,0.5,0.5),1251);
    for (const auto &internal : QVector<QVector<ExtrusionObject>>{{inside,above},{inside,secondInside,above}}) {
        const auto result = build(sketch,internal);
        require(!result.forgeBody && result.error.contains(QStringLiteral("ostacolo interno")),
                "le cavita' scartate non possono riempire il primo ostacolo e produrre un falso successo");
    }
    auto islands = sketch; islands.segments += rectangle(4,0,2,2).segments;
    const auto wide = target(makeBox(Frame3(Vec3(-1,-1,4),Vec3(0,0,1),Vec3(1,0,0)),8,4,2),1252);
    volume(build(islands,{wide}),32);
    const auto internalIsland = build(islands,{inside,wide});
    require(!internalIsland.forgeBody && internalIsland.error.contains(QStringLiteral("ostacolo interno")),
            "la validazione comprende ogni regione di un profilo disgiunto");
    // Fallimenti espliciti: nessun limite, copertura parziale, partenza dentro
    // un corpo, profilo aperto e un percorso laterale oltre il primo ostacolo.
    for (const auto &invalid : QVector<QVector<ExtrusionObject>>{{},{below},{disjoint},{left},
            {target(makeBox(Frame3(Vec3(-1,-1,-1),Vec3(0,0,1),Vec3(1,0,0)),4,4,3),1300)},
            {left,target(makeBox(Frame3(Vec3(1,-1,8),Vec3(0,0,1),Vec3(1,0,0)),2,4,2),1301),farther}}) {
        const auto result = build(sketch,invalid);
        require(!result.forgeBody && !result.error.isEmpty(), "un limite assente o non certificabile deve produrre un errore esplicito");
    }
    auto open = small; open.segments.removeLast();
    const auto openResult = build(open,{above});
    require(!openResult.forgeBody && !openResult.error.isEmpty(), "profilo aperto rifiutato per prossimo corpo");
    auto mixed = small; mixed.segments.append({{3,0},{4,0}});
    const auto mixedResult = build(mixed,{above});
    require(!mixedResult.forgeBody && !mixedResult.error.isEmpty(), "un tratto aperto non viene ignorato in un profilo misto");

    // Candidati rivalutati anche senza riaprire il pannello: un nuovo solido
    // inserito in rollback puo' diventare il nuovo primo limite.
    DocumentState state; state.sketches = {sketch};
    ExtrusionObject distant; distant.feature = BodyFeature::Primitive; distant.name = QStringLiteral("Limite lontano");
    distant.primitive.origin[0] = distant.primitive.origin[1] = -1; distant.primitive.origin[2] = 8;
    distant.primitive.size[0] = distant.primitive.size[1] = 4; distant.primitive.size[2] = 2;
    distant.visible = false; state.extrusions = {distant};
    CadViewport view; view.loadDocument(state);
    ExtrusionObject definition; definition.sketchIndex = 0; definition.extent = 4; definition.distance = 1;
    require(view.createExtrusion(definition,QStringLiteral("Fino al prossimo")).isEmpty(), "creazione prossimo corpo con limite nascosto manualmente");
    const quint64 featureId = view.extrusions().last().featureId;
    const auto check = [&](double height, QVector<int> dependencies) {
        int index = -1;
        for (int i = 0; i < view.extrusions().size(); ++i) if (view.extrusions().at(i).featureId == featureId) index = i;
        require(index >= 0, "identita' dell'estrusione preservata nella storia");
        const auto &feature = view.extrusions().at(index);
        volume(feature,4*height);
        require(feature.booleanTools == dependencies && std::abs(zBounds(feature.forgeBody).second-height) < 1e-12,
                "candidati e geometria si aggiornano a ogni modifica della storia");
    };
    check(8,{0});
    view.setHistoryPosition(1);
    auto closer = distant.primitive; closer.origin[2] = 4;
    require(view.createPrimitive(closer,QStringLiteral("Nuovo limite al cursore")).isEmpty(), "nuovo solido inserito prima dell'estrusione");
    check(4,{0,1});
    view.undo(); check(8,{0}); view.redo(); check(4,{0,1});
    require(view.setFeatureSuppressed(1,true).isEmpty(), "soppressione del nuovo limite"); check(8,{0});
    require(view.setFeatureSuppressed(1,false).isEmpty(), "riattivazione del nuovo limite"); check(4,{0,1});
    view.undo(); check(8,{0}); view.redo(); check(4,{0,1});
    QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes).isEmpty(), "persistenza extent4 nel formato nativo invariato");
    DocumentState loaded; require(decodeDocument(bytes,loaded).isEmpty(), "rilettura della nuova condizione di fine");
    view.loadDocument(loaded); check(4,{0,1});
    require(view.rebuildAllFeatures() == 0, "rigenerazione completa dopo riapertura extent4"); check(4,{0,1});
    auto moved = view.extrusions().at(1); moved.primitive.origin[2] = 2;
    require(view.updateBody(1,moved).isEmpty(), "spostamento del candidato limite"); check(2,{0,1});
    view.undo(); check(4,{0,1}); view.redo(); check(2,{0,1});
    view.deleteFeature(1); check(8,{0}); view.undo(); check(2,{0,1});
    moved = view.extrusions().at(1); moved.primitive.origin[0] = 20; moved.primitive.origin[2] = 1;
    require(view.updateBody(1,moved).isEmpty(), "candidato spostato fuori dal profilo"); check(8,{0,1});
    moved = view.extrusions().at(1); moved.primitive.origin[0] = -1;
    require(view.updateBody(1,moved).isEmpty(), "candidato disgiunto diventa il primo ostacolo"); check(1,{0,1});
    view.undo(); check(8,{0,1}); view.redo(); check(1,{0,1});
    view.setHistoryPosition(2);
    require(view.createBoolean(::BooleanOperation::Union,0,QVector<int>{1},QStringLiteral("Limiti consumati")).isEmpty(),
            "consumo dei due limiti prima dell'estrusione");
    check(1,{2});
    require(view.setFeatureSuppressed(2,true).isEmpty(), "soppressione del consumo"); check(1,{0,1});
    require(view.setFeatureSuppressed(2,false).isEmpty(), "riattivazione del consumo"); check(1,{2});
    view.undo(); check(1,{0,1}); view.redo(); check(1,{2});
    std::cout << "PASS estrusione prossimo corpo: geometria analitica, verso, copertura, dipendenze, rollback, Undo/Redo e codec" << std::endl;
}
