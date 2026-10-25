#include "cad_feature_dependencies.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
GeometryRef reference(int kind, int index) {
    GeometryRef ref;
    ref.kind = kind;
    ref.index = index;
    return ref;
}
}

int main() {
    using namespace ForgeCad;
    try {
        ExtrusionObject loft;
        loft.feature = BodyFeature::Loft;
        loft.loftSections = {reference(4, 2), reference(7, 3)};
        loft.loftEdgeGuides = {reference(4, 2), reference(9, 5), reference(7, 3)};
        loft.loftGuides = {4, 3};
        loft.loftGuidePaths = {{4, {0}, {}}, {6, {}, {0}}};
        require(bodyOperands(loft) == QVector<int>({2, 5}), "loft: dipendenze da bordi/curve senza duplicati");
        require(sketchesOf(loft) == QVector<int>({3, 4, 6}), "loft: sezioni e guide complete/parziali senza duplicati");
        loft.loftSections.clear();
        loft.loftSketches = {1, 2};
        require(sketchesOf(loft) == QVector<int>({1, 2, 3, 4, 6}), "loft: sezioni dei documenti precedenti conservate");

        ExtrusionObject sweep;
        sweep.feature = BodyFeature::Sweep;
        sweep.sweepProfileRef = reference(9, 2);
        sweep.sweepPathRef = reference(4, 5);
        require(bodyOperands(sweep) == QVector<int>({2, 5}) && sketchesOf(sweep).isEmpty(),
                "sweep: sezione estratta e bordo con dipendenze associative");
        sweep.sweepProfileRef = reference(7, 3);
        require(sketchesOf(sweep) == QVector<int>({3}), "sweep: sezione da entita' di schizzo");
        sweep.sweepProfileRef = {}; sweep.sweepPathRef = {};
        sweep.sketchIndex = 1; sweep.pathSketch = 4;
        require(sketchesOf(sweep) == QVector<int>({1, 4}), "sweep: compatibilita' con schizzi dei documenti precedenti");

        ExtrusionObject move;
        move.feature = BodyFeature::Transform;
        move.firstBody = 2;
        move.move.axis = reference(8, 4);
        move.move.copy = true;
        require(bodyOperands(move) == QVector<int>({2, 4}) && hiddenOperands(move).isEmpty(),
                "copia: base e asse necessari, base ancora visibile");
        move.move.copy = false;
        move.move.axis = reference(7, 5);
        require(bodyOperands(move) == QVector<int>({2}) && sketchesOf(move) == QVector<int>({5})
                    && hiddenOperands(move) == QVector<int>({2}), "spostamento: asse dello schizzo e base consumata");

        ExtrusionObject trim;
        trim.feature = BodyFeature::SheetTrim;
        trim.firstBody = 1;
        trim.sketchIndex = 3;
        require(bodyOperands(trim) == QVector<int>({1}) && sketchesOf(trim) == QVector<int>({3}),
                "taglio: superficie e schizzo strumento necessari");
        trim.secondBody = 2;
        trim.trimBoth = true;
        require(hiddenOperands(trim) == QVector<int>({1, 2}), "taglio reciproco: entrambi i corpi consumati");
        trim.trimBoth = false;
        require(hiddenOperands(trim) == QVector<int>({1}), "taglio semplice: strumento ancora visibile");

        ExtrusionObject extrusion;
        extrusion.sketchIndex = -1;
        extrusion.mergeBodies = {4};
        extrusion.extent = 1;
        extrusion.extentRef = reference(5, 8);
        require(bodyOperands(extrusion) == QVector<int>({8}) && sketchesOf(extrusion) == QVector<int>({-1}),
                "riferimenti mancanti mantenuti e fusione inattiva esclusa");
        extrusion.mergeOperation = 1;
        require(bodyOperands(extrusion) == QVector<int>({4, 8}) && hiddenOperands(extrusion) == QVector<int>({4}),
                "fusione: bersaglio consumato, riferimento del limite ancora visibile");
        extrusion.operation = 0;
        extrusion.firstBody = 1;
        extrusion.secondBody = 2;
        extrusion.booleanTools = {3};
        require(bodyOperands(extrusion) == QVector<int>({1, 2, 3}) && sketchesOf(extrusion).isEmpty(),
                "booleana: soli operandi, nessuna dipendenza residua dal profilo");
        ExtrusionObject surface;
        surface.feature = BodyFeature::SurfaceTools;
        surface.firstBody = 1;
        surface.surfaceTool.targets = {reference(5, 4), reference(5, 4), reference(7, 6)};
        surface.ruledFirst = {reference(9, 5), reference(7, 7)};
        surface.ruledSecond = {reference(9, 4), reference(7, 6)};
        surface.extentRef = reference(5, 99);
        require(bodyOperands(surface) == QVector<int>({1, 4, 5}) && sketchesOf(surface) == QVector<int>({6, 7}),
                "multi-target e rete conservano ordine, deduplica e priorita' sul riferimento legacy");
        surface.surfaceTool.mode = 1;
        require(bodyOperands(surface) == QVector<int>({4, 5}), "rete non eredita la superficie sorgente");
        surface.surfaceTool.mode = 0;
        surface.surfaceTool.targets.clear();
        require(bodyOperands(surface) == QVector<int>({1, 5, 4, 99}), "riferimento legacy accodato solo senza target multipli");
        surface.extentRef = reference(7, 9);
        require(sketchesOf(surface) == QVector<int>({9, 7, 6}), "riferimento schizzo legacy precede le famiglie di curve");

        ExtrusionObject extraction;
        extraction.feature = BodyFeature::ExtractedCurve;
        extraction.curveExtraction.source = reference(9, 88);
        extraction.curveExtraction.sources = {reference(9, 3), reference(9, 3), reference(7, 4)};
        extraction.curveExtraction.tool = reference(9, 5);
        require(bodyOperands(extraction) == QVector<int>({3}) && sketchesOf(extraction) == QVector<int>({4}),
                "estrazione multipla ignora sorgente legacy e strumento inattivo");
        extraction.curveExtraction.trimmed = true;
        require(bodyOperands(extraction) == QVector<int>({3, 5}) && hiddenOperands(extraction) == QVector<int>({3}),
                "strumento di taglio dipendenza attiva ma non corpo sostituito");
        extraction.curveExtraction.sources.clear();
        require(bodyOperands(extraction) == QVector<int>({88, 5}) && hiddenOperands(extraction) == QVector<int>({88}),
                "estrazione legacy preservata se manca elenco multiplo");

        ExtrusionObject datum;
        datum.feature = BodyFeature::DatumPlane;
        datum.datum.refs = {reference(5, 3), reference(5, 3), reference(7, 4), reference(7, 4)};
        require(bodyOperands(datum) == QVector<int>({3, 3}) && sketchesOf(datum) == QVector<int>({4, 4}),
                "policy datum conserva riferimenti ripetuti, distinta dalle superfici");
        std::cout << "Dipendenze delle feature senza GUI: OK\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
