# Roadmap generale ForgeCAD

Riesame del 30 ottobre 2026, dopo l'unificazione su `main`. Questo indice raccoglie lo stato corrente; i rapporti datati conservano misure, limiti e verifiche dei rispettivi checkpoint. Il registro condiviso [CLAUDE.md](../CLAUDE.md) contiene gli esiti effettivi e distingue implementazione, verifica ed esperimenti. Le priorità successive sono proposte modificabili durante lo sviluppo.

## Funzioni già presenti

| Area | Stato da sorgenti e registro | Riferimento |
| --- | --- | --- |
| Sviluppo e portabilità | Un solo `main`, backend CPU con CUDA/Metal opzionali, CI Linux/macOS. L'unificazione è stata verificata sui due sistemi. | [CI](ci.md), [rapporto unificazione](unificazione-main-2026-10-30.json) |
| Storia e documenti | Storyboard, rigenerazione, soppressione, ripetizioni, riferimenti topologici, recupero e compressione Zstandard. | [Storia](storyboard-history.md), [README](../README.it.md) |
| Superfici 3D | Estensione fino a riferimento, reti U/V a celle Coons, continuità per contatto, modifica poli/pesi NURBS, con i limiti documentati. | [Superfici](../README.it.md#modellazione-di-superfici) |
| Viste 2D | Proiezioni, sezioni, ausiliarie, dettagli con contorno circolare/rettangolare/libero, collegamenti alla madre e viste interrotte. | [Studio 2D](drawing-revisions-study.md), [interruzioni](drawing-breaks.md) |
| Sezioni composte | Gradini multipli e allineata a due piani, creazione con mouse e snap oltre all'opzione numerica. Restano i limiti delle sezioni rimosse. | [Registro](../CLAUDE.md), [studio 2D](drawing-revisions-study.md) |
| Quotatura e tavole | Catene/ordinate, tolleranze dimensionali, datum e sei caratteristiche GD&T, badge persistenti, fogli multipli, scala foglio/vista, richiami obliqui e PDF. | [Studio 2D](drawing-revisions-study.md) |
| Revisioni | Consultazione, check-out/check-in, ripristino nella copia di lavoro, rilascio coordinato locale, verifica/recupero catalogo. | [Studio, sezioni 24–26](drawing-revisions-study.md#24-ripristino-di-una-revisione-nella-copia-di-lavoro) |
| Prestazioni | Filtri CPU e riuso di query già integrati; GPU disponibile per carichi specifici. Ogni beneficio riguarda il caso misurato, senza fattore globale 10× dimostrato. | [Prestazioni](viewport-performance.md), [storico solver/GPU](solver-cuda-roadmap-2026-10-26.md) |

## Incremento attuale

**Allineamento grafico delle quote**, già durante il primo posizionamento e nel successivo trascinamento: guide lampeggianti sul centro del testo più vicino nel verso del movimento, orizzontale e verticale; snap prioritario fra punte coincidenti, anche quota angolare/lineare con lato comune orizzontale, verticale o inclinato. Indicatore sul punto comune, Alt/Esc e un solo Undo al rilascio. Completato e verificato su Linux: build completa, 37/37 regressioni, Qt XCB nativo, PDF e mappa PASS; dettagli nel [registro](../CLAUDE.md). [Uso](drawing-revisions-study.md#allineamento-grafico-delle-quote--30-ottobre-2026).

Incremento precedente completato: **rugosità strutturate Ra/Rz**, micrometri, asportazione, processo, agganci e PDF vettoriale. [Uso e limiti](drawing-revisions-study.md#rugosità-superficiale-strutturata--30-ottobre-2026).

## Lavoro ancora aperto

| Priorità proposta | Incremento concreto | Criterio di completamento |
| --- | --- | --- |
| 1 | Annotazioni di saldatura strutturate | Campi distinti dal testo libero, simboli vettoriali, agganci, proprietà, Undo e codec; definire prima il sottoinsieme supportato. |
| 2 | Pacchetto autonomo di un rilascio | Modello e tavole nei check-in esatti, tutti i fogli/PDF e manifesto con impronte; consegna verificabile fuori dal workspace originale. Il manifesto locale esiste già. |
| 3 | Ulteriori riferimenti ai contorni apparenti e casi HLR | Partire da casi riproducibili di superfici generiche, senza promuovere mesh o pixel a geometria di quota e senza riassociare per vicinanza. |
| 4 | Profilazione della rigenerazione dopo i filtri già integrati | Baseline attuale, copie congelate, confronto numerico e ripetizioni; scegliere il costo dominante prima di nuovi lotti o parallelismo. |
| 5 | Budget complessivi GPU e carichi prolungati | Coordinare limiti fra backend/superfici, saturazione e fallback CPU; verificare su hardware reale distinto. |

Estensioni successive: altre caratteristiche/modificatori GD&T, più parametri di rugosità e indicazioni complete; ulteriori combinazioni delle sezioni composte. Approvazioni, autenticazione/permessi, filesystem di rete e porting Windows richiedono requisiti e prove propri; non sono coperti dai lock cooperativi o dalla CI Linux/macOS.

## Regole per la prosecuzione

- Verificare prima nel codice e nel registro se un vecchio punto è già completato.
- Integrare incrementi circoscritti su `main`, preservando il fallback CPU e la compatibilità dichiarata.
- Per modifiche strutturali aggiornare e verificare la [mappa locale](architecture/README.md).
- Registrare build, test realmente eseguiti e limiti senza attribuire alla nuova sessione le misure storiche.
