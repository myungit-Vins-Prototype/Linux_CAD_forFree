#ifndef FORGECAD_FK_TOPOLOGY_H
#define FORGECAD_FK_TOPOLOGY_H

#include <string>
#include <vector>

#include "fk_curve.h"
#include "fk_surface.h"

// Topologia B-rep sul modello di Parasolid:
//
//   Body -> Region -> Shell -> Face -> Loop -> Fin -> Edge -> Vertex
//
// - La region 0 e' sempre l'esterno (vuoto infinito); i solidi sono region
//   "solid" delimitate da shell.
// - Una fin e' l'uso di un edge da parte di un loop (il "half-edge" di
//   Mantyla, il "coedge" di ACIS): ogni edge di un solido chiuso ha due fin,
//   una per ciascuna faccia adiacente, percorse in versi opposti.
// - Le facce periodiche non hanno spigolo di cucitura: il fianco di un
//   cilindro ha due loop (i due cerchi), non un loop con seam come in OCCT.
// - Un loop puo' essere un vertice isolato (senza fin): serve agli operatori
//   di Eulero e ai corpi degeneri.
// - Convenzione di orientamento: camminando lungo una fin con la normale
//   della faccia verso l'alto, la faccia e' a sinistra. Quindi il loop esterno
//   gira in senso antiorario attorno alla normale e i fori in senso orario.
//
// Le entita' stanno in vettori e si riferiscono tra loro con indici
// tipizzati: il body si copia con un assegnamento (utile per l'Undo) e non ci
// sono cicli di puntatori. Le entita' cancellate restano come "morte"
// (alive = false) finche' il body esiste.
namespace ForgeCad::Kernel {

template <class Tag>
struct Id {
    int index = -1;
    constexpr Id() = default;
    constexpr explicit Id(int value) : index(value) {}
    constexpr bool valid() const { return index >= 0; }
    friend constexpr bool operator==(Id a, Id b) { return a.index == b.index; }
    friend constexpr bool operator!=(Id a, Id b) { return a.index != b.index; }
};

using VertexId = Id<struct VertexTag>;
using EdgeId = Id<struct EdgeTag>;
using FinId = Id<struct FinTag>;
using LoopId = Id<struct LoopTag>;
using FaceId = Id<struct FaceTag>;
using ShellId = Id<struct ShellTag>;
using RegionId = Id<struct RegionTag>;

// tolerance == 0: tolleranza di default (kLinearResolution). Una tolleranza
// locale maggiore (vertici ed edge "tolleranti" di Parasolid, TVERTEX/TEDGE
// di ACIS) si usa quando la geometria non si chiude entro quella di default,
// per esempio sui risultati delle intersezioni o sui dati importati.
struct Vertex {
    Vec3 point;
    double tolerance = 0.0;
    bool alive = true;
};

struct Edge {
    FinId forward;   // fin che percorre l'edge nel verso della curva
    FinId backward;  // fin opposta
    CurvePtr<3> curve;
    Interval range;  // tratto di curva: range.lo nel vertice iniziale
    double tolerance = 0.0;
    bool alive = true;
};

struct Fin {
    LoopId loop;
    EdgeId edge;
    FinId next, previous;  // nel loop
    VertexId vertex;       // vertice di partenza (nel verso del loop)
    bool sense = true;     // true: percorre l'edge nel suo verso
    // SP-curve: la curva dell'edge nello spazio (u, v) della faccia, con lo
    // stesso parametro dell'edge (S(pcurve(t)) = curve(t), qualunque sia
    // `sense`). Facoltativa; vedi fk_pcurve.h.
    CurvePtr<2> pcurve;
    double pcurveTolerance = 0.0;  // 0: SP-curve esatta; altrimenti scarto massimo in 3D dell'approssimazione
    bool alive = true;
};

struct Loop {
    FaceId face;
    FinId first;            // una fin qualsiasi del ciclo
    VertexId isolatedVertex;  // valido solo per i loop senza fin
    bool alive = true;
};

struct Face {
    ShellId shell;
    std::vector<LoopId> loops;
    SurfacePtr surface;
    bool sense = true;  // true: la normale della faccia e' quella della superficie
    bool alive = true;
};

struct Shell {
    RegionId region;
    std::vector<FaceId> faces;
    bool alive = true;
};

struct Region {
    bool solid = false;
    std::vector<ShellId> shells;
    bool alive = true;
};

struct TopologyCounts {
    int vertices = 0, edges = 0, fins = 0, loops = 0, faces = 0, shells = 0, regions = 0;
    int rings() const { return loops - faces; }  // loop oltre al primo di ogni faccia
};

class Body {
public:
    Body();  // solo la region esterna

