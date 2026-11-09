#ifndef FORGECAD_FK_BSPLINE_SURFACE_H
#define FORGECAD_FK_BSPLINE_SURFACE_H

#include "fk_bspline.h"
#include "fk_surface.h"

// Superfici B-spline e NURBS a prodotto tensoriale con nodi
// espansi (come BSplineCurve). Algoritmi: A3.6 e A4.4 del NURBS Book.
namespace ForgeCad::Kernel {

class BSplineSurface final : public Surface {
public:
    // poles[i * vPoleCount + j]: i lungo u, j lungo v (Poles(i+1, j+1) di OCCT).
    // weights vuoto = polinomiale, altrimenti stessa disposizione dei poli.
    BSplineSurface(int uDegree, int vDegree, std::vector<double> uKnots, std::vector<double> vKnots,
                   int uPoleCount, int vPoleCount, std::vector<Vec3> poles, std::vector<double> weights = {},
                   bool periodicU = false, bool periodicV = false);

    // Istanza esatta di una superficie immutabile: i poli restano nella base,
    // le valutazioni e gli accessi restituiscono coordinate trasformate.
    // Simmetrie e ripetizioni condividono cosi' anche griglie molto grandi.
    BSplineSurface(std::shared_ptr<const BSplineSurface> basis, const Transform3 &placement);
    const std::shared_ptr<const BSplineSurface> &basis() const { return basis_; }
    const Transform3 &placement() const { return placement_; }

    SurfaceType type() const override { return SurfaceType::BSpline; }
    Interval uDomain() const override;
    Interval vDomain() const override;
    // Estensione periodica del dominio fondamentale, anche con nodi clamped.
    // Gli estremi esatti restano distinti per verificare la chiusura dopo un edit.
    bool isUPeriodic() const override { return periodicU_; }
    bool isVPeriodic() const override { return periodicV_; }
    double uPeriod() const override { return periodicU_ ? uDomain().length() : 0; }
    double vPeriod() const override { return periodicV_ ? vDomain().length() : 0; }
    void evaluate(double u, double v, int order, Vec3 *out) const override;
    std::vector<double> uBreakpoints(const Interval &range) const override;
    std::vector<double> vBreakpoints(const Interval &range) const override;
    CurvePtr<3> uIso(double u) const override;
    CurvePtr<3> vIso(double v) const override;

    int uDegree() const { return uDegree_; }
    int vDegree() const { return vDegree_; }
    int uPoleCount() const { return uPoleCount_; }
    int vPoleCount() const { return vPoleCount_; }
    const std::vector<double> &uKnots() const { return basis_ ? basis_->uKnots() : uKnots_; }
    const std::vector<double> &vKnots() const { return basis_ ? basis_->vKnots() : vKnots_; }
    Vec3 pole(int i, int j) const { return basis_ ? placement_.applyToPoint(basis_->pole(i, j)) : poles_[i * vPoleCount_ + j]; }
    double weight(int i, int j) const { return basis_ ? basis_->weight(i, j) : (weights_.empty() ? 1.0 : weights_[i * vPoleCount_ + j]); }
    bool isRational() const { return basis_ ? basis_->isRational() : !weights_.empty(); }

    // Isoparametriche esatte: a u costante (curva in v) e a v costante (curva in u).
    BSplineCurve<3> uIsoCurve(double u) const;
    BSplineCurve<3> vIsoCurve(double v) const;
    // Pezze di Bezier (una per rettangolo tra nodi distinti), ordinate per u
    // e poi per v: [iu * numero_di_pezze_in_v + iv].
    std::vector<BSplineSurface> bezierPatches() const;
    // Le stesse, calcolate una volta per superficie (la superficie e'
    // immutabile; la cache si legge e si scrive in modo atomico). Chi la usa
    // tiene il puntatore per tutto il tempo in cui scorre il vettore.
    std::shared_ptr<const std::vector<BSplineSurface>> cachedBezierPatches() const;
    // Linee di nodo interne in cui la superficie non e' C1 (le derivate prime
    // omogenee dai due lati differiscono): solo li' il punto piu' vicino puo'
    // stare su uno spigolo senza essere stazionario. Ordinate, calcolate una
    // volta (le superfici dei raccordi hanno nodi tripli ma sono C1).
    struct SharpKnotLines {
        std::vector<double> u, v;
    };
    std::shared_ptr<const SharpKnotLines> cachedSharpKnotLines() const;
    // Gerarchia dei box dei poli delle pezze di Bezier (rettangoli della
    // griglia delle pezze divisi a meta' lungo il lato piu' lungo), calcolata
    // una volta: la proiezione di un punto scende dai box invece di guardare
    // tutte le pezze (le superfici a distanza delle B-spline ne hanno
    // decine di migliaia). leaf = indice della pezza nelle foglie, -1 altrove.
    struct PatchTree {
        struct Node {
            Vec3 lo, hi;     // box dei poli euclidei delle pezze del nodo
            Interval u, v;   // rettangolo dei parametri
            int leaf = -1, first = -1, second = -1;
        };
        std::vector<Node> nodes;  // nodes[0] = radice (vuoto senza pezze)
        double largestPatchDiagonal = 0.0;
    };
    std::shared_ptr<const PatchTree> cachedPatchTree() const;

private:
    bool periodicU_ = false, periodicV_ = false;
    int uDegree_, vDegree_;
    std::vector<double> uKnots_, vKnots_;
    int uPoleCount_, vPoleCount_;
    std::vector<Vec3> poles_;
    std::vector<double> weights_;
    std::shared_ptr<const BSplineSurface> basis_;
    Transform3 placement_;
    mutable std::shared_ptr<const std::vector<BSplineSurface>> patchCache_;
    mutable std::shared_ptr<const SharpKnotLines> sharpCache_;
    mutable std::shared_ptr<const PatchTree> treeCache_;
    // Isoparametriche ai nodi (le linee delle pezze), calcolate una volta: la
    // proiezione di un punto le prova tutte, e con le curve rifatte a ogni
    // chiamata costava millisecondi per punto (selezione a video, SP-curve).
    struct IsoCache;
    mutable std::shared_ptr<const IsoCache> isoCache_;
    CurvePtr<3> knotIso(bool fixedU, double value) const;
};

using BSplineSurfacePtr = std::shared_ptr<const BSplineSurface>;

}

#endif
