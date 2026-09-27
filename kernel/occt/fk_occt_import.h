#ifndef FORGECAD_FK_OCCT_IMPORT_H
#define FORGECAD_FK_OCCT_IMPORT_H

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "fk_topology.h"

// Ponte da OpenCASCADE al kernel proprio (libreria forgekernel_occt: il kernel
// resta senza OCCT). Serve ai corpi importati da STEP/IGES quando il kernel
// attivo e' quello di ForgeCAD.
//
// Stessa geometria, senza approssimazioni dove i tipi esistono nei due kernel
// (le parametrizzazioni del kernel proprio sono quelle di OCCT):
//  - curve: rette, cerchi, ellissi, B-spline e Bezier (anche razionali;
//    periodiche rese non periodiche, esatto); le altre (parabole, iperboli,
//    offset) come B-spline di GeomConvert;
//  - superfici: piani, cilindri, coni, sfere, tori (un gp_Ax3 indiretto diventa
//    diretto con u -> -u e la faccia rovesciata), estrusioni, rivoluzioni,
//    B-spline e Bezier; le offset come la superficie equivalente o, se non
//    c'e', approssimate (nota nel rapporto);
//  - topologia: una faccia per faccia OCCT, gli edge condivisi restano
//    condivisi. Le cuciture delle superfici periodiche nel kernel proprio
//    spariscono (le facce periodiche non hanno cucitura: i loop si dividono
//    dove passava), gli edge degeneri nei poli di sfere, coni e rivoluzioni
//    pure (il kernel li gestisce con il cammino lungo il polo). Sulle
//    B-spline le SP-curve sono quelle di OCCT (anche per le cuciture, che
//    restano); altrove si calcolano (computePCurves).
// Il risultato passa checkBody. Eccezione std::domain_error (con il motivo)
// se qualcosa non si puo' rappresentare: edge degeneri su superfici B-spline,
// edge usati da piu' di due facce, facce a cavallo della cucitura di una
// B-spline periodica.
namespace ForgeCad::Kernel {

struct OcctImportReport {
    int faces = 0, edges = 0, vertices = 0;
    int droppedSeams = 0, droppedDegenerated = 0;  // fin tolte (una cucitura ne ha due)
    std::vector<std::string> notes;  // approssimazioni fatte
};

// I solidi della forma (piu' solidi: un body con piu' shell) o, se non ce ne
// sono, le sue facce come lamina (Body::buildSheet).
Body bodyFromOcct(const TopoDS_Shape &shape, OcctImportReport *report = nullptr);

}

#endif
