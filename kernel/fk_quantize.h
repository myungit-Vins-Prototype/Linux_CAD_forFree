#ifndef FORGECAD_FK_QUANTIZE_H
#define FORGECAD_FK_QUANTIZE_H

#include <vector>

#include "fk_classify.h"

#include "fk_intersect.h"
#include "fk_topology.h"

namespace ForgeCad::Kernel {

// Prototipo: discretizzazione volumetrica a celle cartesiane. Senza box
// si quantizza tutto il corpo. Altrimenti le zone hanno interni disgiunti e
// il B-rep esterno viene conservato e unito alle celle. L'operazione fallisce
// se il risultato non e' un solido connesso e valido o supera il budget.
void validateVoxelization(const Box &domain, double pitch);
Body quantizeSolid(const Body &source, double pitch, const std::vector<Box> &zones = {});

struct CubicLattice {
    Vec3 origin;             // Nodo del reticolo, nel sistema del solido.
    double cellPitch = 1.0;  // Distanza fra i nodi.
    double strutWidth = 0.4; // Lato della sezione quadrata delle aste.
};

// Le celle piene sono limitate alle tre famiglie di aste ortogonali che
// attraversano i nodi. Il passo di campionamento risolve la sezione delle aste.
// Scarta frammenti scollegati dal corpo conservato, o tiene la rete
// principale quando l'intero volume e' campionato. Verifica la connessione
// del solido risultante dopo la fusione.
void validateCubicLattice(const Box &domain, double samplingPitch, const CubicLattice &lattice);
bool latticeMaterial(const Vec3 &point, const CubicLattice &lattice);
Body latticeSolid(const Body &source, double samplingPitch, const CubicLattice &lattice,
                  const std::vector<Box> &zones = {});

struct WovenLattice {
    CubicLattice frame;
    double filamentWidth = 0.24;  // Larghezza trasversale sul piano di campionamento.
    double amplitude = 0.28;      // Salita e discesa rispetto al piano della trama.
};

struct WovenLatticeSettings {
    WovenLattice pattern;
    double samplingPitch = 0.2;
};

// Dimensiona una prima proposta sul volume selezionato. Garantisce i
// vincoli di risoluzione e il budget delle celle, non la connessione del B-rep.
WovenLatticeSettings suggestWovenLattice(const Box &domain);
void validateWovenLattice(const Box &domain, const WovenLatticeSettings &settings);
// Limite superiore del passo che separa fili, strati e aste. La griglia
// risultante deve ancora rientrare nel budget del volume selezionato.
double wovenLatticeSamplingLimit(const WovenLattice &lattice);

// Campo della trama, condiviso con la sola rappresentazione grafica.
bool wovenLatticeMaterial(const Vec3 &point, const WovenLattice &lattice);

// Ordito e trama si alternano in quota fra due piani di aste ortogonali.
// Gli incroci sono a meta' fra le colonne del reticolo. Le ondulazioni
// raggiungono il telaio presso gli incroci e lasciano un varco fra i fili.
Body wovenLatticeSolid(const Body &source, double samplingPitch, const WovenLattice &lattice,
                       const std::vector<Box> &zones = {});

// Fascia attorno al livello zero del giroide trigonometrico. Lo spessore
// nominale usa la distanza al primo ordine, prima del campionamento a celle.
struct SheetGyroid {
    Vec3 origin;
    double cellPitch = 2.5;
    double wallThickness = 0.4;
};

double sheetGyroidSamplingLimit(const SheetGyroid &pattern);
void validateSheetGyroid(const Box &domain, double samplingPitch, const SheetGyroid &pattern);
double sheetGyroidField(const Vec3 &point, const SheetGyroid &pattern);
bool sheetGyroidMaterial(const Vec3 &point, const SheetGyroid &pattern);
Body sheetGyroidSolid(const Body &source, double samplingPitch, const SheetGyroid &pattern,
                     const std::vector<Box> &zones = {});

// Condivisi dai generatori a celle e a isosuperficie.
bool solidGridCrossings(const Body &body, const RayFaceIndex &index, const SolidClassifier &classifier,
                        int axis, const Vec3 &origin, double tolerance, std::vector<double> &crossings);
void checkSampledVertexFans(const Body &body);

}

#endif
