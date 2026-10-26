# Specchi storici e riparazione delle feature — 26 ottobre 2026

> Seguito: la soluzione delle due voci descritta qui è stata sostituita da [una sola feature nella storyboard](pattern-single-story-2026-10-26.md). Questo rapporto conserva le verifiche della fase precedente.

Correzione comune su `solver-mesh-gpu` e `main`, compilata e verificata su Linux. Nessuna scrittura nei file CAD, nei check-in o nelle sessioni dell'utente. Build desktop installata invariata; sorgenti locali, senza commit/push.

## Specchio dopo Rigenera tutto

Regressione introdotta dal supporto ai risultati separati delle ripetizioni, confermata sul check-in originale di mouse2 (`96178ed1`, oggetto `4b10f45f...`). Il caricamento conserva 31 feature e lo specchio ha due shell/14 facce. `rebuildAllFeatures` trasformava implicitamente `patternComponent=-1` in `0`: il risultato completo veniva sostituito dalla prima metà, mentre la seconda veniva aggiunta in coda, dopo i consumatori. Restavano riferimenti ai bordi dell'aggregato precedente.

Prima del fix la rigenerazione produce 32 feature e tre errori: superficie planare28 con loop aperto, solido cucito29 senza superficie, raccordo30 senza base. Il benchmark prestazioni `--rebuild` svuotava le cache ma non esercitava questa conversione del comando UI, come già delimitato nel relativo rapporto. Il difetto è nel codice comune Pattern, non nelle nuove ottimizzazioni di proiezione o lunghezza.

Ora rigenerazione e modifica preservano il contratto aggregato dei Pattern legacy: non cambiano da sole la struttura su cui sono salvati i riferimenti. Le nuove ripetizioni conservano invece i risultati separati e le esclusioni. Nessuna rimappatura approssimata a spigoli o copie diverse. Nel vecchio check-in la rigenerazione torna a zero errori e 31 feature; il nuovo file aggiustato, basato sul check-in `1f6bd502`, conserva 33 feature, i due risultati separati e la cucitura che li ricompone, senza errori.

## Riparazione senza cancellare

Difetto indipendente riprodotto: la scelta dei riferimenti consultava la visibilità alla fine della storia. Una base precedente nascosta da una lavorazione successiva rimaneva invisibile e non selezionabile durante la modifica di una feature in errore.

`beginReferencePick` prepara ora un contesto transitorio con risultati e operandi validi prima della feature. Rendering ed eleggibilità usano lo stesso contesto, anche dopo Azzera o senza anteprima valida. Esc/chiusura ripristinano la scena finale; nessuna modifica ai flag di visibilità salvati. Resta vietato scegliere geometria successiva alla feature.

Il test permanente apre il pannello reale di una superficie planare fallita, azzera i riferimenti, sceglie quattro nuovi bordi e conferma. La feature recupera la geometria mantenendo identità e posizione nella storia; Undo/Redo verificati. La selezione viene consegnata al callback reale del picker; non si dichiara una prova automatica del clic a pixel su questa fixture. Prima del fix entrambi i nuovi test fallivano, dopo passano.

## Le due voci del nuovo specchio

Sono la funzione madre e un corpo risultato derivato, non due calcoli di specchio indipendenti. La storia le etichetta ora come `Specchio...` e `Risultato 2 di Specchio...`; il tooltip chiarisce che Modifica agisce sulla madre. Le voci derivate rimangono passi interni dell'attuale modello: non viene dichiarato un nuovo raggruppamento cronologico in un solo evento.

## Verifiche

- App e runner Release compilati su entrambi i branch. 28/28 CTest pertinenti PASS sullo sviluppo (6,32 s), 27/27 su main (6,14 s), inclusi i due nuovi casi, risultati separati/esclusioni, storia, selezione, pannelli e Undo/Redo. Non è la suite kernel completa; nessuna nuova modifica funzionale al kernel in questo intervento.
- Due documenti reali su entrambi i branch: vecchio check-in integro e copia aggiornata. Rigenera tutto termina con zero errori su tutti e quattro i casi.
- OpenGL nativo xcb: tre riaperture dello specchio per ciascun caso, due shell nell'anteprima, zero errori GL e zero pixel diversi dopo ricreazione della cache. Confronto entrambe le metà/sola immagine specchiata: circa335.800–335.966 pixel diversi. Immagini ispezionate sullo sviluppo; il corpo completo e l'anteprima risultano presenti.
- L'assenza della seconda metà nell'anteprima non è stata riprodotta come ulteriore errore autonomo del renderer: già prima del fix la sonda geometrica trovava due shell. Nessun renderer modificato o correzione generale dell'intermittenza GL precedente dichiarata.
- Mappe aggiornate: sviluppo496file/6736relazioni/235TU, main470/6584/230, zero diagnostiche; controlli di aggiornamento, righe e quattro confini chiamante/chiamato in entrambi i versi PASS. Include separati dalle chiamate risolte. Nessuna nuova verifica visiva delle pagine architetturali dichiarata.
- Originali verificati immutati con SHA256, incluse entrambe le revisioni archiviate. Non sono state invocate operazioni di checkout/check-in o sostituzione di file. macOS non compilato/collaudato per questo seguito.

Non viene dichiarata la riparazione automatica di eventuali file intermedi già salvati con conversione parziale; sono verificati il vecchio check-in integro e il nuovo file sistemato dall'utente. Escludere davvero un'occorrenza usata può lasciare riferimenti da ridefinire: il programma non deve assegnarli silenziosamente a un'altra copia.

Dati/impronte: [JSON](pattern-history-repair-2026-10-26.json). Log, sonde e immagini locali in `forgecad-linux-solver/benchmarks/pattern-history-repair-2026-10-26`, ignorati da Git. Nuova regressione permanente: `tests/test_pattern_history_repair.h`; aggiornato il vecchio test che si aspettava la conversione implicita, comportamento responsabile della regressione.