    // Accesso (std::out_of_range per id non validi o entita' cancellate).
    const Vertex &vertex(VertexId id) const;
    Vertex &vertex(VertexId id);
    const Edge &edge(EdgeId id) const;
    Edge &edge(EdgeId id);
    const Fin &fin(FinId id) const;
    Fin &fin(FinId id);
    const Loop &loop(LoopId id) const;
    Loop &loop(LoopId id);
    const Face &face(FaceId id) const;
    Face &face(FaceId id);
    const Shell &shell(ShellId id) const;
    Shell &shell(ShellId id);
    const Region &region(RegionId id) const;
    Region &region(RegionId id);

    bool contains(VertexId id) const;
    bool contains(EdgeId id) const;
    bool contains(FinId id) const;
    bool contains(LoopId id) const;
    bool contains(FaceId id) const;
    bool contains(ShellId id) const;
    bool contains(RegionId id) const;

    RegionId exteriorRegion() const { return RegionId(0); }

    // Entita' vive.
    std::vector<VertexId> vertices() const;
    std::vector<EdgeId> edges() const;
    std::vector<FinId> fins() const;
    std::vector<LoopId> loops() const;
    std::vector<FaceId> faces() const;
    std::vector<ShellId> shells() const;
    std::vector<RegionId> regions() const;
    TopologyCounts counts() const;

    // Navigazione.
    VertexId finStart(FinId f) const { return fin(f).vertex; }
    VertexId finEnd(FinId f) const { return fin(fin(f).next).vertex; }
    FinId otherFin(FinId f) const;
    FaceId finFace(FinId f) const { return loop(fin(f).loop).face; }
    VertexId edgeStart(EdgeId e) const { return fin(edge(e).forward).vertex; }
    VertexId edgeEnd(EdgeId e) const { return fin(edge(e).backward).vertex; }
    // Fin del loop nell'ordine del ciclo, a partire da loop.first.
    std::vector<FinId> loopFins(LoopId l) const;
    // Prima fin del loop che parte da v (non valida se non c'e').
    FinId findFin(LoopId l, VertexId v) const;
    // Punto della fin al parametro normalizzato s in [0, 1] (nel verso della fin).
    Vec3 finPoint(FinId f, double s) const;

    // Inverte il verso dell'edge: scambia le fin forward/backward e, se c'e',
    // sostituisce la curva (e le SP-curve delle fin) con quella percorsa al contrario. La topologia non
    // cambia: il verso di un edge e' solo un'etichetta.
    void reverseEdge(EdgeId edge);

    // --- Costruzione diretta (risultati delle booleane, import) ---
    struct BuildEdge {
        int start = -1, end = -1;  // indici nei vertici; start nel punto curve(range.lo)
        CurvePtr<3> curve;
        Interval range;
        double tolerance = 0.0;
    };
    struct BuildFin {
        int edge = -1;
        bool sense = true;  // true: percorre l'edge nel suo verso
        CurvePtr<2> pcurve;  // facoltativa (vedi Fin::pcurve)
        double pcurveTolerance = 0.0;
    };
    struct BuildFace {
        SurfacePtr surface;
        bool sense = true;
        std::vector<std::vector<BuildFin>> loops;  // ogni loop: fin nell'ordine del ciclo
    };
    // Body con una region solida; le shell sono le componenti connesse delle
    // facce (attraverso gli edge). Ogni edge deve essere usato da esattamente
    // due fin di verso opposto (altrimenti std::invalid_argument). Le SP-curve
    // sono quelle date nelle BuildFin (le mancanti: vedi computePCurves).
    static Body build(const std::vector<Vec3> &vertices, const std::vector<BuildEdge> &edges,
                      const std::vector<BuildFace> &faces);

