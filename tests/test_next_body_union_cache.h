// Incluso nel friend ViewportInteractionTest.
static void nextBodyUnionCacheRegressions() {
    using namespace ForgeCad;
    const auto target = [] {
        ExtrusionObject body; body.feature = BodyFeature::Primitive;
        body.name = QStringLiteral("Corpo limite");
        body.primitive.origin[0] = body.primitive.origin[1] = -1;
        body.primitive.origin[2] = 4;
        body.primitive.size[0] = body.primitive.size[1] = 4;
        body.primitive.size[2] = 2;
        return body;
    };
    SketchObject sketch;
    sketch.segments = {{{0,0},{2,0}},{{2,0},{2,2}},{{2,2},{0,2}},{{0,2},{0,0}}};
    DocumentState state; state.sketches = {sketch}; state.extrusions = {target()};
    CadViewport view; view.tessellationQuality_ = 0; view.loadDocument(state);
    ExtrusionObject prism; prism.sketchIndex = 0; prism.extent = 4; prism.distance = 1;
    require(view.createExtrusion(prism, QStringLiteral("Cache del solo prisma")).isEmpty(), "fixture prossimo corpo separato");
    // Riproduce il file salvato prima della correzione: parametri Unisci
    // automatici, ma B-rep del prisma e nessun bersaglio persistito.
    view.extrusions_[1].mergeOperation = 1; view.extrusions_[1].mergeAuto = true;
    view.extrusions_[1].mergeBodies.clear();
    const auto requested = view.withMergeCandidates(view.extrusions_[1], 1);
    require(requested.mergeProbe && requested.mergeBodies == QVector<int>{0}
                && requested.booleanTools == QVector<int>{0}, "Corpo limite candidato anche all'unione");
    require(!view.unchangedFeature(requested, 1), "vecchia unione vuota deve ricalcolare anteprima e conferma");
    require(!view.unchangedFeature(view.extrusions_[1], 1), "guardia valida anche senza normalizzazione preventiva");

    // Il normale riuso resta valido quando l'unione ha gia' associato il
    // bersaglio, oppure quando si richiede deliberatamente Corpo nuovo.
    view.extrusions_[1].mergeBodies = {0};
    require(view.unchangedFeature(view.withMergeCandidates(view.extrusions_[1], 1), 1), "unione associata invariata riusabile");
    view.extrusions_[1].mergeBodies.clear(); view.extrusions_[1].mergeOperation = 0;
    require(view.unchangedFeature(view.withMergeCandidates(view.extrusions_[1], 1), 1), "corpo nuovo invariato riusabile");
    view.extrusions_[1].mergeOperation = 1; view.extrusions_[1].extent = 0;
    require(view.unchangedFeature(view.withMergeCandidates(view.extrusions_[1], 1), 1), "nessuna invalidazione indiscriminata delle altre estrusioni");
    view.extrusions_[1].extent = 4;
    auto &limit = view.extrusions_[0]; limit.visible = false;
    if (limit.modelBodyId) for (auto &model : view.modelBodies_) if (model.id == limit.modelBodyId) model.visible = false;
    require(view.withMergeCandidates(view.extrusions_[1], 1).mergeBodies.isEmpty()
                && view.unchangedFeature(view.withMergeCandidates(view.extrusions_[1], 1), 1),
            "senza candidati correnti non invalidare il B-rep separato");
    std::cout << "PASS cache unione prossimo corpo: vecchio prisma ricalcolato, riuso normale preservato" << std::endl;
}
