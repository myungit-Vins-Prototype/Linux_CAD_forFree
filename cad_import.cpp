#include "cad_import.h"

#include <QFile>
#include <QDateTime>
#include <QFileInfo>

#include <exception>
#include <memory>
#include <string>

#include "fk_iges.h"
#include "fk_parallel.h"
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
    // Un risultato per indice: niente modifiche concorrenti ai contenitori Qt.
    std::vector<ImportedPart> converted(bodies.size());
    std::vector<QString> failures(bodies.size());
    std::vector<std::exception_ptr> unexpected(bodies.size());
    Kernel::StepWriteOptions storageOptions;
    // writeStep usa localtime per il valore predefinito: calcolalo una volta
    // fuori dai worker (localtime puo' usare memoria statica condivisa).
    storageOptions.timeStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")).toStdString();
    Kernel::parallelFor(bodies.size(), bodies.size() >= 4 ? std::min(8u, Kernel::threadCount(0)) : 1u, [&](std::size_t i) {
        try {
            Kernel::ExchangeBody &b = bodies[i];
            ImportedPart &part = converted[i];
            part.name = b.name.empty() ? base : QString::fromStdString(b.name);
            part.solid = !b.body.isSheet();
            try {
                Kernel::ExchangeBody stored;
                stored.name = b.name;
                stored.body = b.body;
                const std::string text = Kernel::writeStep({stored}, storageOptions);
                part.data = QByteArray(text.data(), qsizetype(text.size()));
            } catch (const std::exception &failure) {
                failures[i] = QStringLiteral("%1: non si salva nel documento (%2)").arg(part.name, QString::fromUtf8(failure.what()));
                return;
            }
            part.body = std::make_shared<const Kernel::Body>(std::move(b.body));
        } catch (...) { unexpected[i] = std::current_exception(); }
    });
    for (std::size_t i = 0; i < converted.size(); ++i) {
        if (unexpected[i]) std::rethrow_exception(unexpected[i]);
        if (!failures[i].isEmpty()) {
            if (notes) notes->append(failures[i]);
        } else parts.append(std::move(converted[i]));
    }
    if (parts.isEmpty()) {
        QString message = QStringLiteral("Il file non contiene solidi ne' superfici leggibili.");
        if (notes && !notes->isEmpty()) message += QStringLiteral("\n\n") + notes->join(QLatin1Char('\n'));
        return message;
    }
    return {};
}

}
