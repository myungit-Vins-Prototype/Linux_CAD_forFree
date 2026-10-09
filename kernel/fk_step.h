#ifndef FORGECAD_FK_STEP_H
#define FORGECAD_FK_STEP_H

#include <string>
#include <vector>

#include "fk_topology.h"

// STEP (ISO 10303-21) per i body del kernel, senza librerie esterne.
//
// Scrittura (writeStep): B-rep avanzato (AP203, AP214, AP242), un prodotto per
// body con nome e colore, unita' mm e radianti. Le superfici e le curve con la
// stessa definizione in STEP (piani, cilindri, coni, sfere, tori, estrusioni,
// rivoluzioni, rette, cerchi, ellissi, B-spline anche razionali) si scrivono
// come sono; le curve trasformate come cerchi o ellissi (similitudini) o
// B-spline esatte, le altre (eliche) come B-spline entro 1e-9. Le facce
// periodiche non hanno cuciture (come in Parasolid); le facce senza bordo
// (sfera, toro interi) hanno un VERTEX_LOOP.
//
// Lettura (readStep): i solidi (MANIFOLD_SOLID_BREP, BREP_WITH_VOIDS,
// FACETED_BREP) e le superfici (SHELL_BASED_SURFACE_MODEL) di tutti i prodotti,
// con i nomi, i colori e le posizioni degli assiemi (NEXT_ASSEMBLY_USAGE_OCCURRENCE,
// MAPPED_ITEM), nelle unita' del file convertite in mm. Le curve degli edge
// hanno il tratto dalle proiezioni dei vertici; le cuciture delle superfici
// periodiche si tolgono (i loop si dividono), le SP-curve si calcolano. Ogni
// body passa checkBody (gli edge prendono la tolleranza misurata sulle facce,
// come in Parasolid).
namespace ForgeCad::Kernel {

struct ExchangeBody {
    std::string name;
    Body body;
    bool hasColor = false;
    double color[3] = {0.0, 0.0, 0.0};  // RGB in [0, 1]
    // Solo in scrittura: una curva al posto del body (eliche), sul tratto
    // `curveRange`; in STEP un wireframe (GEOMETRIC_CURVE_SET con la curva
    // limitata ai suoi estremi), in IGES un'entita' 126 (o 110).
    CurvePtr<3> curve;
    Interval curveRange;
};

enum class StepSchema { AP203, AP214, AP242 };

struct StepWriteOptions {
    StepSchema schema = StepSchema::AP242;
    std::string fileName = "model";
    std::string author = "ForgeCAD";
    std::string timeStamp;  // ISO 8601; vuoto: ora corrente
};

// Il testo del file STEP (eccezione std::domain_error se un body non si puo' scrivere).
std::string writeStep(const std::vector<ExchangeBody> &bodies, const StepWriteOptions &options = {});

struct StepReadResult {
    std::vector<ExchangeBody> bodies;
    std::vector<std::string> notes;  // entita' saltate, corpi non ricostruiti
};

// Il contenuto di un file STEP (eccezione std::domain_error se il file non si legge).
struct StepReadOptions {
    // 0: automatico (al massimo 8 worker); 1: corpi in sequenza.
    int threads = 0;
};

StepReadResult readStep(const std::string &content, const StepReadOptions &options = {});

}

#endif
