#ifndef FORGECAD_FK_LOFT_SECTIONS_H
#define FORGECAD_FK_LOFT_SECTIONS_H
#include "fk_loft.h"

namespace ForgeCad::Kernel::detail {
// Rappresentazione di costruzione della sola loft. Gli input e i riferimenti
// associativi restano immutati; le sezioni razionali non vengono approssimate.
// Per le intersezioni implicite usa il budget 1e-9 mm gia' usato per le guide.
constexpr double kLoftSectionFitTolerance = 1e-9;
std::vector<LoftSection> prepareLoftSections(const std::vector<LoftSection> &input);
std::vector<PathSegment> prepareLoftSectionPath(const std::vector<PathSegment> &input);
}
#endif
