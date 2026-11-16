# Prove cache B-spline — 15 novembre 2026

- `m1-asan-isolata.log`: 200 round × 8 thread, header corretto con libreria kernel originale; comandi in `m1-comandi.txt`.
- `m2-tsan.log`: riproduttore storico su copia corretta, exit 0 (TSan non produce output quando non rileva errori).
- `regressioni-asan.log` e `regressioni-tsan.log`: cinque casi PASS; comandi effettivi e limiti in `comandi-sanitizer.txt`. Sorgente del test integrato in `kernel/tests/test_bspline_cache.cpp`.
- `configure.log` e `build.log`: configurazione conservata e build completa exit 0.
- `ctest.log`, `ctest.xml`, `selected-tests.json`: i 47 test pertinenti e i relativi esiti. Config/cache/data XDG isolati in `/tmp/forgecad-cache-fix-20261115/test-environment`; Qt offscreen.
- `kernel-tests.log`: esecuzione aggiuntiva del runner kernel; consultare il rapporto per distinguere completamento da limite temporale.
- `architecture.log`: generazione mappa; controllo `--check` e archi/HTML effettuati dopo generazione.
- `source-hashes-start.json`: snapshot dei file versionati per preservare modifiche preesistenti, non misura della copertura dei test.

I riproduttori storici prima delle correzioni sono in `docs/review-2026-11-15/prove/`. I sanitizer strumentano i moduli interessati, non l’app intera; nessuna verifica LeakSanitizer. Log dei processi di prova, nessun documento utente incluso.
