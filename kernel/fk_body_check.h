#ifndef FORGECAD_FK_BODY_CHECK_H
#define FORGECAD_FK_BODY_CHECK_H

#include <string>
#include <vector>

#include "fk_topology.h"

// Controllo di validita' di un body (come PK_BODY_check di Parasolid o
// BRepCheck_Analyzer di OCCT). Va eseguito dopo ogni operazione di
// modellazione: un body che non lo supera non deve arrivare al documento.
namespace ForgeCad::Kernel {

enum class CheckCode {
    // Topologia
    BadReference,      // riferimento a un'entita' inesistente o cancellata
    BrokenLoop,        // ciclo next/previous incoerente o fin fuori dal suo loop
    BadEdge,           // fin dell'edge incoerenti (verso, vertici agli estremi)
    BadOwnership,      // loop/faccia/shell non registrati nel contenitore
    DanglingVertex,    // vertice non usato da nessuna fin o loop
    EulerViolation,    // V - E + F - R = 2 (S - G) non ha soluzione con G >= 0
    // Geometria
    MissingGeometry,   // faccia senza superficie o edge senza curva
    BadRange,          // range dell'edge vuoto o non finito
    VertexOffEdge,     // estremo della curva lontano dal vertice
    EdgeOffFace,       // curva dell'edge lontana dalla superficie di una faccia adiacente
    LoopOrientation,   // loop di una faccia piana orientato al contrario
    PCurveOffEdge,     // SP-curve di una fin che non ricade sulla curva dell'edge
};

struct CheckIssue {
    CheckCode code;
    std::string message;
};

struct CheckOptions {
    bool geometry = true;  // anche i controlli geometrici (richiedono curve e superfici)
};

std::vector<CheckIssue> checkBody(const Body &body, const CheckOptions &options = {});

// Genere di una shell chiusa dalla formula di Eulero-Poincare':
// V - E + F - R = 2 (1 - G), con R = anelli (loop oltre al primo di ogni faccia).
// -1 se la formula non ha soluzione intera non negativa.
int shellGenus(const Body &body, ShellId shell);

std::string describe(CheckCode code);

}

#endif
