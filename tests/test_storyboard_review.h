#pragma once

#include "test_storyboard_merge_chain.h"
#include "test_sketch_support.h"
#include "test_feature_faces.h"

// Incluso nel friend ViewportInteractionTest. Le prove riguardano identita'
// e dipendenze del documento, senza richiedere un contesto OpenGL.
static void storyboardReentrantRebuildRegression() {
    using namespace ForgeCad;
    // Il detach e' iniettato da una callback accodata, non rappresenta la
    // riproduzione di un evento normale dell'interfaccia. Il viewport resta
    // nascosto: basta la finestra visibile per esercitare il loop responsivo.
    for (bool datum : {false, true}) {
        QWidget window;
        CadViewport view(&window); view.hide();
        if (datum) {
            ExtrusionObject plane; plane.feature = BodyFeature::DatumPlane;
            GeometryRef xy; xy.kind = 1; xy.index = 0;
            plane.datum.refs = {xy}; plane.datum.distance = 3;
            require(view.createBody(plane).isEmpty(), "piano per prova di reingresso");
            view.extrusions_[0].datum.distance = 7;
        } else {
            PrimitiveParameters box; box.size[0] = box.size[1] = box.size[2] = 2;
            require(view.createPrimitive(box, QStringLiteral("Base")).isEmpty()
                && view.createScale(0, 2, 0, {}, QStringLiteral("Scala")).isEmpty(),
                    "corpo per prova di soppressione responsiva");
        }
        window.show();
        bool detached = false;
        QMetaObject::invokeMethod(&view, [&] {
            const auto *before = view.extrusions_.constData();
            view.extrusions_.detach();
            detached = view.responsiveWork_ > 0 && before != view.extrusions_.constData();
        }, Qt::QueuedConnection);
        if (datum) view.regenerateAll();
        else require(view.setFeatureSuppressed(1, true).isEmpty(), "soppressione durante detach iniettato");
        require(detached, "callback accodata provoca il detach dentro il loop di calcolo");
        if (datum) require(std::abs(view.extrusions().at(0).datumFrame.origin[2] - 7) < 1e-12,
                "risultato del piano pubblicato nel vettore corrente dopo il loop");
        else {
            double maximum = 0;
            const auto &body = *view.extrusions().at(1).forgeBody;
            for (auto vertex : body.vertices()) maximum = std::max(maximum, body.vertex(vertex).point.x());
            require(std::abs(maximum - 2) < 1e-12,
                    "risultato della soppressione pubblicato nel vettore corrente dopo il loop");
        }
    }
}

