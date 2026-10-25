#include "forgeCad2026_gui_common.h"

#include <QGuiApplication>
#include <QLabel>
#include <QSizePolicy>
#include <cmath>

namespace ForgeCad::Gui {
// Font delle scritte disegnate a mano (etichette, quote, pannelli): quello
// di sistema dell'applicazione con la dimensione e il peso chiesti. Una
// famiglia per nome ("Sans") che non esiste su macOS fa costruire a Qt la
// tabella degli alias dei font (decine di millisecondi e un avviso).
QFont uiFont(int pointSize, QFont::Weight weight) {
    QFont font = QGuiApplication::font();
    font.setPointSize(pointSize);
    font.setWeight(weight);
    return font;
}

double pointLength(const QPointF &point) { return std::hypot(point.x(), point.y()); }
double pointDistance(const QPointF &a, const QPointF &b) { return pointLength(b - a); }

// La stessa icona identifica una feature nell'albero e nella voce contestuale
// che ne apre i parametri.
QString featureIconName(const ExtrusionObject &body) {
    if (body.operation >= 0)
        return QStringList{QStringLiteral("union"), QStringLiteral("intersection"), QStringLiteral("difference")}.value(body.operation);
    if (body.feature == BodyFeature::Extrusion) return QStringLiteral("extrude");
    if (body.feature == BodyFeature::Revolution) return QStringLiteral("revolve");
    if (body.feature == BodyFeature::Blend) return body.blendChamfer ? QStringLiteral("chamfer") : QStringLiteral("fillet");
    if (body.feature == BodyFeature::SheetTrim) return QStringLiteral("trimSurface");
    if (body.feature == BodyFeature::SurfaceTools) return body.surfaceTool.mode==0?QStringLiteral("surfaceExtendTo"):body.surfaceTool.mode==1?QStringLiteral("surfaceNetwork"):QStringLiteral("surfacePoles");
    if (body.feature == BodyFeature::SheetExtend) return QStringLiteral("extendSurface");
    if (body.feature == BodyFeature::Scale) return QStringLiteral("scale");
    if (body.feature == BodyFeature::Helix) return QStringLiteral("helix");
    if (body.feature == BodyFeature::Sweep) return body.sweepSurface ? QStringLiteral("sweepSurface") : QStringLiteral("sweep");
    if (body.feature == BodyFeature::Loft) return body.loftSurface ? QStringLiteral("loftSurface") : QStringLiteral("loft");
    if (body.feature == BodyFeature::Ruled) return QStringLiteral("ruledSurface");
    if (body.feature == BodyFeature::PlanarSurface) return QStringLiteral("planarSurface");
    if (body.feature == BodyFeature::DatumPlane) return QStringLiteral("datumPlane");
    if (body.feature == BodyFeature::Imported) return QStringLiteral("import");
    if (body.feature == BodyFeature::Transform) return QStringLiteral("move");
    if (body.feature == BodyFeature::Pattern) return QStringLiteral("patternLinear");
    if (body.feature == BodyFeature::SurfaceOffset) return body.distance == 0.0 ? QStringLiteral("copyFaces") : QStringLiteral("offsetSurface");
    if (body.feature == BodyFeature::DeleteFace) return QStringLiteral("deleteFace");
    if (body.feature == BodyFeature::Draft) return QStringLiteral("draft");
    if (body.feature == BodyFeature::SpatialSketch) return QStringLiteral("newSketch3D");
    if (body.feature == BodyFeature::ExtractedCurve) return body.curveExtraction.trimmed ? QStringLiteral("trimEdge3D") : QStringLiteral("extractEdge3D");
    if (body.feature == BodyFeature::ProjectedCurve) return QStringLiteral("projectedCurve");
    if (body.feature == BodyFeature::Thicken) return QStringLiteral("thicken");
    if (body.feature == BodyFeature::Shell) return QStringLiteral("shell");
    if (body.feature == BodyFeature::Thread) return QStringLiteral("thread");
    if (body.feature == BodyFeature::BoundarySurface) return QStringLiteral("boundarySurface");
    if (body.feature == BodyFeature::FillSurface) return QStringLiteral("fillSurface");
    if (body.feature == BodyFeature::Sew) return QStringLiteral("sewSurfaces");
    if (body.feature == BodyFeature::Primitive)
        return QStringList{QStringLiteral("box"), QStringLiteral("cylinder"), QStringLiteral("sphere"),
                           QStringLiteral("cone"), QStringLiteral("torus")}.value(int(body.primitive.kind));
    return {};
}

// Testo descrittivo delle finestre: va a capo e non allarga la finestra (le
// finestre delle funzioni restano strette, senza scorrimento orizzontale).
QLabel *wrappedNote(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    // Non sostituire la policy con una costruita senza HeightForWidth: in quel
    // caso QFormLayout riserva l'altezza di una sola riga e il testo va sotto
    // ai controlli successivi. I pannelli piu' larghi possono usare piu'
    // spazio, quelli stretti aumentano correttamente l'altezza della nota.
    label->setMinimumWidth(0);
    label->setMaximumWidth(560);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);
    label->setSizePolicy(policy);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}


}