    // --- Operatori di Eulero (Mantyla, "An Introduction to Solid Modeling") ---
    // Ognuno lascia il body topologicamente valido e ha la sua inversa.
    // Non assegnano geometria, tranne i punti dei vertici nuovi e, in semv,
    // la divisione della curva dell'edge.

    struct MvfsResult {
        VertexId vertex;
        FaceId face;
        LoopId loop;
        ShellId shell;
        RegionId region;
    };
    // Make Vertex Face Shell: nuovo solido minimo (un vertice, una faccia il
    // cui unico loop e' il vertice isolato, una shell, una region solida).
    MvfsResult mvfs(const Vec3 &point);
    // Inversa: la shell deve essere minima.
    void kvfs(ShellId shell);

    struct MevResult {
        EdgeId edge;
        VertexId vertex;
    };
    // Make Edge Vertex: nuovo edge "sperone" dal vertice isolato del loop...
    MevResult mev(LoopId isolatedLoop, const Vec3 &point);
    // ...oppure dal vertice iniziale di `before`, inserito nel loop subito prima
    // di `before`. L'edge va dal vecchio vertice al nuovo.
    MevResult mev(FinId before, const Vec3 &point);
    // Inversa: uno dei due vertici dell'edge deve avere solo questo edge.
    void kev(EdgeId edge);

    struct MefResult {
        EdgeId edge;
        FaceId face;
        LoopId loop;
    };
    // Make Edge Face: edge dal vertice di `first` a quello di `second` (stesso
    // loop). Le fin da `first` a quella prima di `second` passano al loop
    // della nuova faccia (fin backward); il vecchio loop tiene la fin forward.
    // first == second: edge chiuso (un cerchio) su quel vertice.
    MefResult mef(FinId first, FinId second);
    // Edge chiuso sul vertice isolato del loop.
    MefResult mef(LoopId isolatedLoop);
    // Inversa: elimina l'edge e la faccia della sua fin backward, fondendola
    // in quella della fin forward (che eredita anche gli altri loop).
    void kef(EdgeId edge);

    // Kill Edge Make Ring: le due fin dell'edge sono nello stesso loop;
    // togliendo l'edge il loop si divide in due. Il tratto tra la fin forward
    // e la backward diventa un nuovo loop (anello) della stessa faccia, che
    // viene restituito.
    LoopId kemr(EdgeId edge);
    // Inversa: unisce due loop della stessa faccia con un nuovo edge dal
    // vertice di `first` (in firstLoop) a quello di `second` (in secondLoop).
    // Una fin non valida indica un loop fatto di un vertice isolato.
    EdgeId mekr(LoopId firstLoop, FinId first, LoopId secondLoop, FinId second);

    // Kill Face Make Ring Hole: una faccia con un solo loop diventa un anello
    // di un'altra faccia della stessa shell (aumenta il genere, per esempio un
    // foro passante).
    void kfmrh(FaceId kill, FaceId keep);
    // Inversa: l'anello diventa l'unico loop di una nuova faccia.
    FaceId mfkrh(LoopId ring);

    struct SemvResult {
        VertexId vertex;
        EdgeId edge;  // seconda meta', dal nuovo vertice alla vecchia fine
    };
    // Split Edge Make Vertex: divide l'edge in un punto. Se l'edge ha una
    // curva, le due meta' la condividono con il range diviso in `parameter`.
    SemvResult semv(EdgeId edge, const Vec3 &point, double parameter = 0.0);
    // Inversa: riunisce l'edge creato da semv con quello che lo precede.
    void jekv(EdgeId secondHalf);

private:
    VertexId newVertex(const Vec3 &point);
    EdgeId newEdge();
    FinId newFin(LoopId loop, EdgeId edge, VertexId vertex, bool sense);
    LoopId newLoop(FaceId face);
    FaceId newFace(ShellId shell);
    ShellId newShell(RegionId region);
    RegionId newRegion(bool solid);
    void link(FinId from, FinId to);
    int finsAt(VertexId v) const;

    std::vector<Vertex> vertices_;
    std::vector<Edge> edges_;
    std::vector<Fin> fins_;
    std::vector<Loop> loops_;
    std::vector<Face> faces_;
    std::vector<Shell> shells_;
    std::vector<Region> regions_;
};

}

#endif
