#pragma once

static void storyboardMergedReferenceRegressions() {
    using namespace ForgeCad;
    DocumentState state;
    ExtrusionObject base; base.feature = BodyFeature::Primitive; base.featureId = 101;
    base.modelBodyId = 10; base.cachedGeometry = true;
    auto tool = base; tool.featureId = 102; tool.modelBodyId = 20;
    auto cut = base; cut.featureId = 103; cut.feature = BodyFeature::Extrusion;
    cut.mergeOperation = 2; cut.mergeBodies = {0};
    auto boss = cut; boss.featureId = 104; boss.feature = BodyFeature::Revolution;
    boss.mergeOperation = 1; boss.mergeBodies = {0,1};
    boss.revolveAxisRef.kind = 4; boss.revolveAxisRef.index = 0; boss.revolveAxisRef.featureId = 101;
    boss.revolveAxisRef.point.subshape = 77;
    auto independent = base; independent.featureId = 105; independent.modelBodyId = 30;
    auto derived = cut; derived.featureId = 106; derived.modelBodyId = 40;
    derived.mergeOperation = 0; derived.sketchIndex = 1;
    state.extrusions = {base,tool,cut,boss,independent,derived};
    require(reconnectMergedHistory(state.extrusions,{20}).isEmpty(), "ricongiungimento limitato al corpo modificato");
    for (auto type : {BodyFeature::Extrusion,BodyFeature::Revolution,BodyFeature::Sweep}) {
        state.extrusions[3].feature = type; state.extrusions[3].mergeBodies = {0,1};
        require(reconnectMergedHistory(state.extrusions) == QVector<int>{3}
            && state.extrusions[3].mergeBodies == QVector<int>({2,1}), "ricongiunta solo la base primaria della fusione");
    }
    const auto &axis = state.extrusions[3].revolveAxisRef;
    require(axis.index == 0 && axis.featureId == 101 && axis.point.subshape == 77,
            "asse esplicito conserva produttore ed entita topologica");
    require(reconnectMergedHistory(state.extrusions).isEmpty(), "ricongiungimento idempotente");
    SketchObject first; first.storyId = 501;
    first.support = std::make_shared<GeometryRef>(); first.support->kind = 5;
    first.support->featureId = 104; first.support->index = 0; // la cache indice e' volutamente obsoleta
    auto second = first; second.storyId = 502; second.support = std::make_shared<GeometryRef>();
    second.support->kind = 6; second.support->featureId = 501; second.support->index = 0;
    state.sketches = {first,second};
    invalidateHistoryCaches(state,{3});
    require(!state.extrusions[3].cachedGeometry && !state.extrusions[5].cachedGeometry,
            "invalidazione attraversa riferimenti persistenti e catene di schizzi");
    for (int index : {0,1,2,4}) require(state.extrusions[index].cachedGeometry,
            "invalidazione conserva predecessori e corpi indipendenti");
    state.extrusions[3].mergeBodies = {-1};
    require(reconnectMergedHistory(state.extrusions).isEmpty() && state.extrusions[3].mergeBodies == QVector<int>{-1},
            "un riferimento mancante mantiene la diagnostica invece di essere riassegnato");
}


