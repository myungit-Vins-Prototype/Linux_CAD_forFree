#ifndef FORGECAD_ELEMENT_NAMES_H
#define FORGECAD_ELEMENT_NAMES_H

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "cad_types.h"
#include "fk_history.h"

// Genealogia delle feature nell'applicazione: ogni B-rep costruito da una
// feature viene registrato con i B-rep degli operandi e il tag della feature
// (il suo ID persistente). La genealogia (fk_history) e i nomi topologici si
// calcolano solo quando servono (scelta o risoluzione di un riferimento) e
// una volta per B-rep; i B-rep sono immutabili e condivisi, per questo la
// registrazione segue la loro vita (una voce scade con il B-rep).
namespace ForgeCad {

// Registra il risultato `body.forgeBody` della feature `index`. Gli operandi
// sono gli stadi precedenti consumati o modificati (base dei raccordi e delle
// altre feature dello stesso corpo, operandi delle booleane, corpi fusi). Le
// estrusioni e le primitive danno alle loro facce nuove etichette
// semantiche (coperchi, fianco per entita' dello schizzo, direzione).
// Un B-rep gia' registrato (feature soppressa: lo stesso della base) resta.
void registerFeatureGenealogy(const ExtrusionObject &body, int index, const QVector<SketchObject> &sketches,
                              const QVector<ExtrusionObject> &bodies);

// Provenienza emessa dal kernel durante la costruzione, prima della
// registrazione della feature. Si usa solo se risultato e operandi sono gli
// stessi B-rep: anteprime scartate e indirizzi riutilizzati non la ereditano.
// Non calcola nomi e non registra da sola una feature. Le dichiarazioni
// vengono anche ripristinate dalla cache del documento prima della feature.
void registerDeclaredHistory(const ForgeBody &result, const std::vector<ForgeBody> &operands, Kernel::BodyHistory history,
                             Kernel::FaceAliases labels = {});

// Entita' del documento importato: nessun genitore B-rep locale, nomi F<indice> storici.
void registerImportedHistory(const ForgeBody &body);

// Operandi validi della definizione corrente, nell'ordine usato dal kernel.
QVector<int> featureGenealogyOperandIndices(const ExtrusionObject &body, int index, const QVector<ExtrusionObject> &bodies);

struct DeclaredHistorySnapshot {
    std::shared_ptr<const Kernel::BodyHistory> history;
    std::shared_ptr<const Kernel::FaceAliases> labels;
    QVector<int> operandIndices;
};
// Solo le dichiarazioni gia' emesse, senza calcolare nomi o inferenza.
// Vuoto se il risultato o gli operandi non coincidono con i B-rep correnti.
DeclaredHistorySnapshot declaredHistorySnapshot(const ExtrusionObject &body, int index, const QVector<ExtrusionObject> &bodies);

// Gli operandi effettivi della genealogia, nell'ordine dei legami del kernel.
// I puntatori deboli non prolungano la vita dei risultati precedenti.
struct GenealogyOperand {
    int index = -1;
    quint64 featureId = 0;
    QString name;
    std::weak_ptr<const Kernel::Body> body;
};
QVector<GenealogyOperand> genealogyOperandsOf(const Kernel::Body &body);
QString entityEvolutionText(const Kernel::Body &body, Kernel::EntityKind kind, int index);
QString featureEvolutionText(const Kernel::Body &body);

// Nomi delle entita' del B-rep (nullptr se non e' registrato).
std::shared_ptr<const Kernel::ElementNames> elementNamesOf(const Kernel::Body &body);
// Genealogia rispetto agli operandi registrati (nullptr se non registrato).
std::shared_ptr<const Kernel::BodyHistory> featureHistoryOf(const Kernel::Body &body);
// Nome principale dell'entita' (vuoto se non c'e').
QString elementName(const Kernel::Body &body, Kernel::EntityKind kind, int index);

// Avvisi della risoluzione dei riferimenti nel thread corrente (riferimenti
// ambigui o ritrovati solo per geometria): buildGeometry li raccoglie
// nell'esito della feature.
void clearReferenceWarnings();
void addReferenceWarning(const QString &warning);
QStringList takeReferenceWarnings();

}

#endif
