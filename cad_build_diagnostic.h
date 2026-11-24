#ifndef FORGECAD_BUILD_DIAGNOSTIC_H
#define FORGECAD_BUILD_DIAGNOSTIC_H

#include <QString>
#include <QVector>
#include <QVector3D>

struct ExtrusionObject;
struct SketchObject;

namespace ForgeCad::Kernel {
template <int N> class Curve;
struct Interval;
}

namespace ForgeCad {

// Geometria di visualizzazione in coordinate mondo. Una polilinea di un solo
// punto indica un punto. La diagnosi vive solo per la durata del risultato.
struct BuildDiagnostic {
    QVector<QVector<QVector3D>> paths;
    QString context;
};

void appendDiagnosticCurve(BuildDiagnostic &diagnostic, const Kernel::Curve<3> &curve, const Kernel::Interval &range);
BuildDiagnostic sketchReferenceDiagnostic(const SketchObject &sketch);
BuildDiagnostic featureDiagnosticContext(const ExtrusionObject &feature, const QVector<SketchObject> &sketches,
                                         const QVector<ExtrusionObject> &bodies);

} // namespace ForgeCad
#endif
