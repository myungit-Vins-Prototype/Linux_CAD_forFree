# Intersezioni fra curve: filtro dei tratti Bezier — 27 ottobre 2026

Su `solver-mesh-gpu` è integrata una gerarchia locale dei riquadri dei tratti Bezier per le intersezioni fra B-spline/NURBS 2D. **CT0578 si carica con cache in 5,08 invece di 5,57 secondi, circa il 9% di tempo in meno.** La rigenerazione completa resta intorno a 27 secondi: la variazione delle mediane è circa l’1% e gli intervalli si sovrappongono. Non è una nuova accelerazione dimostrata della rigenerazione né un 10× dell’applicazione.

## Intervento

Il nuovo profilo della versione già ottimizzata contiene 13.799 campioni CPU aggregati fra thread: 9,35% flat nella suddivisione delle curve 2D e circa 39% inclusivo nella generazione dei semi delle intersezioni fra superfici. Queste quote inclusive si sovrappongono e non sono percentuali del tempo wall. La sonda diagnostica mostra molti prodotti fra centinaia di tratti, per esempio 365×751 e 378×378: ciascuna coppia ricalcolava i riquadri prima di scartarsi.

`kernel/fk_intersect.cpp` costruisce `PieceBoxTree` sui riquadri dei poli della seconda curva quando ci sono almeno 16 pezze. L’albero è locale alla chiamata e richiede memoria temporanea O(numero di pezze), senza cache persistente o nuovo stato del documento. Le foglie contengono al massimo otto pezze e la visita conserva l’ordine originale dei parametri; vengono eliminate soltanto le coppie che il vecchio primo controllo avrebbe scartato. Il confronto dei riquadri, il margine di tolleranza, le suddivisioni, Newton, l’ordine dei candidati e la loro unione restano invariati. Per pochi tratti si usa il ciclo precedente.

La modifica è generale e non contiene condizioni sul nome o sui parametri di CT0578. È codice CPU C++ comune; nessun nuovo percorso CUDA/Metal, formato o API pubblica. Il contratto che ricostruisce la madre della ripetizione anche all’apertura con cache è conservato.

## Confronto documentale

Quattro processi nuovi per versione su CT0578, mouse2 e assieme, ordine alternato, qualità media CPU. Nessuna compilazione, test, mappa o profilazione contemporanei ai lotti. Cache del filesystem, frequenza, temperatura e carico esterno non controllati. Il timer della cache misura `loadDocument`, escludendo la lettura precedente; quello di Rigenera tutto comprende Undo, geometria, normalizzazione e preparazione delle mesh, escludendo il caricamento iniziale, le firme e la presentazione OpenGL/albero.

| Scenario | Prima, mediana | Dopo, mediana | Tempo risparmiato |
|---|---:|---:|---:|
| CT0578: caricamento con cache | 5.572 s | 5.078 s | 8.87% |
| CT0578: Rigenera tutto | 27.409 s | 27.148 s | 0.95% |
| mouse2: caricamento con cache | 0.807 s | 0.807 s | -0.03% |
| mouse2: Rigenera tutto | 7.091 s | 7.074 s | 0.24% |
| Assieme: caricamento con cache | 3.875 s | 3.825 s | 1.31% |
| Assieme: Rigenera tutto | 14.695 s | 15.278 s | -3.97% |

Il beneficio più netto di questo intervento è nel caricamento di CT0578. Il circa 3,1× misurato nel rapporto precedente sulle proiezioni rimane un risultato distinto; non sommare i rapporti di scenari diversi. Su mouse2 interpretare gli intervalli e i campioni del JSON, senza estendere automaticamente il vantaggio di CT0578. La prima coppia sull’assieme da 1.200 corpi indicava un possibile rallentamento: il controllo è stato ampliato a quattro processi per versione; campioni e intervalli completi nel JSON.

