#include "fk_precision.h"
#pragma once
#include "fk_history.h"
#include "fk_topology.h"
namespace ForgeCad::Kernel {
// Sviluppo isometrico di una faccia piana, cilindrica o conica. Le cuciture
// vengono aperte; per i coni i bordi devono essere isoparametrici esatti.
// Rifiuta carrier e trim non rappresentabili esattamente, senza fit.
Body unrollFace(const Body &source, FaceId face, const Frame3 &plane, BodyHistory *history = nullptr);
struct SurfaceWrapOptions {
    Frame3 sourcePlane;
    Vec2 anchor;                 // parametri sulla destinazione
    Vec2 shift;                  // spostamento sul piano sorgente, in mm
    double angle = 0.0;          // rotazione nel piano, radianti
    double normalOffset = 0.0;
    double tolerance = scaledAccuracy(1e-6);    // budget totale di fit, in mm
    bool rigid = false;         // sola rototraslazione per ogni corpo sorgente
    bool quilt = false;         // centraggio nella corrispondenza piano/quilt
};
// Deforma lungo S(u0+x/|Su0|,v0+y/|Sv0|)+z N. Mantiene la topologia e
// dichiara la genealogia; materiali, bordi e SP-curve restano B-rep double.
// Fit NURBS verificato; rifiuta domini singolari, inversioni locali e budget
// non raggiunti. La parametrizzazione puo' introdurre stiramento tangenziale.
Body wrapBodyOnSurface(const Body &source, const Surface &target, const SurfaceWrapOptions &options,
                       BodyHistory *history = nullptr, double *deviation = nullptr);
struct SurfaceTargetFace {
    const Body *body = nullptr;
    FaceId face;
};
// Centro dell'appoggio sulla giunzione topologica comune alla prima faccia
// e a un'altra selezionata; usa l'ingombro proiettato delle facce esatte.
// false se non esiste una giunzione condivisa; non modifica la selezione.
bool commonSurfaceWrapAnchor(const std::vector<SurfaceTargetFace> &targets,
                             const Frame3 &sourcePlane, Vec2 &anchor);
// Cerca una piccola traslazione della disposizione che porti tutti i punti
// sorgenti nell'appoggio rifilato. Non scala i corpi. Ricerca limitata al
// raggio dato; false lascia le opzioni invariate, non certifica impossibilita'.
bool fitSurfaceWrapPlacement(const std::vector<Vec2> &points,
                             const std::vector<SurfaceTargetFace> &targets,
                             SurfaceWrapOptions &options, double radius);
// Appoggio unico su facce rifilate: proiezione lungo la normale all'appoggio
// della prima faccia, offset lungo le normali orientate locali. Nessuna copia
// per destinazione. Rifiuta vuoti, ambiguita', pieghe e fit oltre il budget.
Body wrapBodyOnFaces(const Body &source, const std::vector<SurfaceTargetFace> &targets,
                     const SurfaceWrapOptions &options, BodyHistory *history = nullptr,
                     double *deviation = nullptr);

}
