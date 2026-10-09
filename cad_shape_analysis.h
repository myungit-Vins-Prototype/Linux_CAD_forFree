#ifndef FORGECAD_SHAPE_ANALYSIS_H
#define FORGECAD_SHAPE_ANALYSIS_H
#include "cad_types.h"
#include "cad_overlay_renderer.h"
#include "fk_surface.h"
#include <QStringList>
namespace ForgeCad {
struct SurfaceCurvature {
    Kernel::Vec3 normal;
    double minimum = 0, maximum = 0;
    double tensor[3][3] = {};
};
// Forma di curvatura in coordinate mondo; false nei punti singolari.
bool surfaceCurvature(const Kernel::Surface &, double u, double v, SurfaceCurvature &);
bool curveCurvature(const Kernel::Curve<3> &, double t, Kernel::Vec3 &point, Kernel::Vec3 &curvature);
struct ShapeAnalysis {
    QVector<OverlayVertex> triangles, lines, grid;
    struct Border { QVector<QVector3D> points; QVector4D color; };
    QVector<Border> borders;
    QStringList report;
};
struct SurfaceCombOptions {
    int faceId = -1; // -1: tutte le facce del corpo
    int uLines = 6; // U costante (ciano), V variabile
    int vLines = 6; // V costante (magenta), U variabile
    int samples = 32; // densita indicativa per isoparametrica intera
    bool normalOnly = true;
};
// Vettore curvatura della isoparametrica, oppure sua componente normale alla superficie.
bool surfaceIsoCurvature(const Kernel::Surface &, double u, double v, bool varyU, bool normalOnly,
                         Kernel::Vec3 &point, Kernel::Vec3 &curvature);
BodyDisplay analysisFaceDisplay(const BodyDisplay &, int faceId);
ShapeAnalysis analyzeSurfaceComb(const ExtrusionObject &, double scale, const SurfaceCombOptions & = {});
// scale=0: scala automatica del pettine; altrimenti mm² (pettine) o mm⁻¹ (mappa).
// mode 1: mappa |k|max; 2: segno gaussiano; 3: giunzioni; 4: pettine.
ShapeAnalysis analyzeShape(const ExtrusionObject &, int mode, double scale, double angleTolerance, double curvatureTolerance, int faceId = -1);
}
#endif
