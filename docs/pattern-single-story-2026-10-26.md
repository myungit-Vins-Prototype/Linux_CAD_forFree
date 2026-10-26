# Una sola feature per specchio e ripetizione — 26 ottobre 2026

Correzione comune a `solver-mesh-gpu` e `main`, compilata e verificata su Linux.
La storyboard mostra una sola funzione; i risultati disgiunti restano corpi indipendenti in **Corpi**, sia solidi sia superfici. Sostituisce la precedente soluzione delle etichette «Risultato», descritta nel rapporto sulla riparazione della storia.

I record dei risultati mantengono identità e geometria proprie, ma appartengono allo stesso evento. Cursore, modifica, soppressione, cancellazione, inserimento e riordino operano sull'intero gruppo. I risultati vengono ordinati dopo la madre e prima dei consumatori, rimappando operandi, tutte le famiglie di riferimenti e appoggi/Pierce senza modificare gli snapshot Undo. Uno schizzo ancorato a un risultato resta dopo la funzione che lo genera.

La normalizzazione dell'ordine avviene dopo la decodifica completa del documento, insieme alle cache geometriche: non durante la lettura separata delle definizioni. Il codec resta compatibile, senza cambiamento di versione. I Pattern legacy conservano il risultato aggregato su cui sono salvati i riferimenti; non vengono convertiti implicitamente.

## Verifiche

- Build Release di app, runner e test riferimenti completate su entrambi i branch.
- Sviluppo:30/30 CTest pertinenti PASS in6,64s; main:29/29 PASS in7,61s. Non è la suite completa del kernel.
- Nuovo test `patternSingleStory`: solidi/superfici, una riga nella storia e due in Corpi nell'albero reale, cursore atomico, operazioni dal secondo corpo, Undo/Redo, codec, aggiunta copie con consumatore futuro, inserimento e riordino. Test delle famiglie: rimappatura completa e schizzo sul secondo risultato; quest'ultimo falliva prima della correzione dell'ancoraggio e ora passa.
- Mouse2 recente:33record geometrici,32eventi di feature, un solo Specchio e nessuna riga derivata. Vecchio check-in:31record/eventi, contratto storico conservato. Rigenera tutto:0errori nei due documenti su entrambi i branch.
- QA OpenGL nativa: tre riaperture per documento/branch (12totali), anteprima con2shell; cache ricreata con0pixel diversi e0erroriGL. Il confronto completo/sola immagine specchiata cambia oltre335.000pixel. Questa verifica non dichiara risolta ogni intermittenza storica del renderer.
- Mappe architetturali rigenerate, controllate e ispezionate in Firefox headless: blocco modificato e collegamenti leggibili. Dieci confini di chiamata verificati in entrambi i versi e tutti i siti entro i limiti di riga; HTML/JSON generati ignorati da Git.
- Fixture UI nativa ispezionata: due corpi visibili e due sole feature (Blocco + Specchio). Screenshot `ui-two-bodies-one-feature.png` negli artefatti.

Sorgenti e impronte di eseguibili/input nel JSON omonimo. Log, sonde e immagini locali in `forgecad-linux-solver/benchmarks/pattern-single-story-2026-10-26`.
Nessun file CAD, check-in o sessione utente scritto; SHA256 degli originali immutati. Push non eseguito e app desktop installata invariata; commit locali della sola correzione richiesti dall’utente, stato nel registro Git. Kernel e pipeline main precedenti conservati; nessuna nuova misura di velocità. Build macOS non eseguita.
