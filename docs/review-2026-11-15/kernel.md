# Kernel geometrico — 15 novembre 2026

Sei problemi riprodotti nella review iniziale. **Stato aggiornato:** K1/K3 corretti nell’[intervento sweep](../sweep-review-fix-2026-11-15.md); K4/K5/K6 corretti nella [prosecuzione](../review-fix-next-2026-11-15.md), con74test kernel distinti PASS. **K2 completato e integrato:** [validazione globale, prove e limiti](../sweep-k2-completamento-2026-11-15.md). Le descrizioni e righe sotto conservano i riscontri precedenti alle correzioni.

## K1 — P1 — Aliasing del riconoscimento del percorso: lo sweep a torsione minima deforma il profilo

**Posizione:** `kernel/fk_sweep.cpp:95–114`, ramo del sistema mobile a `:216`.

Il riconoscimento iniziale usa 17 campioni uniformi. In un'elica di esattamente 16 giri i campioni cadono tutti sullo stesso generatore, inducendo la classificazione come percorso rettilineo. Il ramo conseguente mantiene costante la binormale anche dove la tangente cambia: il sistema mobile perde l'ortogonalità.

**Riproduzione:** elica R=4, passo=1, giri=16; cerchio di sezione r=0,2, spire disgiunte. MinimalTwist dà volume 25,3261477033 contro 50,5723587147 atteso. Frenet sullo stesso input dà 50,5723586572. `checkBody` non riporta problemi. L'errore non deriva dall'autointersezione delle spire.

**Proposta:** riconoscimento geometrico/adattivo di rettilineità e planarità che non dipenda da una griglia uniforme finita, con verifica del sistema mobile. Non risolvere semplicemente aumentando da 17 a un altro numero fisso.

Prove: `prove/probe_alias.cpp.txt`, `prove/probe_alias.log`.

## K2 — P1 — Lo sweep accetta un solido con spire sovrapposte

**Posizione:** `kernel/fk_sweep.cpp:719–725`, in particolare il controllo finale a 722.

I controlli sulla curvatura sono locali. Non impediscono che sezioni distanti lungo il percorso si sovrappongano, e il controllo finale restituisce il corpo come valido.

**Riproduzione:** elica R=1, passo 0,1, quattro giri; sezione circolare r=0,5. `checkBody=0`; volume calcolato 19,7417086215. L'intera geometria è contenuta nel box `[-1,5;1,5]² × [-0,5;0,9]`, il cui volume è 12,6. Il risultato conta più volte zone sovrapposte e non rappresenta un solido valido per le operazioni successive.

**Proposta:** verificare le autointersezioni globali della geometria esatta, quindi rifiutare esplicitamente oppure risolvere topologicamente le sovrapposizioni. Il controllo locale del raggio non certifica questa proprietà.

Prove: `prove/probe_overlap.cpp.txt`, `prove/probe_overlap.log`.

## K3 — P1 — Percorso spaziale chiuso in un solo tratto: cucitura UV errata, massa e area nulle

**Posizione:** `kernel/fk_sweep.cpp:681–684`; superficie nonperiodica creata a 659.

**Difetto già registrato nella memoria del progetto**, ora riprodotto e localizzato. Quando il percorso è un unico tratto chiuso, lower/upper usano lo stesso edge e non ricevono SP-curve distinte. La proiezione successiva assegna v=1 a entrambi i lati della cucitura; i bordi longitudinali vanno invece da 0 a 1. I loop nello spazio dei parametri non si chiudono e i contributi degli integrali si cancellano.

**Riproduzione:** spline della fixture `SweepMinimalTwistClosedSpatialSpline`, profilo r=0,1. Un tratto: due facce, volume 0, area 0. Stessa curva divisa a 0,5: quattro facce, volume 0,822445219580, atteso 0,822445218851, area 16,4489043843. La mesh del caso errato è comunque non vuota (44.572 triangoli); `checkBody=0` e nessuna faccia segnalata fallita dalla tessellazione. L'aspetto visivo può quindi nascondere il difetto geometrico. La stampa delle SP-curve conferma direttamente i due usi della cucitura a v=1.

**Proposta:** conservare esplicitamente le due parametrizzazioni v=0/v=1 oppure suddividere in modo generale il percorso chiuso. Aggiungere controlli sulla chiusura UV e sulle proprietà integrali, non soltanto sul risultato visibile.

