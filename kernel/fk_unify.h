#include "fk_precision.h"
#ifndef FORGECAD_FK_UNIFY_H
#define FORGECAD_FK_UNIFY_H

#include "fk_topology.h"
#include <optional>

// Fusione delle entita' sulla stessa geometria, come
// ShapeUpgrade_UnifySameDomain di OCCT (l'app la applica dopo ogni booleana):
//  - facce adiacenti sulla stessa superficie (lo stesso oggetto, oppure piani
//    coincidenti con la stessa normale) diventano una faccia: gli edge tra di
//    loro spariscono e i loop si ricollegano girando attorno ai vertici;
//  - due edge che si incontrano in un vertice usato solo da loro e stanno
//    sulla stessa curva (lo stesso oggetto con i tratti contigui, rette
//    allineate, lo stesso cerchio) diventano un edge e il vertice sparisce.
// La geometria non cambia (volume, area, SP-curve delle fin che restano).
// Il risultato ha una sola region solida, come quelli delle booleane.
namespace ForgeCad::Kernel {

class BodyHistory;

// history facoltativa: provenienza dichiarata delle facce, compresi tutti
// i membri dei gruppi fusi e gli edge/vertici conservati o concatenati.
Body unifySameDomain(const Body &body, double tolerance = scaledAccuracy(1e-6), BodyHistory *history = nullptr);

// Supporti G1 composti da carte NURBS compatibili: concatenazione dei
// coefficienti, rimozione delle cuciture interne e dei vertici artificiali
// nei trim polinomiali tangenti. Conserva geometria e genealogia esplicita;
// restituisce nullopt quando non trova carte certificabili. E' separata da
// unifySameDomain: non cambia automaticamente la topologia delle booleane.
std::optional<Body> unifyTangentNurbsSupports(const Body &body, BodyHistory *history = nullptr);

}

#endif
