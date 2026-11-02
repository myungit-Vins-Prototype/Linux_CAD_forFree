#include "cad_projected_curve.h"
#include "cad_datum.h"
#include "cad_kernel.h"
#include "cad_forge.h"
#include "fk_curve_ops.h"
#include "fk_sketch_projection.h"

#include <exception>
#include <stdexcept>

namespace ForgeCad {

QVector<ForgeCurve> forgeSketchProjectedCurves(const ExtrusionObject &definition, int owner,
    const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies,
    QString *error, QString *summary) {
    if (error) error->clear();
    if (summary) summary->clear();
    try {
        int sourceSketch[2] = {-1, -1};
        std::vector<Kernel::SketchProjectionSpan> spans[2];
        const QVector<GeometryRef> *groups[] = {&definition.ruledFirst, &definition.ruledSecond};
        for (int group = 0; group < 2; ++group) {
            if (groups[group]->isEmpty()) throw std::domain_error("Scegli le curve del primo e del secondo schizzo.");
            QVector<GeometryRef> resolved;
            for (const auto &ref : *groups[group]) {
                if (ref.kind != 7 || ref.index < 0 || ref.index >= sketches.size())
                    throw std::domain_error("Ogni input deve essere uno schizzo esistente o una sua entita' curva.");
                if (sourceSketch[group] >= 0 && sourceSketch[group] != ref.index)
                    throw std::domain_error("Ogni gruppo deve contenere entita' di un solo schizzo.");
                for (const auto &old : resolved) if (old.index == ref.index
                    && (old.element.kind < 0 || ref.element.kind < 0 || old.element == ref.element))
                    throw std::domain_error("La stessa curva e' presente piu' volte nello stesso input della proiezione.");
                resolved.append(ref);
                sourceSketch[group] = ref.index;
                std::vector<Kernel::PathSegment> pieces;
                QString reason;
                if (ref.element.kind < 0) {
                    const auto &sketch = sketches.at(ref.index);
                    if (owner >= 0 && sketch.datumPlane >= owner)
                        throw std::domain_error("Lo schizzo e' appoggiato a un piano successivo alla curva proiettata.");
                    const auto frame = sketchAxes(sketch);
                    for (const auto &piece : forgeSketchSegments(sketch))
                        pieces.push_back({Kernel::embedCurve(piece.curve, frame), piece.range});
                } else if (!geometryRefPath(ref, owner, sketches, bodies, pieces, &reason))
                    throw std::domain_error(reason.toStdString());
                for (const auto &piece : pieces) spans[group].push_back({piece.curve, piece.range});
            }
        }
        if (sourceSketch[0] == sourceSketch[1]) throw std::domain_error("Scegli due schizzi distinti su piani non paralleli.");
        const auto curves = Kernel::projectSketchOnSketch(spans[0], sketchAxes(sketches.at(sourceSketch[0])).zDir(),
            spans[1], sketchAxes(sketches.at(sourceSketch[1])).zDir());
        QVector<ForgeCurve> result;
        bool implicit = false;
        for (const auto &curve : curves) {result.append(curve); implicit |= Kernel::isImplicitSketchProjection(*curve);}
        if (summary) {
            *summary = QStringLiteral("%1 curve 3D dall'intersezione delle proiezioni normali ai due piani.").arg(result.size());
            if (implicit) *summary += QStringLiteral(" Rappresentazione implicita esatta; sweep, conversione in schizzo ed esportazione STEP/IGES non disponibili per questi rami.");
        }
        return result;
    } catch (const std::exception &failure) {
        if (error) *error = QStringLiteral("Proiezione schizzo su schizzo non riuscita: %1").arg(QString::fromUtf8(failure.what()));
        return {};
    }
}

}
