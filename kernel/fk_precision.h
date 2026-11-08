#ifndef FORGECAD_FK_PRECISION_H
#define FORGECAD_FK_PRECISION_H

#include <cmath>
#include <stdexcept>

// Tolleranze del kernel geometrico ForgeCAD.
// Politica e criteri di modifica: docs/tolerance-policy.md.
//
// Come in Parasolid, il modello vive in una "size box": un cubo centrato
// nell'origine di lato kSizeBox. Con le coordinate limitate, una tolleranza
// lineare assoluta resta sempre molto sopra l'errore di arrotondamento dei
// double: 5e5 * 2.2e-16 ~ 1.1e-10, circa mille volte sotto kLinearResolution.
// Unita' del modello: millimetri.
namespace ForgeCad::Kernel {

constexpr double kSizeBox = 1.0e6;              // lato del cubo: 1 km
constexpr double kLinearResolution = 1.0e-7;    // come Precision::Confusion() di OCCT
constexpr double kAngularResolution = 1.0e-11;  // come Parasolid (OCCT usa 1e-12)
// Budget distinti: non sono sinonimi della risoluzione geometrica.
constexpr double kBooleanConnectionTolerance = 1.0e-6;  // mm, fusione vertici
constexpr double kMarchingConnectionTolerance = 1.0e-6; // mm, punti tracciati
constexpr double kMarchingFitTolerance = 1.0e-9;        // mm, approssimazione
constexpr double kPCurveTolerance = kLinearResolution; // mm, fit SP-curve
constexpr double kMachineEpsilon = 2.220446049250313e-16;

// Profilo del documento: i default storici restano costanti per compatibilita'.
// I budget di calcolo mantengono i propri rapporti e le proprie unita'.
// Coordinate, parametri e arrotondamento del double non vengono riscalati.
constexpr double kMinimumCalculationAccuracy = 1.0e-10;
inline bool validCalculationAccuracy(double accuracy) {
    return std::isfinite(accuracy) && accuracy >= kMinimumCalculationAccuracy && accuracy <= kLinearResolution;
}
inline thread_local double currentCalculationAccuracy = kLinearResolution;
inline double accuracyScale() { return currentCalculationAccuracy / kLinearResolution; }
inline double scaledAccuracy(double standard) { return standard * accuracyScale(); }
inline double linearResolution() { return currentCalculationAccuracy; }
inline double angularResolution() { return scaledAccuracy(kAngularResolution); }
inline double booleanConnectionTolerance() { return scaledAccuracy(kBooleanConnectionTolerance); }
inline double marchingConnectionTolerance() { return scaledAccuracy(kMarchingConnectionTolerance); }
inline double marchingFitTolerance() { return scaledAccuracy(kMarchingFitTolerance); }
inline double pCurveTolerance() { return scaledAccuracy(kPCurveTolerance); }

class PrecisionScope {
public:
    explicit PrecisionScope(double accuracy) : previous_(currentCalculationAccuracy) {
        if (!validCalculationAccuracy(accuracy)) throw std::invalid_argument("precisione di calcolo non valida");
        currentCalculationAccuracy = accuracy;
    }
    ~PrecisionScope() { currentCalculationAccuracy = previous_; }
    PrecisionScope(const PrecisionScope &) = delete;
    PrecisionScope &operator=(const PrecisionScope &) = delete;
private:
    double previous_;
};

inline bool insideSizeBox(double coordinate) {
    return coordinate >= -0.5 * kSizeBox && coordinate <= 0.5 * kSizeBox;
}

}

#endif
