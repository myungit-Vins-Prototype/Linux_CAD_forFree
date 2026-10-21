#ifndef FORGECAD_EXPORT_H
#define FORGECAD_EXPORT_H

#include <QString>
#include <QVector>
#include <QByteArray>

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
    QColor color;
};

// Restituisce l'errore (vuoto se riuscito).
QString exportBodies(const QString &path, const QVector<ExportBody> &bodies, ExportFormat format);

// Estensione del file per il formato ("step" o "igs").
QString exportSuffix(ExportFormat format);

// Parametri indipendenti della mesh STL. `maxEdgeLength` regola la dimensione
// e quindi la quantita' dei triangoli anche sui piani; `deflection` e `angle`
// regolano l'approssimazione di raggi e superfici curve.
struct StlExportOptions {
    double maxEdgeLength = 1.0; // mm; 0 = nessun limite esplicito
    double deflection = 0.05;   // scarto cordale massimo, mm
    double angle = 10.0;        // variazione massima delle normali, gradi
    // Ricerca un raffinamento completo e sostenibile, senza esportare facce
    // mancanti. False conserva esattamente i parametri manuali.
    bool automaticRefinement = false;
};

struct StlBuildResult {
    QByteArray data;            // STL binario pronto da salvare
    quint64 triangleCount = 0;
    BodyDisplay preview;        // stessa mesh, alleggerita solo per la vista
    bool previewLimited = false;
    StlExportOptions usedOptions; // parametri della mesh effettivamente esportata
    int tessellationAttempts = 0;
    QString error;
};

struct ObjBuildResult {
    QByteArray data;
    quint64 quadCount = 0;
    quint64 triangleCount = 0; // triangoli non accoppiabili (poli, fori, transizioni)
    BodyDisplay preview;       // facce triangolate e griglia quad-dominant
    bool previewLimited = false;
    StlExportOptions usedOptions; // parametri della mesh effettivamente esportata
    int tessellationAttempts = 0;
    QString error;
};

// Costruisce una sola mesh STL binaria con tutti i corpi. Le curve isolate
// non sono rappresentabili in STL e vengono ignorate.
StlBuildResult buildBinaryStl(const QVector<ExportBody> &bodies, const StlExportOptions &options);
QString saveBinaryStl(const QString &path, const QByteArray &data);

// OBJ con normali e topologia prevalentemente quadrangolare. Ogni quad nasce
// da due triangoli adiacenti della stessa faccia parametrica; dove non e'
// geometricamente valido il triangolo viene conservato.
ObjBuildResult buildQuadObj(const QVector<ExportBody> &bodies, const StlExportOptions &options);
QString saveQuadObj(const QString &path, const QByteArray &data);

}

#endif
