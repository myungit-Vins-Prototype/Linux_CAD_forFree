#ifndef FORGECAD_FK_HISTORY_H
#define FORGECAD_FK_HISTORY_H

#include <cstdint>
#include <string>
#include <vector>

#include "fk_math.h"
#include "fk_topology.h"

// Genealogia delle operazioni (provenienza delle entita' topologiche).
//
// Per ogni faccia, edge e vertice del risultato di un'operazione dice da
// quali entita' degli operandi viene:
//  - Same: la stessa entita' (stessa geometria e stesso contorno);
//  - Modified: un pezzo, un'unione o una versione rifilata/allungata
//    dell'entita' dell'operando (nell'inferenza: stessa superficie o curva;
//    se dichiarata dall'algoritmo, anche con superficie/curva cambiata);
//  - Generated: un'entita' nuova creata a partire da un'entita' dell'operando
//    (la faccia di un raccordo dallo spigolo raccordato, lo spigolo di
//    un'intersezione dalle facce su cui giace).
// Un'entita' di un operando senza discendenti Same/Modified e' stata
// cancellata (eventualmente consumata da un'entita' Generated).
//
// Gli algoritmi possono dichiarare le origini mentre costruiscono le entita'
// (declareOrigins). resolveHistory conserva queste dichiarazioni e ricava
// soltanto le origini non coperte con il confronto geometrico di inferHistory.
// La genealogia di ripiego si ricava dal risultato e dagli operandi con criteri
// geometrici esatti (stessa superficie o curva di supporto e contenimento di
// un punto interno, sulla geometria del kernel, mai sulla mesh): vale per
// tutte le operazioni senza modificarne gli algoritmi. Per le trasformazioni
// che conservano gli indici (transformBody) c'e' sameIndexHistory.
//
// Sopra la genealogia i nomi topologici persistenti (ElementNames): ogni
// entita' ha uno o piu' nomi che passano invariati attraverso le operazioni
// che la modificano (divisioni comprese: piu' pezzi hanno lo stesso nome,
// che diventa ambiguo e va risolto con un punto); le entita' nuove prendono
// un nome composto dal tag dell'operazione e dai nomi delle entita' da cui
// sono generate. I nomi non dipendono dagli indici, quindi sopravvivono alla
// rigenerazione con altri parametri e al riordino della storia finche' le
// entita' da cui derivano esistono.
namespace ForgeCad::Kernel {

enum class EntityKind : unsigned char { Vertex = 0, Edge = 1, Face = 2 };
enum class HistoryRelation : unsigned char { Same = 0, Modified = 1, Generated = 2 };

struct HistoryLink {
    int operand = 0;           // indice dell'operando
    EntityKind kind = EntityKind::Face;  // tipo dell'entita' dell'operando
    int index = -1;            // indice dell'entita' nell'operando
    HistoryRelation relation = HistoryRelation::Same;
};

// Evoluzione di un'entita' di un operando nel risultato.
enum class Evolution : unsigned char {
    Unchanged,  // un solo discendente Same
    Modified,   // un solo discendente Modified (rifilata, allungata)
    Split,      // piu' discendenti
    Merged,     // un discendente che deriva anche da altre entita'
    Deleted     // nessun discendente
};

class BodyHistory {
public:
    BodyHistory() = default;
    BodyHistory(int operandCount, const std::vector<int> &faceCounts, const std::vector<int> &edgeCounts,
                const std::vector<int> &vertexCounts, int resultFaces, int resultEdges, int resultVertices);

    int operandCount() const { return operands_; }
    int resultSize(EntityKind kind) const { return int(links_[int(kind)].size()); }
    int operandSize(int operand, EntityKind kind) const;

    // Provenienza dell'entita' `index` del risultato (vuota per un indice morto).
    const std::vector<HistoryLink> &origins(EntityKind kind, int index) const;
    void addOrigin(EntityKind kind, int index, const HistoryLink &link);

    // Dichiarazione dell'algoritmo: sostituisce tutte le origini dell'entita'.
    // Anche una lista vuota e' esplicita (entita' nuova senza generatori),
    // quindi il ripiego geometrico non deve aggiungervi origini.
    void declareOrigins(EntityKind kind, int index, std::vector<HistoryLink> origins);
    bool isDeclared(EntityKind kind, int index) const;

    // Entita' del risultato (dello stesso tipo) con un legame Same/Modified
    // all'entita' `index` dell'operando, in ordine di indice.
    std::vector<int> descendants(EntityKind kind, int operand, int index) const;
    // Entita' del risultato di tipo `resultKind` generate dall'entita'
    // `index` (di tipo `kind`) dell'operando.
    std::vector<int> generated(EntityKind resultKind, EntityKind kind, int operand, int index) const;
    Evolution evolution(EntityKind kind, int operand, int index) const;
    bool deleted(EntityKind kind, int operand, int index) const { return descendants(kind, operand, index).empty(); }

