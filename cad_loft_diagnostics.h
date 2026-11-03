#ifndef FORGECAD_LOFT_DIAGNOSTICS_H
#define FORGECAD_LOFT_DIAGNOSTICS_H
#include "cad_types.h"
#include "fk_sweep.h"
namespace ForgeCad {
struct LoftFailure {
    int guide = -1, section = -1; // ordine effettivo del kernel, indici da zero
};
struct LoftDiagnosticPath {
    bool guide = false;
    QString label;
    std::vector<Kernel::PathSegment> path;
};
LoftFailure loftFailure(const QString &error);
QString visibleLoftError(QString error);
QString namedLoftError(const QString &error, const ExtrusionObject &definition,
                       const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);
QVector<LoftDiagnosticPath> loftDiagnosticPaths(LoftFailure failure, const ExtrusionObject &definition, int owner,
                                              const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies);
}
#endif