static void storyboardIncrementalRegressions() {
    using namespace ForgeCad;using namespace Kernel;
    PrimitiveParameters box;box.size[0]=box.size[1]=box.size[2]=2;
    {
        CadViewport view;
        for(int i=0;i<5;++i)require(view.createPrimitive(box,QStringLiteral("Indipendente %1").arg(i)).isEmpty(),"corpi indipendenti per invalidazione");
        QVector<ForgeBody> before;for(const auto &b:view.extrusions())before.append(b.forgeBody);
        view.deleteFeature(4);
        require(view.extrusions().size()==4,"eliminata ultima feature");
        for(int i=0;i<4;++i)require(view.extrusions()[i].forgeBody==before[i],"eliminare ultima feature conserva tutti i B-rep precedenti");
        view.undo();require(view.extrusions().size()==5,"Undo eliminazione incrementale");view.redo();
        for(int i=0;i<4;++i)require(view.extrusions()[i].forgeBody==before[i],"Redo conserva geometrie precedenti");
        auto changed=view.extrusions()[2];changed.primitive.size[0]=3;
        require(view.updateBody(2,changed).isEmpty(),"modifica primitiva intermedia");
        require(view.extrusions()[0].forgeBody==before[0]&&view.extrusions()[1].forgeBody==before[1]&&
            view.extrusions()[2].forgeBody!=before[2]&&view.extrusions()[3].forgeBody==before[3],
            "modifica conserva predecessori e rami successivi indipendenti");
        const auto same=view.extrusions()[2].forgeBody;
        require(view.updateBody(2,view.extrusions()[2]).isEmpty()&&view.extrusions()[2].forgeBody==same,
            "conferma senza modifiche conserva il risultato");
    }
    {
        CadViewport view;
        require(view.createPrimitive(box,QStringLiteral("Base")).isEmpty()&&view.createScale(0,2,0,{},QStringLiteral("Scala rimossa")).isEmpty()&&
            view.createPrimitive(box,QStringLiteral("Ramo indipendente")).isEmpty()&&view.createScale(1,3,0,{},QStringLiteral("Consumatore")).isEmpty()&&
            view.createScale(3,2,0,{},QStringLiteral("Discendente")).isEmpty()&&view.createPrimitive(box,QStringLiteral("Ultimo indipendente")).isEmpty(),
            "catena intercalata per invalidazione");
        const auto base=view.extrusions()[0].forgeBody,unrelated=view.extrusions()[2].forgeBody,last=view.extrusions()[5].forgeBody;
        const auto previous=view.extrusions()[3].forgeBody,descendant=view.extrusions()[4].forgeBody;
        view.deleteFeature(1);
        require(view.extrusions().size()==5&&view.extrusions()[2].firstBody==0&&view.extrusions()[3].firstBody==2,"catena ricollegata e indici rimappati");
        require(view.extrusions()[0].forgeBody==base&&view.extrusions()[1].forgeBody==unrelated&&view.extrusions()[4].forgeBody==last,
            "eliminazione intermedia conserva entrambi i rami indipendenti");
        require(view.extrusions()[2].forgeBody!=previous&&view.extrusions()[3].forgeBody!=descendant&&
            std::fabs(massProperties(*view.extrusions()[2].forgeBody).volume-216)<1e-8&&
            std::fabs(massProperties(*view.extrusions()[3].forgeBody).volume-1728)<1e-7,
            "solo consumatore ricollegato e discendente rigenerati con geometria aggiornata");
        view.undo();require(view.extrusions().size()==6&&view.extrusions()[3].forgeBody==previous,"Undo recupera la catena senza nuovo calcolo");
        view.redo();require(view.extrusions().size()==5&&view.extrusions()[0].forgeBody==base,"Redo mantiene il prefisso");
    }
    {
        CadViewport source;require(source.createPrimitive(box,QStringLiteral("Sorgente esterna")).isEmpty(),"sorgente riferimenti indipendenti");
        DocumentState state=source.currentDocument();const auto sourceBody=state.extrusions[0].forgeBody;
        GeometryRef vertex;vertex.kind=3;vertex.index=0;vertex.featureId=state.extrusions[0].featureId;
        vertex.point=vertexReference(*sourceBody,sourceBody->vertices()[0]);
        SketchObject external;external.name="Profilo con riferimento indipendente";external.segments={{{0,0},{1,0}}};external.beforeFeatureId=103;
        require(appendLinkedSketchReference(external,vertex,{},state.extrusions,false,true,100,nullptr,true).isEmpty(),"conversione esplicita indipendente");
        ExtrusionObject datum;datum.feature=BodyFeature::DatumPlane;datum.featureId=102;datum.name="Piano modificato";GeometryRef xy;xy.kind=1;xy.index=0;datum.datum.refs={xy};datum.datum.distance=5;
        ExtrusionObject independent;independent.feature=BodyFeature::Extrusion;independent.featureId=103;independent.sketchIndex=0;independent.extrudeSurface=true;independent.distance=1;
        auto dependent=independent;dependent.featureId=104;dependent.sketchIndex=1;
        SketchObject onDatum;onDatum.name="Profilo sul piano";onDatum.segments={{{0,0},{1,0}}};onDatum.plane=kFacePlane;onDatum.customFrame=true;onDatum.datumPlane=1;onDatum.beforeFeatureId=104;
        state.sketches={external,onDatum};state.extrusions.append(datum);state.extrusions.append(independent);state.extrusions.append(dependent);
        CadViewport view;view.loadDocument(state);
        require(view.extrusions()[2].forgeBody&&view.extrusions()[3].forgeBody,"due superfici per prova datum");
        const auto base=view.extrusions()[0].forgeBody,untouched=view.extrusions()[2].forgeBody,affected=view.extrusions()[3].forgeBody;
        const auto link=view.sketches()[0].linkedReferences[0];
        auto moved=view.extrusions()[1];moved.datum.distance=9;
        require(view.updateBody(1,moved).isEmpty(),"modifica datum incrementale");
        require(view.extrusions()[0].forgeBody==base&&view.extrusions()[2].forgeBody==untouched&&view.sketches()[0].linkedReferences[0]->source.featureId==link->source.featureId
            &&view.sketches()[0].linkedReferences[0]->source.index==link->source.index,
            "datum non aggiorna schizzi o geometrie di rami indipendenti");
        require(view.extrusions()[3].forgeBody&&view.extrusions()[3].forgeBody!=affected&&std::fabs(view.sketches()[1].frame.origin[2]-9)<1e-12,
            "datum aggiorna piano dello schizzo e superficie dipendente");
        view.undo();require(view.extrusions()[2].forgeBody==untouched&&view.extrusions()[3].forgeBody==affected,"Undo datum conserva i risultati precedenti");
        view.redo();require(view.extrusions()[2].forgeBody==untouched&&std::fabs(view.sketches()[1].frame.origin[2]-9)<1e-12,"Redo datum incrementale");
    }
}