    // Genealogia composta: `first` porta dagli operandi A al corpo intermedio
    // M, `second` da (tra gli altri) M al risultato; M e' l'operando
    // `secondOperand` di `second`. Gli operandi del risultato sono quelli di
    // `first` seguiti dagli altri operandi di `second` (nell'ordine, senza M).
    // Same o Same = Same, altrimenti la relazione piu' debole.
    static BodyHistory compose(const BodyHistory &first, const BodyHistory &second, int secondOperand);

private:
    int operands_ = 0;
    std::vector<int> sizes_[3];  // [kind][operand]
    std::vector<std::vector<HistoryLink>> links_[3];
    std::vector<bool> declared_[3];
};

struct HistoryOptions {
    // Distanza entro la quale due punti coincidono (si somma alle
    // tolleranze locali di vertici ed edge).
    double tolerance = 1e-6;
    // Provenienza delle entita' generate (gli spigoli raccordati di una
    // faccia di raccordo, le facce su cui giace uno spigolo nuovo).
    bool generatedOrigins = true;
};

// Genealogia di `result` rispetto agli operandi (nell'ordine dato). Gli
// operandi nulli contano per l'indice ma non contribuiscono.
BodyHistory inferHistory(const std::vector<const Body *> &operands, const Body &result, const HistoryOptions &options = {});

// Storia senza legami, dimensionata sugli slot vivi (inclusi gli eventuali
// buchi negli indici): punto di partenza per le dichiarazioni degli algoritmi.
BodyHistory makeHistory(const std::vector<const Body *> &operands, const Body &result);

// Verifica dimensioni e indici delle dichiarazioni e le conserva senza
// confronti geometrici per le entita' coperte. Le altre usano inferHistory.
// Dichiarazioni incoerenti: std::invalid_argument, mai riutilizzo silenzioso.
BodyHistory resolveHistory(const std::vector<const Body *> &operands, const Body &result,
                           const BodyHistory *declared, const HistoryOptions &options = {});

// Trasformazione che conserva la topologia indice per indice (transformBody
// di una similitudine diretta, scaleBody): ogni entita' viva e' Modified
// della stessa entita' dell'operando. std::invalid_argument se i conteggi
// non tornano.
BodyHistory sameIndexHistory(const Body &operand, const Body &result);

// Punto dentro la faccia (non sul bordo), sulla superficie esatta; uv i suoi
// parametri. Falso se non si trova (faccia degenere o senza SP-curve).
bool faceInteriorPoint(const Body &body, FaceId face, Vec3 &point, Vec2 *uv = nullptr);

// --- Nomi topologici persistenti ---------------------------------------------

struct ElementNames {
    // Il body descritto (solo per riconoscere nomi non piu' validi: non
    // viene dereferenziato).
    const Body *body = nullptr;
    // Nomi per indice di entita' (vuoti per le entita' morte), ordinati.
    std::vector<std::vector<std::string>> faces, edges, vertices;

    const std::vector<std::vector<std::string>> &table(EntityKind kind) const;
    std::vector<std::vector<std::string>> &table(EntityKind kind);
    // Nome principale (il primo) dell'entita', vuoto se non ne ha.
    std::string primary(EntityKind kind, int index) const;
    // Entita' che hanno `name` tra i loro nomi.
    std::vector<int> find(EntityKind kind, const std::string &name) const;
};

// Etichette semantiche delle facce di un'operazione di primo livello (per
// esempio "start", "end", "side.s3" di un'estrusione): indice di faccia ->
// etichetta, vuota se la faccia non ne ha.
using FaceLabels = std::vector<std::string>;
// Etichette emesse dalla costruzione: piu' sorgenti possono confluire in
// una faccia (per esempio due fianchi coplanari dopo una fusione).
using FaceAliases = std::vector<std::vector<std::string>>;

// Nomi del risultato di un'operazione con genealogia `history` rispetto a
// operandi con i nomi `operandNames` (puntatori nulli: operandi senza nomi).
// `tag` identifica l'operazione (unico e stabile, per esempio l'ID
// persistente della feature). Regole:
//  - faccia/edge/vertice con legami Same/Modified: l'unione dei nomi delle
//    entita' da cui deriva;
//  - faccia nuova con un'etichetta in `labels`: tag:etichetta;
//  - faccia nuova senza etichetta: tag:G(nomi delle entita' da cui e'
//    generata), o tag:F<indice> se non ne ha;
//  - edge nuovo: E(nomi principali delle facce adiacenti), vertice nuovo:
//    V(nomi principali degli edge che vi arrivano).
// I nomi piu' lunghi di 160 caratteri si sostituiscono con un'impronta
// (FNV-1a a 64 bit) del nome intero, prefissata dal tag.
ElementNames propagateNames(const Body &result, const BodyHistory &history, const std::vector<const ElementNames *> &operandNames,
                            const std::string &tag, const FaceLabels *labels = nullptr, const FaceAliases *declaredLabels = nullptr);

}

#endif
