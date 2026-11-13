#ifndef FORGECAD2026_FEATURE_PREVIEW_H
#define FORGECAD2026_FEATURE_PREVIEW_H
#include "cad_types.h"
namespace ForgeCad::Gui {
// Calcolo su snapshot immutabili: nessun widget, documento o callback GUI.
class FeaturePreviewBuilder {
public:
    struct PreviewInputs {
        ExtrusionObject definition;
        int index = 0;  // posizione del corpo (gli operandi hanno indice minore)
        QVector<SketchObject> sketches;
        QVector<ExtrusionObject> bodies;  // solo gli operandi, copiati
        int quality = 1;
        double calculationAccuracy = 1.0e-7;
        // Modifica invariata: risultati della scena, comprese tutte le
        // componenti esposte della ripetizione. Il worker prepara soltanto
        // le visualizzazioni mancanti; non ricostruisce la geometria.
        bool reuse = false;
        QVector<ExtrusionObject> existingResults;
    };
    static QString previewKey(const ExtrusionObject &d, int index);
    static bool localMergePreview(const ExtrusionObject &body);
    static bool computePreview(const PreviewInputs &in, BodyDisplay &display, BodyDisplay &retainedDisplay,
                               BodyDisplay &resultDisplay, ForgeBody &geometry, QString &error,
                               QVector<int> *merged = nullptr, QString *notice = nullptr,
                               QVector<BodyDisplay> *additionalDisplays = nullptr);
};
}
#endif
