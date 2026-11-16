# K1 — frame MinimalTwist

Patch stretta `fix.patch`, snapshot iniziale `original.cpp` e hash `original.sha256`.
Nessun sorgente del repository modificato. Parent integra anche snippet test.

Consultati AGENTS.md, CLAUDE.md, review kernel, politica tolleranze e graph.json:
responsabilità PathFrames → sweep → sweepRegions/Chains/Sheet; chiamanti cad_forge.cpp;
dipendenze curve/B-spline, Hermite, geometria/trasformazioni e checkBody.

Implementato nello staging:
- Certificazione piano/retta dall'inviluppo affine di linee, coniche, poli NURBS;
  trim e trasformazioni ricorsivi. Supporti globali conservativi per curve rifilate.
- Curve Other prive di certificato geometrico passano al Bishop adattivo esistente.
- Getti di B proiettati sulla normale a T e rinormalizzati, quindi N=B×T.
- Budget geometrico di planarità storico conservato (1e-9*max(1,size)).

Verifiche effettive (object kernel standalone con libreria corrente):
- Compilazione C++17/O2/-ffp-contract=off PASS.
- Riproduttore elica R4/passo1/16giri/r.2: volume
  50.5723586570814 contro atteso50.5723587146619; checkBody0.
  Baseline documentata review:25.3261477033.
- Probe PathFrames 514 parametri: errore massimo ortonormalità
  baseline0.999205 → patch3.33067e-16.
- Runner ricollegato con il solo object sweep staged e test Sweep ricompilati:
  17/17 PASS, inclusa nuova regressione eliche15/16/17giri, ultima posa rigida;
  nuova regressione verifica massa, area e checker.
- Storici PASS includono rette, coniche, percorso planare, NURBS spaziale con
  rette e flesso, percorso spaziale chiuso, lamine e storia.

Limiti: nessuna build completa/app/CTest; nessuna nuova prova UI; nessuna
certificazione K2/K3. Le curve generiche planari percorrono Bishop e possono
costare più del precedente riconoscimento campionato, ma il trasporto mantiene
cache e controllo adattivo esistenti. Nessuna misura globale prestazionale.
La box iniziale usa ancora campioni soltanto per la scala preesistente; non
certifica piano/retta né determina la costruzione del frame.
