#ifndef FORGECAD_TEST_TANGENT_CONTACTS_H
#define FORGECAD_TEST_TANGENT_CONTACTS_H

#include "cad_constraints.h"
#include "cad_document_io.h"
#include "cad_history.h"
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <iostream>

// La tangente va verificata nelle direzioni geometriche anche quando la
// coincidenza fra gli estremi passa per uno o piu' punti di costruzione.
static void tangentContactTests() {
    using namespace ForgeCad;
    const bool wasSet = qEnvironmentVariableIsSet("FORGECAD_SOLVER_DENSE");
    const QByteArray saved = qgetenv("FORGECAD_SOLVER_DENSE");
    struct Restore { bool set; QByteArray value; ~Restore() {
        if (set) qputenv("FORGECAD_SOLVER_DENSE", value); else qunsetenv("FORGECAD_SOLVER_DENSE");
    }} restore{wasSet, saved};
    const auto fixture = [](double scale, double angle, int bridges, bool reversed) {
        const auto point = [&](double x, double y) {
            return scale * QPointF(12 + std::cos(angle) * x - std::sin(angle) * y,
                                   -8 + std::sin(angle) * x + std::cos(angle) * y);
        };
        SketchObject sketch;
        CurveObject arc; arc.tool = DrawingTool::Arc;
        arc.controlPoints = {point(0,0), point(10,0), point(0,10)};
        sketch.curves.append(arc);
        sketch.segments.append({point(0,10), point(-5,10-1e-6)});
        sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Fix, {{1,0,-1}}));
        ConstraintRef previous{1,0,2};
        const auto join = [&](ConstraintRef first, ConstraintRef second) {
            if (reversed) std::swap(first, second);
            sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Coincident, {first,second}));
        };
        for (int i = 0; i < bridges; ++i) {
            sketch.segments.append({point(0,10), point(1+i,13+i)});
            sketch.constructionSegments.append(i+1);
            join(previous, {0,i+1,0}); previous = {0,i+1,0};
        }
        join(previous, {0,0,0});
        auto dimension = makeConstraint(sketch, ConstraintType::Distance, {{0,0,-1}});
        dimension.value = 5 * scale; sketch.geometricConstraints.append(dimension);
        const QVector<ConstraintRef> entities = reversed ? QVector<ConstraintRef>{{1,0,-1},{0,0,-1}}
                                                       : QVector<ConstraintRef>{{0,0,-1},{1,0,-1}};
        sketch.geometricConstraints.append(makeConstraint(sketch, ConstraintType::Tangent, entities));
        if (reversed) std::reverse(sketch.geometricConstraints.begin(), sketch.geometricConstraints.end());
        sketch.constraints.fill(-1, sketch.segments.size());
        sketch.segmentLengths.fill(0.0, sketch.segments.size());
        sketch.segmentAngles.fill(-1.0, sketch.segments.size());
        return sketch;
    };
    const auto angularError = [](const SketchObject &sketch) {
        const auto radius = sketch.curves[0].controlPoints[2] - sketch.curves[0].controlPoints[0];
        const auto direction = sketch.segments[0].second - sketch.segments[0].first;
        return std::abs(QPointF::dotProduct(radius,direction))
            / (std::hypot(radius.x(),radius.y()) * std::hypot(direction.x(),direction.y()));
    };
    int cases = 0;
    for (int dense : {0,1}) for (double scale : {.1,1.,10.,100.})
        for (double angle : {0.,.7}) for (int bridges : {0,1,3}) for (bool reversed : {false,true}) {
            qputenv("FORGECAD_SOLVER_DENSE", QByteArray::number(dense));
            const auto source = fixture(scale, angle, bridges, reversed);
            auto solved = source;
            require(solveSketch(solved).ok, "tangenza transitiva: soluzione riuscita");
            require(angularError(solved) < 1e-10, "tangenza transitiva: direzioni realmente tangenti");
            require(QLineF(solved.segments[0].first, solved.curves[0].controlPoints[2]).length() < 1e-9 * scale,
                    "tangenza transitiva: contatto conservato");
            require(solved.curves[0].controlPoints == source.curves[0].controlPoints,
                    "tangenza transitiva: arco fisso invariato");
            for (const auto &c : solved.geometricConstraints)
                require(constraintError(solved,c) < 1e-9 * scale, "tangenza transitiva: tutti i vincoli rispettati");
            ++cases;
        }
    // Un punto vicino non autorizza a inventare una coincidenza.
    auto unrelated = fixture(1,0,1,false);
    for (qsizetype i = unrelated.geometricConstraints.size(); i-- > 0;)
        if (unrelated.geometricConstraints[i].type == ConstraintType::Coincident) unrelated.geometricConstraints.removeAt(i);
    require(constraintError(unrelated, unrelated.geometricConstraints.last()) < 1e-9,
            "tangenza libera: nessun contatto dedotto dalla sola vicinanza");
    // Entrambe le entita' fisse e realmente non tangenti: conflitto atomico.
    for (int dense : {0,1}) {
        qputenv("FORGECAD_SOLVER_DENSE", QByteArray::number(dense));
        auto conflict = fixture(1,0,3,false);
        conflict.geometricConstraints.append(makeConstraint(conflict, ConstraintType::Fix, {{0,0,-1}}));
        const auto before = conflict;
        require(!solveSketch(conflict).ok, "tangenza transitiva fissa incoerente: conflitto segnalato");
        require(conflict.segments == before.segments && conflict.curves[0].controlPoints == before.curves[0].controlPoints,
                "tangenza in conflitto: documento invariato");
    }
    DocumentState state, loaded; state.sketches = {fixture(1,.7,3,true)};
    QByteArray bytes;
    require(encodeDocument(state,bytes,false).isEmpty() && decodeDocument(bytes,loaded).isEmpty(),
            "tangenza transitiva: persistenza dei vincoli");
    History history; history.record(loaded);
    const auto reopenedResult = solveSketch(loaded.sketches[0]);
    require(reopenedResult.ok && angularError(loaded.sketches[0]) < 1e-10,
            "tangenza transitiva: risoluzione dopo riapertura");
    require(history.undo(loaded) && angularError(loaded.sketches[0]) > 1e-8,
            "tangenza transitiva: Undo ripristina il dato iniziale");
    require(history.redo(loaded) && angularError(loaded.sketches[0]) < 1e-10,
            "tangenza transitiva: Redo ripristina la soluzione");
    std::cout << "PASS contatti tangenti: " << cases << " casi, conflitti, vicinanza, codec e Undo/Redo" << std::endl;
}

#endif