Prove: `prove/probe_closed.cpp.txt`, `prove/probe_closed.log`, `prove/probe_closed_seams.cpp.txt`, `prove/probe_closed_seams.log`.

## K4 — P2 — Sweep valido rifiutato per la rappresentazione dei poli del cerchio

**Posizione:** `kernel/fk_sweep.cpp:630–633`.

Il controllo `dot(X.d1,T)>0` viene applicato anche ai poli NURBS, che non appartengono necessariamente al profilo reale. Un polo esterno può superare il raggio di curvatura pur con un profilo interamente valido.

**Riproduzione:** cerchio di profilo r=8 su quarto di cerchio R=10. Percorso analitico: successo con entrambi gli orientamenti del frame, volume 3158,27340834859. Stesso percorso convertito esattamente in NURBS: successo con il primo frame; rotazione di 45° del frame del cerchio: errore di sezioni che si incrociano. Il cerchio geometrico non cambia, cambiano solo i poli della sua rappresentazione.

**Proposta:** controllare la geometria effettiva del profilo o un bound certificato che non confonda l'esterno dell'inviluppo dei poli con un'intersezione accertata. Non usare ogni polo come un punto fisico della sezione.

Prove: `prove/probe_sweep.cpp.txt`, `prove/probe_sweep.log`.

## K5 — P2 — Una traslazione cambia la chiusura della sezione di loft

**Posizione:** `kernel/fk_loft.cpp:2055,2059`.

La tolleranza di chiusura è `1e-6 * max(1,norm(primoPunto))`: dipende dalla distanza dall'origine anziché dalla dimensione della sezione e dalla politica delle tolleranze.

**Riproduzione:** due segmenti paralleli lunghi 0,5, distanti 1. All'origine: due estremi nelle corrispondenze, una faccia, area 0,5. Traslazione X=1.000.000: sezioni interpretate come chiuse, un punto nelle corrispondenze, due facce, area 0,998611. `checkBody=0` in entrambi i casi. La sola posizione cambia silenziosamente geometria e topologia.

**Proposta:** criterio invariante alle traslazioni, basato sulla tolleranza di connessione documentata e sulla dimensione locale quando previsto.

Prove: `prove/probe_misc.cpp.txt`, `prove/probe_misc.log`.

## K6 — P3 — reverseEdge su bordo di lamina muta il body prima dell'eccezione

**Posizione:** `kernel/fk_topology.cpp:125–132`.

Il metodo scambia forward/backward e poi accede a entrambe le fin. Un bordo laminare ne possiede una sola: l'accesso alla fin assente lancia dopo una mutazione parziale.

**Riproduzione:** `makePlaneSheet(Frame3(),1)`, `reverseEdge(firstEdge)`: eccezione `Body: fin non valido`, checker da 0 a 2 errori. I chiamanti di produzione esaminati operano su solidi: difetto dell'API latente, nessun flusso GUI confermato.

**Proposta:** supportare esplicitamente una sola fin o rifiutare prima di modificare il corpo.

Prove: `prove/probe_misc.cpp.txt`, `prove/probe_misc.log`.

## Verifiche, freschezza e copertura

- Tutti i probe sono collegati a `forgecad-release/kernel/libforgekernel.a`, più recente dei sorgenti kernel esaminati. I sorgenti e gli output sono conservati in `prove/`.
- **16/16 test Sweep PASS sul runner aggiornato in /tmp**: ricompilato l'unico object di test obsoleto, `test_sweep.cpp`, poi ricollegato con la libreria corrente. Procedura `prove/relink_current.py.txt`, risultato `prove/current-sweep-tests.log`.
- **475/475 sul runner preesistente del 14/11**: risultato utile come baseline precedente, non certificazione delle modifiche del 15/11. Log conservato come `prove/kernel-tests-baseline-precedente.log`.
- Per ricompilare i probe kernel dopo avere rinominato `.cpp.txt` in `.cpp`: `g++ -std=c++17 -O2 -ffp-contract=off -Ikernel /tmp/probe.cpp forgecad-release/kernel/libforgekernel.a -pthread -o /tmp/probe` dalla radice del repository.
- Lettura mirata di matematica/curve/B-spline, topology/checker, profili, sweep/Hermite, loft, booleane, shell/offset e pcurve, con contratti e percorsi principali. Non è una lettura integrale di ogni riga di blend/offset/booleane. Nessun altro difetto in quelle aree viene dichiarato senza prova. Dettagli/hash: `prove/kernel-coverage.txt`.