static void storyboardDeferredFeatureEditRegression() {
    using namespace ForgeCad;
    using namespace Kernel;

    CadViewport view;
    PrimitiveParameters box;
    box.size[0] = box.size[1] = box.size[2] = 2;
    require(view.createPrimitive(box, QStringLiteral("Base")).isEmpty()
        && view.createScale(0, 2, 0, {}, QStringLiteral("Primo stadio")).isEmpty()
        && view.createPrimitive(box, QStringLiteral("Ramo indipendente")).isEmpty()
        && view.createScale(1, 2, 0, {}, QStringLiteral("Secondo stadio")).isEmpty(),
        "stadi dipendenti e ramo indipendente per modifica in rollback");

    const ForgeBody oldBase = view.extrusions().at(0).forgeBody;
    const quint64 firstId = view.extrusions().at(1).featureId;
    const quint64 lastId = view.extrusions().at(3).featureId;
    const ForgeBody oldFirst = view.extrusions().at(1).forgeBody;
    const ForgeBody independent = view.extrusions().at(2).forgeBody;
    const ForgeBody oldLast = view.extrusions().at(3).forgeBody;
    const auto hasVolume = [](const ForgeBody &body, double expected) {
        return body && std::abs(massProperties(*body).volume - expected) < 1e-8;
    };
    const auto stageIs = [&](int stage, double volume) {
        return view.isBodyDrawn(stage) && hasVolume(view.extrusions().at(stage).forgeBody, volume);
    };
    require(stageIs(3, 512) && oldFirst && independent, "geometria iniziale della catena");

    view.setHistoryPosition(1);
    auto edited = view.extrusions().at(0);
    edited.primitive.size[0] = 3;
    require(view.updateBody(0, edited).isEmpty() && view.historyPosition() == 1,
        "modifica della base conserva la posizione di rollback");
    require(stageIs(0, 12) && !view.isBodyDrawn(1) && !view.isBodyDrawn(2) && !view.isBodyDrawn(3),
        "la scena mostra soltanto la base aggiornata");
    const auto suffixDeferred = [&] {
        return !view.extrusions().at(1).forgeBody && !view.extrusions().at(3).forgeBody
            && view.extrusions().at(1).featureId == firstId && view.extrusions().at(3).featureId == lastId
            && view.extrusions().at(2).forgeBody == independent;
    };
    require(suffixDeferred(), "la modifica rinvia gli stadi futuri e conserva identita' e B-rep indipendente");

    view.undo();
    require(view.historyPosition() == 1 && stageIs(0, 8) && view.extrusions().at(0).forgeBody == oldBase
        && view.extrusions().at(1).forgeBody == oldFirst && view.extrusions().at(3).forgeBody == oldLast
        && view.extrusions().at(2).forgeBody == independent,
        "Undo ripristina posizione, geometria e risultati futuri originali");
    view.redo();
    require(view.historyPosition() == 1 && stageIs(0, 12) && suffixDeferred(),
        "Redo ripristina la modifica con gli stadi futuri ancora differiti");

    view.setHistoryPosition(2);
    const ForgeBody newFirst = view.extrusions().at(1).forgeBody;
    require(stageIs(1, 96) && newFirst != oldFirst && !view.isBodyDrawn(0)
        && !view.isBodyDrawn(3) && !view.extrusions().at(3).forgeBody,
        "avanzare di un passo rigenera il solo stadio entrato nella scena");

    view.setHistoryPosition(3);
    require(stageIs(1, 96) && stageIs(2, 8) && view.extrusions().at(1).forgeBody == newFirst
        && view.extrusions().at(2).forgeBody == independent && !view.extrusions().at(3).forgeBody,
        "attraversare il ramo indipendente riutilizza i risultati pronti");

    view.setHistoryPosition(1);
    require(stageIs(0, 12) && !view.isBodyDrawn(1), "arretrare ripristina lo stadio della base");

    QByteArray bytes;
    require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "salvataggio con ultimo stadio ancora differito");
    DocumentState saved;
    require(decodeDocument(bytes, saved).isEmpty(), "rilettura del documento salvato in rollback");
    CadViewport reopened;
    reopened.loadDocument(saved);
    require(reopened.historyPosition() == 4, "riapertura alla fine della storia");
    require(reopened.isBodyDrawn(3) && hasVolume(reopened.extrusions().at(3).forgeBody, 768),
        "la riapertura produce lo stadio finale aggiornato dalla definizione salvata");

    view.setHistoryPosition(4);
    const ForgeBody newLast = view.extrusions().at(3).forgeBody;
    require(stageIs(3, 768) && newLast != oldLast && !view.isBodyDrawn(1)
        && view.extrusions().at(1).forgeBody == newFirst && view.extrusions().at(2).forgeBody == independent,
        "lo stadio finale usa la geometria nuova e conserva gli altri B-rep");
    view.setHistoryPosition(2);
    require(stageIs(1, 96) && view.extrusions().at(1).forgeBody == newFirst,
        "lo stadio intermedio resta pronto quando il cursore arretra");
    view.setHistoryPosition(4);
    require(stageIs(3, 768) && view.extrusions().at(3).forgeBody == newLast,
        "un nuovo avanzamento riusa lo stadio finale aggiornato");
}

