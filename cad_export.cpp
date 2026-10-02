#include "cad_export.h"

#include <QFileInfo>
#include <QSaveFile>

#include <exception>
#include <string>
#include <vector>

#include "fk_curve.h"
#include "fk_iges.h"
#include "fk_step.h"
#include "fk_topology.h"

namespace ForgeCad {

QString exportSuffix(ExportFormat format) {
    return format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces ? QStringLiteral("igs") : QStringLiteral("step");
}

QString exportBodies(const QString &path, const QVector<ExportBody> &bodies, ExportFormat format) {
    const bool iges = format == ExportFormat::IgesSolids || format == ExportFormat::IgesSurfaces;
    std::vector<Kernel::ExchangeBody> exchange;
    for (const ExportBody &body : bodies) {
        if (!body.body && !body.curve) continue;
        Kernel::ExchangeBody e;
        // IGES e le stringhe STEP senza codifica sono ASCII: lettere accentate
        // senza accento (è -> e), il resto '_' (fk_step codifica comunque il resto).
        QString name = body.name;
        if (iges) {
            QString ascii;
            for (const QChar c : name.normalized(QString::NormalizationForm_D))
                if (c.unicode() < 128) ascii += c;
                else if (c.category() != QChar::Mark_NonSpacing) ascii += QLatin1Char('_');
            name = ascii;
        }
        e.name = name.toStdString();
        if (body.body) e.body = *body.body;
        e.curve = body.curve;
        if (body.curve) e.curveRange = body.curve->domain();
        e.hasColor = format != ExportFormat::StepAP203;
        const QColor color = body.color.isValid() ? body.color : QColor::fromRgbF(0.25, 0.65, 0.90);
        e.color[0] = color.redF(), e.color[1] = color.greenF(), e.color[2] = color.blueF();
        exchange.push_back(std::move(e));
    }
    if (exchange.empty()) return QStringLiteral("Non ci sono corpi da esportare.");
    std::string content;
    try {
        const std::string fileName = QFileInfo(path).fileName().toStdString();
        if (iges) {
            Kernel::IgesWriteOptions options;
            options.mode = format == ExportFormat::IgesSolids ? Kernel::IgesMode::Solids : Kernel::IgesMode::Surfaces;
            options.fileName = fileName;
            content = Kernel::writeIges(exchange, options);
        } else {
            Kernel::StepWriteOptions options;
            options.schema = format == ExportFormat::StepAP203 ? Kernel::StepSchema::AP203
                             : format == ExportFormat::StepAP214 ? Kernel::StepSchema::AP214
                                                                 : Kernel::StepSchema::AP242;
            options.fileName = fileName;
            content = Kernel::writeStep(exchange, options);
        }
    } catch (const std::exception &failure) {
        return QStringLiteral("Esportazione non riuscita: %1").arg(QString::fromUtf8(failure.what()));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content.data(), qint64(content.size())) != qint64(content.size()) || !file.commit())
        return QStringLiteral("Impossibile scrivere %1.").arg(path);
    return {};
}

}
