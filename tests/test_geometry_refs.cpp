#include "cad_geometry_refs.h"
#include "cad_model_history.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
GeometryRef ref(int index) {
    GeometryRef value;
    value.kind = 5;
    value.index = index;
    value.point.name = QStringLiteral("faccia-%1").arg(index);
    return value;
}
}

int main() {
    using namespace ForgeCad;
    using Family = GeometryRefFamily;
    try {
        // Popolazione indipendente dal visitatore: un campo omesso, ripetuto
        // o associato alla famiglia sbagliata deve fallire il confronto.
        ExtrusionObject feature;
        feature.datum.refs = {ref(1)};
        feature.pattern.refs = {ref(2)};
        feature.surfaceTool.targets = {ref(3), ref(4)};
        feature.ruledFirst = {ref(5)}; feature.ruledSecond = {ref(6)};
        feature.planarRefs = {ref(7)};
        feature.loftStartFaces = {ref(8)}; feature.loftEndFaces = {ref(9)};
        feature.loftSections = {ref(10)}; feature.loftEdgeGuides = {ref(11)};
        feature.extentRef = ref(12); feature.move.axis = ref(13);
        feature.revolveAxisRef = ref(14); feature.draftNeutral = ref(15);
        feature.thickenDirection = ref(16); feature.sweepProfileRef = ref(17);
        feature.sweepPathRef = ref(18); feature.curveExtraction.source = ref(19);
        feature.curveExtraction.sources = {ref(20), ref(21)};
        feature.curveExtraction.tool = ref(22);
        const QVector<Family> families{Family::Datum, Family::Pattern, Family::SurfaceTargets, Family::SurfaceTargets,
            Family::RuledFirst, Family::RuledSecond, Family::Planar, Family::LoftStartFaces, Family::LoftEndFaces,
            Family::LoftSections, Family::LoftEdgeGuides, Family::Extent, Family::MoveAxis, Family::RevolveAxis,
            Family::DraftNeutral, Family::ThickenDirection, Family::SweepProfile, Family::SweepPath,
            Family::ExtractedSource, Family::ExtractedSources, Family::ExtractedSources, Family::ExtractedTool};
        int visited = 0;
        visitGeometryRefs(std::as_const(feature), [&](const auto &value, Family family, int position) {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(value)>>);
            require(visited < families.size() && value.index == visited + 1 && family == families.at(visited),
                    "tutte le famiglie enumerate una volta nell'ordine dichiarato");
            require(position == ((value.index == 4 || value.index == 21) ? 1 : 0),
                    "posizione conservata per target multipli e sorgenti multiple");
            ++visited;
        });
        require(visited == 22, "numero completo dei riferimenti della fixture");
        QVector<int> selected;
        visitGeometryRefs(feature, {Family::MoveAxis, Family::SurfaceTargets, Family::Datum},
            [&](const GeometryRef &value, Family, int) { selected.append(value.index); });
        require(selected == QVector<int>({13, 3, 4, 1}), "ordine della selezione famiglie scelto dal chiamante");
        const auto before = feature;
        visitGeometryRefs(feature, [](GeometryRef &value, Family, int) { value.index += 100; });
        require(feature.extentRef.index == 112 && feature.surfaceTool.targets[1].index == 104
                && feature.curveExtraction.sources[1].index == 121 && before.extentRef.index == 12
                && before.surfaceTool.targets[1].index == 4 && before.curveExtraction.sources[1].index == 21,
                "visitatore mutabile aggiorna campi singoli/vettori senza alterare copie");

        // La migrazione migra tutti i riferimenti strutturali, anche se
        // inattivi per il tipo corrente; non sovrascrive gli ID gia' assegnati.
        visitGeometryRefs(feature, [](GeometryRef &value, Family, int) { value.index = 0; value.featureId = 0; });
        feature.curveExtraction.source.featureId = 999;
        ExtrusionObject source;
        source.feature = BodyFeature::Primitive; source.featureId = 70; source.modelBodyId = 1;
        feature.feature = BodyFeature::SurfaceTools; feature.featureId = 71;
        feature.firstBody = 0; feature.modelBodyId = 1;
        QVector<ExtrusionObject> features{source, feature};
        QVector<ModelBody> bodies;
        normalizeModelHistory(features, bodies);
        visitGeometryRefs(std::as_const(features.at(1)), [](const GeometryRef &value, Family family, int) {
            require(value.featureId == (family == Family::ExtractedSource ? 999 : 70),
                    "migrazione comune copre tutte le famiglie senza riassegnare ID persistenti");
        });

        SketchObject sketch;
        sketch.support = std::make_shared<GeometryRef>(ref(0));
        SketchConstraint unrelated, constraint;
        constraint.type = ConstraintType::Pierce;
        auto data = std::make_shared<SketchPierceData>(); data->surface = ref(1);
        constraint.pierce = data;
        sketch.geometricConstraints = {unrelated, constraint};
        const auto undoSnapshot = sketch;
        QVector<int> sketchRefs;
        visitSketchGeometryRefs(std::as_const(sketch), [&](const auto &value, SketchGeometryRefFamily family, int position) {
            static_assert(std::is_const_v<std::remove_reference_t<decltype(value)>>);
            sketchRefs.append(value.index);
            require(position == (family == SketchGeometryRefFamily::Support ? 0 : 1),
                    "visita Pierce conserva la posizione del vincolo nel documento");
        });
        require(sketchRefs == QVector<int>({0, 1}), "supporto e Pierce visitati, vincolo privo di riferimenti ignorato");
        QVector<SketchObject> sketches{sketch};
        remapSketchSupports(sketches, [](int index) { return index + 5; });
        require(sketches[0].support->index == 5 && sketches[0].geometricConstraints[1].pierce->surface.index == 6
                && undoSnapshot.support->index == 0 && undoSnapshot.geometricConstraints[1].pierce->surface.index == 1
                && sketches[0].support != undoSnapshot.support
                && sketches[0].geometricConstraints[1].pierce != undoSnapshot.geometricConstraints[1].pierce,
                "remap distacca supporto/Pierce e conserva lo snapshot Undo");
        sketches = {sketch};
        require(!storySketchDependencyError(sketches, features, {{false,0},{true,0},{false,1}}).isEmpty(),
                "Pierce verso feature successiva impedisce il riordino");
        require(storySketchDependencyError(sketches, features, {{false,0},{false,1},{true,0}}).isEmpty(),
                "supporto e Pierce precedenti consentono il riordino");
        // Risultato in coda a un consumatore: rimappare tutte le famiglie,
        // senza separare lo schizzo dal gruppo che ne genera l'appoggio.
        ExtrusionObject root; root.feature = BodyFeature::Pattern; root.patternComponent = 0;
        root.firstBody = 0; root.featureId = 71; root.modelBodyId = 1;
        ExtrusionObject part; part.feature = BodyFeature::Pattern; part.patternComponent = 2;
        part.firstBody = 1; part.featureId = 73; part.modelBodyId = 2;
        auto consumer = feature; consumer.featureId = 72; consumer.modelBodyId = 2;
        consumer.firstBody = 3; consumer.secondBody = 3; consumer.booleanTools = {3}; consumer.mergeBodies = {3};
        visitGeometryRefs(consumer, [](GeometryRef &value, Family, int) { value.index = 3; value.featureId = 73; });
        features = {source, root, consumer, part};
        sketch.support = std::make_shared<GeometryRef>(ref(3)); sketch.support->featureId = 73;
        sketch.storyId = 1; sketch.beforeFeatureId = 73;
        sketches = {sketch};
        const auto snapshot = sketches;
        const auto mapping = normalizePatternResultOrder(sketches, features);
        require(mapping == QVector<int>({0,1,3,2}) && features[2].featureId == 73 && features[2].firstBody == 1,
                "i risultati precedono i consumatori con identita' immutate");
        require(features[3].firstBody == 2 && features[3].secondBody == 2
                && features[3].booleanTools == QVector<int>{2} && features[3].mergeBodies == QVector<int>{2},
                "rimappatura di tutti gli operandi diretti");
        visitGeometryRefs(std::as_const(features[3]), [](const GeometryRef &value, Family, int) {
            require(value.index == 2 && value.featureId == 73, "tutte le famiglie seguono lo stesso risultato persistente");
        });
        require(sketches[0].support->index == 2 && snapshot[0].support->index == 3,
                "riordino non modifica l'appoggio dello snapshot Undo");
        const auto story = modelStory(sketches, features);
        require(story.size() == 4 && !story[1].sketch && story[1].index == 1 && story[2].sketch,
                "lo schizzo ancorato al risultato viene dopo l'intera ripetizione");
        require(storySketchDependencyError(sketches, features, story).isEmpty(),
                "appoggio sul secondo corpo valido dopo il raggruppamento");
        require(storyFeatureLimit(features, story, 2) == 3 && storyFeatureLimit(features, story, 3) == 3,
                "cursore e contesto schizzo includono entrambi i risultati nello stesso passo");
        require(normalizePatternResultOrder(sketches, features).isEmpty(), "normalizzazione idempotente");
        std::cout << "PASS visitatori riferimenti, famiglie, migrazione, supporto/Pierce e Undo\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
