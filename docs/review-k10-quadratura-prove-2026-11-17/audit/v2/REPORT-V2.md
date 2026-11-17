# Rettifica V1 e peer review statica V2 — quadratura / massa

La precedente conclusione «nessun blocker» sulla V1 era incompleta rispetto al contratto del chiamante `fk_mass.cpp`: la consultazione iniziale del grafo non era stata seguita dalla lettura del sentinel nella funzione `FaceIntegrator::finIntegral`. ROOT ha individuato il blocker del consumer. Le prove Python sugli overflow restano valide, ma il giudizio V1 non andava esteso all'integrazione del sistema massa. Il rapporto V1 SHA256 32be6dafd4dedd0141cfa29102b05509c2589ba01411e8dfb59bc4e4f72266ba resta immutato.

## Contratto del consumer verificato ora

I primi 11 valori di LoopEvaluation misurano area/momenti. I valori 11,12,13 integrano du, v*du, dv e servono a ricostruire gli avvolgimenti: non devono guidare la scelta dei nodi. Il codice originario assegnava deliberatamente HUGE_VAL alle ultime tre magnitude; con roundoff positivo il relativo floor era Inf e un errore finito non chiedeva raffinamento. La V1 avrebbe rifiutato il callback per magnitude non finita prima di arrivare a quel criterio. Questa incompatibilità è dimostrabile dalla fonte; nessun runtime C++ eseguito da questo audit.

## Candidato V2 verificato

- fk_quadrature.h SHA256 f1ec31f8673d753192d7d9646448f30a78f16fb9ec0bac514f74680dc663d3d6.
- fk_mass.cpp SHA256 8248392ee0b7be25dd2b4217bc0ad29eeaf04c939e4e4940a12d328be45c66aa.
- Fonte, patch e relazioni typed entranti/uscenti sono allegate come testo. Grafo 801 nodi / 11726 relazioni, call/include presenti e nessun reference nel focus estratto.

**Nessun blocker statico trovato nella V2.** `integrateVector` e `adaptivePanel` ricevono un parametro finale controlled con default N. controlled>N è rifiutato all'ingresso: integrateVector prima di gaussKronrod e quindi prima di qualsiasi callback; adaptivePanel prima di qualsiasi pannello figlio. I vecchi chiamanti senza parametro mantengono tutte le N componenti controllate. controlled=0 ammette il primo pannello finito senza richiesta di raffinamento.

Solo i loop di accettazione e noise si limitano al prefisso controlled. Il parametro viene propagato a entrambe le ricorsioni. La validazione di value/magnitude/error resta su tutte le N componenti prima del caso depth30; anche le somme dei figli restano controllate su N nel ramo noise e nel ramo ricorsivo. Una componente passiva non può quindi pubblicare NaN/Inf. Non cambiano tolleranze, roundoff, formule del percorso ordinario o limiti di profondità.

Il wrapper vettoriale adaptiveIntegral NON ha un nuovo argomento: continua a usare il default N; non esiste quindi un suo controlled invalido che possa sfuggire al preflight prima della valutazione dell'argomento panel. L'estensione è limitata ai due ingressi necessari al consumer.

In fk_mass LoopEvaluation ha già i membri array inizializzati a zero. Rimossa l'assegnazione HUGE_VAL, le tre magnitude passive restano zero; la chiamata integra tutti i 14 valori ma passa kFaceValues=11 come prefisso controllato. Le formule dei tre valori e il loro consumo successivo in domainIntegrals sono invariati; le relative magnitude non sono usate da tale consumo. Le altre chiamate vettoriali conservano default N.

## Limiti / attribuzione

Questa è lettura statica delle due fonti e della patch, non un PASS runtime massa. Nessun C++ compilato/eseguito e nessuna modifica al repository. Test, massa reale, confronti numerici e sanitizzatori appartengono a ROOT/tester. La V2 non rende la quadratura un certificato continuo globale: stima Gauss-Kronrod, underflow finito dei prodotti pesati e altri limiti numerici già dichiarati restano distinti.
