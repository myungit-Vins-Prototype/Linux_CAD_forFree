# Prove K2 integrato — 15 novembre 2026

[Rapporto principale](../sweep-k2-completamento-2026-11-15.md).

- `production-build-final.log`: build completa PASS. `production-build.log` conserva il primo errore nei due helper del test autonomo, poi corretti senza cambiare le geometrie.
- `production-global-tests.log`: 30/30 regressioni del target senza Qt/OCCT.
- `production-kernel-results.json` e log dei filtri: 19 Sweep più 5 consumatori, tutti PASS; con il target nuovo sono 54 test distinti.
- `production-ctest.*`, elenco e selezione: 8/8 test Qt in ambiente temporaneo offscreen/Fusion.
- `production-sanitizer-*`: 22/22 ASan, UBSan e LeakSanitizer, sorgenti strumentati specificati nello script. Il resto della libreria kernel non è strumentato.
- `production-architecture-*` e audit agente: generazione/check, descrizioni, connessioni e HTML incorporato. `architecture-consulted.json` conserva la consultazione iniziale richiesta.
- `source-audit.json` e `binary-hashes.json`: confronto dei file versionati iniziali e hash dei binari finali. File preesistenti non sovrascritti da baseline Git.
- `agente-*` e `gate-*`: prove indipendenti, prestazioni e isolamento delle rappresentazioni. I log prima della variante ibrida sono diagnostici storici, non esiti finali.
- Sorgenti ausiliari `.cpp.txt` e script `.py.txt`: prove riproducibili fuori dai target di produzione. Le regressioni integrate sono in `kernel/tests/test_sweep_*.cpp`.

Nessun CAD originale incluso o modificato. Nessun nuovo test del renderer. I PASS di staging sono distinti dai PASS della produzione integrata.
