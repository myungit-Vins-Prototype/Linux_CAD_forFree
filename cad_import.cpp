#include "cad_import.h"

#include <QFile>
#include <QFileInfo>

#include <exception>
#include <memory>
#include <string>

#include "fk_iges.h"
#include "fk_step.h"
#include "fk_topology.h"

namespace ForgeCad {

QString importCadFile(const QString &path, QVector<ImportedPart> &parts, QStringList *notes) {
    parts.clear();
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool step = suffix == QLatin1String("step") || suffix == QLatin1String("stp");
    const bool iges = suffix == QLatin1String("iges") || suffix == QLatin1String("igs");
    if (!step && !iges) return QStringLiteral("Formato non riconosciuto (servono .step, .stp, .iges o .igs).");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Impossibile aprire %1.").arg(path);
    const QByteArray content = file.readAll();
    std::vector<Kernel::ExchangeBody> bodies;
    std::vector<std::string> readNotes;
    try {
        if (step) {
            Kernel::StepReadResult read = Kernel::readStep(content.toStdString());
            bodies = std::move(read.bodies);
            readNotes = std::move(read.notes);
        } else {
            Kernel::IgesReadResult read = Kernel::readIges(content.toStdString());
            bodies = std::move(read.bodies);
            readNotes = std::move(read.notes);
        }
    } catch (const std::exception &failure) {
        return QStringLiteral("Il file %1 non si legge: %2").arg(step ? QStringLiteral("STEP") : QStringLiteral("IGES"), QString::fromUtf8(failure.what()));
    }
    if (notes)
        for (const std::string &n : readNotes) notes->append(QString::fromStdString(n));
    const QString base = QFileInfo(path).completeBaseName();
    for (Kernel::ExchangeBody &b : bodies) {
        ImportedPart part;
        part.name = b.name.empty() ? base : QString::fromStdString(b.name);
        part.solid = !b.body.isSheet();
        try {
            Kernel::ExchangeBody stored;
            stored.name = b.name;
            stored.body = b.body;
            const std::string text = Kernel::writeStep({stored});
            part.data = QByteArray(text.data(), qsizetype(text.size()));
        } catch (const std::exception &failure) {
            if (notes) notes->append(QStringLiteral("%1: non si salva nel documento (%2)").arg(part.name, QString::fromUtf8(failure.what())));
            continue;
        }
        part.body = std::make_shared<const Kernel::Body>(std::move(b.body));
        parts.append(std::move(part));
    }
    if (parts.isEmpty()) {
        QString message = QStringLiteral("Il file non contiene solidi ne' superfici leggibili.");
        if (notes && !notes->isEmpty()) message += QStringLiteral("\n\n") + notes->join(QLatin1Char('\n'));
        return message;
    }
    return {};
}

}
