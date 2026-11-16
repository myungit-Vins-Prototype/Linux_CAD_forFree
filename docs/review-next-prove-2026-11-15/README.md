# Prove della prosecuzione review — 15 novembre 2026

Rapporto principale: [correzioni e limiti](../review-fix-next-2026-11-15.md).

- `kernel-*.log` ed elenco JSON: runner ricompilato, 74 casi distinti/75 esecuzioni PASS.
- `solver-*`: matrice di nonfiniti/grandi angoli e benchmark diagnostico agente; dettagli dei file strumentati nel rapporto.
- `ui-*`: harness agente lifecycle34verifiche, eseguito su oggetti reali Qt; sorgente conservato come testo fuori dai target.
- `k4-*`: varianti geometriche e piegamenti veri, controllo della sezione continua.
- `ctest-primo.*`: prima batteria combinata33/34, fallimento `selected guide restores its own controls` riprodotto anche sulla baseline, causato dal confronto esatto55==55,000000000000007. I due log `loft-confronto-*` documentano il caso; il test ora usa errore assoluto<1e-12.
- `ctest-finale.*`:34/34PASS dopo la correzione del solo test; build completa in `build-final.log`.
- `architecture-*`: generazione/check, dati HTML e chiamate verificate nei due versi.
- `source-audit.json`: hash iniziali/finali dei23file versionati modificati in questo intervento; nessuna variazione inattesa negli altri file versionati confrontati. `binary-hashes.json`: eseguibili verificati.
- `run-ctest.py.txt`: selezione e ambiente isolato della batteria; percorso temporaneo riferito a questa sessione.
- `schermate/`: pannello reale catturato da Qt offscreen nei temi chiaro/scuro e menu. Le due catture del pannello sono state ispezionate: informazioni e successo leggibili; tutti e quattro i ruoli sono verificati anche dal rapporto di contrasto in test. Non sono catture della viewport OpenGL.

Le log degli agenti distinguono staging e repository integrato. Un PASS isolato non viene usato come sostituto della build/test combinati. Nessun file CAD originale incluso o modificato.
