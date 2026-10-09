#ifndef FORGECAD_FK_IGES_H
#define FORGECAD_FK_IGES_H

#include <string>
#include <vector>

#include "fk_step.h"

// IGES 5.3 per i body del kernel, senza librerie esterne.
//
// Scrittura (writeIges): solidi come B-rep (MSBO 186 con shell 514, facce 510,
// loop 508, liste di edge 504 e di vertici 502) o tutto come superfici
// limitate (144 con i bordi 142, curve nello spazio del modello). Superfici:
// piani, cilindri, coni, sfere e tori analitici (190-198), le altre B-spline
// razionali esatte (128); curve: segmenti (110) e B-spline razionali esatte
// (126; le eliche entro 1e-9). Nome (406 forma 15) e colore (314) per body; mm.
//
// Lettura (readIges): MSBO (186) e superfici limitate (144, 143) con le loro
// curve (100, 102, 104, 106, 110, 112, 126) e superfici (108, 114, 118, 120,
// 122, 128, 190-198), le matrici 124, nelle unita' del file convertite in mm.
// Le superfici limitate si cuciono (vertici ed edge comuni entro la tolleranza
// del file); le shell chiuse diventano solidi, le altre lamine.
namespace ForgeCad::Kernel {

enum class IgesMode { Solids, Surfaces };

struct IgesWriteOptions {
    IgesMode mode = IgesMode::Solids;
    std::string fileName = "model";
    std::string author = "ForgeCAD";
    std::string timeStamp;  // 20260927.120000; vuoto: ora corrente
};

std::string writeIges(const std::vector<ExchangeBody> &bodies, const IgesWriteOptions &options = {});

struct IgesReadResult {
    std::vector<ExchangeBody> bodies;
    std::vector<std::string> notes;
};

struct IgesReadOptions {
    // 0: automatico (al massimo 8 worker); 1: corpi in sequenza.
    int threads = 0;
};

IgesReadResult readIges(const std::string &content, const IgesReadOptions &options = {});

}

#endif
