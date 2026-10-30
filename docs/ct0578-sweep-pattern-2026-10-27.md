# CT0578: ripetizione circolare di Sweep 4 — 27 ottobre 2026

Implementato sullo sviluppo solver-mesh-gpu. La scelta "La funzione" ora supporta gli sweep solidi che si uniscono o si sottraggono a corpi associati. Il pannello e il costruttore usano lo stesso criterio `patternFeatureSupported`; il selettore mostra "Corpo 1 — Sweep 4". Il corpo di base resta fermo e vengono ripetuti solo gli strumenti della funzione. Gli sweep autonomi o di superficie restano ripetibili come corpi. Errori specifici della ricostruzione del profilo conservati.

## Due cause distinte

1. Pannello e costruttore escludevano esplicitamente Sweep dalla ripetizione della funzione. Riprodotto nel file:5 schizzi / 4 feature, Sweep4 indice3 e risultato del corpo 1, mergeOperation2/mergeBodies{1},19facce,0errori.
2. Una volta ammesso lo sweep, le copie a180/270 gradi incontravano un errore reale del kernel. `Marcher::singularBranches` risolve la forma quadratica del contatto: con diagonali nulle e termine misto non nullo divideva per0, producendo un vettore non finito. Corrette le radici in coordinate omogenee scalate, scegliendo il numeratore senza cancellazione. Stesse equazioni e tolleranze, cancellazione preservata; nessun ripiego approssimato o modifica all'ordine delle booleane.

## Verifiche concluse

- Applicazione completa e runner Release compilati;119/119 casi kernel pertinenti superati: intersezioni con riferimenti OCCT, booleane, sweep, trasformazioni, algoritmi di superficie, B-spline, offset, shell e tessellazione. Nuova regressione analitica: piano z=0 e sella z=x*y+eps*(x*x-y*y), eps0 / ±1e-15 / 1e-7, superfici scambiate, verifica di entrambi i rami. Standalone prima fallisce su vettore nullo, dopo trova2 curve / 1 punto singolare.
- 25/25 CTest selezionati superati. Nuove regressioni sweep in unione/sottrazione attorno a Y con volumi analitici indipendenti, scelta nel pannello, esclusioni anche dell'originale, Undo/Redo, codec con/senza cache, rigenerazione ed errore del profilo. Prima esecuzione24PASS/1NotRun per target dipendenze non costruito; compilato quel target e25/25 finali PASS. Nessun test disabilitato o criterio allentato. Non sono le suite complete.
- QA nativa su due copie congelate del CT0578:4 istanze circolariY, un solo corpo finale, sorgente Sweep4 byte-immutato dopo creazione, anteprima geometrica e mesh presenti, topologia valida. Esclusione1 istanza, Undo/Redo e riapertura della definizione e della cache con rigenerazione superati.0 errori GL. Immagini del pannello e dell'applicazione dopo ridisegno ispezionate; le catture grezze del framebuffer del widget possono includere il compositore vetro e non sono un confronto pixel del renderer.

| Volume, prima copia | Valore |
|---|---:|
| Sweep4 singola | 1672.049471600 |
|4 tagli totali | 1666.618638413 |
|3 tagli dopo esclusione | 1668.428916142 |

## Input e istruzioni

Prima copia SHA256 `440c8c7f1d78d39a7d8e530dbe23bf71774f9fbc6ce6c5bfd93961498a2a4a2d` (58118byte). L'originale e' stato salvato nuovamente durante le prove: conservata e verificata anche la copia piu recente SHA256 `3db83144877e68655120eeb9e6bf623f5872c12fd05bdca2977ae5895ffd5d0b`. Entrambe hanno Sweep4 come risultato finale del corpo 1. Le sonde modificano soltanto documenti con identita indipendente in memoria; nessuna scrittura del CAD, archivio, sessione o lock originale. Copie di benchmark immutate; originale finale uguale alla copia piu recente.

Riavviare `/home/myungit/Linux_CAD_forFree/forgecad-linux-solver/forgecad`; scegliere **Serie circolare**, **Corpo1 — Sweep4**, **Ripeti: La funzione**, **Asse Y** e numero delle istanze. Il numero comprende l'originale;4 istanze su360 gradi danno4 tagli totali. Per duplicare tutto il risultato geometrico resta la modalita "Il corpo intero".

Mappa rigenerata: 496file/6732relazioni/235TU,0 diagnostiche; --check superato,80750siti con righe valide,5confini verificati nei due versi e include distinti. HTML/JSON generati ignorati. Artefatti: `forgecad-linux-solver/benchmarks/ct0578-pattern-2026-10-27` (input, sonde, log, JSON, catture e confronto prima/dopo). Nessuna build nativa macOS o misura di velocita dichiarata; installazione desktop preservata. Nessun nuovo porting su main, commit o push; tutto il lavoro locale preesistente conservato.

Viste architetturali `cad_pattern.cpp` e `kernel/fk_marching.cpp` aperte e ispezionate; descrizioni effettive del catalogo verificate.

## Porting main successivo

Su richiesta esplicita riportata la sola correzione CT0578 sul checkout aggiornato main49d69b0, preservando la soppressione locale. Applicazione completa Release Linux e runner compilati:118/118kernel selezionati,25/25CTest e QA nativa CT0578PASS; rigenerazione della nuova copia con62occorrenzePASS. Mappa main aggiornata e verificata. Dettagli in `docs/ct0578-main-integration-2026-10-27.md`; nessun ulteriore commit/push.
