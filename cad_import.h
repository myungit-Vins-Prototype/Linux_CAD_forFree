#ifndef FORGECAD_IMPORT_H
#define FORGECAD_IMPORT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cad_types.h"

// Importazione da altri CAD (File -> Importa): STEP (AP203/AP214/AP242) e
// IGES con i lettori del kernel (fk_step, fk_iges), geometria esatta (niente mesh).
//  - I nomi dei prodotti (e dei componenti degli assiemi) vengono dal file;
//    le posizioni dei componenti si applicano ai body; unita' convertite in mm.
//  - Un corpo per body letto: solidi, o superfici (lamine) se le facce non
//    chiudono un volume (le superfici IGES sciolte si cuciono nel lettore).
// Nel documento il body si salva come testo STEP del kernel (numeri a 17
// cifre, la stessa geometria) e si rilegge a ogni rigenerazione (cad_forge forgeImported).
namespace ForgeCad {

struct ImportedPart {
    QString name;
    ForgeBody body;
    bool solid = false;
    QByteArray data;  // ExtrusionObject::importData
};

// Legge il file (formato dal suffisso: .step/.stp o .iges/.igs). Restituisce
// l'errore (vuoto se riesce); `notes` riceve gli avvisi del lettore (correzioni, entita' saltate).
QString importCadFile(const QString &path, QVector<ImportedPart> &parts, QStringList *notes = nullptr);

}

#endif
