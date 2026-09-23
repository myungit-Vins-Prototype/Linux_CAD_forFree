#ifndef FORGECAD_KERNEL_LAB_H
#define FORGECAD_KERNEL_LAB_H

#include <QString>
#include <QVector>
#include <QVector3D>
#include <memory>

#include "cad_types.h"

// Banco di prova del kernel sperimentale (kernel/): una superficie costruita
// sia con ForgeCad::Kernel sia con OpenCASCADE, e la proiezione di un punto
// calcolata con entrambi per confrontarne risultato e tempi. Solo per il menu
// Debug: non tocca il documento.
namespace ForgeCad {

// Le ultime due voci sono solidi B-rep (topologia + controllo di validita'):
// si disegnano gli spigoli e non si proietta il mouse.
// SketchExtrusion: estrusione di uno schizzo del documento rifatta con il
// nuovo kernel (compareSketchExtrusion); DocumentBody: corpo qualsiasi del
// documento, booleane comprese (compareDocumentBody).
enum class KernelLabShape { None, Sphere, Torus, Cone, BSpline, Revolution, Extrusion, PrismBody, CylinderBody, SketchExtrusion, DocumentBody };

struct KernelLabProbe {
    bool valid = false;
    QVector3D cursor;          // punto proiettato
    QVector3D ours;            // piede sul nuovo kernel
    QVector3D normal;          // normale del nuovo kernel nel piede (se definita)
    bool hasNormal = false;
    double oursDistance = 0.0;
    double oursMicroseconds = 0.0;
    bool occtFound = false;    // OCCT restituisce solo proiezioni ortogonali interne
    QVector3D occt;
    double occtDistance = 0.0;
    double occtMicroseconds = 0.0;
    double pointGap = 0.0;     // distanza tra i due piedi
    // Stessa distanza ma piedi diversi: piu' punti equidistanti (p sull'asse di
    // una rivoluzione, simmetrie), non un errore.
    bool tie = false;
};

class KernelLab {
public:
    KernelLab();
    ~KernelLab();

    void setShape(KernelLabShape shape);
    KernelLabShape shape() const;
    QString shapeName() const;
    QVector3D center() const;
    // Isoparametriche (superfici) o spigoli (solidi) campionati, solo per disegnare.
    const QVector<QVector<QVector3D>> &wireframe() const;
    // Per i solidi: conteggi topologici, genere ed esito di checkBody.
    QString summary() const;
    KernelLabProbe probe(double x, double y, double z) const;

    // Estrude lo schizzo con il nuovo kernel (stessa conversione delle curve
    // di cad_curve_solver, stesso collegamento degli estremi entro 1e-6) e lo
    // confronta con la forma OCCT del documento: validita', conteggi, volume,
    // lunghezza degli spigoli. Restituisce un messaggio d'errore o vuoto.
    QString compareSketchExtrusion(const SketchObject &sketch, double distance, const TopoDS_Shape &occtShape);
    // Rifa col nuovo kernel il corpo `index` del documento: estrusioni degli
    // schizzi e booleane, ricorsivamente (fk_boolean). Confronta volume, area
    // e baricentro con la forma OCCT del documento.
    QString compareDocumentBody(const QVector<SketchObject> &sketches, const QVector<ExtrusionObject> &bodies, int index);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif
