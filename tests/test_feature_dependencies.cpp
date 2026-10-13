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
        std::cout << "Dipendenze delle feature senza GUI: OK\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
