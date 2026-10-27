# Soppressione: ripristino degli operandi — 27 ottobre 2026

Corretto il difetto di visibilita dopo la soppressione di una feature che consuma altri corpi, su main e solver-mesh-gpu e nella build di sviluppo attiva.

## Causa e correzione

`setFeaturesSuppressed` ripristinava il flag visibile dell'operando, ma `consumedBodies_` manteneva il vecchio ID del corpo consumato. La rigenerazione passa da `synchronizePatternResults`: prima della normalizzazione ripristina le preferenze dei corpi consumati. Quando l'utente aveva nascosto l'utensile durante il rollback, questa vecchia preferenza annullava il ripristino richiesto dalla soppressione.

La correzione invalida solo l'ID del corpo effettivamente rilasciato, prima della rigenerazione. La verifica esistente di altri consumatori attivi resta valida: un utensile comune resta nascosto fino alla soppressione dell'ultimo consumatore. Geometria, tolleranze, riferimenti e formato documento invariati. Nessun ripristino generalizzato dei flag Mostra/Nascondi.

## Verifiche

- Prima della correzione: sonde isolate main/solver falliscono entrambe sul ripristino dei due operandi. Dopo: passano tre cicli di soppressione/riattivazione, rigenerazione, codec, cursore e Undo/Redo.
- Regressione permanente `history_position` estesa con verifica della scena (`isBodyDrawn`), Undo/Redo, rigenerazione completa, codec della sola definizione e con cache, cursore, utensile comune a due booleane, soppressione/riattivazione multipla e Undo/Redo della selezione multipla.
- **28/28 CTest pertinenti superati su main, 28/28 su solver-mesh-gpu e 28/28 nello sviluppo locale**. Comprendono `history_position`, che falliva, e `history_suppression`. Nessun test disabilitato o criterio allentato; non e' la suite completa dell'applicazione.
- Applicazioni complete e runner Release compilati nei tre checkout. Corrette nella sola strumentazione le firme/namespace del codec e dei tipi durante la preparazione delle nuove fixture.
- Mappe rigenerate e controllate: main469file/6554relazioni/230TU; solver494/6686/235; sviluppo496/6728/235. Zero diagnostiche; quattro confini di chiamata verificati in entrambi i versi e righe valide, include distinti; viste ispezionate in Firefox con profili temporanei. Il primo controllo manuale chiedeva erroneamente un include diretto: verificato l'include reale dell'header interno, distinto dalla chiamata risolta alla dipendenza. HTML/JSON generati ignorati da Git.

Questo seguito inizia dopo il commit/push B-spline `49d69b05eafcffa32960230aad2535d63b01e8cc` su origin/main. **Il fix di soppressione e' locale e verificato; incluso nel checkpoint completo richiesto successivamente dall’utente, senza push**, separato dall'ottimizzazione pubblicata. Conservati branch locali divergenti, modifiche sperimentali e output/. Installazione desktop preservata; nessuna scrittura di CAD/archivi/sessioni utente. Nessuna build nativa macOS eseguita. Artefatti nel checkout di sviluppo `forgecad-linux-solver/benchmarks/suppression-visibility-2026-10-27`.
