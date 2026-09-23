#ifndef FORGECAD_FK_PRECISION_H
#define FORGECAD_FK_PRECISION_H

// Tolleranze del kernel geometrico ForgeCAD.
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
constexpr double kMachineEpsilon = 2.220446049250313e-16;

inline bool insideSizeBox(double coordinate) {
    return coordinate >= -0.5 * kSizeBox && coordinate <= 0.5 * kSizeBox;
}

}

#endif