static void storyboardEditKeepsPositionRegression() {
    using namespace ForgeCad;
    using namespace Kernel;

    for (bool independentBranch : {false, true}) {
        CadViewport view;
        PrimitiveParameters box;
        box.size[0] = box.size[1] = box.size[2] = 2;
        require(view.createPrimitive(box, QStringLiteral("Base")).isEmpty()
            && view.createScale(0, 2, 0, {}, QStringLiteral("Primo stadio")).isEmpty()
            && view.createScale(1, 2, 0, {}, QStringLiteral("Secondo stadio")).isEmpty(),
            "catena per verifica della maniglia dopo la modifica");
        if (independentBranch)
            require(view.createPrimitive(box, QStringLiteral("Ramo indipendente")).isEmpty(),
                "operazione indipendente successiva alla feature modificata");
        const int end = view.historyPosition();
        const ForgeBody originalBase = view.extrusions().at(0).forgeBody;
        const ForgeBody originalTip = view.extrusions().at(2).forgeBody;
        const ForgeBody independent = independentBranch ? view.extrusions().at(3).forgeBody : ForgeBody{};
        const auto hasVolume = [](const ForgeBody &body, double expected) {
            return body && std::abs(massProperties(*body).volume - expected) < 1e-8;
        };
        require(originalBase && hasVolume(originalTip, 512) && (!independentBranch || independent),
            "geometria iniziale della catena e del ramo indipendente");

        auto edited = view.extrusions().at(0);
        require(view.updateBody(0, edited).isEmpty() && view.historyPosition() == end
            && view.extrusions().at(0).forgeBody == originalBase && view.extrusions().at(2).forgeBody == originalTip,
            "confermare la stessa definizione conserva cursore e geometria");
        edited.primitive.size[0] = 0;
        require(!view.updateBody(0, edited).isEmpty() && view.historyPosition() == end
            && view.extrusions().at(0).forgeBody == originalBase && view.extrusions().at(2).forgeBody == originalTip,
            "una modifica non valida conserva cursore e geometria");

        edited.primitive.size[0] = 3;
        require(view.updateBody(0, edited).isEmpty(), "conferma della modifica interattiva");
        require(view.storyboardPosition() == end && view.isBodyDrawn(2)
            && hasVolume(view.extrusions().at(2).forgeBody, 768)
            && view.extrusions().at(2).forgeBody != originalTip,
            "confermare la modifica a fine storia mantiene la maniglia e aggiorna il risultato finale");
        if (independentBranch) {
            require(view.isBodyDrawn(3) && view.extrusions().at(3).forgeBody == independent,
                "il ramo indipendente conserva il B-rep");
            continue;
        }

        require(view.createScale(2, 2, 0, {}, QStringLiteral("Ultimo stadio")).isEmpty(),
            "aggiunge un successore oltre la posizione scelta");
        view.setStoryboardPosition(3);
        auto middle = view.extrusions().at(1);
        middle.scaleFactor = 3;
        require(view.updateBody(1, middle).isEmpty() && view.storyboardPosition() == 3
            && view.isBodyDrawn(2) && hasVolume(view.extrusions().at(2).forgeBody, 2592)
            && view.extrusions().at(3).historyRebuildPending && !view.extrusions().at(3).forgeBody,
            "la modifica intermedia ricalcola fino alla maniglia e rinvia il successore oltre la maniglia");
        view.undo();
        require(view.storyboardPosition() == 3 && hasVolume(view.extrusions().at(2).forgeBody, 768),
            "Undo della modifica intermedia mantiene la maniglia");
        view.redo();
        require(view.storyboardPosition() == 3 && hasVolume(view.extrusions().at(2).forgeBody, 2592),
            "Redo della modifica intermedia mantiene la maniglia");
        const DocumentState complete = view.evaluatedDocument();
        require(hasVolume(complete.extrusions.at(3).forgeBody, 20736)
            && view.storyboardPosition() == 3 && !view.isBodyDrawn(3),
            "la valutazione completa conserva il punto scelto nella storyboard");
        view.setStoryboardPosition(4);
        require(view.isBodyDrawn(3) && hasVolume(view.extrusions().at(3).forgeBody, 20736),
            "avanzare la maniglia mostra il risultato finale aggiornato");
    }
}

