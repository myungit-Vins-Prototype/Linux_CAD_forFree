#ifndef FORGECAD_EXPORT_H
#define FORGECAD_EXPORT_H

#include <QString>
#include <QVector>

#include "cad_types.h"

// Esportazione dei corpi verso altri CAD con gli scrittori del kernel
// (fk_step, fk_iges): STEP (AP203, AP214, AP242) e IGES 5.3, B-rep esatti in
// millimetri, un prodotto per corpo con il suo nome e il colore.
namespace ForgeCad {

enum class ExportFormat {
    StepAP203 = 0,   // configuration controlled 3D design (geometria, niente colori)
    StepAP214 = 1,   // automotive design (geometria, nomi, colori)
    StepAP242 = 2,   // managed model based 3D engineering (il piu' recente)
    IgesSolids = 3,  // IGES 5.3, solidi B-rep (MSBO, entita' 186)
    IgesSurfaces = 4 // IGES 5.3, superfici tagliate (entita' 144): la piu' compatibile
};

// Un corpo (solido o superficie) o una curva (elica, come curva limitata).
struct ExportBody {
    QString name;
    ForgeBody body;
    ForgeCurve curve;
};

// Restituisce l'errore (vuoto se riuscito).
QString exportBodies(const QString &path, const QVector<ExportBody> &bodies, ExportFormat format);

// Estensione del file per il formato ("step" o "igs").
QString exportSuffix(ExportFormat format);

}

#endif
