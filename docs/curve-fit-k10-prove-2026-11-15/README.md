# Prove K10 — 15 novembre 2026

Rapporto: [correzione e limiti](../curve-fit-k10-2026-11-15.md).

- `independent/audit.md`: verifica matematica/sorgente finale; `final-results.json` confronta 13 geometrie prima/dopo. I massimi densi sono controprove, non il certificato continuo.
- `tests/report.md`: indagine iniziale e inventario dei 24 chiamanti; `test-report.md` e manifest descrivono le regressioni integrate.
- `root/build-final.log`, `root/kernel-results.json`, `root/ctest-result.json`, `root/test-summary.json`: esiti della build completa e dei runner finali. La prima build è separata in `build.log`.
- `kernel/sanitize/README.md`: strumentazione mirata e hash dei due passaggi; i primi log sandbox terminarono con errore LSan/ptrace e sono conservati, superati dai passaggi esterni con exit 0. Il resto dell’archivio non è strumentato.
- `root/architecture-audit.json`: confronto HTML/JSON e relazioni entranti/uscenti, chiamate/include/riferimenti distinti. La seconda generazione rianalizza le due unità cambiate durante il primo passaggio.
- `root/final-audit.json`: hash sorgenti/binari finali, conservazione dei file preesistenti, flag aritmetici e collegamento del nuovo runner senza Qt/OCCT.

Sorgenti ausiliari salvati come `.cpp.txt`, `.h.txt`, `.py.txt` per non introdurli nell’inventario del software. Le ricette mantengono i percorsi dello staging originale; per rieseguire ricreare lo staging o adattare quei percorsi. Nessun binario o archivio statico incluso. `root/change.diff` è relativo all’inizio di questo intervento, non alla base Git che contiene anche le correzioni precedenti.
