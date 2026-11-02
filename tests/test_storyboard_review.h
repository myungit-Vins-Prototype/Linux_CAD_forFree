#pragma once

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

static void storyboardReviewRegressions() {
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
        const auto verify = [&](int target) {
            const int index = findFeature(view, repeated.featureId);
            require(index >= 0 && view.extrusions().at(index).firstBody == findFeature(view, united.featureId)
                && view.extrusions().at(index).secondBody == target && view.extrusions().at(index).forgeBody
                && view.extrusions().at(index).error.isEmpty(),
                "la ripetizione conserva la funzione esplicita e aggiorna solo lo stadio corrente del corpo");
        };
        verify(4);
        view.setHistoryPosition(view.extrusions().size());
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
