# Prove delle correzioni K7/K8/K9/PRE4/UI7

Rapporto principale: [correzioni e limite K10](../review-followup-fix-2026-11-15.md).

- `kernel/`: prove private dell’agente, controprova precedente alla patch, log della regressione intermedia d’area e correzione finale. **`covariance.log` (5/5)** e `rotation-fixed.log` sono i risultati privati finali; `final.log` (4/4) riguarda la versione precedente al controllo sulle rotazioni. Il rapporto conserva questa cronologia.
- `ui/`: harness Qt reale, patch, log positivo e due controprove con le implementazioni originali. Non sono schermate OpenGL.
- `independent/`: review delle formule, due probe indipendenti e verifica K10. Risultati finali autorevoli in `final-results.json`, `final-torus-cancel-k10.log`, `final-edges-extrusion.log`; 60 controlli positivi. I log `baseline.log`, `patched.log` ed `edges-patched.log` sono intermedi. K10 resta riprodotto, non viene contato fra i controlli positivi delle correzioni.
- `root/`: compilazione completa, test integrati sui binari finali, controverifica di `offsetFaces`, rigenerazione e audit bidirezionale dell’architettura. `full-build.log` è la prima build; **`final-build.log`** è la build della patch finale.
- `implementation.patch`: delta dei sorgenti rispetto all’inizio di questo intervento, comprese le modifiche di integrazione del coordinatore; non è il diff complessivo rispetto a HEAD.
- `hashes-before.json`, `source-audit.json`: controllo delle 1346 sorgenti/risorse iniziali per distinguere queste modifiche da quelle preesistenti. `final-hashes.json` identifica sorgenti e binari verificati; `SHA256SUMS-all` identifica le prove archiviate.

## Ripetizione sulla build corrente

Dalla radice del repository:

```sh
cmake --build forgecad-release --parallel 2
ctest --test-dir forgecad-release -R '^forgekernel_offset_validation_tests$' --output-on-failure
forgecad-release/kernel/forgekernel_tests Offset
forgecad-release/kernel/forgekernel_tests Shell
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic QT_STYLE_OVERRIDE=Fusion QT_QPA_OFFSCREEN_NO_GLX=1 XDG_CONFIG_HOME=/tmp/forgecad-review-repeat-settings forgecad-release/forgecad_view_tests --function-panels-review
python3 tools/generate_architecture.py --check
```

Il target di validazione offset non dipende da Qt o OCCT. Il runner generale `forgekernel_tests` richiede la configurazione con i confronti OCCT; le prove kernel nuove rimangono utilizzabili senza quella dipendenza.

I sorgenti e script dei probe sono conservati con estensione `.cpp.txt`/`.py.txt` per evitare di catalogarli come codice di produzione. Copiarli in una directory temporanea, ripristinare l’estensione e adattare i percorsi `root/out` degli script alla propria macchina. I comandi di compilazione e collegamento derivano da `compile_commands.json`/`link.txt` della build configurata. Non riutilizzare binari o cache CMake fra host.

Per la controprova precedente alla patch ricostruire i file applicando al contrario `implementation.patch` **soltanto in una copia separata**, quindi ricompilare. Le patch degli agenti sono sottoinsiemi della stessa modifica: non applicarle in aggiunta alla patch complessiva. I log registrati provengono dalla baseline locale con le correzioni precedenti già presenti, non dal solo HEAD.

K10 è ripetibile dal probe indipendente; `source_bounds.py.txt` ricostruisce i limiti di curvatura con aritmetica razionale sui controlli double effettivi. I test finali positivi non costituiscono una certificazione del fitter per tutte le spline.
