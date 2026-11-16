# Prove della prosecuzione review — 15 novembre 2026

[Rapporto consolidato](../review-followup-2026-11-15.md). Copie documentali; nessun file qui viene aggiunto ai target di produzione.

- `kernel/`: rapporto dell'agente, probe geometrico/cancellazione, build e cinque log. R1→K7, R2→K8, C1→K9 nel rapporto consolidato.
- `preview/`: harness reale per PRE4/UI7, flag di compilazione ricavati dalla build, log, controllo sorgenti/oggetti e contesto architetturale. `discarded-mouse-attempt.cpp.txt` è un tentativo scartato, non prova di un click desktop.
- `lifetime/`: inventario delle 22 orfane, rapporto, quattro test lifetime/cache e cinque revisioni/multifoglio, comandi/esiti.
- `root/`: controverifica indipendente di K7/K8 (inclusa cucitura attiva), verifica indipendente PRE4/UI7, quattro regressioni kernel e log.
- `source-hashes-start.json`: 1289 file locali iniziali, inclusi cambiamenti preesistenti e file non tracciati, prima di questa passata. `preservation.json` documenta le sole differenze di questa review.

## Ripetizione

Ripristinare i file `.cpp.txt`, `.py.txt` e `.sh.txt` rimuovendo il solo suffisso finale `.txt`, nelle directory `/tmp/forgecad-review-followup-20261115/{kernel,preview,lifetime,root}`. Gli script contengono il percorso locale `/home/myungit/Linux_CAD_forFree`; adattarlo se la build si trova altrove. Richiedono la build Release esistente con oggetti di `forgecad_view_tests` e libreria kernel correnti.

1. Kernel: `python3 .../kernel/build.py`, poi `probe bump`, `probe bump 1000`, `probe torus`, `probe torus-body`, `probe fitcancel`.
2. Qt: `sh .../preview/run.sh`. `QSettings` usa organizzazione/app dedicate e `QTemporaryDir`; per isolamento aggiuntivo configurare XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME e XDG_RUNTIME_DIR sotto una nuova directory `/tmp`. Impostare `QT_QPA_OFFSCREEN_NO_GLX=1` oltre ai flag dello script. La controverifica del coordinatore li ha impostati.
3. Lifetime/persistenza: `python3 .../lifetime/repeat_tests.py` e `python3 .../lifetime/repeat_revision_tests.py`. Il secondo isola tutte le directory XDG. Il test multifoglio emette QA temporanea anche in `/tmp/forgecad-multisheet-native-qa`.
4. Controverifica geometrica: `python3 .../root/build_check.py`; regressioni: `python3 .../root/offset_regressions.py`.

I probe descrivono il comportamento difettoso corrente: il successo del processo significa che la riproduzione è riuscita, non che il difetto è corretto. Dopo una correzione trasformarli in regressioni con aspettative aggiornate.

Nessun binario o oggetto compilato è copiato nel repository. Gli hash degli agenti si riferiscono ai nomi originali prima del suffisso documentale; il manifest finale contiene gli hash delle copie consegnate. Nessuna nuova build app, prova OpenGL, sanitizer o certificazione della suite generale.
