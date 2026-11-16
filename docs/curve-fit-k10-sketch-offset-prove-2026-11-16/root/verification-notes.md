# Condizioni e tentativi conservati

`architecture-build.log` appartiene alla prima generazione, interrotta dopo la modifica documentale finale dell'header: il suo hash globale dei contratti era superato. Non costituisce una verifica conclusiva. `architecture-final-build.log` e il controllo finale riguardano gli input finali; un eventuale refresh successivo usa il database CMake definitivo.

Il CMake del test dedicato è stato alleggerito a QtCore dopo la prima build: nessun simbolo Gui è richiesto dai tre sorgenti. La build completa viene ripetuta dopo questo aggiornamento e dopo l'ultimo commento dell'header. Le modifiche avvengono soltanto nell'adattatore dello schizzo; l'archivio del kernel resta identico alla baseline.

I tre tentativi sanitizer sono distinti in `tests/results.json`: sandbox (casi riusciti, LSan non eseguibile sotto ptrace), QtWidgets fuori sandbox (183 byte del driver NVIDIA, nessuno stack del prodotto), infine QtCore con gli stessi sei oggetti strumentati (27/27, exit0, nessuna soppressione e nessuna diagnostica). Il risultato finale non certifica tutti i moduli dell'applicazione.

I dati utente e i documenti CAD originali non sono usati come destinazione delle prove; la regressione UI usa directory temporanee per impostazioni, dati e cache. Qt offscreen non dimostra la resa dei pixel OpenGL.

CTest ha completato 14/14 suite con exit0. Il wrapper di raccolta attendeva il vecchio formato con il conteggio zero fallimenti; questa versione stampa soltanto `100% tests passed out of 14`. Il primo wrapper ha sollevato un assert dopo il completamento riuscito. Corretto il parser e contate indipendentemente tutte le 14 righe Passed dal log originale; nessun test ripetuto, modificato o indebolito. Dettagli in `ctest-wrapper-format.log` e `ctest-result.json`.