static void storyboardMergedChainRegressions() {
    storyboardMergedReferenceRegressions();
    using namespace ForgeCad;
    using namespace Kernel;
    const auto rectangle = [](double x, double y, double width, double height, quint64 before) {
        SketchObject sketch; sketch.plane = 1; sketch.beforeFeatureId = before;
        sketch.storyId = before + quint64(x * 10) + 10000;
        sketch.segments = {{{0,-2},{0,-1}}}; sketch.constructionSegments = {0};
        const QPointF p[] = {{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
        for (int i=0;i<4;++i) sketch.segments.append({p[i],p[(i+1)%4]});
        return sketch;
    };
    const auto definition = [&](bool stale) {
        DocumentState state;
        ExtrusionObject base; base.feature = BodyFeature::Revolution; base.featureId = 9001;
        base.sketchIndex = 0; base.revolveAxis = 0; base.revolveAngle = 360;
        auto boss = base; boss.featureId = 9002; boss.sketchIndex = 1;
        boss.mergeOperation = 1; boss.mergeAuto = false; boss.mergeBodies = {0};
        auto cut = base; cut.featureId = 9003; cut.sketchIndex = 2;
        cut.mergeOperation = 2; cut.mergeAuto = false; cut.mergeBodies = {0};
        ExtrusionObject independent; independent.feature = BodyFeature::Primitive; independent.featureId = 9004;
        independent.primitive.origin[0] = 20;
        independent.primitive.size[0] = independent.primitive.size[1] = independent.primitive.size[2] = 2;
        state.sketches = {rectangle(1,0,3,4,9001), rectangle(3,1,2,2,9002), rectangle(0,0,2,1,stale?9003:9002)};
        state.extrusions = stale ? QVector<ExtrusionObject>{base,cut,boss,independent}
                                 : QVector<ExtrusionObject>{base,boss,independent};
        return state;
    };
    const auto verify = [&](CadViewport &view, int cut, int boss) {
        require(view.extrusions()[boss].mergeBodies == QVector<int>{cut}, "la fusione successiva usa la sottrazione intermedia");
        require(view.extrusions()[boss].forgeBody && view.extrusions()[boss].error.isEmpty(), "risultato successivo valido");
        require(std::abs(massProperties(*view.extrusions()[boss].forgeBody).volume - 75 * M_PI) < 1e-7,
                "il volume finale conserva la sottrazione di tre pi greco");
        const auto shape = view.extrusions()[boss].forgeBody;
        view.setStoryboardPosition(view.storyboardIndex(false,cut)+1);
        view.setStoryboardPosition(view.storyboard().size());
        require(view.extrusions()[boss].forgeBody == shape && view.isBodyDrawn(boss),
                "avanzare il cursore conserva il risultato completo senza ricalcolarlo");
    };
    // Riproduce anche uno snapshot salvato prima della correzione, con la
    // sottrazione presente ma scavalcata dalla fusione seguente.
    {
        CadViewport legacy; legacy.loadDocument(definition(true)); verify(legacy,1,2);
        QByteArray bytes; require(encodeDocument(legacy.currentDocument(),bytes,true).isEmpty(), "codec della catena riparata");
        DocumentState loaded; require(decodeDocument(bytes,loaded,true).isEmpty(), "rilettura della catena");
        loaded.extrusions[2].mergeBodies = {0};
        const auto unrelated = loaded.extrusions[3].forgeBody;
        CadViewport reopened; reopened.loadDocument(loaded); verify(reopened,1,2);
        require(reopened.extrusions()[3].forgeBody == unrelated, "riparazione conserva la cache del ramo indipendente");
        require(reopened.rebuildAllFeatures() == 0, "ricalcolo completo della catena riparata"); verify(reopened,1,2);
        require(encodeDocument(reopened.currentDocument(),bytes,false).isEmpty() && decodeDocument(bytes,loaded,false).isEmpty(),
                "codec della catena senza cache");
        CadViewport portable; portable.loadDocument(loaded); verify(portable,1,2);
    }
    for (bool edit : {false,true}) for (bool automatic : {false,true}) for (bool sketchOpen : {false,true}) {
        CadViewport view; view.loadDocument(definition(false));
        const auto independentId = view.extrusions()[2].featureId;
        view.setStoryboardPosition(view.storyboardIndex(false,1));
        if (sketchOpen) { view.activeSketch_ = 2; view.beginSketchMode(view.sketches()[2].plane); }
        ExtrusionObject cut; cut.feature = BodyFeature::Revolution; cut.sketchIndex = 2;
        cut.revolveAxis = 0; cut.revolveAngle = 360; cut.mergeOperation = edit ? 0 : 2;
        cut.mergeAuto = automatic; cut.mergeBodies = {0};
        require(view.createBody(cut).isEmpty(), "inserimento di una rivoluzione prima della fusione");
        require(view.storyboardIndex(true,2) < view.storyboardIndex(false,1), "il profilo resta prima della rivoluzione inserita");
        const auto independent = view.extrusions()[3].forgeBody;
        if (edit) {
            auto updated = view.extrusions()[1]; updated.mergeOperation = 2;
            require(view.updateBody(1,updated).isEmpty(), "la rivoluzione inserita diventa una sottrazione");
        }
        if (sketchOpen) {
            require(view.isBodyDrawn(1) && !view.isBodyDrawn(2), "la sessione schizzo mostra la conferma e nasconde le funzioni future");
            view.endSketchMode();
        }
        verify(view,1,2);
        require(view.extrusions()[3].featureId == independentId && view.extrusions()[3].forgeBody == independent, "inserimento/modifica conserva il ramo indipendente");
        view.undo();
        if (edit) require(view.extrusions()[1].mergeOperation == 0 && view.extrusions()[2].mergeBodies == QVector<int>{0},
                          "Undo rimuove la fusione dalla catena");
        else require(view.extrusions().size() == 3 && view.extrusions()[1].mergeBodies == QVector<int>{0}, "Undo rimuove l'inserzione");
        view.redo(); verify(view,1,2);
    }
}
