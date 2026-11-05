#pragma once
#include "cad_types.h"
#include "fk_loft.h"
namespace ForgeCad {
// La stessa risoluzione esatta per costruzione e controlli interattivi.
std::vector<Kernel::LoftSection> resolveLoftSections(const ExtrusionObject &definition, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);
void applyLoftCorrespondences(const QVector<LoftSectionCorrespondence> &controls,
    const std::vector<Kernel::LoftSection> &sections, Kernel::LoftOptions &options);
}