static void storyboardSketchTailRegression() {
    using namespace ForgeCad;
    {
        CadViewport view;
        PrimitiveParameters box;
        box.size[0] = box.size[1] = box.size[2] = 2;
        require(view.createPrimitive(box, QStringLiteral("Base")).isEmpty()
            && view.createScale(0, 2, 0, {}, QStringLiteral("Stadio dipendente")).isEmpty(),
            "catena con uno schizzo autonomo in coda");
        const int sketch = view.createSketch(0, QStringLiteral("Schizzo autonomo"));
        require(sketch >= 0, "creazione dello schizzo autonomo");
        view.sketches_[sketch].segments = {{{0, 0}, {1, 0}}};
        view.endSketchMode();
        const auto oldStage = view.extrusions().at(1).forgeBody;
        const int end = view.storyboard().size();
        require(view.storyboardIndex(true, sketch) == end - 1 && view.isSketchDrawn(sketch) && oldStage,
            "lo schizzo autonomo occupa l'ultimo passo della storia");

        auto edited = view.extrusions().at(0);
        edited.primitive.size[0] = 3;
        require(view.updateBody(0, edited).isEmpty()
            && view.storyboardPosition() == end && view.isSketchDrawn(sketch)
            && view.isBodyDrawn(1) && view.extrusions().at(1).forgeBody
            && view.extrusions().at(1).forgeBody != oldStage,
            "uno schizzo autonomo in coda mantiene il cursore al fondo e lo stadio dipendente si aggiorna");
    }
    {
        CadViewport view;
        ExtrusionObject datum;
        datum.feature = BodyFeature::DatumPlane;
        GeometryRef xy;
        xy.kind = 1;
        xy.index = 0;
        datum.datum.refs = {xy};
        datum.datum.distance = 2;
        require(view.createBody(datum).isEmpty(), "piano per lo schizzo associato in coda");
        const int sketch = view.createDatumSketch(0, QStringLiteral("Schizzo associato"));
        require(sketch >= 0, "creazione dello schizzo associato");
        view.sketches_[sketch].segments = {{{0, 0}, {1, 0}}};
        view.endSketchMode();
        require(view.storyboardIndex(true, sketch) == 1 && view.isSketchDrawn(sketch)
            && std::abs(view.sketches().at(sketch).frame.origin[2] - 2) < 1e-12,
            "lo schizzo segue il piano iniziale");

        auto edited = view.extrusions().at(0);
        edited.datum.distance = 7;
        require(view.updateBody(0, edited).isEmpty()
            && view.storyboardPosition() == 2 && view.isSketchDrawn(sketch)
            && std::abs(view.extrusions().at(0).datumFrame.origin[2] - 7) < 1e-12
            && std::abs(view.sketches().at(sketch).frame.origin[2] - 7) < 1e-12,
            "lo schizzo associato prima della maniglia resta visibile e segue subito il piano modificato");
    }
}

