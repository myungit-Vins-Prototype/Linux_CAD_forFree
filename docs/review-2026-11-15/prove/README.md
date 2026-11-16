# Prove della review del 15 novembre 2026

Questa cartella conserva sorgenti dei riproduttori e output effettivi; non contiene eseguibili né modifiche al software. Le estensioni `.cpp.txt` e `.py.txt` evitano che i probe entrino nei moduli/target del progetto. Per usarli, copiarli in una cartella temporanea con l'estensione originaria. Comandi e limiti sono nei rapporti `../kernel.md`, `../memoria.md`, `../solver.md`, `../interfaccia.md`, `../anteprime.md` e `../orfane.md`.

- `probe_*`: sei casi geometrici e dettagli della cucitura UV; `relink_current.py.txt` ricollega il runner in `/tmp`, senza aggiornare l'app.
- `recognition_race*`, `bspline_copy_race*`: output ASan/TSan, non una sessione LeakSanitizer completa.
- `solver-*`: documento in memoria con quota non valida, codec e solver reali; timeout limita solo i processi di prova. `solver-build.py.txt` ricostruisce e avvia i tre modi dalla radice del repository.
- `ui-*`, `contrast-light.png`: widget e pannelli reali Qt offscreen; lo screenshot è un componente di contrasto, non una cattura dell'intera app.
- `preview-*`: dialoghi reali di estrusione/Loft e policy delle sorgenti mapping; esito ripetuto dal coordinatore. Il terzo caso non certifica pixel OpenGL.
- `orphans-*`, `inventory_orphans.py.txt`: ricerche lessicali e chiamate/riferimenti risolti per i simboli candidati; nessuna misura nuova di eliminazione del linker.
- `baseline-ctest.log`, `recheck-ctest.log`, `drawing-ctest.log`: batterie mirate; i primi due conservano anche i fallimenti di ambiente.
- `recovery-outside-sandbox.log`: recupero documenti PASS in ambiente isolato fuori sandbox, mentre nel sandbox era in timeout.
- `kernel-tests-baseline-precedente.log`: 475 test sul runner preesistente del 14/11; **non certifica** i sorgenti kernel del 15/11. `current-sweep-tests.log`: 16 test sul runner corrente.
- `source-hashes-start.json`: SHA256 dei 1.037 file versionati letti all'inizio; i file non versionati non sono inclusi. Serve a distinguere cambi durante la review. Non rappresenta da solo la copertura della lettura manuale.

La build configurata conteneva 221 CTest. Sono stati verificati 45 test distinti nelle batterie mirate, oltre ai probe e al filtro Sweep; non l'intero insieme dei 221. I comandi dei test provengono dai `CTestTestfile.cmake` della build, copiati in `/tmp` per tenere i log fuori dalla cartella in sola lettura.