Sull’assieme la mediana di Rigenera tutto passa da 14,695 a 15,278 secondi (circa il 4% più lenta), con intervalli sovrapposti: 14,283–15,572 prima e 14,451–15,590 dopo. Lo scarto resta registrato e non viene presentato come un beneficio. Una sonda separata registra **zero costruzioni della gerarchia** sull’assieme, mentre il controllo positivo sulle regressioni analitiche la esercita correttamente: questo documento non usa il nuovo filtro. Non è accertata la causa dello scarto dei tempi; i tempi della sonda sono esclusi dai lotti.

Tutti i confronti conservano byte dei B-rep, errori, visibilità, facce e firma completa delle mesh, incluse normali/bordi/ID/trasformazioni. CT0578: copia SHA256 `b87b96efa0e8971d89f8fe5bdad5b63d8119831d3f09430122d51770dc851fbd`, 11 feature e 10 schizzi, 4.108 triangoli visibili, zero errori. Mouse2: copia SHA256 `7ffa64559f38820ed0f070fa233efc27a2b5ea82ee749e04817a756cb1ae92ce`, 36 feature e 14 schizzi, zero errori. Nell’assieme si riproducono i quattro errori già presenti per importati senza sorgente agli indici 274/281/288/295; nessun nuovo errore e firme identiche.

Il lotto CT0578 di otto processi confronta il candidato esterno con il kernel precedente congelato, tenendo identiche le altre ottimizzazioni locali. Una successiva rigenerazione collegata alla libreria effettiva della build finale conferma gli stessi byte; il confronto mouse2/assieme usa direttamente la build finale.

## Verifiche

- 192 confronti con la vecchia funzione esaustiva: intersezioni e parametri bit identici, curve razionali/polinomiali, gradi 1/2/3/5, 8/16/33/97 tratti, nodi ripetuti, range interi/parziali, due richieste concorrenti.
- Due nuove regressioni permanenti in `kernel/tests/test_intersect.cpp`: intersezioni note di polilinee pesate, nodi, assi/ordine invertiti, range parziali, contatto tangente, curve separate, concorrenza e cancellazione. **2/2 passano anche sul kernel precedente.**
- **228/230 test kernel selezionati superati.** I due fallimenti di raccordo (`BlendVanishingAtTangentMitreEnd`, `BlendArcsMeetingSegments`) sono riprodotti sul kernel precedente con gli stessi controlli; nessun test disabilitato o soglia modificata. Non è la suite completa.
- **44/44 CTest applicativi superati**, 13,15 secondi, su build completa Release Linux.
- QA nativa mouse2: Rigenera tutto, modifica faccia/raggio del raccordo, Undo/Redo, tre anteprime e rilettura di una copia indipendente. Zero pixel diversi dopo ricreazione della cache grafica nei tre confronti e zero errori OpenGL; finestra ispezionata. Nessuna correzione generale del renderer dichiarata.
- Mappa aggiornata, controllata e ispezionata: 496 file, 6.739 relazioni, 235 unità C++, zero diagnostiche, 80.900 siti entro le righe. Quattro confini di chiamata verificati in entrata/uscita, include distinti e chiamate interne della gerarchia risolte. Artefatti HTML/JSON ignorati da Git.

## Stato e seguito

Applicazione completa e runner compilati: `/home/myungit/Linux_CAD_forFree/forgecad-linux-solver/forgecad`. Installazione desktop e main preservati. Nessuna build macOS o nuova misura GPU dichiarata; nessun commit/push.

Dati e impronte nel [JSON](curve-intersections-2026-10-27.json). Sonde, binari precedenti, log e immagini in `forgecad-linux-solver/benchmarks/ct0578-followup-2026-10-27`, ignorati da Git. I CAD originali non sono stati scritti; QA e rigenerazioni usano identità indipendenti.

Restano da affrontare i costi di suddivisione delle superfici e generazione dei semi, le proiezioni residue e il raffinamento del loft. Il prossimo intervento va misurato separatamente; il 10× globale resta aperto.