static void storyboardReviewRegressions() {
    sketchSupportRegressions();
    featureFaceSearchRegressions();
    storyboardMergedChainRegressions();
    storyboardIncrementalRegressions();
    storyboardDeferredFeatureEditRegression();
    storyboardEditKeepsPositionRegression();
    storyboardSketchTailRegression();
    using namespace ForgeCad;
    using namespace Kernel;
    const auto box = [] {
        ExtrusionObject result;
        result.feature = BodyFeature::Primitive;
        result.primitive.kind = PrimitiveKind::Box;
        result.primitive.size[0] = result.primitive.size[1] = result.primitive.size[2] = 2;
        return result;
    };
    const auto findFeature = [](const CadViewport &view, quint64 id) {
        for (int i = 0; i < view.extrusions().size(); ++i)
            if (view.extrusions().at(i).featureId == id) return i;
        return -1;
    };
    for (bool visible : {true,false}) {
        CadViewport view;
        require(view.createPrimitive(box().primitive,QStringLiteral("Corpo con visibilita' manuale")).isEmpty(),
                "corpo per regressione di visibilita'");
        view.setModelBodyVisible(0,visible);
        require(view.isBodyDrawn(0) == visible, "preferenza di visibilita' iniziale");
        require(view.setFeatureSuppressed(0,true).isEmpty() && !view.isBodyDrawn(0), "soppressione completa del corpo");
        require(view.setFeatureSuppressed(0,false).isEmpty() && view.isBodyDrawn(0) == visible
            && view.modelBodies().first().visible == visible,
                "riattivazione della radice conserva visibilita' esplicita del corpo");
        view.undo(); require(!view.isBodyDrawn(0), "Undo riattivazione nasconde la radice soppressa");
        view.redo(); require(view.isBodyDrawn(0) == visible, "Redo riattivazione conserva la preferenza del corpo");
        QByteArray bytes; require(encodeDocument(view.currentDocument(),bytes).isEmpty(), "salvataggio visibilita' recuperata");
        DocumentState loaded; require(decodeDocument(bytes,loaded).isEmpty(), "rilettura visibilita' recuperata");
        view.loadDocument(loaded); require(view.isBodyDrawn(0) == visible, "riapertura conserva il corpo visibile o nascosto a mano");
    }
    for (bool visible : {true,false}) {
        CadViewport view;
        auto base = box().primitive; base.size[0] = 4;
        auto tool = base; tool.origin[0] = 3;
        require(view.createPrimitive(base,QStringLiteral("Base booleana")).isEmpty()
            && view.createPrimitive(tool,QStringLiteral("Utensile booleana")).isEmpty(), "corpi per preferenza prima del consumo");
        view.setModelBodyVisible(1,visible);
        require(view.createBoolean(::BooleanOperation::Difference,0,QVector<int>{1},QStringLiteral("Consumo")).isEmpty(),
                "conferma della booleana");
        require(!view.isBodyDrawn(1) && view.modelBodies().at(1).visible == visible,
                "il consumo non sovrascrive la preferenza visibile/nascosto dell'utensile");
        view.setHistoryPosition(2);
        require(view.isBodyDrawn(1) == visible, "rollback ripristina la preferenza precedente al consumo senza richiedere Mostra");
        view.setHistoryPosition(3); require(!view.isBodyDrawn(1), "avanzare riapplica il consumo alla sola scena");
        view.undo(); require(view.extrusions().size() == 2 && view.isBodyDrawn(1) == visible,
                "Undo della conferma ripristina la preferenza dell'utensile");
        view.redo(); view.setHistoryPosition(2);
        require(view.isBodyDrawn(1) == visible, "Redo e rollback conservano la preferenza prima del consumo");
    }
    {
        DocumentState state;
        auto base = box(), tool = box();
        base.featureId = 301; tool.featureId = 302;
        base.primitive.size[0] = 8; base.primitive.size[1] = 6; base.primitive.size[2] = 4;
        tool.primitive.origin[0] = 6; tool.primitive.size[0] = 4;
        tool.primitive.size[1] = 6; tool.primitive.size[2] = 4;
        ExtrusionObject united; united.featureId = 303; united.operation = 0; united.firstBody = 0; united.secondBody = 1;
        ExtrusionObject scale; scale.featureId = 304; scale.feature = BodyFeature::Scale; scale.firstBody = 2; scale.scaleFactor = 1.1;
        ExtrusionObject repeated; repeated.featureId = 305; repeated.feature = BodyFeature::Pattern;
        repeated.firstBody = 2; repeated.secondBody = 3; repeated.pattern.featureOnly = true;
        repeated.pattern.count = 2; repeated.pattern.spacing = 15;
        GeometryRef axis; axis.kind = 2; axis.index = 0; repeated.pattern.refs = {axis};
        state.extrusions = {base, tool, united, scale, repeated};
        CadViewport view; view.loadDocument(state);
        require(view.extrusions().last().forgeBody && view.extrusions().last().error.isEmpty(),
                "ripetizione di funzione dopo una scala valida");
        view.setHistoryPosition(4);
        require(view.createScale(3, 1.1, 0, {}, QStringLiteral("Scala inserita al cursore")).isEmpty(),
                "inserimento prima della ripetizione di funzione");
        const auto verify = [&](int target, bool expectDeferred = false) {
            const int index = findFeature(view, repeated.featureId);
            require(index >= 0 && view.extrusions().at(index).firstBody == findFeature(view, united.featureId)
                && view.extrusions().at(index).secondBody == target,
                "la ripetizione conserva la funzione esplicita e aggiorna solo lo stadio corrente del corpo");
            const int patternStep = view.storyboardIndex(false, index);
            require(patternStep >= 0, "la ripetizione conserva il proprio passo nella storia");
            if (expectDeferred)
                require(view.storyboardPosition() <= patternStep && !view.extrusions().at(index).forgeBody,
                    "l'inserimento rinvia il calcolo della ripetizione fino al suo passo");
            if (view.storyboardPosition() <= patternStep)
                view.setStoryboardPosition(patternStep + 1);
            require(view.extrusions().at(index).forgeBody && view.extrusions().at(index).error.isEmpty(),
                "la ripetizione produce un risultato valido dopo il passaggio del cursore");
        };
        verify(4, true);
        view.undo(); verify(3); view.redo(); verify(4);
        view.deleteFeature(4); verify(3);
        view.undo(); verify(4); view.redo(); verify(3);
        require(view.moveFeatureTo(3, 4).isEmpty(), "riordino di una scala dopo la ripetizione di funzione");
        verify(-1); view.undo(); verify(3); view.redo(); verify(-1); view.undo(); verify(3);
        QByteArray bytes; require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "codec della ripetizione di funzione");
        DocumentState loaded; require(decodeDocument(bytes, loaded).isEmpty(), "rilettura della ripetizione di funzione");
        view.loadDocument(loaded); verify(3);
    }
    {
        DocumentState state;
        auto unrelated = box(), source = box();
        unrelated.featureId = 101; source.featureId = 102;
        ExtrusionObject extracted;
        extracted.feature = BodyFeature::ExtractedCurve; extracted.featureId = 103;
        GeometryRef ref; ref.kind = 4; ref.index = 1; ref.featureId = source.featureId;
        extracted.curveExtraction.sources = {ref, ref};
        state.extrusions = {unrelated, source, extracted};
        CadViewport view; view.loadPreviewDocument(state);
        view.recordUndo();
        view.removeBodies({0}); view.documentChanged();
        const auto verify = [&](int owner, int result) {
            const auto &feature = view.extrusions().at(result);
            require(feature.curveExtraction.sources.size() == 2
                && feature.curveExtraction.sources.at(0).index == owner
                && feature.curveExtraction.sources.at(1).index == owner
                && feature.curveExtraction.sources.at(0).featureId == source.featureId
                && bodyOperands(feature) == QVector<int>{owner},
                "eliminare una feature precedente rimappa tutte le sorgenti dell'estrazione multipla");
        };
        verify(0, 1);
        view.undo(); verify(1, 2);
        view.redo(); verify(0, 1);
    }
    {
        DocumentState state;
        SketchObject unrelated, source;
        unrelated.storyId = 201; source.storyId = 202; source.beforeFeatureId = 203;
        source.segments = {{{0, 0}, {3, 0}}, {{3, 0}, {5, 0}}};
        state.sketches = {unrelated, source};
        ExtrusionObject extracted;
        extracted.feature = BodyFeature::ExtractedCurve; extracted.featureId = 203;
        GeometryRef ref; ref.kind = 7; ref.index = 1; ref.element = {0, 0, -1};
        GeometryRef second = ref; second.element.element = 1;
        extracted.curveExtraction.sources = {ref, second};
        state.extrusions = {extracted};
        CadViewport view; view.loadDocument(state);
        require(view.extrusions().first().curve && view.extrusions().first().error.isEmpty(),
                "estrazione esatta da due segmenti dello schizzo");
        view.deleteObject(SceneObjectKind::Sketch, 0);
        const auto verify = [&](int owner) {
            const auto &feature = view.extrusions().first();
            require(feature.curveExtraction.sources.at(0).index == owner
                && feature.curveExtraction.sources.at(1).index == owner
                && sketchesOf(feature) == QVector<int>{owner},
                "eliminare uno schizzo precedente rimappa tutte le sorgenti dell'estrazione multipla");
        };
        verify(0); view.undo(); verify(1); view.redo(); verify(0);
        view.sketches_[0].segments.last().second = {6, 0};
        view.regenerateDependents(0);
        const auto curve = view.extrusions().first().curve;
        require(curve && view.extrusions().first().error.isEmpty()
            && std::abs(std::max(curve->point(curve->domain().lo).x(), curve->point(curve->domain().hi).x()) - 6) < 1e-12,
                "modificare lo schizzo rimappato rigenera la curva esatta dipendente");
    }
    for (bool pierce : {false, true}) {
        CadViewport view;
        const auto source = box();
        require(view.createPrimitive(source.primitive, QStringLiteral("Base ripetizione")).isEmpty(), "base del test ripetizioni");
        ExtrusionObject pattern;
        pattern.feature = BodyFeature::Pattern; pattern.firstBody = 0;
        pattern.pattern.count = 3; pattern.pattern.spacing = 8;
        GeometryRef axis; axis.kind = 2; axis.index = 0; pattern.pattern.refs = {axis};
        require(view.createBody(pattern, {0}).isEmpty(), "ripetizione con tre risultati distinti");
        int component = -1;
        for (int i = 0; i < view.extrusions().size(); ++i)
            if (view.extrusions().at(i).patternComponent == 3) component = i;
        require(component >= 0, "terza componente disponibile");
        const auto original = view.extrusions().at(component);
        GeometryRef ref; ref.kind = pierce ? 5 : 3; ref.index = component; ref.featureId = original.featureId;
        SketchObject sketch; sketch.name = QStringLiteral("Dipendente dalla terza copia");
        if (pierce) {
            FaceId side;
            for (FaceId face : original.forgeBody->faces()) {
                const auto &plane = static_cast<const Plane &>(*original.forgeBody->face(face).surface);
                const Vec3 normal = original.forgeBody->face(face).sense ? plane.frame().zDir() : -plane.frame().zDir();
                if (normal.x() < -0.99) { side = face; break; }
            }
            require(side.valid(), "faccia laterale della copia per Pierce");
            ref.point = faceReference(*original.forgeBody, side, Vec3(16, 1, 1));
            sketch.segments = {{{16, 1}, {16, 2}}};
            SketchConstraint constraint; constraint.type = ConstraintType::Pierce; constraint.first = {0, 0, 0};
            require(refreshSurfacePierce(sketch, constraint, *original.forgeBody, ref, original.name).isEmpty(),
                    "Pierce valido sulla componente originale");
            sketch.geometricConstraints = {constraint};
        } else {
            ref.point = vertexReference(*original.forgeBody, original.forgeBody->vertices().front());
            require(appendLinkedSketchReference(sketch, ref, {}, view.extrusions()).isEmpty(), "collegamento alla copia");
        }
        view.sketches_ = {sketch}; view.documentChanged();
        auto definition = view.extrusions().at(1); definition.pattern.count = 2;
        require(view.updateBody(1, definition).isEmpty(), "riduzione del conteggio");
        const int missing = findFeature(view, original.featureId);
        require(missing >= 0 && !view.extrusions().at(missing).error.isEmpty(),
                "una copia usata da un riferimento dello schizzo conserva identita' e diagnosi quando scompare");
        require(pierce ? !view.sketches().at(0).geometricConstraints.at(0).pierce->error.isEmpty()
                       : !view.sketches().at(0).referenceError.isEmpty(),
                "lo schizzo segnala la componente assente");
        view.undo();
        require(findFeature(view, original.featureId) >= 0
            && view.extrusions().at(findFeature(view, original.featureId)).forgeBody,
                "Undo conserva l'identita' della copia usata dallo schizzo");
        view.redo();
        require(findFeature(view, original.featureId) >= 0, "Redo conserva la componente assente referenziata");
        definition = view.extrusions().at(1); definition.pattern.count = 3;
        require(view.updateBody(1, definition).isEmpty(), "ripristino del conteggio");
        const int restored = findFeature(view, original.featureId);
        require(restored >= 0 && view.extrusions().at(restored).forgeBody
            && view.extrusions().at(restored).error.isEmpty(),
                "ripristinare il conteggio recupera la stessa componente, senza riagganci ad altre copie");
        require(view.isBodyDrawn(restored), "una componente ripristinata recupera la visibilita' del corpo precedente all'errore");
        if (!pierce) require(view.sketches().at(0).referenceError.isEmpty(), "riferimento associativo recuperato");
        else require(view.sketches().at(0).geometricConstraints.at(0).pierce->error.isEmpty()
            && solveSketch(view.sketches_[0]).ok, "Pierce recuperato sulla stessa componente");
        QByteArray bytes; require(encodeDocument(view.currentDocument(), bytes).isEmpty(), "codec delle identita' ripristinate");
        DocumentState loaded; require(decodeDocument(bytes, loaded).isEmpty(), "rilettura delle identita' ripristinate");
        CadViewport reopened; reopened.loadDocument(loaded);
        require(findFeature(reopened, original.featureId) >= 0, "identita' della copia persistente nel documento");
    }
    storyboardReentrantRebuildRegression();
    std::cout << "PASS storyboard review: rimappatura estrazioni, riferimenti a copie, Undo/Redo, persistenza e detach iniettato" << std::endl;
}
