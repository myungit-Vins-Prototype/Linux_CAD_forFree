# Solver e persistenza — 15 novembre 2026

**Aggiornamento successivo:** S1/S2 corretti: numeri non finiti rifiutati atomicamente, riduzione angolare in tempo costante e verifiche denso/sparso/codec. [Implementazione, prove e limiti](../review-fix-next-2026-11-15.md). Il testo seguente conserva lo stato della review iniziale.

Due problemi distinti riprodotti sul solver e sul codec reali. La prova crea un piccolo documento in memoria con due segmenti, lo serializza con `encodeDocument`, lo rilegge con `decodeDocument` e passa lo schizzo ripristinato a `solveSketch`. Nessun documento utente modificato.

## S1 — P2 — Angolo infinito o molto grande: ciclo che non può progredire

**Posizione:** `cad_constraints.cpp:235–238`; chiamata a `:889`; lettura del valore persistito `cad_document_io.cpp:289`.

`wrapAngle` sottrae/aggiunge ripetutamente `2π`, senza verificare finitezza o avanzamento. Con infinito il valore non cambia; anche con `c.value=1e20` gradi la risoluzione del double è superiore al passo e l'angolo resta identico. Il ciclo diventa realmente infinito. Valori finiti meno grandi richiedono un numero di iterazioni proporzionale all'angolo.

**Prova:** il writer e il reader accettano sia `inf` sia `1e20`; ciascuna chiamata al solver è interrotta dal processo di controllo dopo 3 secondi. La causa di non avanzamento è verificata nel codice; il timeout da solo non viene usato per dedurre un ciclo infinito.

**Impatto e limite:** un documento con una quota angolare non valida può bloccare il ricalcolo. Non è provato un percorso di inserimento ordinario dei valori estremi nell'editor: i campi UI impongono limiti. Il difetto riguarda il confine di validazione del documento e l'API del solver, non ogni documento aperto.

**Correzione proposta:** rifiutare valori non finiti nel codec e nell'API; definire un intervallo angolare valido e normalizzare in tempo costante con gestione esplicita degli estremi. Non basta aggiungere un contatore al ciclo senza definire la semantica.

## S2 — P2 — Un vincolo NaN viene dichiarato soddisfatto

**Posizione:** `cad_constraints.cpp:2175–2178`; accettazione finale `:2268–2274`; filtro `wellFormed` a `:945–967` e codec `cad_document_io.cpp:289`.

La norma dei residui usa `std::max(worst, std::fabs(v))`. Se `v` è NaN, il confronto è falso e `worst` resta 0. Non essendoci validazione generale di finitezza, una quota Distance con NaN viene trattata come un vincolo risolto. Anche il controllo dei blocchi senza variabili usa un confronto che non rileva NaN.

**Prova dopo round-trip del documento:** `encode='' decode=''`, valore ripristinato `nan`, `solveSketch: ok=1 residual=0 iterations=0`, `constraintError=0`. La quota non rappresenta alcuna misura valida ma non viene segnalata.

**Correzione proposta:** validare valori e coordinate al confine del documento e prima del solve; ogni residuo/Jacobiano non finito deve produrre un fallimento esplicito e atomico. Verificare anche `constraintError` per evitare che la diagnostica nasconda il problema.

## Prove e controlli di contesto

- `prove/solver-probe.cpp.txt`: harness; `prove/solver-results.txt`: esito completo.
- Il probe è compilato C++17 con i flag Qt6 del sistema e collegato agli oggetti correnti del runner `forgecad_view_tests`, sostituendo solo `tests/test_viewport.cpp.o` con il suo main. Il comando di collegamento deriva da `forgecad-release/CMakeFiles/forgecad_view_tests.dir/link.txt`, eliminando l'opzione che scrive il file di dipendenze.
- Il controllo Python usa `subprocess.run(..., timeout=3)` separatamente per `nan`, `inf`, `huge`; termina soltanto il proprio processo di prova.
- Passano i test ordinari di solver partizionato, algebra lineare, editing schizzo, lettura documento, precisione, parser espressioni e accesso documenti. I test preesistenti non coprono i due casi qui riprodotti.
- Controllata la struttura dei parser STEP/IGES e del body binario; i controlli di cicli/dimensioni aggiunti dopo la review del 2 novembre e le relative regressioni risultano presenti. I vecchi problemi non sono riproposti come attuali senza una nuova prova.
