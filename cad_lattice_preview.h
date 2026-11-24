#ifndef FORGECAD_LATTICE_PREVIEW_H
#define FORGECAD_LATTICE_PREVIEW_H

#include "cad_volume_structure.h"

namespace ForgeCad::Gui {

struct LatticePreviewMeshes {
    BodyDisplay lattice;
    BodyDisplay retained;
    bool simplified = false;
};

// Approssimazione a celle per la sola visualizzazione. Il solido viene
// costruito a parte con il classificatore B-rep.
LatticePreviewMeshes latticePreviewMeshes(const BodyDisplay &source, const PreparedVolumeStructure &structure, int axis);

// Suddivide la tassellazione del risultato verificato per colorare la zona.
LatticePreviewMeshes latticeResultMeshes(BodyDisplay result, const Kernel::Box &zone, int axis);

}

#endif
